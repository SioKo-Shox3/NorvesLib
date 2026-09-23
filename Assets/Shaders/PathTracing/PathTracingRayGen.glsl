// 決定論的な1試料を追跡し、前回までの平均と合成する。
#version 460
#extension GL_EXT_ray_tracing : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
#include "PathTracing/PathTracingCommon.glsl"

layout(set = 0, binding = 3) uniform sampler2D previousAverage;
layout(set = 0, binding = 4, rgba32f) uniform writeonly image2D currentAverage;
layout(set = 0, binding = 5) uniform sampler2D skyRadiance;
layout(set = 0, binding = 6) uniform sampler2D skyTransmittance;
layout(set = 0, binding = 7) uniform sampler2D skySunDisk;

layout(location = 0) rayPayloadEXT PathPayload payload;

uint NextRandom(inout uint state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float Random01(inout uint state)
{
    return float(NextRandom(state) & 0x00ffffffu) * (1.0 / 16777216.0);
}

vec3 CosineHemisphere(vec3 normal, inout uint state)
{
    float phi = 6.28318530718 * Random01(state);
    float radius = sqrt(Random01(state));
    vec3 tangent = normalize(cross(abs(normal.z) < 0.9 ? vec3(0.0, 0.0, 1.0) :
                                   vec3(0.0, 1.0, 0.0), normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * (radius * cos(phi)) +
                     bitangent * (radius * sin(phi)) +
                     normal * sqrt(max(0.0, 1.0 - radius * radius)));
}

vec2 EquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    return uv * vec2(0.15915494, 0.31830989) + 0.5;
}

vec3 SkyMissRadiance(vec3 direction, bool primaryRay)
{
    if (parameters.skyState.z < 0.5)
    {
        return parameters.skyState.w > 0.5 ? vec3(0.0) : vec3(0.05);
    }
    vec3 sky = textureLod(skyRadiance, EquirectangularUV(direction), 0.0).rgb;
    vec4 disk = textureLod(skySunDisk, vec2(0.5), 0.0);
    if (disk.a > 0.5)
    {
        sky *= clamp(textureLod(skyTransmittance,
                                vec2(clamp(direction.y, 0.0, 1.0), 0.0),
                                0.0).rgb, vec3(0.0), vec3(1.0));
    }
    sky *= parameters.skyState.x;
    if (primaryRay && disk.a > 0.5 &&
        dot(direction, parameters.skySunDirectionAndCosRadius.xyz) >=
            parameters.skySunDirectionAndCosRadius.w)
    {
        sky += disk.rgb;
    }
    return max(sky, vec3(0.0));
}

vec3 SampleSolarDirection(inout uint state)
{
    vec3 sun = parameters.skySunDirectionAndCosRadius.xyz;
    float cosine = mix(parameters.skySunDirectionAndCosRadius.w, 1.0,
                       Random01(state));
    float phi = 6.28318530718 * Random01(state);
    float radius = sqrt(max(0.0, 1.0 - cosine * cosine));
    vec3 tangent = normalize(cross(abs(sun.z) < 0.9 ? vec3(0.0, 0.0, 1.0) :
                                   vec3(0.0, 1.0, 0.0), sun));
    vec3 bitangent = cross(sun, tangent);
    return normalize(sun * cosine + tangent * (radius * cos(phi)) +
                     bitangent * (radius * sin(phi)));
}

bool IsFiniteFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

float FogTransmittance(float originHeight, float directionY, float distance)
{
    float density = max(parameters.fogDensityHeightFalloffAndEnabled.x, 0.0);
    float baseHeight = parameters.fogDensityHeightFalloffAndEnabled.y;
    float falloff = max(parameters.fogDensityHeightFalloffAndEnabled.z, 0.0);
    if (density <= 0.0 || distance <= 0.0 ||
        !IsFiniteFloat(originHeight) || !IsFiniteFloat(directionY) ||
        !IsFiniteFloat(distance))
    {
        return 1.0;
    }

    float originExponent = -falloff * (originHeight - baseHeight);
    float verticalRate = falloff * clamp(directionY, -1.0, 1.0);
    float exponentChange = verticalRate * distance;
    float logIntegral;
    if (abs(verticalRate) < 1.0e-8 || abs(exponentChange) < 1.0e-4)
    {
        logIntegral = log(distance);
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
    return exp(-clamp(exp(logOpticalDepth), 0.0, 80.0));
}

vec3 FogSingleScattering(vec3 origin, vec3 direction, float distance)
{
    if (parameters.fogLightRadianceAndEnabled.w < 0.5)
    {
        return vec3(0.0);
    }
    float anisotropy = clamp(parameters.fogLightDirectionAndAnisotropy.w,
                             -0.95, 0.95);
    float cosineTheta = clamp(dot(parameters.fogLightDirectionAndAnisotropy.xyz,
                                  -direction), -1.0, 1.0);
    float denominator = max(1.0 + anisotropy * anisotropy -
                            2.0 * anisotropy * cosineTheta, 1.0e-4);
    float phase = (1.0 - anisotropy * anisotropy) /
                  (12.5663706 * pow(denominator, 1.5));
    float stepLength = distance / 24.0;
    vec3 lightDirection = -parameters.fogLightDirectionAndAnisotropy.xyz;
    vec3 scattering = vec3(0.0);
    for (int stepIndex = 0; stepIndex < 24; ++stepIndex)
    {
        float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
        vec3 samplePosition = origin + direction * sampleDistance;
        float densityExponent = -parameters.fogDensityHeightFalloffAndEnabled.z *
            (samplePosition.y - parameters.fogDensityHeightFalloffAndEnabled.y);
        float localDensity = parameters.fogDensityHeightFalloffAndEnabled.x *
            exp(clamp(densityExponent, -80.0, 80.0));
        float viewTransmittance = FogTransmittance(
            origin.y, direction.y, sampleDistance);
        if (!IsFiniteFloat(localDensity) || !IsFiniteFloat(viewTransmittance) ||
            localDensity <= 0.0 || viewTransmittance <= 0.0)
        {
            continue;
        }
        payload.Hit = 0u;
        traceRayEXT(scene, gl_RayFlagsOpaqueEXT |
                     gl_RayFlagsTerminateOnFirstHitEXT,
                    0xffu, 0u, 0u, 0u,
                    samplePosition, 0.001, lightDirection, 100000.0, 0);
        if (payload.Hit == 0u)
        {
            scattering += parameters.fogLightRadianceAndEnabled.rgb *
                (phase * localDensity * viewTransmittance * stepLength);
        }
    }
    return min(scattering * parameters.fogColorAndPreExposure.w,
               vec3(65504.0));
}

void ApplyFogSegment(vec3 origin, vec3 direction, vec3 surfacePosition,
                     inout vec3 throughput, inout vec3 radiance)
{
    if (parameters.fogDensityHeightFalloffAndEnabled.w < 0.5)
    {
        return;
    }
    float distance = length(surfacePosition - origin);
    if (!IsFiniteFloat(distance) || distance <= 0.0)
    {
        return;
    }
    float transmittance = FogTransmittance(origin.y, direction.y, distance);
    vec3 fogColor = parameters.fogColorAndPreExposure.rgb;
    if (parameters.skyState.z > 0.5)
    {
        fogColor = textureLod(skyRadiance, EquirectangularUV(direction), 0.0).rgb;
    }
    fogColor = max(fogColor, vec3(0.0)) *
               parameters.fogColorAndPreExposure.w;
    vec3 scattering = FogSingleScattering(origin, direction, distance);
    radiance += throughput *
        (fogColor * clamp(1.0 - transmittance, 0.0, 1.0) + scattering);
    throughput *= transmittance;
}

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    ivec2 extent = imageSize(currentAverage);
    if (any(greaterThanEqual(pixel, extent)))
    {
        return;
    }

    uint sampleIndex = parameters.imageState.z;
    uint state = uint(pixel.x + 1) * 0x9e3779b9u ^
                 uint(pixel.y + 1) * 0x85ebca6bu ^
                 (sampleIndex + 1u) * 0xc2b2ae35u ^ 0x6a09e667u;
    vec2 uv = (vec2(pixel) + vec2(Random01(state), Random01(state))) / vec2(extent);
    vec4 farPoint = parameters.inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    if (isnan(farPoint.w) || isinf(farPoint.w) || abs(farPoint.w) < 0.000001)
    {
        imageStore(currentAverage, pixel, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    vec3 origin = parameters.cameraPosition.xyz;
    vec3 direction = normalize(farPoint.xyz / farPoint.w - origin);
    if (any(isnan(direction)) || any(isinf(direction)))
    {
        imageStore(currentAverage, pixel, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);
    uint debugOutput = uint(parameters.exposureAndDebug.y + 0.5);
    vec3 debugValue = vec3(0.0);

    for (uint bounce = 0u; bounce < 8u; ++bounce)
    {
        payload.Hit = 0u;
        traceRayEXT(scene, gl_RayFlagsOpaqueEXT, 0xffu, 0u, 0u, 0u,
                    origin, 0.001, direction, 100000.0, 0);
        if (payload.Hit == 0u)
        {
            radiance += throughput * SkyMissRadiance(direction, bounce == 0u);
            break;
        }

        vec3 surfacePosition = payload.Position;
        vec3 geometricNormal = payload.GeometricNormal;
        vec3 surfaceNormal = payload.ShadingNormal;
        vec3 surfaceColor = clamp(payload.Albedo, vec3(0.0), vec3(1.0));
        // 発光はGBufferと同じく色×nitsの物理値に、カメラのプリエクスポージャを掛ける。
        vec3 surfaceEmission = payload.Emission * parameters.exposureAndDebug.x;
        if (bounce == 0u && debugOutput != PATH_DEBUG_NONE)
        {
            debugValue = debugOutput == PATH_DEBUG_ALBEDO ? payload.Albedo :
                         debugOutput == PATH_DEBUG_SHADING_NORMAL ? payload.ShadingNormal :
                         vec3(payload.Metallic, payload.Roughness, 0.0);
            break;
        }
        ApplyFogSegment(origin, direction, surfacePosition, throughput, radiance);
        radiance += throughput * surfaceEmission;
        if (parameters.skyState.z > 0.5)
        {
            vec3 solarDirection = SampleSolarDirection(state);
            float incidence = max(dot(surfaceNormal, solarDirection), 0.0);
            if (incidence > 0.0)
            {
                payload.Hit = 0u;
                traceRayEXT(scene, gl_RayFlagsOpaqueEXT |
                                 gl_RayFlagsTerminateOnFirstHitEXT,
                            0xffu, 0u, 0u, 0u,
                            surfacePosition + geometricNormal * 0.002,
                            0.001, solarDirection, 100000.0, 0);
                if (payload.Hit == 0u)
                {
                    radiance += throughput * surfaceColor *
                        (incidence * parameters.skyState.y * 0.31830988618);
                }
            }
        }
        throughput *= surfaceColor;
        if (bounce >= 3u)
        {
            float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)),
                                   0.05, 0.95);
            if (Random01(state) > survival)
            {
                break;
            }
            throughput /= survival;
        }
        origin = surfacePosition + geometricNormal * 0.002;
        direction = CosineHemisphere(surfaceNormal, state);
    }

    if (debugOutput != PATH_DEBUG_NONE)
    {
        radiance = debugValue;
    }
    if (any(isnan(radiance)) || any(isinf(radiance)))
    {
        radiance = vec3(0.0);
    }
    radiance = debugOutput == PATH_DEBUG_SHADING_NORMAL ? clamp(radiance, vec3(-1.0), vec3(1.0))
                                                        : clamp(radiance, vec3(0.0), vec3(65504.0));
    vec3 average = radiance;
    if (sampleIndex > 0u)
    {
        vec3 previous = texelFetch(previousAverage, pixel, 0).rgb;
        average = previous + (radiance - previous) / float(sampleIndex + 1u);
    }
    imageStore(currentAverage, pixel, vec4(average, 1.0));
}
