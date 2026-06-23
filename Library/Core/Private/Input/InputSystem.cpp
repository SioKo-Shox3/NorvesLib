#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Input
{

    void InputSystem::BeginFrame()
    {
        m_State.BeginFrame();
    }

    void InputSystem::EndFrame()
    {
        // 現時点では予約
        // 将来: イベントキューのクリア、入力バッファリング等
    }

    const InputState &InputSystem::GetState() const
    {
        return m_State;
    }

    MulticastDelegate<const KeyEvent &> &InputSystem::OnKeyEvent()
    {
        return m_OnKeyEvent;
    }

    MulticastDelegate<const MouseButtonEvent &> &InputSystem::OnMouseButtonEvent()
    {
        return m_OnMouseButtonEvent;
    }

    MulticastDelegate<const MouseMoveEvent &> &InputSystem::OnMouseMoveEvent()
    {
        return m_OnMouseMoveEvent;
    }

    MulticastDelegate<const MouseScrollEvent &> &InputSystem::OnMouseScrollEvent()
    {
        return m_OnMouseScrollEvent;
    }

    MulticastDelegate<const CharEvent &> &InputSystem::OnCharEvent()
    {
        return m_OnCharEvent;
    }

    void InputSystem::InjectKeyEvent(KeyCode code, InputAction action)
    {
        // 状態を更新
        bool bDown = (action == InputAction::Pressed || action == InputAction::Repeat);
        m_State.SetKeyState(code, bDown);

        // イベントを発火
        KeyEvent event;
        event.Code = code;
        event.Action = action;
        m_OnKeyEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchKey(event);
        }
    }

    void InputSystem::InjectMouseButton(MouseButton button, InputAction action, float x, float y)
    {
        // 状態を更新
        bool bDown = (action == InputAction::Pressed);
        m_State.SetMouseButtonState(button, bDown);

        // イベントを発火
        MouseButtonEvent event;
        event.Button = button;
        event.Action = action;
        event.PositionX = x;
        event.PositionY = y;
        m_OnMouseButtonEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchMouseButton(event);
        }
    }

    void InputSystem::InjectMouseMove(float x, float y)
    {
        // 前の位置を取得（デルタ計算用）
        const auto &prevState = m_State.GetMouseState();
        float prevX = prevState.PositionX;
        float prevY = prevState.PositionY;

        // 状態を更新
        m_State.SetMousePosition(x, y);

        // イベントを発火
        MouseMoveEvent event;
        event.PositionX = x;
        event.PositionY = y;
        event.DeltaX = x - prevX;
        event.DeltaY = y - prevY;
        m_OnMouseMoveEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchMouseMove(event);
        }
    }

    void InputSystem::InjectMouseScroll(float delta)
    {
        // 状態を更新
        m_State.AddMouseScroll(delta);

        float accumulatedScroll = m_State.GetMouseState().ScrollDelta;
        NORVES_LOG_DEBUG("Input", "InjectMouseScroll: delta={:.3f}, accumulated={:.3f}", delta, accumulatedScroll);

        // イベントを発火
        MouseScrollEvent event;
        event.Delta = delta;
        m_OnMouseScrollEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchMouseScroll(event);
        }
    }

    void InputSystem::InjectCharEvent(uint32_t codepoint)
    {
        // 文字入力は瞬間イベント（状態を持たない）。OnCharEvent を発火するのみ。
        CharEvent event;
        event.Codepoint = codepoint;
        m_OnCharEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchChar(event);
        }
    }

    void InputSystem::SetRouter(InputRouter *router)
    {
        m_Router = router;
    }

} // namespace NorvesLib::Core::Input
