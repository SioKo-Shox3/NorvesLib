// 物理と表面の問い合わせで共有するハンドルを定義する。
// 問い合わせの結果型から独立させ、ヘッダの循環参照を防ぐ。
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Scene
{
    struct ColliderHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const ColliderHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        constexpr bool operator<(const ColliderHandle& other) const
        {
            return Index < other.Index || (Index == other.Index && Generation < other.Generation);
        }
    };

    struct BodyHandle
    {
        static constexpr uint32_t InvalidIndex = UINT32_MAX;

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;

        constexpr bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0;
        }

        constexpr bool operator==(const BodyHandle& other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        constexpr bool operator<(const BodyHandle& other) const
        {
            return Index < other.Index || (Index == other.Index && Generation < other.Generation);
        }
    };
} // namespace NorvesLib::Core::Scene
