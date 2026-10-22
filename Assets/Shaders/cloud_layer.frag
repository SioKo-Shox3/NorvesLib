#version 450

// 空の画素に雲の層を描く。惑星の球殻にした雲の層を視線で進め、3D ノイズの密度から太陽の散乱光（前方・後方の
// 2 つの位相と多重散乱の近似）と空の環境光を積分し、前乗算のアルファ（散乱光、1 - 透過率）で出す。
// 式は撮影の雲の書き出し（cloud_layer_probe.frag）と共有する（Common/CloudLayerCommon.glsl）。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/CloudLayerCommon.glsl"

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    if (IsFiniteFloat(depth) && depth < 0.999999)
    {
        // 不透明物の画素。雲の層は地形より高いので描かない
        outColor = vec4(0.0);
        return;
    }

    vec3 rayDirection;
    if (!ComputeCloudRayDirection(fragUV, rayDirection))
    {
        outColor = vec4(0.0);
        return;
    }

    float jitter = InterleavedGradientNoise(gl_FragCoord.xy, params.sunDirectionAndFrameIndex.w);
    CloudRayResult ray = MarchCloudLayer(rayDirection, jitter);
    float opacity = clamp(1.0 - ray.Transmittance, 0.0, 1.0);
    if (!ray.bMarched || opacity <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    float hazeVisibility;
    float fade;
    vec3 cloudRadiance = ComposeCloudRadiance(ray, rayDirection, opacity, hazeVisibility, fade);
    cloudRadiance = min(cloudRadiance * params.cameraPositionAndPreExposure.w, vec3(65504.0));
    outColor = vec4(cloudRadiance, opacity * fade);
}
