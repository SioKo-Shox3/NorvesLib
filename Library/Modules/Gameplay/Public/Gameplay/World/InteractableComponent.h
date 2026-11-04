#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::World
{
    /// 近接操作の候補と、その表示・選択条件を持つ。
    class InteractableComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(InteractableComponent, Core::Component::Component)

    public:
        InteractableComponent();
        void Initialize() override;
        void BeginPlay() override;
        void Finalize() override;
        void Enable() override;
        void Disable() override;

        PROPERTY(Core::Container::String, ActionId)
        PROPERTY(Core::Container::String, PromptTextKey)
        PROPERTY(float, EnterRadius)
        PROPERTY(float, ExitRadius)
        PROPERTY(Math::Vector3, PromptOffset)
        PROPERTY(float, FacingConeDegrees)
        PROPERTY(int32_t, Priority)
        PROPERTY(bool, bDisabledWhileEngaged)
        PROPERTY(bool, bRequiresFreeMouth)

    private:
        friend class InteractionService;
        // 終了前に所有関係が外れても、選択の解除に元のハンドルを使う。
        Core::EntityHandle m_LastEvaluatedOwner;
    };
} // namespace NorvesLib::Modules::Gameplay::World
