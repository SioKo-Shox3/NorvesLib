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
    InputState state;
    assert(state.GetRawMouseActivitySerial()==0 && state.GetMouseScrollActivitySerial()==0);
    assert(state.AddRawMouseDelta(0,0) && state.AddMouseScrollAxes(0,0));
    assert(state.GetRawMouseActivitySerial()==0 && state.GetMouseScrollActivitySerial()==0);
    state.SetMousePosition(100,200);state.SetMousePosition(102,197);
    assert(state.AddRawMouseDelta(3,-4));assert(state.AddRawMouseDelta(-1,2));
    assert(state.AddMouseScrollAxes(1,2));state.AddMouseScroll(-0.5f);
    auto mouse=state.GetMouseState();
    assert(mouse.PositionX==102 && mouse.PositionY==197 && mouse.DeltaX==2 && mouse.DeltaY==-3);
    assert(mouse.RawDeltaX==2 && mouse.RawDeltaY==-2 && mouse.ScrollDelta==0.5f && mouse.HorizontalScrollDelta==2);
    const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
    for(float bad:{nan,inf,-inf})
    {
        assert(!state.AddRawMouseDelta(bad,10));assert(!state.AddRawMouseDelta(10,bad));
        assert(!state.AddMouseScrollAxes(bad,10));assert(!state.AddMouseScrollAxes(10,bad));
        state.AddMouseScroll(bad);
        const auto next=state.GetMouseState();
        assert(next.RawDeltaX==2 && next.RawDeltaY==-2 && next.ScrollDelta==0.5f && next.HorizontalScrollDelta==2);
    }
    assert(state.GetRawMouseActivitySerial()==2 && state.GetMouseScrollActivitySerial()==2);
    state.BeginFrame();mouse=state.GetMouseState();
    assert(state.GetRawMouseActivitySerial()==2 && state.GetMouseScrollActivitySerial()==2);
    assert(mouse.RawDeltaX==0 && mouse.RawDeltaY==0 && mouse.ScrollDelta==0 && mouse.HorizontalScrollDelta==0);
    assert(mouse.PositionX==102 && mouse.PositionY==197);
    const float max=std::numeric_limits<float>::max();
    assert(state.AddRawMouseDelta(1,max));assert(!state.AddRawMouseDelta(9,max));
    assert(state.GetMouseState().RawDeltaX==1 && state.GetMouseState().RawDeltaY==max);
    assert(state.AddMouseScrollAxes(max,1));assert(!state.AddMouseScrollAxes(max,9));
    assert(state.GetMouseState().ScrollDelta==max && state.GetMouseState().HorizontalScrollDelta==1);
    assert(state.AddRawMouseDelta(-1,-max));assert(state.AddMouseScrollAxes(-max,-1));
    assert(state.GetMouseState().RawDeltaX==0 && state.GetMouseState().RawDeltaY==0);
    assert(state.AddRawMouseDelta(3,4));assert(state.AddMouseScrollAxes(5,6));
    assert(state.GetRawMouseActivitySerial()==5 && state.GetMouseScrollActivitySerial()==5);
    state.ReleaseAll();mouse=state.GetMouseState();
    assert(state.GetRawMouseActivitySerial()==5 && state.GetMouseScrollActivitySerial()==5);
    assert(mouse.RawDeltaX==0 && mouse.RawDeltaY==0 && mouse.ScrollDelta==0 && mouse.HorizontalScrollDelta==0);
    state.SetMousePosition(2000,3000);assert(state.GetMouseState().DeltaX==0 && state.GetMouseState().DeltaY==0);
    state.SetMousePosition(2001,2998);assert(state.GetMouseState().DeltaX==1 && state.GetMouseState().DeltaY==-2);
    std::cout << "RawInputStateTest passed\n";
    return 0;
}
