#pragma once

#include "IXInputApi.h"
#include <cmath>

namespace NorvesLib::Core::Input
{
    class IGamepadSampleSink
    {
    public:
        virtual ~IGamepadSampleSink() = default;
        // 呼出中にcopy/受理する。借用stateを保持せず、falseは未受理を示す。
        virtual bool SubmitGamepadSample(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode) = 0;
    };

    // GameThread専用。APIはこのstateより長命、sinkはPoll呼出中だけ借用する。
    // API/sink callbackからの再入や本体破棄は禁止。各slotは最後の受理値をcommitする。
    // focus CancelとShutdown時の操作解除はownerがInputSystem/Mapperへ別途行う。
    class XInputPollingState
    {
    public:
        explicit XInputPollingState(IXInputApi& api) : m_Api(api)
        {
        }
        XInputPollingState(const XInputPollingState&) = delete;
        XInputPollingState& operator=(const XInputPollingState&) = delete;
        XInputPollingState(XInputPollingState&&) = delete;
        XInputPollingState& operator=(XInputPollingState&&) = delete;
        void Initialize()
        {
            if (m_bInitialized)
            {
                return;
            }
            ResetSlots();
            m_bInitialized = true;
        }
        void Shutdown()
        {
            ResetSlots();
            m_bInitialized = false;
        }
        bool IsInitialized() const
        {
            return m_bInitialized;
        }
        void SetFocused(bool focused)
        {
            if (m_bFocused == focused)
            {
                return;
            }
            m_bFocused = focused;
            for (auto& slot : m_Slots)
            {
                slot.bNeedsBaseline = true;
            }
        }
        bool IsFocused() const
        {
            return m_bFocused;
        }
        bool IsConnected(uint8_t slot) const
        {
            return slot < GamepadSlotCount && m_Slots[slot].bConnected;
        }
        bool HasReadResult(uint8_t slot) const
        {
            return slot < GamepadSlotCount && m_Slots[slot].bHasProbe;
        }
        XInputReadResult GetLastReadResult(uint8_t slot) const
        {
            return slot < GamepadSlotCount ? m_Slots[slot].LastRead : XInputReadResult{};
        }
        // deviceは副作用のある出力再試行より先にも、このclock検証を使う。
        bool CanPoll(double unscaledTimeSeconds) const noexcept
        {
            return m_bInitialized && std::isfinite(unscaledTimeSeconds) && unscaledTimeSeconds >= 0 &&
                (!m_bHasTime || unscaledTimeSeconds >= m_LastTime);
        }
        // falseは不正clock/停止中、または未回復のAPI error/sink拒否があることを示す。
        // 他slotの正常処理は継続する。finite非負単調clockを使い、invalidでは呼出しも更新もしない。
        // sink例外は呼出側へ伝播し、その回の後続slotは処理しない。配送再試行状態は保持する。
        bool Poll(double unscaledTimeSeconds, IGamepadSampleSink& sink)
        {
            if (!CanPoll(unscaledTimeSeconds))
            {
                return false;
            }
            m_LastTime = unscaledTimeSeconds;
            m_bHasTime = true;
            bool visited[GamepadSlotCount]{};
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                if (m_Slots[slot].bConnected || m_Slots[slot].bRetryDelivery)
                {
                    ReadSlot(slot, unscaledTimeSeconds, sink);
                    visited[slot] = true;
                }
            }
            // 未接続はslotごと1秒以上間隔を空け、1Pollにつき最大1つ。未probeは最初の4frameで巡回する。
            for (uint8_t offset = 0; offset < GamepadSlotCount; ++offset)
            {
                const auto slot = static_cast<uint8_t>((m_ProbeCursor + offset) % GamepadSlotCount);
                const auto& entry = m_Slots[slot];
                if (visited[slot] || entry.bConnected || entry.bRetryDelivery)
                {
                    continue;
                }
                // deadline加算をせず経過時間で比較し、巨大なfinite clockでもoverflowしない。
                if (entry.bHasProbe && unscaledTimeSeconds - entry.LastProbeTime < DisconnectedProbeSeconds)
                {
                    continue;
                }
                ReadSlot(slot, unscaledTimeSeconds, sink);
                m_ProbeCursor = static_cast<uint8_t>((slot + 1) % GamepadSlotCount);
                break;
            }
            for (const auto& slot : m_Slots)
            {
                if (slot.bRetryDelivery || slot.LastRead.Status == EXInputReadStatus::Error)
                {
                    return false;
                }
            }
            return true;
        }
    private:
        static constexpr double DisconnectedProbeSeconds = 1.0;
        struct Slot
        {
            bool bConnected = false;
            bool bRetryDelivery = false;
            bool bHasProbe = false;
            bool bNeedsBaseline = true;
            double LastProbeTime = 0;
            XInputReadResult LastRead;
        };
        void ResetSlots()
        {
            for (auto& slot : m_Slots)
            {
                slot = {};
            }
            m_bHasTime = false;
            m_LastTime = 0;
            m_ProbeCursor = 0;
        }
        void ReadSlot(uint8_t slot, double time, IGamepadSampleSink& sink)
        {
            auto& entry = m_Slots[slot];
            XInputRawGamepadState raw;
            auto result = m_Api.ReadState(slot, raw);
            if (result.Status != EXInputReadStatus::Connected && result.Status != EXInputReadStatus::Disconnected)
            {
                result.Status = EXInputReadStatus::Error;
            }
            entry.LastRead = result;
            entry.bHasProbe = true;
            entry.LastProbeTime = time;
            const bool bConnected = result.Status == EXInputReadStatus::Connected;
            const auto state = bConnected ? NormalizeXInputGamepadState(raw) : GamepadState{};
            EGamepadSampleMode mode = EGamepadSampleMode::Live;
            if (!m_bFocused)
            {
                mode = EGamepadSampleMode::Background;
            }
            else if (bConnected && entry.bNeedsBaseline)
            {
                mode = EGamepadSampleMode::Baseline;
            }
            if (!bConnected)
            {
                // Errorはstream利用不可として退避する。物理USB切断の診断とは区別する。
                entry.bNeedsBaseline = true;
            }
            entry.bRetryDelivery = true;
            if (sink.SubmitGamepadSample(slot, state, mode))
            {
                entry.bConnected = bConnected;
                entry.bRetryDelivery = false;
                if (bConnected && m_bFocused)
                {
                    entry.bNeedsBaseline = false;
                }
            }
        }
        IXInputApi& m_Api;
        Slot m_Slots[GamepadSlotCount]{};
        bool m_bInitialized = false;
        bool m_bFocused = true;
        bool m_bHasTime = false;
        double m_LastTime = 0;
        uint8_t m_ProbeCursor = 0;
    };
} // namespace NorvesLib::Core::Input
