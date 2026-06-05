#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/PresentationComposer.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    struct FramePacket;
    struct ViewportRenderPlan;
    struct ViewRenderContext;
    class SceneRenderer;
    class View;

    struct RenderFrameExecutionRequest
    {
        const FramePacket *Packet = nullptr;
        const Container::VariableArray<Container::TSharedPtr<View>> *Views = nullptr;
        View *FallbackView = nullptr;
        ViewRenderContext *Context = nullptr;
        SceneRenderer *Renderer = nullptr;
        RHI::ICommandList *CommandList = nullptr;
        Container::VariableArray<FrameCommand> *PendingFrameCommands = nullptr;
        PresentationComposer *Presentation = nullptr;
        PresentationComposeRequest PresentationRequest;
    };

    struct RenderFrameExecutionResult
    {
        bool bRenderedAnyViewport = false;
        uint32_t RenderedViewportCount = 0;
        uint32_t PresentationBlitCount = 0;
    };

    class RenderFrameExecutor
    {
    public:
        RenderFrameExecutionResult Execute(const RenderFrameExecutionRequest &request) const;

        static void ApplyViewportRenderPlan(ViewRenderContext &context, const ViewportRenderPlan *viewportPlan);
        static const ViewportRenderPlan *FindPrimaryViewportRenderPlan(const FramePacket &packet);

    private:
        static void FlushPendingFrameCommands(const RenderFrameExecutionRequest &request);
        static bool RenderViewForCurrentViewport(const RenderFrameExecutionRequest &request, View *view);
        static bool ComposePresentation(const RenderFrameExecutionRequest &request, bool bClearPresentation);
    };

} // namespace NorvesLib::Core::Rendering
