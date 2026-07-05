#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    struct GPUVignetteParams
    {
        float intensity;
        float radius;
        float softness;
        uint32_t bEnabled;
    };

} // namespace NorvesLib::Core::Rendering
