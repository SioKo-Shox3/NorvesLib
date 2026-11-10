// GBuffer のシェーディングモデルごとに面・直接光・最終色を補正する。
#ifndef NORVES_SHADING_MODELS_GLSL
#define NORVES_SHADING_MODELS_GLSL

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

/// 光源の評価前に面を補正する。c1.z は輝度へ寄せる割合、c1.x は明暗差を落とす割合。
void ApplyShadingModelSurface(inout ShadingModelSurface s, vec4 c0, vec4 c1)
{
    if (s.id == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        float luminance = dot(s.albedo, vec3(0.2126, 0.7152, 0.0722));
        s.albedo = mix(s.albedo, vec3(luminance), c1.z);
        s.metallic = 0.0;
        s.roughness = 1.0;
        s.ao = mix(s.ao, 1.0, c1.x);
    }
}

/// 鏡面の粗さを返す。c0.y は Fur の鏡面の粗さの下限。
float ShadingModelSpecularRoughness(ShadingModelSurface s, vec4 c0)
{
    return s.id == GBUFFER_SHADING_MODEL_FUR ? max(s.roughness, c0.y) : s.roughness;
}

/// 直接光を加算する。c0.x は Fur の wrap、c0.z は Eye の湿り P0 あたりの鏡面増分。
/// c1.x は PaleFlat の明暗差を落とす割合、c1.y は平らな直接光の n·l と影の代替値。
void EvaluateShadingModelDirect(ShadingModelSurface s,
                                vec3 N, vec3 V, vec3 L, vec3 H,
                                vec3 lightRadiance, float NdotLSigned, float shadow,
                                vec2 dfg, vec4 c0, vec4 c1,
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
                   mix(NdotL * shadow, c1.y, c1.x);
        specular += specularBrdf * lightRadiance * NdotL * shadow * (1.0 - c1.x);
    }
    else
    {
        // 予約の ID は既定の解析の式で評価する。
        diffuse += diffuseBrdf * lightRadiance * NdotL * shadow;
        specular += specularBrdf * lightRadiance * NdotL * shadow;
    }
}

/// 露出前の色を返す。c1.x は PaleFlat の環境光を平らにし、間接鏡面を減らす割合。
vec3 ShadingModelFinalize(ShadingModelSurface s,
                          vec3 ambient, vec3 directDiffuse, vec3 directSpecular,
                          vec3 flatAmbientIrradiance, vec4 c0, vec4 c1,
                          inout vec3 indirectSpecular,
                          inout vec3 indirectSpecularReflectance)
{
    if (s.id == GBUFFER_SHADING_MODEL_PALE_FLAT)
    {
        ambient = mix(ambient, s.albedo / PI * flatAmbientIrradiance, c1.x);
        indirectSpecular *= 1.0 - c1.x;
        indirectSpecularReflectance *= 1.0 - c1.x;
    }
    return ambient + directDiffuse + directSpecular;
}

#endif
