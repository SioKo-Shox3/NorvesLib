// GBuffer_Emissive（RGBA16F）へ書く発光を求める共通関数。
// gbuffer.frag（スキニングのGBufferも同じ）と megageometry.frag が同じ規則で書く。
//
// 発光は物理の輝度（nits）にそのフレームのプリエクスポージャを掛けた値で書く。
// lighting.frag は GBuffer の発光へ露出を掛けずに SceneColor へ足す。
// 物理の nits のまま書くと、昼の露出でにじむほど明るい発光（数十万 nits 以上）が
// 半精度の上限65504を超えて無限大になるため。

#ifndef NORVES_PRE_EXPOSED_EMISSIVE_GLSL
#define NORVES_PRE_EXPOSED_EMISSIVE_GLSL

// GBuffer へ書く、プリエクスポージャ後の発光の上限。半精度で表せる最大の有限値65504にし、
// 表せない値（無限大になる値）だけを頭打ちにする。範囲内の値は寄与を変えない。
const float GBUFFER_PRE_EXPOSED_EMISSIVE_MAX = 65504.0;

// chromaticity: Y=1 の色度、luminanceNits: 輝度（nits）、preExposure: シーンカラーのプリエクスポージャ
vec3 ComputePreExposedEmissive(vec3 chromaticity, float luminanceNits, float preExposure)
{
    vec3 preExposedEmissive = chromaticity * (luminanceNits * preExposure);
    return min(preExposedEmissive, vec3(GBUFFER_PRE_EXPOSED_EMISSIVE_MAX));
}

#endif
