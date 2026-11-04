#include "Gameplay/World/ZoneSensorComponent.h"

#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/InteractionService.h"

namespace NorvesLib::Modules::Gameplay::World
{
    IMPLEMENT_CLASS(ZoneSensorComponent, Core::Component::Component)

    ZoneSensorComponent::ZoneSensorComponent()
    {
        Offset = Math::Vector3::Zero;
        KindBits = 1u;
    }

    void ZoneSensorComponent::Initialize()
    {
        Super::Initialize();
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestRegisterSensor(GetOwner()->GetEntityHandle());
            module->GetInteractionService().RequestRegisterSensorComponent(GetOwner()->GetEntityHandle(), GetComponentId());
        }
    }

    void ZoneSensorComponent::BeginPlay()
    {
        Super::BeginPlay();
        // 復元時の Initialize が World への接続より先でも、接続後に登録を依頼する。
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestRegisterSensor(GetOwner()->GetEntityHandle());
            module->GetInteractionService().RequestRegisterSensorComponent(GetOwner()->GetEntityHandle(), GetComponentId());
        }
    }

    void ZoneSensorComponent::Finalize()
    {
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()))
        {
            const auto handle = GetOwner() ? GetOwner()->GetEntityHandle() : Core::EntityHandle{};
            if (GetOwner())
            {
                module->GetEncounterService().RequestUnregisterSensor(handle);
            }
            // 所有関係が先に外れた場合も、登録済みの Component 識別子で解除を依頼する。
            module->GetInteractionService().RequestUnregisterSensorComponent(handle, GetComponentId());
        }
        m_bTeleported = false;
        Super::Finalize();
    }
} // namespace NorvesLib::Modules::Gameplay::World
