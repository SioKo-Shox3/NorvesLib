#pragma once

#include "Container/VariableArray.h"
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
        // 通知中の変化は発生順に積み、今の通知を全購読者に届け終えてから次を配る。
        MulticastDelegate<uint32_t> OnChanged;

    private:
        // 変化後の理由を記録し、配信中でなければ順に通知する。
        void NotifyChanged();

        uint32_t m_Counts[static_cast<size_t>(EPauseReason::Count)]{};
        uint32_t m_ActiveReasons = 0;
        // 各変化の直後に有効だった理由を発生順に保持する。
        Container::VariableArray<uint32_t> m_PendingReasons;
        // 待ち行列の配信中は再入による二重配信を防ぐ。
        bool m_bNotifying = false;
    };
} // namespace NorvesLib::Core::Engine
