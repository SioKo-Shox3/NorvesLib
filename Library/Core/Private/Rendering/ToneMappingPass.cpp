#include "Rendering/ToneMappingPass.h"
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

    /** @brief トーンマッピングパラメータUBO（std140アライメント） */
    struct GPUToneMappingParams
    {
        float exposure;
        float gamma;
        uint32_t operatorType; // 0:Reinhard, 1:ACES, 2:Uncharted2, 3:Exposure
        float _pad0;
        // Vignette
        float vignetteIntensity;
        float vignetteRadius;
        float vignetteSoftness;
        float _pad1;
        // Color Grading
        float colorFilter[4]; // rgb + intensity(w)
        float contrast;
        float saturation;
        float brightness;
        float temperature;
    };

    static constexpr uint32_t TONEMAPPING_PARAMS_SIZE = sizeof(GPUToneMappingParams);

    ToneMappingPass::ToneMappingPass(const ToneMappingSettings &settings)
        : m_Settings(settings)
    {
    }

    ToneMappingPass::~ToneMappingPass()
    {
        Shutdown();
    }

    bool ToneMappingPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成（LightingPassとキャッシュ共有）
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "ShaderManager is null");
            return false;
        }

        m_ToneMappingVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_ToneMappingVertexShader)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // トーンマッピングフラグメントシェーダー作成
        // ========================================
        m_ToneMappingFragmentShader = context.ShaderMgr->LoadShader("tonemapping.frag", RHI::ShaderStage::Pixel);
        if (!m_ToneMappingFragmentShader)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping fragment shader");
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
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create scene color sampler");
            return false;
        }

        // ========================================
        // パラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            TONEMAPPING_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "ToneMappingParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("ToneMappingPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("ToneMappingPass", "ToneMappingPass initialized (Operator=%d)",
                        static_cast<int>(m_Settings.Operator));
        return true;
    }

    void ToneMappingPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_ToneMappingRenderPass.reset();
        m_ToneMappingFramebuffer.reset();
        m_ToneMappingPipeline.reset();
        m_ToneMappingVertexShader.reset();
        m_ToneMappingFragmentShader.reset();
        m_ParamsBuffer.reset();
        m_ToneMappingDescriptorSet.reset();
        m_SceneColorSampler.reset();
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("ToneMappingPass", "ToneMappingPass shutdown");
    }

    void ToneMappingPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0)
        {
            return;
        }

        // サイズ変更があればリソースを再作成
        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // LDR出力テクスチャ作成
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "ToneMappedColor"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create output texture");
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

            m_ToneMappingRenderPass = m_Device->CreateRenderPass(rpDesc);
            if (!m_ToneMappingRenderPass)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping render pass");
                return;
            }

            // ========================================
            // フレームバッファ作成
            // ========================================
            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_ToneMappingRenderPass;
            fbDesc.colorTargets.push_back(m_OutputTexture);
            fbDesc.width = width;
            fbDesc.height = height;

            m_ToneMappingFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!m_ToneMappingFramebuffer)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping framebuffer");
                return;
            }

            // ========================================
            // ディスクリプタセット作成
            // ========================================
            // binding 0: SceneColor（combined image sampler）
            // binding 1: ToneMappingParams UBO
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

            m_ToneMappingDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
            if (!m_ToneMappingDescriptorSet)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create descriptor set");
                return;
            }

            // UBOバインド（テクスチャはExecute時にバインド）
            m_ToneMappingDescriptorSet->BindConstantBuffer(1, m_ParamsBuffer, 0, TONEMAPPING_PARAMS_SIZE);

            // ========================================
            // パイプライン作成（フルスクリーン描画）
            // ========================================
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = m_ToneMappingVertexShader;
            pipelineDesc.pixelShader = m_ToneMappingFragmentShader;
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

            // ブレンド無効
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = m_ToneMappingRenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

            m_ToneMappingPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            if (!m_ToneMappingPipeline)
            {
                NORVES_LOG_ERROR("ToneMappingPass", "Failed to create tone mapping pipeline");
                return;
            }

            NORVES_LOG_INFO("ToneMappingPass", "ToneMapping resources resized (%ux%u)", width, height);
        }
    }

    void ToneMappingPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_ToneMappingRenderPass || !m_ToneMappingFramebuffer || !m_ToneMappingPipeline)
        {
            NORVES_LOG_WARNING("ToneMappingPass", "ToneMapping resources not ready, skipping");
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
            NORVES_LOG_WARNING("ToneMappingPass", "SceneColor not available, skipping tone mapping");
            return;
        }

        // パラメータバッファ更新
        GPUToneMappingParams params = {};
        params.exposure = m_Settings.Exposure;
        params.gamma = m_Settings.Gamma;
        params.operatorType = static_cast<uint32_t>(m_Settings.Operator);
        params._pad0 = 0.0f;
        // Vignette
        params.vignetteIntensity = m_Settings.VignetteIntensity;
        params.vignetteRadius = m_Settings.VignetteRadius;
        params.vignetteSoftness = m_Settings.VignetteSoftness;
        params._pad1 = 0.0f;
        // Color Grading
        params.colorFilter[0] = m_Settings.ColorFilter[0];
        params.colorFilter[1] = m_Settings.ColorFilter[1];
        params.colorFilter[2] = m_Settings.ColorFilter[2];
        params.colorFilter[3] = m_Settings.ColorFilterIntensity;
        params.contrast = m_Settings.Contrast;
        params.saturation = m_Settings.Saturation;
        params.brightness = m_Settings.Brightness;
        params.temperature = m_Settings.Temperature;
        m_ParamsBuffer->Update(&params, sizeof(GPUToneMappingParams));

        // トーンマッピング結果をSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ToneMappedColor", m_OutputTexture);
        }

        // SceneColorテクスチャをディスクリプタセットにバインド
        m_ToneMappingDescriptorSet->BindTexture(0, sceneColorPtr);
        m_ToneMappingDescriptorSet->BindSampler(0, m_SceneColorSampler);
        m_ToneMappingDescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_ToneMappingRenderPass,
                                      m_ToneMappingFramebuffer,
                                      viewport,
                                      scissor,
                                      m_ToneMappingPipeline,
                                      m_ToneMappingDescriptorSet);
    }

} // namespace NorvesLib::Core::Rendering
