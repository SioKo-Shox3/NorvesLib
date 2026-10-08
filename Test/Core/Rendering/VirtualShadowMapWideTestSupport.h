#pragma once

// VSM のスライスが 33 個以上の場面を作る、テスト共用の合成データ。
// 先頭は太陽のクリップマップの段、後ろは先頭の段を繰り返した正射影のスライス（スライス s は 段 s % 段の数 と同じ範囲・ページの一辺・texel・基底で、
// ページの表の先頭だけが s × 128 × 128）。後ろのスライスは先頭の段と同じ場面を描くので、結果を先頭の段と texel 単位で比べられる。

#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VirtualShadowMapSample.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::VirtualShadowMapWideTest
{
    /** @brief 太陽のクリップマップの段を先頭に、段を繰り返した正射影のスライスを sliceCount 個まで並べる（sliceCount はクリップマップの段の数以上） */
    inline void BuildWideSlices(const VirtualShadowMapClipmap& clipmap, uint32_t sliceCount, GPUVsmSlice* outSlices)
    {
        BuildVirtualShadowMapSlices(&clipmap, nullptr, sliceCount, outSlices);
        for (uint32_t slice = clipmap.LevelCount; slice < sliceCount; ++slice)
        {
            GPUVsmSlice repeated = outSlices[slice % clipmap.LevelCount];
            repeated.origin[2] = static_cast<int32_t>(slice * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL);
            outSlices[slice] = repeated;
        }
    }
    /**
     * @brief 太陽のクリップマップの段を、スライスの表の firstSun 番目から並べる（段 L はスライス firstSun + L）。
     *        それより前のスライスは、段を繰り返した正射影のスライス（ページの表の先頭は連続する番地）
     *
     * 印付けの MarkFirstSlice = firstSun で、太陽の段がスライス 32 以降にもまたがる場面を作れる。
     * sliceCount は firstSun + 段の数 以上にすること。それより後ろのスライスは空（ページの一辺 0）のまま。
     */
    inline void BuildSunAtSlices(const VirtualShadowMapClipmap& clipmap, uint32_t sliceCount, uint32_t firstSun, GPUVsmSlice* outSlices)
    {
        GPUVsmSlice sun[VirtualShadowMapMaxLevels];
        BuildVirtualShadowMapSlices(&clipmap, nullptr, VirtualShadowMapMaxLevels, sun);
        BuildVirtualShadowMapSlices(nullptr, nullptr, sliceCount, outSlices);
        for (uint32_t slice = 0; slice < firstSun + clipmap.LevelCount && slice < sliceCount; ++slice)
        {
            outSlices[slice] = slice < firstSun ? sun[slice % clipmap.LevelCount] : sun[slice - firstSun];
            outSlices[slice].origin[2] = static_cast<int32_t>(slice * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL);
        }
    }
} // namespace NorvesLib::Core::Rendering::VirtualShadowMapWideTest
