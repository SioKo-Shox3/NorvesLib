#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform ShadowMVP
{
    mat4 lightView;
    mat4 lightProjection;
} shadowMVP;

#include "Common/ShadowSkinning.glsl"

void main()
{
    vec4 worldPosition = skinning.world * vec4(ShadowSkinPosition(inPosition), 1.0);
    gl_Position = shadowMVP.lightProjection * shadowMVP.lightView * worldPosition;
}
