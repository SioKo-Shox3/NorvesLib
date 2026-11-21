#pragma once

#include "Delegate/MulticastDelegate.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Engine
{
    enum class EPauseReason : uint8_t
    {
        Menu = 0,
        FocusLost,
        Minimized,
        Loading,
        Dialog,
        Count
    };

    static_assert(static_cast<size_t>(EPauseReason::Count) <= 32);
    constexpr uint32_t PauseReasonBit(EPauseReason reason)
    {
        return reason < EPauseReason::Count ? uint32_t{1} << static_cast<uint8_t>(reason) : 0;
    }

    // GameThread で理由ごとのポーズ要求を数える。
    class SimulationPauseState
    {
    public:
        SimulationPauseState() = default;
        SimulationPauseState(const SimulationPauseState&) = delete;
        SimulationPauseState& operator=(const SimulationPauseState&) = delete;

        bool Push(EPauseReason reason);
        bool Pop(EPauseReason reason);
        void Reset();

        uint32_t GetRequestCount(EPauseReason reason) const;
        uint32_t GetActiveReasons() const { return m_ActiveReasons; }
        bool IsWorldPaused() const { return m_ActiveReasons != 0; }

        // 有効な理由が変わったとき、その時点で立っている理由のビットを渡す。
        MulticastDelegate<uint32_t> OnChanged;

    private:
        uint32_t m_Counts[static_cast<size_t>(EPauseReason::Count)]{};
        uint32_t m_ActiveReasons = 0;
    };
} // namespace NorvesLib::Core::Engine
