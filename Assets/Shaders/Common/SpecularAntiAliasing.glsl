// 鏡面の AA（Tokuyoshi-Kaplanyan 型）。GBuffer の書き手は法線の確定直後に呼び、粗さを広げてから書き込む。
// ラスタは dFdxFine(N) / dFdyFine(N)、計算シェーダー（可視バッファの解決）は隣の画素の法線との差を渡す。
#ifndef NORVES_SPECULAR_ANTI_ALIASING_GLSL
#define NORVES_SPECULAR_ANTI_ALIASING_GLSL

const float SPECULAR_AA_MAX_ALPHA_SQUARED_ADD = 0.18;
const float SPECULAR_AA_PIXEL_FILTER_VARIANCE = 0.25;

// 粗さは知覚の粗さ r、式は α = r² の空間で適用する。
// strength は Rendering/SpecularAntiAliasing.h の SpecularAAStrength（UBO で渡る）。
float SpecularAAFilterRoughness(float perceptualRoughness, vec3 normalDx, vec3 normalDy, float strength)
{
    if (strength <= 0.0)
    {
        return perceptualRoughness;
    }

    float variance = strength * SPECULAR_AA_PIXEL_FILTER_VARIANCE
        * (dot(normalDx, normalDx) + dot(normalDy, normalDy));
    // 分散が NaN なら広げず、正の無限大なら上限で頭打ちにする。
    float alphaSquaredAdd = isnan(variance) ? 0.0
        : min(2.0 * variance, SPECULAR_AA_MAX_ALPHA_SQUARED_ADD);
    float alpha = perceptualRoughness * perceptualRoughness;
    return clamp(sqrt(sqrt(alpha * alpha + alphaSquaredAdd)), 0.0, 1.0);
}

// メタリックと AO を保ち、y の知覚の粗さだけを広げる。
vec3 SpecularAAFilterMaterial(vec3 metallicRoughnessAo, vec3 normalDx, vec3 normalDy, float strength)
{
    metallicRoughnessAo.y = SpecularAAFilterRoughness(metallicRoughnessAo.y, normalDx, normalDy, strength);
    return metallicRoughnessAo;
}

#endif
