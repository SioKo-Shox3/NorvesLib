#pragma once

#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
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
        Identity InputName;
        RHI::TexturePtr InputTexture;
        RHI::TexturePtr BackBufferTexture;
        RHI::RenderPassPtr RenderPass;
        RHI::FramebufferPtr Framebuffer;
        RHI::Viewport Viewport;
        RHI::ScissorRect Scissor;
        RHI::AttachmentLoadOp LoadOp = RHI::AttachmentLoadOp::Clear;
    };

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

        bool WasHandled() const
        {
            return WasPresented();
        }

        const PresentationPassResult& GetLastResult() const
        {
            return m_Result;
        }

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        PresentationPassRequest m_Request;
        PresentationPassResult m_Result;
        RGTextureHandle m_InputHandle;
        RGResourceHandle m_BackBufferHandle;
        Identity m_InputName;
    };

} // namespace NorvesLib::Core::Rendering
