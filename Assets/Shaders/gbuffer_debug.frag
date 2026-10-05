#version 450

// ========================================
// GBuffer の検証表示: 法線・速度・深度を色にして出す
//
// ビジビリティバッファの on（幾何の解決が書く）と off（GBufferPass が書く）で、同じ画面の GBuffer の値を撮り比べる用。
//   params.x = 0: 世界の法線（xyz × 0.5 + 0.5）
//   params.x = 1: 速度（現在の UV − 前の UV）。R = x、G = y を params.y 倍して灰色 0.5 からの差にする
//   params.x = 2: 深度。R = 深度、G = fract(深度 × 256)、B = fract(深度 × 65536)（細かい差が縞で見える）
// ========================================

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D normalTexture;
layout(set = 0, binding = 1) uniform sampler2D velocityTexture;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

layout(std140, set = 0, binding = 3) uniform DebugParams
{
    // x: 表示する内容、y: 速度の倍率
    vec4 params;
} debugParams;

void main()
{
    const int mode = int(debugParams.params.x + 0.5);
    vec3 color = vec3(0.0);
    if (mode == 0)
    {
        const ivec2 size = textureSize(normalTexture, 0);
        const ivec2 pixel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - ivec2(1));
        color = texelFetch(normalTexture, pixel, 0).xyz * 0.5 + vec3(0.5);
    }
    else if (mode == 1)
    {
        const ivec2 size = textureSize(velocityTexture, 0);
        const ivec2 pixel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - ivec2(1));
        const vec2 velocity = texelFetch(velocityTexture, pixel, 0).xy;
        color = vec3(vec2(0.5) + velocity * debugParams.params.y, 0.5);
    }
    else
    {
        const ivec2 size = textureSize(depthTexture, 0);
        const ivec2 pixel = clamp(ivec2(fragUV * vec2(size)), ivec2(0), size - ivec2(1));
        const float depth = texelFetch(depthTexture, pixel, 0).x;
        color = vec3(depth, fract(depth * 256.0), fract(depth * 65536.0));
    }
    outColor = vec4(color, 1.0);
}
