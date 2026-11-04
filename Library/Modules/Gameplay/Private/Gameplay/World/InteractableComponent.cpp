#include "Gameplay/World/InteractableComponent.h"

#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/InteractionService.h"

namespace NorvesLib::Modules::Gameplay::World
{
    IMPLEMENT_CLASS(InteractableComponent, Core::Component::Component)

    InteractableComponent::InteractableComponent()
    {
        EnterRadius = 2.0f;
        ExitRadius = 2.5f;
        PromptOffset = Math::Vector3(0.0f, 1.0f, 0.0f);
        FacingConeDegrees = 360.0f;
        Priority = 0;
        bDisabledWhileEngaged = false;
        bRequiresFreeMouth = false;
    }

    void InteractableComponent::Initialize()
    {
        Super::Initialize();
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetInteractionService().RequestRegisterInteractable(GetOwner()->GetEntityHandle());
        }
    }

    void InteractableComponent::BeginPlay()
    {
        Super::BeginPlay();
        // World への接続前に初期化された場合も、接続後に登録を依頼する。
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetInteractionService().RequestRegisterInteractable(GetOwner()->GetEntityHandle());
        }
    }

    void InteractableComponent::Finalize()
    {
        auto* module = FindGameplayModule(Core::Module::GetModuleRegistry());
        auto handle = GetOwner() ? GetOwner()->GetEntityHandle() : Core::EntityHandle{};
        if (!handle.IsValid())
        {
            handle = m_LastEvaluatedOwner;
        }
        if (module)
        {
            module->GetInteractionService().RequestUnregisterInteractable(handle);
        }
        Super::Finalize();
        if (module)
        {
            module->GetInteractionService().NotifyInteractableUnavailable(handle);
        }
    }

    void InteractableComponent::Enable()
    {
        Super::Enable();
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetInteractionService().RequestRegisterInteractable(GetOwner()->GetEntityHandle());
        }
    }

    void InteractableComponent::Disable()
    {
        Super::Disable();
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()))
        {
            auto handle = GetOwner() ? GetOwner()->GetEntityHandle() : Core::EntityHandle{};
            if (!handle.IsValid())
            {
                handle = m_LastEvaluatedOwner;
            }
            module->GetInteractionService().NotifyInteractableUnavailable(handle);
        }
    }
} // namespace NorvesLib::Modules::Gameplay::World
