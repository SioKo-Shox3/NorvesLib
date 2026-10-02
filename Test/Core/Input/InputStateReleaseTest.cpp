#include "Input/InputState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Input;

int main()
{
    InputState state;
    state.ReleaseAll();
    for (uint32_t key = 0; key < static_cast<uint32_t>(KeyCode::Count); ++key)
    {
        const auto code = static_cast<KeyCode>(key);
        assert(!state.IsKeyDown(code) && !state.IsKeyReleased(code));
        state.SetKeyState(code, true);
        assert(state.IsKeyPressed(code));
    }
    for (uint32_t button = 0; button < static_cast<uint32_t>(MouseButton::Count); ++button)
    {
        const auto code = static_cast<MouseButton>(button);
        assert(!state.IsMouseButtonDown(code) && !state.IsMouseButtonReleased(code));
        state.SetMouseButtonState(code, true);
        assert(state.IsMouseButtonPressed(code));
    }
    // 当frameの押下後でも解除edgeを残し、二回呼んでも延長しない。
    state.ReleaseAll();
    state.ReleaseAll();
    for (uint32_t key = 0; key < static_cast<uint32_t>(KeyCode::Count); ++key)
    {
        const auto code = static_cast<KeyCode>(key);
        assert(!state.IsKeyDown(code) && !state.IsKeyPressed(code) && state.IsKeyReleased(code));
    }
    for (uint32_t button = 0; button < static_cast<uint32_t>(MouseButton::Count); ++button)
    {
        const auto code = static_cast<MouseButton>(button);
        assert(!state.IsMouseButtonDown(code) && !state.IsMouseButtonPressed(code) && state.IsMouseButtonReleased(code));
    }
    assert(!state.IsAltDown() && !state.IsCtrlDown() && !state.IsShiftDown());
    state.BeginFrame();
    state.ReleaseAll();
    for (uint32_t key = 0; key < static_cast<uint32_t>(KeyCode::Count); ++key)
        assert(!state.IsKeyReleased(static_cast<KeyCode>(key)));
    for (uint32_t button = 0; button < static_cast<uint32_t>(MouseButton::Count); ++button)
        assert(!state.IsMouseButtonReleased(static_cast<MouseButton>(button)));

    // 前frameからのheld、解除、同frame再押下。Pressedは既存frame比較のまま。
    state.SetKeyState(KeyCode::A, true);
    state.SetMouseButtonState(MouseButton::Left, true);
    state.BeginFrame();
    assert(!state.IsKeyPressed(KeyCode::A) && !state.IsMouseButtonPressed(MouseButton::Left));
    state.ReleaseAll();
    state.SetKeyState(KeyCode::A, true);
    state.SetMouseButtonState(MouseButton::Left, true);
    assert(state.IsKeyDown(KeyCode::A) && state.IsKeyReleased(KeyCode::A));
    assert(state.IsMouseButtonDown(MouseButton::Left) && state.IsMouseButtonReleased(MouseButton::Left));
    assert(!state.IsKeyPressed(KeyCode::A) && !state.IsMouseButtonPressed(MouseButton::Left));
    state.BeginFrame();
    assert(state.IsKeyDown(KeyCode::A) && !state.IsKeyReleased(KeyCode::A));
    assert(state.IsMouseButtonDown(MouseButton::Left) && !state.IsMouseButtonReleased(MouseButton::Left));
    state.SetKeyState(KeyCode::A, false);
    state.SetMouseButtonState(MouseButton::Left, false);
    assert(state.IsKeyReleased(KeyCode::A) && state.IsMouseButtonReleased(MouseButton::Left));
    state.BeginFrame();
    assert(!state.IsKeyReleased(KeyCode::A) && !state.IsMouseButtonReleased(MouseButton::Left));

    state.SetMousePosition(10, 20);
    state.SetMousePosition(30, 25);
    state.AddMouseScroll(3);
    assert(state.GetMouseState().DeltaX == 20 && state.GetMouseState().DeltaY == 5);
    state.ReleaseAll();
    assert(state.GetMouseState().DeltaX == 0 && state.GetMouseState().DeltaY == 0 && state.GetMouseState().ScrollDelta == 0);
    assert(state.GetMouseState().PositionX == 30 && state.GetMouseState().PositionY == 25);
    state.SetMousePosition(1000, -500);
    assert(state.GetMouseState().DeltaX == 0 && state.GetMouseState().DeltaY == 0);
    state.SetMousePosition(1003, -502);
    assert(state.GetMouseState().DeltaX == 3 && state.GetMouseState().DeltaY == -2);
    state.BeginFrame();
    state.SetMousePosition(1004, -498);
    assert(state.GetMouseState().DeltaX == 1 && state.GetMouseState().DeltaY == 4);

    for (uint32_t invalid : {static_cast<uint32_t>(KeyCode::Count), 65535u})
    {
        const auto code = static_cast<KeyCode>(invalid);
        state.SetKeyState(code, true);
        assert(!state.IsKeyDown(code) && !state.IsKeyPressed(code) && !state.IsKeyReleased(code));
    }
    for (uint32_t invalid : {static_cast<uint32_t>(MouseButton::Count), 255u})
    {
        const auto code = static_cast<MouseButton>(invalid);
        state.SetMouseButtonState(code, true);
        assert(!state.IsMouseButtonDown(code) && !state.IsMouseButtonPressed(code) && !state.IsMouseButtonReleased(code));
    }
    std::cout << "InputStateReleaseTest passed\n";
    return 0;
}
