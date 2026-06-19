#include "Rendering/UpscalePass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Logging/LogMacros.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"

namespace NorvesLib::Core::Rendering
{
    UpscalePass::UpscalePass(const UpscaleSettings &settings)
        : m_Settings(settings)
    {
    }

    UpscalePass::~UpscalePass()
    {
        Shutdown();
    }

    bool UpscalePass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("UpscalePass", "Required rendering context is missing");
            return false;
        }

        m_Device = context.Device;

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("upscale.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to load upscale shaders");
            return false;
        }

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
            NORVES_LOG_ERROR("UpscalePass", "Failed to create sampler");
            return false;
        }

        m_bInitialized = true;
        return true;
    }

    void UpscalePass::Shutdown()
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
        m_DescriptorSet.reset();
        m_LinearSampler.reset();
        m_Device = nullptr;
        m_CurrentWidth = 0;
        m_CurrentHeight = 0;
        m_InputToneMappedHandle = {};
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;
        m_bInitialized = false;
    }

    void UpscalePass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;
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
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "PresentationColor"));
            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("UpscalePass", "Failed to create output texture");
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
            PrepareResources(width, height, m_OutputTexture, false);
        }
    }

    bool UpscalePass::PrepareResources(uint32_t width,
                                       uint32_t height,
                                       const RHI::TexturePtr& outputTexture,
                                       bool bUseRenderGraphInitialState)
    {
        if (!m_Device || !outputTexture || !m_VertexShader || !m_FragmentShader)
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
            NORVES_LOG_ERROR("UpscalePass", "Failed to create render pass");
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_RenderPass;
        fbDesc.colorTargets.push_back(outputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_Framebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to create framebuffer");
            return false;
        }

        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding inputBinding;
        inputBinding.binding = 0;
        inputBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        inputBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(inputBinding);

        m_DescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!m_DescriptorSet)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to create descriptor set");
            return false;
        }

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
            NORVES_LOG_ERROR("UpscalePass", "Failed to create pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
    }

    bool UpscalePass::NeedsUpscale(uint32_t renderWidth,
                                   uint32_t renderHeight,
                                   uint32_t screenWidth,
                                   uint32_t screenHeight)
    {
        return renderWidth != screenWidth || renderHeight != screenHeight;
    }

    void UpscalePass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList || !context.SharedResources)
        {
            return;
        }

        RHI::TexturePtr inputTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
        if (!inputTexture)
        {
            NORVES_LOG_WARNING("UpscalePass", "ToneMappedColor not available, skipping");
            return;
        }

        if (context.RenderWidth == context.ScreenWidth && context.RenderHeight == context.ScreenHeight)
        {
            context.SharedResources->RegisterTexturePtr("PresentationColor", inputTexture);
            return;
        }

        ExecuteWithInput(context, inputTexture, true);
    }

    void UpscalePass::Declare(RenderGraphBuilder &builder)
    {
        m_bLegacyInputFallbackActive = false;
        const ViewRenderContext *context = builder.GetContext();
        const uint32_t renderWidth = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t renderHeight = context ? context->GetActiveRenderHeight() : 1u;
        const uint32_t screenWidth = context && context->ScreenWidth > 0 ? context->ScreenWidth : renderWidth;
        const uint32_t screenHeight = context && context->ScreenHeight > 0 ? context->ScreenHeight : renderHeight;

        m_InputToneMappedHandle = {};
        m_OutputHandle = {};

        RGTextureHandle inputTextureHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::ToneMappedColor,
                                   inputTextureHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputToneMappedHandle = inputTextureHandle.ToResourceHandle();
        }
        else if (m_InputPass)
        {
            const RGResourceHandle inputHandle = m_InputPass->GetToneMappedColorHandle();
            if (inputHandle.IsValid())
            {
                builder.Read(inputHandle, RHI::ResourceState::ShaderResource);
                m_InputToneMappedHandle = inputHandle;
                inputTextureHandle = m_InputPass->GetToneMappedColorTextureHandle();
                m_bLegacyInputFallbackActive = true;
            }
        }

        if (!NeedsUpscale(renderWidth, renderHeight, screenWidth, screenHeight) &&
            m_InputToneMappedHandle.IsValid())
        {
            m_OutputHandle = m_InputToneMappedHandle;
            if (inputTextureHandle.IsValid())
            {
                builder.PublishTexture(RenderGraphResourceNames::PresentationColor, inputTextureHandle);
                builder.ExportTexture(RenderGraphResourceNames::PresentationColor, inputTextureHandle);
            }
            else
            {
                NORVES_LOG_WARNING("UpscalePass", "PresentationColor export skipped because input texture handle is invalid");
            }
            builder.PreserveInsertionOrder();
            return;
        }

        RGTextureHandle outputHandle = builder.WriteTexture(
            RenderGraphResourceNames::PresentationColor,
            RGTextureDesc::RenderTarget(screenWidth, screenHeight, m_Settings.OutputFormat, "PresentationColor"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        m_OutputHandle = outputHandle.ToResourceHandle();
        builder.ExportTexture(RenderGraphResourceNames::PresentationColor, outputHandle);
        builder.PreserveInsertionOrder();
    }

    void UpscalePass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("UpscalePass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr inputTexture;
        bool bUsedSharedResourceFallback = false;
        if (m_InputToneMappedHandle.IsValid())
        {
            inputTexture = resources.GetTexture(m_InputToneMappedHandle);
        }

        if (!inputTexture && m_InputPass)
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
            bUsedSharedResourceFallback = inputTexture != nullptr;
        }

        if (!inputTexture)
        {
            if (m_OutputHandle.IsValid() && resources.GetTexture(m_OutputHandle))
            {
                EnqueueEmptyNativePass(context);
            }
            return;
        }

        const uint32_t renderWidth = context.GetActiveRenderWidth();
        const uint32_t renderHeight = context.GetActiveRenderHeight();
        const uint32_t screenWidth = context.ScreenWidth > 0 ? context.ScreenWidth : renderWidth;
        const uint32_t screenHeight = context.ScreenHeight > 0 ? context.ScreenHeight : renderHeight;

        const bool bRegisterLegacyBridge =
            m_bLegacyInputFallbackActive || bUsedSharedResourceFallback;

        if (!NeedsUpscale(renderWidth, renderHeight, screenWidth, screenHeight))
        {
            if (bRegisterLegacyBridge && context.SharedResources)
            {
                context.SharedResources->RegisterTexturePtr("PresentationColor", inputTexture);
            }
            return;
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to resolve native presentation output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        ExecuteWithInput(context, inputTexture, bRegisterLegacyBridge);
    }

    void UpscalePass::ExecuteWithInput(ViewRenderContext &context,
                                       const RHI::TexturePtr& inputTexture,
                                       bool bRegisterLegacyBridge)
    {
        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline || !m_DescriptorSet)
        {
            NORVES_LOG_WARNING("UpscalePass", "Upscale resources not ready, skipping");
            return;
        }

        m_DescriptorSet->BindTexture(0, inputTexture);
        m_DescriptorSet->BindSampler(0, m_LinearSampler);
        m_DescriptorSet->Update();

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

        context.EnqueueFullscreenPass(m_RenderPass,
                                      m_Framebuffer,
                                      viewport,
                                      scissor,
                                      m_Pipeline,
                                      m_DescriptorSet);

        if (bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("PresentationColor", m_OutputTexture);
        }
    }

    bool UpscalePass::EnqueueEmptyNativePass(ViewRenderContext &context) const
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
