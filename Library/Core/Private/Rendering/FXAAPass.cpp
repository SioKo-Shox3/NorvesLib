#include "Rendering/FXAAPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    // UBO size: vec4 texelSize + float edge + float edgeMin + float subpixel + uint enabled = 32 bytes
    static constexpr uint32_t FXAA_PARAMS_SIZE = 32;

    struct GPUFXAAParams
    {
        float texelSize[4]; // xy = 1/resolution, zw = resolution
        float edgeThreshold;
        float edgeThresholdMin;
        float subpixelQuality;
        uint32_t bEnabled;
    };

    // ========================================
    // Constructor / Destructor
    // ========================================

    FXAAPass::FXAAPass(const FXAASettings &settings)
        : m_Settings(settings)
    {
    }

    FXAAPass::~FXAAPass()
    {
        if (m_bInitialized)
        {
            Shutdown();
        }
    }

    // ========================================
    // Initialize
    // ========================================

    bool FXAAPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("FXAAPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // シェーダーロード
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("FXAAPass", "ShaderManager is null");
            return false;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_VertexShader)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to load fullscreen vertex shader");
            return false;
        }

        m_FragmentShader = context.ShaderMgr->LoadShader("fxaa.frag", RHI::ShaderStage::Pixel);
        if (!m_FragmentShader)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to load FXAA fragment shader");
            return false;
        }

        // リニアサンプラー作成
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Linear;
        samplerDesc.filterMag = RHI::FilterMode::Linear;
        samplerDesc.filterMip = RHI::FilterMode::Linear;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_LinearSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_LinearSampler)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create sampler");
            return false;
        }

        // パラメータUBO作成
        RHI::BufferDesc paramsUboDesc(
            FXAA_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "FXAAParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("FXAAPass", "FXAAPass initialized");
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void FXAAPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ParamsBuffer.reset();
        m_DescriptorSet.reset();
        m_LinearSampler.reset();
        m_Device = nullptr;
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("FXAAPass", "FXAAPass shutdown");
    }

    // ========================================
    // Setup
    // ========================================

    void FXAAPass::Setup(ViewRenderContext &context)
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
            // 出力テクスチャ作成
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "FXAAOutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("FXAAPass", "Failed to create output texture");
                return;
            }
        }

        const bool bNeedsPrepare =
            bNeedsOutputTexture ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            !m_DescriptorSet ||
            m_FramebufferOutputTexture != m_OutputTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState;

        if (bNeedsPrepare)
        {
            if (PrepareResources(width, height, m_OutputTexture, false))
            {
                NORVES_LOG_INFO("FXAAPass", "Resources created");
            }
        }
    }

    bool FXAAPass::PrepareResources(uint32_t width,
                                    uint32_t height,
                                    const RHI::TexturePtr& outputTexture,
                                    bool bUseRenderGraphInitialState)
    {
        if (!m_Device ||
            !outputTexture ||
            !m_VertexShader ||
            !m_FragmentShader ||
            !m_ParamsBuffer)
        {
            return false;
        }

        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            outputTexture.get() != m_FramebufferOutputTexture ||
            bUseRenderGraphInitialState != m_bRenderPassUsesRenderGraphInitialState ||
            !m_RenderPass ||
            !m_Framebuffer ||
            !m_Pipeline ||
            !m_DescriptorSet;

        m_OutputTexture = outputTexture;
        if (!bResourcesChanged)
        {
            return true;
        }

        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_DescriptorSet.reset();

        // レンダーパス作成
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

        m_RenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create render pass");
            return false;
        }

        // フレームバッファ作成
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_RenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_Framebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create framebuffer");
            return false;
        }

        // ディスクリプタセット作成
        // binding 0: 入力テクスチャ (CombinedImageSampler)
        // binding 1: FXAAParams UBO
        RHI::DescriptorSetDesc dsDesc;

        RHI::DescriptorBinding inputBinding;
        inputBinding.binding = 0;
        inputBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        inputBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(inputBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 1;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(paramsBinding);

        m_DescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_DescriptorSet)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create descriptor set");
            return false;
        }

        // UBOバインド
        m_DescriptorSet->BindConstantBuffer(1, m_ParamsBuffer, 0, FXAA_PARAMS_SIZE);

        // パイプライン作成
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to create pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
    }

    // ========================================
    // Execute
    // ========================================

    void FXAAPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline)
        {
            NORVES_LOG_WARNING("FXAAPass", "Resources not ready, skipping");
            return;
        }

        // ToneMappedColorを入力として取得
        RHI::TexturePtr inputTexture;
        if (context.SharedResources)
        {
            inputTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
        }

        if (!inputTexture)
        {
            NORVES_LOG_WARNING("FXAAPass", "ToneMappedColor not available, skipping");
            return;
        }

        ExecuteWithInput(context, inputTexture);
    }

    void FXAAPass::Declare(RenderGraphBuilder &builder)
    {
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        if (m_InputPass)
        {
            const RGResourceHandle toneMappedHandle = m_InputPass->GetToneMappedColorHandle();
            if (toneMappedHandle.IsValid())
            {
                builder.Read(toneMappedHandle, RHI::ResourceState::ShaderResource);
            }
        }

        m_OutputHandle = builder.CreateTexture(
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "FXAAOutput"));
        builder.Write(m_OutputHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
        builder.PreserveInsertionOrder();
    }

    void FXAAPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("FXAAPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("FXAAPass", "Failed to resolve native FXAA output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        RHI::TexturePtr inputTexture;
        if (m_InputPass)
        {
            const RGResourceHandle toneMappedHandle = m_InputPass->GetToneMappedColorHandle();
            if (toneMappedHandle.IsValid())
            {
                inputTexture = resources.GetTexture(toneMappedHandle);
            }
        }

        if (!inputTexture && context.SharedResources)
        {
            inputTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
        }

        if (!inputTexture)
        {
            EnqueueEmptyNativePass(context);
            return;
        }

        ExecuteWithInput(context, inputTexture);
    }

    void FXAAPass::ExecuteWithInput(ViewRenderContext &context, const RHI::TexturePtr& inputTexture)
    {
        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline || !m_DescriptorSet)
        {
            NORVES_LOG_WARNING("FXAAPass", "Resources not ready, skipping");
            return;
        }

        // パラメータ更新
        GPUFXAAParams params = {};
        params.texelSize[0] = 1.0f / static_cast<float>(m_CurrentWidth);
        params.texelSize[1] = 1.0f / static_cast<float>(m_CurrentHeight);
        params.texelSize[2] = static_cast<float>(m_CurrentWidth);
        params.texelSize[3] = static_cast<float>(m_CurrentHeight);
        params.edgeThreshold = m_Settings.EdgeThreshold;
        params.edgeThresholdMin = m_Settings.EdgeThresholdMin;
        params.subpixelQuality = m_Settings.SubpixelQuality;
        params.bEnabled = m_Settings.bEnabled ? 1u : 0u;
        m_ParamsBuffer->Update(&params, sizeof(GPUFXAAParams));

        // FXAA結果をSharedResourceRegistryに登録（ToneMappedColorを上書き）
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ToneMappedColor", m_OutputTexture);
        }

        // テクスチャバインド
        m_DescriptorSet->BindTexture(0, inputTexture);
        m_DescriptorSet->BindSampler(0, m_LinearSampler);
        m_DescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      viewport,
                                      scissor,
                                      m_Pipeline,
                                      m_DescriptorSet);
    }

    bool FXAAPass::EnqueueEmptyNativePass(ViewRenderContext &context) const
    {
        if (!m_RenderPass || !m_Framebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      nullptr,
                                      nullptr);
        return true;
    }

} // namespace NorvesLib::Core::Rendering
