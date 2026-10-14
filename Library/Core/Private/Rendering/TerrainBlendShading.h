#pragma once

// 地形の 3 層の混ぜ合わせ（MegaGeometry::MegaTerrainBlend）を、シェーダーの TerrainBlendParams（vec4 × 4。
// Assets/Shaders/Common/TerrainLayerBlend.glsl）へ詰める。MegaGeometryPass（ハードのラスタ）と
// VisibilityResolvePass（可視バッファの解決）が同じ並びで使う。

#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief TerrainBlendParams の float の数（vec4 × 4） */
    constexpr uint32_t TERRAIN_BLEND_PARAM_FLOATS = 16u;
    /** @brief layers.w のビット（法線が 2 チャンネル（BC5）の層） */
    constexpr uint32_t TERRAIN_BLEND_DIRT_NORMAL_TWO_CHANNEL = 1u;
    constexpr uint32_t TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL = 2u;

    /**
     * @brief 混ぜ方の値をシェーダーの並びへ詰める
     *
     * bEnabled が偽なら layers.x を 0 にする（シェーダーは 1 層の材質として描く）。角度はラジアン、波長は逆数にする。
     * 並び: slope = 土の始まり・終わり・岩肌の始まり・終わり、height = 土へ寄せる高さの始まり・終わり・寄せの最大・混ぜの深さ、
     * noise = 2 つの波長の逆数・振幅・砂の高さ、layers = 有効・土の UV の倍率・岩肌の UV の倍率・法線のビット。
     */
    inline void PackTerrainBlendParams(const MegaGeometry::MegaTerrainBlend &blend, bool bEnabled,
                                       float out[TERRAIN_BLEND_PARAM_FLOATS])
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
                              (blend.Rock.bNormalTwoChannel ? TERRAIN_BLEND_ROCK_NORMAL_TWO_CHANNEL : 0u);
        out[15] = static_cast<float>(bits);
    }
} // namespace NorvesLib::Core::Rendering
