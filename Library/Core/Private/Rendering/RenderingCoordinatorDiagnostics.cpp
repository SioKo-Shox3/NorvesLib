#include "Rendering/RenderingCoordinatorDiagnostics.h"

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t MaxRenderGraphDebugDumpBytes = 65536;
        constexpr const char *TruncationSuffix = "\n[truncated]\n";

        Container::String TruncateRenderGraphDebugDump(const Container::String &text, bool &bOutTruncated)
        {
            bOutTruncated = text.size() > MaxRenderGraphDebugDumpBytes;
            if (!bOutTruncated)
            {
                return text;
            }

            const uint32_t suffixLength = static_cast<uint32_t>(Container::String(TruncationSuffix).size());
            uint32_t payloadLength = MaxRenderGraphDebugDumpBytes - suffixLength;
            uint32_t newlineLength = 0;
            for (uint32_t index = payloadLength; index > 0; --index)
            {
                if (text[index - 1] == '\n')
                {
                    newlineLength = index;
                    break;
                }
            }

            if (newlineLength > 0)
            {
                payloadLength = newlineLength;
            }
            else
            {
                while (payloadLength > 0 &&
                       (static_cast<unsigned char>(text[payloadLength]) & 0xC0u) == 0x80u)
                {
                    --payloadLength;
                }
            }

            Container::String result = text.substr(0, payloadLength);
            result += TruncationSuffix;
            return result;
        }
    } // namespace

    RenderGraphDebugDumpRequestClaim::RenderGraphDebugDumpRequestClaim(RenderingCoordinatorDiagnostics *owner)
        : m_Owner(owner)
        , m_bRestoreRequest(owner != nullptr)
    {
    }

    RenderGraphDebugDumpRequestClaim::RenderGraphDebugDumpRequestClaim(RenderGraphDebugDumpRequestClaim &&other) noexcept
        : m_Owner(other.m_Owner)
        , m_bRestoreRequest(other.m_bRestoreRequest)
    {
        other.m_Owner = nullptr;
        other.m_bRestoreRequest = false;
    }

    RenderGraphDebugDumpRequestClaim &RenderGraphDebugDumpRequestClaim::operator=(RenderGraphDebugDumpRequestClaim &&other) noexcept
    {
        if (this != &other)
        {
            if (m_Owner && m_bRestoreRequest)
            {
                m_Owner->RestoreRenderGraphDebugDumpRequest();
            }

            m_Owner = other.m_Owner;
            m_bRestoreRequest = other.m_bRestoreRequest;
            other.m_Owner = nullptr;
            other.m_bRestoreRequest = false;
        }
        return *this;
    }

    RenderGraphDebugDumpRequestClaim::~RenderGraphDebugDumpRequestClaim()
    {
        if (m_Owner && m_bRestoreRequest)
        {
            m_Owner->RestoreRenderGraphDebugDumpRequest();
        }
    }

    bool RenderGraphDebugDumpRequestClaim::IsClaimed() const
    {
        return m_Owner != nullptr;
    }

    void RenderGraphDebugDumpRequestClaim::MarkProcessed()
    {
        m_bRestoreRequest = false;
    }

    RenderingCoordinatorStatsSnapshot RenderingCoordinatorDiagnostics::GetStatsSnapshot() const
    {
        Thread::ScopedLock lock(m_Mutex);
        return m_StatsSnapshot;
    }

    void RenderingCoordinatorDiagnostics::PublishStatsSnapshot(RenderingCoordinatorStatsSnapshot snapshot)
    {
        Thread::ScopedLock lock(m_Mutex);
        snapshot.PublicationSequence = m_NextPublicationSequence++;
        m_StatsSnapshot = snapshot;
    }

    void RenderingCoordinatorDiagnostics::Reset()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_StatsSnapshot = RenderingCoordinatorStatsSnapshot{};
        m_DumpSnapshot = RenderGraphDebugDumpSnapshot{};
        m_bRenderGraphDebugDumpRequested.Store(false, std::memory_order_release);
    }

    void RenderingCoordinatorDiagnostics::RequestRenderGraphDebugDump()
    {
        m_bRenderGraphDebugDumpRequested.Store(true, std::memory_order_release);
    }

    bool RenderingCoordinatorDiagnostics::HasRenderGraphDebugDumpRequest() const
    {
        return m_bRenderGraphDebugDumpRequested.Load(std::memory_order_acquire);
    }

    RenderGraphDebugDumpRequestClaim RenderingCoordinatorDiagnostics::TryClaimRenderGraphDebugDumpRequest()
    {
        if (!m_bRenderGraphDebugDumpRequested.Exchange(false, std::memory_order_acq_rel))
        {
            return {};
        }
        return RenderGraphDebugDumpRequestClaim(this);
    }

    bool RenderingCoordinatorDiagnostics::TryGetRenderGraphDebugDumpSnapshot(
        uint64_t knownPublicationSequence,
        RenderGraphDebugDumpSnapshot &outSnapshot) const
    {
        Thread::ScopedLock lock(m_Mutex);
        if (m_DumpSnapshot.PublicationSequence == 0 ||
            m_DumpSnapshot.PublicationSequence <= knownPublicationSequence)
        {
            return false;
        }

        outSnapshot = m_DumpSnapshot;
        return true;
    }

    void RenderingCoordinatorDiagnostics::PublishRenderGraphDebugDump(
        const Container::String &text,
        uint64_t frameNumber,
        bool bAvailable,
        const Container::String &unavailableReason)
    {
        bool bTruncated = false;
        RenderGraphDebugDumpSnapshot snapshot;
        snapshot.Text = TruncateRenderGraphDebugDump(text, bTruncated);
        snapshot.UnavailableReason = unavailableReason;
        snapshot.FrameNumber = frameNumber;
        snapshot.bAvailable = bAvailable;
        snapshot.bTruncated = bTruncated;

        Thread::ScopedLock lock(m_Mutex);
        snapshot.PublicationSequence = m_NextPublicationSequence++;
        m_DumpSnapshot = snapshot;
    }

    void RenderingCoordinatorDiagnostics::RestoreRenderGraphDebugDumpRequest()
    {
        m_bRenderGraphDebugDumpRequested.Store(true, std::memory_order_release);
    }
} // namespace NorvesLib::Core::Rendering
