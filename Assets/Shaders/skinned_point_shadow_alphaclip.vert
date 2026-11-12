#version 450
// スキンメッシュの切り抜きの影へ位置・UV・解決済みの被覆率の乗数を渡す。
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;

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

#include "Common/ShadowSkinning.glsl"

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) flat out float outCoverageMultiplier;

void main()
{
    vec4 worldPosition = skinning.world * vec4(ShadowSkinPosition(inPosition), 1.0);
    outWorldPosition = worldPosition.xyz;
    outTexCoord = ShadowSkinTexCoord();
    outCoverageMultiplier = pointShadowFace.alphaClip.y;
    gl_Position = pointShadowFace.lightProjection * pointShadowFace.lightView * worldPosition;
}
