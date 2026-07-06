#include "Input/CameraInputCollector.h"

namespace Game::Input
{

    bool CameraInputCollector::OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent &event)
    {
        const bool bPressed = (event.Action == NorvesLib::Core::Input::InputAction::Pressed);

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
            // X1/X2 等はカメラ操作の対象外。下位へ伝播させる。
            return false;
        }
    }

    bool CameraInputCollector::OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent &event)
    {
        if (m_bLeftDown || m_bMiddleDown || m_bRightDown)
        {
            m_AccumDeltaX += event.DeltaX;
            m_AccumDeltaY += event.DeltaY;
            return true;
        }
        return false;
    }

    bool CameraInputCollector::OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent &event)
    {
        m_AccumScrollDelta += event.Delta;
        return true;
    }

    NorvesLib::Core::Input::InputState CameraInputCollector::BuildFrameInputState() const
    {
        NorvesLib::Core::Input::InputState input;

        if (m_bLeftDown)
        {
            input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Left, true);
        }
        if (m_bMiddleDown)
        {
            input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Middle, true);
        }
        if (m_bRightDown)
        {
            input.SetMouseButtonState(NorvesLib::Core::Input::MouseButton::Right, true);
        }

        // delta は SetMousePosition(0,0) → SetMousePosition(dx,dy) の2段階で生成する
        // （初回呼び出しは m_bFirstMouseUpdate により prev を確定するだけで delta=0、
        //  2回目の呼び出しで delta = 引数 - prev = 蓄積値 となる）。
        input.SetMousePosition(0.0f, 0.0f);
        input.SetMousePosition(m_AccumDeltaX, m_AccumDeltaY);

        if (m_AccumScrollDelta != 0.0f)
        {
            input.AddMouseScroll(m_AccumScrollDelta);
        }

        return input;
    }

    void CameraInputCollector::ResetFrame()
    {
        m_AccumDeltaX = 0.0f;
        m_AccumDeltaY = 0.0f;
        m_AccumScrollDelta = 0.0f;
    }

} // namespace Game::Input
