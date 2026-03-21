#include "Rendering/MegaGeometry/ClusterGroupBuilder.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    namespace
    {
        /**
         * @brief 2つのバウンディングスフィア間の中心距離を計算
         */
        float DistanceBetweenCenters(const BoundingSphere &a, const BoundingSphere &b)
        {
            float dx = a.CenterX - b.CenterX;
            float dy = a.CenterY - b.CenterY;
            float dz = a.CenterZ - b.CenterZ;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        /**
         * @brief 2つのバウンディングスフィアをマージ
         */
        BoundingSphere MergeBounds(const BoundingSphere &a, const BoundingSphere &b)
        {
            if (!a.IsValid())
            {
                return b;
            }
            if (!b.IsValid())
            {
                return a;
            }

            float dx = b.CenterX - a.CenterX;
            float dy = b.CenterY - a.CenterY;
            float dz = b.CenterZ - a.CenterZ;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

            // 片方がもう片方を完全に包含する場合
            if (dist + b.Radius <= a.Radius)
            {
                return a;
            }
            if (dist + a.Radius <= b.Radius)
            {
                return b;
            }

            // 新しいバウンディングスフィア
            float newRadius = (dist + a.Radius + b.Radius) * 0.5f;
            float t = (newRadius - a.Radius) / dist;

            BoundingSphere result;
            result.CenterX = a.CenterX + dx * t;
            result.CenterY = a.CenterY + dy * t;
            result.CenterZ = a.CenterZ + dz * t;
            result.Radius = newRadius;
            return result;
        }
    } // anonymous namespace

    VariableArray<ClusterGroup> ClusterGroupBuilder::BuildGroups(
        const VariableArray<MeshCluster> &clusters,
        uint32_t clusterStartIndex,
        uint32_t clusterCount)
    {
        VariableArray<ClusterGroup> groups;

        if (clusterCount == 0)
        {
            return groups;
        }

        // 1クラスタ以下ならグループ化不要（1グループにまとめる）
        if (clusterCount <= MAX_CLUSTERS_PER_GROUP)
        {
            ClusterGroup group;
            for (uint32_t i = 0; i < clusterCount; ++i)
            {
                uint32_t idx = clusterStartIndex + i;
                group.ClusterIndices.push_back(idx);
                group.Bounds = MergeBounds(group.Bounds, clusters[idx].Bounds);
            }
            group.LODLevel = clusters[clusterStartIndex].LODLevel;
            groups.push_back(std::move(group));
            return groups;
        }

        // ========================================
        // 貪欲法によるグループ化
        // ========================================

        VariableArray<bool> assigned(clusterCount, false);
        uint32_t assignedCount = 0;

        while (assignedCount < clusterCount)
        {
            // まだ未割り当てのクラスタからシードを選択
            uint32_t seedLocal = UINT32_MAX;
            for (uint32_t i = 0; i < clusterCount; ++i)
            {
                if (!assigned[i])
                {
                    seedLocal = i;
                    break;
                }
            }

            if (seedLocal == UINT32_MAX)
            {
                break;
            }

            ClusterGroup group;
            uint32_t seedGlobal = clusterStartIndex + seedLocal;
            group.ClusterIndices.push_back(seedGlobal);
            group.Bounds = clusters[seedGlobal].Bounds;
            group.LODLevel = clusters[seedGlobal].LODLevel;
            assigned[seedLocal] = true;
            ++assignedCount;

            // 最大クラスタ数まで、最も近いクラスタを追加
            while (static_cast<uint32_t>(group.ClusterIndices.size()) < MAX_CLUSTERS_PER_GROUP &&
                   assignedCount < clusterCount)
            {
                float bestDist = std::numeric_limits<float>::max();
                uint32_t bestLocal = UINT32_MAX;

                // グループ中心に最も近い未割り当てクラスタを探す
                for (uint32_t i = 0; i < clusterCount; ++i)
                {
                    if (assigned[i])
                    {
                        continue;
                    }

                    uint32_t globalIdx = clusterStartIndex + i;
                    float dist = DistanceBetweenCenters(group.Bounds, clusters[globalIdx].Bounds);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        bestLocal = i;
                    }
                }

                if (bestLocal == UINT32_MAX)
                {
                    break;
                }

                uint32_t bestGlobal = clusterStartIndex + bestLocal;
                group.ClusterIndices.push_back(bestGlobal);
                group.Bounds = MergeBounds(group.Bounds, clusters[bestGlobal].Bounds);
                assigned[bestLocal] = true;
                ++assignedCount;
            }

            groups.push_back(std::move(group));
        }

        // 最後のグループが小さすぎる場合、前のグループにマージ
        if (groups.size() > 1)
        {
            auto &lastGroup = groups.back();
            if (static_cast<uint32_t>(lastGroup.ClusterIndices.size()) < MIN_CLUSTERS_PER_GROUP)
            {
                auto &prevGroup = groups[groups.size() - 2];
                for (uint32_t idx : lastGroup.ClusterIndices)
                {
                    prevGroup.ClusterIndices.push_back(idx);
                }
                prevGroup.Bounds = MergeBounds(prevGroup.Bounds, lastGroup.Bounds);
                groups.pop_back();
            }
        }

        NORVES_LOG_DEBUG("ClusterGroupBuilder",
                         "グループ化完了: %u クラスタ → %u グループ",
                         clusterCount, static_cast<uint32_t>(groups.size()));

        return groups;
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
