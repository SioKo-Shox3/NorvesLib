// ファーの殻の描画定数、毛の流れの角度、セル単位の乱数を頂点・断片シェーダーで共有する。
#ifndef NORVES_FUR_STRAND_GLSL
#define NORVES_FUR_STRAND_GLSL

#include "Common/FurMaterialParams.glsl"

layout(std140, set = 0, binding = 0) uniform MVPData
{
    mat4 view; // 0: 現在のビュー行列。
    mat4 projection; // 64: 現在の投影行列。
    mat4 previousView; // 128: 前フレームのビュー行列。
    mat4 previousProjection; // 192: 前フレームの投影行列。
    vec4 cameraPosition; // 256: 世界空間のカメラ位置。
    vec4 emissiveChromaticityAndLuminanceNits; // 272: 発光の色度と輝度。
    vec4 pomParams; // 288: 高さと材質テクスチャの構成。
    vec4 frameParams; // 304: 履歴の有効性、露出、仮想テクスチャの設定。
    vec4 vtFeedbackParams; // 320: 仮想テクスチャの要求と鏡面の有無。
    vec4 shadingParams; // 336: 個体の値を重ねた材質の値。
    vec4 shadingModel; // 352: 材質の種類と切り抜きの設定。
    vec4 vertexAnimInstanceParams; // 368: 個体ごとの頂点アニメーションの設定。
    vec4 vertexAnimTime; // 384: 現在・前フレームの時刻と揺れの許可。
    vec4 specularAAParams; // 400: 鏡面の平滑化と法線・発光の設定。
    vec4 vertexAnimMaterial0; // 416: 材質の頂点アニメーションの設定（前半）。
    vec4 vertexAnimMaterial1; // 432: 材質の頂点アニメーションの設定（後半）。
    vec4 viewClearance[4]; // 448: 視界の確保の設定（64 バイト）。
    vec4 dissolveEdge; // 512: 消失する縁の色と発光輝度。
    vec4 dissolveSweep; // 528: 消失する掃引の向きと強さ。
    vec4 dissolveParams; // 544: 消失の有効性とノイズ・縁・ディザの幅。
    uvec4 dissolveProtectBones; // 560: 消失から保護する骨のビットの組。
    FurMaterialParams fur; // 576: ファーの材質の値（496 バイト）。
    vec4 furDrawForce; // 1072: xyz は世界空間の毛の力、w は予約の 0。
    vec4 furShellParams; // 1088: 殻数、最後の殻の割合、連続の殻数、皮膚と毛の粗さ。
} mvp;

// 描画用の殻数を丸め、使用可能な 1〜64 枚に収める。
uint FurShellCount()
{
    return clamp(uint(mvp.furShellParams.x + 0.5), 1u, 64u);
}

// 殻 0 は皮膚、最後の殻は毛先とし、殻が一枚なら皮膚の高さを返す。
float FurShellHeight(uint shellIndex, uint shellCount)
{
    return shellCount <= 1u ? 0.0 : float(shellIndex) / float(shellCount - 1u);
}

const float FurFlowTwoPi = 6.283185307179586;

// 正規化された書き込み法線から、Duff ら（2017）の式で同じ正規直交基底を作る。
void FurFlowBasis(vec3 n, out vec3 b1, out vec3 b2)
{
    float signZ = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (signZ + n.z);
    float b = n.x * n.y * a;
    b1 = vec3(1.0 + signZ * n.x * n.x * a, signZ * b, -signZ * n.x);
    b2 = vec3(b, signZ + n.y * n.y * a, -n.y);
}

// Emissive.a は半精度で分解能 1/2048 ≒ 0.18 度。
// 流れを法線の面へ射影し、半精度への丸めでも 1.0 にならない角度を返す。
float FurEncodeFlowAngle(vec3 n, vec3 t)
{
    vec3 projected = t - n * dot(n, t);
    if (dot(projected, projected) <= 1.0e-12)
    {
        return 0.0;
    }
    vec3 b1;
    vec3 b2;
    FurFlowBasis(n, b1, b2);
    float angle = atan(dot(projected, b2), dot(projected, b1));
    return min(fract(angle / FurFlowTwoPi), 1.0 - 1.0 / 2048.0);
}

// 書き込み法線と同じ基底を使い、保存した角度から照明用の接線を戻す。
vec3 FurDecodeFlowTangent(vec3 n, float p1)
{
    vec3 b1;
    vec3 b2;
    FurFlowBasis(n, b1, b2);
    float angle = fract(p1) * FurFlowTwoPi;
    return cos(angle) * b1 + sin(angle) * b2;
}

// 符号付きセルを整数のまま混ぜ、二つの乱数のビット列を作る。
uvec2 FurCellHashBits(ivec2 cell)
{
    uvec2 value = uvec2(cell) * 1664525u + 1013904223u;
    value.x += value.y * 1664525u;
    value.y += value.x * 1664525u;
    value ^= value >> 16u;
    value.x += value.y * 1664525u;
    value.y += value.x * 1664525u;
    return value;
}

// 上位 24 ビットを使い、浮動小数点への丸めでも 1.0 を含まない乱数を返す。
float FurCellHash(ivec2 cell)
{
    return float(FurCellHashBits(cell).x >> 8u) * (1.0 / 16777216.0);
}

// 同じセルから毛の位置をずらすための二成分の乱数 [0, 1) を返す。
vec2 FurCellHash2(ivec2 cell)
{
    return vec2(FurCellHashBits(cell) >> 8u) * (1.0 / 16777216.0);
}

#endif // NORVES_FUR_STRAND_GLSL
