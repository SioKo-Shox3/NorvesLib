#version 450
// アルベドの被覆率で影を切り抜き、残る画素の深度を書き込む。
#extension GL_ARB_sparse_texture2 : require
#extension GL_GOOGLE_include_directive : require

#include "Common/ShadowAlphaClip.glsl"

layout(std140, set = 0, binding = 0) uniform PointShadowAlphaClipFace
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 lightPositionAndInvRange;
    mat4 world;
    vec4 worldSource;
    // x = 閾値、y = 被覆率の乗数、z = sparse のアルベドなら 1、w = 予約。
    vec4 alphaClip;
} pointShadowFace;

layout(set = 0, binding = 10) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) flat in float inCoverageMultiplier;

void main()
{
    if (ShadowAlphaClipShouldDiscard(albedoTexture, inTexCoord, inCoverageMultiplier,
                                     pointShadowFace.alphaClip.x, pointShadowFace.alphaClip.z > 0.5))
    {
        discard;
    }
    float distanceToLight = length(inWorldPosition - pointShadowFace.lightPositionAndInvRange.xyz);
    gl_FragDepth = clamp(distanceToLight * pointShadowFace.lightPositionAndInvRange.w, 0.0, 1.0);
}
