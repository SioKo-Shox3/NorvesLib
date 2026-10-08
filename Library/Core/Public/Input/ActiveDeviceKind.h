#pragma once

#include "Input/GamepadTypes.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Input
{
    enum class EInputDeviceKind : uint8_t
    {
        KeyboardMouse, Gamepad
    };
    struct InputDeviceActivitySettings
    {
        double MinimumSwitchSeconds = .3;
        float MouseMoveThreshold = 2;
        float LeftStickDeadZone = .24f;
        float RightStickDeadZone = .27f;
        float TriggerThreshold = .12f;
        float AnalogChangeThreshold = .02f;
    };
    inline bool IsValidInputDeviceActivitySettings(const InputDeviceActivitySettings& value)
    {
        return std::isfinite(value.MinimumSwitchSeconds) && value.MinimumSwitchSeconds >= 0 &&
            std::isfinite(value.MouseMoveThreshold) && value.MouseMoveThreshold > 0 &&
            std::isfinite(value.LeftStickDeadZone) && value.LeftStickDeadZone >= 0 && value.LeftStickDeadZone < 1 &&
            std::isfinite(value.RightStickDeadZone) && value.RightStickDeadZone >= 0 && value.RightStickDeadZone < 1 &&
            std::isfinite(value.TriggerThreshold) && value.TriggerThreshold >= 0 && value.TriggerThreshold < 1 &&
            std::isfinite(value.AnalogChangeThreshold) && value.AnalogChangeThreshold > 0 && value.AnalogChangeThreshold <= 1;
    }
    // 表示用の最後の有効入力源。操作のdeadzone/値を変更せず、時刻/設定を値で所有する。
    // Observe系のtrueはkindが変わったことを示す。拒否された活動は後へ予約しない。
    class ActiveDeviceKindState
    {
    public:
        bool Configure(const InputDeviceActivitySettings& settings) noexcept
        {
            if (!IsValidInputDeviceActivitySettings(settings))
            {
                return false;
            }
            m_Settings = settings;
            ResetMouseTravel();
            ResetPadAnchors();
            return true;
        }
        InputDeviceActivitySettings GetSettings() const noexcept { return m_Settings; }
        EInputDeviceKind GetKind() const noexcept { return m_Kind; }
        bool HasTime() const noexcept { return m_bHasTime; }
        bool CanBeginFrame(double time) const noexcept
        {
            return std::isfinite(time) && time >= 0 && (!m_bHasTime || time >= m_Time);
        }
        bool BeginFrame(double time) noexcept
        {
            if (!CanBeginFrame(time))
            {
                return false;
            }
            m_Time = time;
            m_bHasTime = true;
            ResetMouseTravel();
            return true;
        }
        void SetFocused(bool focused) noexcept
        {
            if (m_bFocused != focused)
            {
                ResetMouseTravel();
                ResetPadAnchors();
            }
            m_bFocused = focused;
        }
        bool ObserveKey(KeyCode code, InputAction action, bool wasDown) noexcept
        {
            return code > KeyCode::None && code < KeyCode::Count && action == InputAction::Pressed && !wasDown &&
                Observe(EInputDeviceKind::KeyboardMouse);
        }
        bool ObserveMouseButton(MouseButton button, InputAction action, bool wasDown) noexcept
        {
            return static_cast<uint16_t>(button) < static_cast<uint16_t>(MouseButton::Count) &&
                action == InputAction::Pressed && !wasDown && Observe(EInputDeviceKind::KeyboardMouse);
        }
        bool ObserveMouseMove(float x, float y, bool raw) noexcept
        {
            if (!m_bFocused || !m_bHasTime || !std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0))
            {
                return false;
            }
            const double distance = std::hypot(static_cast<double>(x),static_cast<double>(y));
            auto& travel = raw ? m_RawMouseTravel : m_AbsoluteMouseTravel;
            travel = std::min(static_cast<double>(m_Settings.MouseMoveThreshold),travel + distance);
            // 同じ移動をRaw/絶対の両経路で受けても足し合わせない。
            return std::max(m_RawMouseTravel,m_AbsoluteMouseTravel) >= m_Settings.MouseMoveThreshold &&
                Observe(EInputDeviceKind::KeyboardMouse);
        }
        bool ObserveMouseScroll(float vertical, float horizontal) noexcept
        {
            return std::isfinite(vertical) && std::isfinite(horizontal) && (vertical != 0 || horizontal != 0) &&
                Observe(EInputDeviceKind::KeyboardMouse);
        }
        bool ObserveCharacter(uint32_t codepoint) noexcept
        {
            return codepoint > 0 && codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF) &&
                Observe(EInputDeviceKind::KeyboardMouse);
        }
        bool ObserveGamepad(uint8_t slot, const GamepadState& previousPhysical, const GamepadState& current,
            EGamepadSampleMode mode) noexcept
        {
            if (slot >= GamepadSlotCount || !IsValidGamepadSampleMode(mode) || !m_bFocused || !m_bHasTime ||
                !IsValidGamepadState(previousPhysical) || !IsValidGamepadState(current))
            {
                return false;
            }
            if (!current.Connected)
            {
                m_bHasPadAnchor[slot] = false;
                return false;
            }
            auto& anchor = m_PadAnchors[slot];
            if (mode != EGamepadSampleMode::Live)
            {
                anchor = current;
                m_bHasPadAnchor[slot] = true;
                return false;
            }
            if (!m_bHasPadAnchor[slot])
            {
                anchor = previousPhysical;
                m_bHasPadAnchor[slot] = true;
            }
            bool active = (current.Buttons & ~previousPhysical.Buttons) != 0;
            // 最後の有意な位置を基準にして、ゆっくりした操作も累積変位で拾う。
            active = active || StickChanged(anchor,current,GamepadAxis::LeftX,m_Settings.LeftStickDeadZone) ||
                StickChanged(anchor,current,GamepadAxis::RightX,m_Settings.RightStickDeadZone);
            for (uint8_t index = 0; index < static_cast<uint8_t>(GamepadTrigger::Count); ++index)
            {
                active = active || (current.Triggers[index] > m_Settings.TriggerThreshold &&
                    std::abs(static_cast<double>(current.Triggers[index]) - anchor.Triggers[index]) >=
                        m_Settings.AnalogChangeThreshold);
            }
            if (active)
            {
                anchor = current;
                return Observe(EInputDeviceKind::Gamepad);
            }
            RebaseQuietStick(anchor,current,GamepadAxis::LeftX,m_Settings.LeftStickDeadZone);
            RebaseQuietStick(anchor,current,GamepadAxis::RightX,m_Settings.RightStickDeadZone);
            for (uint8_t index = 0; index < static_cast<uint8_t>(GamepadTrigger::Count); ++index)
            {
                if (current.Triggers[index] <= m_Settings.TriggerThreshold)
                {
                    anchor.Triggers[index] = current.Triggers[index];
                }
            }
            return false;
        }
    private:
        bool Observe(EInputDeviceKind kind) noexcept
        {
            if (!m_bHasTime || !m_bFocused || kind == m_Kind)
            {
                return false;
            }
            if (m_bHasSwitched && m_Time - m_LastSwitch < m_Settings.MinimumSwitchSeconds)
            {
                return false;
            }
            m_Kind = kind;
            m_LastSwitch = m_Time;
            m_bHasSwitched = true;
            return true;
        }
        bool StickChanged(const GamepadState& previous, const GamepadState& current,
            GamepadAxis first, float deadzone) const noexcept
        {
            const auto x = static_cast<uint8_t>(first);
            const auto y = static_cast<uint8_t>(x + 1);
            const double cx = current.Axes[x], cy = current.Axes[y];
            const double dx = cx - previous.Axes[x], dy = cy - previous.Axes[y];
            return cx*cx + cy*cy > static_cast<double>(deadzone)*deadzone &&
                dx*dx + dy*dy >= static_cast<double>(m_Settings.AnalogChangeThreshold)*m_Settings.AnalogChangeThreshold;
        }
        static void RebaseQuietStick(GamepadState& anchor, const GamepadState& current,
            GamepadAxis first, float deadzone) noexcept
        {
            const auto x = static_cast<uint8_t>(first);
            const auto y = static_cast<uint8_t>(x + 1);
            const double cx = current.Axes[x], cy = current.Axes[y];
            if (cx*cx + cy*cy <= static_cast<double>(deadzone)*deadzone)
            {
                anchor.Axes[x] = current.Axes[x];
                anchor.Axes[y] = current.Axes[y];
            }
        }
        void ResetPadAnchors() noexcept
        {
            for (auto& valid : m_bHasPadAnchor)
            {
                valid = false;
            }
        }
        void ResetMouseTravel() noexcept
        {
            m_RawMouseTravel = 0;
            m_AbsoluteMouseTravel = 0;
        }
        GamepadState m_PadAnchors[GamepadSlotCount]{};
        bool m_bHasPadAnchor[GamepadSlotCount]{};
        InputDeviceActivitySettings m_Settings;
        EInputDeviceKind m_Kind = EInputDeviceKind::KeyboardMouse;
        double m_Time = 0, m_LastSwitch = 0;
        double m_RawMouseTravel = 0, m_AbsoluteMouseTravel = 0;
        bool m_bHasTime = false, m_bHasSwitched = false, m_bFocused = true;
    };
} // namespace NorvesLib::Core::Input
