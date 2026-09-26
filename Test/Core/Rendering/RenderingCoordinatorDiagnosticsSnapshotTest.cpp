#include "Rendering/RenderingCoordinator.h"

#include "Rendering/RenderingCoordinatorDiagnostics.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    void AssertPublicSnapshotsDefaultAndValueBased()
    {
        RenderingCoordinator coordinator;

        const RenderingCoordinatorStatsSnapshot statsSnapshot = coordinator.GetStatsSnapshot();
        assert(statsSnapshot.GeneratedDrawCommandCount == 0);
        assert(statsSnapshot.PublicationSequence == 0);
        assert(!statsSnapshot.bRenderFrameCompleted);
        assert(!statsSnapshot.bGameThreadTimingsAvailable);
        assert(!statsSnapshot.bRenderFrameTimingAvailable);
        assert(!statsSnapshot.bGPUTimeAvailable);
        assert(!statsSnapshot.bTotalFrameTimeAvailable);
        assert(coordinator.GetStats().DrawCalls == 0);

        RenderGraphDebugDumpSnapshot unchangedDump;
        unchangedDump.Text = "unchanged";
        assert(!coordinator.TryGetRenderGraphDebugDumpSnapshot(0, unchangedDump));
        assert(unchangedDump.Text == "unchanged");

        coordinator.RequestRenderGraphDebugDump();
        assert(!coordinator.TryGetRenderGraphDebugDumpSnapshot(0, unchangedDump));
        assert(unchangedDump.Text == "unchanged");
    }

    void AssertClaimPublicationAndTruncation()
    {
        RenderingCoordinatorDiagnostics diagnostics;
        diagnostics.RequestRenderGraphDebugDump();

        RenderGraphDebugDumpRequestClaim claim = diagnostics.TryClaimRenderGraphDebugDumpRequest();
        assert(claim.IsClaimed());
        diagnostics.RequestRenderGraphDebugDump();
        claim.MarkProcessed();
        assert(diagnostics.HasRenderGraphDebugDumpRequest());

        RenderingCoordinatorStatsSnapshot statsSnapshot;
        statsSnapshot.Stats.DrawCalls = 3;
        statsSnapshot.GeneratedDrawCommandCount = 11;
        statsSnapshot.bRenderFrameCompleted = true;
        diagnostics.PublishStatsSnapshot(statsSnapshot);

        const RenderingCoordinatorStatsSnapshot publishedStats = diagnostics.GetStatsSnapshot();
        assert(publishedStats.Stats.DrawCalls == 3);
        assert(publishedStats.GeneratedDrawCommandCount == 11);
        assert(publishedStats.PublicationSequence == 1);

        Container::String longText(70000, 'a');
        diagnostics.PublishRenderGraphDebugDump(longText, 17, true, Container::String{});

        RenderGraphDebugDumpSnapshot dumpSnapshot;
        assert(diagnostics.TryGetRenderGraphDebugDumpSnapshot(0, dumpSnapshot));
        assert(dumpSnapshot.bAvailable);
        assert(dumpSnapshot.bTruncated);
        assert(dumpSnapshot.FrameNumber == 17);
        assert(dumpSnapshot.Text.size() <= 65536);
        assert(dumpSnapshot.Text.find("[truncated]") != Container::String::npos);

        const uint64_t firstDumpSequence = dumpSnapshot.PublicationSequence;
        assert(firstDumpSequence > 0);
        assert(!diagnostics.TryGetRenderGraphDebugDumpSnapshot(firstDumpSequence, dumpSnapshot));
        assert(!diagnostics.TryGetRenderGraphDebugDumpSnapshot(firstDumpSequence + 1, dumpSnapshot));

        diagnostics.PublishRenderGraphDebugDump("next", 18, true, Container::String{});
        assert(diagnostics.TryGetRenderGraphDebugDumpSnapshot(firstDumpSequence, dumpSnapshot));
        const uint64_t secondDumpSequence = dumpSnapshot.PublicationSequence;
        assert(secondDumpSequence > firstDumpSequence);

        diagnostics.Reset();
        diagnostics.PublishRenderGraphDebugDump("after-reset", 19, true, Container::String{});
        assert(diagnostics.TryGetRenderGraphDebugDumpSnapshot(secondDumpSequence, dumpSnapshot));
        assert(dumpSnapshot.PublicationSequence > secondDumpSequence);
    }

    void AssertResetClaimRestoreAndSnapshotValueCopies()
    {
        RenderingCoordinatorDiagnostics diagnostics;
        diagnostics.RequestRenderGraphDebugDump();
        {
            RenderGraphDebugDumpRequestClaim claim = diagnostics.TryClaimRenderGraphDebugDumpRequest();
            assert(claim.IsClaimed());
        }
        assert(diagnostics.HasRenderGraphDebugDumpRequest());

        RenderingCoordinatorStatsSnapshot published;
        published.Stats.DrawCalls = 5;
        published.GeneratedDrawCommandCount = 13;
        published.bRenderFrameCompleted = false;
        published.bGameThreadTimingsAvailable = true;
        diagnostics.PublishStatsSnapshot(published);
        published.Stats.DrawCalls = 99;
        const RenderingCoordinatorStatsSnapshot copied = diagnostics.GetStatsSnapshot();
        assert(copied.Stats.DrawCalls == 5);
        assert(copied.GeneratedDrawCommandCount == 13);
        assert(!copied.bRenderFrameCompleted);
        assert(copied.bGameThreadTimingsAvailable);

        diagnostics.Reset();
        const RenderingCoordinatorStatsSnapshot reset = diagnostics.GetStatsSnapshot();
        assert(reset.PublicationSequence == 0);
        assert(reset.Stats.DrawCalls == 0);
        assert(!diagnostics.HasRenderGraphDebugDumpRequest());
    }

    void AssertDumpCapPreservesUtf8AndNewlineBoundary()
    {
        RenderingCoordinatorDiagnostics diagnostics;
        Container::String text;
        for (uint32_t index = 0; index < 30000; ++index)
        {
            text += "x";
        }
        text += "\n";
        for (uint32_t index = 0; index < 30000; ++index)
        {
            text += "\xE3\x81\x82";
        }

        diagnostics.PublishStatsSnapshot(RenderingCoordinatorStatsSnapshot{});
        diagnostics.PublishRenderGraphDebugDump(text, 3, true, Container::String{});
        RenderGraphDebugDumpSnapshot snapshot;
        assert(diagnostics.TryGetRenderGraphDebugDumpSnapshot(1, snapshot));
        assert(snapshot.bTruncated);
        assert(snapshot.Text.size() <= 65536);
        assert(snapshot.Text.find("\n[truncated]\n") != Container::String::npos);
    }
}

int main()
{
    std::cout << "RenderingCoordinatorDiagnosticsSnapshotTest start\n";
    AssertPublicSnapshotsDefaultAndValueBased();
    AssertClaimPublicationAndTruncation();
    AssertResetClaimRestoreAndSnapshotValueCopies();
    AssertDumpCapPreservesUtf8AndNewlineBoundary();
    std::cout << "RenderingCoordinatorDiagnosticsSnapshotTest passed\n";
    return 0;
}
