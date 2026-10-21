#version 450
#ifdef NORVES_SPARSE_RESIDENCY_SHADING
#extension GL_ARB_sparse_texture2 : require
#endif

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragObjectColor;
layout(location = 3) in vec4 fragEmissiveColor;
layout(location = 4) in vec2 fragTexCoord;
layout(location = 5) in vec3 fragViewDir;
layout(location = 6) flat in uint fragDebugPayload;
layout(location = 7) in vec4 fragCurrentClip;
layout(location = 8) in vec4 fragPreviousClip;

// UBOからPOMパラメータを参照（材質の区間ごとの定数。頂点シェーダーの MVPData と先頭が一致する。
// ワールド変換はインスタンスの表にあり、頂点シェーダーが引く）
layout(set = 0, binding = 0) uniform MVPData
{
    mat4 view;
    mat4 projection;
    vec4 cameraPosition;
    vec4 objectColor;
    vec4 emissiveColor;
    vec4 pomParams;  // x=heightScale, y=hasHeightMap, z=debugMode, w=debugPayloadSupported
    mat4 previousView;
    mat4 previousProjection;
    vec4 frameParams; // x=前のカメラがあるか（1/0）, y=発光に掛けるプリエクスポージャ, z=変位の頂点の間隔（UV。0なら変位なし）, w=fragDebugPayload がLODの段か（1/0）
    vec4 materialParams; // x=ORMの1枚が metallicTexture の枠に張られているか（1/0）, y=法線が2チャンネル（BC5）か（1/0）, z=材質のテクスチャが sparse（VT）か（1/0）, w=VT のフィードバックのパラメータ（アルベド。0 は書かない。VirtualTextureFeedback.glsl）
    vec4 vtFeedbackParams; // VT のフィードバックのパラメータ: x=法線, y=ORM（metallicTexture の枠）, z=高さ（0 は書かない）
    vec4 terrainParams[10]; // 地形の 3 層の混ぜ合わせ（Common/TerrainLayerBlend.glsl の TerrainBlendParams。layers.x が 0 なら 1 層の材質）
} mvp;

// PBRテクスチャサンプラー
layout(set = 0, binding = 1) uniform sampler2D albedoTexture;
layout(set = 0, binding = 2) uniform sampler2D normalTexture;
layout(set = 0, binding = 3) uniform sampler2D metallicTexture;
layout(set = 0, binding = 4) uniform sampler2D roughnessTexture;
layout(set = 0, binding = 5) uniform sampler2D aoTexture;
layout(set = 0, binding = 6) uniform sampler2D heightTexture;
// 地形の 2 層目（乾いた土）・3 層目（岩肌）のアルベド・法線・ORM（地形でない材質は 1 層目と同じテクスチャが張られ、読まない）
layout(set = 0, binding = 10) uniform sampler2D terrainDirtAlbedoTexture;
layout(set = 0, binding = 11) uniform sampler2D terrainDirtNormalTexture;
layout(set = 0, binding = 12) uniform sampler2D terrainDirtOrmTexture;
layout(set = 0, binding = 13) uniform sampler2D terrainRockAlbedoTexture;
layout(set = 0, binding = 14) uniform sampler2D terrainRockNormalTexture;
layout(set = 0, binding = 15) uniform sampler2D terrainRockOrmTexture;
// 地形の地表の分類の地図（R = 露岩・G = 崖錐・B = 地表（細粒）・A = 地表（礫）。地図の無い材質はアルベドが張られ、読まない）
layout(set = 0, binding = 16) uniform sampler2D terrainSurfaceMaskTexture;
// 地形の遠景の地図（地図の無い材質はアルベドが張られ、読まない）
layout(set = 0, binding = 17) uniform sampler2D terrainFarMapTexture;

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/SparseResidencySampling.glsl"
#define VT_FEEDBACK_BINDING 7
#include "Common/VirtualTextureFeedback.glsl"
#include "Common/PbrMaterialTextureSampling.glsl"
#include "Common/TerrainLayerBlend.glsl"
#include "Common/ParallaxOcclusionMapping.glsl"
#include "Common/PreExposedEmissive.glsl"
#include "Common/MegaGeometryDebugColor.glsl"

// GBuffer MRT出力
layout(location = 0) out vec4 outAlbedo;    // RT0: Albedo (RGB) + alpha
layout(location = 1) out vec4 outNormal;    // RT1: World Normal (RGB) + 地形の受け手なら 1（A。lighting.frag の影の余裕を変える）
layout(location = 2) out vec4 outMaterial;  // RT2: Metallic(R) / Roughness(G) / AO(B) / unused(A)
layout(location = 3) out vec4 outEmissive;  // RT3: プリエクスポージャ後の発光（RGB） + 未使用
layout(location = 4) out vec2 outVelocity;  // RT4: currentUV - previousUV（gbuffer.frag と同じ）

const float DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3.0;
const float DEBUG_VIEW_MODE_LOD_LEVEL = 8.0;

// gbuffer.frag と同じ式で、現在と直前のフレームのクリップ座標から画面上の動きを求める。
vec2 ComputeVelocity()
{
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
            return velocity;
        }
    }
    return vec2(0.0);
}

// 高さの場で変位させたメッシュでは、頂点の間隔より粗い凹凸の傾きは形（作り直した頂点の法線）が持つ。
// 法線マップの傾き（xy/z）から、描いている段の頂点の間隔のミップ（画面の画素がそれより粗ければその画素の
// ミップ）で引いた粗い傾きを差し引き、残りの細部だけを接空間の法線にする（同じ傾きを二重に掛けない）。
// 段の頂点の間隔は LOD0 の間隔 × 2^段（描画の番号が段を持たない環境では LOD0 とみなす）。
// normalLod は texCoord での法線テクスチャの標本ミップ（textureQueryLOD。main の一様な位置で取った値を渡す）。
vec3 RemoveDisplacedNormalSlope(vec3 tangentNormal, vec2 texCoord, float displacementUVSpacing, float normalLod)
{
    float lodLevel = mvp.frameParams.w > 0.5 ? float(fragDebugPayload) : 0.0;
    float vertexMip = log2(max(displacementUVSpacing * float(textureSize(normalTexture, 0).x), 1.0)) + lodLevel;
    float coarseMip = max(vertexMip, normalLod);
    // 粗い傾きも標本は2チャンネル（BC5）の法線を復号して引く（B は0なので RGB のままでは Z が負になる）。
    vec3 coarseNormal = DecodePbrTangentNormal(SampleMaterialTextureLod(normalTexture, texCoord, coarseMip, mvp.materialParams.z > 0.5),
                                               mvp.materialParams.y > 0.5);
    vec2 detailSlope = tangentNormal.xy / max(tangentNormal.z, 0.1) - coarseNormal.xy / max(coarseNormal.z, 0.1);
    return normalize(vec3(detailSlope, 1.0));
}

void WriteDebugGBuffer(vec3 albedo)
{
    outAlbedo = vec4(albedo, 1.0);
    outNormal = vec4(normalize(fragNormal), 0.0);
    outMaterial = vec4(0.0, 1.0, 1.0, 0.0);
    outEmissive = vec4(0.0, 0.0, 0.0, 1.0);
    outVelocity = ComputeVelocity();
}

void main()
{
    float debugMode = mvp.pomParams.z;
    float debugPayloadSupported = mvp.pomParams.w;

    // POMパラメータ取得
    float heightScale = mvp.pomParams.x;
    float hasHeightMap = mvp.pomParams.y;
    bool bVirtualTexture = mvp.materialParams.z > 0.5;
    float displacementUVSpacing = mvp.frameParams.z;

    // 画面微分を使う量（高さ・各層の標本ミップと POM の後の UV の勾配）は、デバッグ表示の早期 return（描画ごとの UBO の値で分かれる）や
    // 分岐・ループより前の一様な位置で取る。画面微分は一様でない制御フローの中では未定義。
    // 高さのフィードバックの標本ミップは POM の前の元の UV で取る。
    float heightLod = 0.0;
    if (bVirtualTexture && hasHeightMap > 0.5)
    {
        heightLod = textureQueryLOD(heightTexture, fragTexCoord).y;
    }
    // 地形の混ぜ合わせはワールドの位置から標本するので、位置の画面微分もここで取る
    vec3 worldDx = dFdx(fragWorldPos);
    vec3 worldDy = dFdy(fragWorldPos);

    // 余接フレームは元のUVから一度だけ作り、POMと法線マップの両方に使う。
    mat3 TBN = CalculateCotangentFrame(fragNormal, fragWorldPos, fragTexCoord);

    // POM適用: ハイトマップがある場合のみUVオフセット
    vec2 texCoord = fragTexCoord;
    if (hasHeightMap > 0.5)
    {
        texCoord = ApplyParallaxOcclusionMapping(heightTexture, fragTexCoord, TBN, fragViewDir, heightScale, bVirtualTexture);
    }

    // POM の直後に、POM の後の UV の勾配と各層の標本ミップを取る。法線の粗い傾き（変位メッシュ）も法線のミップを使う。
    MaterialTextureFootprint footprint = QueryMaterialTextureFootprint(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord,
        mvp.materialParams.x > 0.5, bVirtualTexture || displacementUVSpacing > 0.0);

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

    // テクスチャサンプリング × オブジェクトカラー（POM補正済みUV使用）
    PbrMaterialTextureSamples textureSamples = SamplePbrMaterialTextures(
        albedoTexture, normalTexture, metallicTexture, roughnessTexture, aoTexture, texCoord, footprint,
        mvp.materialParams.x > 0.5, mvp.materialParams.y > 0.5, bVirtualTexture);
    // 地形の材質は、砂・土・岩肌の 3 層をワールドの位置から標本して傾斜・高さ・ノイズで混ぜ、法線もワールドの向きで得る
    // （上の 1 層の標本は VT の要求にだけ使う）
    TerrainBlendParams terrain = TerrainBlendParams(mvp.terrainParams[0], mvp.terrainParams[1], mvp.terrainParams[2],
                                                    mvp.terrainParams[3], mvp.terrainParams[4], mvp.terrainParams[5],
                                                    mvp.terrainParams[6], mvp.terrainParams[7], mvp.terrainParams[8],
                                                    mvp.terrainParams[9]);
    bool bTerrain = IsTerrainBlendEnabled(terrain);
    vec3 terrainNormal = vec3(0.0, 1.0, 0.0);
    if (bTerrain)
    {
        TerrainSurfaceInput surface;
        surface.WorldPosition = fragWorldPos;
        surface.WorldDx = worldDx;
        surface.WorldDy = worldDy;
        surface.GeometricNormal = fragNormal;
        surface.CameraDistance = length(fragWorldPos - mvp.cameraPosition.xyz);
        textureSamples = ShadeTerrainLayers(terrain, albedoTexture, normalTexture, metallicTexture,
                                            mvp.materialParams.y > 0.5,
                                            terrainDirtAlbedoTexture, terrainDirtNormalTexture, terrainDirtOrmTexture,
                                            terrainRockAlbedoTexture, terrainRockNormalTexture, terrainRockOrmTexture,
                                            terrainSurfaceMaskTexture, terrainFarMapTexture, surface, bVirtualTexture,
                                            terrainNormal);
    }
    else if ((uint(terrain.layers.w + 0.5) & TERRAIN_BLEND_SAND_ALBEDO_SRGB_DECODE) != 0u)
    {
        // 地形でない材質: sRGB で符号化した基本色が UNORM の形式で張られているので、線形へ戻す（MegaMeshMaterial::bAlbedoSrgb）
        textureSamples.Albedo.rgb = TerrainSrgbToLinear(textureSamples.Albedo.rgb);
    }
    // VT のフィードバック: POM の後の UV で欲しいタイルの要求を書く（VT のテクスチャごとに表の番号を持つ）。
    // 高さだけは POM の前の元の UV で書く。
    WriteVirtualTextureFeedback(albedoTexture, texCoord, DecodeVirtualTextureFeedbackParam(mvp.materialParams.w),
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
    outAlbedo = vec4(ComposePbrSurfaceAlbedo(fragObjectColor, textureSamples), textureSamples.Albedo.a);

    // ノーマルマップ適用（POM補正済みUVで標本し、元のUVの余接フレームで変換する）
    vec3 tangentNormal = textureSamples.TangentNormal;
    if (displacementUVSpacing > 0.0)
    {
        tangentNormal = RemoveDisplacedNormalSlope(tangentNormal, texCoord, displacementUVSpacing, footprint.NormalLod);
    }
    vec3 normal = bTerrain ? terrainNormal : ApplyTangentSpaceNormal(TBN, tangentNormal);
    outNormal = vec4(normal, bTerrain ? 1.0 : 0.0);

    // PBRマテリアルパラメータ（POM補正済みUV使用）
    outMaterial = vec4(textureSamples.Material, 0.0);

    // 発光: Y=1 の色度 × 輝度（nits）にプリエクスポージャを掛けて書く（gbuffer.frag と同じ）
    outEmissive = vec4(ComputePreExposedEmissive(fragEmissiveColor.rgb,
                                                 fragEmissiveColor.a,
                                                 mvp.frameParams.y),
                       1.0);
    outVelocity = ComputeVelocity();
}
