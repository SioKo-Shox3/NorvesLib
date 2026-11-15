// GBuffer を書く描画シェーダーが共有する濡れの式。
// GBuffer の書き手は環境の UBO（NORVES_DECLARE_ENVIRONMENT_UBO）を読み、GBuffer へ書く前に
// ApplyWetSurfaceFromEnvironment（か ApplyWetSurface）を呼ぶ。
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

// C++ 側の写し: Library/Core/Public/Rendering/EnvironmentGpuParams.h の GPUEnvironmentParams。
// std140 の配置で 48 バイト。
struct EnvironmentGpuParams
{
    vec4 weather;  // x=濡れ（無効なら 0）, y=雨の強さ, z=雲量, w=fmod(ゲーム時刻, 4096)
    vec4 surface;  // x=一様の凹み, y=雨の遮蔽 exposure（既定 1）, z=Seed/2^32, w=不連続の印
    vec4 reserved; // 予約
};

// シェーダーで 1 回だけ宣言し、値は u_Environment.params から読む。
#define NORVES_DECLARE_ENVIRONMENT_UBO(SET_INDEX, BINDING_INDEX) layout(std140, set = SET_INDEX, binding = BINDING_INDEX) uniform EnvironmentParamsBlock { EnvironmentGpuParams params; } u_Environment;

// DefaultLit の P0 = 濡れの感度、未指定（負・NaN）は 1。
// インスタンスの指定を材質より優先する（ResolveShadingParams と同じ優先）。
// Material.a には今までどおり解決した P0 を書き、この値は書き戻さない。
// 上限は ApplyWetSurface が w を 1 に収めるため、ここでは切らない。
float ResolveWetSurfaceResponse(float instanceP0, float materialP0, bool bDefaultLit)
{
    if (!bDefaultLit)
    {
        return 1.0;
    }
    float p = (instanceP0 >= 0.0) ? instanceP0 : materialP0;
    return (p >= 0.0) ? p : 1.0;
}

// GBuffer へ書く直前の値に掛ける。geomN はワールド空間の幾何法線（Y が上）。
void ApplyWetSurfaceFromEnvironment(inout vec3 albedo, inout float roughness, inout float metallic, inout vec3 N,
                                    vec3 geomN, EnvironmentGpuParams env, float response)
{
    ApplyWetSurface(albedo, roughness, metallic, N, geomN, env.surface.x, env.weather.x, env.surface.y, response);
}

#endif
