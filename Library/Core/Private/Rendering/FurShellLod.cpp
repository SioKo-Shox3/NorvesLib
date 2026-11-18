// 距離に応じたファーの殻数と殻の境界の上限を求める。
#include "Rendering/FurShellLod.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    static_assert(FurDefaultShellLodLevelCount >= FurMaxLodLevelCount + 1u);

    float ComputeFurShellCountContinuous(const FurParams& params, float distanceMeters)
    {
        const auto clampCount = [](float count)
        {
            return std::clamp(count, static_cast<float>(FurMinLayerCount), static_cast<float>(FurMaxLayerCount));
        };
        if (!std::isfinite(distanceMeters))
        {
            return clampCount(static_cast<float>(params.LayerCount));
        }

        FurLodLevel points[FurDefaultShellLodLevelCount]{};
        uint32_t pointCount = 0u;
        if (params.LodLevelCount == 0u)
        {
            for (const FurLodLevel& level : FurDefaultShellLodLevels)
            {
                points[pointCount++] = {level.StartDistanceMeters, std::min(level.LayerCount, params.LayerCount)};
            }
        }
        else
        {
            if (params.LodLevels[0].StartDistanceMeters > 0.0f)
            {
                points[pointCount++] = {0.0f, params.LayerCount};
            }
            const uint32_t levelCount = std::min(params.LodLevelCount, FurMaxLodLevelCount);
            for (uint32_t index = 0u; index < levelCount; ++index)
            {
                points[pointCount++] = params.LodLevels[index];
            }
        }

        const float distance = std::max(distanceMeters, 0.0f);
        if (distance <= points[0].StartDistanceMeters)
        {
            return clampCount(static_cast<float>(points[0].LayerCount));
        }
        for (uint32_t index = 1u; index < pointCount; ++index)
        {
            const FurLodLevel& previous = points[index - 1u];
            const FurLodLevel& current = points[index];
            if (distance <= current.StartDistanceMeters)
            {
                const float ratio = (distance - previous.StartDistanceMeters) /
                                    (current.StartDistanceMeters - previous.StartDistanceMeters);
                const float first = static_cast<float>(previous.LayerCount);
                const float last = static_cast<float>(current.LayerCount);
                return clampCount(first + (last - first) * ratio);
            }
        }
        return clampCount(static_cast<float>(points[pointCount - 1u].LayerCount));
    }

    FurShellSelection SelectFurShells(const FurParams& params, float distanceMeters)
    {
        FurShellSelection selection;
        selection.ContinuousCount = ComputeFurShellCountContinuous(params, distanceMeters);
        selection.ShellCount = static_cast<uint32_t>(std::ceil(selection.ContinuousCount));
        selection.LastShellFraction = selection.ShellCount == 1u
            ? 1.0f
            : selection.ContinuousCount - static_cast<float>(selection.ShellCount - 1u);
        return selection;
    }

    float ComputeFurMaxShellLengthMeters(const FurMaterialData& fur)
    {
        float maxRegionScale = 1.0f;
        const uint32_t regionCount = std::min(fur.RegionCount, FurMaxRegionCount);
        for (uint32_t index = 0u; index < regionCount; ++index)
        {
            maxRegionScale = std::max(maxRegionScale, fur.Regions[index].LengthScale);
        }
        return fur.Params.MaxLengthMeters * fur.Params.LengthScale * maxRegionScale;
    }
} // namespace NorvesLib::Core::Rendering
