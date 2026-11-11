#pragma once

// 地形の 3 層の混ぜ合わせ（MegaGeometry::MegaTerrainBlend）を、シェーダーの TerrainBlendParams（vec4 × 10。
// Assets/Shaders/Common/TerrainLayerBlend.glsl）へ詰める。MegaGeometryPass（ハードのラスタ）と
// VisibilityResolvePass（可視バッファの解決）が同じ並びで使う。
// スプラットの並び（TerrainSplatParams。vec4 × 12）と束縛の番号もここに置き、ラスタ・可視バッファ・RTGI で共有する。

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "RHI/ITexture.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace NorvesLib::Core::Rendering
{
    /** @brief TerrainBlendParams の float の数（vec4 × 10） */
    constexpr uint32_t TERRAIN_BLEND_PARAM_FLOATS = 40u;
    /** @brief layers.w のビット（法線が 2 チャンネル（BC5）の層） */
    constexpr uint32_t TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL = 1u;
    constexpr uint32_t TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL = 2u;
    /** @brief layers.w のビット（アルベドが sRGB の値のまま UNORM の形式で張られ、シェーダーで線形へ戻す層） */
    constexpr uint32_t TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE = 4u;
    constexpr uint32_t TERRAIN_BLEND_DIRT_ALBEDO_SRGB_DECODE = 8u;
    constexpr uint32_t TERRAIN_BLEND_ROCK_ALBEDO_SRGB_DECODE = 16u;

    constexpr uint32_t TERRAIN_SPLAT_PARAM_FLOATS = 48u;
    constexpr uint32_t TERRAIN_SPLAT_FLAG_NORMAL_TWO_CHANNEL = 1u;
    constexpr uint32_t TERRAIN_SPLAT_FLAG_ALBEDO_SRGB_DECODE = 2u;
    // MegaGeometry のハードのラスタ（megageometry.frag、set 0）
    constexpr uint32_t TERRAIN_SPLAT_MEGA_ALBEDO_BINDING = 18u; // sampler2DArray
    constexpr uint32_t TERRAIN_SPLAT_MEGA_NORMAL_BINDING = 19u; // sampler2DArray
    constexpr uint32_t TERRAIN_SPLAT_MEGA_ARM_BINDING = 20u;    // sampler2DArray
    constexpr uint32_t TERRAIN_SPLAT_MEGA_SPLAT_BINDING = 21u;  // sampler2DArray
    constexpr uint32_t TERRAIN_SPLAT_MEGA_PARAMS_BINDING = 23u; // UBO（std140。vec4 terrainSplat[12]）
    // 可視バッファの解決（ResolveTileParams の terrain[10] の後ろに vec4 terrainSplat[12]）
    constexpr uint32_t TERRAIN_SPLAT_VIS_ALBEDO_BINDING = 32u;
    constexpr uint32_t TERRAIN_SPLAT_VIS_NORMAL_BINDING = 33u;
    constexpr uint32_t TERRAIN_SPLAT_VIS_ARM_BINDING = 34u;
    constexpr uint32_t TERRAIN_SPLAT_VIS_SPLAT_BINDING = 35u;
    // RTGI（RTGIComputeParameters の foliageOcclusion の後ろに vec4 terrainSplat[12]）
    constexpr uint32_t TERRAIN_SPLAT_RTGI_ALBEDO_BINDING = 30u;
    constexpr uint32_t TERRAIN_SPLAT_RTGI_SPLAT_BINDING = 31u;

    /**
     * @brief アルベドのテクスチャを、標本の後にシェーダーで sRGB から線形へ戻すか
     *
     * アルベドの画像は sRGB で符号化されている。8 bit の UNORM の形式で張られている（Game はクック済みの sRGB を
     * 元画像と同じ標本値にするため UNORM で上げる）と、標本は符号化した値のままで、暗い成分ほど明るく（青で 2〜3 倍）なる。
     * sRGB の形式ならハードが戻すので何もしない。
     */
    inline bool IsTerrainAlbedoSrgbDecodeNeeded(const RHI::ITexture *texture)
    {
        if (texture == nullptr)
        {
            return false;
        }
        switch (texture->GetFormat())
        {
        case RHI::Format::R8G8B8A8_UNORM:
        case RHI::Format::B8G8R8A8_UNORM:
        case RHI::Format::BC1_UNORM:
        case RHI::Format::BC7_UNORM:
            return true;
        default:
            return false;
        }
    }

    /** @brief 3 層のアルベドのテクスチャ（砂・土・岩肌）から、シェーダーで線形へ戻す層のビットを求める */
    inline uint32_t ResolveTerrainAlbedoDecodeBits(const RHI::ITexture *sand, const RHI::ITexture *dirt,
                                                   const RHI::ITexture *rock)
    {
        return (IsTerrainAlbedoSrgbDecodeNeeded(sand) ? TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE : 0u) |
               (IsTerrainAlbedoSrgbDecodeNeeded(dirt) ? TERRAIN_BLEND_DIRT_ALBEDO_SRGB_DECODE : 0u) |
               (IsTerrainAlbedoSrgbDecodeNeeded(rock) ? TERRAIN_BLEND_ROCK_ALBEDO_SRGB_DECODE : 0u);
    }

    /**
     * @brief 地形の 3 層の重みを色で描く確認用の表示を求められているか（環境変数 NORVES_TERRAIN_LAYER_COLORS が 1）
     *
     * 地表の分類の地図の写り方と、LOD の段で分類が動かないことを撮影で確かめるためのもの。アルベドを岩肌 = 赤・土 = 緑・
     * 砂 = 青の重みに置き換える（光と法線はそのまま）。プロセスの間は変わらないので、最初の 1 回だけ読む。
     */
    inline bool IsTerrainLayerDebugColorsRequested()
    {
        static const bool bRequested = []
        {
            const char *value = std::getenv("NORVES_TERRAIN_LAYER_COLORS");
            return value != nullptr && value[0] == '1' && value[1] == '\0';
        }();
        return bRequested;
    }

    /**
     * @brief 混ぜ方の値をシェーダーの並びへ詰める
     *
     * albedoDecodeBits は ResolveTerrainAlbedoDecodeBits の値（layers.w の法線のビットに足す）。
     * bEnabled が偽なら layers.x を 0 にする（シェーダーは 1 層の材質として描く）。bSurfaceMask が偽（地図のテクスチャが
     * 引けない）なら mask を 0 にする（シェーダーは傾斜の式だけで混ぜる）。bFarMap が偽（遠景の地図が引けない）なら farMap を
     * 0 にする（遠くでも画素ごとの式で描く）。角度はラジアン、波長は逆数にする。
     * 並び: slope = 土の始まり・終わり・岩肌の始まり・終わり、height = 土へ寄せる高さの始まり・終わり・寄せの最大・混ぜの深さ、
     * noise = 2 つの波長の逆数・振幅・砂の高さ、layers = 有効・土の UV の倍率・岩肌の UV の倍率・法線とアルベドの戻しのビット、
     * macro = 揺らぎの 2 つの波長の逆数・明るさの振幅・色味の振幅、detail = 変え始める距離・距離の比の log2・段の上限・
     * テクスチャの 1 枚の広さ、mask = 地図の外縁の X・Z の最小・X・Z の広さの逆数（z が 0 なら地図なし）、
     * maskMix = 礫を土へ回す割合・外縁の帯の幅（m）・引く位置のずらしの幅（m）・確認用の色分け（1/0）、
     * farMap = 遠景の地図の外縁の X・Z の最小・X・Z の広さの逆数（z が 0 なら地図なし）、farMix = 地図の 1 画素の広さ（m）・
     * 砂・土・岩肌の法線マップの傾きの分散。
     */
    inline void PackTerrainBlendParams(const MegaGeometry::MegaTerrainBlend &blend, bool bEnabled, bool bSurfaceMask,
                                       bool bFarMap, uint32_t albedoDecodeBits, float out[TERRAIN_BLEND_PARAM_FLOATS])
    {
        constexpr float DegreesToRadians = 3.14159265358979f / 180.0f;
        // smoothstep の両端が同じだと未定義なので、終わりを始まりより少し先へずらす
        const auto range = [](float start, float end, float *outPair)
        {
            outPair[0] = start;
            outPair[1] = end > start + 1.0e-4f ? end : start + 1.0e-4f;
        };
        range(blend.DirtSlopeDegrees[0] * DegreesToRadians, blend.DirtSlopeDegrees[1] * DegreesToRadians, &out[0]);
        range(blend.RockSlopeDegrees[0] * DegreesToRadians, blend.RockSlopeDegrees[1] * DegreesToRadians, &out[2]);
        range(blend.DirtHeightRange[0], blend.DirtHeightRange[1], &out[4]);
        out[6] = blend.DirtHeightBiasDegrees * DegreesToRadians;
        out[7] = blend.BlendDepth > 1.0e-3f ? blend.BlendDepth : 1.0e-3f;
        out[8] = blend.NoiseWavelengths[0] > 0.0f ? 1.0f / blend.NoiseWavelengths[0] : 0.0f;
        out[9] = blend.NoiseWavelengths[1] > 0.0f ? 1.0f / blend.NoiseWavelengths[1] : 0.0f;
        out[10] = blend.NoiseAmplitudeDegrees * DegreesToRadians;
        out[11] = blend.SandFillHeight;
        out[12] = bEnabled ? 1.0f : 0.0f;
        out[13] = blend.Dirt.UVScale;
        out[14] = blend.Rock.UVScale;
        const uint32_t bits = (blend.Dirt.bNormalTwoChannel ? TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL : 0u) |
                              (blend.Rock.bNormalTwoChannel ? TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL : 0u) |
                              albedoDecodeBits;
        out[15] = static_cast<float>(bits);
        out[16] = blend.MacroWavelengths[0] > 0.0f ? 1.0f / blend.MacroWavelengths[0] : 0.0f;
        out[17] = blend.MacroWavelengths[1] > 0.0f ? 1.0f / blend.MacroWavelengths[1] : 0.0f;
        out[18] = blend.MacroBrightness;
        out[19] = blend.MacroHue;
        out[20] = blend.DetailStartDistance > 1.0e-3f ? blend.DetailStartDistance : 1.0e-3f;
        // 比が 1 以下だと段が上がらないので、少しだけ 1 より大きくする
        out[21] = std::log2(blend.DetailDistanceRatio > 1.01f ? blend.DetailDistanceRatio : 1.01f);
        out[22] = blend.DetailMaxLevel > 0.0f ? blend.DetailMaxLevel : 0.0f;
        out[23] = blend.TextureTileMeters > 1.0e-3f ? blend.TextureTileMeters : 1.0e-3f;
        const float maskWidth = blend.SurfaceMaskMaxXZ[0] - blend.SurfaceMaskMinXZ[0];
        const float maskDepth = blend.SurfaceMaskMaxXZ[1] - blend.SurfaceMaskMinXZ[1];
        const bool bMask = bEnabled && bSurfaceMask && maskWidth > 1.0e-3f && maskDepth > 1.0e-3f;
        out[24] = bMask ? blend.SurfaceMaskMinXZ[0] : 0.0f;
        out[25] = bMask ? blend.SurfaceMaskMinXZ[1] : 0.0f;
        out[26] = bMask ? 1.0f / maskWidth : 0.0f;
        out[27] = bMask ? 1.0f / maskDepth : 0.0f;
        out[28] = blend.SurfaceMaskGravelToDirt < 0.0f ? 0.0f
                                                       : (blend.SurfaceMaskGravelToDirt > 1.0f ? 1.0f : blend.SurfaceMaskGravelToDirt);
        out[29] = blend.SurfaceMaskEdgeFadeMeters > 1.0e-3f ? blend.SurfaceMaskEdgeFadeMeters : 1.0e-3f;
        out[30] = blend.SurfaceMaskWarpMeters > 0.0f ? blend.SurfaceMaskWarpMeters : 0.0f;
        out[31] = bEnabled && IsTerrainLayerDebugColorsRequested() ? 1.0f : 0.0f;
        const float farWidth = blend.FarMapMaxXZ[0] - blend.FarMapMinXZ[0];
        const float farDepth = blend.FarMapMaxXZ[1] - blend.FarMapMinXZ[1];
        const bool bFar = bEnabled && bFarMap && farWidth > 1.0e-3f && farDepth > 1.0e-3f && blend.FarMapTexelMeters > 1.0e-3f;
        out[32] = bFar ? blend.FarMapMinXZ[0] : 0.0f;
        out[33] = bFar ? blend.FarMapMinXZ[1] : 0.0f;
        out[34] = bFar ? 1.0f / farWidth : 0.0f;
        out[35] = bFar ? 1.0f / farDepth : 0.0f;
        out[36] = bFar ? blend.FarMapTexelMeters : 0.0f;
        for (uint32_t layer = 0; layer < 3u; ++layer)
        {
            const float variance = blend.DetailSlopeVariance[layer];
            out[37 + layer] = std::isfinite(variance) && variance > 0.0f ? variance : 0.0f;
        }
    }

    /// スプラットの層の並びを詰める（並びは TerrainLayerBlend.glsl の TerrainSplatParams と同じ。vec4 × 12）
    /// bEnabled が偽（テクスチャが引けない）なら 48 個とも 0。flagBits は TERRAIN_SPLAT_FLAG_* の和。wetness は 0..1 へ丸める
    inline void PackTerrainSplatParams(const MegaGeometry::MegaTerrainSplat& splat, bool bEnabled, uint32_t flagBits,
                                      float wetness, float out[TERRAIN_SPLAT_PARAM_FLOATS])
    {
        for (uint32_t index = 0; index < TERRAIN_SPLAT_PARAM_FLOATS; ++index)
        {
            out[index] = 0.0f;
        }
        const float width = splat.SplatMaxXZ[0] - splat.SplatMinXZ[0];
        const float depth = splat.SplatMaxXZ[1] - splat.SplatMinXZ[1];
        if (!bEnabled || !splat.bEnabled || splat.LayerCount == 0u ||
            !(width > 1.0e-3f) || !(depth > 1.0e-3f))
        {
            return;
        }

        constexpr float DegreesToRadians = 3.14159265358979f / 180.0f;
        const uint32_t layerCount = splat.LayerCount < MegaGeometry::MegaTerrainSplatMaxLayers
            ? splat.LayerCount : MegaGeometry::MegaTerrainSplatMaxLayers;
        const auto clampUnit = [](float value)
        {
            return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
        };
        out[0] = splat.SplatMinXZ[0];
        out[1] = splat.SplatMinXZ[1];
        out[2] = 1.0f / width;
        out[3] = 1.0f / depth;
        out[4] = static_cast<float>(layerCount);
        out[5] = splat.CliffLayer >= 0 && splat.CliffLayer < static_cast<int32_t>(layerCount)
            ? static_cast<float>(splat.CliffLayer) : -1.0f;
        out[6] = splat.HeightBlendDepth > 1.0e-3f ? splat.HeightBlendDepth : 1.0e-3f;
        out[7] = std::isfinite(wetness) ? clampUnit(wetness) : 0.0f;
        out[8] = splat.CliffSlopeDegrees[0] * DegreesToRadians;
        const float cliffEnd = splat.CliffSlopeDegrees[1] * DegreesToRadians;
        // smoothstep の終わりを始まり + 1e-4 以上へずらす（3 層の range と同じ）。
        out[9] = cliffEnd > out[8] + 1.0e-4f ? cliffEnd : out[8] + 1.0e-4f;
        out[10] = splat.UvRotationWavelength > 0.0f ? 1.0f / splat.UvRotationWavelength : 0.0f;
        out[11] = splat.UvRotationDegrees * DegreesToRadians;
        out[12] = static_cast<float>(flagBits);
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            const float tileMeters = splat.Layers[layer].TileMeters > 1.0e-3f
                ? splat.Layers[layer].TileMeters : 1.0e-3f;
            out[16u + layer * 4u] = 1.0f / tileMeters;
            out[17u + layer * 4u] = clampUnit(splat.Layers[layer].Wettability);
        }
    }
} // namespace NorvesLib::Core::Rendering
