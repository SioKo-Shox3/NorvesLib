#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveColor;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;
layout(location = 6) flat in uint fragDebugPayload;

// UBOからPOMパラメータを参照
layout(set = 0, binding = 0) uniform MVPData
{
    mat4 world;
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
    vec4 emissiveColor;
    vec4 pomParams;  // x=heightScale, y=hasHeightMap, z=debugMode, w=debugPayloadSupported
} mvp;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: Albedo (RGB) + alpha
layout(location = 1) out vec4 outNormal;    // RT1: World Normal (RGB) + unused
layout(location = 2) out vec4 outMaterial;  // RT2: Metallic(R) / Roughness(G) / AO(B) / unused(A)
layout(location = 3) out vec4 outEmissive;  // RT3: Emissive (RGB, HDR) + unused

const float DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3.0;
const float DEBUG_VIEW_MODE_LOD_LEVEL = 8.0;

uint HashClusterId(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

vec3 ClusterDebugColor(uint clusterId)
{
    uint hash = HashClusterId(clusterId);
    vec3 color = vec3(float(hash & 255u),
                      float((hash >> 8) & 255u),
                      float((hash >> 16) & 255u)) / 255.0;
    return mix(vec3(0.18), color, 0.82);
}

vec3 LODLevelDebugColor(uint lodLevel)
{
    const vec3 palette[8] = vec3[8](
        vec3(0.10, 0.72, 0.28),
        vec3(0.14, 0.78, 0.70),
        vec3(0.18, 0.42, 0.90),
        vec3(0.42, 0.24, 0.86),
        vec3(0.78, 0.22, 0.78),
        vec3(0.96, 0.72, 0.18),
        vec3(0.95, 0.42, 0.16),
        vec3(0.86, 0.12, 0.12));
    return palette[min(lodLevel, 7u)];
}

void WriteDebugGBuffer(vec3 albedo)
{
    outAlbedo = vec4(albedo, 1.0);
    outNormal = vec4(normalize(fragNormal), 0.0);
    outMaterial = vec4(0.0, 1.0, 1.0, 0.0);
    outEmissive = vec4(0.0, 0.0, 0.0, 1.0);
}

void main()
{
    float debugMode = mvp.pomParams.z;
    float debugPayloadSupported = mvp.pomParams.w;

    if (debugPayloadSupported > 0.5)
    {
        if (debugMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS)
        {
            WriteDebugGBuffer(ClusterDebugColor(fragDebugPayload));
            return;
        }

        if (debugMode == DEBUG_VIEW_MODE_LOD_LEVEL)
        {
            WriteDebugGBuffer(LODLevelDebugColor(fragDebugPayload));
            return;
        }
    }

    // POMパラメータ取得
    float heightScale = mvp.pomParams.x;
    float hasHeightMap = mvp.pomParams.y;

    // 余接フレームは元のUVから一度だけ作り、POMと法線マップの両方に使う。
    mat3 TBN = CalculateCotangentFrame(fragNormal, fragWorldPos, fragTexCoord);

    // POM適用: ハイトマップがある場合のみUVオフセット
    vec2 texCoord = fragTexCoord;
    if (hasHeightMap > 0.5)
    {
        texCoord = ApplyParallaxOcclusionMapping(heightTexture, fragTexCoord, TBN, fragViewDir, heightScale);
    }

    // テクスチャサンプリング × オブジェクトカラー（POM補正済みUV使用）
    vec4 texColor = texture(albedoTexture, texCoord);
    outAlbedo = vec4(fragObjectColor * texColor.rgb, texColor.a);

    // ノーマルマップ適用（POM補正済みUVで標本し、元のUVの余接フレームで変換する）
    vec3 normalMapSample = texture(normalTexture, texCoord).rgb;
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;
    vec3 normal = ApplyTangentSpaceNormal(TBN, tangentNormal);
    outNormal = vec4(normal, 0.0);

    // PBRマテリアルパラメータ（POM補正済みUV使用）
    float metallic  = texture(metallicTexture, texCoord).r;
    float roughness = texture(roughnessTexture, texCoord).r;
    float ao        = texture(aoTexture, texCoord).r;
    outMaterial = vec4(metallic, roughness, ao, 0.0);

    // Emissive: エミッシブカラー × 強度 → HDR値
    outEmissive = vec4(fragEmissiveColor.rgb * fragEmissiveColor.a, 1.0);
}
