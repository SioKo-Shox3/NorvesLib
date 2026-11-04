#pragma once

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
} // namespace NorvesLib::Modules::Gameplay::World
