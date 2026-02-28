#include "Input/InputState.h"
#include <cstring>

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
        return !m_KeyStates[index] && m_PrevKeyStates[index];
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
        return !m_MouseButtonStates[index] && m_PrevMouseButtonStates[index];
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

        // フレーム間累積値をリセット
        ResetFrameAccumulators();
    }

    void InputState::SetKeyState(KeyCode code, bool bDown)
    {
        uint32_t index = static_cast<uint32_t>(code);
        if (index < KEY_COUNT)
        {
            m_KeyStates[index] = bDown;
        }
    }

    void InputState::SetMouseButtonState(MouseButton button, bool bDown)
    {
        uint32_t index = static_cast<uint32_t>(button);
        if (index < MOUSE_BUTTON_COUNT)
        {
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
