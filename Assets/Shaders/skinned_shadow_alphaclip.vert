#version 450
// スキンメッシュの影を切り抜きとディゾルブで削るため、位置・UV・被覆率・判定値を渡す。
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;

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

#include "Common/ShadowSkinning.glsl"
#include "Common/Dissolve.glsl"

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) flat out float outCoverageMultiplier;
layout(location = 2) out vec3 outRestPosition;
layout(location = 3) out float outDissolveProtect;
layout(location = 4) flat out float outDissolveThreshold;

void main()
{
    vec4 worldPosition = skinning.world * vec4(ShadowSkinPosition(inPosition), 1.0);
    outTexCoord = ShadowSkinTexCoord();
    outCoverageMultiplier = shadowMVP.alphaClip.y;
    outRestPosition = inPosition;
    outDissolveProtect = 0.0;
    outDissolveThreshold = shadowMVP.dissolveParams.z;
    if (shadowMVP.dissolveParams.x > 0.5)
    {
        uint wordOffset = uint(gl_VertexIndex) * 16u;
        uvec4 boneIndices = uvec4(skinVertices.words[wordOffset + 8u],
                                  skinVertices.words[wordOffset + 9u],
                                  skinVertices.words[wordOffset + 10u],
                                  skinVertices.words[wordOffset + 11u]);
        vec4 boneWeights = vec4(uintBitsToFloat(skinVertices.words[wordOffset + 12u]),
                                uintBitsToFloat(skinVertices.words[wordOffset + 13u]),
                                uintBitsToFloat(skinVertices.words[wordOffset + 14u]),
                                uintBitsToFloat(skinVertices.words[wordOffset + 15u]));
        outDissolveProtect = DissolveProtect(boneIndices, boneWeights, shadowMVP.dissolveProtectBones);
    }
    gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * worldPosition;
}
