#version 450

// 撮影の雲の書き出し（--cloud-dump）: 決めた画素の視線ごとに、cloud_layer.frag と同じ式（Common/CloudLayerCommon.glsl）・
// 同じ定数・同じ歩の端数（画素とフレームの interleaved gradient noise）で雲の層を進め、事前露出の前の値を書く。
// 横 = 視線の番号、縦 = 値の種類（CloudProbePass の行の約束）。視線ごとの診断の切り替え（pixels の z）を立てた列は、
// 同じ画素の視線を打ち切り・溶かし・空気遠近を外して評価する（遠い雲の消える原因を分ける反実仮想。描画には効かない）。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/CloudLayerCommon.glsl"

const uint kCloudProbeMaxRays = 256u;

layout(std140, set = 0, binding = 5) uniform CloudProbeRays
{
    uvec4 rayCount;                       // x = 視線の数
    vec4 targetSize;                      // xy = 雲を描く画面の大きさ（画素）
    vec4 pixels[kCloudProbeMaxRays];      // xy = 画素の中心（gl_FragCoord と同じ。左上が原点）、z = 診断の切り替え（kCloudDiagnostic*）
} probe;

void main()
{
    uint rayIndex = uint(gl_FragCoord.x);
    uint row = uint(gl_FragCoord.y);
    if (rayIndex >= min(probe.rayCount.x, kCloudProbeMaxRays))
    {
        outColor = vec4(0.0);
        return;
    }

    vec2 pixel = probe.pixels[rayIndex].xy;
    uint diagnosticFlags = uint(probe.pixels[rayIndex].z + 0.5);
    vec2 uv = pixel / probe.targetSize.xy;
    float depth = textureLod(sceneDepthTexture, uv, 0.0).r;
    bool bSkyPixel = !(IsFiniteFloat(depth) && depth < 0.999999);

    vec3 rayDirection;
    bool bDirection = ComputeCloudRayDirection(uv, rayDirection);
    float jitter = InterleavedGradientNoise(pixel, params.sunDirectionAndFrameIndex.w);
    CloudRayResult ray = bDirection ? MarchCloudLayerWithDiagnostics(rayDirection, jitter, diagnosticFlags)
                                    : MakeEmptyCloudRayResult();
    float opacity = clamp(1.0 - ray.Transmittance, 0.0, 1.0);
    float aerialVisibility = 1.0;
    float fade = 1.0;
    vec3 cloudRadiance = vec3(0.0);
    // cloud_layer.frag がこの画素に重ねる割合（空でない画素・雲の無い視線は 0）
    float alpha = 0.0;
    if (ray.bMarched && opacity > 0.0)
    {
        cloudRadiance = ComposeCloudRadianceWithDiagnostics(ray, rayDirection, opacity, diagnosticFlags, aerialVisibility, fade);
        alpha = bSkyPixel ? opacity * fade : 0.0;
    }

    if (row == 0u)
    {
        outColor = vec4(ray.Transmittance, alpha, fade, float(ray.StepsTaken));
    }
    else if (row == 1u)
    {
        outColor = vec4(ray.Scattered, ray.CloudDistance);
    }
    else if (row == 2u)
    {
        outColor = vec4(cloudRadiance, aerialVisibility);
    }
    else if (row == 3u)
    {
        outColor = vec4(rayDirection, jitter);
    }
    else if (row == 4u)
    {
        outColor = vec4(ray.SkyAbove, ray.StartDistance);
    }
    else if (row == 5u)
    {
        outColor = vec4(ray.GroundRadiance, ray.EndDistance);
    }
    else if (row == 6u)
    {
        outColor = vec4(SampleSky(rayDirection), depth);
    }
    else
    {
        outColor = vec4(ray.OpticalDepth, float(ray.StepCount), ray.StepLength,
                        (ray.bMarched ? 1.0 : 0.0) + (bDirection ? 2.0 : 0.0) + 4.0 * float(diagnosticFlags));
    }
}
