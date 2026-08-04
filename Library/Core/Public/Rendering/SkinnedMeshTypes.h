#pragma once

#include "Container/Containers.h"
#include "Math/GeometryTypes.h"
#include "Math/Matrix4x4.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct SkinnedMeshProxy
    {
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;
        Math::Matrix4x4 WorldTransform;
        Container::VariableArray<Math::Matrix4x4> BonePalette;
        Math::AABB AnimatedBounds;
        bool bHasAnimatedBounds = false;
        bool bVisible = true;

        [[nodiscard]] bool IsValid() const
        {
            return ComponentId != 0 && !BonePalette.empty() && bHasAnimatedBounds && bVisible;
        }
    };
} // namespace NorvesLib::Core::Rendering
