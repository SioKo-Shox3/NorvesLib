#include "Input/InputState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <iostream>
using namespace NorvesLib::Core::Input;
int main()
{
    InputState state;state.SetKeyState(KeyCode::W,true);state.SetMouseButtonState(MouseButton::Left,true);
    state.SetMousePosition(0,0);state.SetMousePosition(12,-9);
    assert(state.AddRawMouseDelta(3,4) && state.AddMouseScrollAxes(5,6));
    const auto serial=state.GetMouseButtonReleaseSerial(MouseButton::Left);
    state.ResetAbsoluteMouseTracking();
    auto mouse=state.GetMouseState();
    assert(mouse.PositionX==12 && mouse.PositionY==-9 && mouse.DeltaX==0 && mouse.DeltaY==0);
    assert(mouse.RawDeltaX==3 && mouse.RawDeltaY==4 && mouse.ScrollDelta==5 && mouse.HorizontalScrollDelta==6);
    assert(state.IsKeyDown(KeyCode::W) && state.IsMouseButtonDown(MouseButton::Left) && state.GetMouseButtonReleaseSerial(MouseButton::Left)==serial);
    state.SetMousePosition(1000,2000);assert(state.GetMouseState().DeltaX==0);
    state.SetMousePosition(1002,2003);assert(state.GetMouseState().DeltaX==2 && state.GetMouseState().DeltaY==3);
    state.SetMousePosition(-500,-600,false);mouse=state.GetMouseState();
    assert(mouse.PositionX==-500 && mouse.PositionY==-600 && mouse.DeltaX==0 && mouse.DeltaY==0);
    assert(mouse.RawDeltaX==3 && mouse.ScrollDelta==5 && state.IsMouseButtonDown(MouseButton::Left));
    state.SetMousePosition(9,10,false);state.SetMousePosition(100,200,true);
    assert(state.GetMouseState().DeltaX==0 && state.GetMouseState().DeltaY==0);
    state.SetMousePosition(101,198);assert(state.GetMouseState().DeltaX==1 && state.GetMouseState().DeltaY==-2);
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
    {
        state.SetMousePosition(bad,0,false);state.SetMousePosition(0,bad,true);
        assert(state.GetMouseState().PositionX==101 && state.GetMouseState().DeltaX==1);
    }
    state.ResetAbsoluteMouseTracking();state.ResetAbsoluteMouseTracking();
    state.SetMousePosition(-2000,-1000);assert(state.GetMouseState().DeltaX==0);
    state.BeginFrame();assert(state.IsKeyDown(KeyCode::W) && state.IsMouseButtonDown(MouseButton::Left));
    state.SetMousePosition(-1995,-999);assert(state.GetMouseState().DeltaX==5 && state.GetMouseState().DeltaY==1);
    std::cout << "AbsoluteMouseTrackingTest passed\n";
    return 0;
}
