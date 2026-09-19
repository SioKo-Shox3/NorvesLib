#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;

layout(std140, set = 0, binding = 2) uniform VolumetricsParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndPreExposure;
    vec4 fogParameters; // x=density, y=base height, z=height falloff, w=sky radiance available
    vec4 fallbackFogColor;
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
    outColor = vec4(fogRadiance * opacity, opacity);
}
