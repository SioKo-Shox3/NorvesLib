#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;
layout(set = 0, binding = 3) uniform sampler2DArray cascadedShadowMap;

layout(std140, set = 0, binding = 2) uniform VolumetricsParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndPreExposure;
    vec4 cameraForwardAndScatteringEnabled;
    vec4 fogParameters; // x=density, y=base height, z=height falloff, w=sky radiance available
    vec4 fallbackFogColor;
    vec4 directionalLightDirectionAndAnisotropy;
    vec4 directionalLightRadianceAndEnabled;
    mat4 cascadeView[4];
    mat4 cascadeProjection[4];
    vec4 cascadeSplitDistances[2];
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

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    if (!IsFiniteFloat(depth) || depth >= 0.999999)
    {
        // LightingPass already wrote the R2 sky and sun disk for clear-depth pixels.
        outColor = vec4(0.0);
        return;
    }

    vec4 clipPosition = vec4(fragUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPosition = params.inverseViewProjection * clipPosition;
    if (!IsFiniteFloat(worldPosition.w) || abs(worldPosition.w) <= 1.0e-8)
    {
        outColor = vec4(0.0);
        return;
    }
    worldPosition.xyz /= worldPosition.w;

    vec3 viewVector = worldPosition.xyz - params.cameraPositionAndPreExposure.xyz;
    float rayDistance = length(viewVector);
    if (!IsFiniteFloat(rayDistance) || rayDistance <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 rayDirection = viewVector / rayDistance;
    float transmittance = ComputeAnalyticTransmittance(
        params.cameraPositionAndPreExposure.y,
        rayDirection.y,
        rayDistance);
    float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
    if (opacity <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 fogRadiance = params.fallbackFogColor.rgb;
    if (params.fogParameters.w > 0.5)
    {
        fogRadiance = texture(skyAtmosphereRadiance,
                              EquirectangularUV(rayDirection)).rgb;
    }
    fogRadiance = max(fogRadiance, vec3(0.0)) * params.cameraPositionAndPreExposure.w;
    vec3 singleScatteringRadiance = IntegrateDirectionalSingleScattering(
        rayDirection,
        rayDistance) * params.cameraPositionAndPreExposure.w;
    singleScatteringRadiance = min(singleScatteringRadiance, vec3(65504.0));
    outColor = vec4(fogRadiance * opacity + singleScatteringRadiance, opacity);
}
