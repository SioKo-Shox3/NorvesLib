// PaleFlat の設定を照明の入力へ展開し、直接光と環境光の係数を求める。
// 式は Library/Core/Public/Rendering/PaleFlatShading.h の C++ の参照と同じ。
// 変えるときは両方を直し、PaleFlatShadingContractTest を回す。
#ifndef NORVES_PALE_FLAT_SHADING_GLSL
#define NORVES_PALE_FLAT_SHADING_GLSL

struct PaleFlatInputs
{
    vec3 color;
    float wrapMin;
    float shadowStrength;
    float ambientLocalMix;
    float specularScale;
    float flatness;
    vec3 averageIrradiance;
    bool bAverageIrradianceValid;
};

PaleFlatInputs MakePaleFlatInputs(vec4 colorAndWrapMin, vec4 shape, vec4 averageIrradiance)
{
    PaleFlatInputs p;
    p.color = colorAndWrapMin.rgb;
    p.wrapMin = colorAndWrapMin.w;
    p.shadowStrength = shape.x;
    p.ambientLocalMix = shape.y;
    p.specularScale = shape.z;
    p.flatness = shape.w;
    p.averageIrradiance = averageIrradiance.rgb;
    p.bAverageIrradianceValid = averageIrradiance.w != 0.0;
    return p;
}

// Lambert と、影を弱めた wrap の係数を Flatness で混ぜる。
float EvaluatePaleFlatDirectFactor(float nDotL, float shadow, PaleFlatInputs p)
{
    shadow = clamp(shadow, 0.0, 1.0);
    float lambert = max(nDotL, 0.0) * shadow;
    float wrapped = clamp(nDotL * 0.5 + 0.5, 0.0, 1.0);
    float weakShadow = mix(1.0, shadow, p.shadowStrength);
    float flatFactor = mix(p.wrapMin, 1.0, wrapped * weakShadow);
    return mix(lambert, flatFactor, p.flatness);
}

// 全球の平均照度に対して局所の環境光を混ぜる重みを返す。
float EvaluatePaleFlatAmbientLocalWeight(PaleFlatInputs p)
{
    return mix(1.0, p.ambientLocalMix, p.flatness);
}

#endif
