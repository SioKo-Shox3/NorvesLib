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
        float _pad2;
        float colorFilter[4];
        float contrast;
        float saturation;
        float brightness;
        float temperature;
    };

    // tonemapping.frag の uniform ブロック（std140）と同じ配置でなければならない。
    static_assert(sizeof(GPUToneMappingParams) == 64);
    static_assert(offsetof(GPUToneMappingParams, operatorType) == 0);
    static_assert(offsetof(GPUToneMappingParams, bBypass) == 4);
    static_assert(offsetof(GPUToneMappingParams, filmGrainSeed) == 8);
    static_assert(offsetof(GPUToneMappingParams, vignetteIntensity) == 12);
    static_assert(offsetof(GPUToneMappingParams, vignetteRadius) == 16);
    static_assert(offsetof(GPUToneMappingParams, vignetteSoftness) == 20);
    static_assert(offsetof(GPUToneMappingParams, filmGrainStrength) == 24);
    static_assert(offsetof(GPUToneMappingParams, _pad2) == 28);
    static_assert(offsetof(GPUToneMappingParams, colorFilter) == 32);
    static_assert(offsetof(GPUToneMappingParams, contrast) == 48);
    static_assert(offsetof(GPUToneMappingParams, saturation) == 52);
    static_assert(offsetof(GPUToneMappingParams, brightness) == 56);
    static_assert(offsetof(GPUToneMappingParams, temperature) == 60);

} // namespace NorvesLib::Core::Rendering

