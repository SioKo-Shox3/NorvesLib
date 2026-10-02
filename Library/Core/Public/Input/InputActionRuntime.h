#pragma once

#include "Input/InputActionSettings.h"
#include "Input/InputArmedState.h"
#include <algorithm>
#include <span>

namespace NorvesLib::Core::Input
{
    // Identity/配列所有に依存しない評価核。spanを保持せず、同じcompile済みbindingsを呼出し中だけ読む。
    class InputActionRuntime
    {
    public:
        bool Configure(const InputActionSettings& settings)
        {
            if (!IsValidInputActionSettings(settings)) return false;
            InputButtonState button;
            (void)button.SetTiming(settings.ButtonTiming);
            (void)button.AdvanceTo(m_Button.GetTime());
            m_Settings = settings;
            m_Button = button;
            m_Axis = {};
            m_RelativeX = m_RelativeY = 0;
            m_Configured = true;
            return true;
        }
        const InputActionSettings& GetSettings() const { return m_Settings; }
        const InputButtonSnapshot& GetButton() const { return m_Button.GetState(); }
        Math::Vector2 GetAxis() const { return m_Axis; }
        bool ConsumeFixedPress() { return m_Button.ConsumeFixedPress(); }
        bool HasPendingFixedPress() const { return m_Button.HasPendingFixedPress(); }
        double GetTime() const { return m_Button.GetTime(); }

        bool BeginFrame(double time)
        {
            if (!m_Configured || !ValidTime(time)) return false;
            m_Button.BeginFrame();
            (void)m_Button.AdvanceTo(time);
            m_RelativeX = m_RelativeY = 0;
            m_Axis = {};
            return true;
        }
        void Cancel()
        {
            m_Button.Cancel();
            m_Axis = {};
            m_RelativeX = m_RelativeY = 0;
        }

        // 到達した物理イベントごとに呼び、同frameのpress/release順を保持する。
        bool SyncButtons(std::span<const InputBinding> bindings, const InputState& raw, const InputArmedState& armed)
        {
            if (!ValidBindings(bindings)) return false;
            if (m_Settings.Type == EInputMappingValueType::Button)
                m_Button.SetDown(AnyPersistentButtonDown(bindings, raw, armed));
            return true;
        }

        bool AccumulateRelative(EInputBindingSource kind, uint16_t code, float amount,
            std::span<const InputBinding> bindings, const InputState& raw, const InputArmedState& armed)
        {
            if (!IsDisplacementSource(kind) || code >= 2 || !std::isfinite(amount) || !ValidBindings(bindings)) return false;
            double nextX = m_RelativeX, nextY = m_RelativeY;
            bool impulse = false;
            for (const auto& binding : bindings)
            {
                if (binding.Source.Kind != kind || binding.Source.Code != code || !ModifiersAllowed(binding, raw, armed)) continue;
                const double value = Scale(amount, binding);
                if (m_Settings.Type == EInputMappingValueType::Button)
                    impulse = impulse || IsButtonValue(value, binding);
                else if (binding.Component == EInputAxisComponent::X) nextX += value;
                else nextY += value;
            }
            if (!std::isfinite(nextX) || !std::isfinite(nextY)) return false;
            if (m_Settings.Type == EInputMappingValueType::Button)
            {
                const bool persistent = AnyPersistentButtonDown(bindings, raw, armed);
                m_Button.SetDown(persistent);
                if (impulse)
                {
                    m_Button.SetDown(true);
                    m_Button.SetDown(persistent);
                }
            }
            else
            {
                m_RelativeX = nextX;
                m_RelativeY = nextY;
            }
            return true;
        }

        bool Update(std::span<const InputBinding> bindings, const InputState& raw, const InputArmedState& armed,
            double time, double unscaledDeltaSeconds)
        {
            if (!ValidBindings(bindings) || !ValidTime(time) || !std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0) return false;
            Math::Vector2 nextAxis;
            bool down = false;
            if (m_Settings.Type == EInputMappingValueType::Button)
            {
                down = AnyPersistentButtonDown(bindings, raw, armed);
            }
            else
            {
                double x = 0, y = 0;
                for (const auto& binding : bindings)
                {
                    const double value = ReadPersistent(binding, raw, armed);
                    if (binding.Component == EInputAxisComponent::X) x += value;
                    else y += value;
                }
                if (!std::isfinite(x) || !std::isfinite(y)) return false;
                Math::Vector2 normalized;
                if (m_Settings.Type == EInputMappingValueType::Axis1D)
                {
                    if (!TryApplyAxisResponseWide(x, m_Settings.AxisResponse, normalized.x)) return false;
                }
                else
                {
                    if (!TryApplyRadialAxisResponseWide(x, y, m_Settings.AxisResponse, normalized)) return false;
                }
                nextAxis = normalized;
                if (m_Settings.Output == EInputAxisOutput::FrameDelta)
                {
                    if (!TryComputeLookDeltaWide(m_RelativeX, normalized.x, m_Settings.MouseSensitivity,
                        m_Settings.RateSensitivity, unscaledDeltaSeconds, nextAxis.x)) return false;
                    if (m_Settings.Type == EInputMappingValueType::Axis2D &&
                        !TryComputeLookDeltaWide(m_RelativeY, normalized.y, m_Settings.MouseSensitivity,
                            m_Settings.RateSensitivity, unscaledDeltaSeconds, nextAxis.y)) return false;
                }
            }
            // 物理イベントは現在のframe時刻で反映し、その後Update時刻まで進める。
            if (m_Settings.Type == EInputMappingValueType::Button) m_Button.SetDown(down);
            (void)m_Button.AdvanceTo(time);
            m_Axis = nextAxis;
            return true;
        }

    private:
        bool ValidTime(double time) const { return std::isfinite(time) && time >= m_Button.GetTime(); }
        bool ValidBindings(std::span<const InputBinding> bindings) const
        {
            return m_Configured && IsValidInputActionBindings(m_Settings, bindings);
        }
        static bool ModifiersAllowed(const InputBinding& binding, const InputState& raw, const InputArmedState& armed)
        {
            return ((binding.RequiredModifiers & InputModifierShift) == 0 || armed.IsShiftArmed(raw)) &&
                ((binding.RequiredModifiers & InputModifierCtrl) == 0 || armed.IsCtrlArmed(raw)) &&
                ((binding.RequiredModifiers & InputModifierAlt) == 0 || armed.IsAltArmed(raw));
        }
        static double Scale(double value, const InputBinding& binding)
        {
            return value * binding.Scale * (binding.Invert ? -1.0 : 1.0);
        }
        static bool IsButtonValue(double value, const InputBinding& binding)
        {
            return value > 0 && value >= binding.ButtonThreshold;
        }
        static double ReadPersistent(const InputBinding& binding, const InputState& raw, const InputArmedState& armed)
        {
            if (!ModifiersAllowed(binding, raw, armed)) return 0;
            const auto& source = binding.Source;
            double value = 0;
            switch (source.Kind)
            {
            case EInputBindingSource::Key:
                value = armed.IsKeyArmed(static_cast<KeyCode>(source.Code), raw) ? 1 : 0; break;
            case EInputBindingSource::MouseButton:
                value = armed.IsMouseButtonArmed(static_cast<MouseButton>(source.Code), raw) ? 1 : 0; break;
            case EInputBindingSource::GamepadButton:
                value = armed.IsGamepadButtonArmed(source.Slot, static_cast<GamepadButton>(source.Code), raw) ? 1 : 0; break;
            case EInputBindingSource::GamepadAxis:
                value = raw.GetGamepadAxis(source.Slot, static_cast<GamepadAxis>(source.Code)); break;
            case EInputBindingSource::GamepadTrigger:
                value = raw.GetGamepadTrigger(source.Slot, static_cast<GamepadTrigger>(source.Code)); break;
            default: break;
            }
            return Scale(value, binding);
        }
        static bool AnyPersistentButtonDown(std::span<const InputBinding> bindings, const InputState& raw, const InputArmedState& armed)
        {
            for (const auto& binding : bindings)
                if (!IsDisplacementSource(binding.Source.Kind) && IsButtonValue(ReadPersistent(binding, raw, armed), binding)) return true;
            return false;
        }

        InputActionSettings m_Settings;
        InputButtonState m_Button;
        Math::Vector2 m_Axis;
        double m_RelativeX = 0, m_RelativeY = 0;
        bool m_Configured = false;
    };
} // namespace NorvesLib::Core::Input
