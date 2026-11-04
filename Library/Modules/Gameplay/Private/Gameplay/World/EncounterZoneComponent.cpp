#include "Gameplay/World/EncounterZoneComponent.h"

#include "Gameplay/IGameplayModule.h"
#include "Gameplay/World/EncounterService.h"

namespace NorvesLib::Modules::Gameplay::World
{
    IMPLEMENT_CLASS(EncounterZoneComponent, Core::Component::Component)

    EncounterZoneComponent::EncounterZoneComponent()
    {
        Shape = EEncounterZoneShape::Sphere;
        HalfExtents = Math::Vector3::One;
        Radius = 1.0f;
        HalfHeight = 1.0f;
        EnterMargin = 0.0f;
        ExitMargin = 0.0f;
        ResetDelaySeconds = 0.0f;
        ResetPolicy = EEncounterResetPolicy::FullHeal;
        ProfileBlendSeconds = 1.0f;
        bRequiresProgression = false;
        Priority = 0;
        SensorMask = ~uint32_t{0};
    }

    void EncounterZoneComponent::Initialize()
    {
        Super::Initialize();
        m_State = EEncounterZoneState::Outside;
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestRegisterZone(GetOwner()->GetEntityHandle());
        }
    }

    void EncounterZoneComponent::BeginPlay()
    {
        Super::BeginPlay();
        // 復元時の Initialize が World への接続より先でも、接続後に登録を依頼する。
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestRegisterZone(GetOwner()->GetEntityHandle());
        }
    }

    void EncounterZoneComponent::Finalize()
    {
        if (auto* module = FindGameplayModule(Core::Module::GetModuleRegistry()); module && GetOwner())
        {
            module->GetEncounterService().RequestUnregisterZone(GetOwner()->GetEntityHandle());
        }
        m_State = EEncounterZoneState::Outside;
        Super::Finalize();
    }
} // namespace NorvesLib::Modules::Gameplay::World
