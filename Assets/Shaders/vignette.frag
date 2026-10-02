#version 450

// トーンマップ後の色へ、放射方向の色収差とビネットを掛ける。
// 色収差は R を外へ・B を内へ、画面中心からの距離に比例してずらす（左右の端で chromaticAberrationPixels 画素）。
// どちらも無効なら入力をそのまま出す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(std140, set = 0, binding = 1) uniform VignetteParams
{
    float intensity;
    float radius;
    float softness;
    uint bEnabled;
    // 画面の左右の端での R・B のずれ（入力の画素数。0で色収差なし）
    float chromaticAberrationPixels;
} params;

float ComputeVignette(vec2 uv, float intensity, float radius, float softness)
{
    vec2 centered = uv - 0.5;
    float dist = length(centered);
    float vignette = smoothstep(radius - softness, radius, dist);
    return mix(1.0, 1.0 - vignette, intensity);
}

vec4 SampleWithChromaticAberration(vec2 uv, float pixels)
{
    vec4 color = texture(inputTexture, uv);
    if (pixels <= 0.0)
    {
        return color;
    }

    // 中心からの UV のずれに一様な倍率を掛けると、画素で見ても縦横同じ割合のずれになる。
    // 倍率は左右の端（中心から UV で0.5）で pixels 画素になるよう、入力の幅で決める。
    vec2 shift = (uv - 0.5) * (2.0 * pixels / float(textureSize(inputTexture, 0).x));
    // R は像を外へ広げる（内側の点を読む）、B は内へ縮める（外側の点を読む）。
    color.r = texture(inputTexture, uv - shift).r;
    color.b = texture(inputTexture, uv + shift).b;
    return color;
}

void main()
{
    if (params.bEnabled == 0u && params.chromaticAberrationPixels <= 0.0)
    {
        outColor = texture(inputTexture, fragUV);
        return;
    }

    vec4 color = SampleWithChromaticAberration(fragUV, params.chromaticAberrationPixels);
    float vignette = params.bEnabled != 0u
                         ? ComputeVignette(fragUV, params.intensity, params.radius, params.softness)
                         : 1.0;
    outColor = vec4(color.rgb * vignette, color.a);
}
