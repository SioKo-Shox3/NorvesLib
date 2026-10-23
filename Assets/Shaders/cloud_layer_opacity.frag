#version 450

// 撮影の雲の不透明度の画像（--wasteland-cloud-opacity）: 画面の全画素で、cloud_layer.frag と同じ式（Common/CloudLayerCommon.glsl）・
// 同じ定数・同じ歩の端数（画素とフレームの interleaved gradient noise）で雲の層を進め、雲だけの不透明度を書く。
// 霞・太陽の円盤・地形は含めない（空気遠近は不透明度に掛からない）。描画先は雲を描く画面と同じ大きさで、gl_FragCoord は
// cloud_layer.frag がその画素で使う値と同じになる。
//   r = 雲だけの不透明度（1 − 透過率に溶かす割合を掛けた後。cloud_layer.frag が重ねるアルファと同じ値。空でない画素は 0）
//   g = 1 − 透過率（溶かす割合を掛ける前。空でない画素は 0）
//   b = 地形のマスク（深度が空でない画素は 1、空の画素は 0）
//   a = 打ち切りの距離へ溶かす割合（雲の無い視線・空でない画素は 1）

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/CloudLayerCommon.glsl"

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    if (IsFiniteFloat(depth) && depth < 0.999999)
    {
        // 不透明物の画素。cloud_layer.frag はここへ何も重ねない
        outColor = vec4(0.0, 0.0, 1.0, 1.0);
        return;
    }

    vec3 rayDirection;
    if (!ComputeCloudRayDirection(fragUV, rayDirection))
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float jitter = InterleavedGradientNoise(gl_FragCoord.xy, params.sunDirectionAndFrameIndex.w);
    CloudRayResult ray = MarchCloudLayer(rayDirection, jitter);
    float opacity = clamp(1.0 - ray.Transmittance, 0.0, 1.0);
    if (!ray.bMarched || opacity <= 0.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float aerialVisibility;
    float fade;
    ComposeCloudRadiance(ray, rayDirection, opacity, aerialVisibility, fade);
    outColor = vec4(opacity * fade, opacity, 0.0, fade);
}
