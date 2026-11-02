#ifndef NORVES_MESH_PARTICLE_DISSOLVE_GLSL
#define NORVES_MESH_PARTICLE_DISSOLVE_GLSL

const uint MESH_PARTICLE_DISSOLVE_DISCARD = 0u;
const uint MESH_PARTICLE_DISSOLVE_EDGE = 1u;
const uint MESH_PARTICLE_DISSOLVE_BODY = 2u;

// 格子の座標を 32 ビットの符号なし整数だけで混ぜる。
uint MeshParticleDissolveHash(uvec2 cell)
{
    uint value = (cell.x * 0x9e3779b9u) ^ (cell.y * 0x85ebca6bu);
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

// 上位 24 ビットを正確に表せる浮動小数点数へ変換し、[0, 1) に収める。
float MeshParticleDissolveLatticeValue(ivec2 cell)
{
    return float(MeshParticleDissolveHash(uvec2(cell)) >> 8u) * (1.0 / 16777216.0);
}

// UV の単位区間を 16 × 16 に分け、格子の値を滑らかに補間する。
float MeshParticleDissolveNoise(vec2 uv)
{
    vec2 position = uv * 16.0;
    ivec2 cell = ivec2(floor(position));
    vec2 weight = smoothstep(vec2(0.0), vec2(1.0), fract(position));
    float lower = mix(MeshParticleDissolveLatticeValue(cell),
                      MeshParticleDissolveLatticeValue(cell + ivec2(1, 0)), weight.x);
    float upper = mix(MeshParticleDissolveLatticeValue(cell + ivec2(0, 1)),
                      MeshParticleDissolveLatticeValue(cell + ivec2(1, 1)), weight.x);
    // 補間の丸めによって 1 になることも防ぐ。
    return clamp(mix(lower, upper, weight.y), 0.0, 1.0 - 1.0 / 16777216.0);
}

// 閾値の両端では縁を出さず、完全な本体または完全な破棄にする。
uint MeshParticleDissolveClassify(float noise, float threshold, float edgeWidth)
{
    if (threshold <= 0.0)
    {
        return MESH_PARTICLE_DISSOLVE_BODY;
    }
    if (threshold >= 1.0)
    {
        return MESH_PARTICLE_DISSOLVE_DISCARD;
    }
    if (noise < threshold)
    {
        return MESH_PARTICLE_DISSOLVE_DISCARD;
    }
    if (noise < threshold + edgeWidth)
    {
        return MESH_PARTICLE_DISSOLVE_EDGE;
    }
    return MESH_PARTICLE_DISSOLVE_BODY;
}

#endif
