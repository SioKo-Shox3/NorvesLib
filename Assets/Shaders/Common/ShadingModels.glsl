// GBuffer のシェーディングモデルごとに面・直接光・最終色を補正する。
#ifndef NORVES_SHADING_MODELS_GLSL
#define NORVES_SHADING_MODELS_GLSL

#include "Common/PaleFlatShading.glsl"

struct ShadingModelSurface
{
    uint id;
    float p0;
    float p1;
    vec3 albedo;
    float metallic;
    float roughness;
    vec3 N;
    float ao;
};

/// 光源の評価前に PaleFlat の色・金属度・粗さを設定し、AO は保持する。
void ApplyShadingModelSurface(inout ShadingModelSurface s, vec4 c0, PaleFlatInputs pale)
{
    if (s.id == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        s.albedo = pale.color;
        s.metallic = 0.0;
        s.roughness = 1.0;
    }
}

/// 鏡面の粗さを返す。c0.y は Fur の鏡面の粗さの下限。
float ShadingModelSpecularRoughness(ShadingModelSurface s, vec4 c0)
{
    return s.id == GBUFFER_SHADING_MODEL_FUR ? max(s.roughness, c0.y) : s.roughness;
}

/// 直接光を加算する。c0.x は Fur の wrap、c0.z は Eye の湿り P0 あたりの鏡面増分。
/// PaleFlat は専用の直接光の係数と、独立した鏡面の倍率を使う。
void EvaluateShadingModelDirect(ShadingModelSurface s,
                                vec3 N, vec3 V, vec3 L, vec3 H,
                                vec3 lightRadiance, float NdotLSigned, float shadow,
                                vec2 dfg, vec4 c0, PaleFlatInputs pale,
                                inout vec3 diffuse, inout vec3 specular)
{
    vec3 diffuseBrdf;
    vec3 specularBrdf;
    EvaluateAnalyticalDirectEndpointBRDF(
        s.albedo, s.metallic, ShadingModelSpecularRoughness(s, c0),
        N, V, L, H, dfg, diffuseBrdf, specularBrdf);
    float NdotL = max(NdotLSigned, 0.0);

    if (s.id == GBUFFER_SHADING_MODEL_FUR)
    {
        // 異方性は接線を持つ GR33 の後で扱う。
        float wrap = clamp((NdotLSigned + c0.x) / (1.0 + c0.x), 0.0, 1.0);
        diffuse += diffuseBrdf * lightRadiance * wrap * shadow;
        specular += specularBrdf * lightRadiance * NdotL * shadow;
    }
    else if (s.id == GBUFFER_SHADING_MODEL_EYE)
    {
        diffuse += diffuseBrdf * lightRadiance * NdotL * shadow;
        specular += specularBrdf * (1.0 + s.p0 * c0.z) * lightRadiance * NdotL * shadow;
    }
    else if (s.id == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        diffuse += EvaluateLambertDiffuseBRDF(s.albedo) * lightRadiance *
                   EvaluatePaleFlatDirectFactor(NdotLSigned, shadow, pale);
        specular += specularBrdf * lightRadiance * max(NdotLSigned, 0.0) * shadow * pale.specularScale;
    }
    else
    {
        // 予約の ID は既定の解析の式で評価する。
        diffuse += diffuseBrdf * lightRadiance * NdotL * shadow;
        specular += specularBrdf * lightRadiance * NdotL * shadow;
    }
}

/// 露出前の色を返す。PaleFlat は平均照度と局所の拡散を混ぜ、間接鏡面に倍率を掛ける。
vec3 ShadingModelFinalize(ShadingModelSurface s,
                          vec3 ambient, vec3 directDiffuse, vec3 directSpecular,
                          vec3 flatAmbientIrradiance, vec4 c0, PaleFlatInputs pale,
                          inout vec3 indirectSpecular,
                          inout vec3 indirectSpecularReflectance)
{
    if (s.id == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        vec3 localDiffuse = max(ambient - indirectSpecular, vec3(0.0));
        vec3 uniformDiffuse = s.albedo / PI * flatAmbientIrradiance;
        vec3 diffuseAmbient = mix(uniformDiffuse, localDiffuse, EvaluatePaleFlatAmbientLocalWeight(pale));
        indirectSpecular *= pale.specularScale;
        indirectSpecularReflectance *= pale.specularScale;
        ambient = diffuseAmbient + indirectSpecular;
    }
    return ambient + directDiffuse + directSpecular;
}

#endif
