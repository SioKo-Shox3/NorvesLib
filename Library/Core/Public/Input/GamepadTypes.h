#pragma once

#include "Input/InputTypes.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Input
{
    inline constexpr uint8_t GamepadSlotCount = 4;

    enum class GamepadButton : uint16_t
    {
        None=0,
        DpadUp=0x0001, DpadDown=0x0002, DpadLeft=0x0004, DpadRight=0x0008,
        Start=0x0010, Back=0x0020, LeftThumb=0x0040, RightThumb=0x0080,
        LeftShoulder=0x0100, RightShoulder=0x0200,
        A=0x1000, B=0x2000, X=0x4000, Y=0x8000
    };
    enum class GamepadAxis : uint8_t { LeftX, LeftY, RightX, RightY, Count };
    enum class GamepadTrigger : uint8_t { Left, Right, Count };
    struct GamepadButtonEvent
    {
        uint8_t Slot = 0;
        GamepadButton Button = GamepadButton::None;
        InputAction Action = InputAction::Pressed;
    };
    struct GamepadConnectionEvent
    {
        uint8_t Slot = 0;
        bool Connected = false;
    };
    inline constexpr uint16_t AllGamepadButtons = 0xF3FF;

    inline constexpr bool IsValidGamepadButton(uint16_t code)
    {
        return code != 0 && (code & (code-1)) == 0 && (code & ~AllGamepadButtons) == 0;
    }

    // backendの正規化後の値。deadzoneはMapper側で一度だけ適用する。
    struct GamepadState
    {
        bool Connected = false;
        uint16_t Buttons = 0;
        float Axes[static_cast<uint8_t>(GamepadAxis::Count)]{};
        float Triggers[static_cast<uint8_t>(GamepadTrigger::Count)]{};
        uint32_t PacketNumber = 0;
    };

    inline bool IsValidGamepadState(const GamepadState& state)
    {
        if ((state.Buttons & ~AllGamepadButtons) != 0) return false;
        if (!state.Connected && state.Buttons != 0) return false;
        for (float value : state.Axes)
            if (!std::isfinite(value) || value < -1 || value > 1 || (!state.Connected && value != 0)) return false;
        for (float value : state.Triggers)
            if (!std::isfinite(value) || value < 0 || value > 1 || (!state.Connected && value != 0)) return false;
        return true;
    }
} // namespace NorvesLib::Core::Input
