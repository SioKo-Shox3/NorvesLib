#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPosition;
    vec4 emissiveChromaticityAndLuminanceNits;
    vec4 pomParams;
    vec4 frameParams;
} mvp;

#define NORVES_SKINNED_VERTEX_BUFFERS_ONLY
#include "Common/SkinnedVertex.glsl"
#undef NORVES_SKINNED_VERTEX_BUFFERS_ONLY

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragObjectColor;
layout(location = 3) out vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) out vec2 fragTexCoord;
layout(location = 5) out vec3 fragViewDir;
layout(location = 6) out vec4 fragCurrentClip;
layout(location = 7) out vec4 fragPreviousClip;
layout(location = 8) flat out vec4 fragInstanceShadingParams;

#include "Common/SkinnedVertex.glsl"

void main()
{
    SkinnedVertex skinned = SkinVertex();
    vec4 worldPos = skinning.world * vec4(skinned.position, 1.0);
    fragWorldPos = worldPos.xyz;
    vec3 worldNormal = mat3(skinning.worldNormal) * skinned.normal;
    fragNormal = dot(worldNormal, worldNormal) > 0.000001 ? normalize(worldNormal) : vec3(0.0);
    fragObjectColor = vec3(1.0);
    fragInstanceShadingParams = vec4(-1.0);
    fragEmissiveChromaticityAndLuminanceNits = mvp.emissiveChromaticityAndLuminanceNits;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);
    fragCurrentClip = mvp.projection * mvp.view * worldPos;
    // 直前のフレームの骨と変換で同じ頂点を動かし、前のカメラで投影する（gbuffer.frag が velocity にする）。
    vec4 previousWorldPos = previousSkinning.world * vec4(SkinPreviousPosition(), 1.0);
    fragPreviousClip = mvp.previousProjection * mvp.previousView * previousWorldPos;
    gl_Position = fragCurrentClip;
}
