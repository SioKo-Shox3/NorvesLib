#include "Rendering/ProceduralMeshGpuStore.h"

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"

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
        if (!m_Device || !handle.IsValid() || !vertices || !indices || indexCount == 0)
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
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create vertex buffer for mesh");
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
            NORVES_LOG_ERROR("RenderResourceManager", "Failed to create index buffer for mesh");
            return false;
        }
        indexBuffer->Update(indices, ibSize);

        RenderResourceManager::MeshGPUData gpuData;
        gpuData.VertexBuffer = vertexBuffer;
        gpuData.IndexBuffer = indexBuffer;
        gpuData.IndexCount = indexCount;

        {
            Thread::ScopedLock lock(m_Mutex);
            m_Meshes[handle.Id] = std::move(gpuData);
        }

        NORVES_LOG_INFO("RenderResourceManager", "Mesh registered successfully");
        return true;
    }

    const RenderResourceManager::MeshGPUData *ProceduralMeshGpuStore::GetMeshGPUData(MeshDataHandle handle) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Meshes.find(handle.Id);
        if (it != m_Meshes.end())
        {
            return &it->second;
        }
        return nullptr;
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
