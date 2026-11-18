// GBuffer の標準の断片シェーダーの本体。材質ごとのプログラムの FS は、この本体か
// Common/GBufferEncoding.glsl の GBufferEncodeNormal・GBufferEncodeMaterial・GBufferEncodeEmissive を通して 5 枚へ書く約束。
// 材質ごとのプログラムの FS も、環境の UBO（set 0 / binding 13）を読み、GBuffer へ書く前に
// ApplyWetSurfaceWithPuddle を呼ぶ。濡れの感度は DefaultLit の P0、未指定は 1。
// 材質ごとのプログラムの FS も法線の確定直後に SpecularAAFilterMaterial を通す（強さは MVPData.specularAAParams.x）。
// 発光テクスチャは binding 14、specularAAParams.z の印は 1=発光テクスチャ、2=NormalScale。
// GBUFFER_ALPHA_CLIP を定義した gbuffer_alphaclip.frag は切り抜き材質に使う。
// 切り抜きの変種は、視界の確保の印の付いた材質のディザも受け持つ。
// discard で深度の書き込みも止めるため、切り抜きの FS では早期の深度テストを宣言しない。
// 通常の FS の早期深度テストを保つため、切り抜きを分ける。
// GBUFFER_LOD_CROSSFADE を定義した gbuffer_crossfade.frag は段の遷移のディザを受け持つ。
// 切り抜きの変種も段の遷移を判定し、両変種では早期の深度テストを宣言しない。
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
layout(location = 8) flat in vec4 fragInstanceShadingParams;
#if defined(GBUFFER_ALPHA_CLIP) || defined(GBUFFER_LOD_CROSSFADE)
layout(location = 11) flat in float fragLodCrossfade;
#include "Common/LodCrossfade.glsl"
#endif
#ifdef GBUFFER_DISSOLVE
layout(location = 9) in vec3 fragRestPosition;
layout(location = 10) in float fragDissolveProtect;
#endif

#include "Common/ViewClearance.glsl"

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
    vec4 vtFeedbackParams; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallicTexture の枠）, z=高さ（0 は書かない）, w=鏡面の無い材質か（1/0）
    vec4 shadingParams; // 材質の値。スキンだけインスタンスの値を重ねた値。負は未指定。
    vec4 shadingModel; // x=GBuffer のシェーディングモデル ID, y=切り抜き閾値 cutoff（0〜1）, z=切り抜き方式（0=なし、1=Test、2=Hash）, w=Hash の位相（0〜7、TAA のジッタの番号）
    vec4 vertexAnimInstanceParams; // FS では読まない。
    vec4 vertexAnimTime; // FS では読まない。
    vec4 specularAAParams; // x=鏡面の AA の強さ, y=NormalScale, z=材質のテクスチャの印（1=発光テクスチャ、2=NormalScale）, w=予約の 0
    vec4 vertexAnimMaterial0; // FS では読まない。
    vec4 vertexAnimMaterial1; // FS では読まない。
    ViewClearanceData viewClearance; // 視界の確保の遮蔽物のディザ（印の付いた材質だけ値が入る。印の無い材質は全 0）
#ifdef GBUFFER_DISSOLVE
    vec4 dissolveEdge;
    vec4 dissolveSweep;
    vec4 dissolveParams;
    uvec4 dissolveProtectBones;
#endif
} mvp;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;
layout(set = 0, binding = 14) uniform sampler2D emissiveTexture;
layout(set = 0, binding = 16) uniform sampler2D overheadHeightMap;
layout(set = 0, binding = 17) uniform sampler2D puddleField;

#include "Common/GBufferEncoding.glsl"
#include "Common/VelocityEncoding.glsl"
#include "Common/SpecularAntiAliasing.glsl"
#ifdef GBUFFER_DISSOLVE
#include "Common/Dissolve.glsl"
#include "Common/AlphaClip.glsl"
#endif
#ifdef GBUFFER_ALPHA_CLIP
#include "Common/AlphaClip.glsl"
#endif
#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#include "Common/WetSurface.glsl"
NORVES_DECLARE_ENVIRONMENT_UBO(0, 13)
NORVES_DECLARE_PUDDLE_SURFACE_UBO(0, 15)
#define VT_FEEDBACK_BINDING 11
#if defined(GBUFFER_ALPHA_CLIP) || defined(GBUFFER_LOD_CROSSFADE)
// 切り抜きと段の遷移で捨てた画素の深度を書かないように、早期の深度テストを宣言しない。
#define NORVES_VT_FEEDBACK_LATE_DEPTH_TESTS 1
#endif
#include "Common/VirtualTextureFeedback.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"
#include "Common/PreExposedEmissive.glsl"

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: アルベド（RGB）とアルファ（A）
layout(location = 1) out vec4 outNormal;    // RT1: ワールド法線（RGB）と材質 ID・受け手の印（A）
layout(location = 2) out vec4 outMaterial;  // RT2: 金属度（R）・粗さ（G）・AO（B）・材質値 P0（A）
layout(location = 3) out vec4 outEmissive;  // RT3: プリエクスポージャ後の発光（RGB）・材質値 P1（A）
layout(location = 4) out vec2 outVelocity;  // RT4: 現在の UV - 前の UV

void main()
{
    // 両面材質はカリングなしで描くため、裏面の法線を反転する。背面カリング時は常に表面なので変わらない。
    vec3 geometricNormal = gl_FrontFacing ? fragNormal : -fragNormal;

    // POMパラメータ取得
    float heightScale = mvp.pomParams.x;
    float hasHeightMap = mvp.pomParams.y;
    bool bVirtualTexture = mvp.frameParams.z > 0.5;
    uint materialTextureFlags = uint(mvp.specularAAParams.z + 0.5);

    // 高さのフィードバックの標本ミップ。POM の前の元の UV で、画素ごとに分かれる分岐・ループより前に取る（画面微分は一様な位置でだけ有効）。
    float heightLod = 0.0;
    if (bVirtualTexture && hasHeightMap > 0.5)
    {
        heightLod = textureQueryLOD(heightTexture, fragTexCoord).y;
    }

    // 余接フレームは元のUVから一度だけ作り、POMと法線マップの両方に使う。
    mat3 TBN = CalculateCotangentFrame(geometricNormal, fragWorldPos, fragTexCoord);

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
    // discard より前に、POM の後の UV で発光テクスチャを標本する。
    vec3 emissiveSample = vec3(1.0);
    if ((materialTextureFlags & 1u) != 0u)
    {
        emissiveSample = texture(emissiveTexture, texCoord).rgb;
    }
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
    // インスタンスの指定を材質より優先し、未指定の成分には既定値を使う。
    vec4 params = ResolveShadingParams(fragInstanceShadingParams, mvp.shadingParams);

    // ノーマルマップ適用（POM補正済みUVで標本し、元のUVの余接フレームで変換する）
    vec3 tangentNormal = textureSamples.TangentNormal;
    if ((materialTextureFlags & 2u) != 0u)
    {
        tangentNormal = ApplyPbrNormalScale(tangentNormal, mvp.specularAAParams.y);
    }
    vec3 normal = ApplyTangentSpaceNormal(TBN, tangentNormal);
    // discard（OpKill）の後は画面微分が未定義になるため、切り抜きの判定より前に取る。
    vec3 normalDx = dFdxFine(normal);
    vec3 normalDy = dFdyFine(normal);

#if defined(GBUFFER_ALPHA_CLIP) || defined(GBUFFER_LOD_CROSSFADE)
    if (LodCrossfadeShouldDiscard(fragLodCrossfade, floor(gl_FragCoord.xy)))
    {
        discard;
    }
#endif

#ifdef GBUFFER_ALPHA_CLIP
    float coverage = textureSamples.Albedo.a * params.w;
    uint alphaClipMode = uint(mvp.shadingModel.z + 0.5);
    uint alphaClipPhase = uint(mvp.shadingModel.w + 0.5);
    if (AlphaClipShouldDiscard(coverage, mvp.shadingModel.y, alphaClipMode, floor(gl_FragCoord.xy), alphaClipPhase))
    {
        discard;
    }
    if (ViewClearanceShouldDiscard(mvp.viewClearance, fragWorldPos, floor(gl_FragCoord.xy)))
    {
        discard;
    }
#endif

#ifdef GBUFFER_DISSOLVE
    float e = 0.0;
    if (mvp.dissolveParams.x > 0.5)
    {
        float threshold = clamp(params.x, 0.0, 1.0);
        float m = DissolveMask(fragRestPosition, fragDissolveProtect, threshold,
                               mvp.dissolveSweep.xyz, mvp.dissolveSweep.w, mvp.dissolveParams.y);
        if (threshold > 0.0 && threshold < 1.0 && mvp.dissolveParams.w > 0.0)
        {
            m += (AlphaClipInterleavedGradientNoise(floor(gl_FragCoord.xy)) - 0.5) * mvp.dissolveParams.w;
        }
        if (m < 0.0)
        {
            discard;
        }
        e = DissolveEdge(m, mvp.dissolveParams.z, threshold);
    }
#endif

    uint id = uint(mvp.shadingModel.x + 0.5) & GBUFFER_SHADING_MODEL_ID_MASK;
    vec3 albedo = ComposePbrSurfaceAlbedo(fragObjectColor, textureSamples);
    float metallic = textureSamples.Material.x;
    float roughness = textureSamples.Material.y;
#ifdef GBUFFER_DISSOLVE
    // ディゾルブの P0 は消滅度なので濡れの感度には使わず、既定の 1 とする。
    float wetResponse = 1.0;
#else
    float wetResponse = ResolveWetSurfaceResponse(fragInstanceShadingParams.x, mvp.shadingParams.x,
                                                  id == GBUFFER_SHADING_MODEL_DEFAULT_LIT);
#endif
    float rainExposure = PuddleRainExposure(overheadHeightMap, u_PuddleSurface.params, fragWorldPos);
    float puddleCavity = SamplePuddleFieldLayer(puddleField, u_PuddleSurface.params.puddleField, fragWorldPos.xz);
    float puddleCoverage = ApplyWetSurfaceWithPuddle(
        albedo, roughness, metallic, normal, normalize(geometricNormal), u_Environment.params,
        wetResponse, rainExposure, puddleCavity, fragWorldPos.xz);
    float emissiveP1 = id == GBUFFER_SHADING_MODEL_DEFAULT_LIT ? puddleCoverage : params.y;
#ifdef GBUFFER_DISSOLVE
    // 縁でアルベドを灰へ寄せる。
    albedo = mix(albedo, vec3(dot(albedo, vec3(0.2126, 0.7152, 0.0722))), e);
#endif

    outAlbedo = vec4(albedo, textureSamples.Albedo.a);
#ifdef GBUFFER_ALPHA_CLIP
    // 残った画素が天球と判定されないよう、不透明として書く。
    outAlbedo.a = 1.0;
#endif

    const uint receiverFlags = mvp.vtFeedbackParams.w > 0.5 ? GBUFFER_RECEIVER_FLAG_DIFFUSE_ONLY : 0u;
    outNormal = GBufferEncodeNormal(normal, id, receiverFlags);

    // シェーディングモデルに応じた材質値と P0 を書く。
    textureSamples.Material.x = metallic;
    textureSamples.Material.y = roughness;
    outMaterial = GBufferEncodeMaterialForShadingModel(
        SpecularAAFilterMaterial(textureSamples.Material, normalDx, normalDy, mvp.specularAAParams.x), params.x, id);

    // 発光のプリエクスポージャ後の値とシェーディングパラメータ P1 を書く。
    // 発光は Y=1 の色度 × 輝度（nits）にプリエクスポージャを掛ける（Lighting は露出を掛けずに足す）。
    vec3 emissiveChromaticity = fragEmissiveChromaticityAndLuminanceNits.rgb;
    if ((materialTextureFlags & 1u) != 0u)
    {
        emissiveChromaticity *= emissiveSample;
    }
#ifdef GBUFFER_DISSOLVE
    vec3 emissive = ComputePreExposedEmissive(emissiveChromaticity,
                                            fragEmissiveChromaticityAndLuminanceNits.a,
                                            mvp.frameParams.y);
    emissive += ComputePreExposedEmissive(mvp.dissolveEdge.rgb,
                                         min(mvp.dissolveEdge.a, DISSOLVE_EDGE_EMISSIVE_NITS_MAX) * e,
                                         mvp.frameParams.y);
    outEmissive = GBufferEncodeEmissive(min(emissive, vec3(GBUFFER_PRE_EXPOSED_EMISSIVE_MAX)), emissiveP1);
#else
    outEmissive = GBufferEncodeEmissive(ComputePreExposedEmissive(emissiveChromaticity,
                                                                fragEmissiveChromaticityAndLuminanceNits.a,
                                                                mvp.frameParams.y),
                                       emissiveP1);
#endif

    // 前のフレームの w が閾値以下なら履歴なしの印を書く。
    outVelocity = vec2(0.0);
    if (mvp.frameParams.x > 0.5 && abs(fragCurrentClip.w) > 1e-6)
    {
        outVelocity = VelocityFromClip(fragCurrentClip.xy / fragCurrentClip.w, fragPreviousClip);
    }
}
