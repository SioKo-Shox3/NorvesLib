#pragma once

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
        float _pad2;
        float colorFilter[4];
        float contrast;
        float saturation;
        float brightness;
        float temperature;
    };

} // namespace NorvesLib::Core::Rendering

