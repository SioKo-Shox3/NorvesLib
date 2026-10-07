#version 450
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

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
    vec4 pomParams;  // x=heightScale, y=hasHeightMap, z=ORMの1枚が metallicTexture の枠に張られているか（1/0）, w=法線が2チャンネル（BC5）か（1/0）
    vec4 frameParams; // x=前フレームカメラ履歴の有効フラグ, y=発光に掛けるプリエクスポージャ, z=材質のテクスチャが sparse（VT）か（1/0）, w=VT のフィードバックのパラメータ（アルベド。0 は書かない。VirtualTextureFeedback.glsl）
    vec4 vtFeedbackParams; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallicTexture の枠）, z=高さ（0 は書かない）
} mvp;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#define VT_FEEDBACK_BINDING 11
#include "Common/VirtualTextureFeedback.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"
#include "Common/PreExposedEmissive.glsl"

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: Albedo (RGB) + alpha
layout(location = 1) out vec4 outNormal;    // RT1: World Normal (RGB) + unused
layout(location = 2) out vec4 outMaterial;  // RT2: Metallic(R) / Roughness(G) / AO(B) / unused(A)
layout(location = 3) out vec4 outEmissive;  // RT3: プリエクスポージャ後の発光（RGB） + 未使用
layout(location = 4) out vec2 outVelocity;  // RT4: currentUV - previousUV

void main()
{
    // POMパラメータ取得
    float heightScale = mvp.pomParams.x;
    float hasHeightMap = mvp.pomParams.y;
    bool bVirtualTexture = mvp.frameParams.z > 0.5;

    // 高さのフィードバックの標本ミップ。POM の前の元の UV で、画素ごとに分かれる分岐・ループより前に取る（画面微分は一様な位置でだけ有効）。
    float heightLod = 0.0;
    if (bVirtualTexture && hasHeightMap > 0.5)
    {
        heightLod = textureQueryLOD(heightTexture, fragTexCoord).y;
    }

    // 余接フレームは元のUVから一度だけ作り、POMと法線マップの両方に使う。
    mat3 TBN = CalculateCotangentFrame(fragNormal, fragWorldPos, fragTexCoord);

    // POM適用: ハイトマップがある場合のみUVオフセット
    vec2 texCoord = fragTexCoord;
    if (hasHeightMap > 0.5)
    {
        texCoord = ApplyParallaxOcclusionMapping(heightTexture, fragTexCoord, TBN, fragViewDir, heightScale, bVirtualTexture);
    }

    // POM の直後の一様な位置で、POM の後の UV の勾配と各層の標本ミップを取る（標本・フィードバックはこれを使い、画面微分を取り直さない）。
    MaterialTextureFootprint footprint = QueryMaterialTextureFootprint(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord,
        mvp.pomParams.z > 0.5, bVirtualTexture);

    // テクスチャサンプリング × オブジェクトカラー（POM補正済みUV使用）
    PbrMaterialTextureSamples textureSamples = SamplePbrMaterialTextures(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord, footprint,
        mvp.pomParams.z > 0.5, mvp.pomParams.w > 0.5, bVirtualTexture);
    // VT のフィードバック: POM の後の UV で欲しいタイルの要求を書く（VT のテクスチャごとに表の番号を持つ）。
    // 高さだけは POM の前の元の UV で書く。
    WriteVirtualTextureFeedback(albedoTexture, texCoord, DecodeVirtualTextureFeedbackParam(mvp.frameParams.w),
                                g_VirtualTextureAlbedoEscaped, footprint.AlbedoLod);
    WriteVirtualTextureFeedback(normalTexture, texCoord, DecodeVirtualTextureFeedbackParam(mvp.vtFeedbackParams.x),
                                g_VirtualTextureNormalEscaped, footprint.NormalLod);
    WriteVirtualTextureFeedback(metallicTexture, texCoord, DecodeVirtualTextureFeedbackParam(mvp.vtFeedbackParams.y),
                                g_VirtualTextureOrmEscaped, footprint.MetallicLod);
    if (hasHeightMap > 0.5)
    {
        WriteVirtualTextureHeightFeedback(heightTexture, fragTexCoord,
                                          DecodeVirtualTextureFeedbackParam(mvp.vtFeedbackParams.z), heightLod);
    }
    outAlbedo = vec4(ComposePbrSurfaceAlbedo(fragObjectColor, textureSamples),
                     textureSamples.Albedo.a);

    // ノーマルマップ適用（POM補正済みUVで標本し、元のUVの余接フレームで変換する）
    vec3 normal = ApplyTangentSpaceNormal(TBN, textureSamples.TangentNormal);
    outNormal = vec4(normal, 0.0);

    // PBRマテリアルパラメータ（POM補正済みUV使用）
    outMaterial = vec4(textureSamples.Material, 0.0);

    // 発光: Y=1 の色度 × 輝度（nits）にプリエクスポージャを掛けて書く（Lighting は露出を掛けずに足す）
    outEmissive = vec4(ComputePreExposedEmissive(fragEmissiveChromaticityAndLuminanceNits.rgb,
                                                 fragEmissiveChromaticityAndLuminanceNits.a,
                                                 mvp.frameParams.y),
                       1.0);

    outVelocity = vec2(0.0);
    if (mvp.frameParams.x > 0.5 &&
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
