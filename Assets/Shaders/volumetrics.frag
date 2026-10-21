#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/VolumetricsCommon.glsl"

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
    VolumetricsRayResult ray = EvaluateVolumetricsRay(rayDirection, rayDistance);
    float transmittance = ray.FogTransmittance;
    float opacity = ray.FogOpacity;
    vec3 fogLight = vec3(0.0);
    if (opacity > 0.0)
    {
        vec3 singleScatteringRadiance = ray.FogDirectional * params.cameraPositionAndPreExposure.w;
        singleScatteringRadiance = min(singleScatteringRadiance, vec3(65504.0));
        fogLight = ray.FogAmbient * params.cameraPositionAndPreExposure.w + singleScatteringRadiance;
    }
    if (params.aerialPerspective.x <= 0.5)
    {
        outColor = vec4(fogLight, opacity);
        return;
    }

    // 霧と空気遠近の光は、視線の上で一緒に積んだ値（互いの透過率で減らしてある）をそのまま足す。合成は 1 - アルファで
    // 下の色を薄めるので、空気遠近の透過率は RGB の平均で掛ける（色は散乱光の側が持つ）。
    vec3 aerialScattering = ray.AerialScattering * params.cameraPositionAndPreExposure.w;
    aerialScattering = min(aerialScattering, vec3(65504.0));
    float aerialMeanTransmittance = clamp(dot(ray.AerialTransmittance, vec3(1.0 / 3.0)), 0.0, 1.0);
    outColor = vec4(fogLight + aerialScattering,
                    clamp(1.0 - transmittance * aerialMeanTransmittance, 0.0, 1.0));
}
