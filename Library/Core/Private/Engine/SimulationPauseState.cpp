#include "Engine/SimulationPauseState.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Engine
{
    bool SimulationPauseState::Push(EPauseReason reason)
    {
        const uint32_t bit = PauseReasonBit(reason);
        if (bit == 0)
        {
            LOG_WARNING("範囲外のポーズ理由 %u の要求を拒否しました", static_cast<unsigned>(reason));
            return false;
        }

        uint32_t& count = m_Counts[static_cast<size_t>(reason)];
        if (count == UINT32_MAX)
        {
            LOG_WARNING("ポーズ理由 %u の要求数が上限に達しています", static_cast<unsigned>(reason));
            return false;
        }

        if (++count == 1)
        {
            m_ActiveReasons |= bit;
            OnChanged.Broadcast(m_ActiveReasons);
        }
        return true;
    }

    bool SimulationPauseState::Pop(EPauseReason reason)
    {
        const uint32_t bit = PauseReasonBit(reason);
        if (bit == 0)
        {
            LOG_WARNING("範囲外のポーズ理由 %u の解除を拒否しました", static_cast<unsigned>(reason));
            return false;
        }

        uint32_t& count = m_Counts[static_cast<size_t>(reason)];
        if (count == 0)
        {
            LOG_WARNING("ポーズ理由 %u には解除できる要求がありません", static_cast<unsigned>(reason));
            return false;
        }

        if (--count == 0)
        {
            m_ActiveReasons &= ~bit;
            OnChanged.Broadcast(m_ActiveReasons);
        }
        return true;
    }

    uint32_t SimulationPauseState::GetRequestCount(EPauseReason reason) const
    {
        return reason < EPauseReason::Count ? m_Counts[static_cast<size_t>(reason)] : 0;
    }

    void SimulationPauseState::Reset()
    {
        if (m_ActiveReasons == 0)
        {
            return;
        }

        for (uint32_t& count : m_Counts)
        {
            count = 0;
        }
        m_ActiveReasons = 0;
        OnChanged.Broadcast(m_ActiveReasons);
    }
} // namespace NorvesLib::Core::Engine
