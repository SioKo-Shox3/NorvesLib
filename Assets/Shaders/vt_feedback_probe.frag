#version 450
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

// ========================================
// VT のフィードバック（材質のサンプルの箇所が書くタイルの要求）の確認用フラグメントシェーダー
//
// 材質のシェーダーと同じ関数（Common/SparseResidencySampling.glsl の SampleSparseResidentTracked と
// Common/VirtualTextureFeedback.glsl の WriteVirtualTextureFeedback）を、1024x1024 の BC7 の sparse テクスチャへ当てる。
// 4x4 画素を 1 つの確認（probe）に使い、確認の中の勾配は一様にする。ミップ 1 のタイル(0..1, 0..1)とミップ 2 のタイル(0, 0)を結び、ミップ 0 は結ばない
// （lod 1.58 の標本は粗い側のミップ 2 も読むので、ミップ 2 も結ぶ）。
//   0: ミップ 1（結んだ領域）のタイル境界をまたぐ 4x4 画素。1 画素あたり 3 texel（lod 1.58）。gl_FragCoord は画素の中心なので、
//      画素 (lx, ly) の標本はミップ 1 の texel (253 + 1.5 (lx + 0.5), 253 + 1.5 (ly + 0.5))（x の並びは 253.75・255.25・256.75・258.25）で、
//      タイルは (lx >= 2, ly >= 2)。常駐しているので逃げず、巡回の位相の画素だけが書く（位相 p の画素は (p & 3, p >> 2)）。
//   1: ミップ 1 のタイル(1, 1) の中だけを引く 4x4 画素（lod 1.58）。毎フレーム同じタイルを 1 件書く。
//   2: ミップ 0（結んでいない）のタイル境界をまたぐ 4x4 画素。1 画素あたり 1.5 texel（lod 0.58）。標本の texel は 253 + 1.5 (l + 0.5) で、
//      画素のタイルは (lx >= 2, ly >= 2)。
//      非常駐で粗いミップへ逃げるので、位相によらず 16 画素すべてが書く（ミップ 0 の 4 タイルすべてが要求になる。1 タイルあたり 4 画素）。
//   3: 確認 2 と同じ 1 画素 1.5 texel（lod 0.58）で、タイル境界を local = 1.0 に置く（標本の texel は 254.5 + 1.5 (l + 0.5)）。
//      タイルは (lx >= 1, ly >= 1) で、ミップ 0 のタイル (0, 0)・(1, 0)・(0, 1)・(1, 1) が 1・3・3・9 画素になる（面積の違うタイル）。
//      非常駐で粗いミップへ逃げるので、位相によらず 16 画素すべてが書く。
// 材質のパラメータ（巡回の位相・テクスチャの番号・タイルの大きさ）は UBO の u_Params.x。0 のときは何も書かない。
// u_Params.y が 0 でないときは、材質の UBO と同じく float の bit 列として受け取り、DecodeVirtualTextureFeedbackParam で戻した値を使う
// （GBuffer・MegaGeometry の経路。24bit の整数が float を経由しても変わらないことを確かめる）。
// ========================================

#include "Common/SparseResidencySampling.glsl"
#define VT_FEEDBACK_BINDING 2
#include "Common/VirtualTextureFeedback.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_Texture;
layout(set = 0, binding = 1) uniform ProbeParams
{
    uvec4 u_Params;
};

layout(location = 0) out vec4 outColor;

// ミップ 0 の 1 texel の uv の大きさ（テクスチャは 1024x1024）
const float TEXEL = 1.0 / 1024.0;

void main()
{
    int probe = int(gl_FragCoord.x) / 4;
    vec2 local = gl_FragCoord.xy - vec2(float(probe * 4), 0.0);

    // 4 画素の標本が、タイル境界（256 texel）の手前に 2 画素・向こうに 2 画素になるよう、境界を local = 2.0 に置く（基点 = 256 - 2 * 1 画素あたりの texel）
    vec2 baseTexel = vec2(506.0, 506.0);
    float stepTexels = 3.0;
    if (probe == 1)
    {
        baseTexel = vec2(800.0, 800.0);
    }
    else if (probe == 2)
    {
        baseTexel = vec2(253.0, 253.0);
        stepTexels = 1.5;
    }
    else if (probe == 3)
    {
        baseTexel = vec2(254.5, 254.5);
        stepTexels = 1.5;
    }

    // 標本と書き込みは分岐の外で行う（画面微分を壊さない）
    vec2 uv = (baseTexel + local * stepTexels) * TEXEL;
    bool bEscaped = false;
    vec4 color = SampleSparseResidentTracked(u_Texture, uv, bEscaped);
    uint param = u_Params.y != 0u ? DecodeVirtualTextureFeedbackParam(uintBitsToFloat(u_Params.y)) : u_Params.x;
    WriteVirtualTextureFeedback(u_Texture, uv, param, bEscaped);
    outColor = color;
}
