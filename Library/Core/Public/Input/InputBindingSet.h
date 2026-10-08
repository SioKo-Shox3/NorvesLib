#pragma once

#include "Application/CursorMode.h"
#include "Input/InputActionSettings.h"
#include "Container/VariableArray.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core::Input
{
    struct InputActionDefinition
    {
        Identity Id;
        InputActionSettings Settings;
        Container::VariableArray<InputBinding> Bindings;
    };
    struct InputContextDefinition
    {
        Identity Id;
        ECursorMode CursorMode = ECursorMode::Normal;
        Container::VariableArray<InputActionDefinition> Actions;
    };

    // 設定の所有者。Mapperはこの配列への長期pointerではなくcompileしたcopyを所有する。
    // Find/Getの借用viewは次の変更/破棄で失効するものとして扱う。
    // boolはvalidation拒否を表す。allocation失敗は例外で伝播し、copy assignmentの強い例外保証は約束しない。
    class InputBindingSet
    {
    public:
        bool AddContext(Identity id, ECursorMode cursorMode);
        bool SetContextCursorMode(Identity id, ECursorMode cursorMode);
        bool AddAction(Identity context, const InputActionDefinition& action);
        bool ReplaceAction(Identity context, const InputActionDefinition& action);
        bool SetBindings(Identity context, Identity action, const Container::VariableArray<InputBinding>& bindings);
        const InputContextDefinition* FindContext(Identity id) const;
        const InputActionDefinition* FindAction(Identity context, Identity id) const;
        const Container::VariableArray<InputContextDefinition>& GetContexts() const { return m_Contexts; }
        void Clear() { m_Contexts.clear(); }
        static bool IsValidAction(const InputActionDefinition& action);
    private:
        InputContextDefinition* FindMutableContext(Identity id);
        InputActionDefinition* FindMutableAction(Identity context, Identity id);
        Container::VariableArray<InputContextDefinition> m_Contexts;
    };
} // namespace NorvesLib::Core::Input
