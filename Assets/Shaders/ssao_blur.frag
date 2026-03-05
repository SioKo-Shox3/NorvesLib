#version 450

// ========================================
// SSAOブラーパス
// ========================================
// SSAOのノイズ除去用4x4ボックスブラー
// エッジを保持するため深度差が大きいサンプルは除外

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D ssaoInput;
layout(set = 0, binding = 1) uniform sampler2D gbufferDepth;

layout(set = 0, binding = 2) uniform BlurParams
{
    vec4 texelSize;   // xy=1/width, 1/height
} params;

void main()
{
    float centerDepth = texture(gbufferDepth, fragUV).r;
    float result = 0.0;
    float weightSum = 0.0;

    // 4x4 エッジ保持ブラー
    for (int x = -2; x < 2; ++x)
    {
        for (int y = -2; y < 2; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * params.texelSize.xy;
            vec2 sampleUV = fragUV + offset;

            float sampleDepth = texture(gbufferDepth, sampleUV).r;
            float sampleAO = texture(ssaoInput, sampleUV).r;

            // エッジ保持: 深度差が大きいサンプルは無視
            float depthDiff = abs(centerDepth - sampleDepth);
            float weight = step(depthDiff, 0.001);

            result += sampleAO * weight;
            weightSum += weight;
        }
    }

    outAO = result / max(weightSum, 1.0);
}
