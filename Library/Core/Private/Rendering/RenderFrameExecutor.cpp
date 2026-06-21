#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/CanvasView.h"
#include "Rendering/CompositePass.h"
#include "Rendering/FramePacket.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"
#include <algorithm>

namespace NorvesLib::Core::Rendering
{
    RenderFrameExecutionResult RenderFrameExecutor::Execute(const RenderFrameExecutionRequest &request) const
    {
        RenderFrameExecutionResult result;
        if (!request.Packet ||
            !request.Views ||
            !request.Context ||
            !request.Renderer ||
            !request.CommandList)
        {
            return result;
        }

        if (!request.PresentationGraphPass &&
            !request.Presentation)
        {
            return result;
        }

        ResetFrameOutputs(request);
        if (ShouldCompose(*request.Packet, *request.Views))
        {
            return ExecuteCompositePath(request);
        }

        return ExecuteLegacyPath(request);
    }

    RenderFrameExecutionResult RenderFrameExecutor::ExecuteLegacyPath(const RenderFrameExecutionRequest &request)
    {
        RenderFrameExecutionResult result;

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

                const bool bClearPresentation = result.PresentationBlitCount == 0;
                ApplyViewportRenderPlan(*request.Context, &viewportPlan);
                ConfigurePresentationGraphPass(request, bClearPresentation);
                if (!RenderViewForCurrentViewport(request, view.get()))
                {
                    continue;
                }

                FlushPendingFrameCommands(request);
                bool bPresented = WasPresentationHandledByGraph(request);
                if (!bPresented)
                {
                    bPresented = ComposeLegacyPresentationFallback(request, bClearPresentation);
                }
                if (bPresented)
                {
                    ++result.PresentationBlitCount;
                }
                ++result.RenderedViewportCount;
                result.bRenderedAnyViewport = true;
            }
        }

        if (!result.bRenderedAnyViewport)
        {
            const bool bClearPresentation = result.PresentationBlitCount == 0;
            ApplyViewportRenderPlan(*request.Context, FindPrimaryViewportRenderPlan(*request.Packet));
            ConfigurePresentationGraphPass(request, bClearPresentation);
            if (RenderViewForCurrentViewport(request, request.FallbackView))
            {
                ++result.RenderedViewportCount;
            }
            FlushPendingFrameCommands(request);
            bool bPresented = WasPresentationHandledByGraph(request);
            if (!bPresented)
            {
                bPresented = ComposeLegacyPresentationFallback(request, bClearPresentation);
            }
            if (bPresented)
            {
                ++result.PresentationBlitCount;
            }
        }

        return result;
    }

    RenderFrameExecutionResult RenderFrameExecutor::ExecuteCompositePath(const RenderFrameExecutionRequest &request)
    {
        if (!request.CompositeGraphPass ||
            !request.PresentationGraphPass ||
            !request.Context ||
            !request.Context->Graph)
        {
            return ExecuteLegacyPath(request);
        }

        RenderFrameExecutionResult result;
        Container::VariableArray<const ViewRenderPlan*> sortedViews;
        sortedViews.reserve(request.Packet->Views.size());
        for (const ViewRenderPlan &viewPlan : request.Packet->Views)
        {
            sortedViews.push_back(&viewPlan);
        }

        std::stable_sort(sortedViews.begin(),
                         sortedViews.end(),
                         [](const ViewRenderPlan* lhs, const ViewRenderPlan* rhs)
                         {
                             return lhs->Priority < rhs->Priority;
                         });

        for (const ViewRenderPlan* viewPlan : sortedViews)
        {
            if (!viewPlan || !viewPlan->bEnabled)
            {
                continue;
            }

            View* view = ResolveView(request, *viewPlan);
            if (!view)
            {
                continue;
            }

            CanvasView* canvasView = dynamic_cast<CanvasView*>(view);
            if (canvasView && viewPlan->Viewports.empty())
            {
                ApplyViewportRenderPlan(*request.Context, nullptr);
                ConfigureNoPresentationGraphPass(request);
                canvasView->Render(*request.Context);
                FlushPendingFrameCommands(request);
                if (canvasView->GetFrameOutputTexture())
                {
                    ++result.RenderedViewportCount;
                    result.bRenderedAnyViewport = true;
                }
                continue;
            }

            for (const ViewportRenderPlan &viewportPlan : viewPlan->Viewports)
            {
                if (!viewportPlan.HasDrawableExtent())
                {
                    continue;
                }

                ApplyViewportRenderPlan(*request.Context, &viewportPlan);
                ConfigureNoPresentationGraphPass(request);
                if (!RenderViewForCurrentViewport(request, view))
                {
                    view->ResetFrameOutput();
                    continue;
                }

                FlushPendingFrameCommands(request);
                if (view->GetFrameOutputTexture())
                {
                    ++result.RenderedViewportCount;
                    result.bRenderedAnyViewport = true;
                }
            }
        }

        const ViewportRenderPlan* primarySceneViewport = FindPrimarySceneViewportRenderPlan(*request.Packet);
        RHI::TexturePtr sceneTexture;
        if (primarySceneViewport &&
            primarySceneViewport->ViewId < request.Views->size())
        {
            auto sceneView = (*request.Views)[primarySceneViewport->ViewId];
            if (sceneView)
            {
                sceneTexture = sceneView->GetFrameOutputTexture();
            }
        }

        if (!sceneTexture)
        {
            ClearStage2RequestsAndResults(request);
            return result;
        }

        CanvasView* canvasView = ResolveEnabledCanvasView(*request.Packet, *request.Views);
        const RHI::TexturePtr canvasTexture = canvasView ? canvasView->GetFrameOutputTexture() : nullptr;

        request.Context->Graph->Reset();
        ApplyViewportRenderPlan(*request.Context, primarySceneViewport);

        CompositePassRequest compositeRequest;
        compositeRequest.SceneTexture = sceneTexture;
        compositeRequest.CanvasTexture = canvasTexture;
        request.CompositeGraphPass->SetRequest(compositeRequest);
        request.Context->Graph->AddPass(request.CompositeGraphPass);

        PresentationPassRequest presentationRequest = request.GraphPresentationRequest;
        presentationRequest.bClearPresentation = true;
        request.PresentationGraphPass->SetRequest(presentationRequest);
        request.Context->PresentationGraphPass = request.PresentationGraphPass;
        request.Context->bPresentationGraphPassHandled = false;
        request.Context->Graph->AddPass(request.PresentationGraphPass);

        if (request.Context->Graph->Compile(*request.Context))
        {
            RenderGraphExecutionResult executionResult = request.Context->Graph->ExecuteWithResult(*request.Context);
            if (executionResult.bSuccess)
            {
                request.Context->CurrentGraphExecutionResult = &request.Context->Graph->GetLastExecutionResult();
                FlushPendingFrameCommands(request);
                if (WasPresentationHandledByGraph(request))
                {
                    ++result.PresentationBlitCount;
                }
            }
        }

        ClearStage2RequestsAndResults(request);
        // This path leaves the back buffer via the graph(composite) presentation load family.
        // Primary signal for the overlay seam to select the graph load path.
        result.bComposite = true;
        return result;
    }

    void RenderFrameExecutor::ApplyViewportRenderPlan(ViewRenderContext &context, const ViewportRenderPlan *viewportPlan)
    {
        context.CurrentGraphExecutionResult = nullptr;
        context.bPresentationGraphPassHandled = false;

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

    const ViewportRenderPlan *RenderFrameExecutor::FindPrimarySceneViewportRenderPlan(const FramePacket &packet)
    {
        for (const auto &viewPlan : packet.Views)
        {
            if (!viewPlan.bEnabled ||
                static_cast<ViewType>(viewPlan.ViewType) != ViewType::Scene)
            {
                continue;
            }

            for (const auto &viewportPlan : viewPlan.Viewports)
            {
                if (viewportPlan.HasDrawableExtent())
                {
                    return &viewportPlan;
                }
            }
        }

        return nullptr;
    }

    bool RenderFrameExecutor::ShouldCompose(
        const FramePacket &packet,
        const Container::VariableArray<Container::TSharedPtr<View>> &views)
    {
        return ResolveEnabledCanvasView(packet, views) != nullptr;
    }

    void RenderFrameExecutor::ResetFrameOutputs(const RenderFrameExecutionRequest &request)
    {
        if (request.Views)
        {
            for (const auto &view : *request.Views)
            {
                if (view)
                {
                    view->ResetFrameOutput();
                }
            }
        }

        if (request.CompositeGraphPass)
        {
            request.CompositeGraphPass->SetRequest(CompositePassRequest{});
        }

        if (request.PresentationGraphPass)
        {
            request.PresentationGraphPass->SetRequest(PresentationPassRequest{});
        }
    }

    CanvasView *RenderFrameExecutor::ResolveEnabledCanvasView(
        const FramePacket &packet,
        const Container::VariableArray<Container::TSharedPtr<View>> &views)
    {
        for (const ViewRenderPlan &viewPlan : packet.Views)
        {
            if (!viewPlan.bEnabled || viewPlan.ViewId >= views.size())
            {
                continue;
            }

            auto canvasView = Container::DynamicPointerCast<CanvasView>(views[viewPlan.ViewId]);
            if (canvasView && canvasView->IsEnabled())
            {
                return canvasView.get();
            }
        }

        return nullptr;
    }

    View *RenderFrameExecutor::ResolveView(const RenderFrameExecutionRequest &request,
                                           const ViewRenderPlan &viewPlan)
    {
        if (!request.Views || viewPlan.ViewId >= request.Views->size())
        {
            return nullptr;
        }

        auto view = (*request.Views)[viewPlan.ViewId];
        return view ? view.get() : nullptr;
    }

    void RenderFrameExecutor::ConfigureNoPresentationGraphPass(const RenderFrameExecutionRequest &request)
    {
        if (!request.Context)
        {
            return;
        }

        request.Context->PresentationGraphPass = nullptr;
        request.Context->bPresentationGraphPassHandled = false;
        if (request.PresentationGraphPass)
        {
            request.PresentationGraphPass->ResetResult();
        }
    }

    void RenderFrameExecutor::ClearStage2RequestsAndResults(const RenderFrameExecutionRequest &request)
    {
        if (request.CompositeGraphPass)
        {
            request.CompositeGraphPass->SetRequest(CompositePassRequest{});
        }

        if (request.PresentationGraphPass)
        {
            request.PresentationGraphPass->SetRequest(PresentationPassRequest{});
        }

        if (request.Context)
        {
            request.Context->PresentationGraphPass = nullptr;
            request.Context->bPresentationGraphPassHandled = false;
        }
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

    void RenderFrameExecutor::ConfigurePresentationGraphPass(const RenderFrameExecutionRequest &request,
                                                            bool bClearPresentation)
    {
        if (!request.Context)
        {
            return;
        }

        request.Context->PresentationGraphPass = request.PresentationGraphPass;
        request.Context->bPresentationGraphPassHandled = false;
        if (!request.PresentationGraphPass)
        {
            return;
        }

        PresentationPassRequest passRequest = request.GraphPresentationRequest;
        passRequest.bClearPresentation = bClearPresentation;
        request.PresentationGraphPass->SetRequest(passRequest);
    }

    bool RenderFrameExecutor::WasPresentationHandledByGraph(const RenderFrameExecutionRequest &request)
    {
        if (!request.Context ||
            !request.PresentationGraphPass ||
            !request.Context->CurrentGraphExecutionResult ||
            !request.Context->CurrentGraphExecutionResult->bSuccess)
        {
            return false;
        }

        return request.Context->bPresentationGraphPassHandled &&
               request.PresentationGraphPass->WasHandled();
    }

    bool RenderFrameExecutor::ComposeLegacyPresentationFallback(const RenderFrameExecutionRequest &request,
                                                               bool bClearPresentation)
    {
        if (!request.Presentation)
        {
            return false;
        }

        PresentationComposeRequest composeRequest = request.PresentationRequest;
        composeRequest.Context = request.Context;
        composeRequest.Renderer = request.Renderer;
        composeRequest.CommandList = request.CommandList;
        composeRequest.bClearPresentation = bClearPresentation;
        return request.Presentation->Compose(composeRequest);
    }

} // namespace NorvesLib::Core::Rendering
