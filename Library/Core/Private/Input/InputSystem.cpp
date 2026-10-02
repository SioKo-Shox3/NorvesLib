#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <chrono>

namespace NorvesLib::Core::Input
{

    void InputSystem::BeginFrame()
    {
        const double time = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        (void)BeginFrame(time);
    }

    bool InputSystem::BeginFrame(double unscaledTimeSeconds)
    {
        if (!m_ActiveDevice.BeginFrame(unscaledTimeSeconds))
        {
            return false;
        }
        if (m_bDeferredInputReset)
        {
            ReleaseAll();
        }
        m_State.BeginFrame();
        return true;
    }

    void InputSystem::EndFrame()
    {
        const auto kind = m_ActiveDevice.GetKind();
        if (m_bNotifyingDeviceKind || kind == m_NotifiedDeviceKind)
        {
            return;
        }
        // callbackより先に確定し、callback中の新活動は次EndFrameへ残す。
        m_NotifiedDeviceKind = kind;
        m_bNotifyingDeviceKind = true;
        struct NotifyGuard
        {
            bool& Flag;
            ~NotifyGuard() { Flag = false; }
        } guard{m_bNotifyingDeviceKind};
        m_OnActiveDeviceKindChanged.Broadcast(kind);
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
        const bool bWasDown = m_State.IsKeyDown(code);
        bool bDown = (action == InputAction::Pressed || action == InputAction::Repeat);
        m_State.SetKeyState(code, bDown);
        (void)m_ActiveDevice.ObserveKey(code, action, bWasDown);

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
        const bool bWasDown = m_State.IsMouseButtonDown(button);
        bool bDown = (action == InputAction::Pressed);
        m_State.SetMouseButtonState(button, bDown);
        (void)m_ActiveDevice.ObserveMouseButton(button, action, bWasDown);

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

    void InputSystem::InjectMouseMove(float x, float y, bool accumulateDelta)
    {
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        // 正本の初回再基準化と同じdeltaを通知する。
        const auto previous = m_State.GetMouseState();

        // 状態を更新
        m_State.SetMousePosition(x, y, accumulateDelta);

        // イベントを発火
        MouseMoveEvent event;
        event.PositionX = x;
        event.PositionY = y;
        event.DeltaX = accumulateDelta ? m_State.GetMouseState().DeltaX - previous.DeltaX : 0;
        event.DeltaY = accumulateDelta ? m_State.GetMouseState().DeltaY - previous.DeltaY : 0;
        (void)m_ActiveDevice.ObserveMouseMove(event.DeltaX, event.DeltaY, false);
        m_OnMouseMoveEvent.Broadcast(event);

        // 優先度付きルーターへ配送（登録 Controller がいなければ空振り）
        if (m_Router)
        {
            m_Router->DispatchMouseMove(event);
        }
    }

    void InputSystem::InjectMouseScroll(float delta)
    {
        (void)InjectMouseScrollAxes(delta, 0.0f);
    }
    bool InputSystem::InjectMouseScrollAxes(float vertical, float horizontal)
    {
        if (!m_State.AddMouseScrollAxes(vertical, horizontal)) return false;
        (void)m_ActiveDevice.ObserveMouseScroll(vertical, horizontal);
        MouseScrollEvent event{vertical, horizontal};
        m_OnMouseScrollEvent.Broadcast(event);
        if (m_Router) m_Router->DispatchMouseScroll(event);
        return true;
    }
    bool InputSystem::InjectRawMouseDelta(float x, float y)
    {
        if (!m_State.AddRawMouseDelta(x, y)) return false;
        (void)m_ActiveDevice.ObserveMouseMove(x, y, true);
        MouseRawMoveEvent event{x, y};
        m_OnMouseRawMoveEvent.Broadcast(event);
        if (m_Router) m_Router->DispatchMouseRawMove(event);
        return true;
    }
    bool InputSystem::InjectGamepadState(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode)
    {
        const GamepadState accepted=state; // callbackが呼出元のstateを変更してもsnapshotを保つ。
        const auto previous = m_State.GetGamepadState(slot);
        const auto previousPhysical = m_State.GetLastGamepadSample(slot);
        if (!m_State.SetGamepadState(slot, accepted, mode))
        {
            return false;
        }
        (void)m_ActiveDevice.ObserveGamepad(slot, previousPhysical, accepted, mode);
        if (previous.Connected != accepted.Connected)
        {
            // 切断取消しは通常release（tap完了）より先に必ず届く。
            GamepadConnectionEvent event{slot, accepted.Connected};
            m_OnGamepadConnectionEvent.Broadcast(event);
            if (m_Router) m_Router->NotifyGamepadConnection(event);
        }
        if (mode == EGamepadSampleMode::Live)
        {
            const uint16_t pressed = static_cast<uint16_t>(accepted.Buttons & ~previous.Buttons);
            const uint16_t released = static_cast<uint16_t>(previous.Buttons & ~accepted.Buttons);
            // A→B持替えで同じactionを離した扱いにしないため、新押下を先に配送。
            for (const auto action : {InputAction::Pressed, InputAction::Released})
            {
                const uint16_t mask = action == InputAction::Pressed ? pressed : released;
                for (uint32_t bit=0; bit<16; ++bit)
                {
                    if ((mask & (1u << bit)) == 0) continue;
                    GamepadButtonEvent event{slot, static_cast<GamepadButton>(1u << bit), action};
                    m_OnGamepadButtonEvent.Broadcast(event);
                    if (m_Router) m_Router->DispatchGamepadButton(event);
                }
            }
        }
        GamepadSampleEvent sample{slot,accepted,m_State.GetGamepadSampleSerial(slot),mode};
        m_OnGamepadSampleEvent.Broadcast(sample);
        if (m_Router)
        {
            m_Router->DispatchGamepadSample(sample);
        }
        return true;
    }
    void InputSystem::ReleaseAll()
    {
        m_State.ReleaseAll();
        m_OnInputResetEvent.Broadcast();
        if (m_Router) m_Router->NotifyInputReset();
        m_bDeferredInputReset = false;
    }

    void InputSystem::InjectCharEvent(uint32_t codepoint)
    {
        // 文字自体は瞬間イベント。表示用の活動判定は有効Unicodeだけ採用する。
        (void)m_ActiveDevice.ObserveCharacter(codepoint);
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
