#pragma once

#include "Container/String.h"
#include "Object/EntityHandle.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::World
{
    enum class EEncounterZoneShape : uint8_t
    {
        Sphere,
        Box,
        CylinderY
    };

    enum class EEncounterZoneState : uint8_t
    {
        Outside,
        Engaged,
        Resetting,
        Cleared
    };

    enum class EEncounterResetPolicy : uint8_t
    {
        FullHeal,
        KeepDamage
    };

    /// 評価中の設定を写した通知。Entity の生存は配信直前に再検証する。
    struct EncounterZoneEvent
    {
        Core::EntityHandle ZoneEntity;
        Core::Container::String ZoneId;
        Core::Container::String EnemyId;
        Core::Container::String ProfileId;
        EEncounterResetPolicy ResetPolicy = EEncounterResetPolicy::FullHeal;
        uint64_t EvaluationSequence = 0;
    };
} // namespace NorvesLib::Modules::Gameplay::World
