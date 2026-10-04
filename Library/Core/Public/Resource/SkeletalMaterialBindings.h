#pragma once

#include "Resource/SkeletalLimits.h"
#include "Resource/MaterialSelection.h"
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

    // UTF8への変換と所有は呼出側、照合規則は共通resolverだけに置く。
    [[nodiscard]] inline int32_t FindSkeletalMaterialSlot(
        Container::Span<const MaterialIdentityView> slots, Container::Span<const uint8_t> name) noexcept
    {
        if (slots.empty() || slots.size() > MaximumMaterialSlotCount)
        {
            return -1;
        }
        uint32_t scratch[MaximumMaterialSlotCount + 2]{};
        uint32_t rows[1]{};
        const MaterialSelectorView query{MaterialSelectorKind::UniqueName, 0, name, false};
        const auto result = ResolveMaterialSelection(MaterialIdentityDomain::GeneratedSlot,
            slots, {&query, 1}, scratch, rows);
        return result.Succeeded() ? static_cast<int32_t>(slots[rows[0]].IdentityIndex) : -1;
    }
} // namespace NorvesLib::Core::Skeletal
