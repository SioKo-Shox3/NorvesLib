#include "Rendering/MegaGeometryResourceStore.h"

#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "Rendering/MaterialTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        using LoadProfileClock = std::chrono::steady_clock;

        LoadProfileClock::time_point LoadProfileNow()
        {
            return LoadProfileClock::now();
        }

        double LoadProfileElapsedMs(LoadProfileClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(LoadProfileClock::now() - startTime).count();
        }
    }

    MegaGeometryResourceStore::MegaGeometryResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                                                         Thread::Atomic<uint64_t> &nextHandleId)
        : m_Device(std::move(device)),
          m_NextHandleId(nextHandleId)
    {
    }

    MegaGeometryResourceStore::~MegaGeometryResourceStore() = default;

    MegaGeometry::MegaMeshHandle MegaGeometryResourceStore::CreateMegaMesh(
        const MegaGeometry::MegaMeshCreateInfo &createInfo)
    {
        if (!m_Device || !createInfo.VertexData || !createInfo.IndexData)
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        if (createInfo.VertexDataSize == 0 || createInfo.IndexCount == 0 || createInfo.Clusters.empty())
        {
            NORVES_LOG_ERROR("MegaGeometryResources", "Invalid MegaMesh create info: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        float canonicalEmissiveColor[3] = {0.0f, 0.0f, 0.0f};
        float canonicalEmissiveLuminanceNits = 0.0f;
        if (!TryBuildCanonicalEmissive(createInfo.Material.EmissiveColor,
                                       createInfo.Material.EmissiveLuminanceNits,
                                       canonicalEmissiveColor,
                                       canonicalEmissiveLuminanceNits))
        {
            NORVES_LOG_ERROR("MegaGeometryResources", "Invalid MegaMesh emissive material: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        MegaGeometry::MegaMeshMaterial canonicalMaterial = createInfo.Material;
        canonicalMaterial.EmissiveColor[0] = canonicalEmissiveColor[0];
        canonicalMaterial.EmissiveColor[1] = canonicalEmissiveColor[1];
        canonicalMaterial.EmissiveColor[2] = canonicalEmissiveColor[2];
        canonicalMaterial.EmissiveLuminanceNits = canonicalEmissiveLuminanceNits;

        // Build an optional LOD hierarchy before GPU upload.
        const void *uploadVertexData = createInfo.VertexData;
        size_t uploadVertexDataSize = createInfo.VertexDataSize;
        const uint32_t *uploadIndexData = createInfo.IndexData;
        uint32_t uploadIndexCount = createInfo.IndexCount;
        uint32_t uploadVertexCount = createInfo.VertexCount;
        const Container::VariableArray<MegaGeometry::MeshCluster> *uploadClusters = &createInfo.Clusters;
        BoundingSphere uploadTotalBounds = createInfo.TotalBounds;

        MegaGeometry::LODHierarchy lodHierarchy;

        // 焼き込み済みの階層（NVMESH v1）は全段のクラスタが揃っているので、実行時には構築しない
        if (createInfo.bBakedLODHierarchy)
        {
            // クラスタの頂点・インデックスの範囲が、渡されたバッファの中に収まっていることを確かめる
            // （範囲外を読むクラスタをGPUへ渡さない）。親の誤差は自分の誤差以上で、値が有限でなければならない
            for (const MegaGeometry::MeshCluster &cluster : createInfo.Clusters)
            {
                const uint64_t indexEnd = static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount;
                const uint64_t vertexEnd = static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount;
                const bool bRoot = cluster.GroupId == MegaGeometry::INVALID_CLUSTER_GROUP_ID;
                const bool bErrorValid = std::isfinite(cluster.LODError) && cluster.LODError >= 0.0f &&
                                         (bRoot || (std::isfinite(cluster.ParentError) &&
                                                    cluster.ParentError >= cluster.LODError));
                if (cluster.VertexOffset < 0 || indexEnd > createInfo.IndexCount ||
                    vertexEnd > createInfo.VertexCount || !bErrorValid)
                {
                    NORVES_LOG_ERROR("MegaGeometryResources", "Invalid baked LOD cluster: %s",
                                     createInfo.DebugName.c_str());
                    return MegaGeometry::MegaMeshHandle::Invalid();
                }
            }
        }

        if (createInfo.bBuildLODHierarchy && !createInfo.bBakedLODHierarchy && createInfo.Clusters.size() > 1)
        {
            MegaGeometry::LODBuildSettings lodSettings;
            lodSettings.SimplificationRatio = createInfo.LODSimplificationRatio;
            lodSettings.MaxLODLevels = createInfo.MaxLODLevels;
            lodSettings.MinTrianglesForLOD = createInfo.MinTrianglesForLOD;

            lodHierarchy = MegaGeometry::LODHierarchyBuilder::Build(
                createInfo.VertexData,
                createInfo.VertexCount,
                createInfo.VertexStride,
                createInfo.IndexData,
                createInfo.IndexCount,
                lodSettings);

            if (!lodHierarchy.AllClusters.empty())
            {
                uploadVertexData = lodHierarchy.AllVertices.data();
                uploadVertexDataSize = lodHierarchy.AllVertices.size();
                uploadIndexData = lodHierarchy.AllIndices.data();
                uploadIndexCount = static_cast<uint32_t>(lodHierarchy.AllIndices.size());
                uploadVertexCount = lodHierarchy.TotalVertexCount;
                uploadClusters = &lodHierarchy.AllClusters;
                uploadTotalBounds = lodHierarchy.TotalBounds;

                NORVES_LOG_INFO("MegaGeometryResources",
                                "LOD hierarchy build succeeded: %s (%u levels, %u clusters)",
                                createInfo.DebugName.c_str(),
                                lodHierarchy.LODLevelCount,
                                static_cast<uint32_t>(lodHierarchy.AllClusters.size()));
            }
        }

        // Create the vertex buffer.
        double vertexUploadMs = 0.0;
        double indexUploadMs = 0.0;
        double clusterUploadMs = 0.0;
        Container::String vbName = createInfo.DebugName + "_VB";
        RHI::BufferDesc vbDesc(
            static_cast<uint64_t>(uploadVertexDataSize),
            RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::StorageBuffer,
            true,
            vbName.c_str());
        auto vertexUploadStartTime = LoadProfileNow();
        auto vertexBuffer = m_Device->CreateBuffer(vbDesc);
        if (!vertexBuffer)
        {
            vertexUploadMs = LoadProfileElapsedMs(vertexUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=0 cluster_bytes=0 vertex_ms=%.3f index_ms=0.000 cluster_ms=0.000 success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            vertexUploadMs);
            NORVES_LOG_ERROR("MegaGeometryResources", "Failed to create MegaMesh vertex buffer: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        vertexBuffer->Update(uploadVertexData, uploadVertexDataSize);
        vertexUploadMs = LoadProfileElapsedMs(vertexUploadStartTime);

        // Create the index buffer.
        size_t ibSize = static_cast<size_t>(uploadIndexCount) * sizeof(uint32_t);
        Container::String ibName = createInfo.DebugName + "_IB";
        RHI::BufferDesc ibDesc(
            static_cast<uint64_t>(ibSize),
            RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::StorageBuffer,
            true,
            ibName.c_str());
        auto indexUploadStartTime = LoadProfileNow();
        auto indexBuffer = m_Device->CreateBuffer(ibDesc);
        if (!indexBuffer)
        {
            indexUploadMs = LoadProfileElapsedMs(indexUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=0 vertex_ms=%.3f index_ms=%.3f cluster_ms=0.000 success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            ibSize,
                            vertexUploadMs,
                            indexUploadMs);
            NORVES_LOG_ERROR("MegaGeometryResources", "Failed to create MegaMesh index buffer: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        indexBuffer->Update(uploadIndexData, ibSize);
        indexUploadMs = LoadProfileElapsedMs(indexUploadStartTime);

        // Create the cluster data SSBO.
        // Convert MeshCluster to GPUClusterData.
        Container::VariableArray<MegaGeometry::GPUClusterData> gpuClusters;
        gpuClusters.reserve(uploadClusters->size());
        for (const auto &cluster : *uploadClusters)
        {
            MegaGeometry::GPUClusterData gpuCluster{};
            gpuCluster.BoundsCenterX = cluster.Bounds.CenterX;
            gpuCluster.BoundsCenterY = cluster.Bounds.CenterY;
            gpuCluster.BoundsCenterZ = cluster.Bounds.CenterZ;
            gpuCluster.BoundsRadius = cluster.Bounds.Radius;
            gpuCluster.ConeAxisX = cluster.ConeAxisX;
            gpuCluster.ConeAxisY = cluster.ConeAxisY;
            gpuCluster.ConeAxisZ = cluster.ConeAxisZ;
            gpuCluster.ConeCutoff = cluster.ConeCutoff;
            gpuCluster.IndexOffset = cluster.IndexOffset;
            gpuCluster.IndexCount = cluster.IndexCount;
            gpuCluster.VertexOffset = cluster.VertexOffset;
            gpuCluster.MaterialIndex = cluster.MaterialIndex;
            gpuCluster.LODLevel = cluster.LODLevel;
            gpuCluster.LODError = cluster.LODError;
            gpuCluster.ParentStart = cluster.ParentStart;
            gpuCluster.ParentCount = cluster.ParentCount;
            if (createInfo.bBakedLODHierarchy)
            {
                // 根は親のグループが無く、親の誤差は判定に使わない（シェーダーは GroupId で根を見分ける）
                gpuCluster.Flags = MegaGeometry::GPU_CLUSTER_FLAG_BAKED_LOD;
                gpuCluster.ParentCenterX = cluster.ParentBounds.CenterX;
                gpuCluster.ParentCenterY = cluster.ParentBounds.CenterY;
                gpuCluster.ParentCenterZ = cluster.ParentBounds.CenterZ;
                gpuCluster.ParentRadius = cluster.ParentBounds.Radius;
                gpuCluster.ParentError = cluster.ParentError;
                gpuCluster.GroupId = cluster.GroupId;
                gpuCluster.PageId = cluster.PageId;
            }
            gpuClusters.push_back(gpuCluster);
        }

        size_t clusterBufferSize = gpuClusters.size() * sizeof(MegaGeometry::GPUClusterData);
        Container::String cbName = createInfo.DebugName + "_ClusterSSBO";
        RHI::BufferDesc cbDesc(
            static_cast<uint64_t>(clusterBufferSize),
            RHI::ResourceUsage::StorageBuffer,
            true,
            cbName.c_str());
        auto clusterUploadStartTime = LoadProfileNow();
        auto clusterBuffer = m_Device->CreateBuffer(cbDesc);
        if (!clusterBuffer)
        {
            clusterUploadMs = LoadProfileElapsedMs(clusterUploadStartTime);
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=%zu vertex_ms=%.3f index_ms=%.3f cluster_ms=%.3f success=0",
                            createInfo.DebugName.c_str(),
                            uploadVertexDataSize,
                            ibSize,
                            clusterBufferSize,
                            vertexUploadMs,
                            indexUploadMs,
                            clusterUploadMs);
            NORVES_LOG_ERROR("MegaGeometryResources", "Failed to create MegaMesh cluster buffer: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        clusterBuffer->Update(gpuClusters.data(), clusterBufferSize);
        clusterUploadMs = LoadProfileElapsedMs(clusterUploadStartTime);

        // Allocate the handle and register GPU data.
        auto handle = AllocateHandle<MegaGeometry::MegaMeshHandle>();

        MegaGeometry::MegaMeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.ClusterBuffer = clusterBuffer;
        gpuData.VertexCount = uploadVertexCount;
        gpuData.IndexCount = uploadIndexCount;
        gpuData.ClusterCount = static_cast<uint32_t>(uploadClusters->size());
        gpuData.TotalBounds = uploadTotalBounds;
        gpuData.LODBounds = createInfo.LODBounds.IsValid() ? createInfo.LODBounds : BoundingSphere{};
        gpuData.Material = canonicalMaterial;
        gpuData.DebugName = createInfo.DebugName;
        // 影とレイトレーシングに使う段（既定はLOD0）のクラスタが、頂点の基点が全て0で統合インデックスの
        // 中に隙間なく並んでいれば、その範囲を1回の描画で描ける（CSM・点光源の影・光線で使う）。
        const uint32_t shadowLODLevel = createInfo.ShadowLODLevel;
        uint64_t shadowIndexBegin = UINT64_MAX;
        uint64_t shadowIndexEnd = 0;
        uint64_t shadowIndexSum = 0;
        bool bShadowLevelDrawableAsOneRange = true;
        for (const auto &cluster : *uploadClusters)
        {
            if (cluster.LODLevel != shadowLODLevel)
            {
                continue;
            }
            if (cluster.VertexOffset != 0)
            {
                bShadowLevelDrawableAsOneRange = false;
                break;
            }
            shadowIndexBegin = std::min<uint64_t>(shadowIndexBegin, cluster.IndexOffset);
            shadowIndexEnd = std::max<uint64_t>(shadowIndexEnd,
                                                static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount);
            shadowIndexSum += cluster.IndexCount;
        }
        const bool bShadowRangeValid = bShadowLevelDrawableAsOneRange && shadowIndexEnd > 0u &&
                                       shadowIndexBegin < shadowIndexEnd &&
                                       shadowIndexEnd - shadowIndexBegin == shadowIndexSum &&
                                       shadowIndexEnd <= uploadIndexCount;
        gpuData.ShadowFirstIndex = bShadowRangeValid ? static_cast<uint32_t>(shadowIndexBegin) : 0u;
        gpuData.ShadowIndexCount =
            bShadowRangeValid ? static_cast<uint32_t>(shadowIndexEnd - shadowIndexBegin) : 0u;
        gpuData.ShadowLODLevel = shadowLODLevel;

        // 段ごとの範囲と誤差（影へカスケード・光源からの距離に見合う段を選ぶため）。段の範囲の決め方は
        // 影の段と同じで、1回の範囲で描けない段は IndexCount を0にする。
        {
            uint32_t maxLevel = 0;
            for (const auto &cluster : *uploadClusters)
            {
                maxLevel = std::max(maxLevel, cluster.LODLevel);
            }
            struct LevelAccumulator
            {
                uint64_t Begin = UINT64_MAX;
                uint64_t End = 0;
                uint64_t Sum = 0;
                bool bDrawableAsOneRange = true;
            };
            Container::VariableArray<LevelAccumulator> accumulators(static_cast<size_t>(maxLevel) + 1u);
            gpuData.LevelRanges.resize(static_cast<size_t>(maxLevel) + 1u);
            for (const auto &cluster : *uploadClusters)
            {
                LevelAccumulator &accumulator = accumulators[cluster.LODLevel];
                MegaGeometry::MegaMeshLevelRange &range = gpuData.LevelRanges[cluster.LODLevel];
                range.Error = std::max(range.Error, cluster.LODError);
                if (cluster.VertexOffset != 0)
                {
                    accumulator.bDrawableAsOneRange = false;
                    continue;
                }
                accumulator.Begin = std::min<uint64_t>(accumulator.Begin, cluster.IndexOffset);
                accumulator.End = std::max<uint64_t>(accumulator.End,
                                                     static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount);
                accumulator.Sum += cluster.IndexCount;
            }
            for (size_t level = 0; level < accumulators.size(); ++level)
            {
                const LevelAccumulator &accumulator = accumulators[level];
                MegaGeometry::MegaMeshLevelRange &range = gpuData.LevelRanges[level];
                const bool bValid = accumulator.bDrawableAsOneRange && accumulator.End > 0u &&
                                    accumulator.Begin < accumulator.End &&
                                    accumulator.End - accumulator.Begin == accumulator.Sum &&
                                    accumulator.End <= uploadIndexCount;
                range.FirstIndex = bValid ? static_cast<uint32_t>(accumulator.Begin) : 0u;
                range.IndexCount = bValid ? static_cast<uint32_t>(accumulator.End - accumulator.Begin) : 0u;
            }
        }

        // クラスタの大きさ（1クラスタあたりの三角形数）を段ごとに記録する。極端に小さいと
        // カリングと間接描画の1件あたりの手間に対して描く量が少なくなる。
        {
            uint32_t maxLevel = 0;
            for (const auto &cluster : *uploadClusters)
            {
                maxLevel = std::max(maxLevel, cluster.LODLevel);
            }
            uint64_t lod0Clusters = 0;
            uint64_t lod0Triangles = 0;
            uint64_t lod0SmallClusters = 0;
            uint64_t allTriangles = 0;
            for (const auto &cluster : *uploadClusters)
            {
                const uint64_t triangles = cluster.IndexCount / 3u;
                allTriangles += triangles;
                if (cluster.LODLevel == 0u)
                {
                    ++lod0Clusters;
                    lod0Triangles += triangles;
                    if (triangles < 16u)
                    {
                        ++lod0SmallClusters;
                    }
                }
            }
            NORVES_LOG_INFO("MegaGeometryResources",
                            "stage=megamesh_cluster_stats debug_name=\"%s\" lod_levels=%u clusters=%u triangles=%llu "
                            "lod0_clusters=%llu lod0_triangles=%llu lod0_avg_triangles_per_cluster=%.2f "
                            "lod0_clusters_under16=%llu shadow_lod=%u shadow_triangles=%u uniform_lod=%d baked_lod=%d groups=%u",
                            createInfo.DebugName.c_str(),
                            maxLevel + 1u,
                            static_cast<uint32_t>(uploadClusters->size()),
                            static_cast<unsigned long long>(allTriangles),
                            static_cast<unsigned long long>(lod0Clusters),
                            static_cast<unsigned long long>(lod0Triangles),
                            lod0Clusters > 0u ? static_cast<double>(lod0Triangles) / static_cast<double>(lod0Clusters)
                                              : 0.0,
                            static_cast<unsigned long long>(lod0SmallClusters),
                            shadowLODLevel,
                            gpuData.ShadowIndexCount / 3u,
                            gpuData.LODBounds.IsValid() ? 1 : 0,
                            createInfo.bBakedLODHierarchy ? 1 : 0,
                            static_cast<uint32_t>(createInfo.ClusterGroups.size()));
        }

        {
            Thread::ScopedLock lock(m_Mutex);
            m_MegaMeshes[handle.Id] = std::move(gpuData);
        }

        NORVES_LOG_INFO("MegaGeometryResources",
                        "MegaMesh created: %s (vertices: %u, indices: %u, clusters: %u)",
                        createInfo.DebugName.c_str(),
                        createInfo.VertexCount,
                        createInfo.IndexCount,
                        static_cast<uint32_t>(createInfo.Clusters.size()));

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%zu index_bytes=%zu cluster_bytes=%zu vertex_ms=%.3f index_ms=%.3f cluster_ms=%.3f success=1",
                        createInfo.DebugName.c_str(),
                        uploadVertexDataSize,
                        ibSize,
                        clusterBufferSize,
                        vertexUploadMs,
                        indexUploadMs,
                        clusterUploadMs);

        return handle;
    }

    const MegaGeometry::MegaMeshGPUData *MegaGeometryResourceStore::GetMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_MegaMeshes.find(handle.Id);
        if (it == m_MegaMeshes.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    void MegaGeometryResourceStore::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        Thread::ScopedLock lock(m_Mutex);
        m_MegaMeshes.erase(handle.Id);
    }

    ModelHandle MegaGeometryResourceStore::RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                                         const Container::String &debugName,
                                                         const Container::String &sourcePath)
    {
        if (!megaMeshHandle.IsValid())
        {
            return ModelHandle::Invalid();
        }

        auto handle = AllocateHandle<ModelHandle>();

        ModelResourceData modelData;
        modelData.MegaMesh = megaMeshHandle;
        modelData.DebugName = debugName;
        modelData.SourcePath = sourcePath;

        Thread::ScopedLock lock(m_Mutex);
        m_Models[handle.Id] = std::move(modelData);
        return handle;
    }

    MegaGeometry::MegaMeshHandle MegaGeometryResourceStore::GetModelMegaMeshHandle(ModelHandle handle) const
    {
        if (!handle.IsValid())
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Models.find(handle.Id);
        if (it == m_Models.end())
        {
            return MegaGeometry::MegaMeshHandle::Invalid();
        }

        return it->second.MegaMesh;
    }

    void MegaGeometryResourceStore::ReleaseModel(ModelHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Models.find(handle.Id);
        if (it == m_Models.end())
        {
            return;
        }

        MegaGeometry::MegaMeshHandle megaMeshHandle = it->second.MegaMesh;
        m_Models.erase(it);

        if (megaMeshHandle.IsValid())
        {
            m_MegaMeshes.erase(megaMeshHandle.Id);
        }
    }

    void MegaGeometryResourceStore::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Models.clear();
        m_MegaMeshes.clear();
    }

} // namespace NorvesLib::Core::Rendering
