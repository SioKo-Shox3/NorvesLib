// インポスターの材質を通常の G-buffer と同じ方法で標本する。
// アルベド・被覆率、オブジェクト空間の法線、ORM、深度をアトラスへ書く。
#version 450
#extension GL_GOOGLE_include_directive : require
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

layout(set = 0, binding = 0) uniform ImpostorBakeParams
{
    vec4 boundsCenterRadius;
    vec4 baseColor;
    vec4 materialFlags;
    vec4 bakeParams;
} bake;

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;

layout(location = 0) in vec3 fragObjectPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) flat in vec3 fragViewDirection;

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outOrm;
layout(location = 3) out float outDepth;

void main()
{
    vec3 geometricNormal = gl_FrontFacing ? fragNormal : -fragNormal;
    mat3 TBN = CalculateCotangentFrame(geometricNormal, fragObjectPos, fragTexCoord);
    vec2 texCoord = fragTexCoord;
    bool bHasORM = bake.materialFlags.x > 0.5;
    bool bNormalTwoChannel = bake.materialFlags.y > 0.5;

    // 画面微分と材質の標本は切り抜きより前に行う。
    MaterialTextureFootprint footprint = QueryMaterialTextureFootprint(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord,
        bHasORM, false);
    PbrMaterialTextureSamples textureSamples = SamplePbrMaterialTextures(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord, footprint,
        bHasORM, bNormalTwoChannel, false);

    vec3 tangentNormal = textureSamples.TangentNormal;
    if (bake.materialFlags.z > 0.5)
    {
        tangentNormal = ApplyPbrNormalScale(tangentNormal, bake.materialFlags.w);
    }
    vec3 normal = ApplyTangentSpaceNormal(TBN, tangentNormal);

    if (bake.bakeParams.x > 0.0 && textureSamples.Albedo.a < bake.bakeParams.x)
    {
        discard;
    }

    vec3 albedo = ComposePbrSurfaceAlbedo(bake.baseColor.rgb, textureSamples);
    outAlbedo = vec4(albedo, 1.0);
    outNormal = vec4(normalize(normal) * 0.5 + 0.5, 1.0);
    outOrm = vec4(textureSamples.Material.z, textureSamples.Material.y, textureSamples.Material.x, 1.0);

    float radius = bake.boundsCenterRadius.w;
    outDepth = clamp((radius - dot(fragObjectPos - bake.boundsCenterRadius.xyz, fragViewDirection)) /
                     (2.0 * radius), 0.0, 1.0);
}
