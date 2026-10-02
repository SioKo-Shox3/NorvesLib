#pragma once

#include "Input/IInputController.h"
#include "Input/InputDebugOverlayState.h"

namespace NorvesLib::Core::Input
{
    class InputSystem;
    class InputRouter;
    class InputMapper;

    // Engine所有。System/Router/Mapperの寿命内で使い、配線変更前にDetachする。
    // ImGuiは予約F1を透過する。Overlayより下、Gameより上でmaskするのでUIには入力が届く。
    // Attach/Detach/SetEnabled/Advanceは配送外。AdvanceはMapper.Updateより前に呼ぶ。
    class InputDebugOverlayController final : public IInputController
    {
    public:
        InputDebugOverlayController(InputSystem& system, InputRouter& router, InputMapper& mapper);
        ~InputDebugOverlayController() override;
        InputDebugOverlayController(const InputDebugOverlayController&) = delete;
        InputDebugOverlayController& operator=(const InputDebugOverlayController&) = delete;
        InputDebugOverlayController(InputDebugOverlayController&&) = delete;
        InputDebugOverlayController& operator=(InputDebugOverlayController&&) = delete;
        bool Attach();
        void Detach();
        void SetEnabled(bool enabled)
        {
            m_State.SetEnabled(enabled);
        }
        bool IsEnabled() const
        {
            return m_State.IsEnabled();
        }
        bool IsOverlayActive() const
        {
            return m_State.IsActive();
        }
        void Advance();

        bool OnKey(const KeyEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent&) override
        {
            return IsMasking();
        }
        bool OnMouseMove(const MouseMoveEvent&) override
        {
            return IsMasking();
        }
        bool OnMouseScroll(const MouseScrollEvent&) override
        {
            return IsMasking();
        }
        bool OnMouseRawMove(const MouseRawMoveEvent&) override
        {
            return IsMasking();
        }
        bool OnChar(const CharEvent&) override
        {
            return IsMasking();
        }
        bool OnGamepadButton(const GamepadButtonEvent&) override
        {
            return IsMasking();
        }
        bool OnGamepadSample(const GamepadSampleEvent&) override
        {
            return IsMasking();
        }
        void OnInputReset() override
        {
            if (!m_bInternalReset)
            {
                m_State.ResetKeys();
            }
        }
        void OnInputFocusChanged(bool focused) override;
        const char* DebugName() const override
        {
            return "InputDebugOverlayController";
        }
    private:
        bool HasMatchingWiring() const;
        void ResetOperations();
        bool IsMasking() const
        {
            return m_bAttached && m_State.IsMasking();
        }
        InputSystem& m_System;
        InputRouter& m_Router;
        InputMapper& m_Mapper;
        InputDebugOverlayState m_State;
        bool m_bAttached = false;
        bool m_bInternalReset = false;
    };
} // namespace NorvesLib::Core::Input
