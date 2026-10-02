// debug overlayの要求と適用、F1 repeat除外をOS非依存で試験する。
#include "Input/InputDebugOverlayState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using namespace NorvesLib::Core::Input;
int main()
{
    InputDebugOverlayState state;
    assert(!state.IsEnabled() && !state.IsActive() && !state.IsMasking());
    assert(!state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(!state.ApplyRequested());
    state.SetEnabled(true);
    assert(!state.OnKey({KeyCode::A, InputAction::Pressed}));
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(state.IsMasking() && !state.IsActive());
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(state.OnKey({KeyCode::F1, InputAction::Repeat}));
    assert(state.ApplyRequested() && state.IsActive());
    assert(!state.ApplyRequested());
    assert(state.OnKey({KeyCode::F1, InputAction::Released}));
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(state.IsMasking() && state.IsActive());
    assert(state.OnKey({KeyCode::F1, InputAction::Repeat}));
    assert(state.ApplyRequested() && !state.IsMasking());
    assert(state.OnKey({KeyCode::F1, InputAction::Released}));
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(state.ApplyRequested() && state.IsActive());
    state.ResetKeys();
    assert(state.IsActive());
    assert(state.OnKey({KeyCode::F1, InputAction::Repeat}));
    assert(!state.ApplyRequested() && state.IsActive());
    state.SetEnabled(false);
    assert(!state.IsEnabled() && state.IsMasking());
    assert(state.ApplyRequested() && !state.IsMasking());
    state.SetEnabled(true);
    assert(state.OnKey({KeyCode::F1, InputAction::Released}));
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    state.Reset();
    assert(state.IsEnabled() && !state.IsActive() && !state.IsMasking());
    assert(!state.ApplyRequested());
    assert(state.OnKey({KeyCode::F1, InputAction::Pressed}));
    assert(state.IsMasking());
    state.SetEnabled(false);
    assert(!state.IsMasking() && !state.ApplyRequested());
    std::puts("InputDebugOverlayStateTest passed");
    return 0;
}
