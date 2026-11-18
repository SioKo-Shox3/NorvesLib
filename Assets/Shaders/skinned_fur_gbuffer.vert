#version 450
#extension GL_GOOGLE_include_directive : require

// スキン済みの体を同じ頂点・パレットで殻ごとに押し出し、ファーの GBuffer へ渡す。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

#include "Common/FurStrand.glsl"
#include "Common/VertexAnimation.glsl"

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
layout(location = 9) out float fragShellHeight;
layout(location = 10) flat out uint fragShellIndex;
layout(location = 11) flat out uint fragShellCount;

#include "Common/SkinnedVertex.glsl"

void main()
{
    SkinnedVertex skinned = SkinVertex();
    vec4 params = VertexAnimationParamsForFrame(mvp.vertexAnimInstanceParams, mvp.vertexAnimTime);
    vec4 worldPos = skinning.world * vec4(skinned.position, 1.0);
    worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, skinned.position, params, mvp.vertexAnimTime.x);
    vec3 worldNormal = mat3(skinning.worldNormal) * skinned.normal;
    vec3 baseNormal = dot(worldNormal, worldNormal) > 0.000001 ? normalize(worldNormal) : vec3(0.0);

    uint shellCount = FurShellCount();
    uint shellIndex = uint(gl_InstanceIndex);
    float shellHeight = FurShellHeight(shellIndex, shellCount);
    // 濡れによる垂れを重力の向き（Y が上）に加える。wetGeometry.w は kSag に濡れ度 W を掛けた値。
    vec3 furForce = mvp.furDrawForce.xyz + vec3(0.0, -1.0, 0.0) * mvp.fur.wetGeometry.w;
    vec3 shellDirection = baseNormal + furForce * shellHeight;
    shellDirection = dot(shellDirection, shellDirection) > 0.000001 ? normalize(shellDirection) : baseNormal;
    vec3 shellOffset = shellDirection * (mvp.fur.growth.x * shellHeight);
    worldPos.xyz += shellOffset;

    fragWorldPos = worldPos.xyz;
    fragNormal = baseNormal;
    fragObjectColor = vec3(1.0);
    fragInstanceShadingParams = vec4(-1.0);
    fragEmissiveChromaticityAndLuminanceNits = mvp.emissiveChromaticityAndLuminanceNits;
    fragTexCoord = inTexCoord;
    fragViewDir = normalize(mvp.cameraPosition.xyz - worldPos.xyz);
    fragCurrentClip = mvp.projection * mvp.view * worldPos;

    // 前フレームの骨と時刻で下地を動かし、現在と同じ殻の変位を足して速度へ渡す。
    vec3 previousLocalPos = SkinPreviousPosition();
    vec4 previousWorldPos = previousSkinning.world * vec4(previousLocalPos, 1.0);
    previousWorldPos.xyz = ApplyVertexAnimation(previousWorldPos.xyz, previousLocalPos, params, mvp.vertexAnimTime.y);
    previousWorldPos.xyz += shellOffset;
    fragPreviousClip = mvp.previousProjection * mvp.previousView * previousWorldPos;

    fragShellHeight = shellHeight;
    fragShellIndex = shellIndex;
    fragShellCount = shellCount;
    gl_Position = fragCurrentClip;
}
