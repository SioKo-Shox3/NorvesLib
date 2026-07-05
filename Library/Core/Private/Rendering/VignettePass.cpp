#include "Rendering/VignettePass.h"
#include "Rendering/VignettePassGpuTypes.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    static constexpr uint32_t VIGNETTE_PARAMS_SIZE = sizeof(GPUVignetteParams);

    VignettePass::VignettePass(const VignetteSettings& settings)
        : m_Settings(settings)
    {
    }

    VignettePass::~VignettePass()
    {
        if (m_bInitialized)
        {
            Shutdown();
        }
    }

    bool VignettePass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("VignettePass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("VignettePass", "ShaderManager is null");
            return false;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_VertexShader)
        {
            NORVES_LOG_ERROR("VignettePass", "Failed to load fullscreen vertex shader");
            return false;
        }

        m_FragmentShader = context.ShaderMgr->LoadShader("vignette.frag", RHI::ShaderStage::Pixel);
        if (!m_FragmentShader)
        {
            NORVES_LOG_ERROR("VignettePass", "Failed to load vignette fragment shader");
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
            NORVES_LOG_ERROR("VignettePass", "Failed to create sampler");
            return false;
        }

        RHI::BufferDesc paramsUboDesc(
            VIGNETTE_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "VignetteParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("VignettePass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("VignettePass", "VignettePass initialized");
        return true;
    }

    void VignettePass::Shutdown()
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
        m_InputToneMappedHandle = {};
        m_OutputTextureHandle = {};
        m_OutputHandle = {};
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferOutputTexture = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("VignettePass", "VignettePass shutdown");
    }

    void VignettePass::Setup(ViewRenderContext& context)
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
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "VignetteOutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("VignettePass", "Failed to create output texture");
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
                NORVES_LOG_INFO("VignettePass", "Resources created");
            }
        }
    }

    bool VignettePass::PrepareResources(uint32_t width,
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
            NORVES_LOG_ERROR("VignettePass", "Failed to create render pass");
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
            NORVES_LOG_ERROR("VignettePass", "Failed to create framebuffer");
            return false;
        }

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
            NORVES_LOG_ERROR("VignettePass", "Failed to create descriptor set");
            return false;
        }

        m_DescriptorSet->BindConstantBuffer(1, m_ParamsBuffer, 0, VIGNETTE_PARAMS_SIZE);

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
            NORVES_LOG_ERROR("VignettePass", "Failed to create pipeline");
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_FramebufferOutputTexture = outputTexture.get();
        m_bRenderPassUsesRenderGraphInitialState = bUseRenderGraphInitialState;
        return true;
    }

    void VignettePass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline)
        {
            NORVES_LOG_WARNING("VignettePass", "Resources not ready, skipping");
            return;
        }

        RHI::TexturePtr inputTexture;
        if (context.SharedResources)
        {
            inputTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
        }

        if (!inputTexture)
        {
            NORVES_LOG_WARNING("VignettePass", "ToneMappedColor not available, skipping");
            return;
        }

        ExecuteWithInput(context, inputTexture, true);
    }

    void VignettePass::Declare(RenderGraphBuilder& builder)
    {
        m_bLegacyInputFallbackActive = false;
        const ViewRenderContext* context = builder.GetContext();
        const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
        const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

        m_InputToneMappedHandle = {};

        RGTextureHandle inputHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::ToneMappedColor,
                                   inputHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputToneMappedHandle = inputHandle.ToResourceHandle();
        }
        else if (m_InputPass)
        {
            const RGResourceHandle toneMappedHandle = m_InputPass->GetToneMappedColorHandle();
            if (toneMappedHandle.IsValid())
            {
                builder.Read(toneMappedHandle, RHI::ResourceState::ShaderResource);
                m_InputToneMappedHandle = toneMappedHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle outputHandle = builder.WriteTexture(
            RenderGraphResourceNames::ToneMappedColor,
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "VignetteOutput"),
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        m_OutputTextureHandle = outputHandle;
        m_OutputHandle = outputHandle.ToResourceHandle();
        builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor, outputHandle);
        builder.PreserveInsertionOrder();
    }

    void VignettePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("VignettePass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr outputTexture = resources.GetTexture(m_OutputHandle);
        if (!outputTexture)
        {
            NORVES_LOG_ERROR("VignettePass", "Failed to resolve native Vignette output texture");
            return;
        }

        if (!PrepareResources(outputTexture->GetWidth(), outputTexture->GetHeight(), outputTexture, true))
        {
            return;
        }

        RHI::TexturePtr inputTexture;
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

        bool bUsedSharedResourceFallback = false;
        if (!inputTexture && context.SharedResources)
        {
            inputTexture = context.SharedResources->GetTexturePtr("ToneMappedColor");
            bUsedSharedResourceFallback = inputTexture != nullptr;
        }

        if (!inputTexture)
        {
            EnqueueEmptyNativePass(context);
            return;
        }

        ExecuteWithInput(context,
                         inputTexture,
                         m_bLegacyInputFallbackActive || bUsedSharedResourceFallback);
    }

    void VignettePass::ExecuteWithInput(ViewRenderContext& context,
                                        const RHI::TexturePtr& inputTexture,
                                        bool bRegisterLegacyBridge)
    {
        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline || !m_DescriptorSet)
        {
            NORVES_LOG_WARNING("VignettePass", "Resources not ready, skipping");
            return;
        }

        GPUVignetteParams params = {};
        params.intensity = m_Settings.Intensity;
        params.radius = m_Settings.Radius;
        params.softness = m_Settings.Softness;
        const bool bDebugPostProcessBypass =
            IsDebugPostProcessBypassMode(context.GetActiveDebugMode());
        params.bEnabled = bDebugPostProcessBypass ? 0u : (m_Settings.bEnabled ? 1u : 0u);
        m_ParamsBuffer->Update(&params, sizeof(GPUVignetteParams));

        if (bRegisterLegacyBridge && context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("ToneMappedColor", m_OutputTexture);
        }

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

    bool VignettePass::EnqueueEmptyNativePass(ViewRenderContext& context) const
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
