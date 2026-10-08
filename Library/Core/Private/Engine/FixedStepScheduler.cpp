#include "Engine/FixedStepScheduler.h"

namespace NorvesLib::Core::Engine
{
    namespace
    {
        constexpr uint64_t NanosecondsPerSecond = 1'000'000'000;
        constexpr uint64_t MaximumStepsAt60Hz = 8;
    } // namespace

    bool FixedStepScheduler::SetRate(uint32_t rate)
    {
        if (m_bRunning || !IsSupportedFixedUpdateRate(rate))
            return false;
        m_Rate = rate;
        m_AccumulatorScaledUnits = 0;
        return true;
    }

    void FixedStepScheduler::BeginRun()
    {
        m_OwnerThreadId = Thread::Thread::GetCurrentThreadId();
        m_AccumulatorScaledUnits = 0;
        m_bRunning = true;
    }

    void FixedStepScheduler::EndRun()
    {
        m_bRunning = false;
        m_OwnerThreadId = {};
    }

    FixedStepAdvanceResult FixedStepScheduler::Advance(int64_t deltaNanoseconds, bool bAdvanceSimulation)
    {
        if (!m_bRunning)
        {
            return {EFixedStepAdvanceStatus::NotRunning, 0, 0, m_AccumulatorScaledUnits};
        }

        if (m_OwnerThreadId != Thread::Thread::GetCurrentThreadId())
        {
            return {EFixedStepAdvanceStatus::WrongThread, 0, 0, m_AccumulatorScaledUnits};
        }

        if (deltaNanoseconds <= 0)
        {
            return {EFixedStepAdvanceStatus::InvalidDelta, 0, 0, m_AccumulatorScaledUnits};
        }

        if (!bAdvanceSimulation)
        {
            return {EFixedStepAdvanceStatus::Paused, 0, 0, m_AccumulatorScaledUnits};
        }

        const uint64_t delta = static_cast<uint64_t>(deltaNanoseconds);
        const uint64_t wholeSeconds = delta / NanosecondsPerSecond;
        const uint64_t remainingNanoseconds = delta % NanosecondsPerSecond;
        const uint64_t stepsFromSeconds = wholeSeconds * m_Rate;
        const uint64_t temporary = m_AccumulatorScaledUnits + remainingNanoseconds * m_Rate;
        const uint64_t extraSteps = temporary / NanosecondsPerSecond;
        m_AccumulatorScaledUnits = temporary % NanosecondsPerSecond;

        // 120Hzも60Hz時と同じ実時間のcatch-up予算を保つ。
        const uint64_t MaximumStepsPerFrame = MaximumStepsAt60Hz * (m_Rate / 60);
        const uint64_t pendingSteps = stepsFromSeconds + extraSteps;
        const uint64_t executedSteps = pendingSteps < MaximumStepsPerFrame ? pendingSteps : MaximumStepsPerFrame;
        const uint64_t droppedSteps = pendingSteps - executedSteps;
        return {EFixedStepAdvanceStatus::Advanced, executedSteps, droppedSteps, m_AccumulatorScaledUnits};
    }
} // namespace NorvesLib::Core::Engine
