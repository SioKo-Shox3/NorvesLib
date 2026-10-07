// 太陽（方向光）のCSMの評価（PCSS）。lighting.frag の照明と、計算シェーダーからの影の測定が
// 同じ規則で影を引くための共通の include。
//
// 影の地図・カスケードの行列・分割の距離・カメラ・検証モードの読み方は、include する側が
// 次のマクロで与える（この include より前に定義する）。
//   SUN_CSM_SHADOW_MAP            カスケードの影の地図（sampler2DArray）
//   SUN_CSM_LIGHT_VIEW(i)         カスケード i のライトビュー行列（mat4）
//   SUN_CSM_LIGHT_PROJECTION(i)   カスケード i のライトプロジェクション行列（mat4）
//   SUN_CSM_SPLIT_DISTANCE(i)     分割 i（0..4）のカメラ前方への距離（float）
//   SUN_CSM_CAMERA_POSITION       カメラ位置（vec3）
//   SUN_CSM_CAMERA_FORWARD        CSM分割に使うカメラ前方ベクトル（vec3、単位でなくてよい）
//   SUN_CSM_ENABLED               影が有効か（bool式）
//   SUN_CSM_CASCADE_COUNT         公開されたカスケード数（uint式。4のときだけ有効）
//   SUN_CSM_HARD_SHADOW_MODE      R5のハードシャドウの検証モードか（bool式。246/247/249）
// 省略できるマクロ:
//   SUN_CSM_SAMPLE_DEPTH(uvw)     影の地図の深度の読み出し（既定は texture(SUN_CSM_SHADOW_MAP, uvw).r。
//                                 計算シェーダーは textureLod で読む定義を与える）
//
// 入口は CalculateShadow(worldPos, normal)。可視度（1=光が当たる、0=影）を返す。

#ifndef SUN_SHADOW_CSM_GLSL
#define SUN_SHADOW_CSM_GLSL

#ifndef SUN_CSM_SAMPLE_DEPTH
#define SUN_CSM_SAMPLE_DEPTH(uvw) texture(SUN_CSM_SHADOW_MAP, (uvw)).r
#endif

// ========================================
// PCSS (Percentage-Closer Soft Shadows)
// ブロッカーサーチ + 可変カーネルPCF
// ========================================

// Poisson disk サンプル（16点）
const vec2 POISSON_DISK[16] = vec2[16](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

// 方向光の角半径（太陽円盤、SkyAtmosphereのSolarDiskSolidAngleSteradiansと同じ大きさ）。半影の幅は
// 遮蔽物から受け側までの距離×tan(角半径)で決まる。
const float DIRECTIONAL_LIGHT_TAN_ANGULAR_RADIUS = 0.00468;
// ブロッカー探索の半径の上限（影の地図のUV）。
const float PCSS_BLOCKER_SEARCH_RADIUS = 0.02;

float GetShadowSplitDistance(uint splitIndex)
{
    return SUN_CSM_SPLIT_DISTANCE(splitIndex);
}

bool IsFiniteShadowValue(float value)
{
    return !isnan(value) && !isinf(value);
}

bool HasValidCascadedShadowData()
{
    if (!(SUN_CSM_ENABLED) ||
        SUN_CSM_CASCADE_COUNT != 4u)
    {
        return false;
    }

    float previousSplit = GetShadowSplitDistance(0u);
    if (!IsFiniteShadowValue(previousSplit))
    {
        return false;
    }
    for (uint splitIndex = 1u; splitIndex < 5u; ++splitIndex)
    {
        float split = GetShadowSplitDistance(splitIndex);
        if (!IsFiniteShadowValue(split) || split <= previousSplit)
        {
            return false;
        }
        previousSplit = split;
    }
    return true;
}

// 受け側の面の、影の標本化UVに対する深度の傾き（ライト空間の法線から求める）。PCSSの探索点ごとに
// 受け側の深度をこの傾きで補い、太陽が低いときに受け側の平らな面そのものを遮蔽物と数えない
// （receiver plane depth bias）。法線が光とほぼ直交する面は傾きを余弦0.05で抑える。
vec2 ComputeReceiverDepthGradient(vec3 normal, uint cascadeIndex)
{
    vec3 lightNormal = mat3(SUN_CSM_LIGHT_VIEW(cascadeIndex)) * normal;
    float normalDepth = abs(lightNormal.z) < 0.05
        ? (lightNormal.z < 0.0 ? -0.05 : 0.05)
        : lightNormal.z;
    mat4 lightProjectionMatrix = SUN_CSM_LIGHT_PROJECTION(cascadeIndex);
    float uPerMeter = 0.5 * lightProjectionMatrix[0][0];
    float vPerMeter = 0.5 * lightProjectionMatrix[1][1];
    float depthPerMeter = lightProjectionMatrix[2][2];
    if (abs(uPerMeter) < 1.0e-8 || abs(vPerMeter) < 1.0e-8 ||
        !IsFiniteShadowValue(depthPerMeter))
    {
        return vec2(0.0);
    }
    vec2 gradient = vec2(depthPerMeter * (-lightNormal.x / normalDepth) / uPerMeter,
                         depthPerMeter * (-lightNormal.y / normalDepth) / vPerMeter);
    return IsFiniteShadowValue(gradient.x) && IsFiniteShadowValue(gradient.y)
        ? gradient
        : vec2(0.0);
}

// 深度の比較の余裕。影の地図の1.5 texel分の世界の長さを深度へ換えた一定の分（曲面と深度の量子化）に、
// 1 texel分の受け側の傾き（texelの中心と受け側の点のずれ）を加える。正規化深度の一定値は
// カスケードの深度範囲が広いほど世界の長さが大きくなり、影が遮蔽物から離れて始まる。
float ComputeShadowCompareBias(vec2 receiverGradient, vec2 texelSize, uint cascadeIndex)
{
    mat4 lightProjectionMatrix = SUN_CSM_LIGHT_PROJECTION(cascadeIndex);
    float depthPerMeter = abs(lightProjectionMatrix[2][2]);
    float uPerMeter = max(0.5 * abs(lightProjectionMatrix[0][0]), 1.0e-8);
    float texelMeters = texelSize.x / uPerMeter;
    return depthPerMeter * texelMeters * 1.5 + dot(abs(receiverGradient), texelSize);
}

// Phase 1: ブロッカーサーチ（平均ブロッカー深度を求める）
float FindBlockerDepth(vec2 shadowUV,
                       float receiverDepth,
                       vec2 receiverGradient,
                       vec2 texelSize,
                       uint cascadeIndex)
{
    float blockerSum = 0.0;
    int blockerCount = 0;
    // カスケードの深度範囲の全体を遮蔽物の距離とした半影を覆う半径（最小2 texel）。
    mat4 lightProjectionMatrix = SUN_CSM_LIGHT_PROJECTION(cascadeIndex);
    float depthRangeMeters = 1.0 / max(abs(lightProjectionMatrix[2][2]), 1.0e-8);
    float searchRadius = clamp(depthRangeMeters * DIRECTIONAL_LIGHT_TAN_ANGULAR_RADIUS *
                                   0.5 * abs(lightProjectionMatrix[0][0]),
                               2.0 * texelSize.x,
                               PCSS_BLOCKER_SEARCH_RADIUS);
    float bias = ComputeShadowCompareBias(receiverGradient, texelSize, cascadeIndex);

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * searchRadius;
        float sampleDepth = SUN_CSM_SAMPLE_DEPTH(vec3(shadowUV + offset, float(cascadeIndex)));
        if (sampleDepth < receiverDepth + dot(receiverGradient, offset) - bias)
        {
            blockerSum += sampleDepth;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
    {
        return -1.0; // ブロッカーなし
    }

    return blockerSum / float(blockerCount);
}

// Phase 2: ペナンブラサイズ推定（影の地図のUVでの半影の半幅）。方向光は平行投影なので、遮蔽物と
// 受け側の深度差（m）×tan(角半径)が半影の半幅（m）になる。
float EstimatePenumbraSize(float receiverDepth, float blockerDepth, uint cascadeIndex)
{
    mat4 lightProjectionMatrix = SUN_CSM_LIGHT_PROJECTION(cascadeIndex);
    float depthPerMeter = max(abs(lightProjectionMatrix[2][2]), 1.0e-8);
    float separationMeters = max(receiverDepth - blockerDepth, 0.0) / depthPerMeter;
    return separationMeters * DIRECTIONAL_LIGHT_TAN_ANGULAR_RADIUS *
           0.5 * abs(lightProjectionMatrix[0][0]);
}

// Phase 3: 可変カーネルPCF
float PCSSFilter(vec2 shadowUV,
                 float receiverDepth,
                 vec2 receiverGradient,
                 vec2 texelSize,
                 float filterRadius,
                 uint cascadeIndex)
{
    float shadow = 0.0;
    float bias = ComputeShadowCompareBias(receiverGradient, texelSize, cascadeIndex);

    for (int i = 0; i < 16; i++)
    {
        vec2 offset = POISSON_DISK[i] * filterRadius;
        float sampleDepth = SUN_CSM_SAMPLE_DEPTH(vec3(shadowUV + offset, float(cascadeIndex)));
        float offsetReceiverDepth = receiverDepth + dot(receiverGradient, offset);
        shadow += (offsetReceiverDepth - bias > sampleDepth) ? 0.0 : 1.0;
    }

    return shadow / 16.0;
}

// 点がカスケードの影の地図のUVと深度の範囲に入るか。境界のブレンドでは、次のカスケードが
// その点を覆うときだけ混ぜる（ブレンド帯は次のカスケードの切片の手前にあり、覆わないと
// 範囲外の「影なし」を混ぜて影が薄くなる）。
bool IsInsideShadowCascade(vec3 worldPos, uint cascadeIndex)
{
    vec4 lightSpacePos = SUN_CSM_LIGHT_PROJECTION(cascadeIndex) *
                         SUN_CSM_LIGHT_VIEW(cascadeIndex) * vec4(worldPos, 1.0);
    if (!IsFiniteShadowValue(lightSpacePos.w) || abs(lightSpacePos.w) < 0.000001)
    {
        return false;
    }
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    return all(greaterThanEqual(shadowUV, vec2(0.0))) &&
           all(lessThanEqual(shadowUV, vec2(1.0))) &&
           projCoords.z >= 0.0 && projCoords.z <= 1.0;
}

float SampleShadowCascade(vec3 worldPos, vec3 normal, uint cascadeIndex)
{
    // ワールド座標をライトクリップ空間に変換
    vec4 lightSpacePos = SUN_CSM_LIGHT_PROJECTION(cascadeIndex) *
                         SUN_CSM_LIGHT_VIEW(cascadeIndex) * vec4(worldPos, 1.0);
    if (!IsFiniteShadowValue(lightSpacePos.w) || abs(lightSpacePos.w) < 0.000001)
    {
        return 1.0;
    }
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // クリップ空間[-1,1] → UV座標[0,1]に変換
    vec2 shadowUV = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    // シャドウマップ範囲外は影なし
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 || shadowUV.y < 0.0 || shadowUV.y > 1.0)
    {
        return 1.0;
    }

    // 深度範囲外も影なし
    if (currentDepth < 0.0 || currentDepth > 1.0)
    {
        return 1.0;
    }

    if (SUN_CSM_HARD_SHADOW_MODE)
    {
        float sampleDepth = SUN_CSM_SAMPLE_DEPTH(vec3(shadowUV, float(cascadeIndex)));
        return currentDepth - 0.005 > sampleDepth ? 0.0 : 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(SUN_CSM_SHADOW_MAP, 0).xy);
    vec2 receiverGradient = ComputeReceiverDepthGradient(normal, cascadeIndex);

    // Phase 1: ブロッカーサーチ
    float avgBlockerDepth = FindBlockerDepth(shadowUV,
                                             currentDepth,
                                             receiverGradient,
                                             texelSize,
                                             cascadeIndex);

    // ブロッカーなし → 完全にライトが当たっている
    if (avgBlockerDepth < 0.0)
    {
        return 1.0;
    }

    // Phase 2: ペナンブラサイズ推定
    float penumbraSize = EstimatePenumbraSize(currentDepth, avgBlockerDepth, cascadeIndex);

    // フィルタ半径をクランプ（最小=1texel, 最大=制限）
    float filterRadius = clamp(penumbraSize, texelSize.x, 0.05);

    // Phase 3: 可変カーネルPCF
    return PCSSFilter(shadowUV, currentDepth, receiverGradient, texelSize, filterRadius,
                      cascadeIndex);
}

float CalculateShadow(vec3 worldPos, vec3 normal)
{
    if (!HasValidCascadedShadowData())
    {
        return 1.0;
    }

    vec3 viewForward = SUN_CSM_CAMERA_FORWARD;
    float forwardLength = length(viewForward);
    if (!IsFiniteShadowValue(forwardLength) || forwardLength <= 0.00001)
    {
        return 1.0;
    }
    viewForward /= forwardLength;
    float receiverDistance = dot(worldPos - SUN_CSM_CAMERA_POSITION, viewForward);
    float nearDistance = GetShadowSplitDistance(0u);
    float farDistance = GetShadowSplitDistance(4u);
    if (!IsFiniteShadowValue(receiverDistance) ||
        receiverDistance < nearDistance || receiverDistance > farDistance)
    {
        return 1.0;
    }

    uint cascadeIndex = 3u;
    for (uint candidate = 0u; candidate < 3u; ++candidate)
    {
        if (receiverDistance < GetShadowSplitDistance(candidate + 1u))
        {
            cascadeIndex = candidate;
            break;
        }
    }

    float shadow = SampleShadowCascade(worldPos, normal, cascadeIndex);
    if (cascadeIndex < 3u)
    {
        float boundary = GetShadowSplitDistance(cascadeIndex + 1u);
        float previousBoundary = GetShadowSplitDistance(cascadeIndex);
        float blendWidth = max((boundary - previousBoundary) * 0.1, 0.001);
        float blendStart = boundary - blendWidth;
        if (receiverDistance > blendStart && IsInsideShadowCascade(worldPos, cascadeIndex + 1u))
        {
            float nextShadow = SampleShadowCascade(worldPos, normal, cascadeIndex + 1u);
            float blend = smoothstep(blendStart, boundary, receiverDistance);
            shadow = mix(shadow, nextShadow, blend);
        }
    }
    else
    {
        // 影の最大距離の手前、最後のカスケードの奥の10%で影を薄め、境界で急に消えないようにする。
        float fadeWidth = max((farDistance - GetShadowSplitDistance(3u)) * 0.1, 0.001);
        shadow = mix(shadow, 1.0, smoothstep(farDistance - fadeWidth, farDistance, receiverDistance));
    }
    return shadow;
}

#endif // SUN_SHADOW_CSM_GLSL
