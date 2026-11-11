#ifndef NORVES_SKY_OCCLUSION_GLSL
#define NORVES_SKY_OCCLUSION_GLSL

// 空の遮蔽の共通評価。資源と配置を引数で受け、呼び出し側の UBO に依存しない。
struct SkyOcclusionResult
{
    float visibility;
    vec3 bentNormal;
    float zenithVisibility;
    vec3 coneAxis;
    float coneVisibility;
};

ivec2 SkyOcclusionAddress(ivec2 worldTexel, ivec2 origin, int resolution)
{
    // 窓の外の隣接点を丸めてから、負のワールド番地も正の剰余へ写す。
    ivec2 bounded = clamp(worldTexel, origin, origin + ivec2(resolution - 1));
    return ((bounded % resolution) + resolution) % resolution;
}

vec4 SampleSkyOcclusionLevel(sampler2DArray map, vec2 worldXZ,
                            ivec2 origin, float texelMeters, int resolution, int level)
{
    vec2 centered = worldXZ / texelMeters - 0.5;
    ivec2 base = ivec2(floor(centered));
    vec2 weight = fract(centered);
    vec4 a = texelFetch(map, ivec3(SkyOcclusionAddress(base, origin, resolution), level), 0);
    vec4 b = texelFetch(map, ivec3(SkyOcclusionAddress(base + ivec2(1, 0), origin, resolution), level), 0);
    vec4 c = texelFetch(map, ivec3(SkyOcclusionAddress(base + ivec2(0, 1), origin, resolution), level), 0);
    vec4 d = texelFetch(map, ivec3(SkyOcclusionAddress(base + ivec2(1, 1), origin, resolution), level), 0);
    return mix(mix(a, b, weight.x), mix(c, d, weight.x), weight.y);
}

vec3 SkyOcclusionNormalizeBent(vec3 bent)
{
    float magnitude = length(bent);
    return magnitude < 1.0e-4 ? vec3(0.0, 1.0, 0.0) : bent / magnitude;
}

SkyOcclusionResult EvaluateSkyOcclusion(vec3 P, vec3 N,
    sampler2DArray visibilityMap, sampler2DArray auxMap,
    uvec4 info, vec4 settings, ivec4 windowOrigin[4], vec4 texelMeters)
{
    SkyOcclusionResult result;
    result.visibility = 1.0;
    result.bentNormal = N;
    result.zenithVisibility = 1.0;
    result.coneAxis = vec3(0.0, 1.0, 0.0);
    result.coneVisibility = 1.0;
    if (info.x == 0u || info.y == 0u || info.z == 0u)
    {
        return result;
    }

    // 粗い段から重ねる。細かい段の窓の外側 20% だけ下の段へ線形に抜ける。
    // 最も粗い段は遮蔽なしへ抜けるため、窓の外でも連続する。
    for (int level = int(min(info.y, 4u)) - 1; level >= 0; --level)
    {
        float spacing = texelMeters[level];
        if (spacing <= 0.0)
        {
            continue;
        }
        ivec2 origin = windowOrigin[level].xy;
        vec2 local = P.xz / spacing - vec2(origin);
        float resolution = float(info.z);
        if (any(lessThan(local, vec2(0.0))) || any(greaterThanEqual(local, vec2(resolution))))
        {
            continue;
        }
        vec2 fromCenter = abs(local - 0.5 * resolution) / (0.5 * resolution);
        float fineWeight = clamp((1.0 - max(fromCenter.x, fromCenter.y)) / 0.2, 0.0, 1.0);
        vec4 coefficients = SampleSkyOcclusionLevel(visibilityMap, P.xz,
            origin, spacing, int(info.z), level);
        vec4 aux = SampleSkyOcclusionLevel(auxMap, P.xz,
            origin, spacing, int(info.z), level);
        float blocked = coefficients.x + (2.0 / 3.0) * dot(coefficients.yzw, N);
        float blockedZenith = coefficients.x + (2.0 / 3.0) * coefficients.z;
        float visibility = pow(clamp(1.0 - settings.x * blocked, 0.0, 1.0), settings.y);
        float zenithVisibility = pow(clamp(1.0 - settings.x * blockedZenith, 0.0, 1.0), settings.y);

        float ground = settings.z + aux.x;
        float height = P.y - ground;
        float canopyFade = aux.w > 0.0 && aux.z > aux.y
            ? smoothstep(aux.y, aux.z, height) : 0.0;
        float belowGroundFade = clamp(-height, 0.0, 1.0);
        float heightFade = max(canopyFade, belowGroundFade);
        visibility = mix(visibility, 1.0, heightFade);
        zenithVisibility = mix(zenithVisibility, 1.0, heightFade);
        vec3 bent = SkyOcclusionNormalizeBent(vec3(-coefficients.y, 0.75 - coefficients.z, -coefficients.w));
        float blockedBent = coefficients.x + (2.0 / 3.0) * dot(coefficients.yzw, bent);
        float coneVisibility = clamp(1.0 - settings.x * blockedBent, 0.0, 1.0);
        coneVisibility = mix(coneVisibility, 1.0, heightFade);
        vec3 coneAxis = SkyOcclusionNormalizeBent(mix(bent, vec3(0.0, 1.0, 0.0), heightFade));
        result.coneAxis = mix(result.coneAxis, coneAxis, fineWeight);
        result.coneVisibility = mix(result.coneVisibility, coneVisibility, fineWeight);
        result.visibility = mix(result.visibility, visibility, fineWeight);
        result.zenithVisibility = mix(result.zenithVisibility, zenithVisibility, fineWeight);
        result.bentNormal = mix(result.bentNormal, bent, fineWeight);
    }
    result.coneAxis = SkyOcclusionNormalizeBent(result.coneAxis);
    result.bentNormal = SkyOcclusionNormalizeBent(result.bentNormal);
    return result;
}

// CPU の正解（SkyOcclusionReference.cpp の EvaluateSkySpecularOcclusionCone）と同じ式。
// 層の数は CPU の SkySpecularOcclusionLobeSampleCount とそろえる。
const int SkySpecularOcclusionLobeSampleCount = 13;

float EvaluateSkySpecularOcclusionCone(vec3 visibleAxis, float visibility, vec3 reflection, float roughness)
{
    float cosineAlpha = sqrt(1.0 - clamp(visibility, 0.0, 1.0));
    float r = clamp(roughness, 0.0, 1.0);
    float ggxAlpha = r * r;
    float ggxAlphaSquared = ggxAlpha * ggxAlpha;
    float cosineDelta = clamp(dot(visibleAxis, reflection), -1.0, 1.0);
    float sineDelta = sqrt(max(0.0, 1.0 - cosineDelta * cosineDelta));
    float weightedVisibility = 0.0;
    float totalWeight = 0.0;
    for (int index = 0; index < SkySpecularOcclusionLobeSampleCount; ++index)
    {
        float xi = (float(index) + 0.5) / float(SkySpecularOcclusionLobeSampleCount);
        float angle = 2.0 * atan(ggxAlpha * sqrt(xi / (1.0 - xi)));
        float cosine = cos(angle);
        float sine = sin(angle);
        float positiveCosine = max(cosine, 0.0);
        float smithDenominator = positiveCosine + sqrt(
            ggxAlphaSquared + (1.0 - ggxAlphaSquared) * positiveCosine * positiveCosine);
        float weight = 2.0 * positiveCosine / max(smithDenominator, 1.0e-6);
        float x = (cosineAlpha - cosine * cosineDelta) / max(sine * sineDelta, 1.0e-6);
        float fraction = acos(clamp(x, -1.0, 1.0)) / 3.14159265358979323846;
        weightedVisibility += weight * fraction;
        totalWeight += weight;
    }
    return clamp(weightedVisibility / max(totalWeight, 1.0e-6), 0.0, 1.0);
}

float EvaluateSkySpecularOcclusion(vec3 P, vec3 N, vec3 R, float roughness,
    sampler2DArray visibilityMap, sampler2DArray auxMap,
    uvec4 info, vec4 settings, ivec4 windowOrigin[4], vec4 texelMeters)
{
    if (info.x == 0u || info.y == 0u || info.z == 0u)
    {
        return 1.0;
    }
    SkyOcclusionResult sky = EvaluateSkyOcclusion(P, N, visibilityMap, auxMap,
        info, settings, windowOrigin, texelMeters);
    float S_occ = EvaluateSkySpecularOcclusionCone(sky.coneAxis, sky.coneVisibility, R, roughness);
    float S_open = EvaluateSkySpecularOcclusionCone(vec3(0.0, 1.0, 0.0), 1.0, R, roughness);
    // 遮蔽物の無い空に対して失った分だけを引き、地平の下の地面の映り込みは変えない。
    return clamp(1.0 - settings.w * max(S_open - S_occ, 0.0), 0.0, 1.0);
}

#endif
