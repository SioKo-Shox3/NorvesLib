#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Input/IInputController.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Input;
namespace
{
    GamepadState Pad(bool pressed = false)
    {
        GamepadState state;
        state.Connected = true;
        state.Buttons = pressed ? static_cast<uint16_t>(GamepadButton::A) : 0;
        return state;
    }
    struct ConsumingUI final : IInputController
    {
        InputSystem* System = nullptr;
        int PadCount = 0;
        bool OnGamepadButton(const GamepadButtonEvent&) override
        {
            ++PadCount;
            assert(System->GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
            return true;
        }
    };
    void SelectPad(InputSystem& system)
    {
        assert(system.InjectGamepadState(0,Pad(false)));
        assert(system.InjectGamepadState(0,Pad(true)));
        assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    }
}
int main()
{
    InputSystem system;
    InputRouter router;
    ConsumingUI ui;
    ui.System = &system;
    router.RegisterController(&ui,InputRouter::PriorityOverlay);
    system.SetRouter(&router);
    int notifications = 0;
    EInputDeviceKind notified = EInputDeviceKind::KeyboardMouse;
    system.OnActiveDeviceKindChanged().Add([&](EInputDeviceKind kind)
    {
        ++notifications;
        notified = kind;
        assert(kind == system.GetActiveDeviceKind());
        // 再入EndFrameは二重通知しない。
        system.EndFrame();
    });
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(system.BeginFrame(0));
    SelectPad(system);
    assert(ui.PadCount == 1 && notifications == 0);
    system.EndFrame();
    assert(notifications == 1 && notified == EInputDeviceKind::Gamepad);
    system.EndFrame();
    assert(notifications == 1);
    assert(system.BeginFrame(.1));
    system.InjectKeyEvent(KeyCode::A,InputAction::Pressed);
    system.EndFrame();
    assert(notifications == 1);
    assert(system.BeginFrame(.3));
    system.InjectKeyEvent(KeyCode::A,InputAction::Repeat);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    system.InjectKeyEvent(KeyCode::B,InputAction::Pressed);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    assert(notifications == 1);
    system.EndFrame();
    assert(notifications == 2);
    // operation reset後も物理heldは新しい活動にしない。
    assert(system.BeginFrame(1));
    router.UnregisterController(&ui);
    system.SetRouter(nullptr);
    system.ReleaseAll();
    assert(system.InjectGamepadState(0,Pad(true)));
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.EndFrame();
    assert(notifications == 2);
    SelectPad(system);
    system.EndFrame();
    assert(notifications == 3);
    assert(system.BeginFrame(2));
    system.InjectMouseMove(100,100);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    system.InjectMouseMove(500,500,false);
    system.InjectMouseMove(501,500,true);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    assert(system.InjectRawMouseDelta(1,0));
    system.InjectMouseMove(502,500,true);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    assert(system.InjectRawMouseDelta(1,0));
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.EndFrame();
    assert(notifications == 4);
    assert(system.BeginFrame(3));
    SelectPad(system);
    system.EndFrame();
    assert(system.BeginFrame(4));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(!system.InjectRawMouseDelta(nan,1));
    assert(!system.InjectMouseScrollAxes(nan,1));
    system.InjectCharEvent(0xD800);
    system.InjectMouseButton(MouseButton::Count,InputAction::Pressed,0,0);
    system.InjectKeyEvent(KeyCode::None,InputAction::Pressed);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    assert(system.InjectMouseScrollAxes(0,1));
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.EndFrame();
    assert(system.BeginFrame(5));
    SelectPad(system);
    system.EndFrame();
    assert(system.BeginFrame(6));
    system.SetInputFocused(false);
    system.ReleaseAll();
    system.InjectKeyEvent(KeyCode::B,InputAction::Pressed);
    assert(system.InjectRawMouseDelta(100,0));
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::Gamepad);
    system.SetInputFocused(true);
    assert(system.InjectGamepadState(0,Pad(true),EGamepadSampleMode::Baseline));
    system.InjectCharEvent('x');
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.EndFrame();
    // 不正frameは累積値/保留reset通知を変更しない。
    int resets = 0;
    system.OnInputResetEvent().Add([&]() { ++resets; });
    system.DeferReleaseAll();
    assert(system.InjectRawMouseDelta(2,3));
    for (double bad : {-1.0, 5.0, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    {
        assert(!system.CanBeginFrame(bad));
        assert(!system.BeginFrame(bad));
        assert(resets == 0 && system.GetState().GetMouseState().RawDeltaX == 2);
    }
    assert(system.BeginFrame(7));
    assert(resets == 1 && system.GetState().GetMouseState().RawDeltaX == 0);
    auto settings = system.GetDeviceActivitySettings();
    settings.MinimumSwitchSeconds = 0;
    assert(system.ConfigureDeviceActivity(settings));
    const int prior = notifications;
    SelectPad(system);
    system.InjectMouseButton(MouseButton::Left,InputAction::Pressed,0,0);
    assert(system.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    system.EndFrame();
    assert(notifications == prior);
    settings.MouseMoveThreshold = 0;
    assert(!system.ConfigureDeviceActivity(settings));
    assert(system.GetDeviceActivitySettings().MouseMoveThreshold == 2);
    // 初回frameより前はkind判定せず、背景sampleも通常活動にしない。
    InputSystem initial;
    assert(initial.InjectGamepadState(0,Pad(true)));
    assert(initial.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    assert(initial.BeginFrame(0));
    assert(initial.InjectGamepadState(0,Pad(true)));
    assert(initial.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    assert(initial.InjectGamepadState(0,Pad(false),EGamepadSampleMode::Background));
    assert(initial.GetActiveDeviceKind() == EInputDeviceKind::KeyboardMouse);
    SelectPad(initial);
    initial.EndFrame();
    std::cout << "ActiveDeviceKindIntegrationTest PASS\n";
    return 0;
}
