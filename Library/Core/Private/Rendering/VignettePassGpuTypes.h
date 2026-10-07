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
        // 画面の左右の端での R・B のずれ（入力の画素数。0で色収差なし）。ビネットの有無とは独立に掛かる。
        float chromaticAberrationPixels;
        float _pad0;
        float _pad1;
        float _pad2;
    };

    // vignette.frag の uniform ブロック（std140）と同じ配置でなければならない。
    static_assert(sizeof(GPUVignetteParams) == 32);
    static_assert(offsetof(GPUVignetteParams, intensity) == 0);
    static_assert(offsetof(GPUVignetteParams, radius) == 4);
    static_assert(offsetof(GPUVignetteParams, softness) == 8);
    static_assert(offsetof(GPUVignetteParams, bEnabled) == 12);
    static_assert(offsetof(GPUVignetteParams, chromaticAberrationPixels) == 16);

} // namespace NorvesLib::Core::Rendering
