#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    struct GPUToneMappingParams
    {
        float exposure;
        float gamma;
        uint32_t operatorType;
        uint32_t bBypass;
        float vignetteIntensity;
        float vignetteRadius;
        float vignetteSoftness;
        float _pad1;
        float colorFilter[4];
        float contrast;
        float saturation;
        float brightness;
        float temperature;
    };

} // namespace NorvesLib::Core::Rendering

