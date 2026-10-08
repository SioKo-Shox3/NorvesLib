#pragma once

#include "Input/HapticsEnvelopeMath.h"

namespace NorvesLib::Core::Input
{
    // device/slotごとに保持する成功ACKと送信不確実性。実API呼出し自体は行わない。
    // 非zero試行後は成功zero ACKまで状態を破棄せず、停止を試みる責務をownerが持つ。
    class HapticsOutputState
    {
    public:
        bool NeedsSend(const HapticsOutput& desired) const
        {
            if (!IsValidHapticsOutput(desired))
            {
                return false;
            }
            if (!m_bHasAck || m_bUncertain)
            {
                return true;
            }
            if ((desired.Low == 0 && m_Acknowledged.Low != 0) ||
                (desired.High == 0 && m_Acknowledged.High != 0))
            {
                return true;
            }
            constexpr double threshold = 1.0 / 255.0;
            return std::abs(static_cast<double>(desired.Low) - m_Acknowledged.Low) >= threshold ||
                std::abs(static_cast<double>(desired.High) - m_Acknowledged.High) >= threshold;
        }
        // 送信を実際に試みた後だけ呼ぶ。失敗した非zeroも作動した可能性として追跡する。
        bool RecordAttempt(const HapticsOutput& attempted, bool accepted)
        {
            if (!IsValidHapticsOutput(attempted))
            {
                return false;
            }
            if (!IsZeroHapticsOutput(attempted))
            {
                m_bMayBeActive = true;
            }
            if (accepted)
            {
                m_Acknowledged = attempted;
                m_bHasAck = true;
                m_bUncertain = false;
                m_bMayBeActive = !IsZeroHapticsOutput(attempted);
            }
            else
            {
                m_bUncertain = true;
            }
            return true;
        }
        bool HasAcknowledgedOutput() const { return m_bHasAck; }
        HapticsOutput GetAcknowledgedOutput() const { return m_Acknowledged; }
        bool IsUncertain() const { return m_bUncertain; }
        bool MayBeActive() const { return m_bMayBeActive; }
    private:
        HapticsOutput m_Acknowledged;
        bool m_bHasAck = false;
        bool m_bUncertain = false;
        bool m_bMayBeActive = false;
    };
} // namespace NorvesLib::Core::Input
