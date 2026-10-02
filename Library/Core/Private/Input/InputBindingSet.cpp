#include "Input/InputBindingSet.h"
#include <utility>

namespace NorvesLib::Core::Input
{
    bool InputBindingSet::IsValidAction(const InputActionDefinition& action)
    {
        return action.Id.IsValid() && IsValidInputActionBindings(action.Settings,
            std::span<const InputBinding>(action.Bindings.data(), action.Bindings.size()));
    }
    bool InputBindingSet::AddContext(Identity id, ECursorMode cursorMode)
    {
        if (!id.IsValid() || !IsValidCursorMode(cursorMode) || FindContext(id)) return false;
        InputContextDefinition context;
        context.Id = id;
        context.CursorMode = cursorMode;
        m_Contexts.push_back(std::move(context));
        return true;
    }
    bool InputBindingSet::SetContextCursorMode(Identity id, ECursorMode cursorMode)
    {
        auto* context = FindMutableContext(id);
        if (!context || !IsValidCursorMode(cursorMode)) return false;
        context->CursorMode = cursorMode;
        return true;
    }
    bool InputBindingSet::AddAction(Identity contextId, const InputActionDefinition& action)
    {
        auto* context = FindMutableContext(contextId);
        if (!context || !IsValidAction(action) || FindAction(contextId, action.Id)) return false;
        context->Actions.push_back(action);
        return true;
    }
    bool InputBindingSet::ReplaceAction(Identity contextId, const InputActionDefinition& action)
    {
        auto* existing = FindMutableAction(contextId, action.Id);
        if (!existing || !IsValidAction(action)) return false;
        InputActionDefinition replacement = action;
        *existing = std::move(replacement);
        return true;
    }
    bool InputBindingSet::SetBindings(Identity contextId, Identity actionId,
        const Container::VariableArray<InputBinding>& bindings)
    {
        auto* action = FindMutableAction(contextId, actionId);
        if (!action || !IsValidInputActionBindings(action->Settings,
            std::span<const InputBinding>(bindings.data(), bindings.size()))) return false;
        Container::VariableArray<InputBinding> replacement = bindings;
        action->Bindings = std::move(replacement);
        return true;
    }
    const InputContextDefinition* InputBindingSet::FindContext(Identity id) const
    {
        for (const auto& context : m_Contexts) if (context.Id == id) return &context;
        return nullptr;
    }
    const InputActionDefinition* InputBindingSet::FindAction(Identity contextId, Identity id) const
    {
        const auto* context = FindContext(contextId);
        if (context) for (const auto& action : context->Actions) if (action.Id == id) return &action;
        return nullptr;
    }
    InputContextDefinition* InputBindingSet::FindMutableContext(Identity id)
    {
        for (auto& context : m_Contexts) if (context.Id == id) return &context;
        return nullptr;
    }
    InputActionDefinition* InputBindingSet::FindMutableAction(Identity contextId, Identity id)
    {
        auto* context = FindMutableContext(contextId);
        if (context) for (auto& action : context->Actions) if (action.Id == id) return &action;
        return nullptr;
    }
} // namespace NorvesLib::Core::Input
