#pragma once

#include "Rendering/RenderResourceManager.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class ProceduralMeshGpuStore final
    {
    public:
        explicit ProceduralMeshGpuStore(Container::TSharedPtr<RHI::IDevice> device);
        ~ProceduralMeshGpuStore();

        ProceduralMeshGpuStore(const ProceduralMeshGpuStore &) = delete;
        ProceduralMeshGpuStore &operator=(const ProceduralMeshGpuStore &) = delete;

        bool RegisterMesh(MeshDataHandle handle,
                          const void *vertices,
                          size_t vertexSize,
                          const uint32_t *indices,
                          uint32_t indexCount);
        const RenderResourceManager::MeshGPUData *GetMeshGPUData(MeshDataHandle handle) const;
        void UnregisterMesh(MeshDataHandle handle);
        void Clear();

    private:
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::Map<uint64_t, RenderResourceManager::MeshGPUData> m_Meshes;
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core::Rendering
