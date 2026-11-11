#ifndef NORVES_WORLD_FIELD_SAMPLE_GLSL
#define NORVES_WORLD_FIELD_SAMPLE_GLSL

// C++ の WorldFieldSampleConstants と同じ std140 の並び。全体は 48 バイト。
struct WorldFieldSampleConstants
{
    vec2 CurrentOriginXZ;
    float CurrentInvExtent;
    uint bEnabled;
    vec2 PreviousOriginXZ;
    float PreviousInvExtent;
    float Padding0;
    float BendScale;
    float FlattenScale;
    vec2 Padding;
};

// 呼び出し側は線形補間・ClampToEdge の sampler を渡す。画像の範囲外では変形を加えない。
vec4 WorldFieldSampleAt(sampler2D field, vec2 originXZ, float invExtent, vec2 worldXZ)
{
    if (invExtent <= 0.0)
    {
        return vec4(0.0);
    }
    vec2 uv = (worldXZ - originXZ) * invExtent;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
    {
        return vec4(0.0);
    }
    return textureLod(field, uv, 0.0);
}

// C++ の WorldFieldDisplacement と同じ式。t=0 の根元は固定し、A は変形に使わない。
vec3 WorldFieldDisplacement(vec4 field, float height, float t, float bendScale, float flattenScale)
{
    float h = max(height, 0.0);
    float s = clamp(t, 0.0, 1.0);
    float bend = h * bendScale * s * s;
    return vec3(field.r * bend, -h * s * flattenScale * clamp(field.b, 0.0, 1.0), field.g * bend);
}

// 根元で引いた値を使い、ずれを変形前の頂点へ加える。前の画像には前の原点・範囲を渡す。
vec3 WorldFieldDisplacementAt(sampler2D field, vec2 originXZ, float invExtent, vec3 rootPosition,
    float height, float t, float bendScale, float flattenScale)
{
    return WorldFieldDisplacement(WorldFieldSampleAt(field, originXZ, invExtent, rootPosition.xz),
        height, t, bendScale, flattenScale);
}

#endif
