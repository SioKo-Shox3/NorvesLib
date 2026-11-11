#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;
layout(location = 6) in vec4 fragCurrentClip;
layout(location = 7) in vec4 fragPreviousClip;
layout(location = 8) flat in vec4 fragInstanceShadingParams;

#include "Common/GBufferEncoding.glsl"

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out vec2 outVelocity;

void main()
{
    outAlbedo = vec4(fragObjectColor, 1.0);
    outNormal = GBufferEncodeNormal(normalize(fragNormal), GBUFFER_SHADING_MODEL_DEFAULT_LIT, 0u);
    outMaterial = GBufferEncodeMaterial(vec3(0.0, 0.5, 1.0), 0.0);
    outEmissive = GBufferEncodeEmissive(vec3(0.125, 0.375, 0.75), 0.625);
    outVelocity = vec2(0.0);
}
