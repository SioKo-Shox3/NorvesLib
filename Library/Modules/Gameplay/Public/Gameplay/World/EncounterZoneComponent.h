#pragma once

#include "Component/Component.h"
#include "Gameplay/World/EncounterZoneTypes.h"

namespace NorvesLib::Modules::Gameplay::World
{
    /// ワールド軸に沿う解析形状と遭遇設定。実行時の状態は保存しない。
    class EncounterZoneComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(EncounterZoneComponent, Core::Component::Component)

    public:
        EncounterZoneComponent();
        void Initialize() override;
        void BeginPlay() override;
        void Finalize() override;
        EEncounterZoneState GetState() const { return m_State; }

        PROPERTY(Core::Container::String, ZoneId)
        PROPERTY(EEncounterZoneShape, Shape)
        PROPERTY(Math::Vector3, HalfExtents)
        PROPERTY(float, Radius)
        PROPERTY(float, HalfHeight)
        PROPERTY(float, EnterMargin)
        PROPERTY(float, ExitMargin)
        PROPERTY(float, ResetDelaySeconds)
        PROPERTY(EEncounterResetPolicy, ResetPolicy)
        PROPERTY(Core::Container::String, EnemyId)
        PROPERTY(Core::Container::String, ProfileId)
        PROPERTY(float, ProfileBlendSeconds)
        PROPERTY(bool, bRequiresProgression)
        PROPERTY(int32_t, Priority)
        PROPERTY(uint32_t, SensorMask)

    private:
        friend class EncounterService;
        EEncounterZoneState m_State = EEncounterZoneState::Outside;
    };
} // namespace NorvesLib::Modules::Gameplay::World
