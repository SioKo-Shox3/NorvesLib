// 実InputSystem/Router/Mapperによるcapture接続試験。既存LoggerSinkTestへ束ねる。
#include "Input/InputRebindCaptureManager.h"
#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Input/InputMapper.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    InputBindingSet Definitions()
    {
        InputBindingSet settings;
        assert(settings.AddContext("Gameplay"_id, ECursorMode::Locked));
        InputActionDefinition action;
        action.Id = "Jump"_id;
        InputBinding key;
        key.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::Space), 0};
        action.Bindings = {key};
        assert(settings.AddAction("Gameplay"_id, action));
        action = {};
        action.Id = "Move"_id;
        action.Settings.Type = EInputMappingValueType::Axis1D;
        key.Source = {EInputBindingSource::GamepadAxis, 0, 0};
        action.Bindings = {key};
        assert(settings.AddAction("Gameplay"_id, action));
        return settings;
    }
    class Observer final : public IInputController
    {
    public:
        int Events = 0;
        int Resets = 0;
        bool bHeld = false;
        const char* DebugName() const override
        {
            return "CaptureTestObserver";
        }
        bool OnKey(const KeyEvent& event) override
        {
            ++Events;
            bHeld = event.Action != InputAction::Released;
            return false;
        }
        bool OnMouseButton(const MouseButtonEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnMouseMove(const MouseMoveEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnMouseRawMove(const MouseRawMoveEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnMouseScroll(const MouseScrollEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnGamepadButton(const GamepadButtonEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnGamepadSample(const GamepadSampleEvent&) override
        {
            ++Events;
            return false;
        }
        bool OnChar(const CharEvent&) override
        {
            ++Events;
            return false;
        }
        void OnInputReset() override
        {
            ++Resets;
            bHeld = false;
        }
    };
    struct Fixture
    {
        InputSystem System;
        InputRouter Router;
        InputMapper Mapper{System.GetState()};
        Observer Legacy;
        InputRebindCaptureManager Capture{System, Router, Mapper};
        double Time = 0;
        Fixture()
        {
            System.SetRouter(&Router);
            Mapper.Attach(Router);
            Router.RegisterController(&Legacy, InputRouter::PriorityOverlay);
            assert(Mapper.ConfigureWithContext(Definitions(), "Gameplay"_id));
            assert(Capture.Attach());
        }
        ~Fixture()
        {
            Capture.Detach();
            Router.UnregisterController(&Legacy);
            Mapper.Detach();
            System.SetRouter(nullptr);
        }
        void Frame()
        {
            Time += 0.01;
            assert(Mapper.BeginFrame(Time));
            System.BeginFrame();
            Capture.BeginFrame();
        }
        void Advance()
        {
            Capture.Advance();
            assert(Mapper.Update(Time, 0.01));
        }
    };
    void RoutingAndRestore()
    {
        Fixture f;
        f.Frame();
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
        f.Advance();
        assert(f.Mapper.GetAction("Jump"_id).Button.Held && f.Legacy.bHeld);
        const auto id = f.Capture.Begin();
        assert(id != 0 && f.Capture.IsCapturing());
        assert(!f.Legacy.bHeld && f.Legacy.Resets == 1);
        assert(!f.Mapper.GetAction("Jump"_id).Active && !f.Mapper.GetAction("Jump"_id).Button.Held);
        assert(!f.Mapper.ConsumeFixedPress("Jump"_id));
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Normal);
        InputRebindCaptureResult result;
        assert(!f.Capture.TryGetResult(id, result));
        assert(f.Capture.Begin() == 0 && !f.Capture.Cancel(id + 1));
        const int events = f.Legacy.Events;
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Repeat);
        f.System.InjectKeyEvent(KeyCode::A, InputAction::Pressed);
        f.System.InjectKeyEvent(KeyCode::A, InputAction::Released);
        f.System.InjectMouseMove(1, 2);
        f.System.InjectRawMouseDelta(3, 4);
        f.System.InjectMouseScroll(1);
        f.System.InjectCharEvent('a');
        f.System.InjectMouseButton(MouseButton::Left, InputAction::Pressed, 0, 0);
        f.System.InjectMouseButton(MouseButton::Left, InputAction::Released, 0, 0);
        assert(f.Legacy.Events == events);
        assert(f.Mapper.ConfigurePreservingContexts(Definitions()));
        assert(!f.Mapper.GetAction("Jump"_id).Active && f.Mapper.GetRequestedCursorMode() == ECursorMode::Normal);
        f.Advance();
        assert(f.Capture.IsCapturing()); // 開始時Spaceが物理解除されるまで待つ。
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Released);
        f.Advance();
        assert(!f.Capture.IsCapturing() && f.Capture.TryGetResult(id, result));
        assert(result.RequestId == id && result.Outcome == EInputRebindOutcome::Captured);
        assert(result.Control.Source.Kind == EInputBindingSource::Key && result.Control.Source.Code == static_cast<uint16_t>(KeyCode::A));
        assert(f.Legacy.Resets == 2 && f.Legacy.Events == events);
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Locked);
        assert(!f.Mapper.GetAction("Jump"_id).Button.Held && !f.Mapper.ConsumeFixedPress("Jump"_id));
        f.Frame();
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
        f.Advance();
        assert(f.Mapper.GetAction("Jump"_id).Button.Held && f.Legacy.bHeld);
        assert(f.Mapper.ConsumeFixedPress("Jump"_id));
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Released);
        const auto next = f.Capture.Begin();
        assert(next > id && !f.Capture.TryGetResult(id, result));
        assert(!f.Capture.Cancel(id) && f.Capture.Cancel(next));
        f.Advance();
        assert(f.Capture.TryGetResult(next, result) && result.Outcome == EInputRebindOutcome::Cancelled);
    }
    void AnalogAndAbort()
    {
        Fixture f;
        f.Frame();
        GamepadState pad;
        pad.Connected = true;
        pad.Axes[0] = 1;
        assert(f.System.InjectGamepadState(0, pad));
        f.Advance();
        assert(f.Mapper.GetAction("Move"_id).Axis.x > 0);
        const auto id = f.Capture.Begin();
        assert(id);
        assert(f.System.InjectGamepadState(0, pad));
        f.Advance();
        assert(f.Capture.IsCapturing() && f.Mapper.GetAction("Move"_id).Axis.x == 0);
        pad.Axes[0] = 0;
        assert(f.System.InjectGamepadState(0, pad));
        pad.Axes[0] = -1;
        assert(f.System.InjectGamepadState(0, pad));
        f.Advance();
        assert(f.Capture.IsCapturing() && f.Mapper.GetAction("Move"_id).Axis.x == 0);
        InputRebindCaptureResult result;
        assert(!f.Capture.TryGetResult(id, result));
        f.System.ReleaseAll(); // 外部resetは捕捉済み候補も中止する。
        assert(!f.Capture.TryGetResult(id, result));
        f.Advance();
        assert(f.Capture.TryGetResult(id, result) && result.Outcome == EInputRebindOutcome::Cancelled);
        assert(f.Mapper.GetAction("Move"_id).Axis.x == 0);
        pad.Axes[0] = 0;
        assert(f.System.InjectGamepadState(0, pad));
        const auto focusId = f.Capture.Begin();
        assert(focusId);
        f.Mapper.SetFocused(false);
        f.Router.NotifyInputFocusChanged(false);
        const int events = f.Legacy.Events;
        f.System.InjectKeyEvent(KeyCode::B, InputAction::Pressed);
        assert(f.Legacy.Events == events);
        f.Advance();
        assert(f.Capture.TryGetResult(focusId, result) && result.Outcome == EInputRebindOutcome::Cancelled);
        assert(f.Capture.Begin() == 0 && !f.System.GetState().IsKeyDown(KeyCode::B));
        f.Mapper.SetFocused(true);
        const auto escape = f.Capture.Begin();
        assert(escape);
        f.System.InjectKeyEvent(KeyCode::Escape, InputAction::Pressed);
        f.Advance();
        assert(f.Capture.IsCapturing());
        f.System.InjectKeyEvent(KeyCode::Escape, InputAction::Released);
        f.Advance();
        assert(f.Capture.TryGetResult(escape, result) && result.Outcome == EInputRebindOutcome::Cancelled);
    }
    void OwnershipAndDetach()
    {
        Fixture f;
        assert(f.Capture.Attach());
        InputRebindCaptureManager second(f.System, f.Router, f.Mapper);
        assert(!second.Attach() && second.Begin() == 0);
        InputSystem foreign;
        foreign.SetRouter(&f.Router);
        InputRebindCaptureManager wrong(foreign, f.Router, f.Mapper);
        assert(!wrong.Attach());
        f.Frame();
        const auto id = f.Capture.Begin();
        assert(id);
        f.System.InjectKeyEvent(KeyCode::A, InputAction::Pressed);
        f.Capture.Detach();
        InputRebindCaptureResult result;
        assert(!f.Capture.IsCapturing() && f.Capture.TryGetResult(id, result));
        assert(result.Outcome == EInputRebindOutcome::Cancelled);
        assert(!f.System.GetState().IsKeyDown(KeyCode::A));
        assert(second.Attach());
        const auto other = second.Begin();
        assert(other);
        assert(second.Cancel(other));
        second.Advance();
        second.Detach();
        assert(f.Capture.Attach());
        InputRouter foreignRouter;
        f.Capture.Detach();
        f.System.SetRouter(&foreignRouter);
        assert(!f.Capture.Attach() && f.Capture.Begin() == 0);
        f.System.SetRouter(&f.Router);
        assert(f.Capture.Attach());
    }
}
int main()
{
    RoutingAndRestore();
    AnalogAndAbort();
    OwnershipAndDetach();
    std::cout << "InputRebindCaptureManagerTest passed\n";
    return 0;
}
