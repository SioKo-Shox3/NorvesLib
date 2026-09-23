#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief パストレーサーが追う光輸送の範囲
     *
     * Fullは多重散乱をすべて追う参照。DirectOnlyとSingleDiffuseBounceは、輸送範囲を限った
     * ラスタの近似（直接光だけ、拡散1バウンスのRTGI）と同じ範囲の参照を作る検証用で、
     * 発光三角形と太陽円盤は光源標本だけで評価する。
     */
    enum class PathTracingTransportScope : uint32_t
    {
        /** @brief 多重散乱をすべて追う */
        Full = 0,
        /** @brief 1次命中の発光と直接光（光源標本）だけ */
        DirectOnly = 1,
        /** @brief 直接光に、拡散葉だけで散乱した1回の間接光（命中点の発光を除く直接光と環境光）を加える */
        SingleDiffuseBounce = 2
    };

    /**
     * @brief 1次光線を画素内のどこから出すか
     *
     * Boxは画素内を一様にずらし、累積で画素面積の平均（縁の被覆）を求める。Centerは常に画素中心から
     * 出し、ラスタのGBufferと同じ標本位置にする（縁のaliasingまでラスタと揃えて光輸送だけを比べる検証用）。
     */
    enum class PathTracingPixelSampling : uint32_t
    {
        Box = 0,
        Center = 1
    };
} // namespace NorvesLib::Core::Rendering
