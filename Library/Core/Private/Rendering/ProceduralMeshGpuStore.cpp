#include "Rendering/ProceduralMeshGpuStore.h"

#include "Rendering/MeshTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <utility>

namespace NorvesLib::Core::Rendering
{
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
            RHI::ResourceUsage::VertexBuffer,
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
            RHI::ResourceUsage::IndexBuffer,
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
        gpuData.SubMeshCount = std::min(subMeshCount, MAX_MATERIAL_SLOTS);
        for (uint32_t i = 0; i < gpuData.SubMeshCount; ++i)
        {
            gpuData.SubMeshes[i].IndexStart = subMeshes[i].IndexStart;
            gpuData.SubMeshes[i].IndexCount = subMeshes[i].IndexCount;
            gpuData.SubMeshes[i].VertexStart = subMeshes[i].VertexStart;
            gpuData.SubMeshes[i].MaterialIndex = subMeshes[i].MaterialIndex;
        }

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
