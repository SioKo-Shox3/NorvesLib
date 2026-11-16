// スキンメッシュの GBuffer 頂点処理を共有し、バインド姿勢の位置と保護の重みを渡す。
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
    vec4 vtFeedbackParams;
    vec4 shadingParams;
    vec4 shadingModel;
    vec4 vertexAnimInstanceParams;
    vec4 vertexAnimTime;
    vec4 specularAAParams;
    vec4 vertexAnimMaterial0;
    vec4 vertexAnimMaterial1;
#ifdef GBUFFER_DISSOLVE
    vec4 viewClearance[4]; // VS では読まない（断片側の ViewClearanceData と同じ 64 バイト）
    vec4 dissolveEdge;
    vec4 dissolveSweep;
    vec4 dissolveParams;
    uvec4 dissolveProtectBones;
#endif
} mvp;

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
#ifdef GBUFFER_DISSOLVE
#include "Common/Dissolve.glsl"
layout(location = 9) out vec3 fragRestPosition;
layout(location = 10) out float fragDissolveProtect;
#endif

#include "Common/SkinnedVertex.glsl"

void main()
{
#ifdef GBUFFER_DISSOLVE
    // 保護の重みはスキンの変形前の骨の番号と重みから求める。
    uint wordOffset = uint(gl_VertexIndex) * 16u;
    uvec4 boneIndices = uvec4(skinVertices.words[wordOffset + 8u],
                              skinVertices.words[wordOffset + 9u],
                              skinVertices.words[wordOffset + 10u],
                              skinVertices.words[wordOffset + 11u]);
    vec4 boneWeights = vec4(uintBitsToFloat(skinVertices.words[wordOffset + 12u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 13u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 14u]),
                            uintBitsToFloat(skinVertices.words[wordOffset + 15u]));
    fragRestPosition = inPosition;
    fragDissolveProtect = DissolveProtect(boneIndices, boneWeights, mvp.dissolveProtectBones);
#endif
    SkinnedVertex skinned = SkinVertex();
    vec4 params = VertexAnimationParamsForFrame(mvp.vertexAnimInstanceParams, mvp.vertexAnimTime);
    vec4 worldPos = skinning.world * vec4(skinned.position, 1.0);
    worldPos.xyz = ApplyVertexAnimation(worldPos.xyz, skinned.position, params, mvp.vertexAnimTime.x);
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
    vec3 previousLocalPos = SkinPreviousPosition();
    vec4 previousWorldPos = previousSkinning.world * vec4(previousLocalPos, 1.0);
    previousWorldPos.xyz = ApplyVertexAnimation(previousWorldPos.xyz, previousLocalPos, params, mvp.vertexAnimTime.y);
    fragPreviousClip = mvp.previousProjection * mvp.previousView * previousWorldPos;
    gl_Position = fragCurrentClip;
}
