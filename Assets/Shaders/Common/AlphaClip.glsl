// GBuffer の切り抜き材質に使う被覆率の判定を定義する。
#ifndef NORVES_ALPHA_CLIP_GLSL
#define NORVES_ALPHA_CLIP_GLSL

// 定数は C++ の GBufferAlphaClip と同じ値に保つ。
const uint ALPHA_CLIP_MODE_NONE = 0u;
const uint ALPHA_CLIP_MODE_TEST = 1u;
const uint ALPHA_CLIP_MODE_HASH = 2u;

float AlphaClipInterleavedGradientNoise(vec2 pixel, uint phase)
{
    vec2 p = pixel + 5.588238 * float(phase & 63u);
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// fragCoord には整数の画素位置を渡す。Hash では cutoff を使わない。
bool AlphaClipShouldDiscard(float coverage, float cutoff, uint mode, vec2 fragCoord, uint phase)
{
    if (mode == ALPHA_CLIP_MODE_NONE)
    {
        return false;
    }
    if (mode == ALPHA_CLIP_MODE_TEST)
    {
        return coverage < cutoff;
    }
    if (mode == ALPHA_CLIP_MODE_HASH)
    {
        float threshold = AlphaClipInterleavedGradientNoise(fragCoord, phase);
        // 閾値が一様なら残存率は coverage になる。等号を含めると 0 は必ず破棄され、1 は残る。
        return coverage <= threshold;
    }
    return false;
}

#endif
