#pragma once

#include "IXInputApi.h"

namespace NorvesLib::Core::Input
{
    // APIは本体より長命。GameThread専用、API callbackから再入/破棄しない。
    // 自身の送信で停止義務が生じたslotだけを停止する。成功zeroまで義務を保持する。
    class XInputVibrationState
    {
    public:
        explicit XInputVibrationState(IXInputApi& api) : m_Api(api)
        {
        }
        XInputVibrationState(const XInputVibrationState&) = delete;
        XInputVibrationState& operator=(const XInputVibrationState&) = delete;
        XInputVibrationState(XInputVibrationState&&) = delete;
        XInputVibrationState& operator=(XInputVibrationState&&) = delete;
        bool Initialize() noexcept
        {
            if (m_bInitialized)
            {
                return true;
            }
            if (!RetryPendingStops())
            {
                return false;
            }
            m_bInitialized = true;
            return true;
        }
        bool Shutdown() noexcept
        {
            m_bInitialized = false;
            return StopAll();
        }
        bool SetFocused(bool focused) noexcept
        {
            m_bFocused = focused;
            return focused ? RetryPendingStops() : StopAll();
        }
        bool SetVibration(uint8_t slot, float low, float high) noexcept
        {
            XInputMotorState motors;
            if (slot >= GamepadSlotCount || !m_bInitialized || !EncodeXInputVibration(low, high, motors))
            {
                return false;
            }
            const bool nonzero = motors.Low != 0 || motors.High != 0;
            if (nonzero && !m_bFocused)
            {
                return false;
            }
            if (m_Slots[slot].bStopPending)
            {
                if (!Send(slot, {}))
                {
                    return false;
                }
                if (!nonzero)
                {
                    return true;
                }
            }
            return Send(slot, motors);
        }
        bool RetryPendingStops() noexcept
        {
            bool success = true;
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                if (m_Slots[slot].bStopPending && !Send(slot, {}))
                {
                    success = false;
                }
            }
            return success;
        }
        bool HasPendingStop() const noexcept
        {
            for (const auto& slot : m_Slots)
            {
                if (slot.bStopPending)
                {
                    return true;
                }
            }
            return false;
        }
        bool MayBeActive(uint8_t slot) const noexcept
        {
            return slot < GamepadSlotCount && m_Slots[slot].bMayBeActive;
        }
        XInputWriteResult GetLastWriteResult(uint8_t slot) const noexcept
        {
            return slot < GamepadSlotCount ? m_Slots[slot].LastWrite : XInputWriteResult{};
        }
    private:
        bool StopAll() noexcept
        {
            bool success = true;
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                if ((m_Slots[slot].bMayBeActive || m_Slots[slot].bStopPending) && !Send(slot, {}))
                {
                    success = false;
                }
            }
            return success;
        }
        bool Send(uint8_t slot, const XInputMotorState& motors) noexcept
        {
            auto& value = m_Slots[slot];
            const bool nonzero = motors.Low != 0 || motors.High != 0;
            if (nonzero)
            {
                // APIが失敗を返しても一部出力された可能性を捨てない。
                value.bMayBeActive = true;
            }
            value.LastWrite = m_Api.WriteVibration(slot, motors);
            if (value.LastWrite.Accepted)
            {
                value.bMayBeActive = nonzero;
                value.bStopPending = false;
                return true;
            }
            value.bStopPending = true;
            return false;
        }
        struct SlotState
        {
            bool bMayBeActive = false;
            bool bStopPending = false;
            XInputWriteResult LastWrite;
        };
        IXInputApi& m_Api;
        SlotState m_Slots[GamepadSlotCount]{};
        bool m_bInitialized = false;
        bool m_bFocused = true;
    };
} // namespace NorvesLib::Core::Input
