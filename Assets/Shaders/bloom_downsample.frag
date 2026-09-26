#version 450

// ========================================
// ブルームの縮小パス（13タップ）
// ========================================
// 入力を半分の解像度へ縮小する。36タップ相当のボックスを13回の双線形サンプルで近似し、
// 中央の箱に0.5、四隅の4つの箱に0.125ずつの重みを付ける。
// 最初の段では、各箱の平均を 1/(1+輝度) で重み付けし（Karis平均）、
// 小さく明るい画素が縮小のたびにちらつくのを抑える。
// しきい値が正のときだけ、最初の段で明るい部分だけを取り出す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// binding 0: 縮小元（最初の段はSceneColor、以降は1つ上の段）
layout(set = 0, binding = 0) uniform sampler2D sourceTexture;

// binding 1: 縮小パラメータ UBO (std140)
layout(set = 0, binding = 1) uniform BloomDownsampleParams
{
    // x = 最初の段なら1（Karis平均と明るさの取り出しを行う）
    // y = しきい値（0以下なら取り出さない）
    // z = ソフトしきい値の膝（0-1）
    // w = 未使用
    vec4 params;
};

float Luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// 非有限値と負値を0にする（1画素のNaNが全段へ広がるのを防ぐ）
vec3 SanitizeColor(vec3 color)
{
    if (any(isnan(color)) || any(isinf(color)))
    {
        return vec3(0.0);
    }
    return max(color, vec3(0.0));
}

vec3 SampleSource(vec2 uv)
{
    return SanitizeColor(texture(sourceTexture, uv).rgb);
}

// ソフトしきい値による明るい部分の取り出し
vec3 ExtractBright(vec3 color, float threshold, float softKnee)
{
    float lum = Luminance(color);
    float knee = threshold * softKnee;
    float soft = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.00001);
    float contribution = max(soft, lum - threshold) / max(lum, 0.00001);
    return color * max(contribution, 0.0);
}

float KarisWeight(vec3 boxAverage)
{
    return 1.0 / (1.0 + Luminance(boxAverage));
}

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(sourceTexture, 0));
    bool bFirstLevel = params.x > 0.5;

    vec3 a = SampleSource(fragUV + texel * vec2(-2.0, -2.0));
    vec3 b = SampleSource(fragUV + texel * vec2( 0.0, -2.0));
    vec3 c = SampleSource(fragUV + texel * vec2( 2.0, -2.0));
    vec3 d = SampleSource(fragUV + texel * vec2(-2.0,  0.0));
    vec3 e = SampleSource(fragUV);
    vec3 f = SampleSource(fragUV + texel * vec2( 2.0,  0.0));
    vec3 g = SampleSource(fragUV + texel * vec2(-2.0,  2.0));
    vec3 h = SampleSource(fragUV + texel * vec2( 0.0,  2.0));
    vec3 i = SampleSource(fragUV + texel * vec2( 2.0,  2.0));
    vec3 j = SampleSource(fragUV + texel * vec2(-1.0, -1.0));
    vec3 k = SampleSource(fragUV + texel * vec2( 1.0, -1.0));
    vec3 l = SampleSource(fragUV + texel * vec2(-1.0,  1.0));
    vec3 m = SampleSource(fragUV + texel * vec2( 1.0,  1.0));

    vec3 boxCenter = (j + k + l + m) * 0.25;
    vec3 boxTopLeft = (a + b + d + e) * 0.25;
    vec3 boxTopRight = (b + c + e + f) * 0.25;
    vec3 boxBottomLeft = (d + e + g + h) * 0.25;
    vec3 boxBottomRight = (e + f + h + i) * 0.25;

    float wCenter = 0.5;
    float wTopLeft = 0.125;
    float wTopRight = 0.125;
    float wBottomLeft = 0.125;
    float wBottomRight = 0.125;

    if (bFirstLevel)
    {
        wCenter *= KarisWeight(boxCenter);
        wTopLeft *= KarisWeight(boxTopLeft);
        wTopRight *= KarisWeight(boxTopRight);
        wBottomLeft *= KarisWeight(boxBottomLeft);
        wBottomRight *= KarisWeight(boxBottomRight);
    }

    float weightSum = wCenter + wTopLeft + wTopRight + wBottomLeft + wBottomRight;
    vec3 result = (boxCenter * wCenter +
                   boxTopLeft * wTopLeft +
                   boxTopRight * wTopRight +
                   boxBottomLeft * wBottomLeft +
                   boxBottomRight * wBottomRight) / max(weightSum, 0.00001);

    if (bFirstLevel && params.y > 0.0)
    {
        result = ExtractBright(result, params.y, params.z);
    }

    outColor = vec4(result, 1.0);
}
