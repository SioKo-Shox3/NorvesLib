#version 450
// アルベドの被覆率で影を切り抜き、残る画素の深度を書き込む。
#extension GL_ARB_sparse_texture2 : require
#extension GL_GOOGLE_include_directive : require

#include "Common/ShadowAlphaClip.glsl"

layout(std140, set = 0, binding = 0) uniform ShadowAlphaClipMVP
{
    mat4 lightView;
    mat4 lightProjection;
    mat4 world;
    vec4 worldSource;
    vec4 vertexAnimTime;
    // x = 閾値、y = 被覆率の乗数、z = sparse のアルベドなら 1、w = 予約。
    vec4 alphaClip;
} shadowMVP;

layout(set = 0, binding = 10) uniform sampler2D albedoTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) flat in float inCoverageMultiplier;

void main()
{
    if (ShadowAlphaClipShouldDiscard(albedoTexture, inTexCoord, inCoverageMultiplier,
                                     shadowMVP.alphaClip.x, shadowMVP.alphaClip.z > 0.5))
    {
        discard;
    }
}
