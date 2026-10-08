#version 450

layout(location = 0) in vec2 fragUV;

// GBufferテクスチャ
layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D gbufferDepth;

// ライティングパラメータ
layout(std140, set = 0, binding = 4) uniform LightingParams
{
    mat4 invViewProjection;
    vec4 cameraPosition;    // xyz=position, w=unused
    vec4 ambientColor;      // xyz=color, w=intensity
    mat4 lightView[4];      // 4カスケードのライトビュー行列
    mat4 lightProjection[4];// 4カスケードのライトプロジェクション行列
    vec4 shadowSplitDistances[2]; // x/y/z/w = split 0..3, split 4 in [1].x
    uint cascadeCount;       // 完全なCSM公開値の場合だけ4
    uint lightCount;
    uint bShadowEnabled;    // シャドウマップ有効フラグ
    uint prefilteredSpecularMipLevels; // prefiltered specular mip level count
    uint bIBLEnabled;       // IBL有効フラグ
    uint bSSAOEnabled;      // SSAO有効フラグ
    uint bNeuralBRDFEnabled; // Neural BRDF有効フラグ
    uint debugViewMode;
    float preExposure;
    uint shadowPadding0;
    float staticEnvironmentScale; // 空が無効なときの静的HDRの背景に掛ける倍率（既定1。空が有効なら1）
    float staticEnvironmentMaxRadiance; // 静的HDRの鏡面と背景の放射輝度の上限（倍率を掛ける前。0は上限なし）
    vec4 skySunDirectionAndCosRadius; // xyz=太陽方向, w=cos(太陽ディスク角半径)
    vec4 cameraForward; // xyz=CSM分割に使うカメラ前方単位ベクトル
    vec4 ddgiVolumeOrigin; // xyz=DDGI volume原点
    vec4 ddgiProbeSpacing; // xyz=DDGI probe間隔
    uvec4 ddgiProbeCounts; // xyz=格子数, w=probe総数
    uvec4 ddgiInfo; // x=DDGI有効フラグ
    mat4 viewProjection; // 接触影のレイを画面へ写すViewProjection（TAAのジッタ込み）
    vec4 contactShadowParams; // x=レイの長さ(m、0で無効), y=雑音の時間のずらし, z=遮る物体の厚さの下限(m)
} params;

// ライトデータ構造
struct LightData
{
    vec4 position;      // xyz=position, w=type (0:Dir, 1:Point, 2:Spot)
    vec4 direction;     // xyz=direction, w=innerAngle
    vec4 chromaticityAndIntensity; // xyz=Y=1 chromaticity, w=canonical lux/cd
    vec4 attenuation;   // x=range, y=outerAngle, z=CSM/RT影を掛ける灯なら1, w=キューブシャドウの番号+1（無ければ0）
};

// ライト配列
layout(std430, set = 0, binding = 5) readonly buffer LightBuffer
{
    LightData lights[];
} lightBuffer;

// 4層CSMシャドウマップ
layout(set = 0, binding = 6) uniform sampler2DArray shadowMap;

// GBufferエミッシブ
layout(set = 0, binding = 7) uniform sampler2D gbufferEmissive; // プリエクスポージャ後の発光

// IBL (Image-Based Lighting)
layout(set = 0, binding = 8) uniform sampler2D envMap;    // HDR環境マップ（equirectangular）
layout(set = 0, binding = 9) uniform sampler2D brdfLUT;   // BRDF LUT（split-sum近似）

// Diffuse irradiance and GGX prefiltered specular resources
layout(set = 0, binding = 12) uniform sampler2D diffuseIrradiance;
layout(set = 0, binding = 13) uniform sampler2D prefilteredSpecular;
layout(set = 0, binding = 14) uniform sampler2D skySunDisk;
layout(set = 0, binding = 15) uniform sampler2D skyTransmittance;
layout(set = 0, binding = 16) uniform sampler2D rayTracingShadowVisibility;
layout(set = 0, binding = 17) uniform sampler2DArray ddgiIrradianceAtlas;
layout(set = 0, binding = 18) uniform sampler2DArray ddgiDistanceAtlas;
layout(set = 0, binding = 19) uniform sampler2D rtgiDiffuseIndirect;
// 点光源のキューブシャドウ（キューブiに光源からの距離/範囲を格納。無いフレームは距離1の既定値）
layout(set = 0, binding = 20) uniform samplerCubeArray pointShadowCubes;

// SSAO (Screen-Space Ambient Occlusion)
layout(set = 0, binding = 10) uniform sampler2D ssaoTexture;

// 太陽の VSM（--shadow-method=vsm）。無効のとき（control.x = 0）はページの表・プールを読まない
#include "Common/VirtualShadowMapParams.glsl"
layout(std140, set = 0, binding = 21) uniform VsmSampleBlock
{
    VsmSampleParams vsm;
} vsmBlock;
// 点光源の VSM（--point-shadow-method=vsm）。灯の数（header.x）が 0 のときは読まず、点光源の影はキューブ。
// ページの表・プール・スライスの表は太陽の VSM と同じもの（点光源のスライスは太陽の段の後ろに並ぶ）
layout(std140, set = 0, binding = 26) uniform VsmPointSampleBlock
{
    VsmPointSampleParams point;
} vsmPointBlock;
layout(std430, set = 0, binding = 22) readonly buffer VsmPageTableBuffer
{
    uint vsmPageTable[];
};
layout(std430, set = 0, binding = 23) readonly buffer VsmPoolBuffer
{
    uint vsmPool[];
};
// スライスの表（ページの一辺・texel・範囲の原点・ページの表の先頭。無効のときは読まない）
#define VSM_SLICE_BINDING 25
#include "Common/VirtualShadowMapSlice.glsl"
// 太陽の VSM の読み出しで、自分の段のページが無く粗い段へ逃げた PCF の標本の数（[0]）。ホストが数フレーム後に読み戻す。
// 断片シェーダーの storage の書き込みを使えるデバイス（NORVES_VSM_STATS が定義される。VT のフィードバックとは独立）だけで数える
#ifdef NORVES_VSM_STATS
layout(std430, set = 0, binding = 24) buffer VsmLightingStatsBuffer
{
    uint vsmLightingStats[];
};
#endif

// Neural BRDF重みデータ（Disney BRDF MLP）
layout(set = 0, binding = 11) readonly buffer NeuralBRDFWeights
{
    float data[];
} neuralBRDF;

layout(location = 0) out vec4 outColor;
// SSRPass が環境光の鏡面反射を画面の反射へ置き換えるための出力。outIndirectSpecular は環境光の鏡面反射として
// outColor へ足した値（露出後）、outSpecularReflectance はその反射率（鏡面の遮蔽込み、無次元、RGB）。
// 環境光を求めない表示（デバッグ・検証の表示、空）では0のままで、SSRは何も足さない。
layout(location = 1) out vec4 outIndirectSpecular;
layout(location = 2) out vec4 outSpecularReflectance;

// ========================================
// PBR関連関数
// ========================================

#include "Common/PbrMaterialEvaluation.glsl"
#include "Common/PointShadow.glsl"
// probeの向きの重み（wrap shading）の下限（RTXGIと同じ0.2）。面の裏側のprobeも少し使い、1つのprobeに
// 重みが集まって斑点になるのを防ぐ。
const float DDGI_WRAP_WEIGHT_FLOOR = 0.2;
// 表面の偏り（surface bias）の大きさ。probe間隔の最小値に対する比で、法線の方向へずらす。大きさは
// Majercikらの自己遮蔽のずらし量（0.3 × 0.75 × 最小間隔）と同じで、視点の側の成分も法線の方向へ置く。
const float DDGI_NORMAL_BIAS_FRACTION = 0.225;
// これより弱いprobeの重みを3乗の割合で押しつぶす（RTXGIのcrush threshold）。
const float DDGI_WEIGHT_CRUSH_THRESHOLD = 0.2;
const uint DEBUG_VIEW_MODE_NORMAL = 0u;
const uint DEBUG_VIEW_MODE_UNLIT = 1u;
const uint DEBUG_VIEW_MODE_WIREFRAME = 2u;
const uint DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3u;
const uint DEBUG_VIEW_MODE_GBUFFER_ALBEDO = 4u;
const uint DEBUG_VIEW_MODE_GBUFFER_NORMAL = 5u;
const uint DEBUG_VIEW_MODE_GBUFFER_MATERIAL = 6u;
const uint DEBUG_VIEW_MODE_GBUFFER_DEPTH = 7u;
const uint DEBUG_VIEW_MODE_LOD_LEVEL = 8u;
const uint DEBUG_VIEW_MODE_POINT_SHADOW_DISTANCE = 9u;
const uint DEBUG_VIEW_MODE_AMBIENT_OCCLUSION = 10u;
const uint DEBUG_VIEW_MODE_COUNT = 11u;
const uint DEBUG_VIEW_MODE_VALIDATION_LAMBERT = 253u;
const uint DEBUG_VIEW_MODE_VALIDATION_PBR = 254u;
const uint DEBUG_VIEW_MODE_RAW250 = 250u;
const uint DEBUG_VIEW_MODE_RAW251 = 251u;
const uint DEBUG_VIEW_MODE_RAW252 = 252u;
const uint DEBUG_VIEW_MODE_R5_RASTER_HARD_SHADOW = 246u;
// 検証表示245: CSM・RT影を掛ける方向光（空の太陽）の可視（面が背を向ければ0）。露出を掛けない。
const uint DEBUG_VIEW_MODE_R7_SUN_VISIBILITY = 245u;
const uint DEBUG_VIEW_MODE_R5_RAY_TRACING_HARD_SHADOW = 247u;
const uint DEBUG_VIEW_MODE_R5_RAY_TRACING_VISIBILITY = 248u;
const uint DEBUG_VIEW_MODE_R5_RASTER_FALLBACK = 249u;

bool IsR5HardShadowValidationMode()
{
    return params.debugViewMode == DEBUG_VIEW_MODE_R5_RASTER_HARD_SHADOW ||
           params.debugViewMode == DEBUG_VIEW_MODE_R5_RAY_TRACING_HARD_SHADOW ||
           params.debugViewMode == DEBUG_VIEW_MODE_R5_RASTER_FALLBACK;
}

bool ShouldApplySceneColorPreExposure()
{
    return params.debugViewMode == DEBUG_VIEW_MODE_NORMAL ||
           params.debugViewMode == DEBUG_VIEW_MODE_RAW252 ||
           params.debugViewMode == DEBUG_VIEW_MODE_VALIDATION_LAMBERT ||
           params.debugViewMode == DEBUG_VIEW_MODE_VALIDATION_PBR ||
           params.debugViewMode == DEBUG_VIEW_MODE_R5_RASTER_HARD_SHADOW ||
           params.debugViewMode == DEBUG_VIEW_MODE_R5_RAY_TRACING_HARD_SHADOW ||
           params.debugViewMode == DEBUG_VIEW_MODE_R5_RASTER_FALLBACK;
}

vec3 ApplySceneColorPreExposure(vec3 sceneColor)
{
    if (ShouldApplySceneColorPreExposure())
    {
        return sceneColor * params.preExposure;
    }

    return sceneColor;
}

// GBufferの発光は書き込み時に同じフレームのプリエクスポージャ（params.preExposure と同じ値）を
// 掛けてある。露出を掛ける表示ではそのまま足し、掛けない表示では物理の値へ戻す。
vec3 ResolveGBufferEmissiveSceneColor(vec3 preExposedEmissive)
{
    if (ShouldApplySceneColorPreExposure())
    {
        return preExposedEmissive;
    }

    return preExposedEmissive / max(params.preExposure, 1.0e-6);
}

// ========================================
// Equirectangular UV from direction vector
// ========================================
vec2 EquirectangularUV(vec3 dir)
{
    // atan(z, x) → [-PI, PI] → [0, 1]
    // asin(y) → [-PI/2, PI/2] → [0, 1]
    // Vulkan座標系ではY軸が反転しているため -dir.y を使用
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(-dir.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989); // 1/(2*PI), 1/PI
    uv += 0.5;
    return uv;
}

// 静的HDR環境の色の最大の成分が上限を超えたら、色相を保ったまま上限まで縮める（0は上限なし）。
// 夜の環境光に残った沈みかけの太陽（倍率を掛ける前で約 15638）が、鏡面の反射や背景で極端に明るくなるのを防ぐ。
vec3 LimitStaticEnvironmentRadiance(vec3 radiance)
{
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    if (params.staticEnvironmentMaxRadiance > 0.0 && peak > params.staticEnvironmentMaxRadiance)
    {
        radiance *= params.staticEnvironmentMaxRadiance / peak;
    }
    return radiance;
}

vec3 SamplePrefilteredSpecular(vec3 direction, float roughness)
{
    float lod = roughness * float(params.prefilteredSpecularMipLevels - 1u);
    vec2 uv = EquirectangularUV(direction);
    return LimitStaticEnvironmentRadiance(textureLod(prefilteredSpecular, uv, lod).rgb);
}

// ========================================
// 深度からワールド座標を復元
// ========================================
vec3 ReconstructWorldPosition(vec2 uv, float depth)
{
    // UV→NDC座標変換 (VulkanのY軸反転を考慮)
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = params.invViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

float ComputeDebugDepth01(vec2 uv, float depth)
{
    vec3 worldPos = ReconstructWorldPosition(uv, depth);
    float cameraDistance = distance(params.cameraPosition.xyz, worldPos);
    float depth01 = cameraDistance / (cameraDistance + 25.0);
    return clamp(depth01, 0.0, 1.0);
}

// ========================================
// ライト減衰計算
// ========================================
float CalculateInverseSquareAttenuation(float distance)
{
    return 1.0 / max(distance * distance, 0.01 * 0.01);
}

float CalculateRangeWindow(float distance, float range)
{
    float factor = max(1.0 - pow(distance / max(range, 0.0001), 4.0), 0.0);
    return factor * factor;
}

// 太陽のCSMの評価（PCSS）。影の地図・行列・分割の距離・カメラ・検証モードの読み方をここで与える。
#define SUN_CSM_SHADOW_MAP shadowMap
#define SUN_CSM_LIGHT_VIEW(i) params.lightView[i]
#define SUN_CSM_LIGHT_PROJECTION(i) params.lightProjection[i]
#define SUN_CSM_SPLIT_DISTANCE(i) ((i) < 4u ? params.shadowSplitDistances[0][i] : params.shadowSplitDistances[1][(i) - 4u])
#define SUN_CSM_CAMERA_POSITION params.cameraPosition.xyz
#define SUN_CSM_CAMERA_FORWARD params.cameraForward.xyz
#define SUN_CSM_ENABLED (params.bShadowEnabled != 0u)
#define SUN_CSM_CASCADE_COUNT params.cascadeCount
#define SUN_CSM_HARD_SHADOW_MODE IsR5HardShadowValidationMode()
#include "Common/SunShadowCsm.glsl"

// 太陽の VSM の評価。ページの表・プール・パラメータの読み方をここで与える。
#define VSM_PARAMS vsmBlock.vsm
#define VSM_SLICE(i) vsmSlices[i]
#define VSM_PAGE_TABLE(i) vsmPageTable[i]
#define VSM_POOL(i) vsmPool[i]
#ifdef NORVES_VSM_STATS
#define VSM_COUNT_FALLBACK() atomicAdd(vsmLightingStats[0], 1u)
#endif
#include "Common/VirtualShadowMap.glsl"

// 点光源の VSM の評価。読み方は太陽と同じ（ページの表・プール・スライスの表）。逃げた標本は統計の語 1 に数える
#define VSM_POINT_PARAMS vsmPointBlock.point
#ifdef NORVES_VSM_STATS
#define VSM_COUNT_POINT_FALLBACK() atomicAdd(vsmLightingStats[1], 1u)
#endif
#include "Common/VirtualShadowMapPoint.glsl"

// 太陽の影の可視度。--shadow-method=vsm で VSM が使えるときは VSM、それ以外（R5 のハードシャドウの検証表示を含む）は CSM。
float CalculateSunShadow(vec3 worldPos, vec3 normal)
{
    if (vsmBlock.vsm.control.x != 0u && !IsR5HardShadowValidationMode())
    {
        float texelMeters = 0.0;
        return VsmSampleSunShadow(worldPos, normal, texelMeters);
    }
    return CalculateShadow(worldPos, normal);
}

// ========================================
// 接触影（Contact Shadow）
// ========================================
// 受け手から光の方向へ短く（contactShadowParams.x m）レイを進め、各段の点を画面へ写して
// GBufferの深度と比べる。点が深度の面より奥（厚さcontactShadowParams.z以内）にあり、かつ
// その画素の法線で決まる面の内側にあれば遮られたとみなす。CSM・キューブシャドウの解像度では
// 出ない接地部の細い影を補い、影の結果へ掛ける。
const uint CONTACT_SHADOW_STEP_COUNT = 12u;

// 画素の位置で決まる雑音（Jimenez 2014のinterleaved gradient noise）。段の位置をずらし、
// 段の間隔の階段状の境目をTAAで均せる細かな雑音にする。
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

float CalculateContactShadow(vec3 worldPos, float depth, vec3 normal, vec3 L, float maxLength)
{
    float rayLength = min(params.contactShadowParams.x, maxLength);
    float thickness = params.contactShadowParams.z;
    if (!(rayLength > 0.0) || !(thickness > 0.0) || dot(normal, L) <= 0.0)
    {
        return 1.0;
    }

    vec3 cameraPos = params.cameraPosition.xyz;
    vec3 forward = params.cameraForward.xyz;
    if (!(dot(worldPos - cameraPos, forward) > 0.0))
    {
        return 1.0;
    }

    ivec2 depthSize = textureSize(gbufferDepth, 0);
    // 受け手の位置での1画素の世界の幅（同じ深度で横に1画素ずらした点との距離）
    vec3 neighborPos = ReconstructWorldPosition(fragUV + vec2(1.0 / float(depthSize.x), 0.0), depth);
    float pixelWorldSize = length(neighborPos - worldPos);
    if (!IsFiniteShadowValue(pixelWorldSize) || pixelWorldSize <= 0.0)
    {
        return 1.0;
    }
    // レイが画面上で数画素にしかならない遠くでは、段の比較が画素の粗さに負けるので効果を消していく
    // （2画素で0、6画素で1）。
    float resolveFade = clamp(rayLength / pixelWorldSize * 0.25 - 0.5, 0.0, 1.0);
    if (resolveFade <= 0.0)
    {
        return 1.0;
    }

    // 起点は法線の方向へ0.25画素だけずらす。受け手自身の面での縞は下の面の内側の判定で防ぐ。
    // 大きくずらすと、物体のすぐ外を通るレイが物体の端を切り、影の縁が外へ広がる。
    vec3 origin = worldPos + normal * (pixelWorldSize * 0.25);
    float jitter = fract(InterleavedGradientNoise(gl_FragCoord.xy) + params.contactShadowParams.y);
    float stepLength = rayLength / float(CONTACT_SHADOW_STEP_COUNT);
    // 遮る物体の厚さ。1段の長さと1画素の幅より薄いと、面へ入った段を見落とすので下限にする。
    thickness = max(thickness, max(stepLength, pixelWorldSize));

    for (uint stepIndex = 0u; stepIndex < CONTACT_SHADOW_STEP_COUNT; ++stepIndex)
    {
        float t = (float(stepIndex) + jitter) * stepLength;
        vec3 samplePos = origin + L * t;
        vec4 sampleClip = params.viewProjection * vec4(samplePos, 1.0);
        if (!(sampleClip.w > 0.0))
        {
            break;
        }
        vec2 sampleUV = sampleClip.xy / sampleClip.w * 0.5 + 0.5;
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThanEqual(sampleUV, vec2(1.0))))
        {
            break;
        }
        ivec2 texel = clamp(ivec2(sampleUV * vec2(depthSize)), ivec2(0), depthSize - ivec2(1));
        float sceneDepth = texelFetch(gbufferDepth, texel, 0).r;
        if (sceneDepth >= 1.0)
        {
            continue; // 空は遮らない
        }
        vec3 scenePos = ReconstructWorldPosition((vec2(texel) + 0.5) / vec2(depthSize), sceneDepth);
        float behind = dot(samplePos - scenePos, forward);
        if (behind <= 0.0 || behind >= thickness)
        {
            continue;
        }
        // 画素の面を、その画素の位置と法線の平面とみなし、段の点がその内側（半画素ぶんの余裕より
        // 深い）にあるときだけ遮りとする。画素の中心の深度だけで比べると、物体の手前の面のすぐ外を
        // かすめる段も、画素の中の面の傾きの差で奥と判定され、影の縁が1画素ほど外へ広がる。
        vec3 sceneNormal = texelFetch(gbufferNormal, texel, 0).xyz;
        float sceneNormalLength = length(sceneNormal);
        if (!(sceneNormalLength > 0.0) ||
            dot(samplePos - scenePos, sceneNormal / sceneNormalLength) >= -0.5 * pixelWorldSize)
        {
            continue;
        }
        // レイの終わり近くの遮りは弱め、長さで切れた影の端を柔らかくする
        float occlusion = 1.0 - smoothstep(0.7, 1.0, t / rayLength);
        return 1.0 - occlusion * resolveFade;
    }
    return 1.0;
}

// ========================================
// Neural Disney BRDF評価
// 事前学習済みMLP（30→32→32→32→4）による推論
// 入力: NdotL, NdotV, NdotH, LdotH, roughness
// 出力: 4コンポーネント (x=diffuse_scale, y=specular_GD, z=fresnel, w=clearcoat)
// ========================================

// 重みオフセット定数（FP32 float配列インデックス）
// Layer 0 (30→32): weights[0..959], biases[960..991]
// Layer 1 (32→32): weights[992..2015], biases[2016..2047]
// Layer 2 (32→32): weights[2048..3071], biases[3072..3103]
// Layer 3 (32→4):  weights[3104..3231], biases[3232..3235]

vec4 EvaluateNeuralBRDF(float NdotL, float NdotV, float NdotH, float LdotH, float roughness)
{
    // 周波数エンコーディング: 5入力 × 3周波数 × 2(sin/cos) = 30ニューロン
    float encoded[30];
    float features[5] = float[5](NdotL, NdotV, NdotH, LdotH, roughness);

    for (int i = 0; i < 5; i++)
    {
        for (int k = 0; k < 3; k++)
        {
            float freq = exp2(float(k)) * PI * features[i];
            encoded[i * 6 + k * 2]     = sin(freq);
            encoded[i * 6 + k * 2 + 1] = cos(freq);
        }
    }

    // Layer 0: 30→32, ReLU
    float h0[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[960 + o];
        for (int i = 0; i < 30; i++)
        {
            sum += encoded[i] * neuralBRDF.data[o * 30 + i];
        }
        h0[o] = max(sum, 0.0);
    }

    // Layer 1: 32→32, ReLU
    float h1[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[2016 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h0[i] * neuralBRDF.data[992 + o * 32 + i];
        }
        h1[o] = max(sum, 0.0);
    }

    // Layer 2: 32→32, ReLU
    float h2[32];
    for (int o = 0; o < 32; o++)
    {
        float sum = neuralBRDF.data[3072 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h1[i] * neuralBRDF.data[2048 + o * 32 + i];
        }
        h2[o] = max(sum, 0.0);
    }

    // Layer 3: 32→4, exp活性化
    vec4 result;
    for (int o = 0; o < 4; o++)
    {
        float sum = neuralBRDF.data[3232 + o];
        for (int i = 0; i < 32; i++)
        {
            sum += h2[i] * neuralBRDF.data[3104 + o * 32 + i];
        }
        result[o] = exp(sum);
    }

    return result;
}

bool IsRaw252ParameterInvariantValid()
{
    return params.bIBLEnabled != 0u &&
           params.bSSAOEnabled == 0u &&
           params.bNeuralBRDFEnabled == 0u &&
           params.lightCount == 0u &&
           abs(params.ambientColor.w - 1.0) <= 0.0001;
}

// GTAOの多重反射の近似（Jimenez et al. 2016）。遮られた方向から来る光も周りの面で何度か反射して
// 届くので、アルベドが高い面ほど可視率を持ち上げる（色ごと）。可視率1では1のまま。
vec3 GTAOMultiBounce(float visibility, vec3 albedo)
{
    vec3 a = 2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c = 2.7552 * albedo + 0.6903;
    vec3 bounced = ((visibility * a + b) * visibility + c) * visibility;
    return clamp(max(vec3(visibility), bounced), vec3(0.0), vec3(1.0));
}

vec3 EvaluateDiffuseEndpoint(vec3 irradiance,
                             vec3 albedo,
                             float metallic,
                             vec2 brdf)
{
    float Ess = max(brdf.x + brdf.y, 0.0001);
    vec3 F0d = vec3(0.04);
    vec3 CompD = vec3(1.0) + F0d * (1.0 - Ess) / Ess;
    vec3 Ed = clamp((F0d * brdf.x + brdf.y) * CompD,
                    vec3(0.0), vec3(1.0));
    return irradiance * (albedo / PI) *
           (1.0 - metallic) * (vec3(1.0) - Ed);
}

// 環境光の鏡面反射の反射率（split-sumのDFGに多重散乱の補償を掛けたもの。誘電体はF0=0.04、金属はalbedo）。
// IBL・RTGIの鏡面の項と、SSRへ渡す反射率で同じ式を使う。
vec3 EvaluateSpecularReflectance(vec3 albedo,
                                 float metallic,
                                 vec2 brdf)
{
    float Ess = max(brdf.x + brdf.y, 0.0001);
    vec3 F0d = vec3(0.04);
    vec3 F0c = albedo;
    vec3 CompD = vec3(1.0) + F0d * (1.0 - Ess) / Ess;
    vec3 CompC = vec3(1.0) + F0c * (1.0 - Ess) / Ess;
    vec3 Ed = clamp((F0d * brdf.x + brdf.y) * CompD,
                    vec3(0.0), vec3(1.0));
    vec3 Ec = clamp((F0c * brdf.x + brdf.y) * CompC,
                    vec3(0.0), vec3(1.0));
    return (1.0 - metallic) * Ed + metallic * Ec;
}

vec3 EvaluateIblEndpoint(vec3 albedo,
                         float metallic,
                         float roughness,
                         vec3 N,
                         vec3 V,
                         vec3 diffuseAO,
                         float specularAO,
                         float iblIntensity,
                         vec2 brdf,
                         bool bUseDDGI,
                         vec3 ddgiIrradiance)
{
    vec3 irradiance = bUseDDGI
        ? ddgiIrradiance
        : textureLod(diffuseIrradiance, EquirectangularUV(N), 0.0).rgb;
    vec3 diffuseIBL = EvaluateDiffuseEndpoint(irradiance, albedo, metallic, brdf);

    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = SamplePrefilteredSpecular(R, roughness);
    vec3 specularIBL = prefilteredColor * EvaluateSpecularReflectance(albedo, metallic, brdf);
    if (bUseDDGI)
    {
        return diffuseIBL * diffuseAO + specularIBL * specularAO * iblIntensity;
    }
    return (diffuseIBL * diffuseAO + specularIBL * specularAO) * iblIntensity;
}

vec3 EvaluateRTGIEndpoint(vec3 albedo,
                          float metallic,
                          float roughness,
                          vec3 N,
                          vec3 V,
                          float ao,
                          float specularAO,
                          float iblIntensity,
                          vec2 brdf,
                          vec3 rtgiDiffuseRadiance)
{
    vec3 diffuse = max(rtgiDiffuseRadiance, vec3(0.0)) * ao;
    vec3 specular = vec3(0.0);
    if (params.bIBLEnabled != 0u)
    {
        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = SamplePrefilteredSpecular(R, roughness);
        specular = prefilteredColor *
                   EvaluateSpecularReflectance(albedo, metallic, brdf) *
                   specularAO * iblIntensity;
    }
    return diffuse + specular;
}

float SignNotZero(float value)
{
    return value < 0.0 ? -1.0 : 1.0;
}

vec2 EncodeDDGIOctahedralDirection(vec3 direction)
{
    float denominator = abs(direction.x) + abs(direction.y) + abs(direction.z);
    vec2 encoded = direction.xy / max(denominator, 1.0e-8);
    if (direction.z < 0.0)
    {
        encoded = (1.0 - abs(encoded.yx)) *
                  vec2(SignNotZero(encoded.x), SignNotZero(encoded.y));
    }
    return encoded * 0.5 + 0.5;
}

vec2 DDGIAtlasUv(vec2 octahedralUv)
{
    vec2 pixelCenter = octahedralUv * 6.0 + 1.0;
    return pixelCenter / 8.0;
}

float SampleDDGIVisibility(uint probeIndex,
                           vec3 probeToPointDirection,
                           float pointDistance)
{
    vec2 moments = textureLod(ddgiDistanceAtlas,
                              vec3(DDGIAtlasUv(EncodeDDGIOctahedralDirection(
                                  probeToPointDirection)),
                                   float(probeIndex)),
                              0.0).rg;
    if (any(isnan(moments)) || any(isinf(moments)))
    {
        return 0.05;
    }

    float meanDistance = max(moments.x, 0.0);
    float variance = max(moments.y - meanDistance * meanDistance, 1.0e-4);
    if (pointDistance <= meanDistance)
    {
        return 1.0;
    }

    // 平均距離より遠い点は、分布のChebyshevの上限の3乗で弱める（RTXGIと同じ。下限は設けず、弱い重みは
    // 呼び側で押しつぶす）。
    float delta = pointDistance - meanDistance;
    float chebyshev = variance / (variance + delta * delta);
    return max(chebyshev * chebyshev * chebyshev, 0.0);
}

// DDGIの体積の照度を、周りの8つのprobeから補間して求める（RTXGIのDDGIGetVolumeIrradianceと同じ手順）。
// 点を法線の方向へずらした点（surface bias）で格子の区画・probeからの距離・可視を求め、面の裏にあるprobeが
// 可視の判定で外れるようにする（ずらさないと、probeから見た面の距離と面上の点の距離が等しく、可視が不安定に
// なる）。視点の側へはずらさない（拡散の照度がカメラの位置で変わり、カメラに近い側のprobeへ補間が寄る）。
// 向きの重み（wrap shading）は元の点から見たprobeの向きで求め、弱い重みを押しつぶし、平方根の空間で補間する
// （暗い側の斑点を抑える）。体積の外の点は偏りの前の位置で判定してfalseを返す（体積の中へ寄せない）。
// 壁の外や閉じた物体の内側にある無効なprobe（照度atlasのalphaが0）は使わない。
bool TrySampleDDGIIrradiance(vec3 worldPosition,
                             vec3 surfaceNormal,
                             out vec3 irradiance)
{
    irradiance = vec3(0.0);
    if (params.ddgiInfo.x == 0u || params.ddgiProbeCounts.w == 0u ||
        any(equal(params.ddgiProbeCounts.xyz, uvec3(0u))) ||
        any(isnan(params.ddgiVolumeOrigin.xyz)) ||
        any(isinf(params.ddgiVolumeOrigin.xyz)) ||
        any(isnan(params.ddgiProbeSpacing.xyz)) ||
        any(isinf(params.ddgiProbeSpacing.xyz)) ||
        any(lessThanEqual(params.ddgiProbeSpacing.xyz, vec3(0.0))) ||
        any(isnan(worldPosition)) || any(isinf(worldPosition)) ||
        any(isnan(surfaceNormal)) || any(isinf(surfaceNormal)))
    {
        return false;
    }

    float normalLengthSquared = dot(surfaceNormal, surfaceNormal);
    if (normalLengthSquared <= 1.0e-8 || isnan(normalLengthSquared) ||
        isinf(normalLengthSquared))
    {
        return false;
    }
    vec3 normal = surfaceNormal * inversesqrt(normalLengthSquared);
    vec3 gridMaximum = vec3(params.ddgiProbeCounts.xyz - uvec3(1u));
    vec3 gridPosition = (worldPosition - params.ddgiVolumeOrigin.xyz) /
                        params.ddgiProbeSpacing.xyz;
    if (any(isnan(gridPosition)) || any(isinf(gridPosition)) ||
        any(lessThan(gridPosition, vec3(0.0))) ||
        any(greaterThan(gridPosition, gridMaximum)))
    {
        return false;
    }

    float minimumSpacing = min(params.ddgiProbeSpacing.x,
                               min(params.ddgiProbeSpacing.y, params.ddgiProbeSpacing.z));
    vec3 biasedPosition = worldPosition + normal * (DDGI_NORMAL_BIAS_FRACTION * minimumSpacing);
    vec3 biasedGridPosition = clamp((biasedPosition - params.ddgiVolumeOrigin.xyz) /
                                        params.ddgiProbeSpacing.xyz,
                                    vec3(0.0),
                                    gridMaximum);

    ivec3 baseProbe = ivec3(floor(biasedGridPosition));
    vec3 alpha = clamp(biasedGridPosition - vec3(baseProbe), vec3(0.0), vec3(1.0));
    vec2 irradianceUv = DDGIAtlasUv(EncodeDDGIOctahedralDirection(normal));
    vec3 accumulatedIrradiance = vec3(0.0);
    float accumulatedWeight = 0.0;
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        uvec3 offset = uvec3(corner & 1u,
                             (corner >> 1u) & 1u,
                             (corner >> 2u) & 1u);
        uvec3 probeCoordinates = min(uvec3(baseProbe) + offset,
                                     params.ddgiProbeCounts.xyz - uvec3(1u));
        uint probeIndex = probeCoordinates.x +
                          probeCoordinates.y * params.ddgiProbeCounts.x +
                          probeCoordinates.z * params.ddgiProbeCounts.x *
                              params.ddgiProbeCounts.y;
        if (probeIndex >= params.ddgiProbeCounts.w)
        {
            return false;
        }

        vec4 probeSample = textureLod(ddgiIrradianceAtlas,
                                      vec3(irradianceUv, float(probeIndex)),
                                      0.0);
        if (any(isnan(probeSample)) || any(isinf(probeSample)))
        {
            return false;
        }
        if (probeSample.a < 0.5)
        {
            continue;
        }

        // 軸ごとの下限はRTXGIと同じ0.001。点がprobeの面の上にあっても隣の層のprobeを残す。
        vec3 trilinear = max(vec3(0.001), mix(vec3(1.0) - alpha, alpha, vec3(offset)));
        float trilinearWeight = trilinear.x * trilinear.y * trilinear.z;

        vec3 probePosition = params.ddgiVolumeOrigin.xyz +
                             params.ddgiProbeSpacing.xyz * vec3(probeCoordinates);
        vec3 pointToProbe = probePosition - worldPosition;
        float pointToProbeLength = length(pointToProbe);
        vec3 pointToProbeDirection = pointToProbeLength > 1.0e-6
            ? pointToProbe / pointToProbeLength
            : normal;
        float wrapShading = (dot(pointToProbeDirection, normal) + 1.0) * 0.5;
        float weight = wrapShading * wrapShading + DDGI_WRAP_WEIGHT_FLOOR;

        vec3 probeToBiasedPoint = biasedPosition - probePosition;
        float biasedDistance = length(probeToBiasedPoint);
        if (isnan(biasedDistance) || isinf(biasedDistance))
        {
            continue;
        }
        vec3 probeToBiasedDirection = biasedDistance > 1.0e-6
            ? probeToBiasedPoint / biasedDistance
            : -normal;
        weight *= SampleDDGIVisibility(probeIndex, probeToBiasedDirection, biasedDistance);
        weight = max(weight, 1.0e-6);
        if (weight < DDGI_WEIGHT_CRUSH_THRESHOLD)
        {
            weight *= weight * weight /
                      (DDGI_WEIGHT_CRUSH_THRESHOLD * DDGI_WEIGHT_CRUSH_THRESHOLD);
        }
        weight *= trilinearWeight;
        if (weight <= 0.0)
        {
            continue;
        }

        accumulatedIrradiance += sqrt(max(probeSample.rgb, vec3(0.0))) * weight;
        accumulatedWeight += weight;
    }

    // 押しつぶした重みは極めて小さくなりうるが、正であれば正規化する（全probeが可視で外れた点も、最も
    // 見込みのあるprobeの照度を使う）。
    if (!(accumulatedWeight > 0.0) ||
        any(isnan(accumulatedIrradiance)) || any(isinf(accumulatedIrradiance)))
    {
        return false;
    }

    vec3 interpolated = accumulatedIrradiance / accumulatedWeight;
    irradiance = interpolated * interpolated;
    return true;
}

void main()
{
    // 環境光を求めずに返す表示では、SSRへ渡す出力は0のまま
    outIndirectSpecular = vec4(0.0);
    outSpecularReflectance = vec4(0.0);

    // GBufferからデータを取得
    vec4 albedoSample = texture(gbufferAlbedo, fragUV);
    vec4 normalSample = texture(gbufferNormal, fragUV);
    vec4 materialSample = texture(gbufferMaterial, fragUV);
    float depthSample = texture(gbufferDepth, fragUV).r;

    bool bRayTracingShadowValidationMode =
        params.debugViewMode == DEBUG_VIEW_MODE_R5_RAY_TRACING_HARD_SHADOW ||
        params.debugViewMode == DEBUG_VIEW_MODE_R5_RAY_TRACING_VISIBILITY;
    if (bRayTracingShadowValidationMode && params.shadowPadding0 == 0u)
    {
        outColor = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_R5_RAY_TRACING_VISIBILITY)
    {
        float visibility = texture(rayTracingShadowVisibility, fragUV).r;
        outColor = vec4(vec3(visibility), 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_RAW250)
    {
        float band = min(floor(clamp(fragUV.y, 0.0, 0.999999) * 5.0), 4.0);
        float localV = fragUV.y * 5.0 - band;
        float rawRoughness = band == 0.0 ? 0.0 :
                              band == 1.0 ? 0.05 :
                              band == 2.0 ? 0.25 :
                              band == 3.0 ? 0.50 : 1.0;
        float phi = 2.0 * PI * (fragUV.x - 0.5);
        float theta = PI * localV;
        vec3 rawDirection = vec3(sin(theta) * cos(phi),
                                 cos(theta),
                                 sin(theta) * sin(phi));
        vec3 rawColor = SamplePrefilteredSpecular(rawDirection, rawRoughness);
        float rawAlpha = albedoSample.a < 0.01 ? 1.0 : ComputeDebugDepth01(fragUV, depthSample);
        outColor = vec4(rawColor, rawAlpha);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_ALBEDO)
    {
        outColor = vec4(albedoSample.rgb, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_NORMAL)
    {
        if (albedoSample.a < 0.01)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        vec3 debugNormal = normalize(normalSample.xyz) * 0.5 + 0.5;
        outColor = vec4(debugNormal, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_MATERIAL)
    {
        outColor = vec4(materialSample.rgb, 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_DEPTH)
    {
        if (albedoSample.a < 0.01)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        float depth01 = ComputeDebugDepth01(fragUV, depthSample);
        outColor = vec4(vec3(depth01), 1.0);
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_POINT_SHADOW_DISTANCE)
    {
        // 面の位置から最初のキューブシャドウを持つ点光源への方向でキューブを引き、格納された
        // 距離（範囲で割った値）を灰色で出す。面が格納距離より光源から遠ければ（遮られていれば）
        // 赤を混ぜる。範囲外は暗い青、キューブシャドウを持つ灯が無ければ黒。
        if (albedoSample.a < 0.01)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        vec3 debugWorldPos = ReconstructWorldPosition(fragUV, depthSample);
        for (uint debugLightIndex = 0u; debugLightIndex < params.lightCount; ++debugLightIndex)
        {
            LightData debugLight = lightBuffer.lights[debugLightIndex];
            float cubeSlot = debugLight.attenuation.w;
            if (cubeSlot < 0.5)
            {
                continue;
            }
            vec3 toSurface = debugWorldPos - debugLight.position.xyz;
            float range = max(debugLight.attenuation.x, 0.0001);
            float surfaceDistance01 = length(toSurface) / range;
            if (surfaceDistance01 >= 1.0)
            {
                outColor = vec4(0.0, 0.0, 0.15, 1.0);
                return;
            }
            float storedDistance01 = textureLod(pointShadowCubes,
                                                vec4(toSurface, cubeSlot - 1.0),
                                                0.0).r;
            bool bOccluded = surfaceDistance01 > storedDistance01 + 0.02;
            vec3 gray = vec3(storedDistance01);
            outColor = vec4(bOccluded ? mix(gray, vec3(1.0, 0.0, 0.0), 0.6) : gray, 1.0);
            return;
        }
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // アルファが0の場合は天球（環境マップ）を描画
    if (albedoSample.a < 0.01)
    {
        if (params.debugViewMode == DEBUG_VIEW_MODE_RAW251 ||
            params.debugViewMode == DEBUG_VIEW_MODE_R7_SUN_VISIBILITY)
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }
        if (params.debugViewMode == DEBUG_VIEW_MODE_RAW252 &&
            !IsRaw252ParameterInvariantValid())
        {
            outColor = vec4(vec3(65504.0), 1.0);
            return;
        }
        if (params.bIBLEnabled != 0u)
        {
            // スクリーンUVからワールド方向を復元（far planeのdepth=1.0を使用）
            vec4 clipPos = vec4(fragUV * 2.0 - 1.0, 1.0, 1.0);
            vec4 worldPos4 = params.invViewProjection * clipPos;
            vec3 worldPos = worldPos4.xyz / worldPos4.w;
            vec3 rayDir = normalize(worldPos - params.cameraPosition.xyz);

            // equirectangular環境マップをサンプリング（LOD 0 = 最高解像度）
            vec2 envUV = EquirectangularUV(rayDir);
            // 空のradiance LUTは視線の透過率と地平線より下の地面を含むので、そのまま使う。
            vec4 skySample = textureLod(envMap, envUV, 0.0);
            // 静的HDRの背景にはシーンの倍率を掛ける（空が有効なら1）。
            vec3 skyColor = LimitStaticEnvironmentRadiance(skySample.rgb) * params.staticEnvironmentScale;
            vec4 sunDiskSample = textureLod(skySunDisk, vec2(0.5), 0.0);
            vec3 sunDirection = normalize(params.skySunDirectionAndCosRadius.xyz);
            float sunDiskMask = step(params.skySunDirectionAndCosRadius.w,
                                     dot(rayDir, sunDirection));
            vec3 preExposedSkyColor = ApplySceneColorPreExposure(skyColor);
            preExposedSkyColor += sunDiskSample.rgb * sunDiskMask;
            preExposedSkyColor = max(preExposedSkyColor, vec3(0.0));

            outColor = vec4(preExposedSkyColor, 1.0);
        }
        else
        {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||
        params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||
        params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS ||
        params.debugViewMode == DEBUG_VIEW_MODE_LOD_LEVEL)
    {
        outColor = vec4(albedoSample.rgb, 1.0);
        return;
    }

    bool bValidationHardShadow = IsR5HardShadowValidationMode();
    bool bValidationLambert = params.debugViewMode == DEBUG_VIEW_MODE_VALIDATION_LAMBERT ||
                              bValidationHardShadow;
    bool bValidationPBR = params.debugViewMode == DEBUG_VIEW_MODE_VALIDATION_PBR;

    // データ展開
    vec3 albedo = albedoSample.rgb;
    vec3 N = normalize(normalSample.xyz);
    float metallic = materialSample.r;
    float roughness = materialSample.g;
    float ao = materialSample.b;
    // 拡散の環境光に掛ける遮蔽（多重反射の近似で色ごとに弱める）
    vec3 diffuseAO = vec3(ao);

    // 画面空間AO（GTAO）適用: マテリアルAOと掛け合わせ、拡散には多重反射の近似を掛ける
    if (params.bSSAOEnabled != 0u)
    {
        float ssao = texture(ssaoTexture, fragUV).r;
        if (params.debugViewMode == DEBUG_VIEW_MODE_AMBIENT_OCCLUSION)
        {
            outColor = vec4(vec3(ssao), 1.0);
            return;
        }
        ao *= ssao;
        diffuseAO = GTAOMultiBounce(ao, albedo);
    }
    else if (params.debugViewMode == DEBUG_VIEW_MODE_AMBIENT_OCCLUSION)
    {
        outColor = vec4(vec3(1.0), 1.0);
        return;
    }

    // ワールド座標を復元
    vec3 worldPos = ReconstructWorldPosition(fragUV, depthSample);

    // カメラからの視線ベクトル
    vec3 V = normalize(params.cameraPosition.xyz - worldPos);

    float validationNdotV = max(dot(N, V), 0.0);
    vec2 dfgForValidation = vec2(0.0);

    if (params.debugViewMode == DEBUG_VIEW_MODE_RAW251)
    {
        vec2 dfgCoordinate = clamp(vec2(materialSample.r, materialSample.g),
                                    vec2(0.5 / 256.0), vec2(255.5 / 256.0));
        dfgForValidation = texture(brdfLUT, dfgCoordinate).rg;
        outColor = vec4(vec3(dfgForValidation.x, dfgForValidation.y,
                             dfgForValidation.x + dfgForValidation.y),
                        ComputeDebugDepth01(fragUV, depthSample));
        return;
    }

    bool bValidationRaw252 = params.debugViewMode == DEBUG_VIEW_MODE_RAW252;
    vec3 emissive = vec3(0.0);
    if (bValidationRaw252)
    {
        emissive = texture(gbufferEmissive, fragUV).rgb;
    }
    if (bValidationRaw252 &&
        (!IsRaw252ParameterInvariantValid() ||
         abs(ao - 1.0) > 0.0001 ||
         any(greaterThan(abs(emissive), vec3(0.0001)))))
    {
        outColor = vec4(vec3(65504.0), ComputeDebugDepth01(fragUV, depthSample));
        return;
    }

    // ライティング計算（ディフューズとスペキュラを分離してAOを個別適用）
    vec3 Lo_diffuse = vec3(0.0);
    vec3 Lo_specular = vec3(0.0);
    float sunVisibility = 0.0;

    for (uint i = 0u; i < params.lightCount; i++)
    {
        LightData light = lightBuffer.lights[i];
        float lightType = light.position.w;
        vec3 lightColor = light.chromaticityAndIntensity.rgb * light.chromaticityAndIntensity.w;

        vec3 L;
        float attenuation = 1.0;

        if (lightType < 0.5)
        {
            // Directional Light
            L = normalize(-light.direction.xyz);
        }
        else if (lightType < 1.5)
        {
            // Point Light
            vec3 toLight = light.position.xyz - worldPos;
            float distance = length(toLight);
            L = normalize(toLight);
            attenuation = CalculateInverseSquareAttenuation(distance) * CalculateRangeWindow(distance, light.attenuation.x);
        }
        else
        {
            // Spot Light
            vec3 toLight = light.position.xyz - worldPos;
            float distance = length(toLight);
            L = normalize(toLight);
            attenuation = CalculateInverseSquareAttenuation(distance) * CalculateRangeWindow(distance, light.attenuation.x);

            // スポットライトのコーン減衰
            float theta = dot(L, normalize(-light.direction.xyz));
            float innerAngle = light.direction.w;
            float outerAngle = light.attenuation.y;
            float epsilon = innerAngle - outerAngle;
            float spotFactor = clamp((theta - outerAngle) / max(epsilon, 0.001), 0.0, 1.0);
            attenuation *= spotFactor;
        }

        // PBR BRDF計算
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);

        // シャドウ計算（CSMとRT影を選んだ方向光だけ。attenuation.z=1がその灯）
        float shadow = 1.0;
        if ((!bValidationLambert || bValidationHardShadow) &&
            lightType < 0.5 && light.attenuation.z > 0.5 && params.bShadowEnabled != 0u)
        {
            shadow = params.shadowPadding0 != 0u
                         ? texture(rayTracingShadowVisibility, fragUV).r
                         : CalculateSunShadow(worldPos, N);
            // RT影は接地部も正しく遮るので、接触影はCSMの結果にだけ掛ける
            if (params.shadowPadding0 == 0u && shadow > 0.0 && NdotL > 0.0)
            {
                shadow *= CalculateContactShadow(worldPos, depthSample, N, L, 1.0e30);
            }
        }
        // 点光源のキューブシャドウ（attenuation.w=キューブの番号+1。0の灯は影を掛けない）
        else if ((!bValidationLambert || bValidationHardShadow) &&
                 lightType > 0.5 && lightType < 1.5 && light.attenuation.w > 0.5 &&
                 NdotL > 0.0)
        {
            // --point-shadow-method=vsm で、この灯の面・段のスライスが並んでいるときは VSM、それ以外はキューブ
            const uint pointShadowIndex = uint(light.attenuation.w - 1.0);
            if (pointShadowIndex < vsmPointBlock.point.header.x)
            {
                float pointTexelMeters = 0.0;
                shadow = VsmSamplePointShadow(pointShadowIndex, worldPos, N, pointTexelMeters);
            }
            else
            {
                shadow = SamplePointShadow(pointShadowCubes,
                                           light.attenuation.w - 1.0,
                                           light.position.xyz,
                                           light.attenuation.x,
                                           worldPos,
                                           N);
            }
            if (shadow > 0.0 && attenuation > 0.0)
            {
                // 光源の手前で止める（光源の球そのものを遮りとみなさない）
                float distanceToLight = length(light.position.xyz - worldPos);
                shadow *= CalculateContactShadow(worldPos, depthSample, N, L,
                                                 max(distanceToLight * 0.5, 0.0));
            }
        }

        vec3 radiance = lightColor * NdotL * attenuation * shadow;
        if (lightType < 0.5 && light.attenuation.z > 0.5)
        {
            sunVisibility = NdotL > 0.0 ? shadow : 0.0;
        }

        if (bValidationLambert)
        {
            // Validation 253: pure direct Lambert. No shadow, ambient, emissive or specular term.
            Lo_diffuse += EvaluateLambertDiffuseBRDF(albedo) * radiance;
        }
        else if (bValidationPBR || params.bNeuralBRDFEnabled == 0u)
        {
            vec2 dfgCoordinate = clamp(vec2(validationNdotV, roughness),
                                       vec2(0.5 / 256.0), vec2(255.5 / 256.0));
            dfgForValidation = texture(brdfLUT, dfgCoordinate).rg;
            vec3 diffuseBrdf;
            vec3 specularBrdf;
            EvaluateAnalyticalDirectEndpointBRDF(
                albedo, metallic, roughness, N, V, L, H, dfgForValidation,
                diffuseBrdf, specularBrdf);
            Lo_diffuse += diffuseBrdf * radiance;
            Lo_specular += specularBrdf * radiance;
        }
        else
        {
            // Neural Disney BRDFによる評価
            // 出力: x=diffuse_scale, y=specular_GD, z=fresnel, w=clearcoat
            float NdotV_val = max(dot(N, V), 0.0);
            float NdotH_val = max(dot(N, H), 0.0);
            float LdotH_val = max(dot(L, H), 0.0);

            vec4 nn = EvaluateNeuralBRDF(NdotL, NdotV_val, NdotH_val, LdotH_val, roughness);

            // Disney BRDF再構成（RTXNS SimpleInferencing準拠）
            vec3 Cspec0 = mix(vec3(0.04), albedo, metallic);
            vec3 diffuseContrib = nn.x * albedo * (1.0 - metallic);
            vec3 specularContrib = nn.y * mix(Cspec0, vec3(1.0), nn.z) + vec3(nn.w);

            Lo_diffuse += diffuseContrib * radiance;
            Lo_specular += specularContrib * radiance;
        }
    }

    if (params.debugViewMode == DEBUG_VIEW_MODE_R7_SUN_VISIBILITY)
    {
        outColor = vec4(vec3(sunVisibility), 1.0);
        return;
    }

    vec3 ambient = vec3(0.0);
    float specularAO = 1.0;
    float directSpecularAO = 1.0;
    // ambient に含めた環境光の鏡面反射（露出前）と、その反射率（SSRへ渡す）
    vec3 indirectSpecular = vec3(0.0);
    vec3 indirectSpecularReflectance = vec3(0.0);

    if (!bValidationLambert && !bValidationPBR)
    {
        // ========================================
        // アンビエント / IBL計算
        // ========================================
        float NdotV = max(dot(N, V), 0.0);
        bool bRaw252PrivateTarget = bValidationRaw252 &&
                                     metallic >= 0.9999 &&
                                     all(greaterThan(albedo, vec3(0.25))) &&
                                     all(lessThan(albedo, vec3(0.75)));
        vec3 iblAlbedo = bRaw252PrivateTarget ? vec3(0.5) : albedo;
        float iblNdotV = bRaw252PrivateTarget ? 0.25 : NdotV;
        float iblRoughness = bRaw252PrivateTarget ? (64.0 / 255.0) : roughness;
        vec3 F0d = vec3(0.04);
        vec3 F0c = iblAlbedo;
        vec3 F_ambient = FresnelSchlick(NdotV, (1.0 - metallic) * F0d + metallic * F0c);
        vec3 ddgiIrradiance = vec3(0.0);
        bool bDDGIValidationMode = params.debugViewMode >= 246u &&
                                   params.debugViewMode <= 255u;
        bool bDDGIAvailable = !bDDGIValidationMode &&
            TrySampleDDGIIrradiance(worldPos, N, ddgiIrradiance);
        bool bRTGIAvailable = !bDDGIValidationMode && params.ddgiInfo.y != 0u;
        vec3 rtgiDiffuseRadiance = vec3(0.0);
        if (bRTGIAvailable)
        {
            rtgiDiffuseRadiance = texture(rtgiDiffuseIndirect, fragUV).rgb /
                                  max(params.preExposure, 1.0e-6);
            if (any(isnan(rtgiDiffuseRadiance)) || any(isinf(rtgiDiffuseRadiance)))
            {
                bRTGIAvailable = false;
                rtgiDiffuseRadiance = vec3(0.0);
            }
            else
            {
                rtgiDiffuseRadiance = min(max(rtgiDiffuseRadiance, vec3(0.0)),
                                          vec3(65504.0));
            }
        }

        // スペキュラAO（Lagarde 2014: 視線角度とラフネスに基づく遮蔽近似）
        specularAO = ComputeSpecularAO(NdotV, ao, roughness);
        directSpecularAO = ComputeSpecularAO(NdotV, materialSample.b, roughness);

        if (params.bIBLEnabled != 0u)
        {
            // ========================================
            // IBL (Image-Based Lighting)
            // ========================================
            float iblIntensity = params.ambientColor.w; // IBL有効時はambientColor.wがIBL強度
            vec2 dfgCoordinate = clamp(vec2(iblNdotV, iblRoughness),
                                       vec2(0.5 / 256.0), vec2(255.5 / 256.0));
            vec2 brdf = texture(brdfLUT, dfgCoordinate).rg;
            // DDGIとRTGIは遮蔽を光線で解くため、画面空間AOを重ねず材質AOだけを掛ける。
            float ddgiAmbientAO = bDDGIAvailable || bRTGIAvailable ? materialSample.b : ao;
            vec3 iblDiffuseAO = bDDGIAvailable || bRTGIAvailable ? vec3(materialSample.b) : diffuseAO;
            ambient = bRTGIAvailable
                ? EvaluateRTGIEndpoint(iblAlbedo,
                                       metallic,
                                       iblRoughness,
                                       N,
                                       V,
                                       ddgiAmbientAO,
                                       specularAO,
                                       iblIntensity,
                                       brdf,
                                       rtgiDiffuseRadiance)
                : EvaluateIblEndpoint(iblAlbedo,
                                       metallic,
                                       iblRoughness,
                                       N,
                                       V,
                                       iblDiffuseAO,
                                       specularAO,
                                       iblIntensity,
                                       brdf,
                                       bDDGIAvailable,
                                       ddgiIrradiance);
            // EvaluateIblEndpoint・EvaluateRTGIEndpoint の鏡面の項と同じ値（どちらも遮蔽と強度を掛ける）
            indirectSpecularReflectance =
                EvaluateSpecularReflectance(iblAlbedo, metallic, brdf) * specularAO;
            indirectSpecular = SamplePrefilteredSpecular(reflect(-V, N), iblRoughness) *
                               indirectSpecularReflectance * iblIntensity;
        }
        else
        {
            // ========================================
            // フォールバック: フラットアンビエントライト
            // ========================================
            vec3 ambientLight = params.ambientColor.rgb * params.ambientColor.w;
            vec3 kD_ambient = (vec3(1.0) - F_ambient) * (1.0 - metallic);
            vec3 diffuseAmbient = kD_ambient * ambientLight * albedo;
            vec3 specularAmbient = F_ambient * ambientLight * (1.0 - roughness * 0.5);
            ambient = diffuseAmbient * diffuseAO + specularAmbient * specularAO;
            // 一様な環境光の鏡面の項（RTGIへ置き換えると、強度0で鏡面の項は無くなる）
            indirectSpecularReflectance = F_ambient * (1.0 - roughness * 0.5) * specularAO;
            indirectSpecular = bRTGIAvailable ? vec3(0.0) : specularAmbient * specularAO;
            if (bRTGIAvailable)
            {
                vec2 rtgiDfgCoordinate = clamp(vec2(NdotV, roughness),
                                                vec2(0.5 / 256.0),
                                                vec2(255.5 / 256.0));
                vec2 rtgiBrdf = texture(brdfLUT, rtgiDfgCoordinate).rg;
                ambient = EvaluateRTGIEndpoint(iblAlbedo,
                                               metallic,
                                               roughness,
                                               N,
                                               V,
                                               materialSample.b,
                                               specularAO,
                                               0.0,
                                               rtgiBrdf,
                                               rtgiDiffuseRadiance);
            }
            else if (bDDGIAvailable)
            {
                vec2 ddgiDfgCoordinate = clamp(vec2(NdotV, roughness),
                                               vec2(0.5 / 256.0),
                                               vec2(255.5 / 256.0));
                vec2 ddgiBrdf = texture(brdfLUT, ddgiDfgCoordinate).rg;
                ambient += EvaluateDiffuseEndpoint(ddgiIrradiance,
                                                   albedo,
                                                   metallic,
                                                   ddgiBrdf) * materialSample.b;
            }
        }
        if (!bValidationRaw252)
        {
            emissive = texture(gbufferEmissive, fragUV).rgb;
        }
        // 発光は露出を掛けた後で足す（下の outColor）。
    }

    // 直接光へのAO適用（マイクロシャドウ近似）:
    // 直接光はライト方向が明確なため、材質のAOだけを控えめに適用する（30%）。画面空間AOは
    // 物体の近くの大きな遮蔽を表し、直接光ではその遮蔽を影（CSM・RT影）が解くので掛けない
    // （掛けると日向の面まで暗くなる）。アンビエント/IBLへはフルAO適用（上記で適用済み）
    float directAO = mix(1.0, materialSample.b, 0.3);
    float directSpecAO = mix(1.0, directSpecularAO, 0.3);

    // 最終カラー（HDR）
    vec3 color;
    if (bValidationLambert)
    {
        color = Lo_diffuse;
    }
    else
    {
        color = ambient + Lo_diffuse * directAO + Lo_specular * directSpecAO;
    }

    float outputAlpha = bValidationRaw252 ? ComputeDebugDepth01(fragUV, depthSample) : 1.0;
    // GBufferの発光はプリエクスポージャ後の値なので、露出を掛けた後の色へ足す。
    // 発光を読まない表示（純Lambert・直接PBRの検証）では emissive は0のまま。
    outColor = vec4(ApplySceneColorPreExposure(color) + ResolveGBufferEmissiveSceneColor(emissive),
                    outputAlpha);
    outIndirectSpecular = vec4(ApplySceneColorPreExposure(indirectSpecular), 0.0);
    outSpecularReflectance = vec4(clamp(indirectSpecularReflectance, vec3(0.0), vec3(1.0)), 0.0);
}
