#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveChromaticityAndLuminanceNits;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;
layout(location = 6) in vec4 fragCurrentClip;
layout(location = 7) in vec4 fragPreviousClip;

// UBOからPOMパラメータを参照
layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    mat4 previousView;
    mat4 previousProjection;
    vec4 cameraPosition;
    vec4 emissiveChromaticityAndLuminanceNits;
    vec4 pomParams;  // x=heightScale, y=hasHeightMap, z=unused, w=unused
    vec4 velocityParams; // x=前フレームカメラ履歴の有効フラグ
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
layout(location = 4) out vec2 outVelocity;  // RT4: currentUV - previousUV

void main()
{
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
    PbrMaterialTextureSamples textureSamples = SamplePbrMaterialTextures(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord);
    outAlbedo = vec4(ComposePbrSurfaceAlbedo(fragObjectColor, textureSamples),
                     textureSamples.Albedo.a);

    // ノーマルマップ適用（POM補正済みUVで標本し、元のUVの余接フレームで変換する）
    vec3 normal = ApplyTangentSpaceNormal(TBN, textureSamples.TangentNormal);
    outNormal = vec4(normal, 0.0);

    // PBRマテリアルパラメータ（POM補正済みUV使用）
    outMaterial = vec4(textureSamples.Material, 0.0);

    // Emissive: Y=1 chromaticity × luminance nits → physical HDR RGB
    vec3 physicalEmissive = fragEmissiveChromaticityAndLuminanceNits.rgb *
                            fragEmissiveChromaticityAndLuminanceNits.a;
    outEmissive = vec4(physicalEmissive, 1.0);

    outVelocity = vec2(0.0);
    if (mvp.velocityParams.x > 0.5 &&
        abs(fragCurrentClip.w) > 1e-6 &&
        abs(fragPreviousClip.w) > 1e-6)
    {
        vec2 currentNdc = fragCurrentClip.xy / fragCurrentClip.w;
        vec2 previousNdc = fragPreviousClip.xy / fragPreviousClip.w;
        vec2 velocity = (currentNdc - previousNdc) * 0.5;
        if (all(equal(velocity, velocity)) &&
            dot(velocity, velocity) < 1.0e6)
        {
            outVelocity = velocity;
        }
    }
}
