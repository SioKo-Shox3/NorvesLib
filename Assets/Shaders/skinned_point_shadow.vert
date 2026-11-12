#version 450
#extension GL_GOOGLE_include_directive : require

// スキンメッシュを点光源のキューブシャドウの1面へ描く頂点シェーダー（skinned_shadow.vertと同じスキニング）。

layout(location = 0) in vec3 inPosition;

// 1面ぶんの行列と光源の位置・範囲の逆数（ShadowMapPassのPointShadowFaceUBOに対応）
layout(set = 0, binding = 0) uniform PointShadowFace
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 lightPositionAndInvRange;
} pointShadowFace;

#include "Common/ShadowSkinning.glsl"

layout(location = 0) out vec3 outWorldPosition;

void main()
{
    vec4 worldPosition = skinning.world * vec4(ShadowSkinPosition(inPosition), 1.0);
    outWorldPosition = worldPosition.xyz;
    gl_Position = pointShadowFace.lightProjection * pointShadowFace.lightView * worldPosition;
}
