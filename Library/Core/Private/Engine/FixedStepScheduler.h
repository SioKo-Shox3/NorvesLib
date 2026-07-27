#pragma once

#include "Thread/Thread.h"

#include <cstdint>

namespace NorvesLib::Core::Engine
{
    enum class EFixedStepAdvanceStatus
    {
        Advanced,
        Paused,
        InvalidDelta,
        NotRunning,
        WrongThread
    };

    struct FixedStepAdvanceResult
    {
        EFixedStepAdvanceStatus Status = EFixedStepAdvanceStatus::NotRunning;
        uint64_t ExecutedSteps = 0;
        uint64_t DroppedSteps = 0;
        uint64_t RemainderScaledUnits = 0;
    };

    class FixedStepScheduler
    {
    public:
        void BeginRun();
        void EndRun();
        FixedStepAdvanceResult Advance(int64_t deltaNanoseconds, bool bAdvanceSimulation);

    private:
        Thread::Thread::ThreadId m_OwnerThreadId;
        uint64_t m_AccumulatorScaledUnits = 0;
        bool m_bRunning = false;
    };
} // namespace NorvesLib::Core::Engine
