#include "Debug/Stats.h"
#include <cassert>
#include <iostream>

int main()
{
    std::cout << "StatsManagerTest start\n";

    auto& manager = NorvesLib::Debug::StatsManager::Get();
    manager.ResetAll();
    manager.BeginFrame(42, 0.016f);
    manager.RecordScope("UnitScope", "Test", 1.5f);
    manager.SetGameThreadTimeMs(2.0f);
    manager.SetRenderPrepareTimeMs(0.5f);
    manager.SetRenderThreadTimeMs(3.0f);
    manager.SetRenderFrameTimeMs(2.5f);
    manager.SetGPUFrameTimeMs(1.25f);
    manager.EndFrame();

    auto profile = manager.GetFrameProfileSnapshot();

#if NORVES_ENABLE_STATS
    assert(NorvesLib::Debug::StatsManager::IsEnabled());
    assert(profile.FrameNumber == 42);
    assert(profile.DeltaTime == 0.016f);
    assert(profile.GameThreadTimeMs == 2.0f);
    assert(profile.RenderPrepareTimeMs == 0.5f);
    assert(profile.RenderThreadTimeMs == 3.0f);
    assert(profile.RenderFrameTimeMs == 2.5f);
    assert(profile.GPUFrameTimeMs == 1.25f);
    assert(profile.CPUFrameTimeMs == 5.0f);
    assert(profile.TotalFrameTimeMs == 5.0f);
    assert(profile.Events.size() == 1);
    assert(profile.Events[0].Name == "UnitScope");
    assert(profile.Events[0].Category == "Test");
#else
    assert(!NorvesLib::Debug::StatsManager::IsEnabled());
    assert(profile.FrameNumber == 0);
    assert(profile.Events.empty());
#endif

    std::cout << "StatsManagerTest passed\n";
    return 0;
}
