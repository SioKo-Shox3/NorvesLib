#include "Platform/PlatformInputDevices.h"
#include "XInputDevice.h"
#include <Windows.h>
#include <Xinput.h>

namespace NorvesLib::Core::Input
{
    namespace
    {
        // SDK構造体のlayoutを独自型へ流用せず、成功時だけ全fieldをコピーする。
        class WindowsXInputApi final : public IXInputApi
        {
        public:
            XInputWriteResult WriteVibration(uint8_t slot, const XInputMotorState& motors) noexcept override
            {
                if (slot >= GamepadSlotCount)
                {
                    return {false, ERROR_BAD_ARGUMENTS};
                }
                XINPUT_VIBRATION native{};
                native.wLeftMotorSpeed = motors.Low;
                native.wRightMotorSpeed = motors.High;
                const DWORD result = ::XInputSetState(slot, &native);
                return {result == ERROR_SUCCESS, result};
            }
            XInputReadResult ReadState(uint8_t slot, XInputRawGamepadState& raw) noexcept override
            {
                if (slot >= GamepadSlotCount)
                {
                    return {EXInputReadStatus::Error, ERROR_BAD_ARGUMENTS};
                }
                XINPUT_STATE native{};
                const DWORD result = ::XInputGetState(slot, &native);
                if (result == ERROR_DEVICE_NOT_CONNECTED)
                {
                    return {EXInputReadStatus::Disconnected, result};
                }
                if (result != ERROR_SUCCESS)
                {
                    return {EXInputReadStatus::Error, result};
                }
                XInputRawGamepadState value;
                value.PacketNumber = native.dwPacketNumber;
                value.Buttons = native.Gamepad.wButtons;
                value.LeftTrigger = native.Gamepad.bLeftTrigger;
                value.RightTrigger = native.Gamepad.bRightTrigger;
                value.LeftX = native.Gamepad.sThumbLX;
                value.LeftY = native.Gamepad.sThumbLY;
                value.RightX = native.Gamepad.sThumbRX;
                value.RightY = native.Gamepad.sThumbRY;
                raw = value;
                return {EXInputReadStatus::Connected, ERROR_SUCCESS};
            }
        };
    }
}

namespace NorvesLib::Core::Platform
{
    Container::TUniquePtr<Input::IInputDevice> CreateGamepadDevice()
    {
        return Input::XInputDevice::Create(Container::MakeUnique<Input::WindowsXInputApi>());
    }
} // namespace NorvesLib::Core::Platform
