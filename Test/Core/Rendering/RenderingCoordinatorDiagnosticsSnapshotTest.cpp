#include "Rendering/RenderingCoordinator.h"

#include "Rendering/RenderingCoordinatorDiagnostics.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;

namespace
{
    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    std::filesystem::path FindSourceRoot()
    {
        std::filesystem::path candidate = std::filesystem::absolute(__FILE__).parent_path();
        for (;;)
        {
            if (std::filesystem::exists(candidate / "Library/Core/Private/Rendering/RenderingCoordinator.cpp"))
            {
                return candidate;
            }

            const std::filesystem::path parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }

        candidate = std::filesystem::current_path();
        for (;;)
        {
            if (std::filesystem::exists(candidate / "Library/Core/Private/Rendering/RenderingCoordinator.cpp"))
            {
                return candidate;
            }

            const std::filesystem::path parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }

        assert(false);
        return {};
    }

    std::size_t FindMatchingBrace(const std::string& source, std::size_t openBrace)
    {
        assert(openBrace != std::string::npos);
        assert(source[openBrace] == '{');

        uint32_t depth = 0;
        for (std::size_t index = openBrace; index < source.size(); ++index)
        {
            if (source[index] == '{')
            {
                ++depth;
            }
            else if (source[index] == '}')
            {
                --depth;
                if (depth == 0)
                {
                    return index;
                }
            }
        }

        assert(false);
        return std::string::npos;
    }

    std::string ExtractBraceBlock(const std::string& source, const std::string& marker)
    {
        const std::size_t markerPosition = source.find(marker);
        assert(markerPosition != std::string::npos);
        const std::size_t openBrace = source.find('{', markerPosition);
        assert(openBrace != std::string::npos);
        const std::size_t closeBrace = FindMatchingBrace(source, openBrace);
        return source.substr(markerPosition, closeBrace - markerPosition + 1);
    }

    uint32_t CountOccurrences(const std::string& source, const std::string& text)
    {
        uint32_t count = 0;
        std::size_t position = source.find(text);
        while (position != std::string::npos)
        {
            ++count;
            position = source.find(text, position + text.size());
        }
        return count;
    }

    void AssertDiagnosticsSourceContract()
    {
        const std::filesystem::path sourceRoot = FindSourceRoot();
        const std::string publicHeader =
            ReadTextFile(sourceRoot / "Library/Core/Public/Rendering/RenderingCoordinator.h");
        const std::string coordinator =
            ReadTextFile(sourceRoot / "Library/Core/Private/Rendering/RenderingCoordinator.cpp");

        assert(publicHeader.find("Debug::RenderingStats GetStats() const;") != std::string::npos);
        assert(publicHeader.find("Debug::RenderingStats& GetStats() const;") == std::string::npos);
        assert(publicHeader.find("const Debug::RenderingStats& GetStats() const;") == std::string::npos);

        const std::string renderFrameBlock =
            ExtractBraceBlock(coordinator, "void RenderingCoordinator::RenderFrame(");
        assert(renderFrameBlock.find("debugDumpCapture.TargetSceneViewId") != std::string::npos);
        assert(renderFrameBlock.find("ShouldBuildDebugDump") == std::string::npos);
        assert(renderFrameBlock.find("ShouldWriteDebugDump") == std::string::npos);
        assert(renderFrameBlock.find("m_DebugDumpOptions") == std::string::npos);

        const std::string generateDrawCommandsBlock =
            ExtractBraceBlock(coordinator, "void RenderingCoordinator::GenerateDrawCommands(");
        assert(CountOccurrences(generateDrawCommandsBlock,
                                "AccumulateViewStats(m_CurrentPacket, sceneView->GetStats());") == 2);
        assert(generateDrawCommandsBlock.find("bTraceActive") == std::string::npos);

        const std::size_t acceptedReadingPosition =
            renderFrameBlock.find("if (!bCanReadPacket)\n        {\n            return;\n        }");
        const std::size_t incompletePublisherPosition =
            renderFrameBlock.find("auto publishIncompleteStats = [this, packet, &renderStats, bGameThreadTimingsAvailable]()");
        const std::size_t normalCompletedPosition =
            renderFrameBlock.find("statsSnapshot.bRenderFrameCompleted = true;");
        const std::size_t normalPublisherPosition =
            renderFrameBlock.rfind("m_Diagnostics->PublishStatsSnapshot(statsSnapshot);");
        assert(acceptedReadingPosition != std::string::npos);
        assert(incompletePublisherPosition != std::string::npos);
        assert(normalCompletedPosition != std::string::npos);
        assert(normalPublisherPosition != std::string::npos);
        assert(acceptedReadingPosition < incompletePublisherPosition);
        assert(incompletePublisherPosition < normalCompletedPosition);
        assert(normalCompletedPosition < normalPublisherPosition);
        assert(CountOccurrences(renderFrameBlock, "publishIncompleteStats();") == 5);

        const std::size_t endFrameErrorPosition =
            coordinator.find("[[noreturn]] void ThrowSwapChainEndFrameError(");
        assert(endFrameErrorPosition != coordinator.npos);
        const std::size_t endFrameErrorOpenBrace = coordinator.find('{', endFrameErrorPosition);
        assert(endFrameErrorOpenBrace != coordinator.npos);
        const std::size_t endFrameErrorCloseBrace = FindMatchingBrace(coordinator, endFrameErrorOpenBrace);
        const auto endFrameErrorContains =
            [&coordinator, endFrameErrorPosition, endFrameErrorCloseBrace](const char* text)
        {
            const std::size_t position = coordinator.find(text, endFrameErrorPosition);
            return position != coordinator.npos && position < endFrameErrorCloseBrace;
        };
        assert(endFrameErrorContains("SwapChainEndFrameStatus::InvalidCommandList"));
        assert(endFrameErrorContains("SwapChainEndFrameStatus::SubmissionSerialExhausted"));
        assert(endFrameErrorContains("SwapChainEndFrameStatus::FenceResetFailed"));
        assert(endFrameErrorContains("SwapChainEndFrameStatus::SubmitFailed"));
        assert(endFrameErrorContains("SwapChainEndFrameStatus::PresentationFailed"));
        assert(CountOccurrences(
                   renderFrameBlock,
                   "endFrameResult.Status == RHI::SwapChainEndFrameStatus::InvalidCommandList") == 0);
        assert(CountOccurrences(renderFrameBlock,
                                 "SwapChain EndFrame rejected the command list; discarding frame") == 0);

        const std::size_t generatedCounterPosition =
            renderFrameBlock.find("statsSnapshot.GeneratedDrawCommandCount = packet->GeneratedDrawCommandCount;");
        const std::size_t gameThreadCountsPosition =
            renderFrameBlock.find("renderStats.VisibleObjects = packet->Stats.VisibleObjects;");
        const std::size_t gameThreadTimingSourcePosition =
            renderFrameBlock.find("const bool bGameThreadTimingsAvailable = packet->Stats.bGameThreadTimingsAvailable;");
        const std::size_t gameThreadTimingAvailabilityPosition =
            renderFrameBlock.find("statsSnapshot.bGameThreadTimingsAvailable = bGameThreadTimingsAvailable;");
        const std::size_t timingAvailabilityPosition =
            renderFrameBlock.find("statsSnapshot.bRenderFrameTimingAvailable = bTraceActive;");
        assert(generatedCounterPosition != std::string::npos);
        assert(gameThreadCountsPosition != std::string::npos);
        assert(gameThreadTimingSourcePosition != std::string::npos);
        assert(gameThreadTimingAvailabilityPosition != std::string::npos);
        assert(timingAvailabilityPosition != std::string::npos);
        assert(gameThreadCountsPosition < incompletePublisherPosition);
        assert(gameThreadTimingSourcePosition < incompletePublisherPosition);
        assert(generatedCounterPosition < timingAvailabilityPosition);
        assert(gameThreadTimingAvailabilityPosition < timingAvailabilityPosition);
        assert(renderFrameBlock.find("#if NORVES_ENABLE_STATS", generatedCounterPosition) < timingAvailabilityPosition);

        const std::string initializeBlock =
            ExtractBraceBlock(coordinator, "bool RenderingCoordinator::Initialize(");
        const std::string shutdownBlock =
            ExtractBraceBlock(coordinator, "void RenderingCoordinator::Shutdown(");
        assert(CountOccurrences(coordinator,
                                "m_PreviousCompletedTotalFrameTimeMs = 0.0f;") == 2);
        assert(CountOccurrences(coordinator,
                                "m_LatestCompletedGPUTimeMs = 0.0f;") == 2);
        assert(CountOccurrences(coordinator,
                                "m_bLatestCompletedGPUTimeValid = false;") == 2);
        assert(initializeBlock.find("m_PreviousCompletedTotalFrameTimeMs = 0.0f;") != std::string::npos);
        assert(shutdownBlock.find("m_PreviousCompletedTotalFrameTimeMs = 0.0f;") != std::string::npos);
    }

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
    AssertDiagnosticsSourceContract();
    std::cout << "AssertDiagnosticsSourceContract passed\n";
    return 0;
}
