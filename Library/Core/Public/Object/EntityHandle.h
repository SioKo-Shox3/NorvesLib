#pragma once

#include <cstdint>

namespace NorvesLib::Core
{
    struct EntityHandle
    {
        static constexpr uint32_t InvalidSlotIndex = ~uint32_t{0};

        uint32_t SlotIndex = InvalidSlotIndex;
        uint32_t Generation = 0;

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
            return SlotIndex == other.SlotIndex && Generation == other.Generation;
        }

        constexpr bool operator!=(const EntityHandle& other) const
        {
            return !(*this == other);
        }
    };

} // namespace NorvesLib::Core
