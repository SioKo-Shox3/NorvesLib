#pragma once

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    struct GPULightingParams
    {
        float invViewProjection[16];
        float cameraPosition[4];
        float ambientColor[4];
        float lightView[16];
        float lightProjection[16];
        uint32_t lightCount;
        uint32_t bShadowEnabled;
        uint32_t envMapMipLevels;
        uint32_t bIBLEnabled;
        uint32_t bSSAOEnabled;
        uint32_t bNeuralBRDFEnabled;
        uint32_t debugViewMode;
        float preExposure;
    };

    struct GPULightData
    {
        float position[4];
        float direction[4];
        float chromaticityAndIntensity[4];
        float attenuation[4];
    };

    static_assert(sizeof(GPULightData) == 64);
    static_assert(offsetof(GPULightData, position) == 0);
    static_assert(offsetof(GPULightData, direction) == 16);
    static_assert(offsetof(GPULightData, chromaticityAndIntensity) == 32);
    static_assert(offsetof(GPULightData, attenuation) == 48);

} // namespace NorvesLib::Core::Rendering
