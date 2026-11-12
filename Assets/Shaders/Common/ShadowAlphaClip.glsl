// 影の被覆率をアルベドのアルファと乗数から求め、材質の閾値で切り抜く。
#ifndef NORVES_SHADOW_ALPHA_CLIP_GLSL
#define NORVES_SHADOW_ALPHA_CLIP_GLSL

#include "Common/AlphaClip.glsl"
#include "Common/SparseResidencySampling.glsl"

bool ShadowAlphaClipShouldDiscard(sampler2D albedoTexture, vec2 uv, float coverageMultiplier,
                                  float cutoff, bool bVirtualTexture)
{
    // 影の画面微分からミップを選ぶ。VT のフィードバックは書かず、常駐するミップだけを読む。
    float lod = textureQueryLOD(albedoTexture, uv).y;
    float coverage = SampleMaterialTextureLod(albedoTexture, uv, lod, bVirtualTexture).a * coverageMultiplier;
    // PCF に渡す影は、材質の方式にかかわらず Test で切り抜く。
    return AlphaClipShouldDiscard(coverage, cutoff, ALPHA_CLIP_MODE_TEST, vec2(0.0), 0u);
}

#endif
