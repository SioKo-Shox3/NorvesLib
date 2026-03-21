#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "Rendering/MegaGeometry/MeshClusterizer.h"
#include "Rendering/MegaGeometry/ClusterGroupBuilder.h"
#include "Rendering/MegaGeometry/MeshSimplifier.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    namespace
    {
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

            if (dist + b.Radius <= a.Radius)
            {
                return a;
            }
            if (dist + a.Radius <= b.Radius)
            {
                return b;
            }

            float newRadius = (dist + a.Radius + b.Radius) * 0.5f;
            float t = (newRadius - a.Radius) / dist;

            BoundingSphere result;
            result.CenterX = a.CenterX + dx * t;
            result.CenterY = a.CenterY + dy * t;
            result.CenterZ = a.CenterZ + dz * t;
            result.Radius = newRadius;
            return result;
        }

        /**
         * @brief グループ内クラスタの統合三角形数を計算
         */
        uint32_t GetGroupTriangleCount(
            const VariableArray<MeshCluster> &allClusters,
            const ClusterGroup &group)
        {
            uint32_t total = 0;
            for (uint32_t idx : group.ClusterIndices)
            {
                total += allClusters[idx].IndexCount / 3;
            }
            return total;
        }

        /**
         * @brief グループ内クラスタの頂点・インデックスを統合して1つのメッシュに戻す
         *
         * 各クラスタのインデックスはグローバルインデックスバッファを参照している。
         * 統合後のメッシュデータを返す。
         */
        struct MergedGroupMesh
        {
            VariableArray<uint8_t> VertexData;
            VariableArray<uint32_t> IndexData;
            uint32_t VertexCount = 0;
        };

        MergedGroupMesh MergeGroupClusters(
            const VariableArray<MeshCluster> &allClusters,
            const VariableArray<uint32_t> &allIndices,
            const VariableArray<uint8_t> &allVertices,
            uint32_t vertexStride,
            const ClusterGroup &group)
        {
            MergedGroupMesh merged;

            // このグループが参照する頂点インデックスを収集し、リマップ
            UnorderedMap<uint32_t, uint32_t> vertexRemap;

            for (uint32_t clusterIdx : group.ClusterIndices)
            {
                const auto &cluster = allClusters[clusterIdx];

                for (uint32_t i = 0; i < cluster.IndexCount; ++i)
                {
                    uint32_t globalVertIdx = allIndices[cluster.IndexOffset + i];
                    uint32_t actualVertIdx = static_cast<uint32_t>(
                        static_cast<int32_t>(globalVertIdx) + cluster.VertexOffset);

                    if (vertexRemap.find(actualVertIdx) == vertexRemap.end())
                    {
                        uint32_t newIdx = merged.VertexCount++;
                        vertexRemap[actualVertIdx] = newIdx;

                        // 頂点データをコピー
                        size_t srcOffset = static_cast<size_t>(actualVertIdx) * vertexStride;
                        size_t currentSize = merged.VertexData.size();
                        merged.VertexData.resize(currentSize + vertexStride);

                        if (srcOffset + vertexStride <= allVertices.size())
                        {
                            std::memcpy(
                                merged.VertexData.data() + currentSize,
                                allVertices.data() + srcOffset,
                                vertexStride);
                        }
                    }

                    merged.IndexData.push_back(vertexRemap[actualVertIdx]);
                }
            }

            return merged;
        }

    } // anonymous namespace

    // ========================================
    // Build
    // ========================================

    LODHierarchy LODHierarchyBuilder::Build(
        const void *vertexData,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const uint32_t *indexData,
        uint32_t indexCount,
        const LODBuildSettings &settings)
    {
        LODHierarchy hierarchy;
        hierarchy.VertexStride = vertexStride;

        if (!vertexData || !indexData || indexCount < 3 || vertexCount < 3)
        {
            return hierarchy;
        }

        // ========================================
        // LOD 0: 入力メッシュをクラスタ化
        // ========================================

        VariableArray<MeshCluster> lod0Clusters;
        VariableArray<uint32_t> lod0Indices;

        MeshClusterizer::Clusterize(
            vertexData, vertexCount, vertexStride,
            indexData, indexCount,
            lod0Clusters, lod0Indices);

        if (lod0Clusters.empty())
        {
            return hierarchy;
        }

        // LOD 0のクラスタ情報を設定
        for (auto &cluster : lod0Clusters)
        {
            cluster.LODLevel = 0;
            cluster.LODError = 0.0f;
        }

        // グローバル配列にLOD 0を追加
        uint32_t baseClusterIndex = 0;
        hierarchy.AllClusters.insert(
            hierarchy.AllClusters.end(),
            lod0Clusters.begin(), lod0Clusters.end());
        hierarchy.AllIndices = lod0Indices;

        // 頂点データをコピー
        size_t vertexDataSize = static_cast<size_t>(vertexCount) * vertexStride;
        hierarchy.AllVertices.resize(vertexDataSize);
        std::memcpy(hierarchy.AllVertices.data(), vertexData, vertexDataSize);
        hierarchy.TotalVertexCount = vertexCount;

        // バウンディング
        for (const auto &cluster : lod0Clusters)
        {
            hierarchy.TotalBounds = MergeBounds(hierarchy.TotalBounds, cluster.Bounds);
        }

        hierarchy.LODLevelCount = 1;

        NORVES_LOG_INFO("LODHierarchyBuilder",
                        "LOD 0: %u クラスタ, %u 三角形",
                        static_cast<uint32_t>(lod0Clusters.size()),
                        indexCount / 3);

        // ========================================
        // LOD N+1 の反復構築
        // ========================================

        uint32_t currentLODLevel = 0;
        uint32_t currentLevelStart = baseClusterIndex;
        uint32_t currentLevelCount = static_cast<uint32_t>(lod0Clusters.size());

        while (currentLODLevel + 1 < settings.MaxLODLevels)
        {
            // 現在のレベルの総三角形数を確認
            uint32_t currentTriangles = 0;
            for (uint32_t i = 0; i < currentLevelCount; ++i)
            {
                currentTriangles += hierarchy.AllClusters[currentLevelStart + i].IndexCount / 3;
            }

            if (currentTriangles <= settings.MinTrianglesForLOD)
            {
                NORVES_LOG_DEBUG("LODHierarchyBuilder",
                                 "LOD %u で終了: 三角形数 %u <= 閾値 %u",
                                 currentLODLevel, currentTriangles, settings.MinTrianglesForLOD);
                break;
            }

            // 1. クラスタをグループ化
            VariableArray<ClusterGroup> groups = ClusterGroupBuilder::BuildGroups(
                hierarchy.AllClusters, currentLevelStart, currentLevelCount);

            if (groups.empty())
            {
                break;
            }

            uint32_t nextLODLevel = currentLODLevel + 1;
            uint32_t nextLevelStart = static_cast<uint32_t>(hierarchy.AllClusters.size());
            uint32_t nextLevelClusterCount = 0;

            // 2. 各グループを簡略化 → 再クラスタ化
            for (const auto &group : groups)
            {
                uint32_t groupTriangles = GetGroupTriangleCount(hierarchy.AllClusters, group);
                uint32_t targetTriangles = static_cast<uint32_t>(
                    static_cast<float>(groupTriangles) * settings.SimplificationRatio);

                if (targetTriangles < 1)
                {
                    targetTriangles = 1;
                }

                // グループ内メッシュを統合
                MergedGroupMesh merged = MergeGroupClusters(
                    hierarchy.AllClusters,
                    hierarchy.AllIndices,
                    hierarchy.AllVertices,
                    vertexStride,
                    group);

                if (merged.IndexData.empty())
                {
                    continue;
                }

                // 簡略化
                SimplificationResult simplified = MeshSimplifier::Simplify(
                    merged.VertexData.data(),
                    merged.VertexCount,
                    vertexStride,
                    merged.IndexData.data(),
                    static_cast<uint32_t>(merged.IndexData.size()),
                    targetTriangles);

                if (simplified.IndexCount < 3)
                {
                    continue;
                }

                // 再クラスタ化
                VariableArray<MeshCluster> newClusters;
                VariableArray<uint32_t> newIndices;

                MeshClusterizer::Clusterize(
                    simplified.VertexData.data(),
                    simplified.VertexCount,
                    vertexStride,
                    simplified.IndexData.data(),
                    simplified.IndexCount,
                    newClusters, newIndices);

                if (newClusters.empty())
                {
                    continue;
                }

                // 頂点をグローバル配列に追加
                uint32_t vertexBaseOffset = hierarchy.TotalVertexCount;
                size_t vertexAppendSize = static_cast<size_t>(simplified.VertexCount) * vertexStride;
                hierarchy.AllVertices.insert(
                    hierarchy.AllVertices.end(),
                    simplified.VertexData.begin(),
                    simplified.VertexData.begin() + vertexAppendSize);
                hierarchy.TotalVertexCount += simplified.VertexCount;

                // インデックスをグローバル配列に追加し、クラスタ情報を更新
                uint32_t indexBaseOffset = static_cast<uint32_t>(hierarchy.AllIndices.size());

                for (auto &newCluster : newClusters)
                {
                    // インデックスオフセットをグローバルに変換
                    uint32_t localIndexStart = newCluster.IndexOffset;
                    newCluster.IndexOffset = indexBaseOffset + localIndexStart;
                    newCluster.VertexOffset = static_cast<int32_t>(vertexBaseOffset);

                    newCluster.LODLevel = nextLODLevel;
                    newCluster.LODError = simplified.MaxError;

                    // 子クラスタ（現LODレベル）の ParentStart/Count を設定
                    uint32_t parentClusterGlobalIdx =
                        static_cast<uint32_t>(hierarchy.AllClusters.size()) + nextLevelClusterCount;

                    for (uint32_t childIdx : group.ClusterIndices)
                    {
                        auto &childCluster = hierarchy.AllClusters[childIdx];
                        if (childCluster.ParentCount == 0)
                        {
                            childCluster.ParentStart = parentClusterGlobalIdx;
                        }
                        childCluster.ParentCount += static_cast<uint32_t>(newClusters.size());
                    }
                }

                // 新しいインデックスをグローバル配列に追加
                hierarchy.AllIndices.insert(
                    hierarchy.AllIndices.end(),
                    newIndices.begin(), newIndices.end());

                // 新しいクラスタをグローバル配列に追加
                nextLevelClusterCount += static_cast<uint32_t>(newClusters.size());
                hierarchy.AllClusters.insert(
                    hierarchy.AllClusters.end(),
                    newClusters.begin(), newClusters.end());
            }

            if (nextLevelClusterCount == 0)
            {
                break;
            }

            NORVES_LOG_INFO("LODHierarchyBuilder",
                            "LOD %u: %u クラスタ",
                            nextLODLevel, nextLevelClusterCount);

            ++hierarchy.LODLevelCount;
            currentLODLevel = nextLODLevel;
            currentLevelStart = nextLevelStart;
            currentLevelCount = nextLevelClusterCount;
        }

        NORVES_LOG_INFO("LODHierarchyBuilder",
                        "LOD階層構築完了: %u レベル, %u 全クラスタ, %u 全頂点",
                        hierarchy.LODLevelCount,
                        static_cast<uint32_t>(hierarchy.AllClusters.size()),
                        hierarchy.TotalVertexCount);

        return hierarchy;
    }

} // namespace NorvesLib::Core::Rendering::MegaGeometry
