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
        float lightView[4][16];
        float lightProjection[4][16];
        float shadowSplitDistances[8];
        uint32_t cascadeCount;
        uint32_t lightCount;
        uint32_t bShadowEnabled;
        uint32_t prefilteredSpecularMipLevels;
        uint32_t bIBLEnabled;
        uint32_t bSSAOEnabled;
        uint32_t bNeuralBRDFEnabled;
        uint32_t debugViewMode;
        float preExposure;
        uint32_t shadowPadding0;
        uint32_t shadowPadding1;
        uint32_t shadowPadding2;
        float skySunDirectionAndCosRadius[4];
    };

    struct GPULightData
    {
        float position[4];
        float direction[4];
        float chromaticityAndIntensity[4];
        float attenuation[4];
    };

    static_assert(sizeof(GPULightingParams) == 704);
    static_assert(offsetof(GPULightingParams, lightView) == 96);
    static_assert(offsetof(GPULightingParams, lightProjection) == 352);
    static_assert(offsetof(GPULightingParams, shadowSplitDistances) == 608);
    static_assert(offsetof(GPULightingParams, cascadeCount) == 640);
    static_assert(offsetof(GPULightingParams, prefilteredSpecularMipLevels) == 652);
    static_assert(offsetof(GPULightingParams, preExposure) == 672);
    static_assert(offsetof(GPULightingParams, skySunDirectionAndCosRadius) == 688);
    static_assert(sizeof(GPULightData) == 64);
    static_assert(offsetof(GPULightData, position) == 0);
    static_assert(offsetof(GPULightData, direction) == 16);
    static_assert(offsetof(GPULightData, chromaticityAndIntensity) == 32);
    static_assert(offsetof(GPULightData, attenuation) == 48);

} // namespace NorvesLib::Core::Rendering
