// GBuffer と影で同じバインド姿勢の位置・骨の保護・閾値からディゾルブを判定する。
#ifndef NORVES_DISSOLVE_GLSL
#define NORVES_DISSOLVE_GLSL

// 縁の発光の上限[nits]。C++ の DissolveEdgeEmissiveNitsMax（Rendering/MaterialTypes.h）と一致させる。
const float DISSOLVE_EDGE_EMISSIVE_NITS_MAX = 20000.0;

// 格子の座標を 32 ビットの符号なし整数だけで混ぜる。
uint DissolveHash3(uvec3 cell)
{
    uint value = (cell.x * 0x9e3779b9u) ^ (cell.y * 0x85ebca6bu) ^ (cell.z * 0xc2b2ae35u);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float DissolveLatticeValue(ivec3 cell)
{
    return float(DissolveHash3(uvec3(cell)) >> 8u) * (1.0 / 16777216.0);
}

// 格子の 8 隅の値を滑らかな重みで三重線形に補間し、丸めで 1 になることも防ぐ。
float DissolveNoise3(vec3 p)
{
    ivec3 cell = ivec3(floor(p));
    vec3 weight = smoothstep(vec3(0.0), vec3(1.0), fract(p));
    float lowerFront = mix(DissolveLatticeValue(cell),
                           DissolveLatticeValue(cell + ivec3(1, 0, 0)), weight.x);
    float upperFront = mix(DissolveLatticeValue(cell + ivec3(0, 1, 0)),
                           DissolveLatticeValue(cell + ivec3(1, 1, 0)), weight.x);
    float lowerBack = mix(DissolveLatticeValue(cell + ivec3(0, 0, 1)),
                          DissolveLatticeValue(cell + ivec3(1, 0, 1)), weight.x);
    float upperBack = mix(DissolveLatticeValue(cell + ivec3(0, 1, 1)),
                          DissolveLatticeValue(cell + ivec3(1, 1, 1)), weight.x);
    float front = mix(lowerFront, upperFront, weight.y);
    float back = mix(lowerBack, upperBack, weight.y);
    return clamp(mix(front, back, weight.z), 0.0, 1.0 - 1.0 / 16777216.0);
}

// 正の重み全体に占める、保護された骨の重みの割合を返す。
float DissolveProtect(uvec4 boneIndices, vec4 boneWeights, uvec4 protectBones)
{
    float totalWeight = 0.0;
    float protectedWeight = 0.0;
    for (uint influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
    {
        float weight = boneWeights[influenceIndex];
        if (weight > 0.0)
        {
            totalWeight += weight;
            uint boneIndex = boneIndices[influenceIndex];
            if (boneIndex < 128u)
            {
                if (((protectBones[boneIndex >> 5u] >> (boneIndex & 31u)) & 1u) != 0u)
                {
                    protectedWeight += weight;
                }
            }
        }
    }
    return totalWeight > 1e-6 ? clamp(protectedWeight / totalWeight, 0.0, 1.0) : 0.0;
}

// 閾値の両端では、保護の重みにかかわらず完全な本体か完全な破棄にする。
float DissolveMask(vec3 restPosition, float protect, float threshold,
                   vec3 direction, float directionBias, float noiseScale)
{
    if (threshold <= 0.0)
    {
        return 2.0;
    }
    if (threshold >= 1.0)
    {
        return -1.0;
    }
    float n = DissolveNoise3(restPosition * noiseScale);
    float d = clamp(n + dot(restPosition, direction) * directionBias, 0.0, 1.0);
    d = d + clamp(protect, 0.0, 1.0) * (1.0 - d);
    return d - threshold;
}

float DissolveEdge(float mask, float edgeWidth, float threshold)
{
    if (threshold <= 0.0 || threshold >= 1.0 || edgeWidth <= 0.0 || mask < 0.0)
    {
        return 0.0;
    }
    return 1.0 - smoothstep(0.0, edgeWidth, mask);
}

#endif
