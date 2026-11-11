#ifndef NORVES_SKY_OCCLUSION_GLSL
#define NORVES_SKY_OCCLUSION_GLSL

// 空の遮蔽の共通評価。資源と配置を引数で受け、呼び出し側の UBO に依存しない。
struct SkyOcclusionResult
{
    float visibility;
    vec3 bentNormal;
    float zenithVisibility;
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
        result.visibility = mix(result.visibility, visibility, fineWeight);
        result.zenithVisibility = mix(result.zenithVisibility, zenithVisibility, fineWeight);
        result.bentNormal = mix(result.bentNormal, bent, fineWeight);
    }
    result.bentNormal = SkyOcclusionNormalizeBent(result.bentNormal);
    return result;
}

#endif
