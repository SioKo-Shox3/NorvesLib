// MegaGeometry のデバッグの表示（クラスタの色・LOD の段）の色。
// megageometry.frag（ラスタ）と visbuffer_resolve（ビジビリティバッファの材質の解決）が同じ色を作る。
// 入力は描画の番号の payload（クラスタの番号、または LOD の段）。

#ifndef NORVES_MEGA_GEOMETRY_DEBUG_COLOR_GLSL
#define NORVES_MEGA_GEOMETRY_DEBUG_COLOR_GLSL

uint HashClusterId(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

vec3 ClusterDebugColor(uint clusterId)
{
    uint hash = HashClusterId(clusterId);
    vec3 color = vec3(float(hash & 255u),
                      float((hash >> 8) & 255u),
                      float((hash >> 16) & 255u)) / 255.0;
    return mix(vec3(0.18), color, 0.82);
}

vec3 LODLevelDebugColor(uint lodLevel)
{
    const vec3 palette[8] = vec3[8](
        vec3(0.10, 0.72, 0.28),
        vec3(0.14, 0.78, 0.70),
        vec3(0.18, 0.42, 0.90),
        vec3(0.42, 0.24, 0.86),
        vec3(0.78, 0.22, 0.78),
        vec3(0.96, 0.72, 0.18),
        vec3(0.95, 0.42, 0.16),
        vec3(0.86, 0.12, 0.12));
    return palette[min(lodLevel, 7u)];
}

#endif // NORVES_MEGA_GEOMETRY_DEBUG_COLOR_GLSL
