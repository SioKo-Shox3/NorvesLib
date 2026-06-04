#include "Debug/Stats.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    std::cout << "StatsManagerTest start\n";

    auto& manager = NorvesLib::Debug::StatsManager::Get();
    const char* tracePath = "StatsManagerTest.trace.csv";
    std::filesystem::remove(tracePath);

    manager.ResetAll();

#if NORVES_ENABLE_STATS
    assert(manager.StartTrace(tracePath));
    assert(manager.IsTraceActive());
#else
    assert(!manager.StartTrace(tracePath));
    assert(!manager.IsTraceActive());
#endif

    manager.BeginFrame(42, 0.016f);
    manager.RecordScope("UnitScope", "Test", 1.5f);
    manager.SetGameThreadTimeMs(2.0f);
    manager.SetRenderPrepareTimeMs(0.5f);
    manager.SetRenderThreadTimeMs(3.0f);
    manager.SetRenderFrameTimeMs(2.5f);
    manager.SetGPUFrameTimeMs(1.25f);
    manager.EndFrame();
    manager.StopTrace();

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
    assert(profile.CPUFrameTimeMs == 3.0f);
    assert(profile.TotalFrameTimeMs == 3.0f);
    assert(profile.Events.size() == 1);
    assert(profile.Events[0].Name == "UnitScope");
    assert(profile.Events[0].Category == "Test");
    assert(!manager.IsTraceActive());
    assert(std::filesystem::exists(tracePath));

    std::ifstream traceFile(tracePath);
    std::string traceContent((std::istreambuf_iterator<char>(traceFile)),
                             std::istreambuf_iterator<char>());
    assert(traceContent.find("Type,Frame,TimestampUs") != std::string::npos);
    assert(traceContent.find("Scope,42") != std::string::npos);
    assert(traceContent.find("\"Test\",\"UnitScope\"") != std::string::npos);
    assert(traceContent.find("Frame,42") != std::string::npos);

    manager.ResetAll();
    manager.BeginFrame(43, 0.02f);
    manager.RecordScope("InactiveScope", "Test", 1.0f);
    manager.EndFrame();
    auto inactiveProfile = manager.GetFrameProfileSnapshot();
    assert(inactiveProfile.FrameNumber == 0);
    assert(inactiveProfile.Events.empty());
#else
    assert(!NorvesLib::Debug::StatsManager::IsEnabled());
    assert(profile.FrameNumber == 0);
    assert(profile.Events.empty());
    assert(!std::filesystem::exists(tracePath));
#endif

    std::cout << "StatsManagerTest passed\n";
    return 0;
}
