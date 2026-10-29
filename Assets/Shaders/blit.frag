#version 450

// ========================================
// Blit (Composite) Fragment Shader
// ========================================
// ToneMappedColorをpresentation surfaceのtransfer責務へ渡す

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D inputTexture;

layout(std140, set = 0, binding = 1) uniform PresentationParams
{
    uint encodePath;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} params;

float EncodeSrgb(float L)
{
    return L <= 0.0031308 ? 12.92 * L : 1.055 * pow(L, 1.0 / 2.4) - 0.055;
}

vec3 EncodeSrgb(vec3 linearColor)
{
    return vec3(EncodeSrgb(linearColor.r), EncodeSrgb(linearColor.g), EncodeSrgb(linearColor.b));
}

vec3 DecodeSrgb(vec3 encodedColor)
{
    vec3 low = encodedColor / 12.92;
    vec3 high = pow((encodedColor + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(encodedColor, vec3(0.04045)));
}

// Jimenez 2014 の interleaved gradient noise。画素の値は [-0.5, 0.5) に一様に分布する。
// 隣の画素との閾値の差は、縦が約 +0.309 / −0.691、横が約 +0.555 / −0.445 に限られる。
// 空の緩い勾配（隣の差が 0.3 段未満）では表示の差が 1 段を超えない。
// 無相関のハッシュでは閾値の差が 1 近くなり、表示に 2 段の差が出た。
// 画素の位置だけで決まり、フレームで変えないので同じ画の撮影は決定的なまま。
float DitherNoise(uvec2 pixel)
{
    return fract(52.9829189 * fract(dot(vec2(pixel), vec2(0.06711056, 0.00583715)))) - 0.5;
}

void main()
{
    vec4 color = texture(inputTexture, fragUV);

    // 8 bit の表示へ丸める前に、sRGB の符号化値の上で ±0.5 段の雑音を足す。値は隣り合う 2 つの段へ
    // 端数の割合で振り分けられ（平均は元の値のまま、元の段からの差は 1 段以下）、雲や空の緩い階調が
    // 同じ値の平らな帯になって等高線のように並ばない。雑音は RGB で共通にして色の粒を出さない。
    vec3 encoded = clamp(EncodeSrgb(max(color.rgb, vec3(0.0))), vec3(0.0), vec3(1.0)) * 255.0;
    vec3 dithered = clamp(encoded + vec3(DitherNoise(uvec2(gl_FragCoord.xy))), vec3(0.0), vec3(255.0));
    if (params.encodePath == 1u)
    {
        color.rgb = round(dithered) / 255.0;
    }
    else
    {
        // sRGB の形式は機器が符号化して最も近い段へ丸めるので、雑音を足した符号化値をリニアへ戻して渡す
        color.rgb = DecodeSrgb(dithered / 255.0);
    }
    outColor = color;
}
