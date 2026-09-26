#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    struct PresentationPassRequest
    {
        RHI::TexturePtr BackBufferTexture;

        RHI::RenderPassPtr ClearRenderPass;
        RHI::RenderPassPtr LoadRenderPass;
        RHI::FramebufferPtr ClearFramebuffer;
        RHI::FramebufferPtr LoadFramebuffer;
        RHI::PipelinePtr BlitPipeline;
        RHI::DescriptorSetPtr BlitDescriptorSet;
        RHI::SamplerPtr BlitSampler;

        bool bClearPresentation = true;
    };

    struct PresentationPassResult
    {
        bool bPresented = false;
        bool bBlitRecorded = false;
        Identity InputName;
        RHI::TexturePtr InputTexture;
        RHI::TexturePtr BackBufferTexture;
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
        RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::Clear;
    };

    /**
     * Primary RenderGraph path for presenting a rendered view to the swapchain.
     */
    class PresentationPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "PresentationPass";
        }

        void SetRequest(const PresentationPassRequest& request);
        void ResetResult();

        bool WasPresented() const
        {
            return m_Result.bPresented;
        }

        /**
         * True when graph presentation handled the frame and legacy fallback is unnecessary.
         */
        bool WasHandled() const
        {
            return WasPresented();
        }

        const PresentationPassResult& GetLastResult() const
        {
            return m_Result;
        }

        void SetDeferBlit(bool bDefer)
        {
            m_bDeferBlit = bDefer;
        }

        bool WasBlitRecorded() const
        {
            return m_Result.bBlitRecorded;
        }

        bool RecordDeferredBlit(ViewRenderContext& context);

        RHI::FramebufferPtr AcquireOverlayFramebuffer(const RHI::DevicePtr& device,
                                                       uint32_t frameSlotIndex,
                                                       uint32_t frameSlotCount,
                                                       const RHI::TexturePtr& targetTexture);

        RHI::RenderPassPtr GetOverlayRenderPass() const
        {
            return m_OverlayRenderPass;
        }

        void InvalidateOverlayResources();

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        struct OverlayFramebufferEntry
        {
            RHI::TexturePtr Texture;
            RHI::FramebufferPtr Framebuffer;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::Format Format = RHI::Format::UNKNOWN;
        };

        PresentationPassRequest m_Request;
        PresentationPassResult m_Result;
        RGTextureHandle m_InputHandle;
        RGResourceHandle m_BackBufferHandle;
        Identity m_InputName;
        bool m_bDeferBlit = false;
        RHI::DevicePtr m_OverlayDevice;
        RHI::RenderPassPtr m_OverlayRenderPass;
        Container::VariableArray<OverlayFramebufferEntry> m_OverlayFramebufferRing;
    };

} // namespace NorvesLib::Core::Rendering
