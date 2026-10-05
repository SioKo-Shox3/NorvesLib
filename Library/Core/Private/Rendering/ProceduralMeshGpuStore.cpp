#include "Rendering/ProceduralMeshGpuStore.h"

#include "Rendering/MeshTypes.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 描画に加えて、計算シェーダー（storage）とアドレス参照（BDA）から頂点・インデックスを読めるようにする。
        // BDA が使えないデバイスでは RHI 側が用途を無視する。
        const RHI::ResourceUsage kVertexBufferUsage = RHI::ResourceUsage::VertexBuffer |
                                                       RHI::ResourceUsage::StorageBuffer |
                                                       RHI::ResourceUsage::ShaderRead |
                                                       RHI::ResourceUsage::BufferDeviceAddress;
        const RHI::ResourceUsage kIndexBufferUsage = RHI::ResourceUsage::IndexBuffer |
                                                      RHI::ResourceUsage::StorageBuffer |
                                                      RHI::ResourceUsage::ShaderRead |
                                                      RHI::ResourceUsage::BufferDeviceAddress;

        // 頂点の位置からローカル空間のAABBを求める。GBufferPass はこの置き場のメッシュを Mesh3DVertex の並びで
        // 描くので、その大きさで割り切れるときだけ位置を読む。非有限の位置があれば求めない。
        bool ComputeMesh3DVertexBounds(const void *vertices, size_t vertexSize, BoundingBox &outBounds)
        {
            if (vertices == nullptr || vertexSize < sizeof(Mesh3DVertex) || vertexSize % sizeof(Mesh3DVertex) != 0)
            {
                return false;
            }
            const auto *meshVertices = static_cast<const Mesh3DVertex *>(vertices);
            const size_t vertexCount = vertexSize / sizeof(Mesh3DVertex);
            BoundingBox bounds;
            bounds.MinX = bounds.MaxX = meshVertices[0].Position[0];
            bounds.MinY = bounds.MaxY = meshVertices[0].Position[1];
            bounds.MinZ = bounds.MaxZ = meshVertices[0].Position[2];
            for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            {
                const float *position = meshVertices[vertexIndex].Position;
                if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
                {
                    return false;
                }
                bounds.Expand(position[0], position[1], position[2]);
            }
            outBounds = bounds;
            return true;
        }
    }

    ProceduralMeshGpuStore::ProceduralMeshGpuStore(Container::TSharedPtr<RHI::IDevice> device)
        : m_Device(std::move(device))
    {
    }

    ProceduralMeshGpuStore::~ProceduralMeshGpuStore() = default;

    bool ProceduralMeshGpuStore::RegisterMesh(MeshDataHandle handle,
                                              const void *vertices,
                                              size_t vertexSize,
                                              const uint32_t *indices,
                                              uint32_t indexCount)
    {
        return RegisterMesh(handle, vertices, vertexSize, indices, indexCount, nullptr, 0);
    }

    bool ProceduralMeshGpuStore::RegisterMesh(MeshDataHandle handle,
                                              const void *vertices,
                                              size_t vertexSize,
                                              const uint32_t *indices,
                                              uint32_t indexCount,
                                              const SubMesh* subMeshes,
                                              uint32_t subMeshCount)
    {
        if (!m_Device ||
            !handle.IsValid() ||
            !vertices ||
            !indices ||
            indexCount == 0 ||
            (subMeshCount > 0 && subMeshes == nullptr))
        {
            return false;
        }

        {
            Thread::ScopedLock lock(m_Mutex);
            m_Meshes.erase(handle.Id);
        }

        RHI::BufferDesc vbDesc(
            static_cast<uint64_t>(vertexSize),
            kVertexBufferUsage,
            true,
            "MeshVB");
        auto vertexBuffer = m_Device->CreateBuffer(vbDesc);
        if (!vertexBuffer)
        {
            NORVES_LOG_ERROR("MeshResources", "Failed to create vertex buffer for mesh");
            return false;
        }
        vertexBuffer->Update(vertices, vertexSize);

        const size_t ibSize = static_cast<size_t>(indexCount) * sizeof(uint32_t);
        RHI::BufferDesc ibDesc(
            static_cast<uint64_t>(ibSize),
            kIndexBufferUsage,
            true,
            "MeshIB");
        auto indexBuffer = m_Device->CreateBuffer(ibDesc);
        if (!indexBuffer)
        {
            NORVES_LOG_ERROR("MeshResources", "Failed to create index buffer for mesh");
            return false;
        }
        indexBuffer->Update(indices, ibSize);

        ProceduralMeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.IndexCount = indexCount;
        gpuData.bHasLocalBounds = ComputeMesh3DVertexBounds(vertices, vertexSize, gpuData.LocalBounds);
        gpuData.SubMeshCount = std::min(subMeshCount, MAX_MATERIAL_SLOTS);
        Container::FixedArray<uint32_t, MAX_MATERIAL_SLOTS * 2> chunkBoundaries;
        uint32_t chunkBoundaryCount = 0;
        for (uint32_t i = 0; i < gpuData.SubMeshCount; ++i)
        {
            gpuData.SubMeshes[i].IndexStart = subMeshes[i].IndexStart;
            gpuData.SubMeshes[i].IndexCount = subMeshes[i].IndexCount;
            gpuData.SubMeshes[i].VertexStart = subMeshes[i].VertexStart;
            gpuData.SubMeshes[i].MaterialIndex = subMeshes[i].MaterialIndex;
            // サブメッシュの始まりと終わりで塊を区切り、1つの塊が複数の材質をまたがないようにする
            chunkBoundaries[chunkBoundaryCount++] = subMeshes[i].IndexStart;
            chunkBoundaries[chunkBoundaryCount++] = subMeshes[i].IndexStart + subMeshes[i].IndexCount;
        }
        BuildMeshIndexChunks(indexCount, chunkBoundaries.data(), chunkBoundaryCount, gpuData.Chunks);

        {
            Thread::ScopedLock lock(m_Mutex);
            m_Meshes[handle.Id] = std::move(gpuData);
        }

        NORVES_LOG_INFO("MeshResources", "Mesh registered successfully");
        return true;
    }

    const ProceduralMeshGPUData *ProceduralMeshGpuStore::GetMeshGPUData(MeshDataHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Meshes.find(handle.Id);
        if (it != m_Meshes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool ProceduralMeshGpuStore::TryGetSubMeshRanges(
        MeshDataHandle handle,
        Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS>& out,
        uint32_t& outCount) const
    {
        outCount = 0;
        if (!handle.IsValid())
        {
            return false;
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Meshes.find(handle.Id);
        if (it == m_Meshes.end())
        {
            return false;
        }

        const ProceduralMeshGPUData& gpuData = it->second;
        outCount = gpuData.SubMeshCount;
        for (uint32_t i = 0; i < outCount; ++i)
        {
            out[i] = gpuData.SubMeshes[i];
        }

        return true;
    }

    bool ProceduralMeshGpuStore::TryGetLocalBounds(MeshDataHandle handle, BoundingBox &outBounds) const
    {
        if (!handle.IsValid())
        {
            return false;
        }

        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Meshes.find(handle.Id);
        if (it == m_Meshes.end() || !it->second.bHasLocalBounds)
        {
            return false;
        }
        outBounds = it->second.LocalBounds;
        return true;
    }

    void ProceduralMeshGpuStore::UnregisterMesh(MeshDataHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        m_Meshes.erase(handle.Id);
    }

    void ProceduralMeshGpuStore::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Meshes.clear();
    }

} // namespace NorvesLib::Core::Rendering
