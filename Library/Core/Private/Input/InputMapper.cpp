#include "Input/InputMapper.h"
#include "Input/InputRouter.h"
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Input
{
    namespace
    {
        Container::Span<const InputBinding> Bindings(const Container::VariableArray<InputBinding>& bindings)
        {
            return {bindings.data(), bindings.size()};
        }
    }
    InputMapper::~InputMapper() { Detach(); }
    bool InputMapper::Configure(const InputBindingSet& settings)
    {
        return ConfigureImpl(settings, {});
    }
    bool InputMapper::ConfigureWithContext(const InputBindingSet& settings, Identity initialContext)
    {
        return initialContext.IsValid() && ConfigureImpl(settings, {&initialContext,1});
    }
    bool InputMapper::ConfigurePreservingContexts(const InputBindingSet& settings)
    {
        Container::VariableArray<Identity> ids;
        ids.reserve(m_Stack.size());
        for(const auto index:m_Stack) ids.push_back(m_Contexts[index].Id);
        return ConfigureImpl(settings,{ids.data(),ids.size()});
    }
    bool InputMapper::ConfigureImpl(const InputBindingSet& settings, Container::Span<const Identity> initialContexts)
    {
        Container::VariableArray<Context> compiled;
        compiled.reserve(settings.GetContexts().size());
        for (const auto& definition : settings.GetContexts())
        {
            if (!definition.Id.IsValid() || !IsValidCursorMode(definition.CursorMode)) return false;
            for (const auto& existing : compiled) if (existing.Id == definition.Id) return false;
            Context context;
            context.Id = definition.Id;
            context.CursorMode = definition.CursorMode;
            context.Actions.reserve(definition.Actions.size());
            for (const auto& source : definition.Actions)
            {
                if (!InputBindingSet::IsValidAction(source)) return false;
                for (const auto& existing : context.Actions) if (existing.Id == source.Id) return false;
                Action action;
                action.Id = source.Id;
                action.Bindings = source.Bindings;
                if (!action.Runtime.Configure(source.Settings) || !action.Runtime.BeginFrame(m_Time)) return false;
                context.Actions.push_back(std::move(action));
            }
            compiled.push_back(std::move(context));
        }
        Container::VariableArray<size_t> stack;
        stack.reserve(initialContexts.size());
        for(const auto id:initialContexts)
        {
            if(!id.IsValid()) return false;
            size_t found=compiled.size();
            for(size_t i=0;i<compiled.size();++i) if(compiled[i].Id==id) { found=i;break; }
            if(found==compiled.size()) return false;
            for(const auto existing:stack) if(existing==found) return false;
            stack.push_back(found);
        }
        CancelAll();
        for (auto& context : compiled)
            for (auto& action : context.Actions)
                for (const auto id : m_FixedEventActions)
                    if (action.Id == id)
                        action.Runtime.SetFixedEventCapture(true);
        m_Contexts = std::move(compiled);
        m_Stack = std::move(stack);
        return true;
    }
    void InputMapper::Attach(InputRouter& router, int32_t priority)
    {
        if (m_Router == &router) return;
        // 新規登録のallocationが失敗しても旧Routerの接続を保つ。
        router.RegisterController(this, priority);
        if (m_Router) m_Router->UnregisterController(this);
        m_Router = &router;
        CancelAll();
    }
    void InputMapper::Detach()
    {
        if (m_Router) m_Router->UnregisterController(this);
        m_Router = nullptr;
        CancelAll();
    }
    InputMapper::Context* InputMapper::Top()
    {
        return m_Stack.empty() ? nullptr : &m_Contexts[m_Stack.back()];
    }
    const InputMapper::Context* InputMapper::Top() const
    {
        return m_Stack.empty() ? nullptr : &m_Contexts[m_Stack.back()];
    }
    bool InputMapper::PushContext(Identity context)
    {
        for (size_t i=0; i<m_Contexts.size(); ++i)
        {
            if (m_Contexts[i].Id != context) continue;
            for (const auto index : m_Stack) if (index == i) return false;
            m_Stack.push_back(i);
            CancelAll();
            return true;
        }
        return false;
    }
    bool InputMapper::PopContext()
    {
        if (m_Stack.empty()) return false;
        m_Stack.pop_back();
        CancelAll();
        return true;
    }
    void InputMapper::ClearContexts()
    {
        m_Stack.clear();
        CancelAll();
    }
    Identity InputMapper::GetActiveContext() const
    {
        const auto* context = Top();
        return context ? context->Id : Identity{};
    }
    ECursorMode InputMapper::GetRequestedCursorMode() const
    {
        const auto* context = Top();
        return m_Router && !IsInputSuppressed() && context ? context->CursorMode : ECursorMode::Normal;
    }
    ECursorMode InputMapper::GetCursorMode() const
    {
        return m_Focused ? GetRequestedCursorMode() : ECursorMode::Normal;
    }
    void InputMapper::SetFocused(bool focused)
    {
        if (m_Focused == focused) return;
        m_Focused = focused;
        CancelAll();
    }
    void InputMapper::SetCaptureSuppressed(bool suppressed)
    {
        if (m_bCaptureSuppressed == suppressed)
        {
            return;
        }
        m_bCaptureSuppressed = suppressed;
        CancelAll();
    }
    void InputMapper::SetDebugOverlaySuppressed(bool suppressed)
    {
        if (m_bDebugOverlaySuppressed == suppressed)
        {
            return;
        }
        m_bDebugOverlaySuppressed = suppressed;
        CancelAll();
    }
    void InputMapper::CancelAction(Action& action)
    {
        ++action.CancellationGeneration;
        action.Runtime.Cancel();
    }
    void InputMapper::CancelAll()
    {
        ++m_CancellationGeneration;
        m_Armed.Reset();
        for (auto& context : m_Contexts)
            for (auto& action : context.Actions) CancelAction(action);
    }
    bool InputMapper::BeginFrame(double time)
    {
        if (!std::isfinite(time) || time < m_Time) return false;
        for (auto& context : m_Contexts)
            for (auto& action : context.Actions) (void)action.Runtime.BeginFrame(time);
        m_Time = time;
        return true;
    }
    bool InputMapper::Update(double time, double unscaledDeltaSeconds)
    {
        if (!std::isfinite(time) || time < m_Time || !std::isfinite(unscaledDeltaSeconds) || unscaledDeltaSeconds < 0) return false;
        m_Armed.Reconcile(m_State);
        auto* top = m_Router && m_Focused && !IsInputSuppressed() ? Top() : nullptr;
        bool success = true;
        for (auto& context : m_Contexts)
        {
            const bool active = &context == top;
            for (auto& action : context.Actions)
            {
                if (!action.Runtime.Update(active ? Bindings(action.Bindings) : Container::Span<const InputBinding>{},
                    m_State, m_Armed, time, active ? unscaledDeltaSeconds : 0))
                {
                    // overflowした結果を前frameから持ち越さず、clockも全actionで揃える。
                    CancelAction(action);
                    (void)action.Runtime.BeginFrame(time);
                    success = false;
                }
            }
        }
        m_Time = time;
        return success;
    }
    InputMappedAction InputMapper::GetAction(Identity action) const
    {
        const auto* context = Top();
        return context ? GetAction(context->Id, action) : InputMappedAction{};
    }
    InputMappedAction InputMapper::GetAction(Identity contextId, Identity actionId) const
    {
        for (const auto& context : m_Contexts)
        {
            if (context.Id != contextId) continue;
            for (const auto& action : context.Actions)
            {
                if (action.Id != actionId) continue;
                InputMappedAction value;
                value.Valid = true;
                value.CancellationGeneration = action.CancellationGeneration;
                value.Active = m_Router && m_Focused && !IsInputSuppressed() && &context == Top();
                value.Type = action.Runtime.GetSettings().Type;
                value.Button = action.Runtime.GetButton();
                value.Axis = action.Runtime.GetAxis();
                return value;
            }
        }
        return {};
    }
    bool InputMapper::ConsumeFixedPress(Identity id)
    {
        auto* context = m_Router && m_Focused && !IsInputSuppressed() ? Top() : nullptr;
        if (context) for (auto& action : context->Actions)
            if (action.Id == id) return action.Runtime.ConsumeFixedPress();
        return false;
    }
    bool InputMapper::SetFixedButtonEventCapture(Identity id, bool enabled)
    {
        if (!id.IsValid())
            return false;
        bool found = false;
        for (const auto& context : m_Contexts)
            for (const auto& action : context.Actions)
                if (action.Id == id && action.Runtime.GetSettings().Type == EInputMappingValueType::Button)
                    found = true;
        size_t registered = m_FixedEventActions.size();
        for (size_t i = 0; i < m_FixedEventActions.size(); ++i)
            if (m_FixedEventActions[i] == id)
            {
                registered = i;
                break;
            }
        const bool wasRegistered = registered < m_FixedEventActions.size();
        if (enabled && !found)
            return false;
        if (enabled && registered == m_FixedEventActions.size())
            m_FixedEventActions.push_back(id);
        if (!enabled && registered < m_FixedEventActions.size())
            m_FixedEventActions.erase(m_FixedEventActions.begin() + registered);
        for (auto& context : m_Contexts)
            for (auto& action : context.Actions)
                if (action.Id == id)
                    action.Runtime.SetFixedEventCapture(enabled);
        return found || wasRegistered;
    }
    bool InputMapper::ConsumeFixedButtonEvent(Identity id, InputButtonEvent& out)
    {
        auto* context = m_Router && m_Focused && !IsInputSuppressed() ? Top() : nullptr;
        if (context)
            for (auto& action : context->Actions)
                if (action.Id == id)
                    return action.Runtime.ConsumeFixedEvent(out);
        return false;
    }
    void InputMapper::SyncActiveButtons()
    {
        auto* context = m_Router && m_Focused && !IsInputSuppressed() ? Top() : nullptr;
        if (!context) return;
        for (auto& action : context->Actions)
            if (!action.Runtime.SyncButtons(Bindings(action.Bindings), m_State, m_Armed)) CancelAction(action);
    }
    void InputMapper::AccumulateRelative(EInputBindingSource kind, float x, float y)
    {
        auto* context = m_Router && m_Focused && !IsInputSuppressed() ? Top() : nullptr;
        if (!context) return;
        for (auto& action : context->Actions)
        {
            const auto bindings = Bindings(action.Bindings);
            if (!action.Runtime.AccumulateRelative(kind, 0, x, bindings, m_State, m_Armed) ||
                !action.Runtime.AccumulateRelative(kind, 1, y, bindings, m_State, m_Armed)) CancelAction(action);
        }
    }
    bool InputMapper::OnKey(const KeyEvent& event)
    {
        if (m_Router && m_Focused && !IsInputSuppressed() && Top()) { m_Armed.OnKey(event, m_State); SyncActiveButtons(); }
        return false;
    }
    bool InputMapper::OnMouseButton(const MouseButtonEvent& event)
    {
        if (m_Router && m_Focused && !IsInputSuppressed() && Top()) { m_Armed.OnMouseButton(event, m_State); SyncActiveButtons(); }
        return false;
    }
    bool InputMapper::OnGamepadButton(const GamepadButtonEvent& event)
    {
        if (m_Router && m_Focused && !IsInputSuppressed() && Top()) { m_Armed.OnGamepadButton(event, m_State); SyncActiveButtons(); }
        return false;
    }
    bool InputMapper::OnGamepadSample(const GamepadSampleEvent& event)
    {
        if (event.Mode == EGamepadSampleMode::Live)
            SyncActiveButtons();
        return false;
    }
    bool InputMapper::OnMouseRawMove(const MouseRawMoveEvent& event)
    {
        AccumulateRelative(EInputBindingSource::MouseDelta, event.DeltaX, event.DeltaY);
        return false;
    }
    bool InputMapper::OnMouseScroll(const MouseScrollEvent& event)
    {
        AccumulateRelative(EInputBindingSource::MouseWheel, event.Delta, event.HorizontalDelta);
        return false;
    }
    void InputMapper::OnGamepadConnection(const GamepadConnectionEvent& event)
    {
        if (event.Connected || event.Slot >= GamepadSlotCount) return;
        m_Armed.ResetGamepad(event.Slot);
        for (auto& context : m_Contexts)
            for (auto& action : context.Actions)
            {
                bool affected = false;
                for (const auto& binding : action.Bindings)
                    if (IsGamepadSource(binding.Source.Kind) && binding.Source.Slot == event.Slot) affected = true;
                if (!affected) continue;
                // 他のbindingがHeldを維持していれば、切断だけで余分なReleased/Pressedを作らない。
                auto candidate = action.Runtime;
                if (candidate.GetSettings().Type == EInputMappingValueType::Button &&
                    candidate.SyncButtons(Bindings(action.Bindings), m_State, m_Armed) && candidate.GetButton().Held) continue;
                CancelAction(action);
            }
    }
}
