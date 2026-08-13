#include "Rendering/PresentationPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"

namespace NorvesLib::Core::Rendering
{
    void PresentationPass::SetRequest(const PresentationPassRequest& request)
    {
        m_Request = request;
        ResetResult();
    }

    void PresentationPass::ResetResult()
    {
        m_Result = PresentationPassResult{};
        m_InputHandle = RGTextureHandle{};
        m_BackBufferHandle = RGResourceHandle{};
        m_InputName = Identity{};
    }

    void PresentationPass::Declare(RenderGraphBuilder& builder)
    {
        ResetResult();

        RGTextureHandle inputHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::CompositeColor,
                                   inputHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::CompositeColor;
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::PresentationColor,
                                   inputHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::PresentationColor;
        }
        else if (builder.TryReadTexture(RenderGraphResourceNames::ToneMappedColor,
                                        inputHandle,
                                        RHI::ResourceState::ShaderResource))
        {
            m_InputHandle = inputHandle;
            m_InputName = RenderGraphResourceNames::ToneMappedColor;
        }

        const RHI::RenderPassPtr renderPass = m_Request.bClearPresentation
                                                  ? m_Request.ClearRenderPass
                                                  : m_Request.LoadRenderPass;
        const RHI::FramebufferPtr framebuffer = m_Request.bClearPresentation
                                                    ? m_Request.ClearFramebuffer
                                                    : m_Request.LoadFramebuffer;
        if (!m_InputHandle.IsValid() ||
            !m_Request.BackBufferTexture ||
            !renderPass ||
            !framebuffer ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitDescriptorSet ||
            !m_Request.BlitSampler)
        {
            return;
        }

        const RHI::ResourceState initialState = m_Request.bClearPresentation
                                                   ? RHI::ResourceState::Undefined
                                                   : RHI::ResourceState::Present;
        m_BackBufferHandle = builder.ImportTexture(m_Request.BackBufferTexture,
                                                   initialState,
                                                   "SwapChainBackBuffer");
        if (!m_BackBufferHandle.IsValid())
        {
            return;
        }

        builder.LoadStoreColorAttachment(m_BackBufferHandle,
                                         m_Request.bClearPresentation
                                             ? RHI::AttachmentLoadOp::Clear
                                             : RHI::AttachmentLoadOp::Load,
                                         RHI::AttachmentStoreOp::Store,
                                         RHI::ResourceState::RenderTarget,
                                         RHI::ResourceState::Present);
    }

    void PresentationPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Result = PresentationPassResult{};
        context.bPresentationGraphPassHandled = false;

        if (!m_InputHandle.IsValid() ||
            !m_BackBufferHandle.IsValid() ||
            !m_Request.BackBufferTexture ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitDescriptorSet ||
            !m_Request.BlitSampler)
        {
            return;
        }

        const RHI::RenderPassPtr renderPass = m_Request.bClearPresentation
                                                  ? m_Request.ClearRenderPass
                                                  : m_Request.LoadRenderPass;
        const RHI::FramebufferPtr framebuffer = m_Request.bClearPresentation
                                                    ? m_Request.ClearFramebuffer
                                                    : m_Request.LoadFramebuffer;
        if (!renderPass || !framebuffer)
        {
            return;
        }

        RHI::TexturePtr inputTexture = resources.GetTexture(m_InputHandle);
        RHI::TexturePtr backBuffer = resources.GetTexture(m_BackBufferHandle);
        if (!inputTexture || backBuffer.get() != m_Request.BackBufferTexture.get())
        {
            return;
        }

        const RHI::Viewport viewport = context.GetActiveOutputViewport();
        const RHI::ScissorRect scissor = context.GetActiveOutputScissor();

        m_Request.BlitDescriptorSet->BindTexture(0, inputTexture);
        m_Request.BlitDescriptorSet->BindSampler(0, m_Request.BlitSampler);
        m_Request.BlitDescriptorSet->Update();

        if (!m_bDeferBlit)
        {
            context.EnqueueFullscreenPass(renderPass,
                                          framebuffer,
                                          viewport,
                                          scissor,
                                          m_Request.BlitPipeline,
                                          m_Request.BlitDescriptorSet);
            m_Result.bBlitRecorded = true;
        }

        m_Result.bPresented = true;
        m_Result.InputName = m_InputName;
        m_Result.InputTexture = inputTexture;
        m_Result.BackBufferTexture = backBuffer;
        m_Result.RenderPass = renderPass;
        m_Result.Framebuffer = framebuffer;
        m_Result.Viewport = viewport;
        m_Result.Scissor = scissor;
        m_Result.LoadOp = m_Request.bClearPresentation
                              ? RHI::AttachmentLoadOp::Clear
                              : RHI::AttachmentLoadOp::Load;
        context.bPresentationGraphPassHandled = true;
    }

    bool PresentationPass::RecordDeferredBlit(ViewRenderContext& context)
    {
        if (!m_Result.bPresented ||
            m_Result.bBlitRecorded ||
            !m_Result.RenderPass ||
            !m_Result.Framebuffer ||
            !m_Request.BlitPipeline ||
            !m_Request.BlitDescriptorSet ||
            !m_Request.BlitSampler)
        {
            return m_Result.bBlitRecorded;
        }

        m_Request.BlitDescriptorSet->BindTexture(0, m_Result.InputTexture);
        m_Request.BlitDescriptorSet->BindSampler(0, m_Request.BlitSampler);
        m_Request.BlitDescriptorSet->Update();
        context.EnqueueFullscreenPass(m_Result.RenderPass,
                                      m_Result.Framebuffer,
                                      m_Result.Viewport,
                                      m_Result.Scissor,
                                      m_Request.BlitPipeline,
                                      m_Request.BlitDescriptorSet);
        m_Result.bBlitRecorded = true;
        return true;
    }

    RHI::FramebufferPtr PresentationPass::AcquireOverlayFramebuffer(
        const RHI::DevicePtr& device,
        uint32_t frameSlotIndex,
        uint32_t frameSlotCount,
        const RHI::TexturePtr& targetTexture)
    {
        if (!device || !targetTexture ||
            targetTexture->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
            frameSlotCount == 0 ||
            frameSlotIndex >= frameSlotCount)
        {
            return {};
        }

        if (!m_OverlayDevice)
        {
            m_OverlayDevice = device;
            m_OverlayFramebufferRing.resize(frameSlotCount);
        }
        else if (m_OverlayDevice.get() != device.get() ||
                 m_OverlayFramebufferRing.size() != frameSlotCount)
        {
            return {};
        }

        if (!m_OverlayRenderPass)
        {
            RHI::AttachmentDesc overlayColorAttachment;
            overlayColorAttachment.format = RHI::Format::R16G16B16A16_FLOAT;
            overlayColorAttachment.isDepthStencil = false;
            overlayColorAttachment.clear = false;
            overlayColorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            overlayColorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            overlayColorAttachment.initialState = RHI::ResourceState::ShaderResource;
            overlayColorAttachment.finalState = RHI::ResourceState::ShaderResource;

            RHI::RenderPassDesc overlayRenderPassDesc;
            overlayRenderPassDesc.colorAttachments.push_back(overlayColorAttachment);
            m_OverlayRenderPass = device->CreateRenderPass(overlayRenderPassDesc);
            if (!m_OverlayRenderPass)
            {
                InvalidateOverlayResources();
                return {};
            }
        }

        OverlayFramebufferEntry& entry = m_OverlayFramebufferRing[frameSlotIndex];
        if (entry.Framebuffer &&
            entry.Texture.get() == targetTexture.get() &&
            entry.Width == targetTexture->GetWidth() &&
            entry.Height == targetTexture->GetHeight() &&
            entry.Format == targetTexture->GetFormat())
        {
            return entry.Framebuffer;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = m_OverlayRenderPass;
        framebufferDesc.colorTargets.push_back(targetTexture);
        framebufferDesc.width = targetTexture->GetWidth();
        framebufferDesc.height = targetTexture->GetHeight();
        RHI::FramebufferPtr framebuffer = device->CreateFramebuffer(framebufferDesc);
        if (!framebuffer)
        {
            return {};
        }

        entry.Texture = targetTexture;
        entry.Framebuffer = framebuffer;
        entry.Width = targetTexture->GetWidth();
        entry.Height = targetTexture->GetHeight();
        entry.Format = targetTexture->GetFormat();
        return framebuffer;
    }

    void PresentationPass::InvalidateOverlayResources()
    {
        m_OverlayFramebufferRing.clear();
        m_OverlayRenderPass.reset();
        m_OverlayDevice.reset();
    }

} // namespace NorvesLib::Core::Rendering
