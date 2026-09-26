#version 450

// 点光源のキューブシャドウ: 光源からの線形距離を範囲で割った値（0〜1）を深度として書く。
// 深度の比較（Less）で、各方向の最も光源に近い面の距離が残る。

// 1面ぶんの行列と光源の位置・範囲の逆数（ShadowMapPassのPointShadowFaceUBOに対応）
layout(set = 0, binding = 0) uniform PointShadowFace
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 lightPositionAndInvRange;
} pointShadowFace;

layout(location = 0) in vec3 inWorldPosition;

void main()
{
    float distanceToLight = length(inWorldPosition - pointShadowFace.lightPositionAndInvRange.xyz);
    gl_FragDepth = clamp(distanceToLight * pointShadowFace.lightPositionAndInvRange.w, 0.0, 1.0);
}
