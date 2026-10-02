#include "Core/Private/Platform/Windows/XInputDevice.h"
#include "Input/InputSystem.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <type_traits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
namespace
{
    class FakeApi final : public IXInputApi
    {
    public:
        explicit FakeApi(int& destroyed) : Destroyed(destroyed)
        {
        }
        ~FakeApi() override
        {
            ++Destroyed;
        }
        XInputReadResult ReadState(uint8_t slot, XInputRawGamepadState& raw) noexcept override
        {
            ++Calls;
            if (slot != 0)
            {
                return {};
            }
            raw = Value;
            return Result;
        }
        int& Destroyed;
        int Calls = 0;
        XInputRawGamepadState Value{};
        XInputReadResult Result{EXInputReadStatus::Connected, 0};
    };
    class LegacyDevice final : public IInputDevice
    {
    public:
        bool Initialize() override
        {
            return true;
        }
        void Shutdown() override
        {
        }
        void PollEvents(InputSystem&) override
        {
            ++Calls;
        }
        int Calls = 0;
    };
}
int main()
{
    static_assert(!std::is_copy_constructible_v<XInputDevice>);
    static_assert(!std::is_move_constructible_v<XInputDevice>);
    assert(!XInputDevice::Create({}));
    InputSystem system;
    LegacyDevice legacy;
    IInputDevice& legacyBase = legacy;
    legacyBase.SetFocused(false);
    assert(!legacyBase.ProvidesGamepadState());
    assert(legacyBase.PollEvents(system, 0));
    assert(legacy.Calls == 1);

    int destroyed = 0;
    auto api = Container::MakeUnique<FakeApi>(destroyed);
    auto* observer = api.get();
    observer->Value.Buttons = static_cast<uint16_t>(GamepadButton::A);
    auto device = XInputDevice::Create(std::move(api));
    assert(!api && device && device->ProvidesGamepadState());
    assert(observer->Calls == 0);
    assert(!device->PollEvents(system, 0));
    assert(observer->Calls == 0);
    assert(device->Initialize());
    assert(device->PollEvents(system, 0));
    assert(system.GetState().IsGamepadButtonDown(0, GamepadButton::A));
    assert(!system.GetState().IsGamepadButtonPressed(0, GamepadButton::A));
    assert(system.GetState().GetGamepadSampleSerial(0) == 1);
    assert(device->Initialize());
    system.BeginFrame();
    observer->Value.Buttons = 0;
    assert(device->PollEvents(system, .1));
    assert(system.GetState().IsGamepadButtonReleased(0, GamepadButton::A));
    system.BeginFrame();
    observer->Value.Buttons = static_cast<uint16_t>(GamepadButton::A);
    assert(device->PollEvents(system, .2));
    assert(system.GetState().IsGamepadButtonPressed(0, GamepadButton::A));

    // 実ownerはMapper取消も先に実施する。ここでは正本と配送modeの接続を検証する。
    system.ReleaseAll();
    device->SetFocused(false);
    assert(device->PollEvents(system, .3));
    assert(!system.GetState().IsGamepadButtonDown(0, GamepadButton::A));
    assert(system.GetState().GetLastGamepadSample(0).Buttons != 0);
    device->SetFocused(true);
    assert(device->PollEvents(system, .4));
    assert(system.GetState().IsGamepadButtonDown(0, GamepadButton::A));
    assert(!system.GetState().IsGamepadButtonPressed(0, GamepadButton::A));

    observer->Result = {EXInputReadStatus::Error, 5};
    assert(!device->PollEvents(system, .5));
    assert(!system.GetState().GetGamepadState(0).Connected);
    assert(system.GetState().IsGamepadButtonReleased(0, GamepadButton::A));
    const int callsBeforeStop = observer->Calls;
    device->SetFocused(false);
    device->Shutdown();
    device->Shutdown();
    assert(!device->PollEvents(system, 1));
    assert(observer->Calls == callsBeforeStop && destroyed == 0);
    assert(device->Initialize());
    observer->Result = {EXInputReadStatus::Connected, 0};
    assert(device->PollEvents(system, 0));
    assert(!system.GetState().IsGamepadButtonDown(0, GamepadButton::A));
    assert(system.GetState().GetLastGamepadSample(0).Buttons != 0);
    device.reset();
    assert(destroyed == 1);
    std::cout << "XInputDeviceIntegrationTest passed\n";
    return 0;
}
