// 太陽の仮想シャドウマップ（VSM。--shadow-method=vsm）の評価。lighting.frag の太陽の影と、計算シェーダーからの影の測定・テストが
// 同じ規則で影を引くための共通の include。
//
// 受け手の段は CPU の SelectVirtualShadowMapLevel と同じ結果になるよう、式を写さず、CPU が二分法で求めた距離のしきい値を使う
// （印付け vsm_mark.comp と同じ。カメラからの直線距離）。各標本は自分の位置のページの表を引いて物理ページの texel を読み、
// 割り当てられていないページ（範囲の外・割り当て前）は粗い段へ順に逃げ、どの段にも無ければ影なしとする。
//
// include する側が、先に Common/VirtualShadowMapParams.glsl を include して VsmSampleParams をメンバに持つ uniform block とページの表・プールの
// storage buffer を宣言し、次のマクロを与える（この include より前に定義する）。
//   VSM_PARAMS               VsmSampleParams の式（std140 の uniform block のメンバ）
//   VSM_PAGE_TABLE(i)        ページの表の語（uint）。i = 段 * 16384 + 番地 y * 128 + 番地 x
//   VSM_POOL(i)              物理ページのプールの語（uint）。i = 物理ページ * 16384 + texel y * 128 + texel x
// 省略できるマクロ:
//   VSM_COUNT_FALLBACK()     自分の段のページが無く、粗い段へ逃げた標本 1 つにつき 1 回呼ぶ（統計の数え上げ。既定は何もしない）
//
// 入口は VsmSampleSunShadow(worldPos, normal, texelMeters)。可視度（1 = 光が当たる、0 = 影）を返し、使った段の texel の一辺（m）を返す。

#ifndef VIRTUAL_SHADOW_MAP_GLSL
#define VIRTUAL_SHADOW_MAP_GLSL

#include "Common/PoissonDisk16.glsl"
#include "Common/VirtualShadowMapParams.glsl"

#ifndef VSM_COUNT_FALLBACK
#define VSM_COUNT_FALLBACK()
#endif

// 1 ページの一辺（texel）と、ページの表の一辺（VirtualShadowMap::PAGE_RESOLUTION・TABLE_DIMENSION と一致）
const uint VSMS_PAGE_RESOLUTION = 128u;
const uint VSMS_TABLE_DIMENSION = 128u;
const uint VSMS_TABLE_ENTRIES_PER_LEVEL = VSMS_TABLE_DIMENSION * VSMS_TABLE_DIMENSION;
const uint VSMS_PAGE_WORDS = VSMS_PAGE_RESOLUTION * VSMS_PAGE_RESOLUTION;
// ページの表の 1 要素の印（VirtualShadowMap::PAGE_ENTRY_* と一致）
const uint VSMS_PAGE_ENTRY_ALLOCATED = 1u << 31;
const uint VSMS_PAGE_INDEX_MASK = (1u << 20) - 1u;

// 法線の向きへのずらしの大きさ（使う段の texel の倍数。面が光に対して斜めなほど大きく、正対すると 0）と、
// 深度の比較の余裕のうち一定の分（使う段の texel の倍数。CSM の 1.5 texel と同じ）
const float VSMS_NORMAL_OFFSET_TEXELS = 1.5;
const float VSMS_CONSTANT_BIAS_TEXELS = 1.5;
// 受け面の深度の傾きの下限（光と面の法線の余弦。CSM の ComputeReceiverDepthGradient と同じ）
const float VSMS_MIN_NORMAL_COSINE = 0.05;

// カメラからの直線距離に対する段（CPU の SelectVirtualShadowMapLevel と同じ結果。d >= しきい値となる k の数）
uint VsmSelectLevel(float distanceToCamera)
{
    uint level = 0u;
    for (uint k = 0u; k + 1u < VSM_PARAMS.control.y; ++k)
    {
        if (distanceToCamera >= VSM_PARAMS.thresholds[k >> 2u][k & 3u])
        {
            ++level;
        }
    }
    return level;
}

// 段 level の、ライト空間の位置 lightXY の texel の深度（m。ライト空間の深度）。
// 位置のページが段の範囲の外、または割り当てられていなければ false
bool VsmFetchDepthMeters(uint level, vec2 lightXY, out float outDepthMeters)
{
    outDepthMeters = 0.0;
    const float pageMeters = VSM_PARAMS.levelInfo[level].x;
    const float texelMeters = VSM_PARAMS.levelInfo[level].y;
    const ivec2 page = ivec2(floor(lightXY / pageMeters));
    const ivec2 origin = VSM_PARAMS.levelOrigin[level].xy;
    const ivec2 count = ivec2(int(VSMS_TABLE_DIMENSION));
    if (any(lessThan(page, origin)) || any(greaterThanEqual(page, origin + count)))
    {
        return false;
    }
    // ページの表の番地はトーラス（絶対のページの座標 mod 128。負でも 0 以上）
    const uint addressX = uint(page.x) & (VSMS_TABLE_DIMENSION - 1u);
    const uint addressY = uint(page.y) & (VSMS_TABLE_DIMENSION - 1u);
    const uint entry = VSM_PAGE_TABLE(level * VSMS_TABLE_ENTRIES_PER_LEVEL + addressY * VSMS_TABLE_DIMENSION + addressX);
    if ((entry & VSMS_PAGE_ENTRY_ALLOCATED) == 0u)
    {
        return false;
    }
    const uint physical = entry & VSMS_PAGE_INDEX_MASK;
    if (physical >= VSM_PARAMS.control.z)
    {
        return false;
    }
    const vec2 local = (lightXY - vec2(page) * pageMeters) / texelMeters;
    const ivec2 texel = clamp(ivec2(floor(local)), ivec2(0), ivec2(int(VSMS_PAGE_RESOLUTION) - 1));
    const uint word = VSM_POOL(physical * VSMS_PAGE_WORDS + uint(texel.y) * VSMS_PAGE_RESOLUTION + uint(texel.x));
    const float depth01 = uintBitsToFloat(word);
    outDepthMeters = (depth01 - 0.5) * VSM_PARAMS.depth.z + VSM_PARAMS.depth.x;
    return true;
}

// 太陽の可視度（1 = 光が当たる、0 = 影）。outTexelMeters は、受け手の距離から選んだ段の texel の一辺（m。逃げた先ではなく、選んだ段）。
// 無効・影の最大の距離の外・不正な値は 1（影なし）。奥の薄めは影の最大の距離の手前の FadeRatio の幅。
float VsmSampleSunShadow(vec3 worldPos, vec3 normal, out float outTexelMeters)
{
    outTexelMeters = 0.0;
    if (VSM_PARAMS.control.x == 0u || VSM_PARAMS.control.y == 0u)
    {
        return 1.0;
    }

    const float distanceToCamera = length(worldPos - VSM_PARAMS.cameraPosition.xyz);
    const float maxDistance = VSM_PARAMS.cameraPosition.w;
    const uint level = VsmSelectLevel(distanceToCamera);
    const float texelMeters = VSM_PARAMS.levelInfo[level].y;
    outTexelMeters = texelMeters;
    if (!(distanceToCamera <= maxDistance) || !(texelMeters > 0.0))
    {
        return 1.0;
    }

    // 法線のライト空間の成分。面が光に斜めなほど（光の向きの成分が小さいほど）法線の向きへずらす
    const float normalLength = length(normal);
    if (!(normalLength > 0.5))
    {
        return 1.0;
    }
    const vec3 unitNormal = normal / normalLength;
    const vec3 lightNormal = vec3(dot(unitNormal, VSM_PARAMS.lightRight.xyz),
                                  dot(unitNormal, VSM_PARAMS.lightUp.xyz),
                                  dot(unitNormal, VSM_PARAMS.lightDirection.xyz));
    const float sineToLight = min(length(lightNormal.xy), 1.0);
    const vec3 offsetWorld = worldPos + unitNormal * (VSMS_NORMAL_OFFSET_TEXELS * texelMeters * sineToLight);

    const vec2 lightXY = vec2(dot(offsetWorld, VSM_PARAMS.lightRight.xyz), dot(offsetWorld, VSM_PARAMS.lightUp.xyz));
    const float receiverDepth = dot(offsetWorld, VSM_PARAMS.lightDirection.xyz);

    // 受け面の深度の傾き（ライト空間の XY 1 m あたりの深度 m）。光に平行に近い面は余弦の下限で抑える
    const float normalDepth = abs(lightNormal.z) < VSMS_MIN_NORMAL_COSINE
        ? (lightNormal.z < 0.0 ? -VSMS_MIN_NORMAL_COSINE : VSMS_MIN_NORMAL_COSINE)
        : lightNormal.z;
    vec2 slope = vec2(-lightNormal.x / normalDepth, -lightNormal.y / normalDepth);
    if (isnan(slope.x) || isinf(slope.x) || isnan(slope.y) || isinf(slope.y))
    {
        slope = vec2(0.0);
    }

    // PCF の半径はワールドで連続な量（段の切り替わりで縁の幅が跳ばない）: 画素の大きさと使う段の 1 texel の大きいほう
    const float radius = max(distanceToCamera * VSM_PARAMS.pixel.x, texelMeters);

    float lit = 0.0;
    for (int index = 0; index < 16; ++index)
    {
        const vec2 offset = POISSON_DISK[index] * radius;
        const vec2 sampleXY = lightXY + offset;
        const float sampleReceiver = receiverDepth + dot(slope, offset);

        // 自分の段から粗い段へ順に引く。どの段にも無ければ影なし
        float visible = 1.0;
        bool bEscaped = false;
        for (uint candidate = level; candidate < VSM_PARAMS.control.y; ++candidate)
        {
            float blockerDepth = 0.0;
            if (VsmFetchDepthMeters(candidate, sampleXY, blockerDepth))
            {
                const float bias = (VSMS_CONSTANT_BIAS_TEXELS + abs(slope.x) + abs(slope.y)) * VSM_PARAMS.levelInfo[candidate].y;
                visible = (sampleReceiver - bias > blockerDepth) ? 0.0 : 1.0;
                break;
            }
            bEscaped = true;
        }
        if (bEscaped)
        {
            VSM_COUNT_FALLBACK();
        }
        lit += visible;
    }
    float shadow = lit / 16.0;

    // 影の最大の距離の手前で影を薄め、境界で急に消えないようにする（CPU の VirtualShadowMapShadowFadeWeight と同じ）
    const float fadeStart = maxDistance * (1.0 - VSM_PARAMS.depth.w);
    shadow = mix(shadow, 1.0, smoothstep(fadeStart, maxDistance, distanceToCamera));
    return shadow;
}

#endif // VIRTUAL_SHADOW_MAP_GLSL
