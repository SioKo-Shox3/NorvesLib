const float PI = 3.14159265358979323846;

struct PbrMaterialTextureSamples
{
    vec4 Albedo;
    vec3 TangentNormal;
    vec3 Material;
};

struct PbrGBufferMaterialSamples
{
    vec4 Albedo;
    vec4 WorldNormal;
    vec4 Material;
};

PbrMaterialTextureSamples SamplePbrMaterialTextures(
    sampler2D albedoSampler,
    sampler2D normalSampler,
    sampler2D metallicSampler,
    sampler2D roughnessSampler,
    sampler2D aoSampler,
    vec2 texCoord)
{
    PbrMaterialTextureSamples result;
    result.Albedo = texture(albedoSampler, texCoord);
    result.TangentNormal = texture(normalSampler, texCoord).rgb * 2.0 - 1.0;
    result.Material = vec3(texture(metallicSampler, texCoord).r,
                            texture(roughnessSampler, texCoord).r,
                            texture(aoSampler, texCoord).r);
    return result;
}

PbrGBufferMaterialSamples SamplePbrMaterialTextures(
    sampler2D albedoSampler,
    sampler2D worldNormalSampler,
    sampler2D packedMaterialSampler,
    vec2 texCoord)
{
    PbrGBufferMaterialSamples result;
    result.Albedo = texture(albedoSampler, texCoord);
    result.WorldNormal = texture(worldNormalSampler, texCoord);
    result.Material = texture(packedMaterialSampler, texCoord);
    return result;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    return ggx1 * ggx2;
}

float GeometrySmithDirect(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

float ComputeSpecularAO(float NdotV, float ao, float roughness)
{
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao,
                 0.0,
                 1.0);
}

void EvaluateAnalyticalDirectEndpointBRDF(vec3 albedo,
                                          float metallic,
                                          float roughness,
                                          vec3 N,
                                          vec3 V,
                                          vec3 L,
                                          vec3 H,
                                          vec2 dfg,
                                          out vec3 diffuseBrdf,
                                          out vec3 specularBrdf)
{
    vec3 F0d = vec3(0.04);
    vec3 F0c = albedo;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float D = DistributionGGX(N, H, roughness);
    float k = ((roughness + 1.0) * (roughness + 1.0)) / 8.0;
    float Gv = NdotV / (NdotV * (1.0 - k) + k);
    float Gl = NdotL / (NdotL * (1.0 - k) + k);
    float G = Gv * Gl;
    float brdfCommon = D * G / (4.0 * NdotV * NdotL + 0.0001);
    vec3 Fd = FresnelSchlick(VdotH, F0d);
    vec3 Fc = FresnelSchlick(VdotH, F0c);
    float Ess = max(dfg.x + dfg.y, 0.0001);
    vec3 CompD = vec3(1.0) + F0d * (1.0 - Ess) / Ess;
    vec3 CompC = vec3(1.0) + F0c * (1.0 - Ess) / Ess;
    vec3 dielectricSpec = brdfCommon * Fd * CompD;
    vec3 conductorSpec = brdfCommon * Fc * CompC;
    vec3 diffuseEndpoint = (1.0 - Fd) * albedo / PI;
    diffuseBrdf = (1.0 - metallic) * diffuseEndpoint;
    specularBrdf = (1.0 - metallic) * dielectricSpec +
                   metallic * conductorSpec;
}

vec3 EvaluateLambertDiffuseBRDF(vec3 albedo)
{
    return albedo / PI;
}

float EvaluateDiffuseMaterialWeight(float metallic)
{
    return clamp(1.0 - metallic, 0.0, 1.0);
}
