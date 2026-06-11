#include "Rendering/MegaGeometryResourceStore.h"

#include "Rendering/MegaGeometry/LODHierarchyBuilder.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <chrono>
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

        // Build an optional LOD hierarchy before GPU upload.
        const void *uploadVertexData = createInfo.VertexData;
        size_t uploadVertexDataSize = createInfo.VertexDataSize;
        const uint32_t *uploadIndexData = createInfo.IndexData;
        uint32_t uploadIndexCount = createInfo.IndexCount;
        uint32_t uploadVertexCount = createInfo.VertexCount;
        const Container::VariableArray<MegaGeometry::MeshCluster> *uploadClusters = &createInfo.Clusters;
        BoundingSphere uploadTotalBounds = createInfo.TotalBounds;

        MegaGeometry::LODHierarchy lodHierarchy;

        if (createInfo.bBuildLODHierarchy && createInfo.Clusters.size() > 1)
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
        gpuData.Material = createInfo.Material;
        gpuData.DebugName = createInfo.DebugName;

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
