#ifndef NORVES_WORLD_FIELD_GLSL
#define NORVES_WORLD_FIELD_GLSL

// 消費側(草・地表・低木)は SampleDryness だけを呼ぶ。
// 呼ぶ前に WORLD_FIELD_DRYNESS_NEAR_LAYER、WORLD_FIELD_DRYNESS_FAR_LAYER、
// WORLD_FIELD_DRYNESS_CONSTANTS の 3 つのマクロを定義して include する。
// 近傍の範囲は近傍、外は遠景、両方の外は 0。
// 近傍の縁から NearBlendMeters で遠景へ連続に寄せる。

// C++ の WorldFieldDrynessSampleConstants と同じ std140 の並び。全体は 32 バイト。
// NearOriginXZ は 0、NearInvExtent は 8、bEnabled は 12、FarOriginXZ は 16、
// FarInvExtent は 24、NearBlendMeters は 28 バイト目。
struct WorldFieldDrynessSampleConstants
{
    vec2 NearOriginXZ;
    float NearInvExtent;
    uint bEnabled;
    vec2 FarOriginXZ;
    float FarInvExtent;
    float NearBlendMeters;
};

// 呼び出し側は線形補間・ClampToEdge の sampler を渡す。
float SampleDrynessLayers(sampler2D nearLayer, sampler2D farLayer,
    WorldFieldDrynessSampleConstants c, vec2 worldXZ)
{
    if (c.bEnabled == 0u)
    {
        return 0.0;
    }

    vec2 uvF = (worldXZ - c.FarOriginXZ) * c.FarInvExtent;
    float farValue = 0.0;
    if (c.FarInvExtent > 0.0 && all(greaterThanEqual(uvF, vec2(0.0))) &&
        all(lessThan(uvF, vec2(1.0))))
    {
        farValue = textureLod(farLayer, uvF, 0.0).r;
    }

    vec2 uvN = (worldXZ - c.NearOriginXZ) * c.NearInvExtent;
    if (c.NearInvExtent <= 0.0 || any(lessThan(uvN, vec2(0.0))) ||
        any(greaterThanEqual(uvN, vec2(1.0))))
    {
        return farValue;
    }

    float nearValue = textureLod(nearLayer, uvN, 0.0).r;
    float edgeMeters = min(min(uvN.x, 1.0 - uvN.x), min(uvN.y, 1.0 - uvN.y)) / c.NearInvExtent;
    float w = c.NearBlendMeters > 0.0 ? clamp(edgeMeters / c.NearBlendMeters, 0.0, 1.0) : 1.0;
    return mix(farValue, nearValue, w);
}

#if defined(WORLD_FIELD_DRYNESS_NEAR_LAYER) && defined(WORLD_FIELD_DRYNESS_FAR_LAYER) && defined(WORLD_FIELD_DRYNESS_CONSTANTS)
float SampleDryness(vec2 worldXZ)
{
    return SampleDrynessLayers(WORLD_FIELD_DRYNESS_NEAR_LAYER, WORLD_FIELD_DRYNESS_FAR_LAYER,
        WORLD_FIELD_DRYNESS_CONSTANTS, worldXZ);
}
#endif

#endif
