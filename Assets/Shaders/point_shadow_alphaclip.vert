#version 450
// 切り抜き材質の影へ位置・UV・被覆率の乗数を渡す。
#extension GL_GOOGLE_include_directive : require

// 点光源のキューブシャドウの1面へ描く頂点シェーダー。ワールド位置をフラグメントへ渡し、
// フラグメント側で光源からの線形距離を深度として書く。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）
layout(location = 2) in vec2 inTexCoord;

// 1面ぶんの行列と光源の位置・範囲の逆数（ShadowMapPassのPointShadowFaceUBOに対応）。
// worldSource.x=1の描画（MegaGeometry）はインスタンスではなくworldの行列で置く。
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

#include "Common/SceneInstanceData.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec3 outWorldPosition;

layout(location = 1) out vec2 outTexCoord;
layout(location = 2) flat out float outCoverageMultiplier;

void main()
{
    outTexCoord = inTexCoord;
    outCoverageMultiplier = pointShadowFace.alphaClip.y;
    if (pointShadowFace.worldSource.x <= 0.5)
    {
        float instanceCoverage = instances[gl_InstanceIndex].shadingParams.w;
        outCoverageMultiplier = instanceCoverage >= 0.0 ? instanceCoverage : pointShadowFace.alphaClip.y;
    }
    mat4 world = pointShadowFace.worldSource.x > 0.5 ? pointShadowFace.world
                                                     : instances[gl_InstanceIndex].world;
    vec4 worldPosition = world * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    gl_Position = pointShadowFace.lightProjection * pointShadowFace.lightView * worldPosition;
}
