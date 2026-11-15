// GBuffer の切り抜き材質に使う被覆率の判定を定義する。
#ifndef NORVES_ALPHA_CLIP_GLSL
#define NORVES_ALPHA_CLIP_GLSL

// 定数は C++ の GBufferAlphaClip と同じ値に保つ。
const uint ALPHA_CLIP_MODE_NONE = 0u;
const uint ALPHA_CLIP_MODE_TEST = 1u;
const uint ALPHA_CLIP_MODE_HASH = 2u;

// C++ の GBufferAlphaHashPhaseCount と同じ値。TAA のジッタの周期。
const uint ALPHA_CLIP_HASH_PHASE_COUNT = 8u;

float AlphaClipInterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// fragCoord には整数の画素位置を渡す。Hash では cutoff を使わない。
// 位相には TAA のジッタの番号を使い、同じ画素の閾値を 8 フレームで 1/8 の等間隔に並べる。
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
        float n = AlphaClipInterleavedGradientNoise(fragCoord);
        float threshold = fract(n + float(phase % ALPHA_CLIP_HASH_PHASE_COUNT) / float(ALPHA_CLIP_HASH_PHASE_COUNT));
        // 閾値が一様なら残存率は coverage になる。等号を含めると 0 は必ず破棄され、1 は残る。
        return coverage <= threshold;
    }
    return false;
}

#endif
