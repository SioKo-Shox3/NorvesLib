// 空からの光を遮る設定と、遮蔽物の鉛直区間を表す。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Rendering/RenderExtension/RenderExtensionData.h"

#include <cstddef>
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

    namespace Terrain
    {
        class TerrainHeightField;
    }

    /**
     * @brief 地面の高さと下草の層で共用する、世界座標の格子の配置。
     *
     * テクセル (i, j) の中心は (OriginX + (i + 0.5) * TexelMeters,
     * OriginZ + (j + 0.5) * TexelMeters)。配列は行優先で j * Width + i、j は Z 方向。
     */
    struct SkyOcclusionFieldLayout
    {
        float OriginX = 0.0f;      // 格子の最小の角（メートル）。
        float OriginZ = 0.0f;
        float TexelMeters = 0.0f;  // テクセルの幅。
        std::uint32_t Width = 0u;  // X 方向のテクセル数。
        std::uint32_t Height = 0u; // Z 方向のテクセル数。
        std::uint32_t Reserved[3] = {};
    };

    static_assert(sizeof(SkyOcclusionFieldLayout) == 32u);
    static_assert(std::is_trivially_copyable_v<SkyOcclusionFieldLayout>);
    static_assert(std::is_standard_layout_v<SkyOcclusionFieldLayout>);

    /** @brief 有限な原点、正のテクセル幅、1〜8192 の寸法とゼロの予約欄を検査する。 */
    bool IsValidSkyOcclusionFieldLayout(const SkyOcclusionFieldLayout& layout);

    /**
     * @brief u = x * [0] + [2]、v = z * [1] + [3] の変換を求める。
     * 無効な配置なら全成分をゼロにする。
     */
    void ComputeSkyOcclusionFieldWorldToUv(
        const SkyOcclusionFieldLayout& layout, float outScaleOffset[4]);

    /**
     * @brief textureLod の線形補間と端の延長に合わせて格子を引く。
     * 範囲外、不正な配置・配列・座標、非有限な標本では fallback を返す。
     */
    float SampleSkyOcclusionField(const SkyOcclusionFieldLayout& layout,
        Container::Span<const float> values, float x, float z, float fallback);

    /** @brief 地面から下草の層の頂までの高さと、層全体の遮蔽率。 */
    struct SkyOcclusionUndergrowthTexel
    {
        float TopMeters = 0.0f; // 地面から層の頂までの高さ。
        float Kappa = 0.0f;     // 層の全体を通した遮蔽率 [0,1]。
    };

    static_assert(sizeof(SkyOcclusionUndergrowthTexel) == 8u);
    static_assert(std::is_trivially_copyable_v<SkyOcclusionUndergrowthTexel>);

    /// 下草の層全体の遮蔽率の上限。
    inline constexpr float SkyOcclusionUndergrowthMaxKappa = 0.3f;
    /// 下草の層を作る最小の高さ（メートル）。
    inline constexpr float SkyOcclusionUndergrowthMinTopMeters = 0.02f;

    /** @brief 密度と草丈から下草の層を求める。非有限な入力はゼロとして扱う。 */
    SkyOcclusionUndergrowthTexel ComputeSkyOcclusionUndergrowthLayer(
        float density, float grassHeightMeters);

    /** @brief 踏み倒しの割合で遮蔽率を減らす。非有限な割合はゼロとして扱う。 */
    float ApplySkyOcclusionTrample(float kappa, float flatten);

    /**
     * @brief 下草の高さと遮蔽率から消散係数（1/m）を求める。
     * GPU の写し込みも同じ式を使う。
     */
    float ComputeSkyOcclusionUndergrowthExtinction(float topMeters, float kappa);

    /**
     * @brief 雲量と天頂・地平線の明るさの比から、空の重みの定数項と傾きを求める。
     *
     * 一般化した CIE の曇天 L ∝ 1 + (r - 1)s（s は仰角の正弦）を、
     * 上半球のコサインの重みの平均が 1 になるよう正規化する。
     * SkyAtmosphere.cpp の ComputeOvercastZenithRadiance と同じ正規化。
     * 雲量ゼロでは一様な重み (1, 0) を返す。
     */
    void ComputeSkyOcclusionSkyWeightCoefficients(float cloudCover,
        float zenithToHorizonRatio, float& outConstant, float& outSlope);

    /** @brief 仰角の正弦に対する空の明るさの重みを評価する。 */
    float EvaluateSkyOcclusionSkyWeight(float constant, float slope, float sinElevation);

    /** @brief 散布する種の尺度 1 における遮蔽物の寸法と遮蔽率。 */
    struct SkyOccluderSpeciesDesc
    {
        bool bEnabled = false;
        SkyOccluderShape Shape = SkyOccluderShape::Ellipsoid;
        SkyOccluderClass Class = SkyOccluderClass::Solid;
        float RadiusMeters = 0.0f; // 尺度 1 の水平半径。
        float HeightMeters = 0.0f; // 尺度 1 の高さ。
        float BaseRatio = 0.0f;    // 高さに対する底の持ち上げの割合。
        float Kappa = 1.0f;        // 樹冠の遮蔽率。Solid は 1 として扱う。
    };

    /**
     * @brief 種の指定と物の配置から、動く印・雨の印を持たない遮蔽物を決定的に作る。
     * 無効な指定では false を返し、出力を変更しない。
     */
    bool MakeSkyOccluderFromInstance(const SkyOccluderSpeciesDesc& desc,
        float x, float groundY, float z, float yaw, float scale, SkyOccluderProxy& outProxy);

    /** @brief GPU の三角関数の丸め差を含む、遮蔽物の水平の外接矩形を求める。 */
    void GetSkyOccluderHorizontalBounds(const SkyOccluderProxy& proxy,
        double& outMinX, double& outMinZ, double& outMaxX, double& outMaxZ);

    /**
     * @brief 散布チャンクの遮蔽物の連続範囲と水平の外接矩形。
     *
     * 表は ChunkKey の昇順で重複なし。Offset/Count は遮蔽物の配列で前から
     * 隙間も重なりもなく並び、Count は 1 以上とする。矩形は範囲の各遮蔽物の
     * GetSkyOccluderHorizontalBounds の和を float へ外側に丸めたもの。
     */
    struct SkyOccluderChunkRange
    {
        std::uint64_t ChunkKey = 0u; // 散布の中で一意で決定的なチャンクの鍵。
        std::uint64_t Revision = 0u; // チャンクの中身を作り直すたびに増える版。
        std::uint32_t Offset = 0u;   // チャンクの遮蔽物の配列での先頭。
        std::uint32_t Count = 0u;
        float MinX = 0.0f;           // 水平の外接矩形（外側へ丸める）。
        float MinZ = 0.0f;
        float MaxX = 0.0f;
        float MaxZ = 0.0f;
    };

    static_assert(sizeof(SkyOccluderChunkRange) == 40u);
    static_assert(std::is_trivially_copyable_v<SkyOccluderChunkRange>);

    /** @brief 鍵の昇順、配列全体を覆う連続範囲、有限で整列した矩形を検査する。 */
    bool IsValidSkyOccluderChunkRanges(
        Container::Span<const SkyOccluderChunkRange> ranges, std::size_t occluderCount);

    // 配置と配列は同じ GameThread のフレームで両方出し、無くなるときは両方消す。
    // キーが無いときは地面を SkyOcclusionParameters::GroundHeightMeters の一定値とし、
    // 下草なし、チャンクの遮蔽物なしとして扱う。
    /// 地面の高さの格子の配置。
    inline constexpr RenderExtensionDataKey<SkyOcclusionFieldLayout> kSkyOcclusionGroundLayoutKey{
        Identity::Literal("SkyOcclusion.GroundLayout", sizeof("SkyOcclusion.GroundLayout") - 1)};
    /// 地面の高さの行優先配列。
    inline constexpr RenderExtensionDataKey<float> kSkyOcclusionGroundHeightsKey{
        Identity::Literal("SkyOcclusion.GroundHeights", sizeof("SkyOcclusion.GroundHeights") - 1)};
    /// 下草の層の格子の配置。
    inline constexpr RenderExtensionDataKey<SkyOcclusionFieldLayout> kSkyOcclusionUndergrowthLayoutKey{
        Identity::Literal("SkyOcclusion.UndergrowthLayout", sizeof("SkyOcclusion.UndergrowthLayout") - 1)};
    /// 下草の層の行優先配列。
    inline constexpr RenderExtensionDataKey<SkyOcclusionUndergrowthTexel> kSkyOcclusionUndergrowthKey{
        Identity::Literal("SkyOcclusion.Undergrowth", sizeof("SkyOcclusion.Undergrowth") - 1)};
    /// チャンクごとの遮蔽物の範囲の表。
    inline constexpr RenderExtensionDataKey<SkyOccluderChunkRange> kSkyOcclusionChunkRangesKey{
        Identity::Literal("SkyOcclusion.ChunkRanges", sizeof("SkyOcclusion.ChunkRanges") - 1)};
    /// チャンクの範囲の表が指す遮蔽物の配列。
    inline constexpr RenderExtensionDataKey<SkyOccluderProxy> kSkyOcclusionChunkOccludersKey{
        Identity::Literal("SkyOcclusion.ChunkOccluders", sizeof("SkyOcclusion.ChunkOccluders") - 1)};

    /**
     * @brief 地形の頂点格子の全体を覆う配置と、テクセル中心の地面の高さを作る。
     * 解像度の上限は 1〜4096 に収め、非有限な標本は有限な代替高さで置き換える。
     * 格子や出力の配置が不正なら false を返し、出力を変更しない。
     */
    bool BuildSkyOcclusionGroundHeights(const Terrain::TerrainHeightField& field,
        std::uint32_t maxResolution, float fallbackHeight, SkyOcclusionFieldLayout& outLayout,
        Container::VariableArray<float>& outHeights);
} // namespace NorvesLib::Core::Rendering
