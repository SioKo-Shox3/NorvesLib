#version 450

// 撮影の大気の書き出し（--atmosphere-dump）: 決めた視線ごとに、volumetrics.frag と同じ式（Common/VolumetricsCommon.glsl）で
// 高さフォグと空気遠近を評価し、事前露出の前の値を書く。横 = 視線の番号、縦 = 値の種類（AtmosphereProbePass の行の約束）。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/VolumetricsCommon.glsl"

const uint kAtmosphereProbeMaxRays = 256u;

layout(std140, set = 0, binding = 5) uniform AtmosphereProbeRays
{
    uvec4 rayCount;                       // x = 視線の数
    vec4 rays[kAtmosphereProbeMaxRays];   // xyz = カメラからの向き（ワールド）, w = 距離 m
} probe;

void main()
{
    uint rayIndex = uint(gl_FragCoord.x);
    uint row = uint(gl_FragCoord.y);
    if (rayIndex >= min(probe.rayCount.x, kAtmosphereProbeMaxRays))
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 rayDirection = normalize(probe.rays[rayIndex].xyz);
    float rayDistance = probe.rays[rayIndex].w;
    VolumetricsRayResult ray = EvaluateVolumetricsRay(rayDirection, rayDistance);

    bool bAerialEnabled = params.aerialPerspective.x > 0.5;
    float aerialMeanTransmittance = bAerialEnabled
        ? clamp(dot(ray.AerialTransmittance, vec3(1.0 / 3.0)), 0.0, 1.0)
        : 1.0;
    // volumetrics.frag の合成と同じ式（事前露出と fp16 の上限の丸めは掛けない）
    vec3 fogLight = ray.FogOpacity > 0.0 ? ray.FogRadiance * ray.FogOpacity + ray.FogDirectional : vec3(0.0);
    vec3 compositeLight = bAerialEnabled ? fogLight + ray.AerialScattering * ray.FogTransmittance : fogLight;
    float compositeAlpha = bAerialEnabled
        ? clamp(1.0 - ray.FogTransmittance * aerialMeanTransmittance, 0.0, 1.0)
        : ray.FogOpacity;

    if (row == 0u)
    {
        outColor = vec4(ray.FogTransmittance, ray.FogOpacity, aerialMeanTransmittance, compositeAlpha);
    }
    else if (row == 1u)
    {
        outColor = vec4(ray.FogRadiance, params.fogParameters.w > 0.5 ? 1.0 : 0.0);
    }
    else if (row == 2u)
    {
        outColor = vec4(ray.FogDirectional, params.cameraForwardAndScatteringEnabled.w > 0.5 ? 1.0 : 0.0);
    }
    else if (row == 3u)
    {
        outColor = vec4(ray.AerialTransmittance, bAerialEnabled ? 1.0 : 0.0);
    }
    else if (row == 4u)
    {
        outColor = vec4(ray.AerialScattering, 0.0);
    }
    else if (row == 5u)
    {
        // 空の放射輝度 LUT のこの向きの値（高さフォグの色と同じ引き方）
        vec3 skyRadiance = params.fogParameters.w > 0.5
            ? texture(skyAtmosphereRadiance, EquirectangularUV(rayDirection)).rgb
            : vec3(0.0);
        outColor = vec4(skyRadiance, 0.0);
    }
    else
    {
        outColor = vec4(compositeLight, compositeAlpha);
    }
}
