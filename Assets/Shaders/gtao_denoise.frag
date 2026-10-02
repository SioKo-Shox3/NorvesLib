#version 450

// ========================================
// GTAOの空間の雑音除去
// ========================================
// gtao.frag の雑音は4×4の画素で一巡するので、4×4の平均で周期を消す。
// ビュー空間の深度が中心と大きく違う画素（物体の輪郭の向こう側）は重みを下げて、遮蔽が輪郭の外へ
// にじまないようにする。最後に0〜1へ収める。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D gtaoInput;
layout(set = 0, binding = 1) uniform sampler2D gbufferDepth;

layout(set = 0, binding = 2) uniform DenoiseParams
{
    mat4 invProjection;  // クリップ → ビュー（深度をビュー空間の距離へ直す）
    vec4 texelSize;      // xy=1/寸法
} params;

// 中心と比べてよい深度の差（中心のビュー空間の距離に対する割合）
const float RELATIVE_DEPTH_TOLERANCE = 0.1;

float ViewDistance(float depth)
{
    // 深度だけで決まる成分（z と w）から、カメラの面からの距離を求める
    vec4 viewPos = params.invProjection * vec4(0.0, 0.0, depth, 1.0);
    return abs(viewPos.z / viewPos.w);
}

void main()
{
    ivec2 size = textureSize(gtaoInput, 0);
    ivec2 centerTexel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - 1);
    float centerDepth = texelFetch(gbufferDepth, centerTexel, 0).r;
    if (centerDepth >= 0.9999)
    {
        outAO = 1.0;
        return;
    }
    float centerDistance = ViewDistance(centerDepth);
    float tolerance = max(centerDistance * RELATIVE_DEPTH_TOLERANCE, 1.0e-4);

    float result = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            ivec2 texel = clamp(centerTexel + ivec2(x, y), ivec2(0), size - 1);
            float sampleDepth = texelFetch(gbufferDepth, texel, 0).r;
            if (sampleDepth >= 0.9999)
            {
                continue;
            }
            float difference = abs(ViewDistance(sampleDepth) - centerDistance);
            float weight = clamp(1.0 - difference / tolerance, 0.0, 1.0);
            result += texelFetch(gtaoInput, texel, 0).r * weight;
            weightSum += weight;
        }
    }

    float visibility = weightSum > 1.0e-4
        ? result / weightSum
        : texelFetch(gtaoInput, centerTexel, 0).r;
    outAO = clamp(visibility, 0.0, 1.0);
}
