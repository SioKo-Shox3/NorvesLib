#version 450

layout(location = 0) in vec2 fragUV;

// シーンカラー（HDR）
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// トーンマッピングパラメータ
layout(set = 0, binding = 1) uniform ToneMappingParams
{
    float exposure;
    float gamma;
    uint operatorType;  // 0:Reinhard, 1:ACES, 2:Uncharted2, 3:Exposure
    float _padding;
} params;

layout(location = 0) out vec4 outColor;

// ========================================
// トーンマッピングアルゴリズム
// ========================================

// Reinhard（シンプル版）
vec3 TonemapReinhard(vec3 color)
{
    return color / (1.0 + color);
}

// ACES Filmic（映画品質）
vec3 TonemapACES(vec3 color)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Uncharted2 ヘルパー
vec3 Uncharted2Helper(vec3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Uncharted2（ゲームで広く使用）
vec3 TonemapUncharted2(vec3 color)
{
    float W = 11.2; // White point
    vec3 curr = Uncharted2Helper(color);
    vec3 whiteScale = 1.0 / Uncharted2Helper(vec3(W));
    return curr * whiteScale;
}

// 露出ベース（単純なクランプ）
vec3 TonemapExposure(vec3 color, float exposure)
{
    return vec3(1.0) - exp(-color * exposure);
}

void main()
{
    // HDRシーンカラーをサンプリング
    vec3 hdrColor = texture(sceneColor, fragUV).rgb;

    // 露出補正
    hdrColor *= params.exposure;

    // トーンマッピング適用
    vec3 mapped;
    if (params.operatorType == 0u)
    {
        mapped = TonemapReinhard(hdrColor);
    }
    else if (params.operatorType == 1u)
    {
        mapped = TonemapACES(hdrColor);
    }
    else if (params.operatorType == 2u)
    {
        mapped = TonemapUncharted2(hdrColor);
    }
    else
    {
        mapped = TonemapExposure(hdrColor, params.exposure);
    }

    // ガンマ補正
    vec3 gammaCorrected = pow(mapped, vec3(1.0 / params.gamma));

    outColor = vec4(gammaCorrected, 1.0);
}
