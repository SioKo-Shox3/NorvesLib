#version 450

// 点光源のキューブシャドウの1面へ描く頂点シェーダー。ワールド位置をフラグメントへ渡し、
// フラグメント側で光源からの線形距離を深度として書く。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）

// 1面ぶんの行列と光源の位置・範囲の逆数（ShadowMapPassのPointShadowFaceUBOに対応）。
// worldSource.x=1の描画（MegaGeometry）はインスタンスではなくworldの行列で置く。
layout(set = 0, binding = 0) uniform PointShadowFace
{
    mat4 lightView;
    mat4 lightProjection;
    vec4 lightPositionAndInvRange;
    mat4 world;
    vec4 worldSource;
} pointShadowFace;

struct InstanceData
{
    mat4 world;
    mat4 previousWorld;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

layout(location = 0) out vec3 outWorldPosition;

void main()
{
    mat4 world = pointShadowFace.worldSource.x > 0.5 ? pointShadowFace.world
                                                     : instances[gl_InstanceIndex].world;
    vec4 worldPosition = world * vec4(inPosition, 1.0);
    outWorldPosition = worldPosition.xyz;
    gl_Position = pointShadowFace.lightProjection * pointShadowFace.lightView * worldPosition;
}
