#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // 頂点レイアウト一致のため（未使用）

layout(set = 0, binding = 0) uniform ShadowMVP
{
    mat4 world;
    mat4 lightView;
    mat4 lightProjection;
} shadowMVP;

void main()
{
    gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * shadowMVP.world * vec4(inPosition, 1.0);
}
