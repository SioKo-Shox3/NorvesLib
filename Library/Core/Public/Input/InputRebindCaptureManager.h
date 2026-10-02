#pragma once

#include "Input/IInputController.h"
#include "Input/InputRebindCaptureState.h"

namespace NorvesLib::Core::Input
{
    class InputSystem;
    class InputRouter;
    class InputMapper;

    struct InputRebindCaptureResult
    {
        uint64_t RequestId = 0;
        EInputRebindOutcome Outcome = EInputRebindOutcome::None;
        InputCapturedControl Control;
    };

    // GameThread専用。System/Router/Mapperより短命で、同じ正本と配線を使う。
    // Attach/Detach/Begin/Advanceは入力配送外から呼ぶ。配線変更の前にはDetachする。
    // 捕捉は予約最高優先度で全通常eventを消費する。Delegateの観測通知は遮断しない。
    // BeginFrame→イベント→Advance→Mapper.Update。結果公開は解除処理の後だけ。
    class InputRebindCaptureManager final : public IInputController
    {
    public:
        InputRebindCaptureManager(InputSystem& system, InputRouter& router, InputMapper& mapper);
        ~InputRebindCaptureManager() override;
        InputRebindCaptureManager(const InputRebindCaptureManager&) = delete;
        InputRebindCaptureManager& operator=(const InputRebindCaptureManager&) = delete;
        InputRebindCaptureManager(InputRebindCaptureManager&&) = delete;
        InputRebindCaptureManager& operator=(InputRebindCaptureManager&&) = delete;

        bool Attach();
        // 所有終了の解除。新たな入力通知を発火せず、進行中要求を中止する。
        void Detach();
        // 0は拒否。既存の要求/結果を変えない。成功IDは同一instance内で再利用しない。
        uint64_t Begin(const InputRebindCaptureOptions& options = {});
        bool Cancel(uint64_t requestId);
        // reset/focus/Run終了用。通常eventの遮断解除と結果公開はAdvanceまで保留する。
        void Abort();
        void BeginFrame();
        void Advance();
        bool IsCapturing() const
        {
            return m_bBlocking;
        }
        bool TryGetResult(uint64_t requestId, InputRebindCaptureResult& result) const;

        bool OnKey(const KeyEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent& event) override;
        bool OnMouseRawMove(const MouseRawMoveEvent& event) override;
        bool OnMouseScroll(const MouseScrollEvent& event) override;
        bool OnGamepadButton(const GamepadButtonEvent& event) override;
        bool OnGamepadSample(const GamepadSampleEvent& event) override;
        bool OnMouseMove(const MouseMoveEvent&) override
        {
            return m_bBlocking;
        }
        bool OnChar(const CharEvent&) override
        {
            return m_bBlocking;
        }
        void OnGamepadConnection(const GamepadConnectionEvent& event) override;
        void OnInputReset() override;
        void OnInputFocusChanged(bool focused) override;
        const char* DebugName() const override
        {
            return "InputRebindCaptureManager";
        }

    private:
        bool HasMatchingWiring() const;
        void ResetOperations();
        InputSystem& m_System;
        InputRouter& m_Router;
        InputMapper& m_Mapper;
        InputRebindCaptureState m_Capture;
        uint64_t m_RequestId = 0;
        bool m_bAttached = false;
        bool m_bBlocking = false;
        bool m_bInternalReset = false;
    };
} // namespace NorvesLib::Core::Input
