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
            NotifyChanged();
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
            NotifyChanged();
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
        NotifyChanged();
    }

    void SimulationPauseState::NotifyChanged()
    {
        m_PendingReasons.push_back(m_ActiveReasons);
        if (m_bNotifying)
        {
            return;
        }

        m_bNotifying = true;
        // 通知中に末尾へ追加された分も、同じ添字のループで順に配る。
        for (size_t index = 0; index < m_PendingReasons.size(); ++index)
        {
            // 購読者が待ち行列を伸ばしても、この通知の値は有効なままにする。
            const uint32_t reasons = m_PendingReasons[index];
            OnChanged.Broadcast(reasons);
        }
        m_PendingReasons.clear();
        m_bNotifying = false;
    }
} // namespace NorvesLib::Core::Engine
