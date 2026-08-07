#include "Input/CameraInputCollector.h"

namespace Game::Input
{
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
        ResetFrame();
    }
} // namespace Game::Input
