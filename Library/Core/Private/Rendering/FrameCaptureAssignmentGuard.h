#pragma once

#include "Rendering/FrameCaptureReadbackHelper.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class FrameCaptureAssignmentGuard
    {
    public:
        FrameCaptureAssignmentGuard(
            FrameCaptureReadbackHelper* helper,
            const FrameCaptureRequestSnapshot& snapshot,
            uint64_t frameNumber) noexcept
            : m_Helper(helper)
            , m_Snapshot(snapshot)
            , m_FrameNumber(frameNumber)
        {
        }

        ~FrameCaptureAssignmentGuard() noexcept
        {
            if (!m_bResolved && m_Helper && m_Snapshot.IsValid())
            {
                m_Helper->AbandonAssignedRequest(
                    m_Snapshot,
                    m_FrameNumber,
                    FrameCaptureResultStatus::SourceUnavailable);
            }
        }

        FrameCaptureAssignmentGuard(const FrameCaptureAssignmentGuard&) = delete;
        FrameCaptureAssignmentGuard& operator=(const FrameCaptureAssignmentGuard&) = delete;
        FrameCaptureAssignmentGuard(FrameCaptureAssignmentGuard&&) = delete;
        FrameCaptureAssignmentGuard& operator=(FrameCaptureAssignmentGuard&&) = delete;

        void MarkResolved() noexcept
        {
            m_bResolved = true;
        }

    private:
        FrameCaptureReadbackHelper* m_Helper = nullptr;
        FrameCaptureRequestSnapshot m_Snapshot;
        uint64_t m_FrameNumber = 0;
        bool m_bResolved = false;
    };

} // namespace NorvesLib::Core::Rendering
