#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(std140, set = 0, binding = 0) uniform SilhouetteData
{
    mat4 view;
    mat4 projection;
    vec4 vertexAnimInstanceParams;
    vec4 vertexAnimTime;
    vec4 color;
} silhouette;

#include "Common/VertexAnimation.glsl"
#include "Common/SkinnedVertex.glsl"

void main()
{
    // GBuffer と同じ現在の姿勢・頂点変形・計算順で深度を求める。
    SkinnedVertex skinned = SkinVertex();
    vec4 params = VertexAnimationParamsForFrame(silhouette.vertexAnimInstanceParams, silhouette.vertexAnimTime);
    vec4 worldPos = skinning.world * vec4(skinned.position, 1.0);
    worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, skinned.position, params, silhouette.vertexAnimTime.x);
    vec4 currentClip = silhouette.projection * silhouette.view * worldPos;
    gl_Position = currentClip;
}
