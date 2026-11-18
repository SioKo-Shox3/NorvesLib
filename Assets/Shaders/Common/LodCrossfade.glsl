// 段の遷移（クロスフェード）の画面ドアのディザ。C++ の MeshTypes.h の MakeLODCrossfadeValue・PackLODCrossfadeIntoTint と同じ約束。
// 値0 = 遷移なし、正f = 入ってくる側で p < f を残す、負-f = 出ていく側で p >= f を残す。同じ画素で同じpなので被覆は相補。
#ifndef NORVES_LOD_CROSSFADE_GLSL
#define NORVES_LOD_CROSSFADE_GLSL

// 整数の画素位置（floor(gl_FragCoord.xy)）から4×4 Bayerのしきい値（b + 0.5）/ 16を返す。
float LodCrossfadeDitherValue(vec2 pixel)
{
    const uint bayer[16] = uint[16](0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u,
                                  3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
    uint x = uint(pixel.x);
    uint y = uint(pixel.y);
    uint index = (y & 3u) * 4u + (x & 3u);
    return (float(bayer[index]) + 0.5) / 16.0;
}

// 符号付きフェード値で画素を捨てるか返す。入る側と出る側は同じしきい値で相補になる。
bool LodCrossfadeShouldDiscard(float signedFade, vec2 pixel)
{
    float p = LodCrossfadeDitherValue(pixel);
    if (signedFade > 0.0)
    {
        return p >= signedFade;
    }
    if (signedFade < 0.0)
    {
        return p < -signedFade;
    }
    return false;
}

// C++ の DecodeLODCrossfadeFromTint と同じくRGBA8のAを読む。予約値0・127・128・255は遷移なし。
float LodCrossfadeFromTint(uint tint)
{
    uint alpha = tint >> 24u;
    if (alpha >= 1u && alpha <= 126u)
    {
        return float(alpha) / 127.0;
    }
    if (alpha >= 129u && alpha <= 254u)
    {
        return -float(alpha - 128u) / 127.0;
    }
    return 0.0;
}

#endif
