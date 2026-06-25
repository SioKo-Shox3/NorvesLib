#include "Rendering/DynamicBufferRing.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    bool DynamicBufferRing::Initialize(RHI::IDevice *device, uint32_t slotCount,
                                       RHI::ResourceUsage usage, uint64_t initialBytes)
    {
        Shutdown();

        if (!device)
        {
            NORVES_LOG_ERROR("DynamicBufferRing", "Initialize failed: device is null");
            return false;
        }

        if (slotCount == 0)
        {
            NORVES_LOG_ERROR("DynamicBufferRing", "Initialize failed: slotCount is zero");
            return false;
        }

        m_Device = device;
        m_Usage = usage;
        m_SlotCount = slotCount;
        const uint64_t capacity = initialBytes > 0 ? initialBytes : 1;

        m_Slots.reserve(slotCount);
        for (uint32_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
        {
            RHI::BufferPtr buffer = CreateBuffer(capacity);
            if (!buffer)
            {
                NORVES_LOG_ERROR("DynamicBufferRing",
                                 "Initialize failed: slot buffer creation failed for slot %u",
                                 slotIndex);
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

    void DynamicBufferRing::Shutdown()
    {
        m_Slots.clear();
        m_DeferredReleases.clear();
        m_Device = nullptr;
        m_Usage = RHI::ResourceUsage::None;
        m_SlotCount = 0;
        m_UploadSerial = 0;
    }

    RHI::BufferPtr DynamicBufferRing::Upload(uint32_t slotIndex, const void *data, uint64_t bytes)
    {
        if (slotIndex >= m_Slots.size())
        {
            NORVES_LOG_WARNING("DynamicBufferRing",
                               "Upload skipped: invalid slotIndex=%u slotCount=%zu",
                               slotIndex,
                               m_Slots.size());
            return nullptr;
        }

        const uint64_t currentSerial = m_UploadSerial;
        ++m_UploadSerial;
        ReleaseExpiredBuffers(currentSerial);

        Slot &slot = m_Slots[slotIndex];
        if (!data || bytes == 0)
        {
            return slot.Buffer;
        }

        if (bytes > slot.Capacity)
        {
            uint64_t newCapacity = slot.Capacity > 0 ? slot.Capacity : 1;
            while (newCapacity < bytes)
            {
                newCapacity *= 2;
            }

            RHI::BufferPtr newBuffer = CreateBuffer(newCapacity);
            if (!newBuffer)
            {
                NORVES_LOG_ERROR("DynamicBufferRing",
                                 "Upload failed: could not resize slot %u from %llu to %llu bytes",
                                 slotIndex,
                                 static_cast<unsigned long long>(slot.Capacity),
                                 static_cast<unsigned long long>(newCapacity));
                return nullptr;
            }

            if (slot.Buffer)
            {
                DeferredRelease release;
                release.Buffer = slot.Buffer;
                release.ReleaseAfterSerial = currentSerial + m_SlotCount;
                m_DeferredReleases.push_back(release);
            }

            slot.Buffer = newBuffer;
            slot.Capacity = newCapacity;
        }

        slot.Buffer->Update(data, bytes, 0);
        return slot.Buffer;
    }

    RHI::BufferPtr DynamicBufferRing::CreateBuffer(uint64_t bytes) const
    {
        if (!m_Device)
        {
            return nullptr;
        }

        RHI::BufferDesc desc;
        desc.Size = bytes;
        desc.Usage = m_Usage;
        desc.CPUAccessible = true;
        desc.DebugName = "DynamicBufferRing_Slot";
        return m_Device->CreateBuffer(desc);
    }

    void DynamicBufferRing::ReleaseExpiredBuffers(uint64_t currentSerial)
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
