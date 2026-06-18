#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/FramePacket.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"

namespace NorvesLib::Core::Rendering
{
    RenderFrameExecutionResult RenderFrameExecutor::Execute(const RenderFrameExecutionRequest &request) const
    {
        RenderFrameExecutionResult result;
        if (!request.Packet ||
            !request.Views ||
            !request.Context ||
            !request.Renderer ||
            !request.CommandList ||
            !request.Presentation)
        {
            return result;
        }

        for (const ViewRenderPlan &viewPlan : request.Packet->Views)
        {
            if (!viewPlan.bEnabled)
            {
                continue;
            }

            if (viewPlan.ViewId >= request.Views->size())
            {
                continue;
            }

            auto view = (*request.Views)[viewPlan.ViewId];
            if (!view)
            {
                continue;
            }

            for (const ViewportRenderPlan &viewportPlan : viewPlan.Viewports)
            {
                if (!viewportPlan.HasDrawableExtent())
                {
                    continue;
                }

                ApplyViewportRenderPlan(*request.Context, &viewportPlan);
                if (!RenderViewForCurrentViewport(request, view.get()))
                {
                    continue;
                }

                FlushPendingFrameCommands(request);
                ComposePresentation(request, result.PresentationBlitCount == 0);
                ++result.PresentationBlitCount;
                ++result.RenderedViewportCount;
                result.bRenderedAnyViewport = true;
            }
        }

        if (!result.bRenderedAnyViewport)
        {
            ApplyViewportRenderPlan(*request.Context, FindPrimaryViewportRenderPlan(*request.Packet));
            if (RenderViewForCurrentViewport(request, request.FallbackView))
            {
                ++result.RenderedViewportCount;
            }
            FlushPendingFrameCommands(request);
            ComposePresentation(request, true);
            ++result.PresentationBlitCount;
        }

        return result;
    }

    void RenderFrameExecutor::ApplyViewportRenderPlan(ViewRenderContext &context, const ViewportRenderPlan *viewportPlan)
    {
        context.CurrentGraphExecutionResult = nullptr;

        if (!viewportPlan)
        {
            context.CurrentViewport = nullptr;
            context.CurrentCamera = nullptr;
            context.CurrentDrawCommands = DrawCommandView{};
            context.CurrentOpaqueCommands = DrawCommandView{};
            context.CurrentTransparentCommands = DrawCommandView{};
            return;
        }

        context.CurrentViewport = viewportPlan;
        context.CurrentCamera = viewportPlan->bHasCamera ? &viewportPlan->Camera : nullptr;
        if (!context.SnapshotDrawCommandSource)
        {
            context.CurrentDrawCommands = DrawCommandView{};
            context.CurrentOpaqueCommands = DrawCommandView{};
            context.CurrentTransparentCommands = DrawCommandView{};
            return;
        }

        context.CurrentDrawCommands = DrawCommandView::FromRange(*context.SnapshotDrawCommandSource,
                                                                 viewportPlan->DrawCommandRange);
        context.CurrentOpaqueCommands = DrawCommandView::FromRange(*context.SnapshotDrawCommandSource,
                                                                   viewportPlan->OpaqueCommandRange);
        context.CurrentTransparentCommands = DrawCommandView::FromRange(*context.SnapshotDrawCommandSource,
                                                                        viewportPlan->TransparentCommandRange);
    }

    const ViewportRenderPlan *RenderFrameExecutor::FindPrimaryViewportRenderPlan(const FramePacket &packet)
    {
        const ViewportRenderPlan *fallbackViewport = nullptr;

        for (const auto &viewPlan : packet.Views)
        {
            if (!viewPlan.bEnabled)
            {
                continue;
            }

            for (const auto &viewportPlan : viewPlan.Viewports)
            {
                if (!viewportPlan.HasDrawableExtent())
                {
                    continue;
                }

                if (!fallbackViewport)
                {
                    fallbackViewport = &viewportPlan;
                }

                if (static_cast<ViewType>(viewPlan.ViewType) == ViewType::Scene)
                {
                    return &viewportPlan;
                }
            }
        }

        return fallbackViewport;
    }

    void RenderFrameExecutor::FlushPendingFrameCommands(const RenderFrameExecutionRequest &request)
    {
        if (!request.PendingFrameCommands || request.PendingFrameCommands->empty())
        {
            return;
        }

        request.Renderer->ExecuteFrameCommands(*request.PendingFrameCommands, request.CommandList);
        request.PendingFrameCommands->clear();
    }

    bool RenderFrameExecutor::RenderViewForCurrentViewport(const RenderFrameExecutionRequest &request, View *view)
    {
        if (!view || !view->IsEnabled())
        {
            return false;
        }

        if (view->GetPassCount() > 0)
        {
            view->Render(*request.Context);
            return true;
        }

        const DrawCommandView activeDrawCommands = request.Context->GetActiveDrawCommands();
        if (!activeDrawCommands.empty())
        {
            request.Renderer->ExecuteDrawCommands(activeDrawCommands, request.CommandList);
            return true;
        }

        return false;
    }

    bool RenderFrameExecutor::ComposePresentation(const RenderFrameExecutionRequest &request, bool bClearPresentation)
    {
        PresentationComposeRequest composeRequest = request.PresentationRequest;
        composeRequest.Context = request.Context;
        composeRequest.Renderer = request.Renderer;
        composeRequest.CommandList = request.CommandList;
        composeRequest.bClearPresentation = bClearPresentation;
        return request.Presentation->Compose(composeRequest);
    }

} // namespace NorvesLib::Core::Rendering
