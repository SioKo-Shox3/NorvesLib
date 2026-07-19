#pragma once

#include "Rendering/RenderingCoordinator.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core::Rendering
{
    class RenderingCoordinatorDiagnostics;

    class RenderGraphDebugDumpRequestClaim
    {
    public:
        RenderGraphDebugDumpRequestClaim() = default;
        RenderGraphDebugDumpRequestClaim(const RenderGraphDebugDumpRequestClaim &) = delete;
        RenderGraphDebugDumpRequestClaim &operator=(const RenderGraphDebugDumpRequestClaim &) = delete;
        RenderGraphDebugDumpRequestClaim(RenderGraphDebugDumpRequestClaim &&other) noexcept;
        RenderGraphDebugDumpRequestClaim &operator=(RenderGraphDebugDumpRequestClaim &&other) noexcept;
        ~RenderGraphDebugDumpRequestClaim();

        bool IsClaimed() const;
        void MarkProcessed();

    private:
        friend class RenderingCoordinatorDiagnostics;
        explicit RenderGraphDebugDumpRequestClaim(RenderingCoordinatorDiagnostics *owner);

        RenderingCoordinatorDiagnostics *m_Owner = nullptr;
        bool m_bRestoreRequest = false;
    };

    class RenderingCoordinatorDiagnostics
    {
    public:
        RenderingCoordinatorStatsSnapshot GetStatsSnapshot() const;
        void PublishStatsSnapshot(RenderingCoordinatorStatsSnapshot snapshot);
        void Reset();

        void RequestRenderGraphDebugDump();
        bool HasRenderGraphDebugDumpRequest() const;
        RenderGraphDebugDumpRequestClaim TryClaimRenderGraphDebugDumpRequest();
        bool TryGetRenderGraphDebugDumpSnapshot(uint64_t knownPublicationSequence,
                                                 RenderGraphDebugDumpSnapshot &outSnapshot) const;
        void PublishRenderGraphDebugDump(const Container::String &text,
                                         uint64_t frameNumber,
                                         bool bAvailable,
                                         const Container::String &unavailableReason);

    private:
        friend class RenderGraphDebugDumpRequestClaim;
        void RestoreRenderGraphDebugDumpRequest();

        mutable Thread::Mutex m_Mutex;
        Thread::Atomic<bool> m_bRenderGraphDebugDumpRequested{false};
        RenderingCoordinatorStatsSnapshot m_StatsSnapshot;
        RenderGraphDebugDumpSnapshot m_DumpSnapshot;
        uint64_t m_NextPublicationSequence = 1;
    };
} // namespace NorvesLib::Core::Rendering
