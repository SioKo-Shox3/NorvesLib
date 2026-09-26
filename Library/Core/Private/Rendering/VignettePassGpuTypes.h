#pragma once

#include <cstddef>
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

    // vignette.frag の uniform ブロック（std140）と同じ配置でなければならない。
    static_assert(sizeof(GPUVignetteParams) == 16);
    static_assert(offsetof(GPUVignetteParams, intensity) == 0);
    static_assert(offsetof(GPUVignetteParams, radius) == 4);
    static_assert(offsetof(GPUVignetteParams, softness) == 8);
    static_assert(offsetof(GPUVignetteParams, bEnabled) == 12);

} // namespace NorvesLib::Core::Rendering
