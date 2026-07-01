#include "Rendering/FramePacket.h"
#include "Rendering/SceneView.h"

#include <cassert>
#include <iostream>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
#include "Rendering/RenderingCoordinatorStatsPropagation.inl"
    }

    void AssertAccumulatesViewStats()
    {
        FrameStatsSnapshot snapshot;
        snapshot.VisibleObjects = 2;
        snapshot.BatchCount = 3;
        snapshot.InstancedDrawCalls = 4;
        snapshot.SavedDrawCalls = 5;
        snapshot.CullingTimeMs = 1.25f;
        snapshot.BatchingTimeMs = 2.5f;

        SceneView::SceneViewStats stats;
        stats.VisibleProxies = 7;
        stats.BatchCount = 11;
        stats.InstancedDrawCalls = 13;
        stats.SavedDrawCalls = 17;
        stats.CullingTimeMs = 3.75f;
        stats.BatchingTimeMs = 4.5f;

        AccumulateViewStats(snapshot, stats);

        assert(snapshot.VisibleObjects == 9);
        assert(snapshot.BatchCount == 14);
        assert(snapshot.InstancedDrawCalls == 17);
        assert(snapshot.SavedDrawCalls == 22);
        assert(snapshot.CullingTimeMs == 5.0f);
        assert(snapshot.BatchingTimeMs == 7.0f);
    }

    void AssertPacketWrapperAccumulatesViewStats()
    {
        FramePacket packet;

        SceneView::SceneViewStats stats;
        stats.VisibleProxies = 19;
        stats.BatchCount = 23;
        stats.InstancedDrawCalls = 29;
        stats.SavedDrawCalls = 31;
        stats.CullingTimeMs = 5.5f;
        stats.BatchingTimeMs = 6.25f;

        AccumulateViewStats(&packet, stats);

        assert(packet.Stats.VisibleObjects == 19);
        assert(packet.Stats.BatchCount == 23);
        assert(packet.Stats.InstancedDrawCalls == 29);
        assert(packet.Stats.SavedDrawCalls == 31);
        assert(packet.Stats.CullingTimeMs == 5.5f);
        assert(packet.Stats.BatchingTimeMs == 6.25f);
    }

    void AssertNullPacketWrapperIsNoOp()
    {
        SceneView::SceneViewStats stats;
        stats.VisibleProxies = 1;
        stats.BatchCount = 1;
        stats.InstancedDrawCalls = 1;
        stats.SavedDrawCalls = 1;
        stats.CullingTimeMs = 1.0f;
        stats.BatchingTimeMs = 1.0f;

        AccumulateViewStats(static_cast<FramePacket*>(nullptr), stats);
    }
} // namespace NorvesLib::Core::Rendering

int main()
{
    using namespace NorvesLib::Core::Rendering;

    std::cout << "RenderingCoordinatorStatsPropagationTest start\n";

    AssertAccumulatesViewStats();
    AssertPacketWrapperAccumulatesViewStats();
    AssertNullPacketWrapperIsNoOp();

    std::cout << "RenderingCoordinatorStatsPropagationTest passed\n";
    return 0;
}
