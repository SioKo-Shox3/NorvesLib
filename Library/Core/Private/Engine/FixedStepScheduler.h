#pragma once

#include "Engine/FixedStepSettings.h"
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
      // 実行中のrate変更は余りの意味が変わるので拒否する。比較設定は起動前に行う。
      bool SetRate(uint32_t rate);
      uint32_t GetRate() const
      {
          return m_Rate;
      }
      float GetDeltaSeconds() const
      {
          return 1.f / m_Rate;
      }
        void BeginRun();
        void EndRun();
        // 倍率0で有効な0nsになるため、0も余り保持のAdvancedとして受理する。
        FixedStepAdvanceResult Advance(int64_t deltaNanoseconds, bool bAdvanceSimulation);
        uint64_t GetRemainderScaledUnits() const
        {
            return m_AccumulatorScaledUnits;
        }

    private:
        Thread::Thread::ThreadId m_OwnerThreadId;
        uint64_t m_AccumulatorScaledUnits = 0;
        bool m_bRunning = false;
        uint32_t m_Rate = DefaultFixedUpdateRateHz;
    };
} // namespace NorvesLib::Core::Engine
