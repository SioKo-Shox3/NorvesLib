// 実Router/MapperとUI→debug mask→Gameの優先度・抑止理由を試験する。
#include "Input/InputDebugOverlayController.h"
#include "Input/InputRebindCaptureManager.h"
#include "Input/InputMapper.h"
#include "Input/InputRouter.h"
#include "Input/InputSystem.h"
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
    class Probe final : public IInputController
    {
    public:
        bool bUi = false;
        bool bConsume = false;
        bool bHeld = false;
        bool bMouseHeld = false;
        int Drags = 0;
        int Events = 0;
        int Resets = 0;
        const char* DebugName() const override
        {
            return "DebugOverlayProbe";
        }
        bool OnKey(const KeyEvent& event) override
        {
            if (bUi && event.Code == KeyCode::F1)
            {
                return false;
            }
            ++Events;
            bHeld = event.Action != InputAction::Released;
            return bConsume;
        }
        bool OnMouseButton(const MouseButtonEvent& event) override
        {
            ++Events;
            bMouseHeld = event.Action != InputAction::Released;
            return bConsume;
        }
        bool OnMouseMove(const MouseMoveEvent&) override
        {
            ++Events;
            if (bMouseHeld)
            {
                ++Drags;
            }
            return bConsume;
        }
        bool OnMouseRawMove(const MouseRawMoveEvent&) override
        {
            ++Events;
            return bConsume;
        }
        bool OnMouseScroll(const MouseScrollEvent&) override
        {
            ++Events;
            return bConsume;
        }
        bool OnChar(const CharEvent&) override
        {
            ++Events;
            return bConsume;
        }
        bool OnGamepadButton(const GamepadButtonEvent&) override
        {
            ++Events;
            return bConsume;
        }
        bool OnGamepadSample(const GamepadSampleEvent&) override
        {
            ++Events;
            return bConsume;
        }
        void OnInputReset() override
        {
            bHeld = false;
            bMouseHeld = false;
            ++Resets;
        }
    };
    struct Fixture
    {
        InputSystem System;
        InputRouter Router;
        InputMapper Mapper{System.GetState()};
        Probe Ui;
        Probe Game;
        InputDebugOverlayController Overlay{System, Router, Mapper};
        InputRebindCaptureManager Capture{System, Router, Mapper};
        Fixture()
        {
            System.SetRouter(&Router);
            Mapper.Attach(Router);
            Ui.bUi = true;
            Router.RegisterController(&Ui, InputRouter::PriorityOverlay);
            Router.RegisterController(&Game, InputRouter::PriorityGame);
            assert(Overlay.Attach() && Capture.Attach());
            InputBindingSet settings;
            assert(settings.AddContext("Gameplay"_id, ECursorMode::Locked));
            InputActionDefinition action;
            action.Id = "Jump"_id;
            InputBinding binding;
            binding.Source = {EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::Space), 0};
            action.Bindings = {binding};
            assert(settings.AddAction("Gameplay"_id, action));
            action = {};
            action.Id = "Move"_id;
            action.Settings.Type = EInputMappingValueType::Axis1D;
            binding.Source = {EInputBindingSource::GamepadAxis, 0, 0};
            action.Bindings = {binding};
            assert(settings.AddAction("Gameplay"_id, action));
            assert(Mapper.ConfigureWithContext(settings, "Gameplay"_id));
        }
        ~Fixture()
        {
            Capture.Detach();
            Overlay.Detach();
            Router.UnregisterController(&Ui);
            Router.UnregisterController(&Game);
            Mapper.Detach();
            System.SetRouter(nullptr);
        }
        void Advance()
        {
            Overlay.Advance();
            Capture.Advance();
            assert(Mapper.Update(0, 0.01));
        }
        void Toggle()
        {
            System.InjectKeyEvent(KeyCode::F1, InputAction::Pressed);
            System.InjectKeyEvent(KeyCode::F1, InputAction::Released);
        }
    };
    void MasksGameAfterUi()
    {
        Fixture f;
        f.Toggle();
        assert(f.Game.Events == 2 && !f.Overlay.IsOverlayActive());
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
        assert(f.Mapper.GetAction("Jump"_id).Button.Held && f.Game.bHeld);
        f.Overlay.SetEnabled(true);
        f.Ui.bConsume = true;
        const int beforeGame = f.Game.Events;
        f.System.InjectKeyEvent(KeyCode::F1, InputAction::Pressed);
        f.System.InjectKeyEvent(KeyCode::F1, InputAction::Repeat);
        f.Advance();
        assert(f.Overlay.IsOverlayActive());
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Normal && f.Mapper.GetActiveContext() == "Gameplay"_id);
        assert(!f.Mapper.GetAction("Jump"_id).Active && !f.Mapper.GetAction("Jump"_id).Button.Held);
        assert(!f.Mapper.ConsumeFixedPress("Jump"_id) && !f.Game.bHeld);
        f.System.InjectKeyEvent(KeyCode::F1, InputAction::Pressed); // 自己reset後も物理release前は反転しない。
        f.Advance();
        assert(f.Overlay.IsOverlayActive());
        f.System.InjectKeyEvent(KeyCode::F1, InputAction::Repeat);
        f.System.InjectKeyEvent(KeyCode::F1, InputAction::Released);
        f.Advance();
        assert(f.Overlay.IsOverlayActive());
        f.Ui.bConsume = false; // UI窓の外でもGameへは渡さない。
        const int beforeUi = f.Ui.Events;
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
        f.System.InjectMouseButton(MouseButton::Left, InputAction::Pressed, 0, 0);
        f.System.InjectMouseMove(2, 3);
        assert(f.System.InjectRawMouseDelta(3, 4));
        f.System.InjectMouseScroll(1);
        f.System.InjectCharEvent('a');
        GamepadState pad;
        pad.Connected = true;
        pad.Axes[0] = 1;
        pad.Buttons = static_cast<uint16_t>(GamepadButton::A);
        assert(f.System.InjectGamepadState(0, pad));
        f.Advance();
        assert(f.Ui.Events > beforeUi && f.Game.Events == beforeGame);
        assert(f.Mapper.GetAction("Move"_id).Axis.x == 0);
        f.Toggle();
        assert(f.Overlay.IsOverlayActive());
        f.Advance();
        assert(!f.Overlay.IsOverlayActive() && f.Mapper.GetRequestedCursorMode() == ECursorMode::Locked);
        assert(f.Mapper.GetAction("Jump"_id).Active && !f.Mapper.GetAction("Jump"_id).Button.Held);
        assert(f.Mapper.GetAction("Move"_id).Axis.x == 0);
        assert(!f.System.GetState().IsKeyDown(KeyCode::Space));
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
        assert(f.Mapper.GetAction("Jump"_id).Button.Held && f.Game.bHeld);
        f.System.InjectKeyEvent(KeyCode::Space, InputAction::Released);
        f.Toggle(); f.Advance();
        assert(f.Overlay.IsOverlayActive());
        f.Overlay.SetEnabled(false);
        f.Advance();
        assert(!f.Overlay.IsOverlayActive() && f.Mapper.GetRequestedCursorMode() == ECursorMode::Locked);
    }
    void CaptureAndLifetime()
    {
        Fixture f;
        f.Overlay.SetEnabled(true);
        f.Toggle(); f.Advance();
        assert(f.Overlay.IsOverlayActive());
        const auto id = f.Capture.Begin();
        assert(id);
        f.Toggle(); // 最高優先度captureがF1を捕捉し、UI modeを変更しない。
        f.Advance();
        InputRebindCaptureResult result;
        assert(f.Capture.TryGetResult(id, result));
        assert(result.Outcome == EInputRebindOutcome::Captured && result.Control.Source.Code == static_cast<uint16_t>(KeyCode::F1));
        assert(f.Overlay.IsOverlayActive() && f.Mapper.GetRequestedCursorMode() == ECursorMode::Normal);
        assert(!f.Mapper.GetAction("Jump"_id).Active);
        InputDebugOverlayController other(f.System, f.Router, f.Mapper);
        assert(!other.Attach());
        InputSystem foreign;
        foreign.SetRouter(&f.Router);
        InputDebugOverlayController wrong(foreign, f.Router, f.Mapper);
        assert(!wrong.Attach());
        f.Mapper.SetFocused(false);
        f.Router.NotifyInputFocusChanged(false);
        f.System.ReleaseAll();
        f.Toggle(); // 非focus中は予約キーもmodeを変えない。
        f.Advance();
        assert(f.Overlay.IsOverlayActive());
        f.Mapper.SetFocused(true);
        const auto cancelled = f.Capture.Begin();
        assert(cancelled);
        f.System.InjectKeyEvent(KeyCode::B, InputAction::Pressed);
        f.Overlay.Detach();
        assert(!f.System.GetState().IsKeyDown(KeyCode::B));
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Normal); // capture理由はまだ解除しない。
        f.Capture.Advance();
        assert(f.Capture.TryGetResult(cancelled, result) && result.Outcome == EInputRebindOutcome::Cancelled);
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Locked);
        assert(f.Overlay.Attach() && f.Overlay.IsEnabled() && !f.Overlay.IsOverlayActive());
        f.Toggle(); f.Advance();
        assert(f.Overlay.IsOverlayActive());
        f.Overlay.Detach();
        assert(f.Mapper.GetRequestedCursorMode() == ECursorMode::Locked);
    }
    void PendingEnterSurvivesCleanup()
    {
        Fixture f;
        f.Overlay.SetEnabled(true);
        auto prepare = [&]()
        {
            f.System.InjectKeyEvent(KeyCode::Space, InputAction::Pressed);
            f.System.InjectMouseButton(MouseButton::Left, InputAction::Pressed, 0, 0);
            assert(f.Game.bHeld && f.Game.bMouseHeld && f.Mapper.GetAction("Jump"_id).Button.Held);
            f.System.InjectKeyEvent(KeyCode::F1, InputAction::Pressed);
            f.System.InjectKeyEvent(KeyCode::Space, InputAction::Released);
            f.System.InjectMouseButton(MouseButton::Left, InputAction::Released, 0, 0);
            assert(f.Game.bHeld && f.Game.bMouseHeld && !f.Overlay.IsOverlayActive());
            const int resets = f.Game.Resets;
            f.Overlay.Detach();
            assert(f.Game.Resets == resets); // 終了処理中はobserverを起動しない。
            assert(!f.Mapper.GetAction("Jump"_id).Button.Held && !f.Mapper.ConsumeFixedPress("Jump"_id));
        };
        prepare();
        {
            InputDebugOverlayController replacement(f.System, f.Router, f.Mapper);
            assert(replacement.Attach()); // 別instanceでもSystem上の未配送resetを回収する。
            assert(!f.Game.bHeld && !f.Game.bMouseHeld);
            const auto drags = f.Game.Drags;
            f.System.InjectMouseMove(3, 4);
            assert(f.Game.Drags == drags);
            replacement.Detach();
        }
        assert(f.Overlay.Attach());
        prepare();
        f.System.BeginFrame(); // 再Attachしなくても入力再開前に解除する。
        assert(!f.Game.bHeld && !f.Game.bMouseHeld);
        const auto drags = f.Game.Drags;
        f.System.InjectMouseMove(5, 6);
        assert(f.Game.Drags == drags);
    }
}
int main()
{
    MasksGameAfterUi();
    CaptureAndLifetime();
    PendingEnterSurvivesCleanup();
    std::cout << "InputDebugOverlayControllerTest passed\n";
    return 0;
}
