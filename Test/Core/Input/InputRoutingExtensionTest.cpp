#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Input/IInputController.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <iostream>
using namespace NorvesLib::Core::Input;
namespace
{
    class Receiver final : public IInputController
    {
    public:
        bool Consume=false;
        int MoveCount=0,SampleCount=0;
        GamepadSampleEvent Sample;
        int RawCount=0,WheelCount=0,MouseButtonCount=0,PadCount=0,ConnectionCount=0,ResetCount=0,OrderCount=0;
        int Order[128]{};
        MouseRawMoveEvent Raw;
        MouseScrollEvent Wheel;
        MouseMoveEvent Move;
        GamepadButtonEvent Pad[32]{};
        InputSystem* System=nullptr;
        const char* DebugName() const override { return "RoutingExtensionReceiver"; }
        bool OnMouseRawMove(const MouseRawMoveEvent& event) override
        {
            ++RawCount;Raw=event;
            assert(System->GetState().GetMouseState().RawDeltaX!=0);
            return Consume;
        }
        bool OnMouseScroll(const MouseScrollEvent& event) override { ++WheelCount;Wheel=event;return Consume; }
        bool OnMouseButton(const MouseButtonEvent&) override { ++MouseButtonCount;return Consume; }
        bool OnMouseMove(const MouseMoveEvent& event) override { ++MoveCount;Move=event;return Consume; }
        bool OnGamepadButton(const GamepadButtonEvent& event) override
        {
            assert(PadCount<32);Pad[PadCount++]=event;
            assert(System->GetState().IsGamepadButtonDown(event.Slot,event.Button)==(event.Action==InputAction::Pressed));
            Order[OrderCount++]=event.Action==InputAction::Pressed?1:2;
            return Consume;
        }
        bool OnGamepadSample(const GamepadSampleEvent& event) override
        {
            ++SampleCount;Sample=event;
            assert(event.Serial==System->GetState().GetGamepadSampleSerial(event.Slot));
            assert(event.State.Buttons==System->GetState().GetLastGamepadSample(event.Slot).Buttons);
            return Consume;
        }
        void OnGamepadConnection(const GamepadConnectionEvent& event) override
        {
            ++ConnectionCount;
            assert(System->GetState().GetGamepadState(event.Slot).Connected==event.Connected);
            Order[OrderCount++]=event.Connected?3:4;
        }
        void OnInputReset() override
        {
            ++ResetCount;Order[OrderCount++]=5;
            assert(!System->GetState().IsKeyDown(KeyCode::W));
            assert(System->GetState().GetMouseState().RawDeltaX==0);
        }
    };
}
int main()
{
    InputSystem system;InputRouter router;Receiver ui,game;
    ui.System=game.System=&system;
    system.SetRouter(&router);
    router.RegisterController(&game,InputRouter::PriorityGame);
    router.RegisterController(&ui,InputRouter::PriorityOverlay);
    int rawDelegates=0,padDelegates=0,connectionDelegates=0,resetDelegates=0;
    system.OnMouseRawMoveEvent().Add([&](const MouseRawMoveEvent& event)
    {
        assert(system.GetState().GetMouseState().RawDeltaX!=0);
        assert(ui.RawCount==rawDelegates);assert(event.DeltaX!=0);++rawDelegates;
    });
    system.OnGamepadButtonEvent().Add([&](const GamepadButtonEvent&)
    { assert(ui.PadCount==padDelegates);++padDelegates; });
    system.OnGamepadConnectionEvent().Add([&](const GamepadConnectionEvent&)
    { assert(ui.ConnectionCount==connectionDelegates);++connectionDelegates; });
    system.OnInputResetEvent().Add([&]() { assert(ui.ResetCount==resetDelegates);++resetDelegates; });
    ui.Consume=true;
    assert(system.InjectRawMouseDelta(2,3));assert(ui.RawCount==1 && game.RawCount==0 && rawDelegates==1);
    assert(system.InjectMouseScrollAxes(1,-2));assert(ui.WheelCount==1 && game.WheelCount==0);
    ui.Consume=false;
    assert(system.InjectRawMouseDelta(4,-1));assert(game.RawCount==1 && game.Raw.DeltaX==4 && game.Raw.DeltaY==-1);
    assert(system.InjectMouseScrollAxes(2,3));assert(game.WheelCount==1 && game.Wheel.Delta==2 && game.Wheel.HorizontalDelta==3);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!system.InjectRawMouseDelta(100,nan));assert(!system.InjectMouseScrollAxes(nan,100));
    assert(rawDelegates==2 && ui.RawCount==2 && ui.WheelCount==2);
    assert(system.GetState().GetMouseState().RawDeltaX==6 && system.GetState().GetMouseState().HorizontalScrollDelta==1);
    system.InjectMouseMove(500,600);assert(game.Move.DeltaX==0 && game.Move.DeltaY==0);
    system.InjectMouseMove(504,598);assert(game.Move.DeltaX==4 && game.Move.DeltaY==-2);
    GamepadState pad;pad.Connected=true;pad.Buttons=static_cast<uint16_t>(GamepadButton::A);
    ui.Consume=true;
    assert(system.InjectGamepadState(0,pad));
    assert(ui.ConnectionCount==1 && game.ConnectionCount==1 && game.PadCount==0);
    assert(ui.Order[0]==3 && ui.Order[1]==1);
    ui.Consume=false;
    pad.Buttons=static_cast<uint16_t>(GamepadButton::B);
    assert(system.InjectGamepadState(0,pad));
    assert(game.PadCount==2 && game.Pad[0].Button==GamepadButton::B && game.Pad[0].Action==InputAction::Pressed);
    assert(game.Pad[1].Button==GamepadButton::A && game.Pad[1].Action==InputAction::Released);
    assert(system.InjectGamepadState(0,pad));assert(game.PadCount==2);
    auto invalid=pad;invalid.Axes[0]=nan;
    assert(!system.InjectGamepadState(0,invalid));assert(!system.InjectGamepadState(4,pad));
    assert(game.PadCount==2 && game.ConnectionCount==1 && padDelegates==3);
    assert(system.GetState().GetGamepadState(0).Buttons==pad.Buttons);
    assert(system.InjectGamepadState(0,{}));
    assert(game.Order[3]==4 && game.Order[4]==2);
    assert(game.ConnectionCount==2 && game.PadCount==3);
    // 全解除は通常Releasedを合成しない。UI消費に関係なく取消通知が届く。
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    ui.Consume=true;
    system.ReleaseAll();assert(ui.ResetCount==1 && game.ResetCount==1 && resetDelegates==1);
    assert(game.PadCount==3 && padDelegates==4);
    ui.Consume=false;
    assert(system.InjectGamepadState(0,pad));
    system.InjectMouseButton(MouseButton::Left,InputAction::Pressed,0,0);
    const int beforePad=padDelegates,beforeMouse=ui.MouseButtonCount;
    ui.Consume=true;
    system.ReleaseAll();
    assert(system.GetState().GetGamepadState(0).Connected);
    assert(system.GetState().GetGamepadState(0).Buttons==0);
    assert(system.GetState().IsGamepadButtonReleased(0,GamepadButton::B));
    assert(!system.GetState().IsMouseButtonDown(MouseButton::Left));
    assert(system.GetState().IsMouseButtonReleased(MouseButton::Left));
    assert(padDelegates==beforePad && ui.MouseButtonCount==beforeMouse);
    assert(ui.ResetCount==2 && game.ResetCount==2 && resetDelegates==2);
    ui.Consume=false;
    system.InjectMouseMove(3000,4000);assert(game.Move.DeltaX==0 && game.Move.DeltaY==0);
    router.UnregisterController(&game);
    assert(system.InjectRawMouseDelta(1,1));assert(game.RawCount==1);
    router.UnregisterController(&ui);system.SetRouter(nullptr);
    {
        InputSystem absolute;InputRouter route;Receiver receiver;receiver.System=&absolute;
        absolute.SetRouter(&route);route.RegisterController(&receiver,0);
        absolute.InjectMouseMove(10,20);absolute.InjectMouseMove(13,24);
        assert(receiver.Move.DeltaX==3 && receiver.Move.DeltaY==4);
        assert(absolute.InjectRawMouseDelta(2,3));
        absolute.InjectMouseMove(1000,2000,false);
        assert(receiver.Move.PositionX==1000 && receiver.Move.DeltaX==0 && receiver.Move.DeltaY==0);
        assert(absolute.GetState().GetMouseState().DeltaX==0 && absolute.GetState().GetMouseState().RawDeltaX==2);
        const auto count=receiver.MoveCount;absolute.ResetAbsoluteMouseTracking();assert(receiver.MoveCount==count);
        absolute.InjectMouseMove(1500,2500);assert(receiver.Move.DeltaX==0 && receiver.Move.DeltaY==0);
        absolute.InjectMouseMove(1501,2502);assert(receiver.Move.DeltaX==1 && receiver.Move.DeltaY==2);
        route.UnregisterController(&receiver);absolute.SetRouter(nullptr);
    }
    {
        InputSystem sampled;InputRouter route;Receiver overlay,receiver;
        overlay.System=receiver.System=&sampled;sampled.SetRouter(&route);
        route.RegisterController(&overlay,1000);route.RegisterController(&receiver,0);
        GamepadState original;original.Connected=true;original.Buttons=static_cast<uint16_t>(GamepadButton::A);original.Axes[0]=0.75f;
        const auto expected=original;int delegates=0,buttons=0;GamepadSampleEvent captured;
        sampled.OnGamepadConnectionEvent().Add([&](const GamepadConnectionEvent&)
        {
            original.Buttons=static_cast<uint16_t>(GamepadButton::B);original.Axes[0]=-0.5f;
        });
        sampled.OnGamepadButtonEvent().Add([&](const GamepadButtonEvent&) { ++buttons;original.Triggers[0]=1; });
        sampled.OnGamepadSampleEvent().Add([&](const GamepadSampleEvent& event)
        {
            ++delegates;captured=event;
            assert(buttons>=1); // button edgeが先に届く。
            assert(receiver.SampleCount==delegates-1); // DelegateがRouterより先。
        });
        assert(sampled.InjectGamepadState(0,original));
        assert(original.Buttons!=expected.Buttons); // 呼出元のmutable値はcallback中に変化する。
        assert(captured.State.Buttons==expected.Buttons && captured.State.Axes[0]==0.75f && captured.State.Triggers[0]==0);
        assert(receiver.PadCount==1 && receiver.Pad[0].Button==GamepadButton::A && receiver.SampleCount==1);
        assert(sampled.InjectGamepadState(0,expected));
        assert(buttons==1 && delegates==2 && receiver.SampleCount==2 && captured.Serial==2);
        sampled.ReleaseAll();assert(delegates==2 && receiver.SampleCount==2);
        assert(sampled.GetState().GetLastGamepadSample(0).Buttons==expected.Buttons && sampled.GetState().GetGamepadState(0).Buttons==0);
        auto invalid=expected;invalid.Triggers[0]=2;
        assert(!sampled.InjectGamepadState(0,invalid) && delegates==2 && sampled.GetState().GetGamepadSampleSerial(0)==2);
        overlay.Consume=true;
        assert(sampled.InjectGamepadState(0,expected));assert(overlay.SampleCount==3 && receiver.SampleCount==2 && delegates==3);
        // 次回以降はRouter消費で受信数とDelegate数が異なるため、監視を解除する。
        sampled.OnGamepadSampleEvent().Clear();
        assert(sampled.InjectGamepadState(0,{}));assert(overlay.SampleCount==4 && receiver.SampleCount==2);
        assert(!overlay.Sample.State.Connected && overlay.Sample.Serial==4);
        route.UnregisterController(&overlay);route.UnregisterController(&receiver);sampled.SetRouter(nullptr);
    }
    std::cout << "InputRoutingExtensionTest passed\n";
    return 0;
}
