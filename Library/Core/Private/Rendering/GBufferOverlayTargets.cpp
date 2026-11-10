#include "Rendering/GBufferOverlayTargets.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    bool GBufferOverlayTargets::Ensure(RHI::IDevice* device, const RHI::TexturePtr (&colorTargets)[kColorTargetCount],
                                       const RHI::TexturePtr& depthTarget, const GBufferOverlayPipelineDesc& desc)
    {
        // render pass とパイプラインは描き先の形式だけで決まる。framebuffer は描き先のテクスチャが替わったら作り直す
        bool bSameFormats = m_RenderPass && m_Pipeline && m_TargetFormats[kColorTargetCount] == depthTarget->GetFormat();
        for (uint32_t index = 0; index < kColorTargetCount && bSameFormats; ++index)
        {
            bSameFormats = m_TargetFormats[index] == colorTargets[index]->GetFormat();
        }
        bool bSameTargets = bSameFormats && m_Framebuffer && m_FramebufferTargets[kColorTargetCount] == depthTarget.get();
        for (uint32_t index = 0; index < kColorTargetCount && bSameTargets; ++index)
        {
            bSameTargets = m_FramebufferTargets[index] == colorTargets[index].get();
        }
        if (bSameTargets)
        {
            return true;
        }
        m_Framebuffer.reset();
        for (const RHI::ITexture*& target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        if (!bSameFormats && !CreatePipeline(device, colorTargets, depthTarget, desc))
        {
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_RenderPass;
        for (const RHI::TexturePtr& target : colorTargets)
        {
            framebufferDesc.colorTargets.push_back(target);
        }
        framebufferDesc.depthStencilTarget = depthTarget;
        framebufferDesc.width = depthTarget->GetWidth();
        framebufferDesc.height = depthTarget->GetHeight();
        m_Framebuffer = device->CreateFramebuffer(framebufferDesc);
        if (!m_Framebuffer)
        {
            NORVES_LOG_ERROR(desc.debugName, "%s_INIT_FAILED framebuffer を作れません", desc.failureLogPrefix);
            return false;
        }
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_FramebufferTargets[index] = colorTargets[index].get();
        }
        m_FramebufferTargets[kColorTargetCount] = depthTarget.get();
        return true;
    }

    bool GBufferOverlayTargets::CreatePipeline(RHI::IDevice* device, const RHI::TexturePtr (&colorTargets)[kColorTargetCount],
                                               const RHI::TexturePtr& depthTarget, const GBufferOverlayPipelineDesc& desc)
    {
        m_RenderPass.reset();
        m_Pipeline.reset();
        for (RHI::Format& format : m_TargetFormats)
        {
            format = RHI::Format::UNKNOWN;
        }

        RHI::RenderPassDesc renderPassDesc;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = colorTargets[index]->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
        }
        renderPassDesc.hasDepthStencil = true;
        renderPassDesc.depthStencilAttachment.format = depthTarget->GetFormat();
        renderPassDesc.depthStencilAttachment.isDepthStencil = true;
        renderPassDesc.depthStencilAttachment.clear = false;
        renderPassDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        renderPassDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        renderPassDesc.depthStencilAttachment.initialState = RHI::ResourceState::DepthWrite;
        renderPassDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;
        m_RenderPass = device->CreateRenderPass(renderPassDesc);
        if (!m_RenderPass)
        {
            NORVES_LOG_ERROR(desc.debugName, "%s_INIT_FAILED render pass を作れません", desc.failureLogPrefix);
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = desc.vertexShader;
        pipelineDesc.pixelShader = desc.pixelShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        pipelineDesc.vertexBindings = desc.vertexBindings;
        pipelineDesc.vertexAttributes = desc.vertexAttributes;

        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = desc.cullMode;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = desc.depthCompareOp;
        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }
        pipelineDesc.renderPass = m_RenderPass;
        pipelineDesc.descriptorSetLayouts = desc.descriptorSetLayouts;
        m_Pipeline = device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_ERROR(desc.debugName, "%s_INIT_FAILED パイプラインを作れません", desc.failureLogPrefix);
            m_RenderPass.reset();
            return false;
        }

        for (uint32_t index = 0; index < kColorTargetCount; ++index)
        {
            m_TargetFormats[index] = colorTargets[index]->GetFormat();
        }
        m_TargetFormats[kColorTargetCount] = depthTarget->GetFormat();
        return true;
    }

    void GBufferOverlayTargets::Reset()
    {
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        for (const RHI::ITexture*& target : m_FramebufferTargets)
        {
            target = nullptr;
        }
        for (RHI::Format& format : m_TargetFormats)
        {
            format = RHI::Format::UNKNOWN;
        }
    }

} // namespace NorvesLib::Core::Rendering
