#version 450
// アルベドの被覆率とディゾルブで影を切り抜き、残る画素の深度を書き込む。
#extension GL_ARB_sparse_texture2 : require
#extension GL_GOOGLE_include_directive : require

#include "Common/ShadowAlphaClip.glsl"
#include "Common/Dissolve.glsl"

layout(std140, set = 0, binding = 0) uniform ShadowAlphaClipMVP
{
    mat4 lightView;
    mat4 lightProjection;
    mat4 world;
    vec4 worldSource;
    vec4 vertexAnimTime;
    // x = 閾値、y = 被覆率の乗数、z = sparse のアルベドなら 1、w = 予約。
    vec4 alphaClip;
    vec4 dissolveSweep; // xyz = 局所座標の掃引方向、w = 掃引の強さ。
    vec4 dissolveParams; // x = 有効の印、y = ノイズの拡大率、z = 既定の閾値、w = 予約。
    uvec4 dissolveProtectBones; // 保護する骨のビット集合。
} shadowMVP;

layout(set = 0, binding = 10) uniform sampler2D albedoTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) flat in float inCoverageMultiplier;
layout(location = 2) in vec3 inRestPosition;
layout(location = 3) in float inDissolveProtect;
layout(location = 4) flat in float inDissolveThreshold;

void main()
{
    if (ShadowAlphaClipShouldDiscard(albedoTexture, inTexCoord, inCoverageMultiplier,
                                     shadowMVP.alphaClip.x, shadowMVP.alphaClip.z > 0.5))
    {
        discard;
    }
    if (shadowMVP.dissolveParams.x > 0.5 &&
        DissolveMask(inRestPosition, inDissolveProtect, inDissolveThreshold,
                     shadowMVP.dissolveSweep.xyz, shadowMVP.dissolveSweep.w, shadowMVP.dissolveParams.y) < 0.0)
    {
        discard;
    }
}
