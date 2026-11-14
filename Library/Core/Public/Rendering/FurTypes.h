#pragma once

#include "RenderTypes.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t FurMinLayerCount = 1u;
    inline constexpr uint32_t FurMaxLayerCount = 64u;
    inline constexpr uint32_t FurMaxRegionCount = 8u;
    inline constexpr uint32_t FurMaxLodLevelCount = 4u;

    /** @brief 距離に応じた殻数の一段。 */
    struct FurLodLevel
    {
        float StartDistanceMeters = 0.0f; ///< この距離以上で適用する距離（m、有限・0 以上、既定は 0 m）。
        uint32_t LayerCount = 0u; ///< 殻数（1〜親の殻数、既定の 0 は未使用枠）。
    };

    /** @brief 毛皮全体の殻と毛束の値。 */
    struct FurParams
    {
        uint32_t LayerCount = 16u; ///< 距離段がない場合の殻数（1〜64、既定は 16 枚）。
        float MaxLengthMeters = 0.02f; ///< 毛の最大長（m、有限・0 より大きく 1 以下、既定は 0.02 m）。
        float LengthScale = 1.0f; ///< 毛の長さの倍率（有限・0〜4、既定の 1 は不変）。
        float StrandDensity = 1024.0f; ///< UV 1 あたりのセル数（有限・0 より大きく 65536 以下、既定は 1024）。
        float ClumpSize = 0.01f; ///< 一束の UV 上の幅（有限・0 より大きく 1 以下、既定は 0.01）。
        float ClumpStrength = 0.0f; ///< 束の強さ（0〜1、既定の 0 は束ねない）。
        float RootRadius = 0.5f; ///< セル幅に対する根元の半径の割合（有限・0 より大きく 1 以下、既定は 0.5）。
        float TipRadius = 0.1f; ///< セル幅に対する毛先の半径の割合（0〜1、既定は 0.1）。
        float ThicknessExponent = 1.0f; ///< 根元から毛先への細りの指数（有限・0 より大きく 16 以下、既定は 1）。
        float UndercoatColor[3] = {0.05f, 0.04f, 0.03f}; ///< 下毛の線形 RGB（各 0〜1、既定は暗い茶色）。
        float UndercoatAlbedoMix = 0.5f; ///< 下毛色とアルベドの混合量（0〜1、既定は 0.5）。
        float AoRoot = 0.5f; ///< 根元の遮蔽の明るさ（0〜1、毛先は 1、既定は 0.5）。
        float AoExponent = 1.0f; ///< 根元から毛先への遮蔽の指数（有限・0 より大きく 16 以下、既定は 1）。
        float Tilt = 0.3f; ///< 毛の傾き（0〜1、0 は法線向き・1 は流れ向き、既定は 0.3）。
        FurLodLevel LodLevels[FurMaxLodLevelCount]; ///< 距離と殻数の段（最大 4 段、既定は全枠未使用）。
        uint32_t LodLevelCount = 0u; ///< 使用する距離段の数（0〜4、既定の 0 は常に LayerCount を使う）。
        float WetDarken = 0.0f; ///< 濡れたときの暗化量（0〜1、既定の 0 は不変）。
        float WetDesaturate = 0.0f; ///< 濡れたときの彩度低下量（0〜1、既定の 0 は不変）。
        float WetRoughness = 0.0f; ///< 濡れ切ったときの粗さの減少量（0〜1、既定の 0 は不変）。
        float WetGloss = 0.0f; ///< 濡れ切ったときの鏡面の強まり（0〜1、既定の 0 は不変）。
        float WetLengthCollapse = 0.0f; ///< 濡れ切ったときに長さが縮む割合（0〜1、既定の 0 は不変）。
        float WetThicken = 0.0f; ///< 濡れ切ったときの半径の増し分（有限・0〜4、半径に 1+値 を掛け、既定の 0 は不変）。
        float WetClump = 0.0f; ///< 濡れ切ったときに束の強さへ足す量（0〜1、既定の 0 は不変）。
        float WetSag = 0.0f; ///< 濡れ切ったときの重力による垂れ（0〜1、既定の 0 は不変）。
    };

    /** @brief 領域マップの ID に対応する値。 */
    struct FurRegion
    {
        float RootDarkness = 0.0f; ///< 根元の暗さ（0〜1、既定の 0 は暗くしない）。
        float TipBrightness = 0.0f; ///< 毛先の明るさ（0〜1、既定の 0 は明るくしない）。
        float LengthScale = 1.0f; ///< 長さの倍率（有限・0〜4、既定の 1 は不変）。
        float ThicknessScale = 1.0f; ///< 太さの倍率（有限・0 より大きく 4 以下、既定の 1 は不変）。
        float WetSusceptibility = 1.0f; ///< 濡れの応答への倍率（0〜1、既定の 1 は不変）。
        float PrimaryShift = -0.1f; ///< 一つ目の鏡面ローブの接線方向へのずれ（rad、有限・-1〜1、既定は -0.1）。
        float SecondaryShift = 0.1f; ///< 二つ目の鏡面ローブの接線方向へのずれ（rad、有限・-1〜1、既定は 0.1）。
        float PrimaryExponent = 80.0f; ///< 一つ目の鏡面ローブの指数（有限・0 より大きく 1024 以下、既定は 80）。
        float SecondaryExponent = 20.0f; ///< 二つ目の鏡面ローブの指数（有限・0 より大きく 1024 以下、既定は 20）。
    };

    /** @brief UV0 空間の線形マップ。無効なハンドルはマップがなく描画時に既定値を使う。 */
    struct FurMapTextures
    {
        TextureHandle Length; ///< R8 の長さ（値×MaxLengthMeters が実長、0 は毛なし、既定は無効）。
        TextureHandle Flow; ///< RG8 の流れ（UV 方向 v=2*rg-1、|v| は整い度、既定は無効）。
        TextureHandle DensityClump; ///< RG8 の密度と束（R は密度倍率で 0 は素肌、G は束の強さの倍率、既定は無効）。
        TextureHandle Region; ///< R8 の領域 ID（round(r*255)、補間を避け texelFetch で読み、既定は無効）。
    };

    /** @brief 材質に保持する毛皮の値。濡れ度はインスタンスの ShadingParams の P0 で持つ。 */
    struct FurMaterialData
    {
        FurParams Params; ///< 毛皮全体の値（既定は FurParams の既定値）。
        FurRegion Regions[FurMaxRegionCount]; ///< 領域ごとの値（最大 8 個、既定は各 FurRegion の既定値）。
        uint32_t RegionCount = 0u; ///< 使用する領域数（0〜8、既定の 0 は表なしで既定の FurRegion を一つ使う）。
        FurMapTextures Maps; ///< UV0 のマップ（既定は全て無効）。
    };

    /** @brief 使用中の毛皮の値が有限で指定範囲を満たすかを確かめる。 */
    inline bool IsValidFurMaterialData(const FurMaterialData& data)
    {
        const auto isInRange = [](float value, float minimum, float maximum)
        {
            return std::isfinite(value) && value >= minimum && value <= maximum;
        };
        const auto isPositiveInRange = [](float value, float maximum)
        {
            return std::isfinite(value) && value > 0.0f && value <= maximum;
        };

        const FurParams& params = data.Params;
        if (params.LayerCount < FurMinLayerCount || params.LayerCount > FurMaxLayerCount ||
            !isPositiveInRange(params.MaxLengthMeters, 1.0f) ||
            !isInRange(params.LengthScale, 0.0f, 4.0f) ||
            !isPositiveInRange(params.StrandDensity, 65536.0f) ||
            !isPositiveInRange(params.ClumpSize, 1.0f) ||
            !isInRange(params.ClumpStrength, 0.0f, 1.0f) ||
            !isPositiveInRange(params.RootRadius, 1.0f) ||
            !isInRange(params.TipRadius, 0.0f, 1.0f) ||
            !isPositiveInRange(params.ThicknessExponent, 16.0f) ||
            !isInRange(params.UndercoatAlbedoMix, 0.0f, 1.0f) ||
            !isInRange(params.AoRoot, 0.0f, 1.0f) ||
            !isPositiveInRange(params.AoExponent, 16.0f) ||
            !isInRange(params.Tilt, 0.0f, 1.0f) ||
            params.LodLevelCount > FurMaxLodLevelCount ||
            !isInRange(params.WetDarken, 0.0f, 1.0f) ||
            !isInRange(params.WetDesaturate, 0.0f, 1.0f) ||
            !isInRange(params.WetRoughness, 0.0f, 1.0f) ||
            !isInRange(params.WetGloss, 0.0f, 1.0f) ||
            !isInRange(params.WetLengthCollapse, 0.0f, 1.0f) ||
            !isInRange(params.WetThicken, 0.0f, 4.0f) ||
            !isInRange(params.WetClump, 0.0f, 1.0f) ||
            !isInRange(params.WetSag, 0.0f, 1.0f) ||
            data.RegionCount > FurMaxRegionCount)
        {
            return false;
        }

        for (float color : params.UndercoatColor)
        {
            if (!isInRange(color, 0.0f, 1.0f))
            {
                return false;
            }
        }

        for (uint32_t index = 0; index < params.LodLevelCount; ++index)
        {
            const FurLodLevel& level = params.LodLevels[index];
            if (!std::isfinite(level.StartDistanceMeters) || !(level.StartDistanceMeters >= 0.0f) ||
                level.LayerCount < FurMinLayerCount || level.LayerCount > params.LayerCount ||
                (index > 0 && (!(level.StartDistanceMeters > params.LodLevels[index - 1].StartDistanceMeters) ||
                               level.LayerCount > params.LodLevels[index - 1].LayerCount)))
            {
                return false;
            }
        }

        for (uint32_t index = 0; index < data.RegionCount; ++index)
        {
            const FurRegion& region = data.Regions[index];
            if (!isInRange(region.RootDarkness, 0.0f, 1.0f) ||
                !isInRange(region.TipBrightness, 0.0f, 1.0f) ||
                !isInRange(region.LengthScale, 0.0f, 4.0f) ||
                !isPositiveInRange(region.ThicknessScale, 4.0f) ||
                !isInRange(region.WetSusceptibility, 0.0f, 1.0f) ||
                !isInRange(region.PrimaryShift, -1.0f, 1.0f) ||
                !isInRange(region.SecondaryShift, -1.0f, 1.0f) ||
                !isPositiveInRange(region.PrimaryExponent, 1024.0f) ||
                !isPositiveInRange(region.SecondaryExponent, 1024.0f))
            {
                return false;
            }
        }
        return true;
    }

    /** @brief 距離に適用される最後の段の殻数を返す。 */
    inline uint32_t SelectFurLayerCount(const FurParams& params, float distanceMeters)
    {
        uint32_t layerCount = params.LayerCount;
        if (!std::isfinite(distanceMeters))
        {
            return layerCount;
        }

        for (uint32_t index = 0; index < params.LodLevelCount && index < FurMaxLodLevelCount; ++index)
        {
            if (distanceMeters >= params.LodLevels[index].StartDistanceMeters)
            {
                layerCount = params.LodLevels[index].LayerCount;
            }
        }
        return layerCount;
    }

    inline constexpr uint32_t FurMapMaskLength = 1u << 0;
    inline constexpr uint32_t FurMapMaskFlow = 1u << 1;
    inline constexpr uint32_t FurMapMaskDensityClump = 1u << 2;
    inline constexpr uint32_t FurMapMaskRegion = 1u << 3;

    /** @brief ファーの領域ごとの std140 描画定数。 */
    struct FurMaterialGpuRegion
    {
        float Shading[4]; ///< x: 根元の暗さ、y: 毛先の明るさ、z: 長さの倍率、w: 太さの倍率。
        float Lobes[4]; ///< x: 一つ目のローブのずれ、y: 二つ目のずれ、z: 一つ目の指数、w: 二つ目の指数。
        float Wet[4]; ///< x: 濡れの応答への倍率、yzw: 空き（0）。
    };

    /** @brief ファーの材質の std140 描画定数。 */
    struct FurMaterialGpuParams
    {
        uint32_t Counts[4]; ///< x: 殻数、y: 領域数（1〜8）、z: マップの有無の印、w: 空き（0）。
        float Growth[4]; ///< x: 毛の最大長×長さの倍率（m）、y: 密度、z: 束の幅、w: 束の強さ。
        float Strand[4]; ///< x: 根元の半径、y: 毛先の半径、z: 細りの指数、w: 毛の傾き。
        float Undercoat[4]; ///< rgb: 下毛の線形色、a: アルベドとの混合量。
        float Occlusion[4]; ///< x: 根元の遮蔽の明るさ、y: 遮蔽の指数、zw: 空き（0）。
        float WetGeometry[4]; ///< x: 長さの縮み、y: 半径の増し分、z: 束の強さ、w: 垂れ。各値は濡れ度を乗算済み。
        float WetShading[4]; ///< x: 暗化、y: 彩度低下、z: 粗さの減少、w: 鏡面の強まり。各値は濡れ度を乗算済み。
        FurMaterialGpuRegion Regions[FurMaxRegionCount]; ///< 使用領域と未使用枠の既定値。
    };

    /**
     * @brief 検査済みのファーの材質を std140 の描画定数に詰める。
     * @param fur IsValidFurMaterialData で検査済みの材質。殻数は距離による選択をせず材質の値を使う。
     * @param wetness 濡れ度。有限値は 0〜1 に収め、非有限値は 0 とする。
     * @return 濡れ度をそのまま保存せず、濡れの各応答に乗算した定数。
     * 領域数が 0 のときは既定の領域を一つ使い、8 を超えるときは 8 に収める。
     * 未使用の領域には既定値、空き成分には 0 を入れ、マップの有効性を印にする。
     */
    FurMaterialGpuParams PackFurMaterialGpuParams(const FurMaterialData& fur, float wetness);
} // namespace NorvesLib::Core::Rendering
