#pragma once

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
        uint32_t _pad2;
    };

} // namespace NorvesLib::Core::Rendering
