#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）

// 1カスケードぶんの行列（ShadowMapPassのShadowPerObjectUBOに対応）。
// worldSource.x=1の描画（MegaGeometry）はインスタンスではなくworldの行列で置く。
layout(set = 0, binding = 0) uniform ShadowMVP
{
    mat4 lightView;
    mat4 lightProjection;
    mat4 world;
    vec4 worldSource;
    vec4 vertexAnimTime;
} shadowMVP;

#include "Common/SceneInstanceData.glsl"
#include "Common/VertexAnimation.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

void main()
{
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
