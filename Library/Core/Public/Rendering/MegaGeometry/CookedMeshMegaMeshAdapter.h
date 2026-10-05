#pragma once

// クック済みメッシュ（NVMESH v0・v1）を MegaGeometry の MegaMeshCreateInfo へ渡す形に直す。
// Asset 層は Rendering を include しないので、変換は Rendering 側のこのヘッダに置く（Rendering -> Asset の向き）。

#include "Asset/CookedMeshFormat.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    namespace CookedMeshAdapterDetail
    {
        // 頂点は cooked の配列をそのまま渡す（位置・法線・UV の float 8個。Mesh3DVertex と同じ並び）
        static_assert(sizeof(Asset::CookedMeshVertex) == 32);
        static_assert(std::is_standard_layout_v<Asset::CookedMeshVertex>);
        static_assert(offsetof(Asset::CookedMeshVertex, Position) == 0);
        static_assert(offsetof(Asset::CookedMeshVertex, Normal) == 12);
        static_assert(offsetof(Asset::CookedMeshVertex, TexCoord) == 24);

        inline BoundingSphere ToSphere(const Asset::CookedMeshFloat3 &center, float radius)
        {
            BoundingSphere sphere;
            sphere.CenterX = center.X;
            sphere.CenterY = center.Y;
            sphere.CenterZ = center.Z;
            sphere.Radius = radius;
            return sphere;
        }
    } // namespace CookedMeshAdapterDetail

    /**
     * @brief クック済みメッシュから MegaMeshCreateInfo を作る
     *
     * VertexData・IndexData は cooked の配列を指すので、cooked は CreateMegaMesh が終わるまで生かしておく。
     * v0 は従来の1段のメッシュ（bBakedLODHierarchy=false、フォールバック無し）。
     * v1 は全段のクラスタとグループの表、フォールバックの範囲を渡す（bBuildLODHierarchy は false のまま）。
     * 材質（テクスチャ）は含まない。呼び出し側が Material へ入れる。
     *
     * @return 頂点・インデックス・クラスタのどれかが空なら false
     */
    inline bool BuildMegaMeshCreateInfoFromCookedMesh(const Asset::CookedMeshData &cooked,
                                                      MegaMeshCreateInfo &outCreateInfo)
    {
        using CookedMeshAdapterDetail::ToSphere;

        if (cooked.Vertices.empty() || cooked.Indices.empty() || cooked.Clusters.empty())
        {
            return false;
        }

        outCreateInfo.VertexData = cooked.Vertices.data();
        outCreateInfo.VertexDataSize = cooked.Vertices.size() * sizeof(Asset::CookedMeshVertex);
        outCreateInfo.VertexCount = static_cast<uint32_t>(cooked.Vertices.size());
        outCreateInfo.VertexStride = static_cast<uint32_t>(sizeof(Asset::CookedMeshVertex));
        outCreateInfo.IndexData = cooked.Indices.data();
        outCreateInfo.IndexCount = static_cast<uint32_t>(cooked.Indices.size());
        outCreateInfo.TotalBounds = ToSphere(cooked.TotalBoundsCenter, cooked.TotalBoundsRadius);
        outCreateInfo.bBuildLODHierarchy = false;

        outCreateInfo.Clusters.clear();
        outCreateInfo.Clusters.reserve(cooked.Clusters.size());
        for (const Asset::CookedMeshCluster &cookedCluster : cooked.Clusters)
        {
            MeshCluster cluster;
            cluster.IndexOffset = cookedCluster.IndexOffset;
            cluster.IndexCount = cookedCluster.IndexCount;
            cluster.VertexOffset = static_cast<int32_t>(cookedCluster.VertexOffset);
            cluster.VertexCount = cookedCluster.VertexCount;
            cluster.Bounds = ToSphere(cookedCluster.BoundsCenter, cookedCluster.BoundsRadius);
            cluster.ConeAxisX = cookedCluster.ConeAxis.X;
            cluster.ConeAxisY = cookedCluster.ConeAxis.Y;
            cluster.ConeAxisZ = cookedCluster.ConeAxis.Z;
            cluster.ConeCutoff = cookedCluster.ConeCutoff;
            cluster.LODLevel = cookedCluster.LODLevel;
            cluster.LODError = cookedCluster.LODError;
            cluster.ParentStart = cookedCluster.ParentStart;
            cluster.ParentCount = cookedCluster.ParentCount;
            cluster.MaterialIndex = cookedCluster.MaterialIndex;
            if (cooked.FormatMajor >= 1)
            {
                cluster.ParentBounds = ToSphere(cookedCluster.ParentBoundsCenter, cookedCluster.ParentBoundsRadius);
                cluster.ParentError = cookedCluster.ParentError;
                cluster.GroupId = cookedCluster.GroupId;
                cluster.PageId = cookedCluster.PageId;
                cluster.SourceGroupId = cookedCluster.SourceGroupId;
            }
            outCreateInfo.Clusters.push_back(cluster);
        }

        outCreateInfo.ClusterGroups.clear();
        outCreateInfo.GroupBVH.clear();
        outCreateInfo.Pages.clear();
        outCreateInfo.PageSource = nullptr;
        outCreateInfo.bBakedLODHierarchy = cooked.FormatMajor >= 1;
        outCreateInfo.BakedLODLevelCount = cooked.LODLevelCount;
        outCreateInfo.FallbackIndexOffset = 0;
        outCreateInfo.FallbackIndexCount = 0;
        outCreateInfo.FallbackError = 0.0f;
        if (cooked.FormatMajor >= 1)
        {
            outCreateInfo.ClusterGroups.reserve(cooked.Groups.size());
            for (const Asset::CookedMeshClusterGroup &cookedGroup : cooked.Groups)
            {
                MeshClusterGroup group;
                group.Bounds = ToSphere(cookedGroup.BoundsCenter, cookedGroup.BoundsRadius);
                group.Error = cookedGroup.Error;
                group.ClusterOffset = cookedGroup.ClusterOffset;
                group.ClusterCount = cookedGroup.ClusterCount;
                group.LODLevel = cookedGroup.LODLevel;
                outCreateInfo.ClusterGroups.push_back(group);
            }
            outCreateInfo.FallbackIndexOffset = cooked.FallbackIndexOffset;
            outCreateInfo.FallbackIndexCount = cooked.FallbackIndexCount;
            outCreateInfo.FallbackError = cooked.FallbackError;

            // v1.1 のグループの BVH（v1.0 は空。空なら GPU は平らなクラスタの列を判定する）
            outCreateInfo.GroupBVH.reserve(cooked.GroupBVH.size());
            for (const Asset::CookedMeshGroupBVHNode &cookedNode : cooked.GroupBVH)
            {
                GPUGroupBVHNode node{};
                node.BoundsCenterX = cookedNode.BoundsCenter.X;
                node.BoundsCenterY = cookedNode.BoundsCenter.Y;
                node.BoundsCenterZ = cookedNode.BoundsCenter.Z;
                node.BoundsRadius = cookedNode.BoundsRadius;
                node.MaxParentError = cookedNode.MaxParentError;
                node.First = cookedNode.First;
                node.Count = cookedNode.Count;
                node.Flags = cookedNode.bLeaf ? GPU_GROUP_BVH_NODE_FLAG_LEAF : 0u;
                outCreateInfo.GroupBVH.push_back(node);
            }

            // v1.1 のページの範囲（ページが 2 つ以上あるときだけ。読み込み元 PageSource は呼び出し側が足す）
            if (cooked.Pages.size() >= 2)
            {
                outCreateInfo.Pages.reserve(cooked.Pages.size());
                for (const Asset::CookedMeshPage &cookedPage : cooked.Pages)
                {
                    MeshPageInfo page;
                    page.bRoot = cookedPage.bIsRoot;
                    page.FirstCluster = cookedPage.FirstClusterIndex;
                    page.ClusterCount = cookedPage.ClusterCount;
                    page.FirstVertex = cookedPage.FirstVertex;
                    page.VertexCount = cookedPage.VertexCount;
                    page.FirstIndex = cookedPage.FirstIndex;
                    page.IndexCount = cookedPage.IndexCount;
                    outCreateInfo.Pages.push_back(page);
                }
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering::MegaGeometry
