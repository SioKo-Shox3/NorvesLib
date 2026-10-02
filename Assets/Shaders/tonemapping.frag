#version 450

layout(location = 0) in vec2 fragUV;

// シーンカラー（HDR）
layout(set = 0, binding = 0) uniform sampler2D sceneColor;

// トーンマッピングパラメータ
layout(std140, set = 0, binding = 1) uniform ToneMappingParams
{
    uint operatorType;  // 0:Reinhard, 1:ACES, 2:Uncharted2, 3:Exposure, 4:ACES 2.0 SDR LUT
    uint bBypass;
    uint filmGrainSeed;       // フィルムグレインのフレームごとのseed
    // Vignette パラメータ
    float vignetteIntensity;  // 0.0 = off, ~0.3 = subtle
    float vignetteRadius;     // 内側半径 ~0.8
    float vignetteSoftness;   // フォールオフの柔らかさ ~0.5
    float filmGrainStrength;  // フィルムグレインの強さ（sRGBの符号化値での標準偏差。0でオフ）
    uint gradingMode;         // 0: View の設定の式、1: カメラの差し替えの式（知覚的なS字コントラスト・輝度を保つ色温度）
    // Color Grading パラメータ
    vec4 colorFilter;         // カラーフィルター (rgb * intensity in w)
    float contrast;           // コントラスト (1.0 = default)
    float saturation;         // 彩度 (1.0 = default)
    float brightness;         // 明度オフセット (0.0 = default)
    float temperature;        // 色温度シフト (-1..+1, 0=neutral)
    float contrastPivot;      // カメラの差し替えの式のコントラストの軸（表示のリニア値）
    float lookLutIntensity;   // 見た目の3D LUTの混ぜ具合（0で掛けない）
    float _pad4;
    float _pad5;
} params;

// ACES 2.0 SDR 100 nit Rec.709 のベイク3D LUT（display-linear。x=R, y=G, z=B）
layout(set = 0, binding = 2) uniform sampler3D colorLut;

// グレーディング用の見た目の3D LUT（sRGBの符号化値で引き、格子点の座標からの符号化値の差分を返す。x=R, y=G, z=B）
layout(set = 0, binding = 3) uniform sampler3D lookLut;

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

// 表示の知覚的な値（2.2乗の逆）の上で、軸（表示のリニア値で指定）と0・1を動かさずに軸の周りを立てる
// S字のコントラスト。軸より下は軸へ向かう冪、上は1へ向かう冪で、0.5中心の線形のコントラストと違い、
// 暗部を黒へ切らず、明部を白へ飛ばさない。
vec3 ApplyContrastPerceptual(vec3 color, float contrast, float pivotLinear)
{
    float pivot = clamp(pow(clamp(pivotLinear, 0.0, 1.0), 1.0 / 2.2), 0.01, 0.99);
    vec3 x = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    vec3 lower = pivot * pow(x / pivot, vec3(contrast));
    vec3 upper = 1.0 - (1.0 - pivot) * pow(max(1.0 - x, 0.0) / (1.0 - pivot), vec3(contrast));
    vec3 y = mix(lower, upper, step(vec3(pivot), x));
    return pow(clamp(y, 0.0, 1.0), vec3(2.2));
}

// 輝度を保ってR・Bの倍率を変える色温度（正で暖色。1あたりR・Bを±10%）。黒は黒のまま。
vec3 ApplyWhiteBalance(vec3 color, float temp)
{
    vec3 gain = vec3(1.0 + temp * 0.1, 1.0, 1.0 - temp * 0.1);
    gain /= dot(gain, vec3(0.2126, 0.7152, 0.0722));
    return clamp(color * gain, 0.0, 1.0);
}

// フィルムグレイン: 画素・フレームごとのseedから決まる、平均0・分散1の三角分布の雑音（PCGのhash）。
uint FilmGrainHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float FilmGrainNoise(uvec2 pixel, uint frameSeed)
{
    uint first = FilmGrainHash(pixel.x ^ FilmGrainHash(pixel.y ^ frameSeed));
    uint second = FilmGrainHash(first);
    float u0 = float(first >> 8u) * (1.0 / 16777216.0);
    float u1 = float(second >> 8u) * (1.0 / 16777216.0);
    // 2つの一様乱数の和から1を引くと平均0・分散1/6の三角分布になる
    return (u0 + u1 - 1.0) * sqrt(6.0);
}

vec3 EncodeSrgb(vec3 linearColor)
{
    vec3 low = linearColor * 12.92;
    vec3 high = 1.055 * pow(linearColor, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(linearColor, vec3(0.0031308)));
}

vec3 DecodeSrgb(vec3 encodedColor)
{
    vec3 low = encodedColor / 12.92;
    vec3 high = pow((encodedColor + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(encodedColor, vec3(0.04045)));
}

// sRGBの符号化値へ強さ（標準偏差）の雑音を足してdisplay-linearへ戻す。表示の符号化の上で平均を変えない。
vec3 ApplyFilmGrain(vec3 displayLinear, uvec2 pixel, uint frameSeed, float strength)
{
    float noise = FilmGrainNoise(pixel, frameSeed) * strength;
    vec3 encoded = EncodeSrgb(clamp(displayLinear, vec3(0.0), vec3(1.0)));
    return DecodeSrgb(clamp(encoded + vec3(noise), vec3(0.0), vec3(1.0)));
}

// 見た目の3D LUT: 表示のリニア値をsRGBの符号化値へ写し、格子点 0 と N-1 がテクセル中心に来るよう
// 半テクセル内側の座標で三線形補間する。LUTは格子点の座標からの差分（符号化値）を持つので、
// 補間した差分を足した符号化値と元の符号化値をそれぞれリニアへ戻し、その差を入力へ足す。
// 恒等のLUTは差分がすべて0なので、補間の精度に関わらず入力をビット単位でそのまま返す。
// precise は2つの DecodeSrgb の演算の融合を揃え、差分0のときの差を厳密に0にするため。
vec3 ApplyLookLut(vec3 displayLinear, float intensity)
{
    vec3 encoded = clamp(EncodeSrgb(clamp(displayLinear, vec3(0.0), vec3(1.0))), vec3(0.0), vec3(1.0));
    float lutSize = float(textureSize(lookLut, 0).x);
    vec3 lutCoord = (encoded * (lutSize - 1.0) + 0.5) / lutSize;
    vec3 offset = textureLod(lookLut, lutCoord, 0.0).rgb;
    precise vec3 looked = DecodeSrgb(clamp(encoded + offset, vec3(0.0), vec3(1.0)));
    precise vec3 original = DecodeSrgb(encoded);
    precise vec3 delta = looked - original;
    return displayLinear + intensity * delta;
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

        if (params.gradingMode == 1u)
        {
            // カメラの差し替え: 知覚的なS字のコントラスト、彩度、輝度を保つ色温度
            result = ApplyContrastPerceptual(result, params.contrast, params.contrastPivot);
            result = ApplySaturation(result, params.saturation);
            result = ApplyWhiteBalance(result, params.temperature);
        }
        else
        {
            // コントラスト
            result = ApplyContrast(result, params.contrast);

            // 彩度
            result = ApplySaturation(result, params.saturation);

            // 色温度
            result = ApplyTemperature(result, params.temperature);
        }

        // 見た目の3D LUT（グレーディングの後、ビネットの前）
        if (params.lookLutIntensity > 0.0)
        {
            result = ApplyLookLut(result, params.lookLutIntensity);
        }
    }

    // ========================================
    // Vignette（最終段で適用）
    // ========================================
    float vignette = ComputeVignette(fragUV, params.vignetteIntensity, params.vignetteRadius, params.vignetteSoftness);
    result *= vignette;

    // ========================================
    // フィルムグレイン（出力変換の後、display空間。既定はオフ）
    // ========================================
    if (params.filmGrainStrength > 0.0)
    {
        result = ApplyFilmGrain(result, uvec2(gl_FragCoord.xy), params.filmGrainSeed, params.filmGrainStrength);
    }

    outColor = vec4(result, 1.0);
}
