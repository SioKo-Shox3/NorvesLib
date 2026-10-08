#include "Game/Input/CameraInputCollector.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace NorvesLib::Core::Input;
int main()
{
    Game::Input::CameraInputCollector collector;
    IInputController& input=collector;
    input.OnKey({KeyCode::LeftAlt,InputAction::Pressed});
    input.OnKey({KeyCode::RightAlt,InputAction::Pressed});
    for(auto button:{MouseButton::Left,MouseButton::Middle,MouseButton::Right})
        input.OnMouseButton({button,InputAction::Pressed,10,20});
    assert(input.OnMouseMove({15,25,5,5}));input.OnMouseScroll({2,1});
    auto before=collector.BuildFrameInputState();
    assert(before.IsAltDown() && before.GetMouseState().DeltaX==5 && before.GetMouseState().ScrollDelta==2);
    input.OnInputReset();input.OnInputReset(); // 冪等な取消は通常のrelease操作を合成しない。
    auto after=collector.BuildFrameInputState();
    assert(!after.IsAltDown());
    for(auto button:{MouseButton::Left,MouseButton::Middle,MouseButton::Right})
        assert(!after.IsMouseButtonDown(button));
    assert(after.GetMouseState().DeltaX==0 && after.GetMouseState().DeltaY==0 && after.GetMouseState().ScrollDelta==0);
    assert(!input.OnMouseMove({40,50,25,25}));
    input.OnMouseButton({MouseButton::Left,InputAction::Released});
    assert(!input.OnMouseMove({45,55,5,5}));
    // 新しい押下は受け付ける。前回frameの移動/修飾は持ち越さない。
    input.OnMouseButton({MouseButton::Left,InputAction::Pressed});
    assert(input.OnMouseMove({48,59,3,4}));
    after=collector.BuildFrameInputState();
    assert(after.IsMouseButtonDown(MouseButton::Left) && !after.IsAltDown());
    assert(after.GetMouseState().DeltaX==3 && after.GetMouseState().DeltaY==4);
    collector.ResetFrame();after=collector.BuildFrameInputState();
    assert(after.IsMouseButtonDown(MouseButton::Left) && after.GetMouseState().DeltaX==0);
    std::cout << "CameraInputResetTest passed\n";
    return 0;
}
