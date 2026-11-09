// 草の生成で CPU と共有する配置と計算。
#ifndef NORVES_GRASS_COMMON_GLSL
#define NORVES_GRASS_COMMON_GLSL

const uint kGrassLodCount = 3u;
const uint kGrassHashSaltJitterX = 0u;
const uint kGrassHashSaltJitterZ = 1u;
const uint kGrassHashSaltKeep = 2u;
const uint kGrassHashSaltHeight = 3u;
const uint kGrassHashSaltFacing = 4u;
const uint kGrassGenerateFlagHiZ = 1u;

struct GrassTileInfo
{
    float OriginX;
    float OriginZ;
    float GroundY;
    uint Band;
    uint LayerIndex;
    uint Flags;
    int TileX;
    int TileZ;
};

struct GrassBladeRecord
{
    float PositionX;
    float PositionY;
    float PositionZ;
    uint Packed;
};

struct GrassSpeciesGpuData
{
    vec4 RootColor;
    vec4 TipColor;
    vec4 WetColor;
    float HeightMinMeters;
    float HeightMaxMeters;
    float BladeWidthMeters;
    float Bend;
    float Stiffness;
    float Roughness;
    float WetRoughness;
    float WetDroop;
    float WindResponse;
    float TrampleResponse;
    float Opacity;
    float BladesPerSquareMeter;
    vec4 LodDistanceMeters;
    vec4 LodKeepFraction;
};

// GrassGenerateConstants と同じ 480 バイトの std140 配置。
layout(std140, set = 0, binding = 0) uniform GrassGenerateConstants
{
    vec4 CameraPosition;
    vec4 FrustumPlanes[6];
    vec4 BandSpacingMeters;
    vec4 BandMaxDistanceMeters;
    vec4 BandMaxDistanceSquared;
    uvec4 BandCellsPerAxis;
    vec4 BandKeepScale;
    vec4 KeepDistanceSquared;
    vec4 KeepInvSpanSquared;
    vec4 KeepFractions;
    uvec4 LodCapacity;
    uvec4 LodBase;
    uvec4 LodIndexCount;
    uvec4 LodFirstIndex;
    vec4 TileChunkFadeFacing;
    uvec4 TilesPerChunk;
    uvec4 LayerResolutions;
    vec4 SamplingAndFacing;
    vec4 BladeQuantMaxMeters;
    uvec4 Flags;
    mat4 HiZViewProjection;
    uvec4 HiZDimensions;
} constants;

// 引数は各 5 語、カウンタはバイト位置 64 から。予約語も含めて 80 バイト。
layout(std430, set = 0, binding = 6) buffer GrassGenerateArgsBuffer
{
    uint Args[15];
    uint Reserved0;
    uint Counter[3];
    uint Reserved1;
} args;

uint GrassHash(uint x, uint y, uint z)
{
    uint h = (x * 0x8da6b343u) ^ (y * 0xd8163841u) ^ (z * 0xcb1ab31fu);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float GrassHashToUnit(uint h)
{
    precise float value = float(h >> 8) * uintBitsToFloat(0x33800000u);
    return value;
}

float GrassLerp(float a, float b, float fraction)
{
    precise float result = a + (b - a) * fraction;
    return result;
}

int GrassFloorDiv(int value, uint divisor)
{
    // 負数に対する剰余を避け、非負の整数の除算で床除算を求める。
    if (value >= 0)
    {
        return int(uint(value) / divisor);
    }
    uint magnitude = uint(-(value + 1));
    return -int(magnitude / divisor) - 1;
}

float GrassSaturate(float value)
{
    return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}

float ComputeKeepFraction(float distanceSquared)
{
    if (distanceSquared > constants.BandMaxDistanceSquared[2] ||
        distanceSquared > constants.KeepDistanceSquared[3])
    {
        return 0.0;
    }
    if (distanceSquared <= constants.KeepDistanceSquared[0])
    {
        return constants.KeepFractions[0];
    }
    for (uint knot = 1u; knot < 4u; ++knot)
    {
        if (distanceSquared <= constants.KeepDistanceSquared[knot])
        {
            precise float fraction = (distanceSquared - constants.KeepDistanceSquared[knot - 1u]) *
                constants.KeepInvSpanSquared[knot - 1u];
            return GrassLerp(constants.KeepFractions[knot - 1u], constants.KeepFractions[knot], fraction);
        }
    }
    return constants.KeepFractions[3];
}

float ComputeFade(float distance)
{
    precise float end = constants.BandMaxDistanceMeters[2];
    precise float start = constants.TileChunkFadeFacing[2] * end;
    precise float t = GrassSaturate((distance - start) * constants.SamplingAndFacing[3]);
    precise float smoothValue = (t * t) * (3.0 - 2.0 * t);
    return 1.0 - smoothValue;
}

uint PackGrassBlade(float heightMeters, float widthMeters, uint speciesId, float fade,
                    float heightQuantMaxMeters, float widthQuantMaxMeters)
{
    precise float heightValue = GrassSaturate(heightMeters / heightQuantMaxMeters) * 1023.0 + 0.5;
    precise float widthValue = GrassSaturate(widthMeters / widthQuantMaxMeters) * 255.0 + 0.5;
    precise float fadeValue = GrassSaturate(fade) * 255.0 + 0.5;
    uint height10 = uint(heightValue);
    uint width8 = uint(widthValue);
    uint fade8 = uint(fadeValue);
    return height10 | (width8 << 10) | ((speciesId & 63u) << 18) | (fade8 << 24);
}

#endif
