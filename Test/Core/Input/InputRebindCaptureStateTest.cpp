// リバインドkernelの契約試験。OS/Router接続は含まない。
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Input/InputRebindCaptureState.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace NorvesLib::Core::Input;
namespace
{
    struct Fixture
    {
        InputState Raw;
        InputRebindCaptureState Capture;
        void Start(InputRebindCaptureOptions options = {})
        {
            assert(Capture.Begin(options, Raw));
            Capture.BeginFrame(Raw);
        }
        void Key(KeyCode code, InputAction action, bool deliver = true)
        {
            Raw.SetKeyState(code, action != InputAction::Released);
            if (deliver)
            {
                Capture.OnKey({code, action}, Raw);
            }
        }
        void Mouse(MouseButton button, InputAction action, bool deliver = true)
        {
            Raw.SetMouseButtonState(button, action != InputAction::Released);
            if (deliver)
            {
                Capture.OnMouseButton({button, action}, Raw);
            }
        }
        void Pad(uint8_t slot, const GamepadState& state, bool deliver = true, EGamepadSampleMode mode = EGamepadSampleMode::Live)
        {
            const auto old = Raw.GetGamepadState(slot);
            assert(Raw.SetGamepadState(slot, state, mode));
            if (old.Connected != state.Connected)
            {
                Capture.OnGamepadConnection({slot, state.Connected});
            }

            if (!deliver)
            {
                return;
            }

            if (mode == EGamepadSampleMode::Live)
            {
                for (uint16_t bit = 1; bit != 0; bit = static_cast<uint16_t>(bit << 1))
                {
                    if ((state.Buttons & bit) && !(old.Buttons & bit))
                    {
                        Capture.OnGamepadButton({slot, static_cast<GamepadButton>(bit), InputAction::Pressed}, Raw);
                    }
                }

                for (uint16_t bit = 1; bit != 0; bit = static_cast<uint16_t>(bit << 1))
                {
                    if ((old.Buttons & bit) && !(state.Buttons & bit))
                    {
                        Capture.OnGamepadButton({slot, static_cast<GamepadButton>(bit), InputAction::Released}, Raw);
                    }
                }

            }
            Capture.OnGamepadSample({slot, state, Raw.GetGamepadSampleSerial(slot), mode}, Raw);
        }
        void Advance()
        {
            Capture.Advance(Raw);
        }
        void Capturing() const
        {
            assert(Capture.GetPhase() == EInputRebindPhase::Capturing);
        }
        void Waiting() const
        {
            assert(Capture.GetPhase() == EInputRebindPhase::AwaitingNeutral);
        }
        void Finished(EInputRebindOutcome outcome = EInputRebindOutcome::Captured) const
        {
            assert(Capture.GetPhase() == EInputRebindPhase::Finished);
            assert(Capture.GetOutcome() == outcome);
        }
        void Control(EInputBindingSource source, uint16_t code, int8_t direction = 1, InputModifierMask modifiers = 0, uint8_t slot = 0) const
        {
            const auto value = Capture.GetControl();
            assert(value.Source.Kind == source && value.Source.Code == code && value.Source.Slot == slot);
            assert(value.Direction == direction && value.RequiredModifiers == modifiers);
        }
    };
    constexpr uint16_t Code(KeyCode code)
    {
        return static_cast<uint16_t>(code);
    }
    void DigitalAndReset()
    {
        Fixture f;
        f.Raw.SetKeyState(KeyCode::Enter, true);
        f.Raw.SetMouseButtonState(MouseButton::Left, true);
        f.Start();
        f.Raw.ReleaseAll(); // 内部取消を物理解除と誤認しない。
        f.Key(KeyCode::Enter, InputAction::Repeat);
        f.Key(KeyCode::Enter, InputAction::Pressed);
        f.Capturing();
        f.Key(KeyCode::A, InputAction::Pressed);
        f.Control(EInputBindingSource::Key, Code(KeyCode::A));
        f.Key(KeyCode::A, InputAction::Released);
        f.Advance(); f.Waiting();
        f.Key(KeyCode::Enter, InputAction::Released);
        f.Advance(); f.Waiting();
        f.Mouse(MouseButton::Left, InputAction::Released);
        f.Advance(); f.Finished();
        f.Start();
        f.Mouse(MouseButton::X2, InputAction::Pressed);
        f.Control(EInputBindingSource::MouseButton, static_cast<uint16_t>(MouseButton::X2));
        f.Advance(); f.Waiting();
        f.Mouse(MouseButton::X2, InputAction::Released, false);
        f.Advance(); f.Finished(); // 通常押下の上位UI消費releaseは正本で照合できる。
    }
    void Modifiers()
    {
        Fixture f; f.Start();
        f.Key(KeyCode::LeftCtrl, InputAction::Pressed);
        f.Capturing();
        f.Key(KeyCode::K, InputAction::Pressed);
        f.Control(EInputBindingSource::Key, Code(KeyCode::K), 1, InputModifierCtrl);
        f.Key(KeyCode::K, InputAction::Released);
        f.Advance(); f.Waiting();
        f.Key(KeyCode::LeftCtrl, InputAction::Released);
        f.Advance(); f.Finished();
        f.Start();
        f.Key(KeyCode::RightShift, InputAction::Pressed);
        f.Key(KeyCode::RightShift, InputAction::Released);
        f.Control(EInputBindingSource::Key, Code(KeyCode::RightShift));
        f.Advance(); f.Finished();
        f.Start();
        f.Key(KeyCode::LeftCtrl, InputAction::Pressed);
        f.Key(KeyCode::LeftCtrl, InputAction::Released, false);
        f.Key(KeyCode::LeftCtrl, InputAction::Pressed, false);
        f.Key(KeyCode::A, InputAction::Pressed);
        f.Control(EInputBindingSource::Key, Code(KeyCode::A), 1, 0);
        f.Capture.Abort();
        f.Raw.ReleaseAll(); f.Start();
        f.Key(KeyCode::LeftAlt, InputAction::Pressed);
        f.Key(KeyCode::LeftAlt, InputAction::Released, false);
        f.Key(KeyCode::LeftAlt, InputAction::Pressed, false);
        f.Key(KeyCode::LeftAlt, InputAction::Released);
        f.Capturing(); // 消費された別押下のreleaseを単独修飾として拾わない。
        f.Capture.Abort();
        InputRebindCaptureOptions options; options.CaptureModifiers = false;
        f.Start(options); f.Key(KeyCode::LeftCtrl, InputAction::Pressed);
        f.Control(EInputBindingSource::Key, Code(KeyCode::LeftCtrl));
    }
    void CancellationAndValidation()
    {
        Fixture f;
        assert(!f.Capture.IsBlocking());
        InputRebindCaptureOptions invalid;
        invalid.AllowedSources = 0;
        assert(!f.Capture.Begin(invalid, f.Raw));
        assert(f.Capture.GetPhase() == EInputRebindPhase::Idle);
        invalid = {}; invalid.NeutralThreshold = invalid.ActivationThreshold;
        assert(!f.Capture.Begin(invalid, f.Raw));
        invalid = {}; invalid.MouseMotionThreshold = std::numeric_limits<float>::infinity();
        assert(!f.Capture.Begin(invalid, f.Raw));
        invalid = {}; invalid.AllowedSources = 0x80;
        assert(!f.Capture.Begin(invalid, f.Raw));
        InputRebindCaptureOptions options; options.AllowedSources = InputRebindSourceBit(EInputBindingSource::MouseButton);
        f.Start(options);
        assert(!f.Capture.Begin({}, f.Raw));
        f.Key(KeyCode::A, InputAction::Pressed); f.Capturing();
        f.Key(KeyCode::A, InputAction::Released);
        f.Key(KeyCode::Escape, InputAction::Pressed);
        f.Advance(); f.Waiting();
        assert(f.Capture.GetOutcome() == EInputRebindOutcome::Cancelled);
        f.Key(KeyCode::Escape, InputAction::Released);
        f.Advance(); f.Finished(EInputRebindOutcome::Cancelled);
        assert(!f.Capture.Begin(invalid, f.Raw)); f.Finished(EInputRebindOutcome::Cancelled);
        f.Start(); f.Capture.Cancel(); f.Advance(); f.Finished(EInputRebindOutcome::Cancelled);
        f.Start(); f.Key(KeyCode::W, InputAction::Pressed); f.Capture.Abort();
        f.Finished(EInputRebindOutcome::Cancelled);
    }
    void AnalogAndConnections()
    {
        Fixture f;
        GamepadState pad; pad.Connected = true; pad.Buttons = static_cast<uint16_t>(GamepadButton::A); pad.Axes[0] = 1;
        assert(f.Raw.SetGamepadState(2, pad)); f.Start(); f.Raw.ReleaseAll();
        f.Pad(2, pad); f.Capturing(); // reset後の人工的なPressedも開始時heldを再捕捉しない。
        pad.Buttons = 0; pad.Axes[0] = 0.3f;
        f.Pad(2, pad); f.Capturing();
        pad.Axes[0] = 0.8f; f.Pad(2, pad); f.Capturing();
        pad.Axes[0] = 0.2f; f.Pad(2, pad);
        pad.Axes[0] = 0.7f; pad.Axes[1] = -0.9f; f.Pad(2, pad);
        f.Control(EInputBindingSource::GamepadAxis, 1, -1, 0, 2);
        f.Raw.ReleaseAll(); f.Advance(); f.Waiting(); // 最新provider sampleはまだ傾いている。
        pad.Axes[0] = pad.Axes[1] = 0.2f; f.Pad(2, pad); f.Advance(); f.Finished();
        f.Start();
        GamepadState connected; connected.Connected = true; connected.Triggers[1] = 1;
        f.Pad(0, connected); f.Capturing();
        connected.Triggers[1] = 0; f.Pad(0, connected);
        connected.Triggers[1] = 0.8f; f.Pad(0, connected);
        f.Control(EInputBindingSource::GamepadTrigger, 1, 1, 0, 0);
        f.Pad(0, {}); f.Advance(); f.Finished();
        f.Start(); pad = {}; pad.Connected = true; f.Pad(3, pad);
        pad.Buttons = static_cast<uint16_t>(GamepadButton::Y); f.Pad(3, pad);
        f.Control(EInputBindingSource::GamepadButton, pad.Buttons, 1, 0, 3);
        pad.Buttons = 0; f.Pad(3, pad, false); f.Advance(); f.Finished();
    }
    void ConsumedAndStaleSamples()
    {
        Fixture f; GamepadState pad; pad.Connected = true;
        assert(f.Raw.SetGamepadState(0, pad)); f.Start();
        // 同frameでUI消費sampleの次だけが到達してもheldを捕捉しない。
        pad.Axes[0] = 1; f.Pad(0, pad, false); f.Pad(0, pad); f.Capturing();
        pad.Axes[0] = 0; f.Pad(0, pad);
        pad.Axes[0] = 1; f.Pad(0, pad, false); f.Advance(); f.Capturing();
        f.Pad(0, pad); f.Capturing(); // UI消費後heldを後続sampleから拾わない。
        pad.Axes[0] = 0; f.Pad(0, pad);
        const auto stale = GamepadSampleEvent{0, pad, f.Raw.GetGamepadSampleSerial(0)};
        f.Capture.OnGamepadSample(stale, f.Raw); f.Capturing();
        auto invalid = stale; invalid.State.Axes[0] = std::numeric_limits<float>::quiet_NaN();
        f.Capture.OnGamepadSample(invalid, f.Raw); f.Capturing();
        pad.Axes[0] = -1; f.Pad(0, pad, false);
        f.Capture.OnGamepadSample(stale, f.Raw); f.Capturing();
        f.Advance(); f.Capturing();
        pad.Axes[0] = 0; f.Pad(0, pad);
        pad.Axes[0] = -0.6f; f.Pad(0, pad);
        f.Control(EInputBindingSource::GamepadAxis, 0, -1);
    }
    void RelativeInputs()
    {
        Fixture f; f.Start();
        f.Capture.OnMouseRawMove({100, 0}, f.Raw); f.Capturing();
        f.Capture.OnMouseScroll({0, -1}, f.Raw);
        f.Control(EInputBindingSource::MouseWheel, 1, -1);
        f.Advance(); f.Waiting();
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw); f.Capture.OnMouseScroll({1, 0}, f.Raw); f.Advance(); f.Waiting();
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw);
        assert(f.Raw.AddMouseScrollAxes(1, 0)); assert(f.Raw.AddMouseScrollAxes(-1, 0));
        assert(f.Raw.GetMouseState().ScrollDelta == 0);
        f.Advance(); f.Waiting(); // UI消費でeventが来ず累積が相殺されても静止ではない。
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw);
        f.Capture.OnMouseRawMove({100, 0}, f.Raw); f.Advance(); f.Finished();
        InputRebindCaptureOptions options; options.AllowedSources = InputRebindSourceBit(EInputBindingSource::MouseDelta);
        f.Start(options);
        f.Capture.OnMouseRawMove({1, -2}, f.Raw); f.Capturing();
        f.Capture.OnMouseRawMove({0, std::numeric_limits<float>::infinity()}, f.Raw); f.Capturing();
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw); f.Capture.OnMouseRawMove({1, -3}, f.Raw);
        f.Control(EInputBindingSource::MouseDelta, 1, -1);
        f.Capture.Cancel(); f.Advance(); f.Waiting();
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw);
        assert(f.Raw.AddRawMouseDelta(2, -1)); assert(f.Raw.AddRawMouseDelta(-2, 1));
        f.Advance(); f.Waiting();
        f.Raw.BeginFrame(); f.Capture.BeginFrame(f.Raw); f.Advance(); f.Finished(EInputRebindOutcome::Cancelled);
        f.Start(); f.Key(KeyCode::A, InputAction::Pressed); f.Key(KeyCode::A, InputAction::Released);
        f.Capture.OnMouseRawMove({9, 9}, f.Raw); f.Advance(); f.Finished();
    }
    void BackgroundAndBaselineCapture()
    {
        Fixture f;
        GamepadState pad;
        pad.Connected = true;
        assert(f.Raw.SetGamepadState(0, pad));
        f.Start();
        pad.Buttons = static_cast<uint16_t>(GamepadButton::A);
        pad.Axes[0] = 0.9f;
        f.Pad(0, pad, true, EGamepadSampleMode::Baseline);
        f.Capturing();
        assert(!f.Raw.IsGamepadButtonPressed(0, GamepadButton::A));
        f.Pad(0, pad); f.Capturing(); // 復帰時heldは次のLive sampleでも再捕捉しない。
        pad.Buttons = 0; pad.Axes[0] = 0;
        f.Pad(0, pad); f.Capturing();
        pad.Axes[0] = -0.9f;
        f.Pad(0, pad);
        f.Control(EInputBindingSource::GamepadAxis, 0, -1);
        f.Advance(); f.Waiting();
        f.Pad(0, pad, true, EGamepadSampleMode::Background);
        assert(f.Raw.GetGamepadAxis(0, GamepadAxis::LeftX) == 0);
        f.Advance(); f.Waiting(); // 正本neutralでも実sampleが非neutralなら解除待ちを続ける。
        pad.Axes[0] = 0;
        f.Pad(0, pad, true, EGamepadSampleMode::Background);
        f.Advance(); f.Finished();

        pad.Buttons = static_cast<uint16_t>(GamepadButton::A);
        assert(f.Raw.SetGamepadState(0, pad, EGamepadSampleMode::Background));
        f.Start();
        f.Key(KeyCode::Q, InputAction::Pressed);
        f.Key(KeyCode::Q, InputAction::Released);
        f.Advance(); f.Waiting(); // 開始時のbackground実heldも待つ。
        pad.Buttons = 0;
        f.Pad(0, pad, true, EGamepadSampleMode::Background);
        f.Advance(); f.Finished();
        f.Start();
        pad.Axes[0] = 0.9f;
        assert(f.Raw.SetGamepadState(0, pad));
        auto invalid = GamepadSampleEvent{0, pad, f.Raw.GetGamepadSampleSerial(0), static_cast<EGamepadSampleMode>(255)};
        f.Capture.OnGamepadSample(invalid, f.Raw);
        f.Capturing();
        invalid.Mode = EGamepadSampleMode::Live;
        f.Capture.OnGamepadSample(invalid, f.Raw); // invalid modeではこの新serialも消費していない。
        f.Control(EInputBindingSource::GamepadAxis, 0);
        f.Waiting();
    }
}
int main()
{
    DigitalAndReset(); Modifiers(); CancellationAndValidation(); AnalogAndConnections(); ConsumedAndStaleSamples(); RelativeInputs();
    BackgroundAndBaselineCapture();
    std::puts("InputRebindCaptureStateTest passed");
    return 0;
}
