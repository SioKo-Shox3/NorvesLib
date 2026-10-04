#version 450
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

// ========================================
// sparse テクスチャの常駐フォールバック（暗黙の勾配版）の確認用フラグメントシェーダー
//
// 材質のシェーダーが使う暗黙の標本関数（Common/PbrMaterialTextureSampling.glsl の SampleMaterialTexture）を、
// 一部のタイルとミップテイルだけを結んだテクスチャへ当てる。画面微分が要るので、コンピュートではなく描画で確かめる。
// 2x2 画素（1つの quad）を1つの確認に使い、quad の中の勾配は一様にする。確認ごとの勾配は次のとおり。
//   0: 結んでいない領域・勾配 1 テクセル（等方）
//   1: 結んでいない領域・勾配 (4, 1) テクセル（異方性 4 倍のときの標本ミップは 0）
//   2: 結んでいない領域・勾配 4 テクセル（等方。標本ミップ 2 はミップテイルの中）
//   3: 結んだタイル・勾配 1 テクセル（逃げない）
//   4: 結んだタイル・勾配 (8, 1) テクセル（異方性の上限 4 倍を超える）を、POM と同じ形（textureQueryLOD で実際の標本ミップを
//      先に取り、明示勾配の関数へ渡す）で引く。標本ミップは 1（ミップ0へ戻ってはならない）
// 結果は quad の画素 (quad * 2, 0) の色を読む。
// ========================================

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"

layout(set = 0, binding = 0) uniform sampler2D u_Texture;

layout(location = 0) out vec4 outColor;

// テクスチャは 512x512（ミップ0は 256x256 のタイル 2x2 枚）。タイル(0,0)は uv が 0〜0.5 の領域。
const vec2 RESIDENT_UV = vec2(0.25, 0.25);
const vec2 UNBOUND_UV = vec2(0.75, 0.75);
const float TEXEL = 1.0 / 512.0;

void main()
{
    int probe = int(gl_FragCoord.x) / 2;
    vec2 local = gl_FragCoord.xy - vec2(float(probe * 2), 0.0);

    vec2 baseUv = UNBOUND_UV;
    vec2 uvStep = vec2(TEXEL, TEXEL);
    if (probe == 1)
    {
        uvStep = vec2(4.0 * TEXEL, TEXEL);
    }
    else if (probe == 2)
    {
        uvStep = vec2(4.0 * TEXEL, 4.0 * TEXEL);
    }
    else if (probe == 3)
    {
        baseUv = RESIDENT_UV;
    }
    else if (probe == 4)
    {
        baseUv = RESIDENT_UV;
        uvStep = vec2(8.0 * TEXEL, TEXEL);
    }

    // quad の中の勾配は uvStep になる。標本は分岐の外で行う（画面微分を壊さない）。
    vec2 uv = baseUv + local * uvStep;
    if (probe == 4)
    {
        float sampledLod = textureQueryLOD(u_Texture, uv).y;
        outColor = SampleMaterialTextureGrad(u_Texture, uv, dFdx(uv), dFdy(uv), sampledLod, true);
        return;
    }
    outColor = SampleMaterialTexture(u_Texture, uv, true);
}
