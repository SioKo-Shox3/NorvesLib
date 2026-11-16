#version 450
// 切り抜きとディゾルブで影を削るため、位置・UV・被覆率・判定値を渡す。
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
    vec4 dissolveSweep; // xyz = 局所座標の掃引方向、w = 掃引の強さ。
    vec4 dissolveParams; // x = 有効の印、y = ノイズの拡大率、z = 既定の閾値、w = 予約。
    uvec4 dissolveProtectBones; // 保護する骨のビット集合。
} pointShadowFace;

#include "Common/SceneInstanceData.glsl"
#include "Common/Dissolve.glsl"

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec3 outWorldPosition;

layout(location = 1) out vec2 outTexCoord;
layout(location = 2) flat out float outCoverageMultiplier;
layout(location = 3) out vec3 outRestPosition;
layout(location = 4) out float outDissolveProtect;
layout(location = 5) flat out float outDissolveThreshold;

void main()
{
    outTexCoord = inTexCoord;
    outCoverageMultiplier = pointShadowFace.alphaClip.y;
    outRestPosition = inPosition;
    outDissolveProtect = 0.0;
    outDissolveThreshold = pointShadowFace.dissolveParams.z;
    if (pointShadowFace.worldSource.x <= 0.5)
    {
        float instanceCoverage = instances[gl_InstanceIndex].shadingParams.w;
        outCoverageMultiplier = instanceCoverage >= 0.0 ? instanceCoverage : pointShadowFace.alphaClip.y;
        float instanceThreshold = instances[gl_InstanceIndex].shadingParams.x;
        if (instanceThreshold >= 0.0)
        {
            outDissolveThreshold = clamp(instanceThreshold, 0.0, 1.0);
        }
    }
    mat4 world = pointShadowFace.worldSource.x > 0.5 ? pointShadowFace.world
                                                     : instances[gl_InstanceIndex].world;
    vec4 worldPosition = world * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    gl_Position = pointShadowFace.lightProjection * pointShadowFace.lightView * worldPosition;
}
