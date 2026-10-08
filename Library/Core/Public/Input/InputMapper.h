#pragma once
#include "Container/Span.h"
#include "Input/InputBindingSet.h"
#include "Input/InputActionRuntime.h"
#include "Input/IInputController.h"

namespace NorvesLib::Core::Input
{
    class InputRouter;
    class InputRebindCaptureManager;
    class InputDebugOverlayController;
    struct InputMappedAction
    {
        bool Valid = false;
        bool Active = false;
        EInputMappingValueType Type = EInputMappingValueType::Button;
        InputButtonSnapshot Button;
        Math::Vector2 Axis;
    };

    // GameThread専用。正本/Routerより短い寿命で使う。配送中のAttach/Detach/Configure/stack変更は禁止。
    class InputMapper final : public IInputController
    {
    public:
        explicit InputMapper(const InputState& state) : m_State(state) {}
        ~InputMapper() override;
        InputMapper(const InputMapper&) = delete;
        InputMapper& operator=(const InputMapper&) = delete;
        InputMapper(InputMapper&&) = delete;
        InputMapper& operator=(InputMapper&&) = delete;

        // 設定をcopyしてcompile。成功時はstackを空にし旧入力を取り消す。
        bool Configure(const InputBindingSet& settings);
        // 設定と初期contextを候補で構築し、一括反映する。不明contextでは旧状態を維持。
        bool ConfigureWithContext(const InputBindingSet& settings, Identity initialContext);
        // 現context stackをIDで維持する。消えるcontextがあれば旧設定/操作を変更しない。
        bool ConfigurePreservingContexts(const InputBindingSet& settings);
        // 同じRouterへの再Attachは冪等。優先度変更は配送外でDetach→Attachする。
        void Attach(InputRouter& router, int32_t priority = 0);
        void Detach();
        bool PushContext(Identity context);
        bool PopContext();
        void ClearContexts();
        Identity GetActiveContext() const;
        ECursorMode GetCursorMode() const;
        // focus停止中もcontextの要求を保持する。Window側が有効modeを決定する。
        ECursorMode GetRequestedCursorMode() const;
        void SetFocused(bool focused);
        bool IsFocused() const { return m_Focused; }
        bool BeginFrame(double time);
        // falseはそのactionを安全にCancelしたことを示す。正常な他actionは評価を続ける。
        bool Update(double time, double unscaledDeltaSeconds);
        InputMappedAction GetAction(Identity action) const;
        InputMappedAction GetAction(Identity context, Identity action) const;
        bool ConsumeFixedPress(Identity action);
        void CancelAll();

        bool OnKey(const KeyEvent& event) override;
        bool OnMouseButton(const MouseButtonEvent& event) override;
        bool OnMouseRawMove(const MouseRawMoveEvent& event) override;
        bool OnMouseScroll(const MouseScrollEvent& event) override;
        bool OnGamepadButton(const GamepadButtonEvent& event) override;
        void OnGamepadConnection(const GamepadConnectionEvent& event) override;
        void OnInputReset() override { CancelAll(); }
        const char* DebugName() const override { return "InputMapper"; }
    private:
        friend class InputRebindCaptureManager;
        friend class InputDebugOverlayController;
        void SetCaptureSuppressed(bool suppressed);
        void SetDebugOverlaySuppressed(bool suppressed);
        bool IsInputSuppressed() const
        {
            return m_bCaptureSuppressed || m_bDebugOverlaySuppressed;
        }
        struct Action
        {
            Identity Id;
            Container::VariableArray<InputBinding> Bindings;
            InputActionRuntime Runtime;
        };
        struct Context
        {
            Identity Id;
            ECursorMode CursorMode = ECursorMode::Normal;
            Container::VariableArray<Action> Actions;
        };
        bool ConfigureImpl(const InputBindingSet& settings, Container::Span<const Identity> initialContexts);
        Context* Top();
        const Context* Top() const;
        void SyncActiveButtons();
        void AccumulateRelative(EInputBindingSource kind, float x, float y);
        const InputState& m_State;
        InputRouter* m_Router = nullptr;
        Container::VariableArray<Context> m_Contexts;
        Container::VariableArray<size_t> m_Stack;
        InputArmedState m_Armed;
        double m_Time = 0;
        bool m_Focused = true;
        InputRebindCaptureManager* m_CaptureOwner = nullptr;
        bool m_bCaptureSuppressed = false;
        InputDebugOverlayController* m_DebugOverlayOwner = nullptr;
        bool m_bDebugOverlaySuppressed = false;
    };
}
