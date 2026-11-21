#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    struct GPUToneMappingParams
    {
        uint32_t operatorType;
        uint32_t bBypass;
        // フィルムグレインのフレームごとのseed（seedとフレーム番号から決まる）
        uint32_t filmGrainSeed;
        float vignetteIntensity;
        float vignetteRadius;
        float vignetteSoftness;
        // フィルムグレインの強さ（sRGBの符号化値での標準偏差。0でオフ）
        float filmGrainStrength;
        // グレーディングの式（0: View の設定の式、1: カメラの差し替えの式、2: カメラのポストプロセスのプロファイル（Grade・Look 2 枚・ディザ）。tonemapping.frag と同じ値）
        uint32_t gradingMode;
        float colorFilter[4];
        float contrast;
        float saturation;
        float brightness;
        float temperature;
        // カメラの差し替えの式のコントラストの軸（表示のリニア値）
        float contrastPivot;
        // グレーディング用の見た目の3D LUT（binding 3）の混ぜ具合（0で掛けない）
        float lookLutIntensity;
        float _pad4;
        float _pad5;
        float gradeExposureScale;             // 露出倍率（2^ExposureBias）
        float gradeContrast;                  // log2 空間のコントラスト（1 で変えない）
        float gradeContrastPivot;             // 露出後のシーンリニアの支点（既定 0.18）
        float gradeSaturation;                // 彩度（1 で変えない）
        float gradeWhiteBalanceGain[4];       // rgb は CPU が求めたホワイトバランスの倍率。w は未使用
        float gradeShadowTint[4];             // rgb は暗部に掛ける色。w は未使用
        float gradeHighlightTint[4];          // rgb は明部に掛ける色。w は未使用
        float gradeShadowLiftFloor;           // 黒の持ち上げ先（CPU が Amount · Pivot · 2^-RangeEV を入れる。0 で無効）
        float gradeHighlightRolloffStart;     // ハイライトの肩が始まるシーンリニアの輝度
        float gradeHighlightRolloffStrength;  // ハイライトの肩の強さ（0 で無効）
        float gradeProtectedHueKeep;          // 色相の保護率（無効のときは CPU が 0 を入れる）
        float gradeProtectedHueCenterDeg;     // 保護する色相の中心角（0 度以上 360 度未満）
        float gradeProtectedHueHalfWidthDeg;  // 保護する色相の半幅（度。0 以下で無効）
        float lookLutWeightA;                 // binding 3 の LUT の重み
        float lookLutWeightB;                 // binding 4 の LUT の重み
        float ditherStrength;                 // sRGB の符号化値でのディザの振幅（0 で無効）
        uint32_t ditherSeed;                  // フレームごとのディザのシード
        float _pad6;                          // 末尾の詰め物（未使用）
        float _pad7;                          // 末尾の詰め物（未使用）
    };

    // tonemapping.frag の uniform ブロック（std140）と同じ配置でなければならない。
    static_assert(sizeof(GPUToneMappingParams) == 192);
    static_assert(offsetof(GPUToneMappingParams, operatorType) == 0);
    static_assert(offsetof(GPUToneMappingParams, bBypass) == 4);
    static_assert(offsetof(GPUToneMappingParams, filmGrainSeed) == 8);
    static_assert(offsetof(GPUToneMappingParams, vignetteIntensity) == 12);
    static_assert(offsetof(GPUToneMappingParams, vignetteRadius) == 16);
    static_assert(offsetof(GPUToneMappingParams, vignetteSoftness) == 20);
    static_assert(offsetof(GPUToneMappingParams, filmGrainStrength) == 24);
    static_assert(offsetof(GPUToneMappingParams, gradingMode) == 28);
    static_assert(offsetof(GPUToneMappingParams, colorFilter) == 32);
    static_assert(offsetof(GPUToneMappingParams, contrast) == 48);
    static_assert(offsetof(GPUToneMappingParams, saturation) == 52);
    static_assert(offsetof(GPUToneMappingParams, brightness) == 56);
    static_assert(offsetof(GPUToneMappingParams, temperature) == 60);
    static_assert(offsetof(GPUToneMappingParams, contrastPivot) == 64);
    static_assert(offsetof(GPUToneMappingParams, lookLutIntensity) == 68);
    static_assert(offsetof(GPUToneMappingParams, _pad4) == 72);
    static_assert(offsetof(GPUToneMappingParams, _pad5) == 76);
    static_assert(offsetof(GPUToneMappingParams, gradeExposureScale) == 80);
    static_assert(offsetof(GPUToneMappingParams, gradeContrast) == 84);
    static_assert(offsetof(GPUToneMappingParams, gradeContrastPivot) == 88);
    static_assert(offsetof(GPUToneMappingParams, gradeSaturation) == 92);
    static_assert(offsetof(GPUToneMappingParams, gradeWhiteBalanceGain) == 96);
    static_assert(offsetof(GPUToneMappingParams, gradeShadowTint) == 112);
    static_assert(offsetof(GPUToneMappingParams, gradeHighlightTint) == 128);
    static_assert(offsetof(GPUToneMappingParams, gradeShadowLiftFloor) == 144);
    static_assert(offsetof(GPUToneMappingParams, gradeHighlightRolloffStart) == 148);
    static_assert(offsetof(GPUToneMappingParams, gradeHighlightRolloffStrength) == 152);
    static_assert(offsetof(GPUToneMappingParams, gradeProtectedHueKeep) == 156);
    static_assert(offsetof(GPUToneMappingParams, gradeProtectedHueCenterDeg) == 160);
    static_assert(offsetof(GPUToneMappingParams, gradeProtectedHueHalfWidthDeg) == 164);
    static_assert(offsetof(GPUToneMappingParams, lookLutWeightA) == 168);
    static_assert(offsetof(GPUToneMappingParams, lookLutWeightB) == 172);
    static_assert(offsetof(GPUToneMappingParams, ditherStrength) == 176);
    static_assert(offsetof(GPUToneMappingParams, ditherSeed) == 180);
    static_assert(offsetof(GPUToneMappingParams, _pad6) == 184);
    static_assert(offsetof(GPUToneMappingParams, _pad7) == 188);

} // namespace NorvesLib::Core::Rendering

