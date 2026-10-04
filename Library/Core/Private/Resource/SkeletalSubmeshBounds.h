#pragma once

#include "Container/Span.h"
#include "Resource/SkeletalSubMesh.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Skeletal
{
    // readPositionは不変な頂点のX/Y/Zを返す。範囲/有限性は全点で先に検査する。
    // float中心の丸め後に半径を求め、正の半径は1ULP外側へ丸める。失敗時outは不変。
    template<typename ReadPosition>
    [[nodiscard]] bool ComputeSkeletalSubmeshBounds(Container::Span<const uint32_t> indices,
        size_t vertexCount, ReadPosition readPosition, SkeletalSubMesh& out) noexcept
    {
        if (!indices.data() || indices.empty() || vertexCount == 0)
        {
            return false;
        }
        double minimum[3] = {INFINITY, INFINITY, INFINITY};
        double maximum[3] = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t index : indices)
        {
            if (index >= vertexCount)
            {
                return false;
            }
            const auto position = readPosition(index);
            const double values[] = {position.X, position.Y, position.Z};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(values[axis]))
                {
                    return false;
                }
                minimum[axis] = std::fmin(minimum[axis], values[axis]);
                maximum[axis] = std::fmax(maximum[axis], values[axis]);
            }
        }
        SkeletalSubMesh candidate = out;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            candidate.BoundsCenter[axis] = static_cast<float>((minimum[axis] + maximum[axis]) * 0.5);
            if (!std::isfinite(candidate.BoundsCenter[axis]))
            {
                return false;
            }
        }
        double maximumSquared = 0.0;
        for (uint32_t index : indices)
        {
            const auto position = readPosition(index);
            const double x = double(position.X) - candidate.BoundsCenter[0];
            const double y = double(position.Y) - candidate.BoundsCenter[1];
            const double z = double(position.Z) - candidate.BoundsCenter[2];
            maximumSquared = std::fmax(maximumSquared, x*x + y*y + z*z);
        }
        const double radius = std::sqrt(maximumSquared);
        candidate.BoundsRadius = static_cast<float>(radius);
        if (radius > 0.0)
        {
            candidate.BoundsRadius = std::nextafter(candidate.BoundsRadius, std::numeric_limits<float>::infinity());
        }
        if (!std::isfinite(candidate.BoundsRadius))
        {
            return false;
        }
        out = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Skeletal
