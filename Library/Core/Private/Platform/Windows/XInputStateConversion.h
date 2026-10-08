#pragma once

#include "Input/GamepadTypes.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Input
{
    // XInputGetState成功後の値だけを渡す。native構造体のABI/packingには依存しない。
    // https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad
    struct XInputRawGamepadState
    {
        uint32_t PacketNumber = 0;
        uint16_t Buttons = 0;
        uint8_t LeftTrigger = 0;
        uint8_t RightTrigger = 0;
        int16_t LeftX = 0;
        int16_t LeftY = 0;
        int16_t RightX = 0;
        int16_t RightY = 0;
    };
    inline float NormalizeXInputAxis(int16_t value)
    {
        // -32768を整数のまま反転せず、左右の端点をそれぞれ±1へ合わせる。
        return value < 0 ? static_cast<float>(value) / 32768.0f : static_cast<float>(value) / 32767.0f;
    }
    inline GamepadState NormalizeXInputGamepadState(const XInputRawGamepadState& raw)
    {
        GamepadState state;
        state.Connected = true;
        state.PacketNumber = raw.PacketNumber;
        // XInputの未定義bitは値が不定なので、エンジンの14ボタンだけを採用する。
        state.Buttons = static_cast<uint16_t>(raw.Buttons & AllGamepadButtons);
        state.Axes[static_cast<uint8_t>(GamepadAxis::LeftX)] = NormalizeXInputAxis(raw.LeftX);
        state.Axes[static_cast<uint8_t>(GamepadAxis::LeftY)] = NormalizeXInputAxis(raw.LeftY);
        state.Axes[static_cast<uint8_t>(GamepadAxis::RightX)] = NormalizeXInputAxis(raw.RightX);
        state.Axes[static_cast<uint8_t>(GamepadAxis::RightY)] = NormalizeXInputAxis(raw.RightY);
        state.Triggers[static_cast<uint8_t>(GamepadTrigger::Left)] = static_cast<float>(raw.LeftTrigger) / 255.0f;
        state.Triggers[static_cast<uint8_t>(GamepadTrigger::Right)] = static_cast<float>(raw.RightTrigger) / 255.0f;
        // 応答曲線/deadzoneはInputMapperへ残す。
        return state;
    }
    struct XInputMotorState
    {
        uint16_t Low = 0;
        uint16_t High = 0;
    };
    // 左右を両方検証してからcommitし、失敗時はresultを保持する。
    inline bool EncodeXInputVibration(float low, float high, XInputMotorState& result)
    {
        if (!std::isfinite(low) || !std::isfinite(high) || low < 0 || low > 1 || high < 0 || high > 1)
        {
            return false;
        }
        XInputMotorState candidate;
        candidate.Low = static_cast<uint16_t>(static_cast<double>(low) * 65535.0 + 0.5);
        candidate.High = static_cast<uint16_t>(static_cast<double>(high) * 65535.0 + 0.5);
        result = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Input
