#version 450
// 切り抜きとディゾルブで影を削るため、位置・UV・被覆率・判定値を渡す。
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）
layout(location = 2) in vec2 inTexCoord;

// 1カスケードぶんの行列（ShadowMapPassのShadowPerObjectUBOに対応）。
// worldSource.x=1の描画（MegaGeometry）はインスタンスではなくworldの行列で置く。
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

#include "Common/SceneInstanceData.glsl"
#include "Common/VertexAnimation.glsl"
#include "Common/Dissolve.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) flat out float outCoverageMultiplier;
layout(location = 2) out vec3 outRestPosition;
layout(location = 3) out float outDissolveProtect;
layout(location = 4) flat out float outDissolveThreshold;

void main()
{
    outTexCoord = inTexCoord;
    outCoverageMultiplier = shadowMVP.alphaClip.y;
    outRestPosition = inPosition;
    outDissolveProtect = 0.0;
    outDissolveThreshold = shadowMVP.dissolveParams.z;
    if (shadowMVP.worldSource.x <= 0.5)
    {
        float instanceCoverage = instances[gl_InstanceIndex].shadingParams.w;
        outCoverageMultiplier = instanceCoverage >= 0.0 ? instanceCoverage : shadowMVP.alphaClip.y;
        float instanceThreshold = instances[gl_InstanceIndex].shadingParams.x;
        if (instanceThreshold >= 0.0)
        {
            outDissolveThreshold = clamp(instanceThreshold, 0.0, 1.0);
        }
    }
    mat4 world = shadowMVP.worldSource.x > 0.5 ? shadowMVP.world
                                               : instances[gl_InstanceIndex].world;
    const vec4 instanceParams = shadowMVP.worldSource.x > 0.5 ? vec4(-1.0)
                                                              : instances[gl_InstanceIndex].shadingParams;
    const vec4 animParams = VertexAnimationParamsForFrame(instanceParams, shadowMVP.vertexAnimTime);
    if (VertexAnimationKind(animParams) == VERTEX_ANIMATION_KIND_NONE)
    {
        gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * world * vec4(inPosition, 1.0);
    }
    else
    {
        vec4 worldPos = world * vec4(inPosition, 1.0);
        worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, inPosition, animParams, shadowMVP.vertexAnimTime.x);
        gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * worldPos;
    }
}
