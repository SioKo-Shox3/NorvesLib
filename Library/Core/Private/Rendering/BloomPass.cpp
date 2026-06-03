#include "Rendering/BloomPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // GPU側パラメータ構造体（シェーダーのUBOレイアウトに対応）
    // ========================================

    /** @brief ブルームパラメータUBO（std140アライメント） */
    struct GPUBloomParams
    {
        float threshold;
        float intensity;
        float radius;
        float softKnee;
    };

    static constexpr uint32_t BLOOM_PARAMS_SIZE = sizeof(GPUBloomParams);

    BloomPass::BloomPass(const BloomSettings &settings)
        : m_Settings(settings)
    {
    }

    BloomPass::~BloomPass()
    {
        Shutdown();
    }

    bool BloomPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("BloomPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成（LightingPassとキャッシュ共有）
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("BloomPass", "ShaderManager is null");
            return false;
        }

        m_BloomVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_BloomVertexShader)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // ブルームフラグメントシェーダー作成
        // ========================================
        m_BloomFragmentShader = context.ShaderMgr->LoadShader("bloom.frag", RHI::ShaderStage::Pixel);
        if (!m_BloomFragmentShader)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom fragment shader");
            return false;
        }

        // ========================================
        // SceneColorサンプラー作成（リニアフィルタ）
        // ========================================
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Linear;
        samplerDesc.filterMag = RHI::FilterMode::Linear;
        samplerDesc.filterMip = RHI::FilterMode::Linear;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_SceneColorSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_SceneColorSampler)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create scene color sampler");
            return false;
        }

        // ========================================
        // パラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            BLOOM_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "BloomParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("BloomPass", "BloomPass initialized");
        return true;
    }

    void BloomPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_BloomRenderPass.reset();
        m_BloomFramebuffer.reset();
        m_BloomPipeline.reset();
        m_BloomVertexShader.reset();
        m_BloomFragmentShader.reset();
        m_ParamsBuffer.reset();
        m_BloomDescriptorSet.reset();
        m_SceneColorSampler.reset();
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("BloomPass", "BloomPass shutdown");
    }

    void BloomPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.RenderWidth;
        uint32_t height = context.RenderHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        // サイズ変更があればリソースを再作成
        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // HDR出力テクスチャ作成（ToneMappingの前なのでHDR維持）
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "BloomOutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create output texture");
                return;
            }

            // ========================================
            // レンダーパス作成（1カラー、デプスなし）
            // ========================================
            RHI::RenderPassDesc rpDesc;

            RHI::AttachmentDesc colorAttach;
            colorAttach.format = m_Settings.OutputFormat;
            colorAttach.isDepthStencil = false;
            colorAttach.clear = false;
            colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttach.initialState = RHI::ResourceState::Undefined;
            colorAttach.finalState = RHI::ResourceState::ShaderResource;
            rpDesc.colorAttachments.push_back(colorAttach);

            rpDesc.hasDepthStencil = false;

            m_BloomRenderPass = m_Device->CreateRenderPass(rpDesc);
            if (!m_BloomRenderPass)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom render pass");
                return;
            }

            // ========================================
            // フレームバッファ作成
            // ========================================
            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_BloomRenderPass;
            fbDesc.colorTargets.push_back(m_OutputTexture);
            fbDesc.width = width;
            fbDesc.height = height;

            m_BloomFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!m_BloomFramebuffer)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom framebuffer");
                return;
            }

            // ========================================
            // ディスクリプタセット作成
            // ========================================
            // binding 0: SceneColor（combined image sampler）
            // binding 1: BloomParams UBO
            RHI::DescriptorSetDesc dsDesc;

            RHI::DescriptorBinding sceneColorBinding;
            sceneColorBinding.binding = 0;
            sceneColorBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            sceneColorBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(sceneColorBinding);

            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 1;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(paramsBinding);

            m_BloomDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
            if (!m_BloomDescriptorSet)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create descriptor set");
                return;
            }

            // UBOバインド（テクスチャはExecute時にバインド）
            m_BloomDescriptorSet->BindConstantBuffer(1, m_ParamsBuffer, 0, BLOOM_PARAMS_SIZE);

            // ========================================
            // パイプライン作成（フルスクリーン描画）
            // ========================================
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = m_BloomVertexShader;
            pipelineDesc.pixelShader = m_BloomFragmentShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            // 頂点入力なし

            // ラスタライザ
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;

            // デプステスト無効
            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            // ブレンド無効（シェーダー内で元色+ブルームの合成を行う）
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = m_BloomRenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

            m_BloomPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            if (!m_BloomPipeline)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create bloom pipeline");
                return;
            }

            NORVES_LOG_INFO("BloomPass", "Bloom resources resized (%ux%u)", width, height);
        }
    }

    void BloomPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_BloomRenderPass || !m_BloomFramebuffer || !m_BloomPipeline)
        {
            NORVES_LOG_WARNING("BloomPass", "Bloom resources not ready, skipping");
            return;
        }

        // HDRシーンカラーをSharedResourceRegistryから取得（TexturePtr版）
        RHI::TexturePtr sceneColorPtr;
        if (context.SharedResources)
        {
            sceneColorPtr = context.SharedResources->GetTexturePtr("SceneColor");
        }

        if (!sceneColorPtr)
        {
            NORVES_LOG_WARNING("BloomPass", "SceneColor not available, skipping bloom");
            return;
        }

        // パラメータバッファ更新
        GPUBloomParams params = {};
        params.threshold = m_Settings.Threshold;
        params.intensity = m_Settings.Intensity;
        params.radius = m_Settings.Radius;
        params.softKnee = m_Settings.SoftKnee;
        m_ParamsBuffer->Update(&params, sizeof(GPUBloomParams));

        // SceneColorテクスチャをディスクリプタセットにバインド
        m_BloomDescriptorSet->BindTexture(0, sceneColorPtr);
        m_BloomDescriptorSet->BindSampler(0, m_SceneColorSampler);
        m_BloomDescriptorSet->Update();

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_CurrentWidth);
        viewport.height = static_cast<float>(m_CurrentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_CurrentWidth);
        scissor.bottom = static_cast<int32_t>(m_CurrentHeight);

        context.EnqueueFullscreenPass(m_BloomRenderPass,
                                      m_BloomFramebuffer,
                                      viewport,
                                      scissor,
                                      m_BloomPipeline,
                                      m_BloomDescriptorSet);

        // ブルーム適用済みSceneColorとしてSharedResourceRegistryに上書き登録
        // → 後段のToneMappingPassが "SceneColor" として読み取る
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_OutputTexture);
        }
    }

} // namespace NorvesLib::Core::Rendering
