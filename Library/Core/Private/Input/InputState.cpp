#include "Input/InputState.h"
#include <cstring>
#include <bit>

namespace NorvesLib::Core::Input
{

    InputState::InputState()
        : m_PrevMouseX(0.0f), m_PrevMouseY(0.0f), m_bFirstMouseUpdate(true)
    {
        std::memset(m_KeyStates, 0, sizeof(m_KeyStates));
        std::memset(m_PrevKeyStates, 0, sizeof(m_PrevKeyStates));
        std::memset(m_MouseButtonStates, 0, sizeof(m_MouseButtonStates));
        std::memset(m_PrevMouseButtonStates, 0, sizeof(m_PrevMouseButtonStates));
    }

    bool InputState::IsKeyDown(KeyCode code) const
    {
        uint32_t index = static_cast<uint32_t>(code);
        if (index >= KEY_COUNT)
        {
            return false;
        }
        return m_KeyStates[index];
    }

    bool InputState::IsKeyPressed(KeyCode code) const
    {
        uint32_t index = static_cast<uint32_t>(code);
        if (index >= KEY_COUNT)
        {
            return false;
        }
        return m_KeyStates[index] && !m_PrevKeyStates[index];
    }

    bool InputState::IsKeyReleased(KeyCode code) const
    {
        uint32_t index = static_cast<uint32_t>(code);
        if (index >= KEY_COUNT)
        {
            return false;
        }
        return m_KeyReleasedByReset[index] || (!m_KeyStates[index] && m_PrevKeyStates[index]);
    }

    bool InputState::IsMouseButtonDown(MouseButton button) const
    {
        uint32_t index = static_cast<uint32_t>(button);
        if (index >= MOUSE_BUTTON_COUNT)
        {
            return false;
        }
        return m_MouseButtonStates[index];
    }

    bool InputState::IsMouseButtonPressed(MouseButton button) const
    {
        uint32_t index = static_cast<uint32_t>(button);
        if (index >= MOUSE_BUTTON_COUNT)
        {
            return false;
        }
        return m_MouseButtonStates[index] && !m_PrevMouseButtonStates[index];
    }

    bool InputState::IsMouseButtonReleased(MouseButton button) const
    {
        uint32_t index = static_cast<uint32_t>(button);
        if (index >= MOUSE_BUTTON_COUNT)
        {
            return false;
        }
        return m_MouseReleasedByReset[index] || (!m_MouseButtonStates[index] && m_PrevMouseButtonStates[index]);
    }

    const MouseState &InputState::GetMouseState() const
    {
        return m_MouseState;
    }

    bool InputState::IsAltDown() const
    {
        return IsKeyDown(KeyCode::LeftAlt) || IsKeyDown(KeyCode::RightAlt);
    }

    bool InputState::IsCtrlDown() const
    {
        return IsKeyDown(KeyCode::LeftCtrl) || IsKeyDown(KeyCode::RightCtrl);
    }

    bool InputState::IsShiftDown() const
    {
        return IsKeyDown(KeyCode::LeftShift) || IsKeyDown(KeyCode::RightShift);
    }

    void InputState::BeginFrame()
    {
        // 前フレームの状態を保存
        std::memcpy(m_PrevKeyStates, m_KeyStates, sizeof(m_KeyStates));
        std::memcpy(m_PrevMouseButtonStates, m_MouseButtonStates, sizeof(m_MouseButtonStates));

        std::memset(m_KeyReleasedByReset, 0, sizeof(m_KeyReleasedByReset));
        std::memset(m_MouseReleasedByReset, 0, sizeof(m_MouseReleasedByReset));

        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            m_PrevGamepadStates[slot] = m_GamepadStates[slot];
            m_GamepadPressed[slot] = 0;
            m_GamepadReleased[slot] = 0;
        }

        // フレーム間累積値をリセット
        ResetFrameAccumulators();
    }

    void InputState::ReleaseAll()
    {
        for (uint32_t index = 0; index < KEY_COUNT; ++index)
        {
            if (m_KeyStates[index]) ++m_KeyReleaseSerial[index];
            m_KeyReleasedByReset[index] = m_KeyReleasedByReset[index] || m_KeyStates[index];
            m_KeyStates[index] = false;
        }
        for (uint32_t index = 0; index < MOUSE_BUTTON_COUNT; ++index)
        {
            if (m_MouseButtonStates[index]) ++m_MouseReleaseSerial[index];
            m_MouseReleasedByReset[index] = m_MouseReleasedByReset[index] || m_MouseButtonStates[index];
            m_MouseButtonStates[index] = false;
        }
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            GamepadState neutral;
            neutral.Connected = m_GamepadStates[slot].Connected;
            neutral.PacketNumber = m_GamepadStates[slot].PacketNumber;
            (void)SetGamepadState(slot, neutral);
            m_GamepadPressed[slot] = 0;
        }
        ResetFrameAccumulators();
        m_bFirstMouseUpdate = true;
    }

    uint64_t InputState::GetKeyReleaseSerial(KeyCode code) const
    {
        const auto index = static_cast<uint32_t>(code);
        return index < KEY_COUNT ? m_KeyReleaseSerial[index] : 0;
    }

    uint64_t InputState::GetMouseButtonReleaseSerial(MouseButton button) const
    {
        const auto index = static_cast<uint32_t>(button);
        return index < MOUSE_BUTTON_COUNT ? m_MouseReleaseSerial[index] : 0;
    }

    bool InputState::SetGamepadState(uint8_t slot, const GamepadState& state)
    {
        if (slot >= GamepadSlotCount || !IsValidGamepadState(state)) return false;
        const uint16_t oldButtons = m_GamepadStates[slot].Buttons;
        const uint16_t pressed = static_cast<uint16_t>(state.Buttons & ~oldButtons);
        const uint16_t released = static_cast<uint16_t>(oldButtons & ~state.Buttons);
        m_GamepadPressed[slot] |= pressed;
        m_GamepadReleased[slot] |= released;
        for (uint32_t bit = 0; bit < 16; ++bit)
            if ((released & (1u << bit)) != 0) ++m_GamepadReleaseSerial[slot][bit];
        m_GamepadStates[slot] = state;
        if (!state.Connected) m_GamepadPressed[slot] = 0;
        return true;
    }

    GamepadState InputState::GetGamepadState(uint8_t slot) const
    {
        return slot < GamepadSlotCount ? m_GamepadStates[slot] : GamepadState{};
    }
    GamepadState InputState::GetPreviousGamepadState(uint8_t slot) const
    {
        return slot < GamepadSlotCount ? m_PrevGamepadStates[slot] : GamepadState{};
    }
    bool InputState::IsGamepadButtonDown(uint8_t slot, GamepadButton button) const
    {
        const auto code = static_cast<uint16_t>(button);
        return slot < GamepadSlotCount && IsValidGamepadButton(code) && m_GamepadStates[slot].Connected &&
            (m_GamepadStates[slot].Buttons & code) != 0;
    }
    bool InputState::IsGamepadButtonPressed(uint8_t slot, GamepadButton button) const
    {
        const auto code = static_cast<uint16_t>(button);
        return slot < GamepadSlotCount && IsValidGamepadButton(code) && (m_GamepadPressed[slot] & code) != 0;
    }
    bool InputState::IsGamepadButtonReleased(uint8_t slot, GamepadButton button) const
    {
        const auto code = static_cast<uint16_t>(button);
        return slot < GamepadSlotCount && IsValidGamepadButton(code) && (m_GamepadReleased[slot] & code) != 0;
    }
    uint64_t InputState::GetGamepadButtonReleaseSerial(uint8_t slot, GamepadButton button) const
    {
        const auto code = static_cast<uint16_t>(button);
        return slot < GamepadSlotCount && IsValidGamepadButton(code)
            ? m_GamepadReleaseSerial[slot][std::countr_zero(code)] : 0;
    }
    float InputState::GetGamepadAxis(uint8_t slot, GamepadAxis axis) const
    {
        const auto index = static_cast<uint8_t>(axis);
        return slot < GamepadSlotCount && index < static_cast<uint8_t>(GamepadAxis::Count) &&
            m_GamepadStates[slot].Connected ? m_GamepadStates[slot].Axes[index] : 0.0f;
    }
    float InputState::GetGamepadTrigger(uint8_t slot, GamepadTrigger trigger) const
    {
        const auto index = static_cast<uint8_t>(trigger);
        return slot < GamepadSlotCount && index < static_cast<uint8_t>(GamepadTrigger::Count) &&
            m_GamepadStates[slot].Connected ? m_GamepadStates[slot].Triggers[index] : 0.0f;
    }

    void InputState::SetKeyState(KeyCode code, bool bDown)
    {
        uint32_t index = static_cast<uint32_t>(code);
        if (index < KEY_COUNT)
        {
            if (m_KeyStates[index] && !bDown) ++m_KeyReleaseSerial[index];
            m_KeyStates[index] = bDown;
        }
    }

    void InputState::SetMouseButtonState(MouseButton button, bool bDown)
    {
        uint32_t index = static_cast<uint32_t>(button);
        if (index < MOUSE_BUTTON_COUNT)
        {
            if (m_MouseButtonStates[index] && !bDown) ++m_MouseReleaseSerial[index];
            m_MouseButtonStates[index] = bDown;
        }
    }

    void InputState::SetMousePosition(float x, float y)
    {
        if (m_bFirstMouseUpdate)
        {
            m_PrevMouseX = x;
            m_PrevMouseY = y;
            m_bFirstMouseUpdate = false;
        }

        // フレーム内のデルタを蓄積（BeginFrameでリセットされる）
        m_MouseState.DeltaX += (x - m_PrevMouseX);
        m_MouseState.DeltaY += (y - m_PrevMouseY);
        m_MouseState.PositionX = x;
        m_MouseState.PositionY = y;

        m_PrevMouseX = x;
        m_PrevMouseY = y;
    }

    void InputState::AddMouseScroll(float delta)
    {
        m_MouseState.ScrollDelta += delta;
    }

    void InputState::ResetFrameAccumulators()
    {
        m_MouseState.DeltaX = 0.0f;
        m_MouseState.DeltaY = 0.0f;
        m_MouseState.ScrollDelta = 0.0f;
    }

} // namespace NorvesLib::Core::Input
