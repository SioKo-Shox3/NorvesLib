#version 450

layout(location = 0) in vec2 fragUV;

// シーンカラー（HDR）
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// トーンマッピングパラメータ
layout(std140, set = 0, binding = 1) uniform ToneMappingParams
{
    uint operatorType;  // 0:Reinhard, 1:ACES, 2:Uncharted2, 3:Exposure, 4:ACES 2.0 SDR LUT
    uint bBypass;
    uint _pad0;
    // Vignette パラメータ
    float vignetteIntensity;  // 0.0 = off, ~0.3 = subtle
    float vignetteRadius;     // 内側半径 ~0.8
    float vignetteSoftness;   // フォールオフの柔らかさ ~0.5
    float _pad1;
    float _pad2;
    // Color Grading パラメータ
    vec4 colorFilter;         // カラーフィルター (rgb * intensity in w)
    float contrast;           // コントラスト (1.0 = default)
    float saturation;         // 彩度 (1.0 = default)
    float brightness;         // 明度オフセット (0.0 = default)
    float temperature;        // 色温度シフト (-1..+1, 0=neutral)
} params;

// ACES 2.0 SDR 100 nit Rec.709 のベイク3D LUT（display-linear。x=R, y=G, z=B）
layout(set = 0, binding = 2) uniform sampler3D colorLut;

layout(location = 0) out vec4 outColor;

// LUTの shaper（Scripts/BakeAcesOutputLut.py と同じ固定値）
// u = log2(x / 2^-8 + 1) / log2(2^8 / 2^-8 + 1)、x は [0, 256] へ飽和
const uint ACES20_LUT_OPERATOR = 4u;
const float ACES20_LUT_SHAPER_OFFSET = 0.00390625;
const float ACES20_LUT_SHAPER_MAX = 256.0;
const float ACES20_LUT_SHAPER_SPAN = log2(ACES20_LUT_SHAPER_MAX / ACES20_LUT_SHAPER_OFFSET + 1.0);

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
vec3 TonemapExposure(vec3 color)
{
    return vec3(1.0) - exp(-color);
}

// ACES 2.0 SDR（ベイク3D LUTを log2 shaper の座標で三線形補間）
vec3 TonemapAces20Lut(vec3 color)
{
    vec3 clamped = clamp(color, vec3(0.0), vec3(ACES20_LUT_SHAPER_MAX));
    vec3 shaped = clamp(log2(clamped / ACES20_LUT_SHAPER_OFFSET + 1.0) / ACES20_LUT_SHAPER_SPAN, 0.0, 1.0);
    // 格子点 0 と N-1 がテクセル中心に来るよう、[0,1] を半テクセル内側へ写す
    float lutSize = float(textureSize(colorLut, 0).x);
    vec3 lutCoord = (shaped * (lutSize - 1.0) + 0.5) / lutSize;
    return textureLod(colorLut, lutCoord, 0.0).rgb;
}

// ========================================
// Vignette（周辺減光）
// ========================================
float ComputeVignette(vec2 uv, float intensity, float radius, float softness)
{
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(radius - softness, radius, dist);
    return mix(1.0, 1.0 - vignette, intensity);
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
    else if (params.operatorType == ACES20_LUT_OPERATOR)
    {
        mapped = TonemapAces20Lut(hdrColor);
    }
    else
    {
        mapped = TonemapExposure(hdrColor);
    }

    // ToneMappedColor は display-linear のまま presentation へ渡す
    vec3 result = mapped;

    // ========================================
    // Color Grading（display-linear Rec.709空間で適用）
    // ACES 2.0 SDR LUT は表示変換そのものなので、既定のグレーディングを掛けない
    // ========================================
    if (params.operatorType != ACES20_LUT_OPERATOR)
    {
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
    }

    // ========================================
    // Vignette（最終段で適用）
    // ========================================
    float vignette = ComputeVignette(fragUV, params.vignetteIntensity, params.vignetteRadius, params.vignetteSoftness);
    result *= vignette;

    outColor = vec4(result, 1.0);
}
