#pragma once

#include "Input/InputState.h"
#include <bit>

namespace NorvesLib::Core::Input
{
    // Router到達イベント専用。InputSystemの正本更新後に呼ぶ。
    // Delegateの無条件通知や後段の最終状態だけからPressedを作ってはいけない。
    // 同じInputState正本を使い続ける。差替え/再初期化時はResetして世代の一致を持ち越さない。
    class InputArmedState
    {
    public:
        void OnKey(const KeyEvent& event, const InputState& raw)
        {
            const auto index = static_cast<uint32_t>(event.Code);
            if (event.Code == KeyCode::None || index >= KeyCount) return;
            if (event.Action == InputAction::Pressed)
            {
                m_Keys[index] = {raw.IsKeyDown(event.Code), raw.GetKeyReleaseSerial(event.Code)};
            }
            else if (event.Action == InputAction::Released)
            {
                m_Keys[index].Armed = false;
            }
        }

        void OnMouseButton(const MouseButtonEvent& event, const InputState& raw)
        {
            const auto index = static_cast<uint32_t>(event.Button);
            if (index >= MouseCount) return;
            if (event.Action == InputAction::Pressed)
            {
                m_Mouse[index] = {raw.IsMouseButtonDown(event.Button), raw.GetMouseButtonReleaseSerial(event.Button)};
            }
            else if (event.Action == InputAction::Released)
            {
                m_Mouse[index].Armed = false;
            }
        }

        void OnGamepadButton(const GamepadButtonEvent& event, const InputState& raw)
        {
            const auto code = static_cast<uint16_t>(event.Button);
            if (event.Slot >= GamepadSlotCount || !IsValidGamepadButton(code)) return;
            auto& entry = m_Gamepad[event.Slot][std::countr_zero(code)];
            if (event.Action == InputAction::Pressed)
                entry = {raw.IsGamepadButtonDown(event.Slot, event.Button), raw.GetGamepadButtonReleaseSerial(event.Slot, event.Button)};
            else if (event.Action == InputAction::Released) entry.Armed = false;
        }
        bool IsGamepadButtonArmed(uint8_t slot, GamepadButton button, const InputState& raw) const
        {
            const auto code = static_cast<uint16_t>(button);
            if (slot >= GamepadSlotCount || !IsValidGamepadButton(code)) return false;
            const auto& entry = m_Gamepad[slot][std::countr_zero(code)];
            return entry.Armed && raw.IsGamepadButtonDown(slot, button) &&
                entry.ReleaseSerial == raw.GetGamepadButtonReleaseSerial(slot, button);
        }
        void ResetGamepad(uint8_t slot)
        {
            if (slot < GamepadSlotCount)
                for (auto& entry : m_Gamepad[slot]) entry = {};
        }

        bool IsKeyArmed(KeyCode code, const InputState& raw) const
        {
            const auto index = static_cast<uint32_t>(code);
            return code != KeyCode::None && index < KeyCount && m_Keys[index].Armed && raw.IsKeyDown(code) &&
                m_Keys[index].ReleaseSerial == raw.GetKeyReleaseSerial(code);
        }
        bool IsMouseButtonArmed(MouseButton button, const InputState& raw) const
        {
            const auto index = static_cast<uint32_t>(button);
            return index < MouseCount && m_Mouse[index].Armed && raw.IsMouseButtonDown(button) &&
                m_Mouse[index].ReleaseSerial == raw.GetMouseButtonReleaseSerial(button);
        }
        bool IsShiftArmed(const InputState& raw) const
        {
            return IsKeyArmed(KeyCode::LeftShift, raw) || IsKeyArmed(KeyCode::RightShift, raw);
        }
        bool IsCtrlArmed(const InputState& raw) const
        {
            return IsKeyArmed(KeyCode::LeftCtrl, raw) || IsKeyArmed(KeyCode::RightCtrl, raw);
        }
        bool IsAltArmed(const InputState& raw) const
        {
            return IsKeyArmed(KeyCode::LeftAlt, raw) || IsKeyArmed(KeyCode::RightAlt, raw);
        }

        void Reconcile(const InputState& raw)
        {
            for (uint32_t index = 0; index < KeyCount; ++index)
                if (!IsKeyArmed(static_cast<KeyCode>(index), raw)) m_Keys[index].Armed = false;
            for (uint32_t index = 0; index < MouseCount; ++index)
                if (!IsMouseButtonArmed(static_cast<MouseButton>(index), raw)) m_Mouse[index].Armed = false;
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
                for (uint32_t bit = 0; bit < 16; ++bit)
                    if (!IsGamepadButtonArmed(slot, static_cast<GamepadButton>(1u << bit), raw)) m_Gamepad[slot][bit].Armed = false;
        }
        void Reset()
        {
            for (auto& entry : m_Keys) entry = {};
            for (auto& entry : m_Mouse) entry = {};
            for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot) ResetGamepad(slot);
        }

    private:
        struct Entry { bool Armed = false; uint64_t ReleaseSerial = 0; };
        static constexpr uint32_t KeyCount = static_cast<uint32_t>(KeyCode::Count);
        static constexpr uint32_t MouseCount = static_cast<uint32_t>(MouseButton::Count);
        Entry m_Keys[KeyCount]{};
        Entry m_Mouse[MouseCount]{};
        Entry m_Gamepad[GamepadSlotCount][16]{};
    };
} // namespace NorvesLib::Core::Input
