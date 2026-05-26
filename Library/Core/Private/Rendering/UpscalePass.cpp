#include "Rendering/UpscalePass.h"
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
        m_bInitialized = false;
    }

    void UpscalePass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;
        if (width == 0 || height == 0)
        {
            return;
        }

        if (width == m_CurrentWidth && height == m_CurrentHeight)
        {
            return;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;

        m_OutputTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "PresentationColor"));
        if (!m_OutputTexture)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to create output texture");
            return;
        }

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

        m_RenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to create render pass");
            return;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_RenderPass;
        fbDesc.colorTargets.push_back(m_OutputTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_Framebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR("UpscalePass", "Failed to create framebuffer");
            return;
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
            return;
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
        }
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

        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline || !m_DescriptorSet)
        {
            NORVES_LOG_WARNING("UpscalePass", "Upscale resources not ready, skipping");
            return;
        }

        m_DescriptorSet->BindTexture(0, inputTexture);
        m_DescriptorSet->BindSampler(0, m_LinearSampler);
        m_DescriptorSet->Update();

        context.CommandList->BeginRenderPass(m_RenderPass, m_Framebuffer);

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_CurrentWidth);
        viewport.height = static_cast<float>(m_CurrentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context.CommandList->SetViewport(viewport);

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_CurrentWidth);
        scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
        context.CommandList->SetScissor(scissor);

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(m_DescriptorSet, 0);
        context.CommandList->Draw(3, 0);
        context.CommandList->EndRenderPass();

        context.SharedResources->RegisterTexturePtr("PresentationColor", m_OutputTexture);
    }

} // namespace NorvesLib::Core::Rendering
