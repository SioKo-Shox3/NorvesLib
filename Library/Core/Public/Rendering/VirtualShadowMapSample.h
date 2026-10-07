#pragma once

// 太陽の VSM（--shadow-method=vsm）を照明・影の測定・テストが読むためのパラメータ。
// シェーダー（Common/VirtualShadowMap.glsl）の VsmSampleParams（std140）と同じ並びで、クリップマップとカメラから作る。
// 段の選び方（カメラからの直線距離のしきい値）は印付け（vsm_mark.comp）と同じ値を使うので、読む段と印を付けた段が一致する。

#include "Rendering/VirtualShadowMapClipmap.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief シェーダーの VsmSampleParams（std140）と同じ並び */
    struct GPUVsmSampleParams
    {
        float lightRight[4];
        float lightUp[4];
        float lightDirection[4];
        /** @brief xyz = カメラの位置、w = 影の最大の距離（m） */
        float cameraPosition[4];
        /** @brief x = 深度の原点、y = 1 / (2 × 深度の範囲)、z = 2 × 深度の範囲（m）、w = 奥の薄めの幅（影の最大の距離に対する割合） */
        float depth[4];
        /** @brief x = 画面上の 1 画素の大きさ / カメラからの距離（2 tan(fovY / 2) / 画面の高さ） */
        float pixel[4];
        /** @brief x = 1 なら有効、y = 段の数、z = 物理ページの数 */
        uint32_t control[4];
        float thresholds[VirtualShadowMapMaxLevels];
        /** @brief x = ページの一辺（m）、y = texel の一辺（m） */
        float levelInfo[VirtualShadowMapMaxLevels][4];
        /** @brief x, y = 範囲の最小の絶対のページの番号 */
        int32_t levelOrigin[VirtualShadowMapMaxLevels][4];
    };
    static_assert(sizeof(GPUVsmSampleParams) == 688, "Common/VirtualShadowMap.glsl の VsmSampleParams と同じ大きさにすること");

    /**
     * @brief クリップマップとカメラから、VSM を読むパラメータを作る
     *
     * 使えない入力（クリップマップが無効・段の数やページの大きさが資源と合わない・物理ページが 0・画角や画面の高さが不正）なら、
     * 全部 0（control.x = 0 = 無効）にして false を返す。呼び出し側は無効のパラメータを照明へ渡し、CSM のまま描く。
     *
     * @param clipmap 今フレームのクリップマップ
     * @param cameraPosition カメラの位置（ワールド）
     * @param fovYDegrees 垂直の画角（度）
     * @param screenHeightPixels 画面の高さ（画素）
     * @param poolPages 物理ページの数
     */
    bool BuildVirtualShadowMapSampleParams(const VirtualShadowMapClipmap* clipmap,
                                           const float* cameraPosition,
                                           float fovYDegrees,
                                           float screenHeightPixels,
                                           uint32_t poolPages,
                                           GPUVsmSampleParams& outParams);
} // namespace NorvesLib::Core::Rendering
