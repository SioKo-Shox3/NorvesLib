// ファーの材質から std140 の描画定数を作る。
// 描画定数の C++ 側の std140 の並びをコンパイル時に確かめる。

#include "Rendering/FurTypes.h"
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace NorvesLib::Core::Rendering
{
    static_assert(std::is_standard_layout_v<FurMaterialGpuRegion>);
    static_assert(std::is_trivially_copyable_v<FurMaterialGpuRegion>);
    static_assert(std::is_standard_layout_v<FurMaterialGpuParams>);
    static_assert(std::is_trivially_copyable_v<FurMaterialGpuParams>);
    static_assert(sizeof(FurMaterialGpuRegion) % 16 == 0);
    static_assert(sizeof(FurMaterialGpuParams) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuRegion, Shading) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuRegion, Lobes) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuRegion, Wet) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Counts) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Growth) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Strand) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Undercoat) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Occlusion) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, WetGeometry) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, WetShading) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Regions) % 16 == 0);
    static_assert(offsetof(FurMaterialGpuParams, Regions) +
                  sizeof(FurMaterialGpuRegion) * FurMaxRegionCount == sizeof(FurMaterialGpuParams));

    FurMaterialGpuParams PackFurMaterialGpuParams(const FurMaterialData& fur, float wetness)
    {
        FurMaterialGpuParams out{};
        const FurParams& params = fur.Params;
        const float w = !std::isfinite(wetness) || wetness <= 0.0f
            ? 0.0f
            : (wetness >= 1.0f ? 1.0f : wetness);
        const uint32_t activeRegionCount = fur.RegionCount < FurMaxRegionCount
            ? fur.RegionCount
            : FurMaxRegionCount;

        out.Counts[0] = params.LayerCount;
        out.Counts[1] = activeRegionCount == 0u ? 1u : activeRegionCount;
        if (fur.Maps.Length.IsValid())
        {
            out.Counts[2] |= FurMapMaskLength;
        }
        if (fur.Maps.Flow.IsValid())
        {
            out.Counts[2] |= FurMapMaskFlow;
        }
        if (fur.Maps.DensityClump.IsValid())
        {
            out.Counts[2] |= FurMapMaskDensityClump;
        }
        if (fur.Maps.Region.IsValid())
        {
            out.Counts[2] |= FurMapMaskRegion;
        }

        out.Growth[0] = params.MaxLengthMeters * params.LengthScale;
        out.Growth[1] = params.StrandDensity;
        out.Growth[2] = params.ClumpSize;
        out.Growth[3] = params.ClumpStrength;
        out.Strand[0] = params.RootRadius;
        out.Strand[1] = params.TipRadius;
        out.Strand[2] = params.ThicknessExponent;
        out.Strand[3] = params.Tilt;
        out.Undercoat[0] = params.UndercoatColor[0];
        out.Undercoat[1] = params.UndercoatColor[1];
        out.Undercoat[2] = params.UndercoatColor[2];
        out.Undercoat[3] = params.UndercoatAlbedoMix;
        out.Occlusion[0] = params.AoRoot;
        out.Occlusion[1] = params.AoExponent;
        out.WetGeometry[0] = params.WetLengthCollapse * w;
        out.WetGeometry[1] = params.WetThicken * w;
        out.WetGeometry[2] = params.WetClump * w;
        out.WetGeometry[3] = params.WetSag * w;
        out.WetShading[0] = params.WetDarken * w;
        out.WetShading[1] = params.WetDesaturate * w;
        out.WetShading[2] = params.WetRoughness * w;
        out.WetShading[3] = params.WetGloss * w;

        const FurRegion defaultRegion{};
        for (uint32_t index = 0; index < FurMaxRegionCount; ++index)
        {
            const FurRegion& region = index < activeRegionCount ? fur.Regions[index] : defaultRegion;
            FurMaterialGpuRegion& gpuRegion = out.Regions[index];
            gpuRegion.Shading[0] = region.RootDarkness;
            gpuRegion.Shading[1] = region.TipBrightness;
            gpuRegion.Shading[2] = region.LengthScale;
            gpuRegion.Shading[3] = region.ThicknessScale;
            gpuRegion.Lobes[0] = region.PrimaryShift;
            gpuRegion.Lobes[1] = region.SecondaryShift;
            gpuRegion.Lobes[2] = region.PrimaryExponent;
            gpuRegion.Lobes[3] = region.SecondaryExponent;
            gpuRegion.Wet[0] = region.WetSusceptibility;
        }
        return out;
    }
} // namespace NorvesLib::Core::Rendering
