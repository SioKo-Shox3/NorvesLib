#version 450
// アルベドの被覆率とディゾルブで影を切り抜き、残る画素の深度を書き込む。
#extension GL_ARB_sparse_texture2 : require
#extension GL_GOOGLE_include_directive : require

#include "Common/ShadowAlphaClip.glsl"
#include "Common/Dissolve.glsl"

layout(std140, set = 0, binding = 0) uniform PointShadowAlphaClipFace
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 lightPositionAndInvRange;
    mat4 world;
    vec4 worldSource;
    // x = 閾値、y = 被覆率の乗数、z = sparse のアルベドなら 1、w = 予約。
    vec4 alphaClip;
    vec4 dissolveSweep; // xyz = 局所座標の掃引方向、w = 掃引の強さ。
    vec4 dissolveParams; // x = 有効の印、y = ノイズの拡大率、z = 既定の閾値、w = 予約。
    uvec4 dissolveProtectBones; // 保護する骨のビット集合。
} pointShadowFace;

layout(set = 0, binding = 10) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in float inCoverageMultiplier;
layout(location = 3) in vec3 inRestPosition;
layout(location = 4) in float inDissolveProtect;
layout(location = 5) flat in float inDissolveThreshold;

void main()
{
    if (ShadowAlphaClipShouldDiscard(albedoTexture, inTexCoord, inCoverageMultiplier,
                                     pointShadowFace.alphaClip.x, pointShadowFace.alphaClip.z > 0.5))
    {
        discard;
    }
    if (pointShadowFace.dissolveParams.x > 0.5 &&
        DissolveMask(inRestPosition, inDissolveProtect, inDissolveThreshold,
                     pointShadowFace.dissolveSweep.xyz, pointShadowFace.dissolveSweep.w, pointShadowFace.dissolveParams.y) < 0.0)
    {
        discard;
    }
    float distanceToLight = length(inWorldPosition - pointShadowFace.lightPositionAndInvRange.xyz);
    gl_FragDepth = clamp(distanceToLight * pointShadowFace.lightPositionAndInvRange.w, 0.0, 1.0);
}
