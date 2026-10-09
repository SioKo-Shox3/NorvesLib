#include "Input/CameraInputCollector.h"

namespace Game::Input
{
    const NorvesLib::Core::Input::KeyCode CameraInputCollector::kTrackedKeys[kTrackedKeyCount] = {
        NorvesLib::Core::Input::KeyCode::W,         NorvesLib::Core::Input::KeyCode::A,
        NorvesLib::Core::Input::KeyCode::S,         NorvesLib::Core::Input::KeyCode::D,
        NorvesLib::Core::Input::KeyCode::Q,         NorvesLib::Core::Input::KeyCode::E,
        NorvesLib::Core::Input::KeyCode::LeftShift, NorvesLib::Core::Input::KeyCode::RightShift,
    };

    bool CameraInputCollector::OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent& event)
    {
        const bool bPressed = event.Action == NorvesLib::Core::Input::InputAction::Pressed;
        switch (event.Button)
        {
        case NorvesLib::Core::Input::MouseButton::Left:
            m_bLeftDown = bPressed;
            return true;
        case NorvesLib::Core::Input::MouseButton::Middle:
            m_bMiddleDown = bPressed;
            return true;
        case NorvesLib::Core::Input::MouseButton::Right:
            m_bRightDown = bPressed;
            return true;
        default:
            return false;
        }
    }

    bool CameraInputCollector::OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent& event)
    {
        if (!m_bLeftDown && !m_bMiddleDown && !m_bRightDown)
        {
            return false;
        }

        m_AccumDeltaX += event.DeltaX;
        m_AccumDeltaY += event.DeltaY;
        return true;
    }

    bool CameraInputCollector::OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent& event)
    {
        m_AccumScrollDelta += event.Delta;
        return true;
    }

    bool CameraInputCollector::OnKey(const NorvesLib::Core::Input::KeyEvent& event)
    {
        const bool bDown = event.Action == NorvesLib::Core::Input::InputAction::Pressed ||
                           event.Action == NorvesLib::Core::Input::InputAction::Repeat;
        if (event.Code == NorvesLib::Core::Input::KeyCode::LeftAlt)
        {
            m_bLeftAltDown = bDown;
        }
        else if (event.Code == NorvesLib::Core::Input::KeyCode::RightAlt)
        {
            m_bRightAltDown = bDown;
        }
        for (int index = 0; index < kTrackedKeyCount; ++index)
        {
            if (event.Code == kTrackedKeys[index])
            {
                m_bTrackedKeyDown[index] = bDown;
            }
        }
        return false;
    }

    bool CameraInputCollector::OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent& event)
    {
        if (event.Slot >= NorvesLib::Core::Input::GamepadSlotCount)
        {
            return false;
        }
        // 実際の操作ではないサンプル（フォーカスを外した間など）は、何も倒していない状態として扱う
        if (event.Mode == NorvesLib::Core::Input::EGamepadSampleMode::Live)
        {
            m_GamepadStates[event.Slot] = event.State;
        }
        else
        {
            m_GamepadStates[event.Slot] = {};
        }
        return false;
    }

    NorvesLib::Core::Input::InputState CameraInputCollector::BuildFrameInputState() const
    {
        NorvesLib::Core::Input::InputState input;
        input.SetKeyState(NorvesLib::Core::Input::KeyCode::LeftAlt, m_bLeftAltDown);
        input.SetKeyState(NorvesLib::Core::Input::KeyCode::RightAlt, m_bRightAltDown);
        input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Left, m_bLeftDown);
        input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Middle, m_bMiddleDown);
        input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Right, m_bRightDown);
        for (int index = 0; index < kTrackedKeyCount; ++index)
        {
            input.SetKeyState(kTrackedKeys[index], m_bTrackedKeyDown[index]);
        }
        for (uint8_t slot = 0; slot < NorvesLib::Core::Input::GamepadSlotCount; ++slot)
        {
            if (m_GamepadStates[slot].Connected)
            {
                input.SetGamepadState(slot, m_GamepadStates[slot]);
            }
        }

        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(m_AccumDeltaX, m_AccumDeltaY);
        input.AddMouseScroll(m_AccumScrollDelta);
        return input;
    }

    void CameraInputCollector::ResetFrame()
    {
        m_AccumDeltaX = 0.0f;
        m_AccumDeltaY = 0.0f;
        m_AccumScrollDelta = 0.0f;
    }

    void CameraInputCollector::ResetAll()
    {
        m_bLeftDown = false;
        m_bMiddleDown = false;
        m_bRightDown = false;
        m_bLeftAltDown = false;
        m_bRightAltDown = false;
        for (bool& bDown : m_bTrackedKeyDown)
        {
            bDown = false;
        }
        for (auto& state : m_GamepadStates)
        {
            state = {};
        }
        ResetFrame();
    }
} // namespace Game::Input
