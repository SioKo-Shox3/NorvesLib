#include "Rendering/Viewport.h"

#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Check failed: " << message << "\n";
            return false;
        }

        return true;
    }
}

int main()
{
    std::cout << "ViewportDebugViewModeReleaseClampTest start\n";

    Viewport viewport;

    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::Normal,
               "default debug view mode should be Normal"))
    {
        return 1;
    }

    viewport.SetDebugViewMode(DebugViewMode::Normal);
    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::Normal,
               "setting Normal should keep Normal"))
    {
        return 1;
    }

    viewport.SetDebugViewMode(DebugViewMode::Wireframe);
#if NORVES_BUILD_DEVELOPMENT
    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::Wireframe,
               "development build should preserve Wireframe"))
    {
        return 1;
    }

    viewport.SetDebugViewMode(DebugViewMode::MegaGeometryClusters);
    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::MegaGeometryClusters,
               "development build should preserve MegaGeometryClusters"))
    {
        return 1;
    }
#else
    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::Normal,
               "release build should clamp Wireframe to Normal"))
    {
        return 1;
    }

    viewport.SetDebugViewMode(DebugViewMode::MegaGeometryClusters);
    if (!Check(viewport.GetDebugViewMode() == DebugViewMode::Normal,
               "release build should clamp MegaGeometryClusters to Normal"))
    {
        return 1;
    }
#endif

    std::cout << "ViewportDebugViewModeReleaseClampTest passed\n";
    return 0;
}
