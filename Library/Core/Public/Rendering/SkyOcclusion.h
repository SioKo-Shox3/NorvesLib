// 空からの光を遮る設定と、遮蔽物の鉛直区間を表す。
#pragma once

#include <cstdint>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 屋外の空の遮蔽に使う値設定。距離と高さの単位はメートル。
     */
    struct SkyOcclusionParameters
    {
        bool bEnabled = false;
        float Strength = 1.0f;
        float DiffuseExponent = 1.0f;
        float SpecularStrength = 1.0f;
        float OccluderBounceColorR = 0.25f;
        float OccluderBounceColorG = 0.30f;
        float OccluderBounceColorB = 0.20f;
        float OccluderBounceIntensity = 0.15f;
        float ReceiverHeightMeters = 0.25f;
        float GroundHeightMeters = 0.0f;
        std::uint32_t LevelCount = 3u;
        float BaseTexelMeters = 0.5f;
        std::uint32_t Resolution = 256u;
        float MaxHorizonMeters = 96.0f;
        bool bUseCenterOverride = false;
        float CenterOverrideX = 0.0f;
        float CenterOverrideZ = 0.0f;
    };

    static_assert(std::is_trivially_copyable_v<SkyOcclusionParameters>);
    static_assert(std::is_standard_layout_v<SkyOcclusionParameters>);

    SkyOcclusionParameters MakeDefaultSkyOcclusionParameters();
    SkyOcclusionParameters SanitizeSkyOcclusionParameters(
        const SkyOcclusionParameters& parameters);
    bool AreEqualSkyOcclusionParameters(
        const SkyOcclusionParameters& lhs,
        const SkyOcclusionParameters& rhs);

    inline constexpr std::uint32_t SkyOccluderShapeBoxFlag = 1u << 0u;
    inline constexpr std::uint32_t SkyOccluderClassCanopyFlag = 1u << 1u;
    inline constexpr std::uint32_t SkyOccluderDynamicFlag = 1u << 2u;
    inline constexpr std::uint32_t SkyOccluderBlocksRainFlag = 1u << 3u;
    inline constexpr std::uint32_t SkyOccluderKnownFlags =
        SkyOccluderShapeBoxFlag | SkyOccluderClassCanopyFlag |
        SkyOccluderDynamicFlag | SkyOccluderBlocksRainFlag;

    enum class SkyOccluderShape : std::uint32_t
    {
        Ellipsoid = 0u,
        Box = 1u,
    };

    enum class SkyOccluderClass : std::uint32_t
    {
        Solid = 0u,
        Canopy = 1u,
    };

    /**
     * @brief 空や雨の遮蔽物。距離の単位はメートル、Yaw はラジアン。
     *
     * 予約欄は GPU に渡すときもゼロとし、要素の大きさを 64 バイトに保つ。
     */
    struct SkyOccluderProxy
    {
        float CenterX = 0.0f;
        float CenterY = 0.0f;
        float CenterZ = 0.0f;
        float Yaw = 0.0f;
        float HalfExtentX = 1.0f;
        float HalfExtentY = 1.0f;
        float HalfExtentZ = 1.0f;
        float BaseOffset = 0.0f;
        float Opacity = 1.0f;
        std::uint32_t Flags = 0u;
        std::uint32_t Reserved[6] = {};
    };

    static_assert(sizeof(SkyOccluderProxy) == 64u);
    static_assert(std::is_trivially_copyable_v<SkyOccluderProxy>);
    static_assert(std::is_standard_layout_v<SkyOccluderProxy>);

    constexpr SkyOccluderShape GetSkyOccluderShape(const SkyOccluderProxy& proxy)
    {
        return (proxy.Flags & SkyOccluderShapeBoxFlag) != 0u
            ? SkyOccluderShape::Box : SkyOccluderShape::Ellipsoid;
    }

    constexpr void SetSkyOccluderShape(SkyOccluderProxy& proxy, SkyOccluderShape shape)
    {
        proxy.Flags = (proxy.Flags & ~SkyOccluderShapeBoxFlag) |
            (shape == SkyOccluderShape::Box ? SkyOccluderShapeBoxFlag : 0u);
    }

    constexpr SkyOccluderClass GetSkyOccluderClass(const SkyOccluderProxy& proxy)
    {
        return (proxy.Flags & SkyOccluderClassCanopyFlag) != 0u
            ? SkyOccluderClass::Canopy : SkyOccluderClass::Solid;
    }

    constexpr void SetSkyOccluderClass(SkyOccluderProxy& proxy, SkyOccluderClass occluderClass)
    {
        proxy.Flags = (proxy.Flags & ~SkyOccluderClassCanopyFlag) |
            (occluderClass == SkyOccluderClass::Canopy ? SkyOccluderClassCanopyFlag : 0u);
    }

    constexpr bool IsSkyOccluderDynamic(const SkyOccluderProxy& proxy)
    {
        return (proxy.Flags & SkyOccluderDynamicFlag) != 0u;
    }

    constexpr void SetSkyOccluderDynamic(SkyOccluderProxy& proxy, bool bDynamic)
    {
        proxy.Flags = (proxy.Flags & ~SkyOccluderDynamicFlag) |
            (bDynamic ? SkyOccluderDynamicFlag : 0u);
    }

    constexpr bool DoesSkyOccluderBlockRain(const SkyOccluderProxy& proxy)
    {
        return (proxy.Flags & SkyOccluderBlocksRainFlag) != 0u;
    }

    constexpr void SetSkyOccluderBlocksRain(SkyOccluderProxy& proxy, bool bBlocksRain)
    {
        proxy.Flags = (proxy.Flags & ~SkyOccluderBlocksRainFlag) |
            (bBlocksRain ? SkyOccluderBlocksRainFlag : 0u);
    }

    SkyOccluderProxy MakeDefaultSkyOccluderProxy();
    bool IsValidSkyOccluderProxy(const SkyOccluderProxy& proxy);

    /**
     * @brief 有限な寸法と遮蔽率を範囲に収め、未定義の印と予約欄を消す。
     *
     * 非有限の位置と Yaw は既定値に戻す。その他の NaN は残すため、
     * 利用前に IsValidSkyOccluderProxy で無効な遮蔽物を判定する。
     */
    SkyOccluderProxy SanitizeSkyOccluderProxy(const SkyOccluderProxy& proxy);
    bool AreEqualSkyOccluderProxy(const SkyOccluderProxy& lhs, const SkyOccluderProxy& rhs);

    /**
     * @brief ワールドの (worldX, worldZ) を通る鉛直線と遮蔽物の交差区間を求める。
     *
     * 局所 +X 軸はワールドで (cos(Yaw), 0, -sin(Yaw))、局所 +Z 軸は
     * (sin(Yaw), 0, cos(Yaw))。中心からの水平差 (dx, dz) に逆回転を掛け、
     * lx = cos(Yaw) * dx - sin(Yaw) * dz、
     * lz = sin(Yaw) * dx + cos(Yaw) * dz とする。
     * 楕円体は q = 1 - (lx / rx)^2 - (lz / rz)^2 が負なら交差せず、
     * 底と頂は cy - ry * sqrt(q)、cy + ry * sqrt(q)。
     * 箱は |lx| <= rx かつ |lz| <= rz のとき、底と頂が cy - ry、cy + ry。
     * BaseOffset > 0 なら底を max(底, cy - ry + BaseOffset) に切り上げ、
     * 底が頂を超えたら交差しない。交差しない場合は出力を変更しない。
     */
    bool EvaluateSkyOccluderColumn(
        const SkyOccluderProxy& proxy,
        float worldX,
        float worldZ,
        float& outBottomY,
        float& outTopY);
} // namespace NorvesLib::Core::Rendering
