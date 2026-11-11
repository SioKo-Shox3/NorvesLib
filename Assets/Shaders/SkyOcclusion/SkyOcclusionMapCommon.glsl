// 空の遮蔽クリップマップの配置と、積分で引く段を定める。
#ifndef SKY_OCCLUSION_MAP_COMMON_GLSL
#define SKY_OCCLUSION_MAP_COMMON_GLSL

// SkyOcclusionReference.h と同じ方位数、仰角数、歩数上限、距離を使う。
#define SKY_OCCLUSION_AZIMUTH_COUNT 12u
#define SKY_OCCLUSION_ELEVATION_COUNT 6u
#define SKY_OCCLUSION_HORIZON_MAX_STEP_COUNT 28u
#define SKY_OCCLUSION_HORIZON_FIRST_STEP_METERS 0.5
#define SKY_OCCLUSION_HORIZON_STEP_RATIO 1.4

const float kSkyOcclusionPi = 3.14159265358979323846;
const int kSkyOcclusionTileSize = 8;

layout(std140, set = 0, binding = 0) uniform SkyOcclusionMapParams
{
    uvec4 counts;          // x: 段の数、y: 解像度、z: 作業タイルの数、w: 予約。
    vec4 heights;         // x: 地面高、y: 受け手の高さ、z: 水平線までの最大距離。
    ivec4 windowOrigin[4]; // xy: 各段の窓の最小ワールドテクセル、zw: 予約。
    vec4 texelMeters;     // 各段のテクセル幅。使わない段はゼロ。
} map;

struct SkyOcclusionWorkTile
{
    int tileX;
    int tileZ;
    uint level;
    uint listOffset;
};

ivec2 SkyOcclusionWorldTexel(SkyOcclusionWorkTile tile)
{
    return ivec2(tile.tileX, tile.tileZ) * kSkyOcclusionTileSize +
        ivec2(gl_LocalInvocationID.xy);
}

vec2 SkyOcclusionWorldCenter(ivec2 worldTexel, uint level)
{
    return (vec2(worldTexel) + vec2(0.5)) * map.texelMeters[level];
}

ivec2 SkyOcclusionWorldToTexel(vec2 worldPosition, uint level)
{
    return ivec2(floor(worldPosition / map.texelMeters[level]));
}

bool SkyOcclusionWindowContainsTexel(ivec2 worldTexel, uint level)
{
    ivec2 origin = map.windowOrigin[level].xy;
    return all(greaterThanEqual(worldTexel, origin)) &&
        all(lessThan(worldTexel, origin + ivec2(int(map.counts.y))));
}

bool SkyOcclusionWindowContains(vec2 worldPosition, uint level)
{
    return SkyOcclusionWindowContainsTexel(SkyOcclusionWorldToTexel(worldPosition, level), level);
}

int SkyOcclusionPositiveModulo(int value, int divisor)
{
    int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

ivec3 SkyOcclusionTextureCoordinate(ivec2 worldTexel, uint level)
{
    int resolution = int(map.counts.y);
    return ivec3(SkyOcclusionPositiveModulo(worldTexel.x, resolution),
        SkyOcclusionPositiveModulo(worldTexel.y, resolution), int(level));
}

float SkyOcclusionLevelDistance(uint level)
{
    return float(map.counts.y) * map.texelMeters[level] / 8.0;
}

uint SkyOcclusionSampleLevel(uint receiverLevel, float midpoint, vec2 worldPosition)
{
    uint coarsest = map.counts.x - 1u;
    uint level = receiverLevel;
    // 距離だけで候補を選び、その後で窓への包含を調べる。
    while (level < coarsest && midpoint >= SkyOcclusionLevelDistance(level))
    {
        ++level;
    }
    while (level < coarsest && !SkyOcclusionWindowContains(worldPosition, level))
    {
        ++level;
    }
    return level;
}

ivec3 SkyOcclusionSampleCoordinate(uint receiverLevel, float midpoint, vec2 worldPosition)
{
    uint level = SkyOcclusionSampleLevel(receiverLevel, midpoint, worldPosition);
    ivec2 worldTexel = SkyOcclusionWorldToTexel(worldPosition, level);
    // 最も粗い段の窓の外では、CPU の Sample と同じく端の列を続ける。
    ivec2 origin = map.windowOrigin[level].xy;
    worldTexel = clamp(worldTexel, origin, origin + ivec2(int(map.counts.y) - 1));
    return SkyOcclusionTextureCoordinate(worldTexel, level);
}

#endif
