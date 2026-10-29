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

// 画素位置の一次式の端数による格子の雑音で、値は [-0.5, 0.5) に一様に分布する。
// 隣り合う画素の閾値の差は縦が +0.44 / −0.56、横が +0.755 / −0.245 に限られる。
// 隣の表示差は「値の差 + 閾値の差」が ±1 を超えない限り 1 段以下。縦の勾配が上りでも下りでも
// 0.3 段/画素未満の空では隣の差は 1 段を超えない。無相関ハッシュでは閾値の差が 1 に近づき、
// IGN の縦の差 +0.309 / −0.691 では下りの余裕が小さく、どちらも 2 段の差が残った。
// 画素の位置だけで決まりフレームで変えないので、同じ画の撮影は決定的なまま。
float DitherNoise(uvec2 pixel)
{
    return fract(dot(vec2(pixel), vec2(0.7548776662, 0.44))) - 0.5;
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
