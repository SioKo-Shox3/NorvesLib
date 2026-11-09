#pragma once

#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 生成で使う塩 0〜4 に続く、位相・色・傾きの塩。
    inline constexpr uint32_t kGrassHashSaltPhase = 5;
    inline constexpr uint32_t kGrassHashSaltColor = 6;
    inline constexpr uint32_t kGrassHashSaltLean = 7;

    /// 近接時に草丈を縮め始める距離と、画面ドアで捨て切る距離（m）。
    inline constexpr float kGrassNearFadeStartMeters = 1.2f;
    inline constexpr float kGrassNearFadeDiscardMeters = 0.35f;
    /// 全体の濡れ度と踏み倒しの既定値。踏み倒しは予約の 0。
    inline constexpr float kGrassDefaultGlobalWetness = 0.0f;
    inline constexpr float kGrassDefaultTrample = 0.0f;
    inline constexpr uint32_t kGrassDrawFlagPreviousCamera = 1u;
    inline constexpr uint32_t kGrassDrawFlagWind = 2u;

    /**
     * @brief 草の描画で CPU と GLSL が共有する 464 バイトの std140 定数。
     * 行列は GLSL mat4 の列の順で、CameraViewConstants::CopyShaderView / CopyShaderProjection
     * に各行列の先頭（&View[0][0] など）を渡して書く。予約成分はすべて 0。
     * 間隔・量子化の最大値・分割数は生成設定と、窓の値は GrassFieldGpuWindow と一致させる。
     */
    struct alignas(16) GrassDrawConstants
    {
        float View[4][4] = {};                        // 現在のビュー行列。
        float Projection[4][4] = {};                  // 現在の射影行列。
        float PreviousView[4][4] = {};                // 前のビュー行列。
        float PreviousProjection[4][4] = {};          // 前の射影行列。
        float CameraPosition[4] = {};                 // xyz: 世界位置、w: 距離 1 m あたりの 1 画素の大きさ（m）。
        float PreviousCameraPosition[4] = {};         // xyz: 前の世界位置、w: 予約。
        float Wind[4] = {};                           // xy: 水平単位方向 x/z、z: 時刻（s）、w: 前の時刻（s）。
        float WindParams[4] = {};                     // BaseSpeed、GustStrength、GustFrequencyHz、GustScaleMeters。
        float WindExtra[4] = {};                      // x: Turbulence、yzw: 予約。
        float Surface[4] = {kGrassDefaultGlobalWetness, 1.0f, 8.0f, 0.0f}; // 濡れ度、最小画素幅、拡幅上限倍率、予約。
        float NearFade[4] = {kGrassNearFadeStartMeters, kGrassNearFadeDiscardMeters, 0.0f, 0.0f}; // 縮め始め（m）、捨て切る距離（m）、予約、予約。
        float BandSpacingMeters[4] = {0.125f, 0.25f, 0.5f, 0.0f}; // xyz: LOD 0/1/2 の間隔（m）、w: 予約。
        uint32_t LodSegments[4] = {7, 3, 1, 0};        // xyz: LOD 0/1/2 の区切り数、w: 予約。
        float Quant[4] = {2.0f, 0.25f, 0.0f, 0.0f};   // x: 高さの量子化最大（m）、y: 幅の量子化最大（m）、zw: 予約。
        float LayerWindow[4] = {};                    // ChunkSizeMeters、窓の N、LayerA 解像度、予約（N と解像度は整数値）。
        uint32_t Flags[4] = {};                       // x の bit0: 前のカメラあり、bit1: 風が有効、yzw: 予約。
        float Trample[4] = {kGrassDefaultTrample, 0.0f, 0.0f, 0.0f}; // 全成分予約の 0。
    };
    static_assert(alignof(GrassDrawConstants) == 16);
    static_assert(sizeof(GrassDrawConstants) == 464);
    static_assert(offsetof(GrassDrawConstants, View) == 0);
    static_assert(offsetof(GrassDrawConstants, Projection) == 64);
    static_assert(offsetof(GrassDrawConstants, PreviousView) == 128);
    static_assert(offsetof(GrassDrawConstants, PreviousProjection) == 192);
    static_assert(offsetof(GrassDrawConstants, CameraPosition) == 256);
    static_assert(offsetof(GrassDrawConstants, PreviousCameraPosition) == 272);
    static_assert(offsetof(GrassDrawConstants, Wind) == 288);
    static_assert(offsetof(GrassDrawConstants, WindParams) == 304);
    static_assert(offsetof(GrassDrawConstants, WindExtra) == 320);
    static_assert(offsetof(GrassDrawConstants, Surface) == 336);
    static_assert(offsetof(GrassDrawConstants, NearFade) == 352);
    static_assert(offsetof(GrassDrawConstants, BandSpacingMeters) == 368);
    static_assert(offsetof(GrassDrawConstants, LodSegments) == 384);
    static_assert(offsetof(GrassDrawConstants, Quant) == 400);
    static_assert(offsetof(GrassDrawConstants, LayerWindow) == 416);
    static_assert(offsetof(GrassDrawConstants, Flags) == 432);
    static_assert(offsetof(GrassDrawConstants, Trample) == 448);

    /**
     * @brief LOD 0→1→2 の帯を結ぶ共有インデックスを作り、出力配列を置き換える。
     * 各区切り数 S は 0..32767（行番号が下位 16 ビットに収まる範囲）。0 は空の帯。
     * 値は (lod << 16) | (row << 1) | side。頂点バッファと頂点オフセットは不要。
     * outFirstIndex はインデックス単位で、生成定数の LodFirstIndex と一致する。
     */
    void BuildGrassBladeIndices(const uint32_t (&lodSegments)[3],
        Container::VariableArray<uint32_t>& outIndices, uint32_t (&outFirstIndex)[3]);

    /// 符号化された頂点インデックスから LOD を復元する。
    inline constexpr uint32_t GrassBladeIndexLod(uint32_t index)
    {
        return index >> 16;
    }

    /// 符号化された頂点インデックスから帯の行番号を復元する。
    inline constexpr uint32_t GrassBladeIndexRow(uint32_t index)
    {
        return (index & 0xffffu) >> 1;
    }

    /// 符号化された頂点インデックスから左右（0 / 1）を復元する。
    inline constexpr uint32_t GrassBladeIndexSide(uint32_t index)
    {
        return index & 1u;
    }
} // namespace NorvesLib::Core::Rendering
