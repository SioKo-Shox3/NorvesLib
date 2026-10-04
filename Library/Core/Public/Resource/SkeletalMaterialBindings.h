#pragma once

#include "Resource/SkeletalLimits.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    // 数値material idのoverrideをmesh世代へ束縛する。0は全体材質へのfallback。
    class SkeletalMaterialBindings
    {
    public:
        void Clear() noexcept
        {
            m_MeshId = 0;
            m_Generation = 0;
            m_SlotCount = 0;
            for (auto& id : m_Overrides)
            {
                id = 0;
            }
        }
        [[nodiscard]] bool Set(uint64_t meshId, uint64_t generation, uint32_t slotCount,
            uint32_t slot, uint64_t materialId) noexcept
        {
            if (!IsValidSlot(meshId, generation, slotCount, slot))
            {
                return false;
            }
            if (m_MeshId != meshId || m_Generation != generation || m_SlotCount != slotCount)
            {
                Clear();
                m_MeshId = meshId;
                m_Generation = generation;
                m_SlotCount = slotCount;
            }
            m_Overrides[slot] = materialId;
            return true;
        }
        // 別世代のoverrideは使わずfallback。無効slotはfalseでoutを保持する。
        [[nodiscard]] bool TryGet(uint64_t meshId, uint64_t generation, uint32_t slotCount,
            uint32_t slot, uint64_t fallback, uint64_t& out) const noexcept
        {
            if (!IsValidSlot(meshId, generation, slotCount, slot))
            {
                return false;
            }
            const bool bSame = m_MeshId == meshId && m_Generation == generation && m_SlotCount == slotCount;
            out = bSame && m_Overrides[slot] != 0 ? m_Overrides[slot] : fallback;
            return true;
        }
    private:
        static bool IsValidSlot(uint64_t meshId, uint64_t generation, uint32_t count, uint32_t slot) noexcept
        {
            return meshId != 0 && generation != 0 && count > 0 && count <= MaximumMaterialSlotCount && slot < count;
        }
        uint64_t m_MeshId = 0;
        uint64_t m_Generation = 0;
        uint32_t m_SlotCount = 0;
        uint64_t m_Overrides[MaximumMaterialSlotCount]{};
    };

    // 名前照合を所有層へ委ねる。0件/不在/重複は-1で、曖昧な名前を先頭slotへ割り当てない。
    template<typename Matches>
    [[nodiscard]] int32_t FindUniqueSkeletalMaterialSlot(uint32_t count, Matches matches)
    {
        if (count == 0 || count > MaximumMaterialSlotCount)
        {
            return -1;
        }
        int32_t found = -1;
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            if (matches(slot))
            {
                if (found != -1)
                {
                    return -1;
                }
                found = static_cast<int32_t>(slot);
            }
        }
        return found;
    }
} // namespace NorvesLib::Core::Skeletal
