// 高さフォグと空気遠近の評価（volumetrics.frag の合成と、撮影の大気の書き出し volumetrics_probe.frag が同じ式を使うための共通の include）。
// 記述子の束縛（0〜3）と定数（VolumetricsParams）もここで宣言する。値は VolumetricsPass が詰める。
// 束縛 4（空の透過率 LUT）は記述子の形に残っているが、空気遠近は高度ごとの表（定数）で積むので読まない。

#ifndef VOLUMETRICS_COMMON_GLSL
#define VOLUMETRICS_COMMON_GLSL

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;
layout(set = 0, binding = 3) uniform sampler2DArray cascadedShadowMap;

// 空気遠近の高度ごとの表の段の数（SkyAerialProfileSize）と、霧と空気遠近を一緒に積む視線の歩数
const uint kAerialProfileSize = 128u;
const int kAtmosphereSteps = 32;

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
} params;

bool IsFiniteFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

vec2 EquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989);
    uv += 0.5;
    return uv;
}

float ComputeAnalyticTransmittance(float originHeight,
                                   float rayDirectionY,
                                   float rayDistance)
{
    float density = max(params.fogParameters.x, 0.0);
    float baseHeight = params.fogParameters.y;
    float falloff = max(params.fogParameters.z, 0.0);
    if (density <= 0.0 || rayDistance <= 0.0 ||
        !IsFiniteFloat(originHeight) || !IsFiniteFloat(rayDirectionY) ||
        !IsFiniteFloat(rayDistance))
    {
        return 1.0;
    }

    float originExponent = -falloff * (originHeight - baseHeight);
    float verticalRate = falloff * clamp(rayDirectionY, -1.0, 1.0);
    float exponentChange = verticalRate * rayDistance;
    float logIntegral;
    if (abs(verticalRate) < 1.0e-8 || abs(exponentChange) < 1.0e-4)
    {
        logIntegral = log(rayDistance);
    }
    else
    {
        float logNumerator;
        if (exponentChange > 80.0)
        {
            logNumerator = 0.0;
        }
        else if (exponentChange > 0.0)
        {
            logNumerator = log(max(1.0 - exp(-exponentChange), 1.0e-35));
        }
        else if (exponentChange < -80.0)
        {
            logNumerator = -exponentChange;
        }
        else
        {
            logNumerator = log(max(exp(-exponentChange) - 1.0, 1.0e-35));
        }
        logIntegral = logNumerator - log(abs(verticalRate));
    }

    float logOpticalDepth = log(density) + originExponent + logIntegral;
    if (!IsFiniteFloat(logOpticalDepth))
    {
        return logOpticalDepth > 0.0 ? exp(-80.0) : 1.0;
    }
    if (logOpticalDepth >= log(80.0))
    {
        return exp(-80.0);
    }

    float opticalDepth = clamp(exp(logOpticalDepth), 0.0, 80.0);
    return exp(-opticalDepth);
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

vec3 IntegrateDirectionalSingleScattering(vec3 rayDirection, float rayDistance)
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
    const float stepLength = rayDistance / 24.0;
    vec3 scatteringRadiance = vec3(0.0);

    for (int stepIndex = 0; stepIndex < 24; ++stepIndex)
    {
        float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
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
                                   shadowVisibility * stepLength);
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
    // 高さフォグの周りの光の色（空の放射輝度 LUT の視線の向きの値か、LUT が無いときの既定の色。負は 0）
    vec3 FogRadiance;
    // 高さフォグの周りの光（FogRadiance × 霧の散乱係数 × 透過率を視線で積んだ値。空気遠近が無効なら × 不透明度）
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
// 高度ごとの表の太陽の照度 × 透過率）と多重散乱（散乱係数 × 表の Ψ_ms）。霧は太陽の単一散乱（影つき）と周りの光（空の放射輝度 LUT の
// 視線の向きの値）を散乱する。中点則で kAtmosphereSteps 歩
void MarchFogAndAerial(vec3 rayDirection, float rayDistance, inout VolumetricsRayResult result)
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

    float stepLength = rayDistance / float(kAtmosphereSteps);
    vec3 airDepth = vec3(0.0);
    float fogDepth = 0.0;
    vec3 single = vec3(0.0);
    vec3 multiple = vec3(0.0);
    vec3 fogAmbientWeight = vec3(0.0);
    vec3 fogDirectional = vec3(0.0);
    for (int stepIndex = 0; stepIndex < kAtmosphereSteps; ++stepIndex)
    {
        float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
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
        if (fogDensity > 0.0)
        {
            float worldY = params.cameraPositionAndPreExposure.y + rayDirection.y * sampleDistance;
            fogScattering = fogDensity * exp(clamp(-params.fogParameters.z * (worldY - params.fogParameters.y),
                                                   -80.0, 80.0));
        }

        vec3 stepAirDepth = airExtinction * stepLength;
        float stepFogDepth = fogScattering * stepLength;
        // その歩の中点までの透過率（空の媒質と霧の両方）
        vec3 transmittance = exp(-(airDepth + 0.5 * stepAirDepth) - vec3(fogDepth + 0.5 * stepFogDepth));
        vec3 sunIlluminance = SampleAerialProfile(0u, altitude);
        vec3 multipleScattering = SampleAerialProfile(1u, altitude);
        single += transmittance * (rayleighScattering * rayleighPhase + vec3(mieScattering * miePhase)) *
                  sunIlluminance * stepLength;
        multiple += transmittance * (rayleighScattering + vec3(mieScattering)) * multipleScattering * stepLength;
        if (fogScattering > 0.0)
        {
            fogAmbientWeight += transmittance * (fogScattering * stepLength);
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
        result.FogAmbient = result.FogRadiance * fogAmbientWeight;
        result.FogDirectional = fogDirectional;
    }
}

VolumetricsRayResult EvaluateVolumetricsRay(vec3 rayDirection, float rayDistance)
{
    VolumetricsRayResult result;
    result.FogTransmittance = ComputeAnalyticTransmittance(
        params.cameraPositionAndPreExposure.y,
        rayDirection.y,
        rayDistance);
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
        vec3 fogRadiance = params.fallbackFogColor.rgb;
        if (params.fogParameters.w > 0.5)
        {
            fogRadiance = texture(skyAtmosphereRadiance,
                                  EquirectangularUV(rayDirection)).rgb;
        }
        result.FogRadiance = max(fogRadiance, vec3(0.0));
    }
    if (params.aerialPerspective.x > 0.5 && rayDistance > 0.0)
    {
        MarchFogAndAerial(rayDirection, rayDistance, result);
    }
    else if (result.FogOpacity > 0.0)
    {
        // 霧だけ: 周りの光は不透明度を掛け、太陽の単一散乱は霧の透過率だけで減らして積む
        result.FogAmbient = result.FogRadiance * result.FogOpacity;
        result.FogDirectional = IntegrateDirectionalSingleScattering(rayDirection, rayDistance);
    }
    return result;
}

#endif
