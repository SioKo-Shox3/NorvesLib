#pragma once

#include "Math/GeometryTypes.h"
#include "Scene/SceneQuery.h"

#include <cstdint>

namespace NorvesLib::Modules::Physics
{
    enum class EPhysicsResult : uint8_t
    {
        Success,
        InvalidArgument,
        WrongThread,
        NotRegistered,
        Duplicate,
        InvalidState,
    };

    struct PhysicsCallbackHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const PhysicsCallbackHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }
    };

    enum class EPhysicsBodyType : uint8_t
    {
        Static,
        Dynamic,
        Kinematic,
    };

    struct PhysicsContactEvent
    {
        Core::Scene::ColliderHandle Self;
        Core::Scene::ColliderHandle Other;
        Math::GeometryContact Contact;
        float NormalImpulse = 0.0f;
    };
} // namespace NorvesLib::Modules::Physics
