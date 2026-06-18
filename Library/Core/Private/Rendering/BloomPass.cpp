#include "Rendering/BloomPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SSRPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
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
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("BloomPass", "BloomPass shutdown");
    }

    void BloomPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0 || !m_Device)
        {
            return;
        }

        const bool bNeedsOutputTexture =
            !m_OutputTexture ||
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsOutputTexture)
        {
            // HDR出力テクスチャ作成（ToneMappingの前なのでHDR維持）
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "BloomOutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to create output texture");
                return;
            }
        }

        const bool bNeedsPrepare =
            bNeedsOutputTexture ||
            !m_BloomRenderPass ||
            !m_BloomFramebuffer ||
            !m_BloomPipeline ||
            !m_BloomDescriptorSet ||
            m_FramebufferOutputTexture != m_OutputTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsPrepare)
        {
            if (PrepareResources(width, height, m_OutputTexture, false))
            {
                NORVES_LOG_INFO("BloomPass", "Bloom resources resized (%ux%u)", width, height);
            }
        }
    }

    bool BloomPass::PrepareResources(uint32_t width,
                                     uint32_t height,
                                     const RHI::TexturePtr& outputTexture,
                                     bool bUseRenderGraphInitialState)
    {
        if (!m_Device ||
            !outputTexture ||
            !m_BloomVertexShader ||
            !m_BloomFragmentShader ||
            !m_ParamsBuffer)
        {
            return false;
        }

        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            outputTexture.get() != m_FramebufferOutputTexture ||
            bUseRenderGraphInitialState != m_bRenderPassUsesRenderGraphInitialState ||
            !m_BloomRenderPass ||
            !m_BloomFramebuffer ||
            !m_BloomPipeline ||
            !m_BloomDescriptorSet;

        m_OutputTexture = outputTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_BloomRenderPass.reset();
        m_BloomFramebuffer.reset();
        m_BloomPipeline.reset();
        m_BloomDescriptorSet.reset();

        // ========================================
        // レンダーパス作成（1カラー、デプスなし）
        // ========================================
        RHI::RenderPassDesc rpDesc;

        RHI::AttachmentDesc colorAttach;
        colorAttach.format = outputTexture->GetFormat();
        colorAttach.isDepthStencil = false;
        colorAttach.clear = false;
        colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
        colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttach.initialState = bUseRenderGraphInitialState
                                       ? RHI::ResourceState::RenderTarget
                                       : RHI::ResourceState::Undefined;
        colorAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(colorAttach);

        rpDesc.hasDepthStencil = false;

        m_BloomRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_BloomRenderPass)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom render pass");
            return false;
        }

        // ========================================
        // フレームバッファ作成
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_BloomRenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_BloomFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_BloomFramebuffer)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to create bloom framebuffer");
            return false;
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
            return false;
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
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
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

        ExecuteWithInput(context, sceneColorPtr);
    }

    void BloomPass::Declare(RenderGraphBuilder &builder)
    {
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        m_InputSceneColorHandle = {};

        RGTextureHandle sceneColorHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SSRSceneColor,
                                   sceneColorHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::SceneColor,
                                        sceneColorHandle,
                                        RHI::ResourceState::ShaderResource))
        {
            m_InputSceneColorHandle = sceneColorHandle.ToResourceHandle();
        }
        else if (m_InputPass)
        {
            const RGResourceHandle fallbackSceneColorHandle = m_InputPass->GetSceneColorHandle();
            if (fallbackSceneColorHandle.IsValid())
            {
                builder.Read(fallbackSceneColorHandle, RHI::ResourceState::ShaderResource);
                m_InputSceneColorHandle = fallbackSceneColorHandle;
            }
        }

        RGTextureHandle outputHandle = builder.WriteTexture(
            RenderGraphResourceNames::BloomSceneColor,
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "BloomOutput"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        m_OutputHandle = outputHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void BloomPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("BloomPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("BloomPass", "Failed to resolve native bloom output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        RHI::TexturePtr sceneColorPtr;
        if (m_InputSceneColorHandle.IsValid())
        {
            sceneColorPtr = resources.GetTexture(m_InputSceneColorHandle);
        }
        if (m_InputPass)
        {
            const RGResourceHandle sceneColorHandle = m_InputPass->GetSceneColorHandle();
            if (!sceneColorPtr && sceneColorHandle.IsValid())
            {
                sceneColorPtr = resources.GetTexture(sceneColorHandle);
            }
        }

        if (!sceneColorPtr && context.SharedResources)
        {
            sceneColorPtr = context.SharedResources->GetTexturePtr("SceneColor");
        }

        if (!sceneColorPtr)
        {
            EnqueueEmptyNativePass(context);
            return;
        }

        ExecuteWithInput(context, sceneColorPtr);
    }

    void BloomPass::ExecuteWithInput(ViewRenderContext &context, const RHI::TexturePtr& sceneColorPtr)
    {
        if (!m_BloomRenderPass || !m_BloomFramebuffer || !m_BloomPipeline || !m_BloomDescriptorSet)
        {
            NORVES_LOG_WARNING("BloomPass", "Bloom resources not ready, skipping");
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

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

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

    bool BloomPass::EnqueueEmptyNativePass(ViewRenderContext &context) const
    {
        if (!m_BloomRenderPass || !m_BloomFramebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_BloomRenderPass,
                                      m_BloomFramebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      nullptr,
                                      nullptr);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
