// 高さフォグと空気遠近の評価（volumetrics.frag の合成と、撮影の大気の書き出し volumetrics_probe.frag が同じ式を使うための共通の include）。
// 記述子の束縛（0〜3）と定数（VolumetricsParams）もここで宣言する。値は VolumetricsPass が詰める。
// 束縛 4（空の透過率 LUT）は記述子の形に残っているが、空気遠近は高度ごとの表（定数）で積むので読まない。

#ifndef VOLUMETRICS_COMMON_GLSL
#define VOLUMETRICS_COMMON_GLSL
#include "Common/HeightFog.glsl"

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;
layout(set = 0, binding = 3) uniform sampler2DArray cascadedShadowMap;

#ifdef VOLUMETRICS_USE_VSM
// 大気の書き出しは CSM のまま。VSM の資源は画面へ霧を重ねるときだけ束縛する。
#include "Common/VirtualShadowMapParams.glsl"
layout(std140, set = 0, binding = 6) uniform VsmSampleBlock
{
    VsmSampleParams vsm;
} vsmBlock;
layout(std430, set = 0, binding = 7) readonly buffer VsmPageTableBuffer
{
    uint vsmPageTable[];
};
layout(std430, set = 0, binding = 8) readonly buffer VsmPoolBuffer
{
    uint vsmPool[];
};
#define VSM_SLICE_BINDING 9
#include "Common/VirtualShadowMapSlice.glsl"
#define VSM_PARAMS vsmBlock.vsm
#define VSM_SLICE(i) vsmSlices[i]
#define VSM_PAGE_TABLE(i) vsmPageTable[i]
#define VSM_POOL(i) vsmPool[i]
#include "Common/VirtualShadowMap.glsl"
#endif

// 空気遠近の高度ごとの表の段数と、霧・空気遠近・参照の視線の歩数
const uint kAerialProfileSize = 128u;
const int kAtmosphereSteps = 32;
const int kFogOnlySteps = 24;
const int kAtmosphereFarSteps = 8;
const int kReferenceSteps = 256;
const float kMinimumExponentialStepRatio = 1.0001;

layout(std140, set = 0, binding = 2) uniform VolumetricsParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndPreExposure;
    vec4 cameraForwardAndScatteringEnabled;
    vec4 fogParameters; // x=密度, y=基準の高さ, z=高さの減衰, w=空の放射輝度が使えれば 1
    vec4 fallbackFogColor;
    vec4 directionalLightDirectionAndAnisotropy;
    vec4 directionalLightRadianceAndEnabled;
    mat4 cascadeView[4];
    mat4 cascadeProjection[4];
    vec4 cascadeSplitDistances[2];
    vec4 aerialPerspective; // x=有効なら 1, y=距離の倍率, z=掛け始める距離(m), w=観測点の高度の透過率 LUT の v（記録だけ）
    vec4 rayleighExtinctionAndScaleHeight; // rgb=高度 0 の消散係数(1/m), w=スケールハイト(m)
    vec4 mieExtinctionAndScaleHeight; // x=高度 0 の消散係数(1/m), y=スケールハイト(m), z=ワールドの y = 0 の高度(m)
    vec4 sunDirectionAndMieAnisotropy; // xyz=太陽へ向かう向き, w=空の Mie の位相の異方性
    vec4 aerialScattering; // x=高度 0 の Mie の散乱係数(1/m), y=高度ごとの表のいちばん上の段の高度(m), z=惑星の半径(m)
    // 高度ごとの表: [i] = 太陽の照度 × 透過率(lux), [kAerialProfileSize + i] = 太陽の照度 × 多重散乱の表 Ψ_ms(cd/m²)
    vec4 aerialProfile[2u * kAerialProfileSize];
    vec4 fogRange; // x=霧の距離の上限(m、0 で視線全体), y=周りの光の倍率, zw=未使用
    vec4 fogTintAndSkyBlend;    // rgb=霧の色味, w=空への寄せ（0..1）
    vec4 fogReferenceElevation; // x=基準の仰角の正弦, y=余弦, zw=未使用
    vec4 fogSteps;  // x=歩の配置（0=等間隔、1=指数）, y=指数の比, z=影つきの散乱を歩で積む距離の上限(m、0で上限なし), w=参照の256歩なら1
    vec4 fogJitter; // x=画素ごとのずらしが有効なら1, y=TAAが掛かるフレームで時間方向のずらしが有効なら1, z=フレーム番号を64で割った余り, w=未使用
} params;

bool IsFiniteFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

struct FogStep
{
    float Start;
    float Length;
};

// 視線の区間を分け、指数配置では手前から隣の歩の長さを比率に従って伸ばす。
FogStep PlaceFogStep(int stepIndex, int stepCount, float span, bool bExponential, float ratio)
{
    FogStep step;
    if (!bExponential || !IsFiniteFloat(ratio) || ratio < kMinimumExponentialStepRatio || stepCount <= 1)
    {
        step.Length = span / float(stepCount);
        step.Start = float(stepIndex) * step.Length;
        return step;
    }
    float denominator = pow(ratio, float(stepCount)) - 1.0;
    float growth = pow(ratio, float(stepIndex));
    step.Start = span * (growth - 1.0) / denominator;
    step.Length = span * growth * (ratio - 1.0) / denominator;
    return step;
}

// 距離の上限より奥がある場合、手前は選んだ配置、奥は等間隔で分ける。
FogStep PlaceAtmosphereMarchStep(int stepIndex, int stepCount, float rayDistance,
                                bool bExponential, float ratio, float cutoff)
{
    if (cutoff > 0.0 && rayDistance > cutoff && stepCount > kAtmosphereFarSteps)
    {
        int nearCount = stepCount - kAtmosphereFarSteps;
        if (stepIndex < nearCount)
        {
            return PlaceFogStep(stepIndex, nearCount, cutoff, bExponential, ratio);
        }
        FogStep farStep = PlaceFogStep(stepIndex - nearCount, kAtmosphereFarSteps,
                                       rayDistance - cutoff, false, 1.0);
        farStep.Start += cutoff;
        return farStep;
    }
    return PlaceFogStep(stepIndex, stepCount, rayDistance, bExponential, ratio);
}

// 原点からの等間隔では、標本距離を従来と同じ演算順で求める。
float FogSampleDistance(FogStep step, int stepIndex, float sampleOffset, bool bUniformFromOrigin)
{
    if (bUniformFromOrigin)
    {
        return (float(stepIndex) + sampleOffset) * step.Length;
    }
    return step.Start + sampleOffset * step.Length;
}

// 整数の画素座標から画素ごとに固定されたノイズを求める（Jimenez 2014）。
float InterleavedGradientNoise(vec2 p)
{
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// 参照と大気の書き出しは中点を使い、必要に応じて画素と時間で標本をずらす。
float FogStepOffset(vec2 pixelPosition, bool bJitterFog, bool bLegacyPixelJitter)
{
    if (params.fogSteps.w > 0.5 || !bJitterFog)
    {
        return 0.5;
    }
    if (params.fogJitter.x > 0.5)
    {
        vec2 p = floor(pixelPosition);
        if (params.fogJitter.y > 0.5)
        {
            p += vec2(5.588238 * params.fogJitter.z);
        }
        return InterleavedGradientNoise(p);
    }
    if (bLegacyPixelJitter)
    {
        return InterleavedGradientNoise(floor(pixelPosition));
    }
    return 0.5;
}

vec2 EquirectangularUV(vec3 direction)
{
    return HeightFogEquirectUV(direction);
}

float ComputeAnalyticTransmittance(float originHeight,
                                   float rayDirectionY,
                                   float rayDistance)
{
    return HeightFogTransmittance(params.fogParameters.x, params.fogParameters.y,
                                  params.fogParameters.z, originHeight, rayDirectionY, rayDistance);
}

float GetCascadeSplitDistance(uint splitIndex)
{
    return splitIndex < 4u
               ? params.cascadeSplitDistances[0][splitIndex]
               : params.cascadeSplitDistances[1].x;
}

float SampleVolumetricShadowCascade(vec3 worldPosition, uint cascadeIndex)
{
    vec4 lightSpacePosition = params.cascadeProjection[cascadeIndex] *
                              params.cascadeView[cascadeIndex] *
                              vec4(worldPosition, 1.0);
    if (!IsFiniteFloat(lightSpacePosition.x) || !IsFiniteFloat(lightSpacePosition.y) ||
        !IsFiniteFloat(lightSpacePosition.z) || !IsFiniteFloat(lightSpacePosition.w) ||
        abs(lightSpacePosition.w) <= 1.0e-6)
    {
        return 0.0;
    }

    vec3 projectedPosition = lightSpacePosition.xyz / lightSpacePosition.w;
    vec2 shadowUV = projectedPosition.xy * 0.5 + 0.5;
    float receiverDepth = projectedPosition.z;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        receiverDepth < 0.0 || receiverDepth > 1.0)
    {
        return 1.0;
    }

    float storedDepth = textureLod(cascadedShadowMap,
                                   vec3(shadowUV, float(cascadeIndex)),
                                   0.0).r;
    if (!IsFiniteFloat(storedDepth))
    {
        return 0.0;
    }
    return receiverDepth - 0.005 > storedDepth ? 0.0 : 1.0;
}

float CalculateVolumetricShadowVisibility(vec3 worldPosition)
{
    if (params.cameraForwardAndScatteringEnabled.w <= 0.5 ||
        params.directionalLightRadianceAndEnabled.w <= 0.5)
    {
        return 0.0;
    }

    float receiverDistance = dot(worldPosition - params.cameraPositionAndPreExposure.xyz,
                                 params.cameraForwardAndScatteringEnabled.xyz);
    float nearDistance = GetCascadeSplitDistance(0u);
    float farDistance = GetCascadeSplitDistance(4u);
    if (!IsFiniteFloat(receiverDistance) || !IsFiniteFloat(nearDistance) ||
        !IsFiniteFloat(farDistance) || farDistance <= nearDistance)
    {
        return 0.0;
    }
    if (receiverDistance < nearDistance || receiverDistance > farDistance)
    {
        return 1.0;
    }

#ifdef VOLUMETRICS_USE_VSM
    float vsmVisibility;
    if (VsmSampleSunVisibilityPoint(worldPosition, VSMS_CONSTANT_BIAS_TEXELS, vsmVisibility))
    {
        // CSM と同じく、最後のカスケードの奥の 10% で影を薄める。
        float fadeWidth = max((farDistance - GetCascadeSplitDistance(3u)) * 0.1, 0.001);
        return mix(vsmVisibility, 1.0,
                   smoothstep(farDistance - fadeWidth, farDistance, receiverDistance));
    }
#endif

    uint cascadeIndex = 3u;
    for (uint candidate = 0u; candidate < 3u; ++candidate)
    {
        if (receiverDistance < GetCascadeSplitDistance(candidate + 1u))
        {
            cascadeIndex = candidate;
            break;
        }
    }

    float visibility = SampleVolumetricShadowCascade(worldPosition, cascadeIndex);
    if (cascadeIndex < 3u)
    {
        float boundary = GetCascadeSplitDistance(cascadeIndex + 1u);
        float previousBoundary = GetCascadeSplitDistance(cascadeIndex);
        if (!IsFiniteFloat(boundary) || !IsFiniteFloat(previousBoundary) ||
            boundary <= previousBoundary)
        {
            return 0.0;
        }
        float blendWidth = max((boundary - previousBoundary) * 0.1, 0.001);
        float blendStart = boundary - blendWidth;
        if (receiverDistance > blendStart)
        {
            float nextVisibility = SampleVolumetricShadowCascade(worldPosition,
                                                                 cascadeIndex + 1u);
            float blend = smoothstep(blendStart, boundary, receiverDistance);
            visibility = mix(visibility, nextVisibility, blend);
        }
    }
    else
    {
        // 影の最大距離の手前、最後のカスケードの奥の10%で影を薄め、境界で急に消えないようにする。
        float fadeWidth = max((farDistance - GetCascadeSplitDistance(3u)) * 0.1, 0.001);
        visibility = mix(visibility, 1.0,
                         smoothstep(farDistance - fadeWidth, farDistance, receiverDistance));
    }
    return visibility;
}

vec3 IntegrateDirectionalSingleScattering(vec3 rayDirection, float rayDistance, float sampleOffset)
{
    if (params.cameraForwardAndScatteringEnabled.w <= 0.5 ||
        params.directionalLightRadianceAndEnabled.w <= 0.5)
    {
        return vec3(0.0);
    }

    const float anisotropy = clamp(params.directionalLightDirectionAndAnisotropy.w,
                                   -0.95,
                                   0.95);
    const float cosineTheta = clamp(dot(params.directionalLightDirectionAndAnisotropy.xyz,
                                         -rayDirection),
                                    -1.0,
                                    1.0);
    const float phaseDenominator = max(1.0 + anisotropy * anisotropy -
                                           2.0 * anisotropy * cosineTheta,
                                       1.0e-4);
    const float phase = (1.0 - anisotropy * anisotropy) /
                        (12.5663706 * pow(phaseDenominator, 1.5));
    bool bReference = params.fogSteps.w > 0.5;
    int stepCount = bReference ? kReferenceSteps : kFogOnlySteps;
    float span = !bReference && params.fogSteps.z > 0.0 ? min(rayDistance, params.fogSteps.z) : rayDistance;
    bool bExponential = !bReference && params.fogSteps.x > 0.5;
    float ratio = bReference ? 1.0 : params.fogSteps.y;
    bool bUniformFromOrigin = !bExponential || !IsFiniteFloat(ratio) ||
                              ratio < kMinimumExponentialStepRatio || stepCount <= 1;
    vec3 scatteringRadiance = vec3(0.0);

    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex)
    {
        FogStep step = PlaceFogStep(stepIndex, stepCount, span, bExponential, ratio);
        float sampleDistance = FogSampleDistance(step, stepIndex, sampleOffset, bUniformFromOrigin);
        vec3 samplePosition = params.cameraPositionAndPreExposure.xyz +
                              rayDirection * sampleDistance;
        float densityExponent = -params.fogParameters.z *
                                (samplePosition.y - params.fogParameters.y);
        float localDensity = params.fogParameters.x *
                             exp(clamp(densityExponent, -80.0, 80.0));
        float viewTransmittance = ComputeAnalyticTransmittance(
            params.cameraPositionAndPreExposure.y,
            rayDirection.y,
            sampleDistance);
        float shadowVisibility = CalculateVolumetricShadowVisibility(samplePosition);
        if (IsFiniteFloat(localDensity) && IsFiniteFloat(viewTransmittance) &&
            IsFiniteFloat(shadowVisibility) && localDensity > 0.0 &&
            viewTransmittance > 0.0 && shadowVisibility > 0.0)
        {
            scatteringRadiance += params.directionalLightRadianceAndEnabled.rgb *
                                  (phase * localDensity * viewTransmittance *
                                   shadowVisibility * step.Length);
        }
    }
    if (span < rayDistance)
    {
        // 上限より奥は影を引かず、解析した透過率の差で散乱を足す。
        float nearTransmittance = ComputeAnalyticTransmittance(
            params.cameraPositionAndPreExposure.y, rayDirection.y, span);
        float farTransmittance = ComputeAnalyticTransmittance(
            params.cameraPositionAndPreExposure.y, rayDirection.y, rayDistance);
        if (IsFiniteFloat(nearTransmittance) && IsFiniteFloat(farTransmittance))
        {
            vec3 farRadiance = params.directionalLightRadianceAndEnabled.rgb *
                               (phase * max(nearTransmittance - farTransmittance, 0.0));
            if (IsFiniteFloat(farRadiance.x) && IsFiniteFloat(farRadiance.y) && IsFiniteFloat(farRadiance.z))
            {
                scatteringRadiance += farRadiance;
            }
        }
    }
    return scatteringRadiance;
}

// 空気遠近の高度ごとの表を、海面からの高度 altitude（m）で線形に引く（row 0 = 太陽の照度 × 透過率、row 1 = 太陽の照度 × Ψ_ms）
vec3 SampleAerialProfile(uint row, float altitude)
{
    float position = clamp(altitude / params.aerialScattering.y, 0.0, 1.0) * float(kAerialProfileSize - 1u);
    uint index = min(uint(position), kAerialProfileSize - 2u);
    float weight = position - float(index);
    uint base = row * kAerialProfileSize + index;
    return mix(params.aerialProfile[base].rgb, params.aerialProfile[base + 1u].rgb, weight);
}

float RayleighPhase(float cosine)
{
    return 0.0596831 * (1.0 + cosine * cosine);
}

float HenyeyGreensteinPhase(float cosine, float anisotropy)
{
    float denominator = max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine, 1.0e-4);
    return (1.0 - anisotropy * anisotropy) / (12.5663706 * pow(denominator, 1.5));
}

// 1 本の視線（カメラから rayDirection の向きに rayDistance m）の高さフォグと空気遠近（どれも事前露出の前の値）
struct VolumetricsRayResult
{
    // 高さフォグの透過率と不透明度（1 - 透過率）
    float FogTransmittance;
    float FogOpacity;
    // 高さフォグの周りの光の色。空の放射輝度 LUT では視線の向きと基準の仰角の値を空への寄せで混ぜ、
    // LUT が無いときは既定の色を使う。どちらも色味を掛け、負は 0
    vec3 FogRadiance;
    // 高さフォグの周りの光。空気遠近が有効なら、各点の空の多重散乱の表 Ψ_ms（高度ごとの表）× 霧の散乱係数 × 透過率を
    // 視線で積んだ値に色味を掛ける。無効なら空への寄せと色味を反映した FogRadiance × 不透明度。
    // 空への寄せは LUT の経路だけで混ぜ、どちらも周りの光の倍率を掛ける
    vec3 FogAmbient;
    // 高さフォグの太陽の単一散乱（影つき）。不透明度が 0 のときは数えない
    vec3 FogDirectional;
    // 空気遠近の RGB の透過率と散乱光（空気遠近が無効なら 1 と 0）。散乱光は単一散乱と多重散乱の和
    vec3 AerialTransmittance;
    vec3 AerialScattering;
    vec3 AerialSingle;
    vec3 AerialMultiple;
};

// 空気遠近が有効なとき: 霧と空の媒質（Rayleigh・Mie）を 1 本の視線で一緒に進め、各点の散乱光をその点までの両方の透過率で減らして
// 積む（霧を手前・空気遠近を奥と決めて重ねない）。空の媒質の高度は球の上で数え（カメラの位置にワールドの y = 0 の高度を足した点から）、
// 掛け始める距離より手前は数えない。各点の空の散乱の源は空の LUT と同じ大気の値で、太陽の単一散乱（Rayleigh と Mie の位相 ×
// 高度ごとの表の太陽の照度 × 透過率）と多重散乱（散乱係数 × 表の Ψ_ms）。霧は太陽の単一散乱（影つき）と周りの光を散乱する。
// 霧の周りの光は空の媒質と同じ多重散乱の表 Ψ_ms（等方の近似）で数える。視線の向きの空の輝度を使うと、その輝度が視線の奥の
// 太陽の散乱まで含むので、霧の点の周りの光を多く数える（正解 B の 1.3〜1.6 倍）。歩は等間隔または指数に配置し、上限より奥は等間隔に分ける。
// 標本は画素と時間でずらせる。参照の 256 歩と大気の書き出しは中点を使う。
void MarchFogAndAerial(vec3 rayDirection, float rayDistance, float fogDistance, float sampleOffset,
                       inout VolumetricsRayResult result)
{
    float startDistance = max(params.aerialPerspective.z, 0.0);
    float distanceScale = params.aerialPerspective.y;
    float fogDensity = max(params.fogParameters.x, 0.0);
    bool bFogDirectional = fogDensity > 0.0 &&
                           params.cameraForwardAndScatteringEnabled.w > 0.5 &&
                           params.directionalLightRadianceAndEnabled.w > 0.5;
    float planetRadius = params.aerialScattering.z;
    float originHeight = params.cameraPositionAndPreExposure.y + params.mieExtinctionAndScaleHeight.z;
    vec3 sunDirection = params.sunDirectionAndMieAnisotropy.xyz;
    float viewSunCosine = clamp(dot(rayDirection, sunDirection), -1.0, 1.0);
    float rayleighPhase = RayleighPhase(viewSunCosine);
    float miePhase = HenyeyGreensteinPhase(viewSunCosine, params.sunDirectionAndMieAnisotropy.w);
    float fogAnisotropy = clamp(params.directionalLightDirectionAndAnisotropy.w, -0.95, 0.95);
    float fogPhase = HenyeyGreensteinPhase(
        clamp(dot(params.directionalLightDirectionAndAnisotropy.xyz, -rayDirection), -1.0, 1.0), fogAnisotropy);

    bool bReference = params.fogSteps.w > 0.5;
    int stepCount = bReference ? kReferenceSteps : kAtmosphereSteps;
    bool bExponential = !bReference && params.fogSteps.x > 0.5;
    float ratio = bReference ? 1.0 : params.fogSteps.y;
    float cutoff = bReference ? 0.0 : params.fogSteps.z;
    bool bSplitAtCutoff = cutoff > 0.0 && rayDistance > cutoff && stepCount > kAtmosphereFarSteps;
    int nearCount = bSplitAtCutoff ? stepCount - kAtmosphereFarSteps : stepCount;
    bool bUniformNear = !bExponential || !IsFiniteFloat(ratio) ||
                        ratio < kMinimumExponentialStepRatio || nearCount <= 1;
    vec3 airDepth = vec3(0.0);
    float fogDepth = 0.0;
    vec3 single = vec3(0.0);
    vec3 multiple = vec3(0.0);
    vec3 fogAmbient = vec3(0.0);
    vec3 fogDirectional = vec3(0.0);
    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex)
    {
        FogStep step = PlaceAtmosphereMarchStep(stepIndex, stepCount, rayDistance, bExponential, ratio, cutoff);
        bool bUniformFromOrigin = bUniformNear && (!bSplitAtCutoff || stepIndex < nearCount);
        float sampleDistance = FogSampleDistance(step, stepIndex, sampleOffset, bUniformFromOrigin);
        float stepLength = step.Length;
        vec3 fromCenter = vec3(rayDirection.x * sampleDistance,
                               planetRadius + originHeight + rayDirection.y * sampleDistance,
                               rayDirection.z * sampleDistance);
        float altitude = length(fromCenter) - planetRadius;
        float airWeight = sampleDistance >= startDistance ? distanceScale : 0.0;
        float rayleighDensity = exp(clamp(-altitude / params.rayleighExtinctionAndScaleHeight.w, -80.0, 80.0));
        float mieDensity = exp(clamp(-altitude / params.mieExtinctionAndScaleHeight.y, -80.0, 80.0));
        vec3 rayleighScattering = params.rayleighExtinctionAndScaleHeight.rgb * (rayleighDensity * airWeight);
        float mieScattering = params.aerialScattering.x * mieDensity * airWeight;
        vec3 airExtinction = rayleighScattering + vec3(params.mieExtinctionAndScaleHeight.x * mieDensity * airWeight);
        float fogScattering = 0.0;
        if (fogDensity > 0.0 && sampleDistance <= fogDistance)
        {
            float worldY = params.cameraPositionAndPreExposure.y + rayDirection.y * sampleDistance;
            fogScattering = fogDensity * exp(clamp(-params.fogParameters.z * (worldY - params.fogParameters.y),
                                                   -80.0, 80.0));
        }

        vec3 stepAirDepth = airExtinction * stepLength;
        float stepFogDepth = fogScattering * stepLength;
        // その歩の標本までの透過率（空の媒質と霧の両方）
        vec3 transmittance = exp(-(airDepth + sampleOffset * stepAirDepth) -
                                 vec3(fogDepth + sampleOffset * stepFogDepth));
        vec3 sunIlluminance = SampleAerialProfile(0u, altitude);
        vec3 multipleScattering = SampleAerialProfile(1u, altitude);
        single += transmittance * (rayleighScattering * rayleighPhase + vec3(mieScattering * miePhase)) *
                  sunIlluminance * stepLength;
        multiple += transmittance * (rayleighScattering + vec3(mieScattering)) * multipleScattering * stepLength;
        if (fogScattering > 0.0)
        {
            fogAmbient += transmittance * multipleScattering * (fogScattering * stepLength);
            if (bFogDirectional)
            {
                vec3 samplePosition = params.cameraPositionAndPreExposure.xyz + rayDirection * sampleDistance;
                float shadowVisibility = CalculateVolumetricShadowVisibility(samplePosition);
                if (IsFiniteFloat(shadowVisibility) && shadowVisibility > 0.0)
                {
                    fogDirectional += transmittance * params.directionalLightRadianceAndEnabled.rgb *
                                      (fogScattering * fogPhase * shadowVisibility * stepLength);
                }
            }
        }
        airDepth += stepAirDepth;
        fogDepth += stepFogDepth;
    }

    result.AerialTransmittance = exp(-clamp(airDepth, vec3(0.0), vec3(80.0)));
    result.AerialSingle = max(single, vec3(0.0));
    result.AerialMultiple = max(multiple, vec3(0.0));
    result.AerialScattering = result.AerialSingle + result.AerialMultiple;
    if (result.FogOpacity > 0.0)
    {
        result.FogAmbient = fogAmbient * (params.fogRange.y * params.fogTintAndSkyBlend.rgb);
        result.FogDirectional = fogDirectional;
    }
}

VolumetricsRayResult EvaluateVolumetricsRay(vec3 rayDirection, float rayDistance,
                                            vec2 pixelPosition, bool bJitterFog)
{
    float fogDistance = params.fogRange.x > 0.0 ? min(rayDistance, params.fogRange.x) : rayDistance;
    VolumetricsRayResult result;
    result.FogTransmittance = ComputeAnalyticTransmittance(
        params.cameraPositionAndPreExposure.y,
        rayDirection.y,
        fogDistance);
    result.FogOpacity = clamp(1.0 - result.FogTransmittance, 0.0, 1.0);
    result.FogRadiance = vec3(0.0);
    result.FogAmbient = vec3(0.0);
    result.FogDirectional = vec3(0.0);
    result.AerialTransmittance = vec3(1.0);
    result.AerialScattering = vec3(0.0);
    result.AerialSingle = vec3(0.0);
    result.AerialMultiple = vec3(0.0);
    if (result.FogOpacity > 0.0)
    {
        result.FogRadiance = HeightFogRadiance(skyAtmosphereRadiance, params.fogParameters.w > 0.5,
                                                params.fallbackFogColor.rgb, rayDirection,
                                                params.fogTintAndSkyBlend, params.fogReferenceElevation);
    }
    if (params.aerialPerspective.x > 0.5 && rayDistance > 0.0)
    {
        MarchFogAndAerial(rayDirection, rayDistance, fogDistance,
                          FogStepOffset(pixelPosition, bJitterFog, false), result);
    }
    else if (result.FogOpacity > 0.0)
    {
        // 霧だけ: 周りの光には不透明度を掛け、太陽の散乱は選んだ配置とずらしで積む。
        // 距離の上限より奥は、影を引かずに解析した透過率の差を使う。
        result.FogAmbient = result.FogRadiance * result.FogOpacity * params.fogRange.y;
        float sampleOffset = FogStepOffset(pixelPosition, bJitterFog, params.fogRange.x > 0.0);
        result.FogDirectional = IntegrateDirectionalSingleScattering(rayDirection, fogDistance, sampleOffset);
    }
    return result;
}

// 大気の書き出しは画素によるずれを使わず、歩の中点で評価する。
VolumetricsRayResult EvaluateVolumetricsRay(vec3 rayDirection, float rayDistance)
{
    return EvaluateVolumetricsRay(rayDirection, rayDistance, vec2(0.0), false);
}

#endif
