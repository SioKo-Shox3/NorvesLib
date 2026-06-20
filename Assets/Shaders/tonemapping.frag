#version 450

layout(location = 0) in vec2 fragUV;

// シーンカラー（HDR）
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// トーンマッピングパラメータ
layout(std140, set = 0, binding = 1) uniform ToneMappingParams
{
    float exposure;
    float gamma;
    uint operatorType;  // 0:Reinhard, 1:ACES, 2:Uncharted2, 3:Exposure
    uint bBypass;
    // Vignette パラメータ
    float vignetteIntensity;  // 0.0 = off, ~0.3 = subtle
    float vignetteRadius;     // 内側半径 ~0.8
    float vignetteSoftness;   // フォールオフの柔らかさ ~0.5
    float _pad1;
    // Color Grading パラメータ
    vec4 colorFilter;         // カラーフィルター (rgb * intensity in w)
    float contrast;           // コントラスト (1.0 = default)
    float saturation;         // 彩度 (1.0 = default)
    float brightness;         // 明度オフセット (0.0 = default)
    float temperature;        // 色温度シフト (-1..+1, 0=neutral)
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

// ========================================
// Vignette（周辺減光）
// ========================================
float ComputeVignette(vec2 uv, float intensity, float radius, float softness)
{
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(radius, radius - softness, dist);
    return mix(1.0, vignette, intensity);
}

// ========================================
// Color Grading
// ========================================

// 色温度補正（簡易版）
vec3 ApplyTemperature(vec3 color, float temp)
{
    // temp: -1 (cool/blue) to +1 (warm/orange)
    color.r += temp * 0.1;
    color.b -= temp * 0.1;
    return clamp(color, 0.0, 1.0);
}

// コントラスト調整
vec3 ApplyContrast(vec3 color, float contrast)
{
    return clamp((color - 0.5) * contrast + 0.5, 0.0, 1.0);
}

// 彩度調整
vec3 ApplySaturation(vec3 color, float saturation)
{
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3(luma), color, saturation), 0.0, 1.0);
}

void main()
{
    if (params.bBypass != 0u)
    {
        outColor = texture(sceneColor, fragUV);
        return;
    }

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
    vec3 result = pow(mapped, vec3(1.0 / params.gamma));

    // ========================================
    // Color Grading（LDR空間で適用）
    // ========================================
    // カラーフィルター
    result *= params.colorFilter.rgb * params.colorFilter.w;

    // 明度
    result += vec3(params.brightness);

    // コントラスト
    result = ApplyContrast(result, params.contrast);

    // 彩度
    result = ApplySaturation(result, params.saturation);

    // 色温度
    result = ApplyTemperature(result, params.temperature);

    // ========================================
    // Vignette（最終段で適用）
    // ========================================
    float vignette = ComputeVignette(fragUV, params.vignetteIntensity, params.vignetteRadius, params.vignetteSoftness);
    result *= vignette;

    outColor = vec4(result, 1.0);
}
