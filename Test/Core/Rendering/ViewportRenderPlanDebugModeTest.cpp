#include "Rendering/ViewRenderPlan.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "ViewportRenderPlanDebugModeTest start\n";

    ViewportRenderPlan plan;
    assert(plan.DebugMode == DebugViewMode::Normal);

    plan.DebugMode = DebugViewMode::Wireframe;
    assert(plan.DebugMode == DebugViewMode::Wireframe);

    plan.Clear();
    assert(plan.DebugMode == DebugViewMode::Normal);

    std::cout << "ViewportRenderPlanDebugModeTest passed\n";
    return 0;
}
