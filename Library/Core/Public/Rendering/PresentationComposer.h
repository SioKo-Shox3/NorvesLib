#pragma once

#include "RHI/RHITypes.h"

namespace NorvesLib::RHI
{
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;
    class SceneRenderer;

    struct PresentationComposeRequest
    {
        ViewRenderContext *Context = nullptr;
        SceneRenderer *Renderer = nullptr;
        RHI::ICommandList *CommandList = nullptr;

        RHI::RenderPassPtr ClearRenderPass;
        RHI::RenderPassPtr LoadRenderPass;
        RHI::FramebufferPtr ClearFramebuffer;
        RHI::FramebufferPtr LoadFramebuffer;
        RHI::PipelinePtr BlitPipeline;
        RHI::DescriptorSetPtr BlitDescriptorSet;
        RHI::SamplerPtr BlitSampler;

        bool bClearPresentation = true;
    };

    /**
     * Legacy presentation fallback helper.
     *
     * The normal swapchain presentation path is PresentationPass inside
     * RenderGraph. This helper is kept for cases where the graph presentation
     * pass cannot handle the current frame and the older compositor path must
     * present a fallback texture safely.
     */
    class PresentationComposer
    {
    public:
        bool Compose(const PresentationComposeRequest &request) const;

    private:
        static RHI::TexturePtr ResolvePresentationTexture(const ViewRenderContext &context);
    };

} // namespace NorvesLib::Core::Rendering
