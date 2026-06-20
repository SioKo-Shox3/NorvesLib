#include "Rendering/RenderFrameExecutor.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/ViewRenderPlan.h"
#include "Rendering/Viewport.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "ViewportSnapshotDebugWiringTest start\n";

    Viewport viewport;
    assert(viewport.GetDebugViewMode() == DebugViewMode::Normal);

    viewport.SetDebugViewMode(DebugViewMode::MegaGeometryClusters);
    assert(viewport.GetDebugViewMode() == DebugViewMode::MegaGeometryClusters);

    ViewportRenderPlan plan;
    plan.DebugMode = viewport.GetDebugViewMode();
    assert(plan.DebugMode == DebugViewMode::MegaGeometryClusters);

    ViewRenderContext context;
    assert(context.GetActiveDebugMode() == DebugViewMode::Normal);

    RenderFrameExecutor::ApplyViewportRenderPlan(context, &plan);
    assert(context.CurrentViewport == &plan);
    assert(context.GetActiveDebugMode() == DebugViewMode::MegaGeometryClusters);

    RenderFrameExecutor::ApplyViewportRenderPlan(context, nullptr);
    assert(context.CurrentViewport == nullptr);
    assert(context.GetActiveDebugMode() == DebugViewMode::Normal);

    std::cout << "ViewportSnapshotDebugWiringTest passed\n";
    return 0;
}
