#pragma once

/**
 * @file EcsTypes.h
 * @brief ECS の実体の識別子と、列（コンポーネントの型）の記述
 *
 * EcsEntity は「表の番号 + 世代」の 64 bit。解放した番号は世代を進めてから再利用するので、
 * 古い識別子は IsAlive や取得で拒まれる。世代は 1 から始まり、全ビット 0 の識別子は常に null を表す。
 *
 * 列の型は trivially copyable に限る（移動と入れ替えは memcpy）。所有や参照カウントを持つ値は
 * 列に置かず Object に残す。列の型の番号は TypeRegistry::GetTypeId<T>() の結果を使う。
 */

#include "Object/RuntimeSchema.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Ecs
{
    /// 実体の識別子。Index は実体の表の番号、Generation は再利用のたびに進む世代（0 は null）。
    struct EcsEntity
    {
        uint32_t Index = 0;
        uint32_t Generation = 0;

        [[nodiscard]] constexpr bool IsNull() const
        {
            return Generation == 0;
        }

        [[nodiscard]] constexpr bool operator==(const EcsEntity &other) const
        {
            return Index == other.Index && Generation == other.Generation;
        }

        [[nodiscard]] constexpr bool operator!=(const EcsEntity &other) const
        {
            return !(*this == other);
        }
    };

    inline constexpr EcsEntity kNullEcsEntity{};

    /// アーキタイプの番号（EcsWorld が割り当てる）
    using EcsArchetypeId = uint32_t;
    inline constexpr EcsArchetypeId kInvalidEcsArchetype = 0xFFFFFFFFu;

    /// 列 1 本の記述。TypeId は TypeRegistry の番号、Size / Align は値 1 つの大きさと配置。
    struct EcsColumnInfo
    {
        TypeId Type = 0;
        uint32_t Size = 0;
        uint32_t Align = 1;
    };

    /// 型 T の列の記述を返す。T は trivially copyable でなければならない。
    template <typename T>
    [[nodiscard]] EcsColumnInfo EcsColumnInfoOf()
    {
        static_assert(std::is_trivially_copyable_v<T>, "ECS の列の型は trivially copyable でなければならない");
        static_assert(std::is_trivially_destructible_v<T>, "ECS の列の型は自明に破棄できなければならない");
        static_assert(sizeof(T) > 0 && alignof(T) <= 64, "ECS の列の型の配置は 64 byte 以下");
        // 型ごとの番号は最初の呼び出しで覚える（TypeRegistry は並列の中では引かない）
        static const TypeId s_Type = TypeRegistry::Get().GetTypeId<T>();
        return EcsColumnInfo{s_Type, static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(alignof(T))};
    }

}
