#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    struct GPUToneMappingParams
    {
        uint32_t operatorType;
        uint32_t bBypass;
        uint32_t _pad0;
        float vignetteIntensity;
        float vignetteRadius;
        float vignetteSoftness;
        float _pad1;
        float _pad2;
        float colorFilter[4];
        float contrast;
        float saturation;
        float brightness;
        float temperature;
    };

} // namespace NorvesLib::Core::Rendering

