#ifndef NORVES_OVERHEAD_HEIGHT_MAP_GLSL
#define NORVES_OVERHEAD_HEIGHT_MAP_GLSL

// 窓は原点 XZ・範囲・テクセル幅、深度は上端・範囲・解像度・有効フラグ。
struct OverheadHeightMapView
{
    vec4 window;
    vec4 depth;
};

const float OVERHEAD_RAIN_SURFACE_BIAS_METERS = 0.15;
const float OVERHEAD_SKY_COVER_HEIGHT_METERS = 2.0;
const float OVERHEAD_SKY_SAMPLE_RADII_METERS[4] = float[4](0.5, 1.0, 2.0, 4.0);
const float OVERHEAD_SKY_SAMPLE_ANGLE_RADIANS = 2.39996323;

/** @brief 屋根の高さを取得し、該当しない点や無効なマップは開けているとみなす。 */
bool OverheadHeightMapSampleRoofY(sampler2D map, OverheadHeightMapView view, vec2 worldXZ, out float roofY)
{
    roofY = 0.0;
    if (!(view.depth.w > 0.0) || !(view.depth.z > 0.0) ||
        !(view.window.z > 0.0) || !(view.window.w > 0.0))
    {
        return false;
    }

    vec2 offset = worldXZ - view.window.xy;
    vec2 uv = offset / view.window.z;
    if (!all(greaterThanEqual(uv, vec2(0.0))) || !all(lessThan(uv, vec2(1.0))))
    {
        return false;
    }

    // 不一致のある配置でも解像度や実際の画像の外を読まない。
    vec2 texel = floor(offset / view.window.w);
    if (!all(greaterThanEqual(texel, vec2(0.0))) ||
        !all(lessThan(texel, vec2(view.depth.z))) ||
        !all(lessThan(texel, vec2(textureSize(map, 0)))))
    {
        return false;
    }

    float sampleDepth = texelFetch(map, ivec2(texel), 0).r;
    if (!(sampleDepth < 1.0))
    {
        return false;
    }
    roofY = view.depth.x - sampleDepth * view.depth.y;
    return true;
}

/** @brief 雨が当たるかを返し、屋根が該当しない点や無効なマップは開けているとみなす。 */
bool OverheadHeightMapIsExposedToRain(sampler2D map, OverheadHeightMapView view, vec3 worldPos)
{
    float roofY;
    return !OverheadHeightMapSampleRoofY(map, view, worldPos.xz, roofY) ||
        roofY <= worldPos.y + OVERHEAD_RAIN_SURFACE_BIAS_METERS;
}

/** @brief 周囲の空の見え方を返し、標本が該当しない点や無効なマップは開けているとみなす。 */
float OverheadHeightMapSkyVisibility(sampler2D map, OverheadHeightMapView view, vec3 worldPos)
{
    int covered = 0;
    for (int index = 0; index < 4; ++index)
    {
        float angle = float(index) * OVERHEAD_SKY_SAMPLE_ANGLE_RADIANS;
        vec2 sampleXZ = worldPos.xz +
            OVERHEAD_SKY_SAMPLE_RADII_METERS[index] * vec2(cos(angle), sin(angle));
        float roofY;
        if (OverheadHeightMapSampleRoofY(map, view, sampleXZ, roofY) &&
            roofY >= worldPos.y + OVERHEAD_SKY_COVER_HEIGHT_METERS)
        {
            ++covered;
        }
    }
    return 1.0 - float(covered) / 4.0;
}

#endif
