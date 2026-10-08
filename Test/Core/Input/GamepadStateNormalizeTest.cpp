// XInput整数値の全範囲を使って変換の境界を試験する。native APIは呼ばない。
#include "Core/Private/Platform/Windows/XInputStateConversion.h"
#include "Input/InputState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace NorvesLib::Core::Input;
namespace
{
    void AxesAndTriggers()
    {
        XInputRawGamepadState raw;
        float previous = -2;
        for (int value = -32768; value <= 32767; ++value)
        {
            raw.LeftX = static_cast<int16_t>(value);
            const auto state = NormalizeXInputGamepadState(raw);
            const float axis = state.Axes[static_cast<uint8_t>(GamepadAxis::LeftX)];
            assert(IsValidGamepadState(state));
            assert(axis >= -1 && axis <= 1 && axis > previous);
            assert((value < 0 && axis < 0) || (value == 0 && axis == 0) || (value > 0 && axis > 0));
            previous = axis;
        }
        assert(NormalizeXInputAxis(-32768) == -1 && NormalizeXInputAxis(32767) == 1);
        assert(NormalizeXInputAxis(-1) < 0 && NormalizeXInputAxis(1) > 0); // deadzoneを二重適用しない。
        raw.LeftX = -32768; raw.LeftY = 32767; raw.RightX = -16384; raw.RightY = 8192;
        raw.LeftTrigger = 0; raw.RightTrigger = 255;
        auto state = NormalizeXInputGamepadState(raw);
        assert(state.Axes[0] == -1 && state.Axes[1] == 1 && state.Axes[2] == -0.5f && state.Axes[3] == 8192.0f / 32767.0f);
        assert(state.Triggers[0] == 0 && state.Triggers[1] == 1);
        previous = -1;
        for (int value = 0; value <= 255; ++value)
        {
            raw.LeftTrigger = static_cast<uint8_t>(value);
            raw.RightTrigger = static_cast<uint8_t>(255 - value);
            state = NormalizeXInputGamepadState(raw);
            assert(IsValidGamepadState(state));
            assert(state.Triggers[0] >= 0 && state.Triggers[0] <= 1 && state.Triggers[0] > previous);
            assert(std::abs(state.Triggers[0] + state.Triggers[1] - 1) < 1e-6f);
            previous = state.Triggers[0];
        }
    }
    void ButtonsPacketAndState()
    {
        XInputRawGamepadState raw;
        raw.Buttons = 0xffff;
        raw.PacketNumber = 0xffffffff;
        auto state = NormalizeXInputGamepadState(raw);
        assert(state.Connected && state.Buttons == AllGamepadButtons && state.PacketNumber == 0xffffffff);
        InputState input;
        assert(input.SetGamepadState(3, state));
        assert(input.GetLastGamepadSample(3).Buttons == AllGamepadButtons && input.GetGamepadSampleSerial(3) == 1);
        assert(input.IsGamepadButtonPressed(3, GamepadButton::A));
        for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1)
        {
            raw.Buttons = static_cast<uint16_t>(bit);
            state = NormalizeXInputGamepadState(raw);
            assert(state.Buttons == (bit & AllGamepadButtons));
            assert(IsValidGamepadState(state));
        }
        raw.Buttons = static_cast<uint16_t>(~AllGamepadButtons);
        raw.PacketNumber = 0;
        state = NormalizeXInputGamepadState(raw);
        assert(state.Buttons == 0 && state.PacketNumber == 0);
        assert(input.SetGamepadState(3, state));
        assert(input.IsGamepadButtonReleased(3, GamepadButton::A));
    }
    void Motors()
    {
        XInputMotorState motors{11, 22};
        assert(EncodeXInputVibration(0, 1, motors) && motors.Low == 0 && motors.High == 65535);
        assert(EncodeXInputVibration(0.5f, 0, motors) && motors.Low == 32768 && motors.High == 0);
        uint16_t previous = 0;
        for (int i = 0; i <= 1000; ++i)
        {
            const float value = static_cast<float>(i) / 1000;
            assert(EncodeXInputVibration(value, 1 - value, motors));
            assert(motors.Low >= previous);
            assert(std::abs(static_cast<double>(motors.Low) / 65535 - value) <= 0.50001 / 65535);
            assert(std::abs(static_cast<double>(motors.High) / 65535 - (1 - value)) <= 0.50001 / 65535);
            previous = motors.Low;
        }
        const float invalid[] = {-0.01f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        for (float value : invalid)
        {
            motors = {11, 22};
            assert(!EncodeXInputVibration(value, 0, motors));
            assert(motors.Low == 11 && motors.High == 22);
            assert(!EncodeXInputVibration(0, value, motors));
            assert(motors.Low == 11 && motors.High == 22);
        }
        assert(EncodeXInputVibration(-0.0f, 0, motors) && motors.Low == 0 && motors.High == 0);
    }
}
int main()
{
    AxesAndTriggers();
    ButtonsPacketAndState();
    Motors();
    std::puts("GamepadStateNormalizeTest passed");
    return 0;
}
