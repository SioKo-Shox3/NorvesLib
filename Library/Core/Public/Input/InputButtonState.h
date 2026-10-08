#pragma once

#include "Container/VariableArray.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Input
{
    enum class EInputButtonEventType : uint8_t
    {
        Pressed,
        Released
    };
    struct InputButtonEvent
    {
        EInputButtonEventType Type = EInputButtonEventType::Pressed;
        double UnscaledTimeSeconds = 0;
        double HeldDuration = 0;
    };
    struct InputButtonTiming
    {
        double TapMaxSeconds = 0.2;
        double DoubleTapMaxGapSeconds = 0.3;
        double HoldSeconds = 0.5;
    };

    inline bool IsValidInputButtonTiming(const InputButtonTiming& timing)
    {
        return std::isfinite(timing.TapMaxSeconds) && timing.TapMaxSeconds >= 0.0 &&
            std::isfinite(timing.DoubleTapMaxGapSeconds) && timing.DoubleTapMaxGapSeconds >= 0.0 &&
            std::isfinite(timing.HoldSeconds) && timing.HoldSeconds > 0.0;
    }

    struct InputButtonSnapshot
    {
        bool Pressed = false;
        bool Held = false;
        bool Released = false;
        bool Tap = false;
        bool DoubleTap = false;
        bool Hold = false;
        bool HoldStarted = false;
        double HeldDuration = 0.0;
        double ReleasedHeldDuration = 0.0;
    };

    // 呼出側が全bindingのdownを集約し、単調な実時間を渡す。OS/Identityへの依存はない。
    class InputButtonState
    {
    public:
        const InputButtonSnapshot& GetState() const { return m_State; }
        const InputButtonTiming& GetTiming() const { return m_Timing; }
        double GetTime() const { return m_Time; }

        // 固定入力を読むactionだけ明示的に有効化する。既定無効で未使用actionの履歴を溜めない。
        void SetFixedEventCapture(bool enabled)
        {
            if (m_bCaptureFixedEvents == enabled)
                return;
            m_bCaptureFixedEvents = enabled;
            m_FixedEvents.clear();
            m_NextFixedEvent = 0;
        }
        bool IsFixedEventCaptureEnabled() const
        {
            return m_bCaptureFixedEvents;
        }
        // 同じaction内の到達順を保持する。BeginFrameでは消さず、消費/Cancelでのみ取り除く。
        bool ConsumeFixedEvent(InputButtonEvent& out)
        {
            if (m_NextFixedEvent >= m_FixedEvents.size())
            {
                m_FixedEvents.clear();
                m_NextFixedEvent = 0;
                return false;
            }
            out = m_FixedEvents[m_NextFixedEvent++];
            if (m_NextFixedEvent == m_FixedEvents.size())
            {
                m_FixedEvents.clear();
                m_NextFixedEvent = 0;
            }
            else if (m_NextFixedEvent >= 64 && m_NextFixedEvent >= m_FixedEvents.size() / 2)
            {
                m_FixedEvents.erase(m_FixedEvents.begin(), m_FixedEvents.begin() + m_NextFixedEvent);
                m_NextFixedEvent = 0;
            }
            return true;
        }

        // 押下中の意味を途中で変えない。不正設定/held中は全状態を維持してfalse。
        bool SetTiming(const InputButtonTiming& timing)
        {
            if (!IsValidInputButtonTiming(timing) || m_State.Held) return false;
            m_Timing = timing;
            m_bLastTapValid = false;
            return true;
        }

        void BeginFrame()
        {
            m_State.Pressed = false;
            m_State.Released = false;
            m_State.Tap = false;
            m_State.DoubleTap = false;
            m_State.HoldStarted = false;
            m_State.ReleasedHeldDuration = 0.0;
        }

        // finiteな非負の絶対時刻。巻き戻りは拒否して状態を変更しない。
        bool AdvanceTo(double unscaledTimeSeconds)
        {
            if (!std::isfinite(unscaledTimeSeconds) || unscaledTimeSeconds < m_Time) return false;
            m_Time = unscaledTimeSeconds;
            if (m_State.Held)
            {
                m_State.HeldDuration = m_Time - m_PressTime;
                if (!m_State.Hold && HasReachedDeadline(m_PressTime, m_Timing.HoldSeconds, m_Time))
                {
                    m_State.Hold = true;
                    m_State.HoldStarted = true;
                }
            }
            return true;
        }

        void SetDown(bool down)
        {
            if (down == m_State.Held) return;
            // move元は配列が空でもcursorが残るため、新しい記録前に正規化する。
            if (m_NextFixedEvent >= m_FixedEvents.size())
            {
                m_FixedEvents.clear();
                m_NextFixedEvent = 0;
            }
            if (m_bCaptureFixedEvents)
                m_FixedEvents.push_back({down ? EInputButtonEventType::Pressed : EInputButtonEventType::Released,
                                         m_Time, down ? 0 : m_State.HeldDuration});
            if (down)
            {
                m_State.Held = true;
                m_State.Pressed = true;
                m_State.Hold = false;
                m_State.HeldDuration = 0.0;
                m_PressTime = m_Time;
                m_bFixedPressPending = true;
                return;
            }
            m_State.Released = true;
            m_State.ReleasedHeldDuration = m_State.HeldDuration;
            const bool tap = !m_State.Hold && IsWithinDeadline(m_PressTime, m_Timing.TapMaxSeconds, m_Time);
            m_State.Held = false;
            m_State.Hold = false;
            m_State.HeldDuration = 0.0;
            if (tap)
            {
                m_State.Tap = true;
                // release間の間隔で判定し、2tapを非重複の組として消費する。
                if (m_bLastTapValid && IsWithinDeadline(m_LastTapTime, m_Timing.DoubleTapMaxGapSeconds, m_Time))
                {
                    m_State.DoubleTap = true;
                    m_bLastTapValid = false;
                }
                else
                {
                    m_LastTapTime = m_Time;
                    m_bLastTapValid = true;
                }
            }
            else
            {
                m_bLastTapValid = false;
            }
        }

        // focus/context喪失は操作完了と区別し、Tapや遅延fixed押下を発火させない。
        void Cancel()
        {
            if (m_State.Held)
            {
                m_State.Released = true;
                m_State.ReleasedHeldDuration = m_State.HeldDuration;
            }
            m_State.Pressed = false;
            m_State.Held = false;
            m_State.Tap = false;
            m_State.DoubleTap = false;
            m_State.Hold = false;
            m_State.HoldStarted = false;
            m_State.HeldDuration = 0.0;
            m_bFixedPressPending = false;
            m_bLastTapValid = false;
            m_FixedEvents.clear();
            m_NextFixedEvent = 0;
        }

        bool HasPendingFixedPress() const { return m_bFixedPressPending; }
        bool ConsumeFixedPress()
        {
            const bool pending = m_bFixedPressPending;
            m_bFixedPressPending = false;
            return pending;
        }

    private:
        // 差分と閾値の比較は0.2/0.3等の境界で桁落ちするため、同じ絶対deadlineを比較する。
        static bool HasReachedDeadline(double start, double interval, double now)
        {
            if (interval > 0.0 && now == start) return false;
            const double deadline = start + interval;
            // 加算overflowなら有限の現在時刻からはまだ到達できない。
            return std::isfinite(deadline) && now >= deadline;
        }
        static bool IsWithinDeadline(double start, double interval, double now)
        {
            const double deadline = start + interval;
            // +Infのdeadlineは全ての有限の現在時刻を含む。入力値自体のInfは先に拒否済み。
            return now <= deadline;
        }

        Container::VariableArray<InputButtonEvent> m_FixedEvents;
        size_t m_NextFixedEvent = 0;
        bool m_bCaptureFixedEvents = false;
        InputButtonTiming m_Timing;
        InputButtonSnapshot m_State;
        double m_Time = 0.0;
        double m_PressTime = 0.0;
        double m_LastTapTime = 0.0;
        bool m_bLastTapValid = false;
        bool m_bFixedPressPending = false;
    };
} // namespace NorvesLib::Core::Input
