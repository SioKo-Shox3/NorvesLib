#pragma once

// BVH を持たないメッシュ（NVMESH v1.0・手続き・実行時に作るメッシュ）のクラスタに、グループの BVH（NVMESH v1.1 と
// 同じ節の形・同じ上限）を実行時に作る。
// 葉は「並べ替えたクラスタの列」の連続した範囲で、並べ替えの表（ClusterOrder）を一緒に返す。クラスタの記録そのものは
// 並べ替えないので、今の平らな経路の描画は変わらない（BVH の経路で描くときに、この表でクラスタを並べ直す）。
// 作り方: LOD の段ごとにクラスタを分け、段の中を中心の広がりが最も大きい軸の中央で再帰的に二分して、8 クラスタ以下の葉を
// 作る（葉はなるべく 8 個で埋まるように分ける）。上の段は、並びの隣り合う節を 8 個ずつ束ねて組む。

#include "Rendering/MegaGeometry/MegaGeometryBvhSelection.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    /** @brief 実行時に作ったグループの BVH */
    struct BuiltClusterBVH
    {
        /** @brief 節（幅優先の並び。根が 0 番。葉の First・Count は ClusterOrder の位置） */
        VariableArray<GPUGroupBVHNode> Nodes;
        /** @brief 葉の範囲の位置 → 元のクラスタの番号（MegaMeshCreateInfo::Clusters などの添字） */
        VariableArray<uint32_t> ClusterOrder;
        /** @brief 段ごとの節の数（添字が段） */
        VariableArray<uint32_t> LevelNodeCounts;
        uint32_t LeafCount = 0;

        bool IsEmpty() const { return Nodes.empty(); }
    };

    namespace BvhBuildDetail
    {
        struct SphereD
        {
            double X = 0.0;
            double Y = 0.0;
            double Z = 0.0;
            double R = 0.0;
        };

        inline SphereD ToSphereD(const BoundingSphere &sphere)
        {
            return SphereD{sphere.CenterX, sphere.CenterY, sphere.CenterZ, sphere.Radius};
        }

        inline SphereD ToSphereD(const GPUGroupBVHNode &node)
        {
            return SphereD{node.BoundsCenterX, node.BoundsCenterY, node.BoundsCenterZ, node.BoundsRadius};
        }

        /** @brief 焼き込み済みの階層で親のグループを持つクラスタ（親の球も節に包ませる） */
        inline bool HasParentSphere(const MeshCluster &cluster)
        {
            return cluster.GroupId != INVALID_CLUSTER_GROUP_ID;
        }

        inline bool IsSphereFinite(const BoundingSphere &sphere)
        {
            return std::isfinite(sphere.CenterX) && std::isfinite(sphere.CenterY) && std::isfinite(sphere.CenterZ) &&
                   std::isfinite(sphere.Radius) && sphere.Radius >= 0.0f;
        }

        /** @brief 球 inner が球 outer の中に入るか（倍精度で、丸めの分だけ許す） */
        inline bool SphereContains(const SphereD &outer, const SphereD &inner)
        {
            const double dx = inner.X - outer.X;
            const double dy = inner.Y - outer.Y;
            const double dz = inner.Z - outer.Z;
            const double reach = std::sqrt(dx * dx + dy * dy + dz * dz) + inner.R;
            return reach <= outer.R + 1.0e-6 * std::max(1.0, outer.R);
        }

        /**
         * @brief 球の集まりを包む球を作り、節へ書く
         *
         * 中心は球の広がりの箱の中心。中心を float へ丸めた後に、その中心から各球の遠い端までの距離の最大を倍精度で求め、
         * 半径は float へ切り上げる（float の節が全ての球を必ず包む）。
         */
        class EnclosingSphereBuilder
        {
        public:
            void Add(const SphereD &sphere)
            {
                m_Spheres.push_back(sphere);
            }

            void WriteTo(GPUGroupBVHNode &node) const
            {
                double minX = std::numeric_limits<double>::max();
                double minY = std::numeric_limits<double>::max();
                double minZ = std::numeric_limits<double>::max();
                double maxX = std::numeric_limits<double>::lowest();
                double maxY = std::numeric_limits<double>::lowest();
                double maxZ = std::numeric_limits<double>::lowest();
                for (const SphereD &sphere : m_Spheres)
                {
                    minX = std::min(minX, sphere.X - sphere.R);
                    minY = std::min(minY, sphere.Y - sphere.R);
                    minZ = std::min(minZ, sphere.Z - sphere.R);
                    maxX = std::max(maxX, sphere.X + sphere.R);
                    maxY = std::max(maxY, sphere.Y + sphere.R);
                    maxZ = std::max(maxZ, sphere.Z + sphere.R);
                }
                node.BoundsCenterX = static_cast<float>((minX + maxX) * 0.5);
                node.BoundsCenterY = static_cast<float>((minY + maxY) * 0.5);
                node.BoundsCenterZ = static_cast<float>((minZ + maxZ) * 0.5);
                const SphereD center{node.BoundsCenterX, node.BoundsCenterY, node.BoundsCenterZ, 0.0};
                double radius = 0.0;
                for (const SphereD &sphere : m_Spheres)
                {
                    const double dx = sphere.X - center.X;
                    const double dy = sphere.Y - center.Y;
                    const double dz = sphere.Z - center.Z;
                    radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz) + sphere.R);
                }
                float radiusF = static_cast<float>(radius);
                if (static_cast<double>(radiusF) < radius)
                {
                    radiusF = std::nextafter(radiusF, std::numeric_limits<float>::infinity());
                }
                node.BoundsRadius = radiusF;
            }

        private:
            VariableArray<SphereD> m_Spheres;
        };

        /**
         * @brief order[begin, end)（同じ LOD の段のクラスタ）を、中心の広がりが最も大きい軸で再帰的に二分して葉を作る
         *
         * 左に 8 の倍数の個数を渡すので、葉は最後の 1 つを除いて 8 個で埋まる。同じ座標は番号で順を決める（決定的）。
         */
        inline void SplitIntoLeaves(const VariableArray<MeshCluster> &clusters, VariableArray<uint32_t> &order,
                                    uint32_t begin, uint32_t end, VariableArray<GPUGroupBVHNode> &outLeaves)
        {
            const uint32_t count = end - begin;
            if (count <= GROUP_BVH_MAX_LEAF_CLUSTERS)
            {
                GPUGroupBVHNode leaf{};
                leaf.First = begin;
                leaf.Count = count;
                leaf.Flags = GPU_GROUP_BVH_NODE_FLAG_LEAF;
                outLeaves.push_back(leaf);
                return;
            }

            float minCenter[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                  std::numeric_limits<float>::max()};
            float maxCenter[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                  std::numeric_limits<float>::lowest()};
            for (uint32_t position = begin; position < end; ++position)
            {
                const BoundingSphere &bounds = clusters[order[position]].Bounds;
                const float center[3] = {bounds.CenterX, bounds.CenterY, bounds.CenterZ};
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    minCenter[axis] = std::min(minCenter[axis], center[axis]);
                    maxCenter[axis] = std::max(maxCenter[axis], center[axis]);
                }
            }
            uint32_t splitAxis = 0;
            for (uint32_t axis = 1; axis < 3; ++axis)
            {
                if (maxCenter[axis] - minCenter[axis] > maxCenter[splitAxis] - minCenter[splitAxis])
                {
                    splitAxis = axis;
                }
            }

            const uint32_t leavesNeeded = (count + GROUP_BVH_MAX_LEAF_CLUSTERS - 1) / GROUP_BVH_MAX_LEAF_CLUSTERS;
            const uint32_t leftCount = (leavesNeeded / 2) * GROUP_BVH_MAX_LEAF_CLUSTERS;
            auto axisValue = [&clusters, splitAxis](uint32_t clusterIndex) {
                const BoundingSphere &bounds = clusters[clusterIndex].Bounds;
                return splitAxis == 0 ? bounds.CenterX : (splitAxis == 1 ? bounds.CenterY : bounds.CenterZ);
            };
            std::nth_element(order.begin() + begin, order.begin() + begin + leftCount, order.begin() + end,
                             [&axisValue](uint32_t a, uint32_t b) {
                                 const float valueA = axisValue(a);
                                 const float valueB = axisValue(b);
                                 return valueA < valueB || (valueA == valueB && a < b);
                             });
            SplitIntoLeaves(clusters, order, begin, begin + leftCount, outLeaves);
            SplitIntoLeaves(clusters, order, begin + leftCount, end, outLeaves);
        }
    } // namespace BvhBuildDetail

    /**
     * @brief クラスタにグループの BVH を作る
     * @return クラスタが無い・境界球か親の誤差が有限でない・段の数が上限を超えるなら false（out は空）
     */
    inline bool BuildClusterBVH(const VariableArray<MeshCluster> &clusters, BuiltClusterBVH &out)
    {
        out = BuiltClusterBVH{};
        if (clusters.empty() || clusters.size() > 0x7FFFFFFFull)
        {
            return false;
        }
        for (const MeshCluster &cluster : clusters)
        {
            if (!BvhBuildDetail::IsSphereFinite(cluster.Bounds) || !std::isfinite(cluster.ParentError) ||
                cluster.ParentError < 0.0f ||
                (BvhBuildDetail::HasParentSphere(cluster) && !BvhBuildDetail::IsSphereFinite(cluster.ParentBounds)))
            {
                return false;
            }
        }

        // LOD の段ごとに分け（段の中は番号の順）、段の中を空間で分けて葉を作る
        const uint32_t clusterCount = static_cast<uint32_t>(clusters.size());
        VariableArray<uint32_t> order(clusterCount);
        for (uint32_t index = 0; index < clusterCount; ++index)
        {
            order[index] = index;
        }
        std::stable_sort(order.begin(), order.end(), [&clusters](uint32_t a, uint32_t b) {
            return clusters[a].LODLevel < clusters[b].LODLevel;
        });

        VariableArray<GPUGroupBVHNode> leaves;
        leaves.reserve((clusterCount + GROUP_BVH_MAX_LEAF_CLUSTERS - 1) / GROUP_BVH_MAX_LEAF_CLUSTERS + 64);
        uint32_t levelBegin = 0;
        while (levelBegin < clusterCount)
        {
            uint32_t levelEnd = levelBegin + 1;
            while (levelEnd < clusterCount && clusters[order[levelEnd]].LODLevel == clusters[order[levelBegin]].LODLevel)
            {
                ++levelEnd;
            }
            BvhBuildDetail::SplitIntoLeaves(clusters, order, levelBegin, levelEnd, leaves);
            levelBegin = levelEnd;
        }

        // 葉の球と親の誤差の最大
        for (GPUGroupBVHNode &leaf : leaves)
        {
            BvhBuildDetail::EnclosingSphereBuilder enclosing;
            float maxParentError = 0.0f;
            for (uint32_t position = leaf.First; position < leaf.First + leaf.Count; ++position)
            {
                const MeshCluster &cluster = clusters[order[position]];
                enclosing.Add(BvhBuildDetail::ToSphereD(cluster.Bounds));
                if (BvhBuildDetail::HasParentSphere(cluster))
                {
                    enclosing.Add(BvhBuildDetail::ToSphereD(cluster.ParentBounds));
                }
                maxParentError = std::max(maxParentError, cluster.ParentError);
            }
            enclosing.WriteTo(leaf);
            leaf.MaxParentError = maxParentError;
        }

        // 下の段の隣り合う節を 8 個ずつ束ねて、根が 1 つになるまで上の段を組む（levels[0] が葉の段）
        VariableArray<VariableArray<GPUGroupBVHNode>> levels;
        levels.push_back(std::move(leaves));
        while (levels.back().size() > 1)
        {
            if (levels.size() >= GROUP_BVH_MAX_LEVELS)
            {
                return false;
            }
            const VariableArray<GPUGroupBVHNode> &below = levels.back();
            VariableArray<GPUGroupBVHNode> above;
            above.reserve((below.size() + GROUP_BVH_MAX_CHILDREN - 1) / GROUP_BVH_MAX_CHILDREN);
            for (uint32_t first = 0; first < below.size(); first += GROUP_BVH_MAX_CHILDREN)
            {
                const uint32_t childCount =
                    std::min<uint32_t>(GROUP_BVH_MAX_CHILDREN, static_cast<uint32_t>(below.size()) - first);
                GPUGroupBVHNode parent{};
                parent.First = first; // 段の中の位置。幅優先へ並べるときに全体の位置へ直す
                parent.Count = childCount;
                parent.Flags = 0u;
                BvhBuildDetail::EnclosingSphereBuilder enclosing;
                float maxParentError = 0.0f;
                for (uint32_t child = first; child < first + childCount; ++child)
                {
                    enclosing.Add(BvhBuildDetail::ToSphereD(below[child]));
                    maxParentError = std::max(maxParentError, below[child].MaxParentError);
                }
                enclosing.WriteTo(parent);
                parent.MaxParentError = maxParentError;
                above.push_back(parent);
            }
            levels.push_back(std::move(above));
        }

        // 根の段から幅優先に並べる。内部の節の子は 1 つ下の段の連続した範囲なので、段の先頭の位置を足す
        size_t totalNodes = 0;
        for (const VariableArray<GPUGroupBVHNode> &level : levels)
        {
            totalNodes += level.size();
        }
        out.Nodes.reserve(totalNodes);
        for (size_t depth = 0; depth < levels.size(); ++depth)
        {
            const size_t levelIndex = levels.size() - 1 - depth;
            const uint32_t nextLevelBase = static_cast<uint32_t>(out.Nodes.size() + levels[levelIndex].size());
            for (GPUGroupBVHNode node : levels[levelIndex])
            {
                if ((node.Flags & GPU_GROUP_BVH_NODE_FLAG_LEAF) == 0u)
                {
                    node.First += nextLevelBase;
                }
                out.Nodes.push_back(node);
            }
            out.LevelNodeCounts.push_back(static_cast<uint32_t>(levels[levelIndex].size()));
        }
        out.LeafCount = static_cast<uint32_t>(levels.front().size());
        out.ClusterOrder = std::move(order);
        return true;
    }

    /**
     * @brief 作った BVH を検査し、破れた約束の数を返す（0 なら正しい）
     *
     * 構造（幅優先の並び・子の数と葉のクラスタの数の上限・全ての位置がちょうど 1 つの葉に入る）、並べ替えの表（全クラスタが
     * 1 回ずつ現れる）、節の球（葉は下のクラスタの球と親の球を、内部の節は子の節の球を包む）、親の誤差の最大（下の最大以上）を見る。
     */
    inline uint32_t ValidateClusterBVH(const VariableArray<MeshCluster> &clusters, const BuiltClusterBVH &bvh)
    {
        const uint32_t clusterCount = static_cast<uint32_t>(clusters.size());
        VariableArray<uint32_t> levelNodeCounts;
        uint32_t leafCount = 0;
        if (!AnalyzeGroupBVH(bvh.Nodes, clusterCount, levelNodeCounts, leafCount) ||
            bvh.ClusterOrder.size() != clusterCount)
        {
            // 構造が壊れていると、範囲を信じて下を調べられない
            return 1u;
        }

        uint32_t violations = 0;
        if (levelNodeCounts != bvh.LevelNodeCounts || leafCount != bvh.LeafCount)
        {
            ++violations;
        }

        VariableArray<uint8_t> seen(clusterCount, 0);
        for (const uint32_t clusterIndex : bvh.ClusterOrder)
        {
            if (clusterIndex >= clusterCount || seen[clusterIndex] != 0)
            {
                ++violations;
                continue;
            }
            seen[clusterIndex] = 1;
        }
        if (violations > 0)
        {
            return violations;
        }

        for (const GPUGroupBVHNode &node : bvh.Nodes)
        {
            const BvhBuildDetail::SphereD outer = BvhBuildDetail::ToSphereD(node);
            if ((node.Flags & GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0u)
            {
                for (uint32_t position = node.First; position < node.First + node.Count; ++position)
                {
                    const MeshCluster &cluster = clusters[bvh.ClusterOrder[position]];
                    if (!BvhBuildDetail::SphereContains(outer, BvhBuildDetail::ToSphereD(cluster.Bounds)) ||
                        (BvhBuildDetail::HasParentSphere(cluster) &&
                         !BvhBuildDetail::SphereContains(outer, BvhBuildDetail::ToSphereD(cluster.ParentBounds))) ||
                        !(node.MaxParentError >= cluster.ParentError))
                    {
                        ++violations;
                    }
                }
                continue;
            }
            for (uint32_t child = node.First; child < node.First + node.Count; ++child)
            {
                const GPUGroupBVHNode &childNode = bvh.Nodes[child];
                if (!BvhBuildDetail::SphereContains(outer, BvhBuildDetail::ToSphereD(childNode)) ||
                    !(node.MaxParentError >= childNode.MaxParentError))
                {
                    ++violations;
                }
            }
        }
        return violations;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
