#pragma once

#include "Rendering/ProceduralMeshGPUData.h"
#include "Rendering/RenderTypes.h"
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
    struct SubMesh;

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
        bool RegisterMesh(MeshDataHandle handle,
                          const void *vertices,
                          size_t vertexSize,
                          const uint32_t *indices,
                          uint32_t indexCount,
                          const SubMesh* subMeshes,
                          uint32_t subMeshCount);
        const ProceduralMeshGPUData *GetMeshGPUData(MeshDataHandle handle) const;
        bool TryGetSubMeshRanges(MeshDataHandle handle,
                                 Container::FixedArray<SubMeshRange, MAX_MATERIAL_SLOTS>& out,
                                 uint32_t& outCount) const;
        void UnregisterMesh(MeshDataHandle handle);
        void Clear();

    private:
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::Map<uint64_t, ProceduralMeshGPUData> m_Meshes;
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core::Rendering
