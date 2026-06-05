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

    class PresentationComposer
    {
    public:
        bool Compose(const PresentationComposeRequest &request) const;

    private:
        static RHI::TexturePtr ResolvePresentationTexture(const ViewRenderContext &context);
    };

} // namespace NorvesLib::Core::Rendering
