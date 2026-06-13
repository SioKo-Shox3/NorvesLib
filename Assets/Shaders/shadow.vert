#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）

layout(set = 0, binding = 0) uniform ShadowMVP
{
    mat4 lightView;
    mat4 lightProjection;
} shadowMVP;

struct InstanceData
{
    mat4 world;
    vec4 normalRows[3];
    vec4 objectColor;
    vec4 customData;
};

layout(std430, set = 0, binding = 7) readonly buffer InstanceBuffer
{
    InstanceData instances[];
};

void main()
{
    mat4 world = instances[gl_InstanceIndex].world;
    gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * world * vec4(inPosition, 1.0);
}
