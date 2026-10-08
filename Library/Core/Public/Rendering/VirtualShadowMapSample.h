#pragma once

// 太陽の VSM（--shadow-method=vsm）を照明・影の測定・テストが読むためのパラメータ。
// シェーダー（Common/VirtualShadowMap.glsl）の VsmSampleParams（std140）と同じ並びで、クリップマップとカメラから作る。
// 段の選び方（カメラからの直線距離のしきい値）は印付け（vsm_mark.comp）と同じ値を使うので、読む段と印を付けた段が一致する。
// ページの一辺・texel・範囲の原点・ページの表の先頭などスライス（太陽では段）ごとの値は、パラメータではなくスライスの表（GPUVsmSlice の
// storage buffer。BuildVirtualShadowMapSlices が作る）にある。

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
        /** @brief x = 深度の原点、y = 1 / (2 × 深度の範囲)、z = 2 × 深度の範囲（m）、w = 予約（0） */
        float depth[4];
        /**
         * @brief x = 画面上の 1 画素の大きさ / カメラからの距離（2 tan(fovY / 2) / 画面の高さ）、y = 太陽の角半径の tan、
         *        z = ブロッカーの探索と PCF の半径の上限（m。ワールドの長さ）
         */
        float pixel[4];
        /** @brief xyz = カメラの前方（単位ベクトル）。影の距離の範囲・薄めはこの前方への距離で測る（CSM と同じ） */
        float view[4];
        /** @brief x = 影の最小の距離、y = 影の最大の距離、z = 奥の薄めの幅（m） */
        float range[4];
        /** @brief x = 1 なら有効、y = スライス（段）の数、z = 物理ページの数 */
        uint32_t control[4];
        /** @brief 段を選ぶ距離のしきい値（太陽のクリップマップのもの）。ページの一辺・texel・範囲の原点などスライスごとの値はスライスの表（GPUVsmSlice）にある */
        float thresholds[VirtualShadowMapMaxLevels];
    };
    static_assert(sizeof(GPUVsmSampleParams) == 208, "Common/VirtualShadowMap.glsl の VsmSampleParams と同じ大きさにすること");

    /**
     * @brief 投影の種類（GPUVsmSlice::extra[2]）。太陽のクリップマップの段は正射影、点光源の面は透視
     */
    inline constexpr int32_t VirtualShadowMapSliceProjectionOrtho = 0;
    inline constexpr int32_t VirtualShadowMapSliceProjectionPerspective = 1;

    /** @brief スライスの表に並べられるスライスの数の上限（今は太陽の段の上限と同じ。表の大きさはこの数 × sizeof(GPUVsmSlice)） */
    inline constexpr uint32_t VirtualShadowMapMaxSlices = VirtualShadowMapMaxLevels;

    /**
     * @brief スライスの表（storage buffer）の 1 件。Common/VirtualShadowMapSlice.glsl の VsmSlice（std430）と同じ並び
     *
     * 太陽のクリップマップの段も点光源の面も、同じ形のスライスとして並べる。ページの表の番地は「origin[2] + トーラスの番地」で、
     * 太陽の段は 128 × 128 のページを段の順に先頭から並べる（origin[2] = 段 × 16384）。
     */
    struct GPUVsmSlice
    {
        /** @brief 投影の行列の上 3 行（x = axisX · (p, 1)、y = axisY · (p, 1)、z = axisZ · (p, 1)）。正射影の段はライト空間の基底 */
        float axisX[4];
        float axisY[4];
        float axisZ[4];
        /** @brief x = ページの一辺（m）、y = texel の一辺（m）、z, w = 予約（0） */
        float info[4];
        /** @brief x, y = 範囲の最小の絶対のページの番号、z = ページの表の先頭（要素）、w = ページの表の一辺（ページの数。2 の冪） */
        int32_t origin[4];
        /** @brief x, y = 前フレームの範囲の最小の絶対のページの番号（割り当てだけが読む）、z = 投影の種類、w = 予約（0） */
        int32_t extra[4];
    };
    static_assert(sizeof(GPUVsmSlice) == 96, "Common/VirtualShadowMapSlice.glsl の VsmSlice と同じ大きさにすること");

    /**
     * @brief クリップマップ（太陽）から、スライスの表の先頭 VirtualShadowMapMaxSlices 件を作る
     *
     * 全件にページの表の先頭・一辺・投影の種類を入れる（クリップマップが無い・無効でも、段の数の上限まで既定の番地になる。
     * ページの一辺・texel・原点は 0）。使える段（LevelCount 未満）には、ページの一辺・texel・範囲の原点・ライト空間の基底を書く。
     * previous が非 null なら前フレームの原点を extra に書き、null なら今フレームと同じにする（範囲が動いていない扱い）。
     * 段が使えない値（原点が int32 に収まらない・ページの一辺が 0 以下）でも false にはせず、その段は 0 のまま残す。
     *
     * @param outSlices VirtualShadowMapMaxSlices 件以上の領域
     * @return 作った（使える）スライスの数
     */
    uint32_t BuildVirtualShadowMapSlices(const VirtualShadowMapClipmap* clipmap, const VirtualShadowMapClipmap* previous, GPUVsmSlice* outSlices);

    /**
     * @brief 影の距離の範囲・奥の薄めの幅を決める（照明の読み出しと、印付けが同じ値を使う）
     *
     * カメラの前方への距離が [outNear, outFar] の外なら影なし、outFade の幅で奥を薄める。
     * cascadeSplitDistances（5 個）が使えれば CSM の分割（最初・最後・最後のカスケードの幅の 10%）、
     * nullptr または不正（非有限・負・増加しない）なら [0, MaxShadowDistance]・MaxShadowDistance × FadeRatio。
     */
    void ResolveVirtualShadowMapViewRange(const VirtualShadowMapClipmapSettings& settings,
                                          const float* cascadeSplitDistances,
                                          float& outNear,
                                          float& outFar,
                                          float& outFade);

    /**
     * @brief クリップマップとカメラから、VSM を読むパラメータを作る
     *
     * 使えない入力（クリップマップが無効・段の数やページの大きさが資源と合わない・物理ページが 0・画角や画面の高さが不正）なら、
     * 全部 0（control.x = 0 = 無効）にして false を返す。呼び出し側は無効のパラメータを照明へ渡し、CSM のまま描く。
     *
     * 影の距離の範囲・奥の薄めは CSM と同じ量にする: カメラの前方への距離が [分割の最初, 分割の最後] の外なら影なし、
     * 最後のカスケードの幅（分割の最後 − 最後から 2 番目）の 10% で薄める。分割の距離が使えなければ
     * [0, MaxShadowDistance]・MaxShadowDistance × FadeRatio で薄める。
     *
     * @param clipmap 今フレームのクリップマップ
     * @param cameraPosition カメラの位置（ワールド）
     * @param cameraForward カメラの前方（ワールド。長さは問わない。0・非有限なら無効）
     * @param cascadeSplitDistances CSM の分割の距離（5 個。前方への距離）。nullptr、または不正（非有限・増加しない）なら使わない
     * @param fovYDegrees 垂直の画角（度）
     * @param screenHeightPixels 画面の高さ（画素）
     * @param poolPages 物理ページの数
     */
    bool BuildVirtualShadowMapSampleParams(const VirtualShadowMapClipmap* clipmap,
                                           const float* cameraPosition,
                                           const float* cameraForward,
                                           const float* cascadeSplitDistances,
                                           float fovYDegrees,
                                           float screenHeightPixels,
                                           uint32_t poolPages,
                                           GPUVsmSampleParams& outParams);
} // namespace NorvesLib::Core::Rendering
