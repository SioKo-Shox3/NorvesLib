#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#define VOLUMETRICS_USE_VSM 1
#include "Common/DepthConvention.glsl"
#include "Common/VolumetricsCommon.glsl"

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    if (!IsFiniteFloat(depth) || IsBackgroundDepth(depth))
    {
        // クリア値の画素には空と太陽の円盤があるため、局所霧だけを手前に重ねる。
        if (LocalFogCount(params.localFog) == 0)
        {
            outColor = vec4(0.0);
            return;
        }
        vec4 backgroundPosition = params.inverseViewProjection * vec4(fragUV * 2.0 - 1.0, 0.5, 1.0);
        if (!IsFiniteFloat(backgroundPosition.w) || abs(backgroundPosition.w) <= 1.0e-8)
        {
            outColor = vec4(0.0);
            return;
        }
        vec3 backgroundView = backgroundPosition.xyz / backgroundPosition.w -
                              params.cameraPositionAndPreExposure.xyz;
        float backgroundDistance = length(backgroundView);
        if (!IsFiniteFloat(backgroundDistance) || backgroundDistance <= 0.0)
        {
            outColor = vec4(0.0);
            return;
        }
        vec3 backgroundDirection = backgroundView / backgroundDistance;
        float localTransmittance;
        vec3 localRadiance;
        IntegrateLocalFog(backgroundDirection, 1.0e30, FogStepOffset(gl_FragCoord.xy, true, true),
                          localTransmittance, localRadiance);
        outColor = vec4(min(localRadiance * params.cameraPositionAndPreExposure.w, vec3(65504.0)),
                        1.0 - localTransmittance);
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
    VolumetricsRayResult ray = EvaluateVolumetricsRay(rayDirection, rayDistance, gl_FragCoord.xy, true);
    float transmittance = ray.FogTransmittance;
    float opacity = ray.FogOpacity;
    vec3 fogLight = vec3(0.0);
    if (opacity > 0.0)
    {
        vec3 singleScatteringRadiance = ray.FogDirectional * params.cameraPositionAndPreExposure.w;
        singleScatteringRadiance = min(singleScatteringRadiance, vec3(65504.0));
        fogLight = ray.FogAmbient * params.cameraPositionAndPreExposure.w + singleScatteringRadiance;
    }
    vec4 baseColor;
    if (params.aerialPerspective.x <= 0.5)
    {
        baseColor = vec4(fogLight, opacity);
    }
    else
    {
        // 霧と空気遠近の光は、視線の上で一緒に積んだ値（互いの透過率で減らしてある）をそのまま足す。合成は 1 - アルファで
        // 下の色を薄めるので、空気遠近の透過率は RGB の平均で掛ける（色は散乱光の側が持つ）。
        vec3 aerialScattering = ray.AerialScattering * params.cameraPositionAndPreExposure.w;
        aerialScattering = min(aerialScattering, vec3(65504.0));
        float aerialMeanTransmittance = clamp(dot(ray.AerialTransmittance, vec3(1.0 / 3.0)), 0.0, 1.0);
        baseColor = vec4(fogLight + aerialScattering,
                         clamp(1.0 - transmittance * aerialMeanTransmittance, 0.0, 1.0));
    }

    // 局所霧の透過率で奥の光を減らし、局所霧の散乱光を手前に足す。
    if (LocalFogCount(params.localFog) > 0)
    {
        baseColor.rgb = baseColor.rgb * ray.LocalFogTransmittance +
                        min(ray.LocalFogRadiance * params.cameraPositionAndPreExposure.w, vec3(65504.0));
        baseColor.a = 1.0 - (1.0 - baseColor.a) * ray.LocalFogTransmittance;
    }
    outColor = baseColor;
}
