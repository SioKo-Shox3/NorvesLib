// GBuffer の書き手が共有する水たまりの式。
// WetSurface.glsl の ApplyWetSurfaceWithPuddle から呼ぶ。
// C++ 側の UBO の写し: Library/Core/Private/Rendering/PuddleSurfaceParams.h

#ifndef NORVES_PUDDLE_GLSL
#define NORVES_PUDDLE_GLSL

#include "Common/OverheadHeightMap.glsl"

// std140 の配置で 48 バイト。
struct PuddleSurfaceGpuParams
{
    vec4 overheadWindow; // xy=窓の原点 XZ m、z=範囲 m、w=テクセル幅 m
    vec4 overheadDepth;  // x=上端 Y、y=範囲 m、z=解像度、w=有効 1/0
    vec4 puddleField;    // xy=原点 XZ m、z=範囲の逆数 1/m、w=有効 1/0
};

#define NORVES_DECLARE_PUDDLE_SURFACE_UBO(SET_INDEX, BINDING_INDEX) layout(std140, set = SET_INDEX, binding = BINDING_INDEX) uniform PuddleSurfaceParamsBlock { PuddleSurfaceGpuParams params; } u_PuddleSurface;

OverheadHeightMapView PuddleOverheadView(PuddleSurfaceGpuParams p)
{
    return OverheadHeightMapView(p.overheadWindow, p.overheadDepth);
}

float PuddleRainExposure(sampler2D overheadMap, PuddleSurfaceGpuParams p, vec3 worldPos)
{
    return OverheadHeightMapIsExposedToRain(overheadMap, PuddleOverheadView(p), worldPos) ? 1.0 : 0.0;
}

float SamplePuddleFieldLayer(sampler2D layer, vec4 puddleField, vec2 worldXZ)
{
    if (!(puddleField.w > 0.5) || !(puddleField.z > 0.0))
    {
        return 0.0;
    }

    vec2 uv = (worldXZ - puddleField.xy) * puddleField.z;
    if (!all(greaterThanEqual(uv, vec2(0.0))) || !all(lessThan(uv, vec2(1.0))))
    {
        return 0.0;
    }

    float edgeDistance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) / puddleField.z;
    float edgeWeight = clamp(edgeDistance / 2.0, 0.0, 1.0);
    return clamp(textureLod(layer, uv, 0.0).r, 0.0, 1.0) * edgeWeight;
}

// 既定値は仮であり、人が見て調整する。
const float PUDDLE_LEVEL_WETNESS_START = 0.3;
const float PUDDLE_MAX_LEVEL = 0.8;
const float PUDDLE_COVERAGE_BAND = 0.1;
const float PUDDLE_UP_START = 0.90;
const float PUDDLE_UP_END = 0.98;
const float PUDDLE_ALBEDO_DARKEN = 0.6;
const float PUDDLE_ROUGHNESS_EDGE = 0.1;
const float PUDDLE_ROUGHNESS_FULL = 0.03;
const float PUDDLE_RIPPLE_CELL_METERS = 0.35;
const float PUDDLE_RIPPLE_MAX_SLOPE = 0.14054;
const float PUDDLE_RIPPLE_MAX_RADIUS_METERS = 0.15;
const float PUDDLE_RIPPLE_WAVELENGTH_METERS = 0.04;
const float PUDDLE_PI = 3.14159265358979323846;

float PuddleWaterLevel(float wetness)
{
    float level = (wetness - PUDDLE_LEVEL_WETNESS_START) / (1.0 - PUDDLE_LEVEL_WETNESS_START);
    return (level >= 0.0) ? min(level, 1.0) : 0.0;
}

float PuddleUpFacing(vec3 geomN)
{
    return (geomN.y >= 0.0) ? smoothstep(PUDDLE_UP_START, PUDDLE_UP_END, geomN.y) : 0.0;
}

float PuddleCoverage(float cavity, float waterLevel, vec3 geomN, float rainExposure)
{
    float c = (cavity >= 0.0) ? min(cavity, 1.0) : 0.0;
    float level = (waterLevel >= 0.0) ? min(waterLevel, 1.0) : 0.0;
    float exposure = (rainExposure >= 0.0) ? min(rainExposure, 1.0) : 0.0;
    float threshold = 1.0 - PUDDLE_MAX_LEVEL * level;
    return smoothstep(threshold, threshold + PUDDLE_COVERAGE_BAND, c) * PuddleUpFacing(geomN) * exposure;
}

// セル座標のビットを混ぜ、負の座標も同じ整数演算で扱う。
uint PuddleHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float PuddleHashUnit(uint value)
{
    return float(PuddleHash(value) >> 8u) * (1.0 / 16777216.0);
}

vec2 PuddleRippleLayerSlope(vec2 worldXZ, float timeSeconds, float gridOffset, uint seed)
{
    vec2 grid = worldXZ / PUDDLE_RIPPLE_CELL_METERS - vec2(gridOffset);
    ivec2 cell = ivec2(floor(grid));
    uint cellHash = PuddleHash(uint(cell.x) ^ (PuddleHash(uint(cell.y)) + seed));
    vec2 center = mix(vec2(0.3), vec2(0.7),
                      vec2(PuddleHashUnit(cellHash), PuddleHashUnit(cellHash + 1u)));
    float period = mix(0.8, 1.6, PuddleHashUnit(cellHash + 2u));
    float phase = PuddleHashUnit(cellHash + 3u);
    float t = fract(timeSeconds / period + phase);
    vec2 fromCenter = (fract(grid) - center) * PUDDLE_RIPPLE_CELL_METERS;
    float distanceToCenter = length(fromCenter);
    float q = (distanceToCenter - t * PUDDLE_RIPPLE_MAX_RADIUS_METERS) / PUDDLE_RIPPLE_WAVELENGTH_METERS;
    if (!(distanceToCenter > 1e-6) || !(abs(q) < 1.0))
    {
        return vec2(0.0);
    }

    // 最大の環はセルの外に届くため、中心から最も近いセルの縁で窓とその微分を 0 にする。
    float cellRadius = min(min(center.x, 1.0 - center.x), min(center.y, 1.0 - center.y)) *
                       PUDDLE_RIPPLE_CELL_METERS;
    float edge = clamp((cellRadius - distanceToCenter) / PUDDLE_RIPPLE_WAVELENGTH_METERS, 0.0, 1.0);
    float cellWindow = edge * edge * (3.0 - 2.0 * edge);
    float cellWindowDerivative = -6.0 * edge * (1.0 - edge) / PUDDLE_RIPPLE_WAVELENGTH_METERS;

    float windowAngle = 0.5 * PUDDLE_PI * q;
    float windowCos = cos(windowAngle);
    float radialWindow = windowCos * windowCos;
    float radialWindowDerivative = -PUDDLE_PI * windowCos * sin(windowAngle) / PUDDLE_RIPPLE_WAVELENGTH_METERS;
    float waveAngle = 2.0 * PUDDLE_PI * q;
    float wave = sin(waveAngle);
    float waveDerivative = 2.0 * PUDDLE_PI * cos(waveAngle) / PUDDLE_RIPPLE_WAVELENGTH_METERS;
    float amplitude = (1.0 - t) * (1.0 - t);
    float heightDerivative = amplitude * (cellWindowDerivative * radialWindow * wave +
        cellWindow * (radialWindowDerivative * wave + radialWindow * waveDerivative));
    return heightDerivative * fromCenter / distanceToCenter;
}

vec2 PuddleRippleSlope(vec2 worldXZ, float timeSeconds)
{
    vec2 slope = PuddleRippleLayerSlope(worldXZ, timeSeconds, 0.0, 0x9e3779b9u) +
                 PuddleRippleLayerSlope(worldXZ, timeSeconds, 0.5, 0x85ebca6bu);
    float slopeLength = length(slope);
    if (slopeLength > PUDDLE_RIPPLE_MAX_SLOPE)
    {
        slope *= PUDDLE_RIPPLE_MAX_SLOPE / slopeLength;
    }
    return slope;
}

void ApplyPuddleSurface(inout vec3 albedo, inout float roughness, inout float metallic, inout vec3 N,
                        vec3 geomN, float coverage, float rippleStrength, vec2 worldXZ, float timeSeconds)
{
    float c = (coverage >= 0.0) ? min(coverage, 1.0) : 0.0;
    if (!(c > 0.0))
    {
        return;
    }

    albedo *= mix(1.0, PUDDLE_ALBEDO_DARKEN, c);
    float targetRoughness = mix(PUDDLE_ROUGHNESS_EDGE, PUDDLE_ROUGHNESS_FULL, c);
    roughness = mix(roughness, targetRoughness, c);
    metallic *= 1.0 - c;

    vec3 n = mix(N, geomN, c);
    float lenSq = dot(n, n);
    n = (lenSq > 1e-12) ? n * inversesqrt(lenSq) : geomN;
    float strength = (rippleStrength >= 0.0) ? min(rippleStrength, 1.0) : 0.0;
    vec2 slope = vec2(0.0);
    if (strength > 0.0)
    {
        slope = PuddleRippleSlope(worldXZ, timeSeconds) * strength;
    }
    N = normalize(n + vec3(-slope.x, 0.0, -slope.y));
}

#endif
