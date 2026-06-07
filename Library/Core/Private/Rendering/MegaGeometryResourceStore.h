#pragma once

#include "Rendering/RenderResourceManager.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class MegaGeometryResourceStore final
    {
    public:
        MegaGeometryResourceStore(Container::TSharedPtr<RHI::IDevice> device,
                                  Thread::Atomic<uint64_t> &nextHandleId);
        ~MegaGeometryResourceStore();

        MegaGeometryResourceStore(const MegaGeometryResourceStore &) = delete;
        MegaGeometryResourceStore &operator=(const MegaGeometryResourceStore &) = delete;

        MegaGeometry::MegaMeshHandle CreateMegaMesh(const MegaGeometry::MegaMeshCreateInfo &createInfo);
        const MegaGeometry::MegaMeshGPUData *GetMegaMeshGPUData(MegaGeometry::MegaMeshHandle handle) const;
        void ReleaseMegaMesh(MegaGeometry::MegaMeshHandle handle);

        ModelHandle RegisterModel(MegaGeometry::MegaMeshHandle megaMeshHandle,
                                  const Container::String &debugName = "",
                                  const Container::String &sourcePath = "");
        MegaGeometry::MegaMeshHandle GetModelMegaMeshHandle(ModelHandle handle) const;
        void ReleaseModel(ModelHandle handle);

        void Clear();

    private:
        template <typename HandleType>
        HandleType AllocateHandle()
        {
            HandleType handle;
            handle.Id = m_NextHandleId.FetchAdd(1, std::memory_order_relaxed);
            return handle;
        }

        Container::TSharedPtr<RHI::IDevice> m_Device;
        Thread::Atomic<uint64_t> &m_NextHandleId;
        Container::Map<uint64_t, MegaGeometry::MegaMeshGPUData> m_MegaMeshes;
        Container::Map<uint64_t, ModelResourceData> m_Models;
        mutable Thread::Mutex m_Mutex;
    };

} // namespace NorvesLib::Core::Rendering
