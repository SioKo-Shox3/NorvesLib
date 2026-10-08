#pragma once

#include "Input/InputArmedState.h"
#include "Input/InputBindingTypes.h"

namespace NorvesLib::Core::Input
{
    enum class EInputRebindPhase : uint8_t
    {
        Idle, Capturing, AwaitingNeutral, Finished
    };
    enum class EInputRebindOutcome : uint8_t
    {
        None, Captured, Cancelled
    };
    using InputRebindSourceMask = uint8_t;
    inline constexpr InputRebindSourceMask InputRebindSourceBit(EInputBindingSource source)
    {
        const auto index = static_cast<uint8_t>(source);
        return index <= static_cast<uint8_t>(EInputBindingSource::GamepadTrigger)
        ? static_cast<InputRebindSourceMask>(1u << index) : 0;
    }
    inline constexpr InputRebindSourceMask AllInputRebindSources = 0x7f;

    struct InputRebindCaptureOptions
    {
        // 通常のマウス移動を誤捕捉しない。相対軸の割当時だけ明示的に有効化する。
        InputRebindSourceMask AllowedSources = AllInputRebindSources & ~InputRebindSourceBit(EInputBindingSource::MouseDelta);
        float ActivationThreshold = 0.6f;
        float NeutralThreshold = 0.2f;
        float MouseMotionThreshold = 4.0f;
        bool CaptureModifiers = true;
    };
    inline bool IsValidInputRebindCaptureOptions(const InputRebindCaptureOptions& options)
    {
        return options.AllowedSources != 0 && (options.AllowedSources & ~AllInputRebindSources) == 0 &&
        std::isfinite(options.ActivationThreshold) && std::isfinite(options.NeutralThreshold) &&
        options.NeutralThreshold >= 0 && options.NeutralThreshold < options.ActivationThreshold &&
        options.ActivationThreshold <= 1 && std::isfinite(options.MouseMotionThreshold) && options.MouseMotionThreshold > 0;
    }
    struct InputCapturedControl
    {
        InputPhysicalSource Source;
        InputModifierMask RequiredModifiers = 0;
        // 軸/相対入力の操作方向。最終bindingのScale/Invert/Componentは呼出側が決める。
        int8_t Direction = 0;
    };

    // GameThread専用の値状態。正本更新後、Routerで到達したイベントだけを順番に渡す。
    // BeginFrame→イベント群→Advanceの順。同じInputStateを使用し、外部reset/focus喪失時はAbortする。
    // Begin直後の内部ReleaseAllは許可するが、開始時heldの物理Releasedは必ず配送すること。
    // 開始時heldは正本の人工的なupから解除扱いにしない。配送不能ならAbortで終了させる。
    // 結果はFinishedかつCapturedでのみ確定。所有/登録/設定反映/保存は呼出側の責任。
    class InputRebindCaptureState
    {
    public:
        bool Begin(const InputRebindCaptureOptions& options, const InputState& raw)
        {
            if (IsBlocking() || !IsValidInputRebindCaptureOptions(options))
            {
                return false;
            }

            *this = InputRebindCaptureState{};
            m_Options = options;
            m_Phase = EInputRebindPhase::Capturing;
            BeginFrame(raw);
            for (uint16_t i = 1; i < KeyCount; ++i)
            {
                m_InitialKeys[i] = raw.IsKeyDown(static_cast<KeyCode>(i));
            }

            for (uint8_t i = 0; i < MouseCount; ++i)
            {
                m_InitialMouse[i] = raw.IsMouseButtonDown(static_cast<MouseButton>(i));
            }

            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                m_Pads[slot].Serial = raw.GetGamepadSampleSerial(slot);
                BaselinePad(m_Pads[slot], raw.GetLastGamepadSample(slot));
            }
            return true;
        }
        void BeginFrame(const InputState& raw)
        {
            m_RelativeSeen = false;
            m_RawFrameSerial = raw.GetRawMouseActivitySerial();
            m_ScrollFrameSerial = raw.GetMouseScrollActivitySerial();
        }
        EInputRebindPhase GetPhase() const
        {
            return m_Phase;
        }
        EInputRebindOutcome GetOutcome() const
        {
            return m_Outcome;
        }
        InputCapturedControl GetControl() const
        {
            return m_Control;
        }
        bool IsBlocking() const
        {
            return m_Phase == EInputRebindPhase::Capturing || m_Phase == EInputRebindPhase::AwaitingNeutral;
        }
        void Cancel()
        {
            if (!IsBlocking())
            {
                return;
            }

            m_Outcome = EInputRebindOutcome::Cancelled;
            m_Phase = EInputRebindPhase::AwaitingNeutral;
        }
        void Abort()
        {
            if (!IsBlocking())
            {
                return;
            }

            m_Outcome = EInputRebindOutcome::Cancelled;
            m_Phase = EInputRebindPhase::Finished;
        }

        void OnKey(const KeyEvent& event, const InputState& raw)
        {
            const auto code = static_cast<uint16_t>(event.Code);
            if (!IsBlocking() || code == 0 || code >= KeyCount)
            {
                return;
            }

            const bool down = raw.IsKeyDown(event.Code);
            if (event.Action == InputAction::Released && !down)
            {
                const bool wasInitial = m_InitialKeys[code];
                m_InitialKeys[code] = false;
                auto& pending = m_ModifierKeys[code];
                if (!wasInitial && pending.Pending && raw.GetKeyReleaseSerial(event.Code) == pending.Serial + uint64_t{1})
                {
                    Capture({EInputBindingSource::Key, code, 0}, 1,
                        static_cast<InputModifierMask>(Modifiers(raw) & ~ModifierGroup(event.Code)));
                }

                pending.Pending = false;
                m_Armed.OnKey(event, raw);
                return;
            }
            if (event.Action != InputAction::Pressed || !down)
            {
                return;
            }

            if (event.Code == KeyCode::Escape)
            {
                Cancel();
                return;
            }
            if (m_InitialKeys[code] || m_Phase != EInputRebindPhase::Capturing)
            {
                return;
            }

            m_Armed.OnKey(event, raw);
            if (ModifierGroup(event.Code) != 0 && m_Options.CaptureModifiers)
            {
                m_ModifierKeys[code] = {true, raw.GetKeyReleaseSerial(event.Code)};
                return;
            }
            Capture({EInputBindingSource::Key, code, 0}, 1, Modifiers(raw));
        }
        void OnMouseButton(const MouseButtonEvent& event, const InputState& raw)
        {
            const auto code = static_cast<uint8_t>(event.Button);
            if (!IsBlocking() || code >= MouseCount)
            {
                return;
            }

            if (event.Action == InputAction::Released && !raw.IsMouseButtonDown(event.Button))
            {
                m_InitialMouse[code] = false;
            }
            else if (event.Action == InputAction::Pressed && raw.IsMouseButtonDown(event.Button) && !m_InitialMouse[code])
            {
                Capture({EInputBindingSource::MouseButton, code, 0}, 1, Modifiers(raw));
            }
        }
        void OnMouseRawMove(const MouseRawMoveEvent& event, const InputState& raw)
        {
            Relative(EInputBindingSource::MouseDelta, event.DeltaX, event.DeltaY, raw);
        }
        void OnMouseScroll(const MouseScrollEvent& event, const InputState& raw)
        {
            Relative(EInputBindingSource::MouseWheel, event.Delta, event.HorizontalDelta, raw);
        }
        void OnGamepadConnection(const GamepadConnectionEvent& event)
        {
            if (!IsBlocking() || event.Slot >= GamepadSlotCount)
            {
                return;
            }

            auto& pad = m_Pads[event.Slot];
            pad.AwaitingSample = event.Connected;
            if (!event.Connected)
            {
                BaselinePad(pad, {});
            }
        }
        void OnGamepadButton(const GamepadButtonEvent& event, const InputState& raw)
        {
            const auto button = static_cast<uint16_t>(event.Button);
            if (!IsBlocking() || event.Slot >= GamepadSlotCount || !IsValidGamepadButton(button))
            {
                return;
            }

            auto& pad = m_Pads[event.Slot];
            if (event.Action == InputAction::Released && !raw.IsGamepadButtonDown(event.Slot, event.Button))
            {
                pad.BlockedButtons &= static_cast<uint16_t>(~button);
            }
            else if (event.Action == InputAction::Pressed && !pad.AwaitingSample &&
                (pad.BlockedButtons & button) == 0 && raw.IsGamepadButtonDown(event.Slot, event.Button))
            {
                Capture({EInputBindingSource::GamepadButton, button, event.Slot}, 1, Modifiers(raw));
            }
        }
        void OnGamepadSample(const GamepadSampleEvent& event, const InputState& raw)
        {
            if (!IsBlocking() || event.Slot >= GamepadSlotCount || !IsValidGamepadSampleMode(event.Mode) ||
                !IsValidGamepadState(event.State))
            {
                return;
            }

            auto& pad = m_Pads[event.Slot];
            // 正本より古いeventや重複を候補にしない（uint64 wrapを含む半範囲順序）。
            if (event.Serial != raw.GetGamepadSampleSerial(event.Slot) || !IsNewer(event.Serial, pad.Serial))
            {
                return;
            }

            const bool skippedSample = event.Serial - pad.Serial != 1;
            pad.Serial = event.Serial;
            if (event.Mode != EGamepadSampleMode::Live || skippedSample || pad.AwaitingSample ||
                (!pad.Sample.Connected && event.State.Connected))
            {
                BaselinePad(pad, event.State);
                return;
            }
            ApplyPadSample(pad, event.State, event.Slot, raw);
        }
        void Advance(const InputState& raw)
        {
            if (!IsBlocking())
            {
                return;
            }

            m_Armed.Reconcile(raw);
            for (uint16_t code = 1; code < KeyCount; ++code)
            {
                auto& pending = m_ModifierKeys[code];
                if (!raw.IsKeyDown(static_cast<KeyCode>(code)) ||
                    pending.Serial != raw.GetKeyReleaseSerial(static_cast<KeyCode>(code)))
                {
                    pending.Pending = false;
                }
            }
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
            {
                auto& pad = m_Pads[slot];
                const auto serial = raw.GetGamepadSampleSerial(slot);
                if (!IsNewer(serial, pad.Serial))
                {
                    continue;
                }

                pad.Serial = serial;
                // 上位UIが消費したsampleは候補を作らず、次回のneutral待ちへ取り込む。
                BaselinePad(pad, raw.GetLastGamepadSample(slot));
            }
            const bool relativeActivity = m_RelativeSeen || (m_Control.Source.Kind == EInputBindingSource::MouseDelta
                ? raw.GetRawMouseActivitySerial() != m_RawFrameSerial
                : raw.GetMouseScrollActivitySerial() != m_ScrollFrameSerial);
            if (m_Phase != EInputRebindPhase::AwaitingNeutral || (m_WaitRelative && relativeActivity))
            {
                return;
            }

            for (uint16_t code = 1; code < KeyCount; ++code)
            {
                if (m_InitialKeys[code] || raw.IsKeyDown(static_cast<KeyCode>(code)))
                {
                    return;
                }
            }

            for (uint8_t code = 0; code < MouseCount; ++code)
            {
                if (m_InitialMouse[code] || raw.IsMouseButtonDown(static_cast<MouseButton>(code)))
                {
                    return;
                }
            }

            for (const auto& pad : m_Pads)
            {
                if (pad.AwaitingSample || pad.Sample.Buttons != 0)
                {
                    return;
                }

                for (float value : pad.Sample.Axes)
                {
                    if (std::abs(value) > m_Options.NeutralThreshold)
                    {
                        return;
                    }
                }

                for (float value : pad.Sample.Triggers)
                {
                    if (value > m_Options.NeutralThreshold)
                    {
                        return;
                    }
                }
            }
            m_Phase = EInputRebindPhase::Finished;
        }

    private:
        static constexpr uint16_t KeyCount = static_cast<uint16_t>(KeyCode::Count);
        static constexpr uint8_t MouseCount = static_cast<uint8_t>(MouseButton::Count);
        static constexpr uint8_t AxisCount = static_cast<uint8_t>(GamepadAxis::Count);
        static constexpr uint8_t TriggerCount = static_cast<uint8_t>(GamepadTrigger::Count);
        struct PendingModifier
        {
            bool Pending = false;
            uint64_t Serial = 0;
        };
        struct Pad
        {
            GamepadState Sample;
            uint64_t Serial = 0;
            uint16_t BlockedButtons = 0;
            bool AxisReady[AxisCount]{};
            bool TriggerReady[TriggerCount]{};
            bool AwaitingSample = false;
        };
        static bool IsNewer(uint64_t value, uint64_t old)
        {
            const auto delta = value - old;
            return delta != 0 && delta < (uint64_t{1} << 63);
        }
        bool Allows(EInputBindingSource kind) const
        {
            return (m_Options.AllowedSources & InputRebindSourceBit(kind)) != 0;
        }
        static InputModifierMask ModifierGroup(KeyCode code)
        {
            switch (code)
            {
                case KeyCode::LeftShift: case KeyCode::RightShift: return InputModifierShift;
                case KeyCode::LeftCtrl: case KeyCode::RightCtrl: return InputModifierCtrl;
                case KeyCode::LeftAlt: case KeyCode::RightAlt: return InputModifierAlt;
                default: return 0;
            }
        }
        InputModifierMask Modifiers(const InputState& raw) const
        {
            if (!m_Options.CaptureModifiers)
            {
                return 0;
            }

            return static_cast<InputModifierMask>((m_Armed.IsShiftArmed(raw) ? InputModifierShift : 0) |
                (m_Armed.IsCtrlArmed(raw) ? InputModifierCtrl : 0) | (m_Armed.IsAltArmed(raw) ? InputModifierAlt : 0));
        }
        void Capture(InputPhysicalSource source, int8_t direction, InputModifierMask modifiers)
        {
            if (m_Phase != EInputRebindPhase::Capturing || !Allows(source.Kind))
            {
                return;
            }

            m_Control = {source, modifiers, direction};
            m_Outcome = EInputRebindOutcome::Captured;
            m_Phase = EInputRebindPhase::AwaitingNeutral;
            m_WaitRelative = IsDisplacementSource(source.Kind);
            if (m_WaitRelative)
            {
                m_RelativeSeen = true;
            }
        }
        void BaselinePad(Pad& pad, const GamepadState& sample)
        {
            pad.Sample = sample;
            pad.BlockedButtons = sample.Buttons;
            pad.AwaitingSample = false;
            for (uint8_t i = 0; i < AxisCount; ++i)
            {
                pad.AxisReady[i] = std::abs(sample.Axes[i]) <= m_Options.NeutralThreshold;
            }

            for (uint8_t i = 0; i < TriggerCount; ++i)
            {
                pad.TriggerReady[i] = sample.Triggers[i] <= m_Options.NeutralThreshold;
            }
        }
        void ApplyPadSample(Pad& pad, const GamepadState& sample, uint8_t slot, const InputState& raw)
        {
            pad.Sample = sample;
            pad.BlockedButtons &= sample.Buttons;
            float strongest = 0;
            InputPhysicalSource source;
            int8_t direction = 0;
            for (uint8_t i = 0; i < AxisCount; ++i)
            {
                const float value = sample.Axes[i], magnitude = std::abs(value);
                if (magnitude <= m_Options.NeutralThreshold)
                {
                    pad.AxisReady[i] = true;
                }

                if (pad.AxisReady[i] && Allows(EInputBindingSource::GamepadAxis) && magnitude >= m_Options.ActivationThreshold && magnitude > strongest)
                {
                    strongest = magnitude;
                    source = {EInputBindingSource::GamepadAxis, i, slot};
                    direction = value < 0 ? -1 : 1;
                }
            }
            for (uint8_t i = 0; i < TriggerCount; ++i)
            {
                const float value = sample.Triggers[i];
                if (value <= m_Options.NeutralThreshold)
                {
                    pad.TriggerReady[i] = true;
                }

                if (pad.TriggerReady[i] && Allows(EInputBindingSource::GamepadTrigger) && value >= m_Options.ActivationThreshold && value > strongest)
                {
                    strongest = value;
                    source = {EInputBindingSource::GamepadTrigger, i, slot};
                    direction = 1;
                }
            }
            if (strongest > 0)
            {
                Capture(source, direction, Modifiers(raw));
            }
        }
        void Relative(EInputBindingSource kind, float x, float y, const InputState& raw)
        {
            if (!IsBlocking() || !std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0))
            {
                return;
            }

            if (m_WaitRelative && m_Control.Source.Kind == kind)
            {
                m_RelativeSeen = true;
            }

            if (m_Phase != EInputRebindPhase::Capturing || !Allows(kind))
            {
                return;
            }

            double dx = x, dy = y;
            if (kind == EInputBindingSource::MouseDelta)
            {
                dx += m_MotionX; dy += m_MotionY;
                if (!std::isfinite(dx) || !std::isfinite(dy))
                {
                    return;
                }

                m_MotionX = dx; m_MotionY = dy;
                if (std::abs(dx) < m_Options.MouseMotionThreshold && std::abs(dy) < m_Options.MouseMotionThreshold)
                {
                    return;
                }
            }
            const bool second = std::abs(dy) > std::abs(dx);
            Capture({kind, static_cast<uint16_t>(second ? 1 : 0), 0}, (second ? dy : dx) < 0 ? -1 : 1, Modifiers(raw));
        }
        InputRebindCaptureOptions m_Options;
        EInputRebindPhase m_Phase = EInputRebindPhase::Idle;
        EInputRebindOutcome m_Outcome = EInputRebindOutcome::None;
        InputCapturedControl m_Control;
        InputArmedState m_Armed;
        bool m_InitialKeys[KeyCount]{};
        bool m_InitialMouse[MouseCount]{};
        PendingModifier m_ModifierKeys[KeyCount]{};
        Pad m_Pads[GamepadSlotCount]{};
        double m_MotionX = 0, m_MotionY = 0;
        bool m_WaitRelative = false, m_RelativeSeen = false;
        uint64_t m_RawFrameSerial = 0, m_ScrollFrameSerial = 0;
    };
} // namespace NorvesLib::Core::Input
