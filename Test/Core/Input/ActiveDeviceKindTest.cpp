#include "Input/ActiveDeviceKind.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Input;
namespace
{
    GamepadState Pad()
    {
        GamepadState value;
        value.Connected = true;
        return value;
    }
    void SelectPad(ActiveDeviceKindState& state)
    {
        auto previous = Pad(), current = previous;
        current.Buttons = static_cast<uint16_t>(GamepadButton::A);
        assert(state.ObserveGamepad(0,previous,current,EGamepadSampleMode::Live));
    }
    ActiveDeviceKindState ReadyPad()
    {
        ActiveDeviceKindState state;
        assert(state.BeginFrame(0));
        SelectPad(state);
        assert(state.BeginFrame(1));
        return state;
    }
}
int main()
{
    ActiveDeviceKindState state;
    assert(!state.HasTime() && state.GetKind() == EInputDeviceKind::KeyboardMouse);
    auto neutral = Pad(), pressed = neutral;
    pressed.Buttons = static_cast<uint16_t>(GamepadButton::A);
    assert(!state.ObserveGamepad(0,neutral,pressed,EGamepadSampleMode::Live));
    assert(state.BeginFrame(0));
    SelectPad(state);
    assert(!state.ObserveKey(KeyCode::A,InputAction::Pressed,false));
    assert(state.BeginFrame(.299));
    assert(!state.ObserveCharacter('a'));
    assert(state.BeginFrame(.3));
    assert(state.GetKind() == EInputDeviceKind::Gamepad);
    assert(state.ObserveKey(KeyCode::A,InputAction::Pressed,false));
    assert(!state.ObserveGamepad(0,neutral,pressed,EGamepadSampleMode::Live));
    assert(state.BeginFrame(1));
    // 抑制された入力を時刻だけで再生しない。
    assert(!state.ObserveGamepad(0,pressed,pressed,EGamepadSampleMode::Live));
    assert(state.GetKind() == EInputDeviceKind::KeyboardMouse);
    for (double invalid : {-1.0, .5, std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()})
    {
        assert(!state.BeginFrame(invalid));
    }
    assert(state.BeginFrame(1));
    assert(state.BeginFrame(std::numeric_limits<double>::max()));
    SelectPad(state);

    auto keys = ReadyPad();
    assert(!keys.ObserveKey(KeyCode::None,InputAction::Pressed,false));
    assert(!keys.ObserveKey(KeyCode::Count,InputAction::Pressed,false));
    assert(!keys.ObserveKey(KeyCode::A,InputAction::Repeat,false));
    assert(!keys.ObserveKey(KeyCode::A,InputAction::Released,false));
    assert(!keys.ObserveKey(KeyCode::A,InputAction::Pressed,true));
    assert(!keys.ObserveMouseButton(MouseButton::Count,InputAction::Pressed,false));
    assert(!keys.ObserveMouseButton(MouseButton::Left,InputAction::Pressed,true));
    assert(keys.ObserveMouseButton(MouseButton::Left,InputAction::Pressed,false));
    auto text = ReadyPad();
    assert(!text.ObserveCharacter(0));
    assert(!text.ObserveCharacter(0xD800));
    assert(!text.ObserveCharacter(0x110000));
    assert(text.ObserveCharacter(0x10FFFF));
    auto scroll = ReadyPad();
    assert(!scroll.ObserveMouseScroll(0,0));
    assert(!scroll.ObserveMouseScroll(1,std::numeric_limits<float>::infinity()));
    assert(scroll.ObserveMouseScroll(0,-.01f));

    auto mouse = ReadyPad();
    assert(!mouse.ObserveMouseMove(1,0,true));
    assert(!mouse.ObserveMouseMove(1,0,false));
    assert(!mouse.ObserveMouseMove(std::numeric_limits<float>::quiet_NaN(),0,true));
    assert(mouse.ObserveMouseMove(-1,0,true));
    auto frameMouse = ReadyPad();
    assert(!frameMouse.ObserveMouseMove(1,0,false));
    assert(frameMouse.BeginFrame(2));
    assert(!frameMouse.ObserveMouseMove(1,0,false));
    assert(frameMouse.ObserveMouseMove(0,1,false));
    auto diagonal = ReadyPad();
    assert(diagonal.ObserveMouseMove(1.5f,1.5f,true));
    auto focus = ReadyPad();
    assert(!focus.ObserveMouseMove(1,0,true));
    focus.SetFocused(false);
    assert(!focus.ObserveKey(KeyCode::A,InputAction::Pressed,false));
    assert(!focus.ObserveMouseMove(10,0,true));
    focus.SetFocused(true);
    assert(!focus.ObserveMouseMove(1,0,true));
    assert(focus.ObserveMouseMove(1,0,true));

    ActiveDeviceKindState analog;
    assert(analog.BeginFrame(0));
    auto held = neutral;
    held.Axes[0] = .5f;
    assert(!analog.ObserveGamepad(0,neutral,held,EGamepadSampleMode::Baseline));
    assert(!analog.ObserveGamepad(0,held,held,EGamepadSampleMode::Live));
    auto previous = held;
    for (int i = 1; i <= 3; ++i)
    {
        auto current = held;
        current.Axes[0] += .005f*i;
        assert(!analog.ObserveGamepad(0,previous,current,EGamepadSampleMode::Live));
        previous = current;
    }
    auto moved = held;
    moved.Axes[0] = .525f;
    auto invalid = moved;
    invalid.Axes[3] = 2;
    assert(!analog.ObserveGamepad(0,previous,invalid,EGamepadSampleMode::Live));
    assert(!analog.ObserveGamepad(4,previous,moved,EGamepadSampleMode::Live));
    assert(!analog.ObserveGamepad(0,previous,moved,static_cast<EGamepadSampleMode>(99)));
    assert(analog.ObserveGamepad(0,previous,moved,EGamepadSampleMode::Live));
    // 左右スティック・トリガー、各スロットを独立に評価する。
    for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
    {
        for (uint8_t axis = 0; axis < 4; ++axis)
        {
            ActiveDeviceKindState axes;
            assert(axes.BeginFrame(0));
            auto noise = neutral;
            noise.Axes[axis] = .1f;
            assert(!axes.ObserveGamepad(slot,neutral,noise,EGamepadSampleMode::Live));
            auto active = noise;
            active.Axes[axis] = -.8f;
            assert(axes.ObserveGamepad(slot,noise,active,EGamepadSampleMode::Live));
        }
        for (uint8_t trigger = 0; trigger < 2; ++trigger)
        {
            ActiveDeviceKindState triggers;
            assert(triggers.BeginFrame(0));
            auto low = neutral;
            low.Triggers[trigger] = .1f;
            assert(!triggers.ObserveGamepad(slot,neutral,low,EGamepadSampleMode::Live));
            auto active = low;
            active.Triggers[trigger] = .5f;
            assert(triggers.ObserveGamepad(slot,low,active,EGamepadSampleMode::Live));
        }
    }
    ActiveDeviceKindState slots;
    assert(slots.BeginFrame(0));
    assert(!slots.ObserveGamepad(0,neutral,held,EGamepadSampleMode::Baseline));
    assert(!slots.ObserveGamepad(1,neutral,neutral,EGamepadSampleMode::Baseline));
    assert(!slots.ObserveGamepad(0,held,held,EGamepadSampleMode::Live));
    assert(slots.ObserveGamepad(1,neutral,held,EGamepadSampleMode::Live));
    ActiveDeviceKindState sync;
    assert(sync.BeginFrame(0));
    assert(!sync.ObserveGamepad(0,neutral,pressed,EGamepadSampleMode::Background));
    assert(!sync.ObserveGamepad(0,pressed,neutral,EGamepadSampleMode::Live));
    assert(!sync.ObserveGamepad(0,neutral,GamepadState{},EGamepadSampleMode::Live));
    assert(!sync.ObserveGamepad(0,GamepadState{},pressed,EGamepadSampleMode::Baseline));
    assert(!sync.ObserveGamepad(0,pressed,pressed,EGamepadSampleMode::Live));
    sync.SetFocused(false);
    assert(!sync.ObserveGamepad(0,neutral,held,EGamepadSampleMode::Live));
    sync.SetFocused(true);
    assert(!sync.ObserveGamepad(0,neutral,held,EGamepadSampleMode::Baseline));
    assert(!sync.ObserveGamepad(0,held,held,EGamepadSampleMode::Live));
    assert(sync.GetKind() == EInputDeviceKind::KeyboardMouse);

    auto configured = ReadyPad();
    assert(!configured.ObserveMouseMove(1,0,true));
    auto settings = configured.GetSettings();
    settings.MouseMoveThreshold = 0;
    assert(!configured.Configure(settings));
    assert(configured.ObserveMouseMove(1,0,true));
    settings.MouseMoveThreshold = 2;
    settings.MinimumSwitchSeconds = 0;
    assert(configured.Configure(settings));
    SelectPad(configured);
    assert(!configured.ObserveMouseMove(1,0,true));
    assert(configured.Configure(settings));
    assert(!configured.ObserveMouseMove(1,0,true));
    assert(configured.ObserveMouseMove(1,0,true));
    settings.AnalogChangeThreshold = std::numeric_limits<float>::quiet_NaN();
    assert(!configured.Configure(settings));
    // 全設定の範囲拒否が旧設定を壊さない。
    const auto validSettings = configured.GetSettings();
    for (int field = 0; field < 6; ++field)
    {
        for (float bad : {-1.f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            auto rejected = validSettings;
            switch (field)
            {
            case 0: rejected.MinimumSwitchSeconds = bad; break;
            case 1: rejected.MouseMoveThreshold = bad; break;
            case 2: rejected.LeftStickDeadZone = bad; break;
            case 3: rejected.RightStickDeadZone = bad; break;
            case 4: rejected.TriggerThreshold = bad; break;
            case 5: rejected.AnalogChangeThreshold = bad; break;
            }
            assert(!configured.Configure(rejected));
            assert(configured.GetSettings().MinimumSwitchSeconds == validSettings.MinimumSwitchSeconds);
            assert(configured.GetSettings().AnalogChangeThreshold == validSettings.AnalogChangeThreshold);
        }
    }
    for (int field = 0; field < 4; ++field)
    {
        auto rejected = validSettings;
        switch (field)
        {
        case 0: rejected.LeftStickDeadZone = 1; break;
        case 1: rejected.RightStickDeadZone = 1; break;
        case 2: rejected.TriggerThreshold = 1; break;
        case 3: rejected.AnalogChangeThreshold = 0; break;
        }
        assert(!configured.Configure(rejected));
    }
    std::cout << "ActiveDeviceKindTest PASS\n";
    return 0;
}
