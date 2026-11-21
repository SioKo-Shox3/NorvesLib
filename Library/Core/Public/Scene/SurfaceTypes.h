// 足音・足跡・踏み倒しなどで共有する、表面の語彙の型を定義する。
// 保存には安定した名前を使い、登録簿の添字である SurfaceId は保存に使わない。
// SurfaceId は地形の層の番号（u8）とは別物として扱う。
#pragma once

#include "Scene/PhysicsHandles.h"
#include "Container/String.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Scene
{
    // 登録簿の添字。読み込み順で決まるため、保存には名前を使う。
    // 地形の Rendering::Terrain::GroundSample::SurfaceId（u8）や物理の結果の
    // uint8_t SurfaceId とは別物で、暗黙に変換しない。
    // 層から表面への橋渡しは地形の provider が名前の表で行う。
    struct SurfaceId
    {
        uint16_t Value = 0;

        constexpr SurfaceId() = default;
        constexpr explicit SurfaceId(uint16_t value) : Value(value)
        {
        }

        static constexpr SurfaceId Default()
        {
            return SurfaceId{};
        }

        constexpr bool IsDefault() const
        {
            return Value == 0;
        }

        constexpr bool operator==(const SurfaceId&) const = default;
    };

    static_assert(!std::is_convertible_v<uint8_t, SurfaceId>);
    static_assert(!std::is_convertible_v<int, SurfaceId>);
    static_assert(!std::is_convertible_v<uint16_t, SurfaceId>);
    static_assert(!std::is_convertible_v<SurfaceId, uint16_t>);
    static_assert(std::is_trivially_copyable_v<SurfaceId>);
    static_assert(sizeof(SurfaceId) == 2);

    enum class SurfaceFlags : uint8_t
    {
        None = 0,
        Soft = 1 << 0,
        Hard = 1 << 1,
        Liquid = 1 << 2,
        Vegetated = 1 << 3,
        Wettable = 1 << 4
    };

    constexpr SurfaceFlags operator|(SurfaceFlags left, SurfaceFlags right)
    {
        return static_cast<SurfaceFlags>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr SurfaceFlags operator&(SurfaceFlags left, SurfaceFlags right)
    {
        return static_cast<SurfaceFlags>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    constexpr SurfaceFlags& operator|=(SurfaceFlags& left, SurfaceFlags right)
    {
        left = left | right;
        return left;
    }

    constexpr bool HasAnySurfaceFlag(SurfaceFlags value, SurfaceFlags test)
    {
        return (value & test) != SurfaceFlags::None;
    }

    struct SurfaceAudioDesc
    {
        // 足音の組の名前（GR75）。
        Container::String FootstepSet;
        // 足音の大きさ（聴覚 GR24）。
        float NoiseLevelDb = 0.0f;
        // 遮蔽の損失（GR116）。
        float TransmissionLossDb = 0.0f;
        // audio に transmissionLossDb の鍵があれば true。false のときは親の表面か音の評価器の既定の壁の損失を使う（GR116）。
        bool bHasTransmissionLossDb = false;
    };

    struct SurfaceVisualDesc
    {
        // 足跡の効果の名前（GR59。空は無し）。
        Container::String Footprint;
        // 水しぶきの効果の名前（GR59。空は無し）。
        Container::String Splash;
        // 波紋の効果の名前（GR59。空は無し）。
        Container::String Ripple;
        // 草の踏み倒しの強さ 0..1（GR44）。
        float GrassTrample = 0.0f;
        // 毛へ移る濡れ 0..1（GR35/GR36）。
        float WetFurTransfer = 0.0f;
    };

    struct SurfacePhysicalDesc
    {
        // 将来の物理用の摩擦係数。
        float Friction = 0.6f;
        // 将来の物理用の反発係数。
        float Restitution = 0.0f;
        // 足 IK の沈み込み（GR14）。
        float FootSinkCm = 0.0f;
    };

    struct SurfaceDesc
    {
        // 安定名（保存・スクリプトの鍵）。
        Container::String Name;
        // フォールバックの親。根は Default、Default 自身の Parent も Default。
        SurfaceId Parent;
        SurfaceFlags Flags = SurfaceFlags::None;
        SurfaceAudioDesc Audio;
        SurfaceVisualDesc Visual;
        SurfacePhysicalDesc Physical;
        // 草の場の密度がこの値を超えたら、この表面を草の表面として選ぶ（GR42。草の provider が引く対象の表面だけで使う）。
        float VegetationDensityThreshold = 0.35f;
    };

    enum class SurfaceQueryFlags : uint8_t
    {
        None = 0,
        // コライダーの明示タグを引かない。
        SkipCollider = 1 << 0,
        // 地形・草などの provider を引かない。
        SkipProviders = 1 << 1
    };

    constexpr SurfaceQueryFlags operator|(SurfaceQueryFlags left, SurfaceQueryFlags right)
    {
        return static_cast<SurfaceQueryFlags>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr SurfaceQueryFlags operator&(SurfaceQueryFlags left, SurfaceQueryFlags right)
    {
        return static_cast<SurfaceQueryFlags>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    constexpr bool HasAnySurfaceQueryFlag(SurfaceQueryFlags value, SurfaceQueryFlags test)
    {
        return (value & test) != SurfaceQueryFlags::None;
    }

    struct SurfaceQuery
    {
        Math::Vector3 Position;
        Math::Vector3 Normal{0.0f, 1.0f, 0.0f};
        // 当たったコライダー（無ければ無効）。
        ColliderHandle Collider;
        // 足の大きさなどの範囲（0 は点）。
        float Radius = 0.0f;
        SurfaceQueryFlags Flags = SurfaceQueryFlags::None;
    };

    // Primary の重みは 1 - SecondaryWeight で、2 層の重みの和は 1。
    // SecondaryWeight は 0..0.5。Secondary が無いときは Default と 0。
    // 濡れ・水たまり・草の密度は、表面の種別ではなく状態として扱う。
    struct SurfaceSample
    {
        SurfaceId Primary;
        SurfaceId Secondary;
        float SecondaryWeight = 0.0f;
        float Wetness01 = 0.0f;
        float PuddleDepthM = 0.0f;
        float VegetationDensity01 = 0.0f;
        // 明示タグで表面が決まったときのコライダー。
        ColliderHandle Collider;
    };
} // namespace NorvesLib::Core::Scene
