// GBuffer を書く描画シェーダーが共有する濡れの式。
// アルベド・粗さ・金属度・法線へ GBuffer 書き込み前に適用し、最終的な w は GBuffer に書き戻さない。
// Material.a は材質の値のままとする。
// CPU 側の写し: Library/Core/Public/Rendering/WetSurface.h

#ifndef NORVES_WET_SURFACE_GLSL
#define NORVES_WET_SURFACE_GLSL

// 既定値は仮であり、色と明度のルールに合わせて後で調整する。
// 水膜の smoothstep のため poolStart は poolEnd より小さくする。
struct WetSurfaceTuning
{
    float darkenDielectric;
    float darkenMetal;
    float roughnessScale;
    float poolRoughness;
    float poolStart;
    float poolEnd;
    float poolCavityBase;
    float poolCavityScale;
    float poolUpScale;
    float poolUpBias;
};

const WetSurfaceTuning WET_SURFACE_TUNING_DEFAULT = WetSurfaceTuning(
    0.55, 0.9, 0.45, 0.04, 0.55, 0.95, 0.6, 0.4, 4.0, 3.0);

// AI 生成メッシュで高さ・AO がないときは一様の凹みを渡す。
const float WET_SURFACE_DEFAULT_CAVITY = 0.5;

void ApplyWetSurfaceTuned(inout vec3 albedo, inout float roughness, inout float metallic, inout vec3 N,
                          vec3 geomN, float cavity, float wetness, float exposure, float response,
                          WetSurfaceTuning t)
{
    float w = wetness * exposure * response;
    if (!(w > 0.0))
    {
        return;
    }
    w = min(w, 1.0);

    // 金属度は [0,1] に収め、NaN は 0 とする。
    float m = (metallic >= 0.0) ? min(metallic, 1.0) : 0.0;
    float darken = mix(t.darkenDielectric, t.darkenMetal, m);
    albedo *= mix(1.0, darken, w);

    roughness = mix(roughness, roughness * t.roughnessScale, w);

    // 凹みは [0,1] に収め、NaN は 0 とする。
    float c = (cavity >= 0.0) ? min(cavity, 1.0) : 0.0;
    float pool = smoothstep(t.poolStart, t.poolEnd, w * (t.poolCavityBase + t.poolCavityScale * c));
    float up = geomN.y * t.poolUpScale - t.poolUpBias;
    up = (up > 0.0) ? min(up, 1.0) : 0.0;
    pool *= up;
    if (pool > 0.0)
    {
        roughness = mix(roughness, t.poolRoughness, pool);
        vec3 n = mix(N, geomN, pool);
        float lenSq = dot(n, n);
        N = (lenSq > 1e-12) ? n * (1.0 / sqrt(lenSq)) : geomN;
        metallic *= 1.0 - pool;
    }
}

void ApplyWetSurface(inout vec3 albedo, inout float roughness, inout float metallic, inout vec3 N,
                     vec3 geomN, float cavity, float wetness, float exposure, float response)
{
    ApplyWetSurfaceTuned(albedo, roughness, metallic, N, geomN, cavity, wetness, exposure, response,
                         WET_SURFACE_TUNING_DEFAULT);
}

#endif
