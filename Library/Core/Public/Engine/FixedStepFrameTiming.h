#pragma once
#include "Engine/FixedStepSettings.h"
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

    // Game側がprivate schedulerを参照せず、時間付き入力の消費/drop/端数を照合する。
    // Input/AnimationではbScheduled=false、固定stepとLateTickでは結果が確定済み。
    struct FixedStepFrameTiming
    {
        uint64_t Serial = 0;
        uint32_t Rate = DefaultFixedUpdateRateHz;
        int64_t InputNanoseconds = 0;
        uint64_t StartRemainderScaledUnits = 0;
        FixedStepAdvanceResult Advance;
        bool bScheduled = false;
    };
} // namespace NorvesLib::Core::Engine
