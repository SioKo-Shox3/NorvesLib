#include "Gameplay/World/ZoneSensorComponent.h"

#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/EncounterService.h"

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
        }
    }

    void ZoneSensorComponent::BeginPlay()
    {
        Super::BeginPlay();
        // 復元時の Initialize が World への接続より先でも、接続後に登録を依頼する。
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestRegisterSensor(GetOwner()->GetEntityHandle());
        }
    }

    void ZoneSensorComponent::Finalize()
    {
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestUnregisterSensor(GetOwner()->GetEntityHandle());
        }
        m_bTeleported = false;
        Super::Finalize();
    }
} // namespace NorvesLib::Modules::Gameplay::World
