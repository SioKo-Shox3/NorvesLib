#pragma once
#include <cstdint>

namespace NorvesLib::Core
{
    /**
     * @brief Unreal風のキャストフラグ（第1層キャスト判定用ビット集合）
     *
     * 頻出する基底型へのキャストを、祖先テーブル（配列1アクセス）より軽い
     * 単一ビットANDで判定するためのフラグ。各クラスは自分＋全祖先のビットのOR
     * を保持する。最大63bitまで割り当て可能。
     */
    enum class EClassCastFlags : uint64_t
    {
        None                  = 0,
        Object                = 1ull << 0,
        Entity                = 1ull << 1,
        World                 = 1ull << 2,
        Resource              = 1ull << 3,
        Component             = 1ull << 4,
        MeshComponent         = 1ull << 5,
        MegaGeometryComponent = 1ull << 6,
        LightComponent        = 1ull << 7,
        PointLightComponent   = 1ull << 8,
        BoardComponent        = 1ull << 9,
        BillboardComponent    = 1ull << 10,
        // 将来のホット基底型はここに追記（最大63bitまで）
    };

    /** @brief フラグのビット和を返します */
    constexpr EClassCastFlags operator|(EClassCastFlags a, EClassCastFlags b)
    {
        return static_cast<EClassCastFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
    }

    /** @brief valueがtestのいずれかのビットを持つか判定します */
    constexpr bool HasAnyFlags(EClassCastFlags value, EClassCastFlags test)
    {
        return (static_cast<uint64_t>(value) & static_cast<uint64_t>(test)) != 0;
    }

    /**
     * @brief 型ごとのキャストフラグを返すトレイト
     *
     * 既定はNone。ホット型のみDECLARE_CLASS_CAST_FLAGで特殊化する。
     */
    template <typename T>
    struct ClassCastFlagTraits
    {
        static constexpr EClassCastFlags Value = EClassCastFlags::None;
    };
} // namespace NorvesLib::Core

// 型ごとのビット割当マクロ。【重要】各型の「自分のヘッダ内」でglobal scopeに置き、
// その型を使う全TUで可視にすること。可視性が割れるとODR違反/挙動不整合になる。
#define DECLARE_CLASS_CAST_FLAG(QualifiedClass, FlagExpr)              \
    namespace NorvesLib::Core                                          \
    {                                                                  \
        template <>                                                    \
        struct ClassCastFlagTraits<QualifiedClass>                     \
        {                                                              \
            static constexpr EClassCastFlags Value = FlagExpr;         \
        };                                                             \
    }
