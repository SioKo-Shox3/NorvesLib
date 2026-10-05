#include "Rendering/MegaGeometryResourceStore.h"

#include "Rendering/GpuRetireQueue.h"
#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/TileUploader.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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

        // 1回にリングへ積むチャンクの大きさ。リング（32 MiB）とフレームのコピー量の上限より十分小さくする
        constexpr uint64_t UploadChunkBytes = 4ull * 1024ull * 1024ull;
        // 区画の中の各領域の整列。storage buffer の範囲のオフセット（minStorageBufferOffsetAlignment の上限 256）を満たす
        constexpr uint64_t RegionAlignmentBytes = GeometryPool::DefaultAlignmentBytes;

        constexpr uint64_t AlignUpBytes(uint64_t value, uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }
    }

    MegaGeometryResourceStore::MegaGeometryResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                                                         Thread::Atomic<uint64_t> &nextHandleId,
                                                         GeometryPool *pool,
                                                         TileUploader *uploader,
                                                         GpuRetireQueue *retireQueue)
        : m_Device(std::move(device)),
          m_NextHandleId(nextHandleId),
          m_Pool(pool),
          m_Uploader(uploader),
          m_RetireQueue(retireQueue)
    {
    }

    MegaGeometryResourceStore::~MegaGeometryResourceStore() = default;

    MegaGeometry::MegaMeshHandle MegaGeometryResourceStore::CreateMegaMesh(
        const MegaGeometry::MegaMeshCreateInfo &createInfo)
    {
        if (!m_Device || !m_Pool || !m_Uploader || !createInfo.VertexData || !createInfo.IndexData)
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
                    NORVES_LOG_ERROR("MegaGeometryResources", "焼き込み済みLOD階層のクラスタが不正です: %s",
                                     createInfo.DebugName.c_str());
                    return MegaGeometry::MegaMeshHandle::Invalid();
                }
            }

            // フォールバックの段は、影とレイトレーシングが1回の範囲で描くので、範囲が三角形の単位で
            // インデックスの中に収まり、全ての頂点の番号が頂点の数より小さいことを確かめる。
            if (createInfo.FallbackIndexCount > 0u)
            {
                const uint64_t fallbackEnd =
                    static_cast<uint64_t>(createInfo.FallbackIndexOffset) + createInfo.FallbackIndexCount;
                bool bFallbackValid = createInfo.IndexData != nullptr && createInfo.FallbackIndexCount % 3u == 0u &&
                                      createInfo.FallbackIndexOffset % 3u == 0u && fallbackEnd <= createInfo.IndexCount &&
                                      std::isfinite(createInfo.FallbackError) && createInfo.FallbackError >= 0.0f;
                for (uint32_t i = 0; bFallbackValid && i < createInfo.FallbackIndexCount; ++i)
                {
                    bFallbackValid = createInfo.IndexData[createInfo.FallbackIndexOffset + i] < createInfo.VertexCount;
                }
                if (!bFallbackValid)
                {
                    NORVES_LOG_ERROR("MegaGeometryResources", "焼き込み済みLOD階層のフォールバックの段が不正です: %s",
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

        // 区画の大きさを決める。クラスタ・頂点・インデックスの順に 256 バイト整列で1つの区画へ並べる
        // （クラスタは storage buffer の範囲として結ぶので、区画の先頭＝プールの整列の位置に置く）。
        const uint64_t vertexBytes = static_cast<uint64_t>(uploadVertexDataSize);
        const uint64_t indexBytes = static_cast<uint64_t>(uploadIndexCount) * sizeof(uint32_t);
        const uint64_t clusterBytes =
            static_cast<uint64_t>(uploadClusters->size()) * sizeof(MegaGeometry::GPUClusterData);
        const uint64_t clusterRegionOffset = 0;
        const uint64_t vertexRegionOffset = AlignUpBytes(clusterBytes, RegionAlignmentBytes);
        const uint64_t indexRegionOffset = AlignUpBytes(vertexRegionOffset + vertexBytes, RegionAlignmentBytes);
        const uint64_t regionBytes = indexRegionOffset + indexBytes;

        auto stageStartTime = LoadProfileNow();
        GeometryPool::RegionLease lease = m_Pool->Allocate(regionBytes, RegionAlignmentBytes);
        if (!lease.IsValid())
        {
            NORVES_LOG_INFO("AssetLoadProfile",
                            "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%llu index_bytes=%llu cluster_bytes=%llu vertex_ms=0.000 index_ms=0.000 cluster_ms=0.000 success=0",
                            createInfo.DebugName.c_str(),
                            static_cast<unsigned long long>(vertexBytes),
                            static_cast<unsigned long long>(indexBytes),
                            static_cast<unsigned long long>(clusterBytes));
            NORVES_LOG_ERROR("MegaGeometryResources", "Failed to allocate MegaMesh region in the geometry pool: %s (%llu bytes)",
                             createInfo.DebugName.c_str(),
                             static_cast<unsigned long long>(regionBytes));
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        // クラスタの storage buffer の範囲のオフセットは 32 ビット（ディスクリプタの更新の引数）に収める
        if (lease.GetOffsetBytes() + clusterRegionOffset + clusterBytes > UINT32_MAX)
        {
            NORVES_LOG_ERROR("MegaGeometryResources", "MegaMesh cluster region is beyond the 32-bit offset range: %s",
                             createInfo.DebugName.c_str());
            return MegaGeometry::MegaMeshHandle::Invalid();
        }
        const double allocateMs = LoadProfileElapsedMs(stageStartTime);

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

        // 区画の中身を CPU 側に組み立てる（リングへ積むまで持つ。呼び出し側の頂点・インデックスは返った後に手放される）
        Container::VariableArray<uint8_t> stagedBytes(static_cast<size_t>(regionBytes));
        auto clusterStageStartTime = LoadProfileNow();
        std::memcpy(stagedBytes.data() + clusterRegionOffset, gpuClusters.data(), static_cast<size_t>(clusterBytes));
        const double clusterUploadMs = LoadProfileElapsedMs(clusterStageStartTime);
        auto vertexStageStartTime = LoadProfileNow();
        std::memcpy(stagedBytes.data() + vertexRegionOffset, uploadVertexData, static_cast<size_t>(vertexBytes));
        const double vertexUploadMs = LoadProfileElapsedMs(vertexStageStartTime) + allocateMs;
        auto indexStageStartTime = LoadProfileNow();
        std::memcpy(stagedBytes.data() + indexRegionOffset, uploadIndexData, static_cast<size_t>(indexBytes));
        const double indexUploadMs = LoadProfileElapsedMs(indexStageStartTime);

        // Allocate the handle and register GPU data.
        auto handle = AllocateHandle<MegaGeometry::MegaMeshHandle>();

        MegaGeometry::MegaMeshGPUData gpuData;
        gpuData.VertexBuffer = lease.GetBufferHandle();
        gpuData.IndexBuffer = lease.GetBufferHandle();
        gpuData.ClusterBuffer = lease.GetBufferHandle();
        gpuData.VertexBufferOffsetBytes = lease.GetOffsetBytes() + vertexRegionOffset;
        gpuData.IndexBufferOffsetBytes = lease.GetOffsetBytes() + indexRegionOffset;
        gpuData.ClusterBufferOffsetBytes = lease.GetOffsetBytes() + clusterRegionOffset;
        gpuData.VertexBufferBytes = vertexBytes;
        gpuData.IndexBufferBytes = indexBytes;
        gpuData.ClusterBufferBytes = clusterBytes;
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

        // 焼き込み済みの階層は、段のクラスタが全体の頂点の中に散らばるので1回の範囲では描けない。代わりに
        // 常駐のフォールバックの段（頂点の基点が0の1つの範囲）を最も粗い段として足し、影とレイトレーシングは
        // その範囲を使う（BLAS のキーは同じバッファの範囲なので、別のバッファは要らない）。
        if (createInfo.bBakedLODHierarchy && createInfo.FallbackIndexCount > 0u)
        {
            MegaGeometry::MegaMeshLevelRange fallbackRange;
            fallbackRange.FirstIndex = createInfo.FallbackIndexOffset;
            fallbackRange.IndexCount = createInfo.FallbackIndexCount;
            fallbackRange.Error = createInfo.FallbackError;
            gpuData.ShadowLODLevel = static_cast<uint32_t>(gpuData.LevelRanges.size());
            gpuData.ShadowFirstIndex = fallbackRange.FirstIndex;
            gpuData.ShadowIndexCount = fallbackRange.IndexCount;
            gpuData.LevelRanges.push_back(fallbackRange);
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
                            gpuData.ShadowLODLevel,
                            gpuData.ShadowIndexCount / 3u,
                            gpuData.LODBounds.IsValid() ? 1 : 0,
                            createInfo.bBakedLODHierarchy ? 1 : 0,
                            static_cast<uint32_t>(createInfo.ClusterGroups.size()));
        }

        {
            MegaMeshEntry entry;
            entry.Data = std::move(gpuData);
            entry.Lease = std::move(lease);
            entry.StagedBytes = std::move(stagedBytes);
            Thread::ScopedLock lock(m_Mutex);
            m_MegaMeshes[handle.Id] = std::move(entry);
            m_PendingUploadIds.push_back(handle.Id);
        }

        NORVES_LOG_INFO("MegaGeometryResources",
                        "MegaMesh created: %s (vertices: %u, indices: %u, clusters: %u)",
                        createInfo.DebugName.c_str(),
                        createInfo.VertexCount,
                        createInfo.IndexCount,
                        static_cast<uint32_t>(createInfo.Clusters.size()));

        NORVES_LOG_INFO("AssetLoadProfile",
                        "stage=megamesh_gpu_upload role=main_render debug_name=\"%s\" vertex_bytes=%llu index_bytes=%llu cluster_bytes=%llu vertex_ms=%.3f index_ms=%.3f cluster_ms=%.3f success=1",
                        createInfo.DebugName.c_str(),
                        static_cast<unsigned long long>(vertexBytes),
                        static_cast<unsigned long long>(indexBytes),
                        static_cast<unsigned long long>(clusterBytes),
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
        return &it->second.Data;
    }

    const MegaGeometry::MegaMeshGPUData *MegaGeometryResourceStore::GetReadyMegaMeshGPUData(
        MegaGeometry::MegaMeshHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_MegaMeshes.find(handle.Id);
        if (it == m_MegaMeshes.end() || !IsEntryGpuReadyLocked(it->second))
        {
            return nullptr;
        }
        return &it->second.Data;
    }

    bool MegaGeometryResourceStore::IsMegaMeshGpuReady(MegaGeometry::MegaMeshHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_MegaMeshes.find(handle.Id);
        return it != m_MegaMeshes.end() && IsEntryGpuReadyLocked(it->second);
    }

    bool MegaGeometryResourceStore::IsEntryGpuReadyLocked(const MegaMeshEntry &entry) const
    {
        if (entry.bGpuReady)
        {
            return true;
        }
        // 全てをリングへ積み終え、その範囲宛てのコピーが GPU で完了（リングの区画が手放された）してから読める
        if (!entry.bFullyEnqueued || !m_Uploader ||
            m_Uploader->HasUnfinishedBufferCopies(entry.Lease.GetBufferHandle(), entry.Lease.GetOffsetBytes(),
                                                  entry.Lease.GetSizeBytes()))
        {
            return false;
        }
        entry.bGpuReady = true;
        return true;
    }

    uint64_t MegaGeometryResourceStore::PumpUploads(uint64_t maxBytes)
    {
        if (!m_Uploader || maxBytes == 0)
        {
            return 0;
        }

        Thread::ScopedLock lock(m_Mutex);
        if (m_PendingUploadIds.empty())
        {
            return 0;
        }

        uint64_t budget = std::min(maxBytes, m_Uploader->GetRecordableCopyBytes());
        uint64_t enqueuedTotal = 0;
        bool bAnyCompleted = false;
        for (size_t index = 0; index < m_PendingUploadIds.size() && budget > 0; ++index)
        {
            auto it = m_MegaMeshes.find(m_PendingUploadIds[index]);
            if (it == m_MegaMeshes.end())
            {
                continue;
            }
            MegaMeshEntry &entry = it->second;
            const uint64_t totalBytes = entry.StagedBytes.size();
            const RHI::BufferPtr &buffer = entry.Lease.GetBufferHandle();
            bool bBlocked = false;
            while (entry.EnqueuedBytes < totalBytes)
            {
                const uint64_t chunkBytes = std::min({UploadChunkBytes, totalBytes - entry.EnqueuedBytes, budget});
                if (chunkBytes == 0)
                {
                    bBlocked = true;
                    break;
                }
                if (!m_Uploader->EnqueueBufferCopy(buffer, entry.Lease.GetOffsetBytes() + entry.EnqueuedBytes,
                                                   entry.StagedBytes.data() + entry.EnqueuedBytes, chunkBytes))
                {
                    // リングに空きが無い（フレームが進めば空く）。次のフレームでここから続ける
                    bBlocked = true;
                    break;
                }
                entry.EnqueuedBytes += chunkBytes;
                budget -= chunkBytes;
                enqueuedTotal += chunkBytes;
            }
            if (entry.EnqueuedBytes >= totalBytes)
            {
                entry.bFullyEnqueued = true;
                entry.StagedBytes = Container::VariableArray<uint8_t>();
                bAnyCompleted = true;
                NORVES_LOG_INFO("MegaGeometryResources",
                                "stage=megamesh_pool_upload_enqueued debug_name=\"%s\" region_bytes=%llu",
                                entry.Data.DebugName.c_str(),
                                static_cast<unsigned long long>(totalBytes));
            }
            if (bBlocked)
            {
                break;
            }
        }

        // 積み終えたメッシュを待ち行列から外す（残りの順序は保つ）
        if (bAnyCompleted)
        {
            Container::VariableArray<uint64_t> remaining;
            for (size_t index = 0; index < m_PendingUploadIds.size(); ++index)
            {
                auto it = m_MegaMeshes.find(m_PendingUploadIds[index]);
                if (it != m_MegaMeshes.end() && !it->second.bFullyEnqueued)
                {
                    remaining.push_back(m_PendingUploadIds[index]);
                }
            }
            m_PendingUploadIds = std::move(remaining);
        }
        return enqueuedTotal;
    }

    bool MegaGeometryResourceStore::HasPendingGpuUploads() const
    {
        Thread::ScopedLock lock(m_Mutex);
        for (const auto &pair : m_MegaMeshes)
        {
            if (!IsEntryGpuReadyLocked(pair.second))
            {
                return true;
            }
        }
        return false;
    }

    void MegaGeometryResourceStore::RetireEntryLocked(MegaMeshEntry &entry)
    {
        if (!entry.Lease.IsValid())
        {
            return;
        }
        // まだ GPU へ出していないコピーが、解放後に使い回される区画へ書き込まないようにする
        if (m_Uploader)
        {
            m_Uploader->AbandonBufferRange(entry.Lease.GetBufferHandle(), entry.Lease.GetOffsetBytes(),
                                           entry.Lease.GetSizeBytes());
        }
        if (m_RetireQueue)
        {
            m_RetireQueue->Retire(std::move(entry.Lease));
        }
        else
        {
            entry.Lease.Reset();
        }
    }

    void MegaGeometryResourceStore::ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle)
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_MegaMeshes.find(handle.Id);
        if (it == m_MegaMeshes.end())
        {
            return;
        }
        RetireEntryLocked(it->second);
        m_MegaMeshes.erase(it);
        m_PendingUploadIds.erase(std::remove(m_PendingUploadIds.begin(), m_PendingUploadIds.end(), handle.Id),
                                 m_PendingUploadIds.end());
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
            auto meshIt = m_MegaMeshes.find(megaMeshHandle.Id);
            if (meshIt != m_MegaMeshes.end())
            {
                RetireEntryLocked(meshIt->second);
                m_MegaMeshes.erase(meshIt);
                m_PendingUploadIds.erase(
                    std::remove(m_PendingUploadIds.begin(), m_PendingUploadIds.end(), megaMeshHandle.Id),
                    m_PendingUploadIds.end());
            }
        }
    }

    void MegaGeometryResourceStore::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Models.clear();
        for (auto &pair : m_MegaMeshes)
        {
            RetireEntryLocked(pair.second);
        }
        m_MegaMeshes.clear();
        m_PendingUploadIds.clear();
    }

} // namespace NorvesLib::Core::Rendering
