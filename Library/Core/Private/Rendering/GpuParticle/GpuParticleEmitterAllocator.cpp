// 空き範囲を前後で併合し、解放済みハンドルと世代の折り返しを拒否する。
#include "Rendering/GpuParticle/GpuParticleEmitterAllocator.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    bool GpuParticleEmitterHandle::IsValid() const
    {
        return Index != 0xFFFFFFFFu && Generation != 0u;
    }

    GpuParticleEmitterAllocator::GpuParticleEmitterAllocator(uint32_t poolCapacity)
        : m_PoolCapacity(poolCapacity)
    {
        if (poolCapacity > 0u)
        {
            m_FreeRanges.push_back(GpuParticleEmitterRange{0u, poolCapacity});
        }
    }

    GpuParticleEmitterHandle GpuParticleEmitterAllocator::Allocate(uint32_t capacity)
    {
        if (capacity == 0u)
        {
            return {};
        }

        for (size_t rangeIndex = 0u; rangeIndex < m_FreeRanges.size(); ++rangeIndex)
        {
            GpuParticleEmitterRange& freeRange = m_FreeRanges[rangeIndex];
            if (freeRange.Capacity < capacity)
            {
                continue;
            }

            uint32_t slotIndex = 0u;
            if (m_FreeSlots.empty())
            {
                if (m_Slots.size() >= std::numeric_limits<uint32_t>::max())
                {
                    return {};
                }
                slotIndex = static_cast<uint32_t>(m_Slots.size());
                m_Slots.push_back(EmitterSlot{});
            }
            else
            {
                slotIndex = m_FreeSlots.back();
                m_FreeSlots.pop_back();
            }

            EmitterSlot& slot = m_Slots[slotIndex];
            slot.Range = GpuParticleEmitterRange{freeRange.Offset, capacity};
            slot.bLive = true;
            freeRange.Offset += capacity;
            freeRange.Capacity -= capacity;
            if (freeRange.Capacity == 0u)
            {
                m_FreeRanges.erase(m_FreeRanges.begin() + rangeIndex);
            }
            m_UsedCapacity += capacity;
            return GpuParticleEmitterHandle{slotIndex, slot.Generation};
        }
        return {};
    }

    bool GpuParticleEmitterAllocator::Free(GpuParticleEmitterHandle handle)
    {
        if (!IsLive(handle))
        {
            return false;
        }

        EmitterSlot& slot = m_Slots[handle.Index];
        const GpuParticleEmitterRange releasedRange = slot.Range;
        size_t rangeIndex = 0u;
        while (rangeIndex < m_FreeRanges.size() && m_FreeRanges[rangeIndex].Offset < releasedRange.Offset)
        {
            ++rangeIndex;
        }
        m_FreeRanges.insert(m_FreeRanges.begin() + rangeIndex, releasedRange);

        if (rangeIndex > 0u)
        {
            GpuParticleEmitterRange& previous = m_FreeRanges[rangeIndex - 1u];
            const GpuParticleEmitterRange& current = m_FreeRanges[rangeIndex];
            if (previous.Offset + previous.Capacity == current.Offset)
            {
                previous.Capacity += current.Capacity;
                m_FreeRanges.erase(m_FreeRanges.begin() + rangeIndex);
                --rangeIndex;
            }
        }
        if (rangeIndex + 1u < m_FreeRanges.size())
        {
            GpuParticleEmitterRange& current = m_FreeRanges[rangeIndex];
            const GpuParticleEmitterRange& next = m_FreeRanges[rangeIndex + 1u];
            if (current.Offset + current.Capacity == next.Offset)
            {
                current.Capacity += next.Capacity;
                m_FreeRanges.erase(m_FreeRanges.begin() + rangeIndex + 1u);
            }
        }

        m_UsedCapacity -= releasedRange.Capacity;
        slot.Range = {};
        slot.bLive = false;
        constexpr uint32_t maxGeneration = std::numeric_limits<uint32_t>::max();
        if (slot.Generation < maxGeneration)
        {
            ++slot.Generation;
        }
        // 最大世代に達したスロットは空き表へ戻さず、恒久的に退役させる。
        if (slot.Generation < maxGeneration)
        {
            m_FreeSlots.push_back(handle.Index);
        }
        return true;
    }

    bool GpuParticleEmitterAllocator::TryGetRange(GpuParticleEmitterHandle handle, GpuParticleEmitterRange& outRange) const
    {
        if (!IsLive(handle))
        {
            return false;
        }
        outRange = m_Slots[handle.Index].Range;
        return true;
    }

    bool GpuParticleEmitterAllocator::IsLive(GpuParticleEmitterHandle handle) const
    {
        return handle.IsValid() && handle.Index < m_Slots.size() &&
            m_Slots[handle.Index].bLive && m_Slots[handle.Index].Generation == handle.Generation;
    }

    uint32_t GpuParticleEmitterAllocator::GetPoolCapacity() const
    {
        return m_PoolCapacity;
    }

    uint32_t GpuParticleEmitterAllocator::GetUsedCapacity() const
    {
        return m_UsedCapacity;
    }

    uint32_t GpuParticleEmitterAllocator::GetLargestFreeBlock() const
    {
        uint32_t largest = 0u;
        for (const GpuParticleEmitterRange& range : m_FreeRanges)
        {
            largest = range.Capacity > largest ? range.Capacity : largest;
        }
        return largest;
    }
} // namespace NorvesLib::Core::Rendering
