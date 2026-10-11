#pragma once

#include <cstdint>

namespace NorvesLib::Core
{
    /// Entity の世代付きハンドル。World::GetEntityHandle が発行し、World::ResolveEntityHandle で引く。
    /// SlotIndex・Generation は World の ECS の行の番号と世代、WorldId は発行した World の識別子
    /// （別の World・作り直した ECS の行の番号を取り違えないため）。
    struct EntityHandle
    {
        static constexpr uint32_t InvalidSlotIndex = ~uint32_t{0};

        uint32_t SlotIndex = InvalidSlotIndex;
        uint32_t Generation = 0;
        uint32_t WorldId = 0;

        static constexpr EntityHandle Invalid()
        {
            return EntityHandle{};
        }

        constexpr bool IsValid() const
        {
            return SlotIndex != InvalidSlotIndex && Generation != 0;
        }

        constexpr bool operator==(const EntityHandle& other) const
        {
            return SlotIndex == other.SlotIndex && Generation == other.Generation && WorldId == other.WorldId;
        }

        constexpr bool operator!=(const EntityHandle& other) const
        {
            return !(*this == other);
        }
    };

} // namespace NorvesLib::Core
