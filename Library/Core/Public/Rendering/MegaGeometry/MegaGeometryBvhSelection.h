#pragma once

// クラスタのグループの BVH（NVMESH v1.1）をたどってクラスタを選ぶ計算の、CPU での写し。
// GPU のカリング（cluster_bvh_cull.comp）は節を段ごとに判定し、枝を切り、葉のクラスタを cluster_cull.comp と同じ
// 判定にかける。ここは同じ枝の切り方・クラスタの判定を CPU で行い、BVH をたどった選択が平らなクラスタの列の選択と
// 一致すること（枝を切る条件が保守的であること）を試験で確かめるのに使う。
// 法線のコーンの判定・Hi-Z の遮蔽の判定は、呼び出し側が与える遮蔽の関数（球を包む判定）で代える。

#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /**
     * @brief 節の LOD の判定の余裕（cluster_bvh_cull.comp の BVH_LOD_PRUNE_MARGIN と同じ）
     *
     * 節の球は葉の球を包み、誤差は葉の最大以上なので、節の投影は葉の投影以上になる（単調）。浮動小数の丸めで
     * 投影が葉より僅かに小さく出ても、葉で描かれるクラスタを枝ごと切らないよう、この倍率を掛けて比べる。
     */
    constexpr float BVH_LOD_PRUNE_MARGIN = 1.001f;

    /** @brief BVH をたどる判定の、カメラ・画面・視錐台の値（cluster_bvh_cull.comp の cullData に相当） */
    struct BvhCullView
    {
        BakedLODView Lod;
        /** @brief 視錐台の6平面（内向きの法線 a,b,c と d。a·x + b·y + c·z + d < −半径 なら外） */
        float FrustumPlanes[6][4] = {};
    };

    /** @brief ワールドの球が視錐台の外か（cluster_cull.comp の FrustumCullSphere と同じ） */
    inline bool IsWorldSphereOutsideFrustum(const float (&planes)[6][4], const float *center, float radius)
    {
        for (uint32_t plane = 0; plane < 6; ++plane)
        {
            const float distance =
                planes[plane][0] * center[0] + planes[plane][1] * center[1] + planes[plane][2] * center[2] + planes[plane][3];
            if (distance < -radius)
            {
                return true;
            }
        }
        return false;
    }

    /** @brief ローカルの球を、行ベクトル規約のワールド行列でワールドへ移した中心と半径（半径は最大の軸の伸びで広げる） */
    inline void TransformSphereToWorld(float centerX, float centerY, float centerZ, float radius,
                                       const float *worldMatrix, float *outCenter, float &outRadius)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            outCenter[axis] = centerX * worldMatrix[0 + axis] + centerY * worldMatrix[4 + axis] +
                              centerZ * worldMatrix[8 + axis] + worldMatrix[12 + axis];
        }
        outRadius = radius * ComputeWorldMaxAxisScale(worldMatrix);
    }

    /**
     * @brief 節の LOD の判定: 節の球から投影した親の誤差の最大が許容以下なら true（下のどのクラスタも描かれない）
     *
     * NaN は切らない側（false）。cluster_bvh_cull.comp の CanPruneBvhNodeByLOD と同じ。
     */
    inline bool CanPruneBvhNodeByLOD(const GPUGroupBVHNode &node, const float *worldMatrix, const BakedLODView &view)
    {
        BoundingSphere sphere;
        sphere.CenterX = node.BoundsCenterX;
        sphere.CenterY = node.BoundsCenterY;
        sphere.CenterZ = node.BoundsCenterZ;
        sphere.Radius = node.BoundsRadius;
        return ProjectBakedSphereError(sphere, node.MaxParentError, worldMatrix, view) * BVH_LOD_PRUNE_MARGIN <=
               view.LODBias;
    }

    /** @brief BVH をたどった選択の、判定の数（試験が枝を切る効果を確かめる） */
    struct BvhTraversalStats
    {
        uint32_t NodesTested = 0;
        uint32_t NodesPrunedByFrustum = 0;
        uint32_t NodesPrunedByOcclusion = 0;
        uint32_t NodesPrunedByLOD = 0;
        uint32_t ClustersTested = 0;
    };

    namespace BvhSelectionDetail
    {
        /** @brief クラスタの判定（視錐台・焼き込み済みの階層の段・遮蔽）。平らな列と葉の中で同じ */
        template <typename OcclusionFn>
        bool ClusterIsSelected(const MeshCluster &cluster, const float *worldMatrix, const BvhCullView &view,
                               OcclusionFn &&isOccluded)
        {
            float center[3];
            float radius = 0.0f;
            TransformSphereToWorld(cluster.Bounds.CenterX, cluster.Bounds.CenterY, cluster.Bounds.CenterZ,
                                   cluster.Bounds.Radius, worldMatrix, center, radius);
            return !IsWorldSphereOutsideFrustum(view.FrustumPlanes, center, radius) &&
                   ShouldDrawBakedCluster(cluster, worldMatrix, view.Lod) && !isOccluded(center, radius);
        }
    } // namespace BvhSelectionDetail

    /** @brief 平らなクラスタの列を全部判定して、選ばれるクラスタの番号を昇順で返す（cluster_cull.comp の経路） */
    template <typename OcclusionFn>
    void SelectClustersFlat(const VariableArray<MeshCluster> &clusters, const float *worldMatrix,
                            const BvhCullView &view, OcclusionFn &&isOccluded, VariableArray<uint32_t> &outSelected)
    {
        outSelected.clear();
        for (uint32_t index = 0; index < clusters.size(); ++index)
        {
            if (BvhSelectionDetail::ClusterIsSelected(clusters[index], worldMatrix, view, isOccluded))
            {
                outSelected.push_back(index);
            }
        }
    }

    /**
     * @brief BVH をたどってグループを選び、葉のクラスタを判定して、選ばれるクラスタの番号を昇順で返す
     *
     * 節ごとに、視錐台（節の球）・遮蔽（節の球）・LOD（節の球から投影した親の誤差の最大）で枝を切る。
     * 葉に届いたクラスタは平らな列と同じ判定にかける。
     */
    template <typename OcclusionFn>
    void SelectClustersByBvh(const VariableArray<MeshCluster> &clusters, const VariableArray<GPUGroupBVHNode> &nodes,
                             const float *worldMatrix, const BvhCullView &view, OcclusionFn &&isOccluded,
                             VariableArray<uint32_t> &outSelected, BvhTraversalStats *outStats = nullptr)
    {
        outSelected.clear();
        BvhTraversalStats stats;
        if (!nodes.empty())
        {
            VariableArray<uint32_t> pending;
            pending.push_back(0);
            while (!pending.empty())
            {
                const GPUGroupBVHNode &node = nodes[pending.back()];
                pending.pop_back();
                ++stats.NodesTested;

                float center[3];
                float radius = 0.0f;
                TransformSphereToWorld(node.BoundsCenterX, node.BoundsCenterY, node.BoundsCenterZ, node.BoundsRadius,
                                       worldMatrix, center, radius);
                if (IsWorldSphereOutsideFrustum(view.FrustumPlanes, center, radius))
                {
                    ++stats.NodesPrunedByFrustum;
                    continue;
                }
                if (isOccluded(center, radius))
                {
                    ++stats.NodesPrunedByOcclusion;
                    continue;
                }
                if (CanPruneBvhNodeByLOD(node, worldMatrix, view.Lod))
                {
                    ++stats.NodesPrunedByLOD;
                    continue;
                }

                if ((node.Flags & GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0u)
                {
                    for (uint32_t member = node.First; member < node.First + node.Count; ++member)
                    {
                        ++stats.ClustersTested;
                        if (BvhSelectionDetail::ClusterIsSelected(clusters[member], worldMatrix, view, isOccluded))
                        {
                            outSelected.push_back(member);
                        }
                    }
                }
                else
                {
                    for (uint32_t child = node.First; child < node.First + node.Count; ++child)
                    {
                        pending.push_back(child);
                    }
                }
            }
            std::sort(outSelected.begin(), outSelected.end());
        }
        if (outStats != nullptr)
        {
            *outStats = stats;
        }
    }

    /**
     * @brief BVH の構造を検査し、段ごとの節の数と葉の数を返す（MegaGeometryResourceStore が GPU へ渡す前に使う）
     *
     * 幅優先の並び（内部の節の子は、それまでの子の合計 + 1 から連続する）・子の数と葉の大きさの上限・葉の範囲が
     * クラスタの数に収まり、全クラスタがちょうど1つの葉に入ること・値が有限で非負であることを確かめる。
     * GPU は節を段ごとの列へ積んでたどるので、この構造（各節の親が1つ）が崩れていると列があふれる。
     */
    inline bool AnalyzeGroupBVH(const VariableArray<GPUGroupBVHNode> &nodes, uint32_t clusterCount,
                                VariableArray<uint32_t> &outLevelNodeCounts, uint32_t &outLeafCount)
    {
        outLevelNodeCounts.clear();
        outLeafCount = 0;
        if (nodes.empty() || clusterCount == 0 || nodes.size() > 0x7FFFFFFFull)
        {
            return false;
        }

        VariableArray<uint32_t> levelOf(nodes.size(), 0);
        VariableArray<uint8_t> covered(clusterCount, 0);
        uint64_t coveredTotal = 0;
        uint64_t nextChild = 1;
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            const GPUGroupBVHNode &node = nodes[index];
            if (!std::isfinite(node.BoundsCenterX) || !std::isfinite(node.BoundsCenterY) ||
                !std::isfinite(node.BoundsCenterZ) || !std::isfinite(node.BoundsRadius) || node.BoundsRadius < 0.0f ||
                !std::isfinite(node.MaxParentError) || node.MaxParentError < 0.0f || (node.Flags & ~GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0u)
            {
                return false;
            }

            const uint32_t level = levelOf[index];
            if (level >= GROUP_BVH_MAX_LEVELS)
            {
                return false;
            }
            if (outLevelNodeCounts.size() <= level)
            {
                outLevelNodeCounts.resize(level + 1, 0u);
            }
            ++outLevelNodeCounts[level];

            if ((node.Flags & GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0u)
            {
                if (node.Count == 0 || node.Count > GROUP_BVH_MAX_LEAF_CLUSTERS ||
                    static_cast<uint64_t>(node.First) + node.Count > clusterCount)
                {
                    return false;
                }
                for (uint32_t member = node.First; member < node.First + node.Count; ++member)
                {
                    if (covered[member] != 0)
                    {
                        return false;
                    }
                    covered[member] = 1;
                    ++coveredTotal;
                }
                ++outLeafCount;
                continue;
            }

            if (node.Count == 0 || node.Count > GROUP_BVH_MAX_CHILDREN || node.First != nextChild ||
                nextChild + node.Count > nodes.size())
            {
                return false;
            }
            for (uint32_t child = node.First; child < node.First + node.Count; ++child)
            {
                levelOf[child] = level + 1;
            }
            nextChild += node.Count;
        }
        return nextChild == nodes.size() && coveredTotal == clusterCount;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
