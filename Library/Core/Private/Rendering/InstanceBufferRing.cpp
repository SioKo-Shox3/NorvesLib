#include "Rendering/InstanceBufferRing.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    bool InstanceBufferRing::Initialize(RHI::IDevice* device, uint32_t framesInFlight, uint32_t initialCapacity)
    {
        Shutdown();

        if (!device)
        {
            NORVES_LOG_ERROR("InstanceBufferRing", "Initialize failed: device is null");
            return false;
        }

        if (framesInFlight == 0)
        {
            NORVES_LOG_ERROR("InstanceBufferRing", "Initialize failed: framesInFlight is zero");
            return false;
        }

        m_Device = device;
        m_FramesInFlight = framesInFlight;
        const uint32_t capacity = initialCapacity > 0 ? initialCapacity : 1;

        m_Slots.reserve(framesInFlight);
        for (uint32_t frameIndex = 0; frameIndex < framesInFlight; ++frameIndex)
        {
            RHI::BufferPtr buffer = CreateBuffer(capacity);
            if (!buffer)
            {
                NORVES_LOG_ERROR("InstanceBufferRing",
                                 "Initialize failed: slot buffer creation failed for frame %u",
                                 frameIndex);
                Shutdown();
                return false;
            }

            Slot slot;
            slot.Buffer = buffer;
            slot.Capacity = capacity;
            m_Slots.push_back(slot);
        }

        return true;
    }

    void InstanceBufferRing::Shutdown()
    {
        m_Slots.clear();
        m_DeferredReleases.clear();
        m_Device = nullptr;
        m_FramesInFlight = 0;
        m_UploadSerial = 0;
    }

    RHI::BufferPtr InstanceBufferRing::Upload(uint32_t frameIndex,
                                              const Container::VariableArray<GPUSceneInstanceData>& data)
    {
        if (frameIndex >= m_Slots.size())
        {
            NORVES_LOG_WARNING("InstanceBufferRing",
                               "Upload skipped: invalid frameIndex=%u slotCount=%zu",
                               frameIndex,
                               m_Slots.size());
            return nullptr;
        }

        const uint64_t currentSerial = m_UploadSerial;
        ++m_UploadSerial;
        ReleaseExpiredBuffers(currentSerial);

        Slot& slot = m_Slots[frameIndex];
        if (data.empty())
        {
            return slot.Buffer;
        }

        const uint32_t requiredCapacity = static_cast<uint32_t>(data.size());
        if (requiredCapacity > slot.Capacity)
        {
            uint32_t newCapacity = slot.Capacity > 0 ? slot.Capacity : 1;
            while (newCapacity < requiredCapacity)
            {
                newCapacity *= 2;
            }

            RHI::BufferPtr newBuffer = CreateBuffer(newCapacity);
            if (!newBuffer)
            {
                NORVES_LOG_ERROR("InstanceBufferRing",
                                 "Upload failed: could not resize slot %u from %u to %u instances",
                                 frameIndex,
                                 slot.Capacity,
                                 newCapacity);
                return nullptr;
            }

            if (slot.Buffer)
            {
                DeferredRelease release;
                release.Buffer = slot.Buffer;
                release.ReleaseAfterSerial = currentSerial + m_FramesInFlight;
                m_DeferredReleases.push_back(release);
            }

            slot.Buffer = newBuffer;
            slot.Capacity = newCapacity;
        }

        const uint64_t uploadSize = static_cast<uint64_t>(data.size() * sizeof(GPUSceneInstanceData));
        slot.Buffer->Update(data.data(), uploadSize, 0);
        return slot.Buffer;
    }

    RHI::BufferPtr InstanceBufferRing::CreateBuffer(uint32_t capacity) const
    {
        if (!m_Device)
        {
            return nullptr;
        }

        RHI::BufferDesc desc;
        desc.Size = static_cast<uint64_t>(capacity) * sizeof(GPUSceneInstanceData);
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
        desc.CPUAccessible = true;
        desc.DebugName = "InstanceBufferRing_Slot";
        return m_Device->CreateBuffer(desc);
    }

    void InstanceBufferRing::ReleaseExpiredBuffers(uint64_t currentSerial)
    {
        for (auto it = m_DeferredReleases.begin(); it != m_DeferredReleases.end();)
        {
            if (currentSerial >= it->ReleaseAfterSerial)
            {
                it = m_DeferredReleases.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace NorvesLib::Core::Rendering
