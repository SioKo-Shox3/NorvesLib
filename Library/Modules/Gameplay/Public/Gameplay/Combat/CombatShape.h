#pragma once

#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    enum class ECombatShapeType : uint8_t
    {
        Sphere,
        Capsule,
        Box
    };

    // 形は束縛先のローカル空間で表し、長さにスケールを適用しない。
    struct CombatShape
    {
        ECombatShapeType Type = ECombatShapeType::Sphere;
        Math::Vector3 Center;
        Math::Vector3 PointA;
        Math::Vector3 PointB;
        float Radius = 0.0f;
        Math::Vector3 HalfExtents;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat
