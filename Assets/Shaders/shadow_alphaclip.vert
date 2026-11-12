#version 450
// 切り抜き材質の影へ位置・UV・被覆率の乗数を渡す。
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
} shadowMVP;

#include "Common/SceneInstanceData.glsl"
#include "Common/VertexAnimation.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) flat out float outCoverageMultiplier;

void main()
{
    outTexCoord = inTexCoord;
    outCoverageMultiplier = shadowMVP.alphaClip.y;
    if (shadowMVP.worldSource.x <= 0.5)
    {
        float instanceCoverage = instances[gl_InstanceIndex].shadingParams.w;
        outCoverageMultiplier = instanceCoverage >= 0.0 ? instanceCoverage : shadowMVP.alphaClip.y;
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
