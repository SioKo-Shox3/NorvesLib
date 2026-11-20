// 濡れによる面の見た目の変化を、材質の種類に依らない式で求める。
// Fur の殻に使い、地面・草・岩も同じ式を使える。w = 0 では入力をビット単位でそのまま返す。
#ifndef NORVES_SURFACE_WETNESS_GLSL
#define NORVES_SURFACE_WETNESS_GLSL

const vec3 SurfaceWetnessLuminanceWeights = vec3(0.2126, 0.7152, 0.0722);

// w は濡れ度（0〜1）。kDark は w = 1 での明るさの倍率（1 で暗くしない）、
// kDesat は w = 1 での彩度低下（0 で変えない）。
vec3 SurfaceWetnessAlbedo(vec3 albedo, float w, float kDark, float kDesat)
{
    if (!(w > 0.0))
    {
        return albedo;
    }
    float lum = dot(albedo, SurfaceWetnessLuminanceWeights);
    vec3 desaturated = albedo + (vec3(lum) - albedo) * (kDesat * w);
    return desaturated * (1.0 + (kDark - 1.0) * w);
}

// w は濡れ度（0〜1）、kWetRough は w = 1 での粗さ。
float SurfaceWetnessRoughness(float roughness, float w, float kWetRough)
{
    if (!(w > 0.0))
    {
        return roughness;
    }
    return roughness + (kWetRough - roughness) * w;
}

#endif // NORVES_SURFACE_WETNESS_GLSL
