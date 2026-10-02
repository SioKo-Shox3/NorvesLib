#include "Input/InputArmedState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Input;

int main()
{
    InputState raw;
    InputArmedState armed;
    for (uint32_t value=1; value<static_cast<uint32_t>(KeyCode::Count); ++value)
    {
        const auto key=static_cast<KeyCode>(value);
        assert(raw.GetKeyReleaseSerial(key)==0);
        raw.SetKeyState(key,true); // UIがPressedを消費し、callbackが来ない場合
        assert(!armed.IsKeyArmed(key,raw));
        armed.OnKey({key,InputAction::Repeat},raw);
        assert(!armed.IsKeyArmed(key,raw));
        armed.OnKey({key,InputAction::Pressed},raw);
        assert(armed.IsKeyArmed(key,raw));
        raw.SetKeyState(key,true);
        assert(raw.GetKeyReleaseSerial(key)==0);
        raw.BeginFrame();
        assert(armed.IsKeyArmed(key,raw));
        raw.SetKeyState(key,false); // releaseと再pressをUIが両方消費
        raw.SetKeyState(key,true);
        assert(raw.IsKeyDown(key) && raw.GetKeyReleaseSerial(key)==1);
        assert(!armed.IsKeyArmed(key,raw));
        armed.OnKey({key,InputAction::Repeat},raw);
        assert(!armed.IsKeyArmed(key,raw));
        armed.Reconcile(raw);
        assert(!armed.IsKeyArmed(key,raw));
        armed.OnKey({key,InputAction::Pressed},raw); // 到達した新規Pressだけが再許可
        assert(armed.IsKeyArmed(key,raw));
        raw.SetKeyState(key,false);
        armed.OnKey({key,InputAction::Released},raw);
        raw.SetKeyState(key,false);
        assert(raw.GetKeyReleaseSerial(key)==2 && !armed.IsKeyArmed(key,raw));
        raw.BeginFrame();
        assert(raw.GetKeyReleaseSerial(key)==2);
    }
    for (uint32_t value=0; value<static_cast<uint32_t>(MouseButton::Count); ++value)
    {
        const auto button=static_cast<MouseButton>(value);
        MouseButtonEvent event;
        event.Button=button;
        raw.SetMouseButtonState(button,true);
        assert(!armed.IsMouseButtonArmed(button,raw));
        event.Action=InputAction::Repeat; armed.OnMouseButton(event,raw);
        assert(!armed.IsMouseButtonArmed(button,raw));
        event.Action=InputAction::Pressed; armed.OnMouseButton(event,raw);
        assert(armed.IsMouseButtonArmed(button,raw));
        raw.SetMouseButtonState(button,false);raw.SetMouseButtonState(button,true);
        assert(raw.GetMouseButtonReleaseSerial(button)==1 && !armed.IsMouseButtonArmed(button,raw));
        armed.OnMouseButton(event,raw);
        assert(armed.IsMouseButtonArmed(button,raw));
        raw.ReleaseAll();
        assert(raw.GetMouseButtonReleaseSerial(button)==2 && !armed.IsMouseButtonArmed(button,raw));
        raw.ReleaseAll(); raw.BeginFrame();
        assert(raw.GetMouseButtonReleaseSerial(button)==2);
    }

    // 修飾キーの物理downを、UIを通過した許可と取り違えない。
    raw.SetKeyState(KeyCode::LeftShift,true);
    raw.SetKeyState(KeyCode::LeftCtrl,true);
    raw.SetKeyState(KeyCode::LeftAlt,true);
    assert(raw.IsShiftDown() && raw.IsCtrlDown() && raw.IsAltDown());
    assert(!armed.IsShiftArmed(raw) && !armed.IsCtrlArmed(raw) && !armed.IsAltArmed(raw));
    armed.OnKey({KeyCode::LeftShift,InputAction::Pressed},raw);
    armed.OnKey({KeyCode::LeftCtrl,InputAction::Pressed},raw);
    armed.OnKey({KeyCode::LeftAlt,InputAction::Pressed},raw);
    assert(armed.IsShiftArmed(raw) && armed.IsCtrlArmed(raw) && armed.IsAltArmed(raw));
    const auto generation=raw.GetKeyReleaseSerial(KeyCode::LeftShift);
    raw.ReleaseAll();
    assert(raw.GetKeyReleaseSerial(KeyCode::LeftShift)==generation+1);
    raw.SetKeyState(KeyCode::LeftShift,true);
    assert(!armed.IsShiftArmed(raw));
    armed.OnKey({KeyCode::LeftShift,InputAction::Pressed},raw);
    assert(armed.IsShiftArmed(raw));
    armed.Reset();
    armed.OnKey({KeyCode::LeftShift,InputAction::Repeat},raw);
    assert(!armed.IsShiftArmed(raw));
    raw.SetKeyState(KeyCode::RightShift,true);
    raw.SetKeyState(KeyCode::RightCtrl,true);
    raw.SetKeyState(KeyCode::RightAlt,true);
    armed.OnKey({KeyCode::RightShift,InputAction::Pressed},raw);
    armed.OnKey({KeyCode::RightCtrl,InputAction::Pressed},raw);
    armed.OnKey({KeyCode::RightAlt,InputAction::Pressed},raw);
    assert(armed.IsShiftArmed(raw) && armed.IsCtrlArmed(raw) && armed.IsAltArmed(raw));

    for (uint32_t invalid : {0u,static_cast<uint32_t>(KeyCode::Count),65535u})
    {
        const auto key=static_cast<KeyCode>(invalid);
        raw.SetKeyState(key,true);
        armed.OnKey({key,InputAction::Pressed},raw);
        assert(!armed.IsKeyArmed(key,raw));
        if(invalid!=0) assert(raw.GetKeyReleaseSerial(key)==0);
    }
    for (uint32_t invalid : {static_cast<uint32_t>(MouseButton::Count),255u})
    {
        const auto button=static_cast<MouseButton>(invalid);
        raw.SetMouseButtonState(button,true);
        MouseButtonEvent event;event.Button=button;
        armed.OnMouseButton(event,raw);
        assert(!armed.IsMouseButtonArmed(button,raw) && raw.GetMouseButtonReleaseSerial(button)==0);
    }
    raw.SetKeyState(KeyCode::A,false);
    armed.OnKey({KeyCode::A,InputAction::Pressed},raw); // 正本がupなら許可しない。
    raw.SetKeyState(KeyCode::A,true);
    assert(!armed.IsKeyArmed(KeyCode::A,raw));
    armed.OnKey({KeyCode::A,static_cast<InputAction>(255)},raw);
    assert(!armed.IsKeyArmed(KeyCode::A,raw));
    std::cout << "InputArmedStateTest passed\n";
    return 0;
}
