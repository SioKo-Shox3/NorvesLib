#include "Rendering/RenderTypes.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

int main()
{
    std::cout << "DebugViewModeStringTest start\n";

    assert(std::strcmp(DebugViewModeToString(DebugViewMode::Normal), "Normal") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::Unlit), "Unlit") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::Wireframe), "Wireframe") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::MegaGeometryClusters), "MegaGeometryClusters") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::GBufferAlbedo), "GBufferAlbedo") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::GBufferNormal), "GBufferNormal") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::GBufferMaterial), "GBufferMaterial") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::GBufferDepth), "GBufferDepth") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::LODLevel), "LODLevel") == 0);
    assert(std::strcmp(DebugViewModeToString(DebugViewMode::Count), "Invalid") == 0);
    assert(std::strcmp(DebugViewModeToString(static_cast<DebugViewMode>(255)), "Invalid") == 0);

    std::cout << "DebugViewModeStringTest passed\n";
    return 0;
}
