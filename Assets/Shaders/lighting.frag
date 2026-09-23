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
    uint shadowPadding1;
    uint shadowPadding2;
    vec4 skySunDirectionAndCosRadius; // xyz=太陽方向, w=cos(太陽ディスク角半径)
    vec4 cameraForward; // xyz=CSM分割に使うカメラ前方単位ベクトル
    vec4 ddgiVolumeOrigin; // xyz=DDGI volume原点
    vec4 ddgiProbeSpacing; // xyz=DDGI probe間隔
    uvec4 ddgiProbeCounts; // xyz=格子数, w=probe総数
    uvec4 ddgiInfo; // x=DDGI有効フラグ
} params;

// ライトデータ構造
struct LightData
{
    vec4 position;      // xyz=position, w=type (0:Dir, 1:Point, 2:Spot)
    vec4 direction;     // xyz=direction, w=innerAngle
    vec4 chromaticityAndIntensity; // xyz=Y=1 chromaticity, w=canonical lux/cd
    vec4 attenuation;   // x=range, y=outerAngle, z=unused, w=unused
};

// ライト配列
layout(std430, set = 0, binding = 5) readonly buffer LightBuffer
{
    LightData lights[];
} lightBuffer;

// 4層CSMシャドウマップ
layout(set = 0, binding = 6) uniform sampler2DArray shadowMap;

// GBufferエミッシブ
layout(set = 0, binding = 7) uniform sampler2D gbufferEmissive;

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

// SSAO (Screen-Space Ambient Occlusion)
layout(set = 0, binding = 10) uniform sampler2D ssaoTexture;

// Neural BRDF重みデータ（Disney BRDF MLP）
layout(set = 0, binding = 11) readonly buffer NeuralBRDFWeights
{
    float data[];
} neuralBRDF;

layout(location = 0) out vec4 outColor;

// ========================================
// PBR関連関数
// ========================================

#include "Common/PbrMaterialEvaluation.glsl"
// 角度重みが完全なゼロになる場合を避け、probe間の補間を安定させる最小床。
const float DDGI_WRAP_WEIGHT_FLOOR = 0.004;
const uint DEBUG_VIEW_MODE_NORMAL = 0u;
const uint DEBUG_VIEW_MODE_UNLIT = 1u;
const uint DEBUG_VIEW_MODE_WIREFRAME = 2u;
const uint DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS = 3u;
const uint DEBUG_VIEW_MODE_GBUFFER_ALBEDO = 4u;
const uint DEBUG_VIEW_MODE_GBUFFER_NORMAL = 5u;
const uint DEBUG_VIEW_MODE_GBUFFER_MATERIAL = 6u;
const uint DEBUG_VIEW_MODE_GBUFFER_DEPTH = 7u;
const uint DEBUG_VIEW_MODE_LOD_LEVEL = 8u;
const uint DEBUG_VIEW_MODE_COUNT = 9u;
const uint DEBUG_VIEW_MODE_VALIDATION_LAMBERT = 253u;
const uint DEBUG_VIEW_MODE_VALIDATION_PBR = 254u;
const uint DEBUG_VIEW_MODE_RAW250 = 250u;
const uint DEBUG_VIEW_MODE_RAW251 = 251u;
const uint DEBUG_VIEW_MODE_RAW252 = 252u;
const uint DEBUG_VIEW_MODE_R5_RASTER_HARD_SHADOW = 246u;
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

vec3 SamplePrefilteredSpecular(vec3 direction, float roughness)
{
    float lod = roughness * float(params.prefilteredSpecularMipLevels - 1u);
    vec2 uv = EquirectangularUV(direction);
    return textureLod(prefilteredSpecular, uv, lod).rgb;
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

// ========================================
// PCSS (Percentage-Closer Soft Shadows)
// ブロッカーサーチ + 可変カーネルPCF
// ========================================

// Poisson disk サンプル（16点）
const vec2 POISSON_DISK[16] = vec2[16](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

// ライトサイズ（ソフトネス制御）
const float PCSS_LIGHT_SIZE = 0.04;
const float PCSS_BLOCKER_SEARCH_RADIUS = 0.02;

float GetShadowSplitDistance(uint splitIndex)
{
    return splitIndex < 4u
               ? params.shadowSplitDistances[0][splitIndex]
               : params.shadowSplitDistances[1][splitIndex - 4u];
}

bool IsFiniteShadowValue(float value)
{
    return !isnan(value) && !isinf(value);
}

bool HasValidCascadedShadowData()
{
    if (params.bShadowEnabled == 0u ||
        params.cascadeCount != 4u)
    {
        return false;
    }

    float previousSplit = GetShadowSplitDistance(0u);
    if (!IsFiniteShadowValue(previousSplit))
    {
        return false;
    }
    for (uint splitIndex = 1u; splitIndex < 5u; ++splitIndex)
    {
        float split = GetShadowSplitDistance(splitIndex);
        if (!IsFiniteShadowValue(split) || split <= previousSplit)
        {
            return false;
        }
        previousSplit = split;
    }
    return true;
}

// Phase 1: ブロッカーサーチ（平均ブロッカー深度を求める）
float FindBlockerDepth(vec2 shadowUV,
                       float receiverDepth,
                       vec2 texelSize,
                       uint cascadeIndex)
{
    float blockerSum = 0.0;
    int blockerCount = 0;
    float searchRadius = PCSS_BLOCKER_SEARCH_RADIUS;

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * searchRadius;
        float sampleDepth = texture(shadowMap,
                                    vec3(shadowUV + offset, float(cascadeIndex))).r;
        if (sampleDepth < receiverDepth - 0.005)
        {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
    {
        return -1.0; // ブロッカーなし
    }

    return blockerSum / float(blockerCount);
}

// Phase 2: ペナンブラサイズ推定
float EstimatePenumbraSize(float receiverDepth, float blockerDepth)
{
    return PCSS_LIGHT_SIZE * (receiverDepth - blockerDepth) / blockerDepth;
}

// Phase 3: 可変カーネルPCF
float PCSSFilter(vec2 shadowUV,
                 float receiverDepth,
                 float filterRadius,
                 uint cascadeIndex)
{
    float shadow = 0.0;
    float bias = 0.005;

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * filterRadius;
        float sampleDepth = texture(shadowMap,
                                    vec3(shadowUV + offset, float(cascadeIndex))).r;
        shadow += (receiverDepth - bias > sampleDepth) ? 0.0 : 1.0;
    }

    return shadow / 16.0;
}

float SampleShadowCascade(vec3 worldPos, uint cascadeIndex)
{
    // ワールド座標をライトクリップ空間に変換
    vec4 lightSpacePos = params.lightProjection[cascadeIndex] *
                         params.lightView[cascadeIndex] * vec4(worldPos, 1.0);
    if (!IsFiniteShadowValue(lightSpacePos.w) || abs(lightSpacePos.w) < 0.000001)
    {
        return 1.0;
    }
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // クリップ空間[-1,1] → UV座標[0,1]に変換
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    // シャドウマップ範囲外は影なし
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
    {
        return 1.0;
    }

    // 深度範囲外も影なし
    if (currentDepth < 0.0 || currentDepth > 1.0)
    {
        return 1.0;
    }

    if (IsR5HardShadowValidationMode())
    {
        float sampleDepth = texture(shadowMap,
                                    vec3(shadowUV, float(cascadeIndex))).r;
        return currentDepth - 0.005 > sampleDepth ? 0.0 : 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);

    // Phase 1: ブロッカーサーチ
    float avgBlockerDepth = FindBlockerDepth(shadowUV,
                                             currentDepth,
                                             texelSize,
                                             cascadeIndex);

    // ブロッカーなし → 完全にライトが当たっている
    if (avgBlockerDepth < 0.0)
    {
        return 1.0;
    }

    // Phase 2: ペナンブラサイズ推定
    float penumbraSize = EstimatePenumbraSize(currentDepth, avgBlockerDepth);

    // フィルタ半径をクランプ（最小=1texel, 最大=制限）
    float filterRadius = clamp(penumbraSize, texelSize.x, 0.05);

    // Phase 3: 可変カーネルPCF
    return PCSSFilter(shadowUV, currentDepth, filterRadius, cascadeIndex);
}

float CalculateShadow(vec3 worldPos)
{
    if (!HasValidCascadedShadowData())
    {
        return 1.0;
    }

    vec3 viewForward = params.cameraForward.xyz;
    float forwardLength = length(viewForward);
    if (!IsFiniteShadowValue(forwardLength) || forwardLength <= 0.00001)
    {
        return 1.0;
    }
    viewForward /= forwardLength;
    float receiverDistance = dot(worldPos - params.cameraPosition.xyz, viewForward);
    float nearDistance = GetShadowSplitDistance(0u);
    float farDistance = GetShadowSplitDistance(4u);
    if (!IsFiniteShadowValue(receiverDistance) ||
        receiverDistance < nearDistance || receiverDistance > farDistance)
    {
        return 1.0;
    }

    uint cascadeIndex = 3u;
    for (uint candidate = 0u; candidate < 3u; ++candidate)
    {
        if (receiverDistance < GetShadowSplitDistance(candidate + 1u))
        {
            cascadeIndex = candidate;
            break;
        }
    }

    float shadow = SampleShadowCascade(worldPos, cascadeIndex);
    if (cascadeIndex < 3u)
    {
        float boundary = GetShadowSplitDistance(cascadeIndex + 1u);
        float previousBoundary = GetShadowSplitDistance(cascadeIndex);
        float blendWidth = max((boundary - previousBoundary) * 0.1, 0.001);
        float blendStart = boundary - blendWidth;
        if (receiverDistance > blendStart)
        {
            float nextShadow = SampleShadowCascade(worldPos, cascadeIndex + 1u);
            float blend = smoothstep(blendStart, boundary, receiverDistance);
            shadow = mix(shadow, nextShadow, blend);
        }
    }
    return shadow;
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

vec3 EvaluateIblEndpoint(vec3 albedo,
                         float metallic,
                         float roughness,
                         vec3 N,
                         vec3 V,
                         float ao,
                         float specularAO,
                         float iblIntensity,
                         vec2 brdf,
                         bool bUseDDGI,
                         vec3 ddgiIrradiance)
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

    vec3 irradiance = bUseDDGI
        ? ddgiIrradiance
        : textureLod(diffuseIrradiance, EquirectangularUV(N), 0.0).rgb;
    vec3 diffuseIBL = EvaluateDiffuseEndpoint(irradiance, albedo, metallic, brdf);

    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = SamplePrefilteredSpecular(R, roughness);
    vec3 specularIBL = prefilteredColor *
                       ((1.0 - metallic) * Ed + metallic * Ec);
    if (bUseDDGI)
    {
        return diffuseIBL * ao + specularIBL * specularAO * iblIntensity;
    }
    return (diffuseIBL * ao + specularIBL * specularAO) * iblIntensity;
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
        float Ess = max(brdf.x + brdf.y, 0.0001);
        vec3 F0d = vec3(0.04);
        vec3 F0c = albedo;
        vec3 CompD = vec3(1.0) + F0d * (1.0 - Ess) / Ess;
        vec3 CompC = vec3(1.0) + F0c * (1.0 - Ess) / Ess;
        vec3 Ed = clamp((F0d * brdf.x + brdf.y) * CompD,
                        vec3(0.0), vec3(1.0));
        vec3 Ec = clamp((F0c * brdf.x + brdf.y) * CompC,
                        vec3(0.0), vec3(1.0));
        vec3 R = reflect(-V, N);
        vec3 prefilteredColor = SamplePrefilteredSpecular(R, roughness);
        specular = prefilteredColor *
                   ((1.0 - metallic) * Ed + metallic * Ec) *
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

    float delta = pointDistance - meanDistance;
    float chebyshev = variance / (variance + delta * delta);
    chebyshev *= chebyshev * chebyshev;
    return max(0.05, chebyshev);
}

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

    ivec3 baseProbe = ivec3(floor(gridPosition));
    vec3 alpha = clamp(gridPosition - vec3(baseProbe), vec3(0.0), vec3(1.0));
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

        vec3 trilinear = mix(vec3(1.0) - alpha, alpha, vec3(offset));
        float weight = trilinear.x * trilinear.y * trilinear.z;
        if (weight <= 0.0)
        {
            continue;
        }

        vec3 probePosition = params.ddgiVolumeOrigin.xyz +
                             params.ddgiProbeSpacing.xyz * vec3(probeCoordinates);
        vec3 probeToPoint = worldPosition - probePosition;
        float pointDistance = length(probeToPoint);
        if (isnan(pointDistance) || isinf(pointDistance))
        {
            continue;
        }
        vec3 probeToPointDirection = pointDistance > 1.0e-6
            ? probeToPoint / pointDistance
            : normal;
        float wrapShading = (dot(-probeToPointDirection, normal) + 1.0) * 0.5;
        weight *= wrapShading * wrapShading + DDGI_WRAP_WEIGHT_FLOOR;
        weight *= SampleDDGIVisibility(probeIndex,
                                       probeToPointDirection,
                                       pointDistance);

        vec3 probeIrradiance = textureLod(ddgiIrradianceAtlas,
                                          vec3(irradianceUv, float(probeIndex)),
                                          0.0).rgb;
        if (any(isnan(probeIrradiance)) || any(isinf(probeIrradiance)))
        {
            return false;
        }
        accumulatedIrradiance += probeIrradiance * weight;
        accumulatedWeight += weight;
    }

    if (accumulatedWeight <= 1.0e-6 ||
        any(isnan(accumulatedIrradiance)) || any(isinf(accumulatedIrradiance)))
    {
        return false;
    }

    irradiance = max(accumulatedIrradiance / accumulatedWeight, vec3(0.0));
    return true;
}

void main()
{
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

    // アルファが0の場合は天球（環境マップ）を描画
    if (albedoSample.a < 0.01)
    {
        if (params.debugViewMode == DEBUG_VIEW_MODE_RAW251)
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
            vec4 skySample = textureLod(envMap, envUV, 0.0);
            vec3 skyColor = skySample.rgb;
            vec4 sunDiskSample = textureLod(skySunDisk, vec2(0.5), 0.0);
            if (sunDiskSample.a > 0.5)
            {
                vec3 transmittance = textureLod(skyTransmittance,
                                                vec2(clamp(rayDir.y, 0.0, 1.0), 0.0),
                                                0.0).rgb;
                skyColor *= clamp(transmittance, vec3(0.0), vec3(1.0));
            }
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

    // SSAO適用: マテリアルAOとSSAOを掛け合わせる
    if (params.bSSAOEnabled != 0u)
    {
        float ssao = texture(ssaoTexture, fragUV).r;
        ao *= ssao;
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

        // シャドウ計算（ディレクショナルライトのみ）
        float shadow = 1.0;
        if ((!bValidationLambert || bValidationHardShadow) &&
            lightType < 0.5 && params.bShadowEnabled != 0u)
        {
            shadow = params.shadowPadding0 != 0u
                         ? texture(rayTracingShadowVisibility, fragUV).r
                         : CalculateShadow(worldPos);
        }

        vec3 radiance = lightColor * NdotL * attenuation * shadow;

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

    vec3 ambient = vec3(0.0);
    float specularAO = 1.0;

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
                                       ddgiAmbientAO,
                                       specularAO,
                                       iblIntensity,
                                       brdf,
                                       bDDGIAvailable,
                                       ddgiIrradiance);
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
            ambient = diffuseAmbient * ao + specularAmbient * specularAO;
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
        ambient += emissive;
    }

    // 直接光へのAO適用（マイクロシャドウ近似）:
    // 直接光はライト方向が明確なため、AOは控えめに適用（30%）
    // アンビエント/IBLへはフルAO適用（上記で適用済み）
    float directAO = mix(1.0, ao, 0.3);
    float directSpecAO = mix(1.0, specularAO, 0.3);

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
    outColor = vec4(ApplySceneColorPreExposure(color), outputAlpha);
}
