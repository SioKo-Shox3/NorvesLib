// 草の描画で CPU と共有する定数と頂点インデックスの符号化。
#ifndef NORVES_GRASS_DRAW_GLSL
#define NORVES_GRASS_DRAW_GLSL

#include "WorldField/WorldFieldSample.glsl"

const uint kGrassLodCount = 3u;
const uint kGrassHashSaltFacing = 4u;
const uint kGrassHashSaltPhase = 5u;
const uint kGrassHashSaltColor = 6u;
const uint kGrassHashSaltLean = 7u;
const float kGrassNearFadeStartMeters = 1.2;
const float kGrassNearFadeDiscardMeters = 0.35;
const float kGrassDefaultGlobalWetness = 0.0;
const float kGrassDefaultTrample = 0.0;
const uint kGrassDrawFlagPreviousCamera = 1u;
const uint kGrassDrawFlagWind = 2u;
const uint kGrassDrawFlagWorldField = 4u;

// set 0 の binding: 0 = 描画定数、1 = ブレード SSBO（GrassBladeRecord Blades[]）。
// 1 は LOD ごとの範囲をバイトオフセット付きで束ね、Blades[0] がその LOD の先頭。
// 2 = 種別 SSBO（GrassSpeciesGpuData Species[]）、3 = LayerA の RGBA8 2D 配列。
// 4 = 今の世界フィールド、5 = 前の世界フィールド（線形補間・Clamp）。
// LayerA は texelFetch で読む。正規化後の B（バイト値 / 255）が moistureBias、層は (cz mod N) * N + (cx mod N)。
// 負座標の mod は正の剰余に直す。有効な濡れ度は clamp(global * (0.6 + 0.8 * B), 0, 1)。
// 生成側の定数も binding 0 を使うため、構造体とハッシュだけをここで定義する。

struct GrassBladeRecord
{
    float PositionX;
    float PositionY;
    float PositionZ;
    uint Packed;
};

struct GrassSpeciesGpuData
{
    vec4 RootColor;
    vec4 TipColor;
    vec4 WetColor;
    float HeightMinMeters;
    float HeightMaxMeters;
    float BladeWidthMeters;
    float Bend;
    float Stiffness;
    float Roughness;
    float WetRoughness;
    float WetDroop;
    float WindResponse;
    float TrampleResponse;
    float Opacity;
    float BladesPerSquareMeter;
    vec4 LodDistanceMeters;
    vec4 LodKeepFraction;
};

// CPU の GrassDrawWorldFieldConstants と同じ 512 バイトの std140 配置。
// 先頭 464 バイトは GrassDrawConstants。bit2 が無ければ追加領域を読まない。
layout(std140, set = 0, binding = 0) uniform GrassDrawConstants
{
    mat4 View;                       // 現在のビュー行列。
    mat4 Projection;                 // 現在の射影行列。
    mat4 PreviousView;               // 前のビュー行列。
    mat4 PreviousProjection;         // 前の射影行列。
    vec4 CameraPosition;             // xyz: 世界位置、w: 距離 1 m あたりの 1 画素の大きさ（m）。
    vec4 PreviousCameraPosition;     // xyz: 前の世界位置、w: 前の 1 m あたりの画素寸法（0 なら現在値）。
    vec4 Wind;                       // xy: 風向 x/z、z: 時刻（s）、w: 前の時刻（s）。
    vec4 WindParams;                 // BaseSpeed、GustStrength、GustFrequencyHz、GustScaleMeters。
    vec4 WindExtra;                  // x: Turbulence、yzw: 予約。
    vec4 Surface;                    // 全体の濡れ度、最小画素幅、拡幅上限倍率、予約。
    vec4 NearFade;                   // 縮め始め（m）、捨て切る距離（m）、予約、予約。
    vec4 BandSpacingMeters;          // xyz: LOD 0/1/2 の間隔（m）、w: 予約。
    uvec4 LodSegments;               // xyz: LOD 0/1/2 の区切り数、w: 予約。
    vec4 Quant;                      // 高さの量子化最大（m）、幅の量子化最大（m）、予約、予約。
    vec4 LayerWindow;                // ChunkSizeMeters、窓の N、LayerA 解像度、予約。
    uvec4 Flags;                     // x の bit0: 前のカメラあり、bit1: 風が有効、bit2: 世界フィールドの追加定数あり、yzw: 予約。
    vec4 Trample;                    // 全成分予約の 0。
    WorldFieldSampleConstants WorldField;
} draw;

// gl_VertexIndex を uint に写した値から、帯の LOD・行・左右を復元する。
uint GrassBladeIndexLod(uint index)
{
    return index >> 16;
}

uint GrassBladeIndexRow(uint index)
{
    return (index & 0xffffu) >> 1;
}

uint GrassBladeIndexSide(uint index)
{
    return index & 1u;
}

uint GrassHash(uint x, uint y, uint z)
{
    uint h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (z * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float GrassHashToUnit(uint h)
{
    precise float value = float(h >> 8) * uintBitsToFloat(0x33800000u);
    return value;
}

#endif
