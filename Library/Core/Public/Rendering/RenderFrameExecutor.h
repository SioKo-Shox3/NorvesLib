#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Rendering/CompositePass.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/PresentationComposer.h"
#include "Rendering/PresentationPass.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    struct FramePacket;
    struct ViewRenderPlan;
    struct ViewportRenderPlan;
    struct ViewRenderContext;
    class SceneRenderer;
    class CanvasView;
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
        PresentationPass *PresentationGraphPass = nullptr;
        PresentationPassRequest GraphPresentationRequest;
        CompositePass *CompositeGraphPass = nullptr;
    };

    struct RenderFrameExecutionResult
    {
        bool bRenderedAnyViewport = false;
        uint32_t RenderedViewportCount = 0;
        uint32_t PresentationBlitCount = 0;
        // Which presentation path actually ran. true = composite(graph) path, false = legacy path.
        // The overlay seam reads this to pick the path-dependent presentation load family (backward compatible: default false).
        bool bComposite = false;
    };

    class RenderFrameExecutor
    {
    public:
        RenderFrameExecutionResult Execute(const RenderFrameExecutionRequest &request) const;

        static void ApplyViewportRenderPlan(ViewRenderContext &context, const ViewportRenderPlan *viewportPlan);
        static const ViewportRenderPlan *FindPrimaryViewportRenderPlan(const FramePacket &packet);
        static const ViewportRenderPlan *FindPrimarySceneViewportRenderPlan(const FramePacket &packet);
        static bool ShouldCompose(const FramePacket &packet,
                                  const Container::VariableArray<Container::TSharedPtr<View>> &views);

    private:
        static RenderFrameExecutionResult ExecuteLegacyPath(const RenderFrameExecutionRequest &request);
        static RenderFrameExecutionResult ExecuteCompositePath(const RenderFrameExecutionRequest &request);
        static void ResetFrameOutputs(const RenderFrameExecutionRequest &request);
        static CanvasView *ResolveEnabledCanvasView(
            const FramePacket &packet,
            const Container::VariableArray<Container::TSharedPtr<View>> &views);
        static View *ResolveView(const RenderFrameExecutionRequest &request, const ViewRenderPlan &viewPlan);
        static void ConfigureNoPresentationGraphPass(const RenderFrameExecutionRequest &request);
        static void ClearStage2RequestsAndResults(const RenderFrameExecutionRequest &request);
        static void FlushPendingFrameCommands(const RenderFrameExecutionRequest &request);
        static bool RenderViewForCurrentViewport(const RenderFrameExecutionRequest &request, View *view);
        static void ConfigurePresentationGraphPass(const RenderFrameExecutionRequest &request, bool bClearPresentation);
        static bool WasPresentationHandledByGraph(const RenderFrameExecutionRequest &request);
        static bool ComposeLegacyPresentationFallback(const RenderFrameExecutionRequest &request,
                                                      bool bClearPresentation);
    };

} // namespace NorvesLib::Core::Rendering
