#pragma once

#include "Rendering/MeshTypes.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief FramePacketのインスタンスデータをframes-in-flight別SSBOへアップロードするリング。
     */
    class InstanceBufferRing
    {
    public:
        bool Initialize(RHI::IDevice* device, uint32_t framesInFlight, uint32_t initialCapacity);
        void Shutdown();

        RHI::BufferPtr Upload(uint32_t frameIndex, const Container::VariableArray<GPUSceneInstanceData>& data);

    private:
        struct Slot
        {
            RHI::BufferPtr Buffer;
            uint32_t Capacity = 0;
        };

        struct DeferredRelease
        {
            RHI::BufferPtr Buffer;
            uint64_t ReleaseAfterSerial = 0;
        };

        RHI::BufferPtr CreateBuffer(uint32_t capacity) const;
        void ReleaseExpiredBuffers(uint64_t currentSerial);

        RHI::IDevice* m_Device = nullptr;
        uint32_t m_FramesInFlight = 0;
        uint64_t m_UploadSerial = 0;
        Container::VariableArray<Slot> m_Slots;
        Container::VariableArray<DeferredRelease> m_DeferredReleases;
    };

} // namespace NorvesLib::Core::Rendering
