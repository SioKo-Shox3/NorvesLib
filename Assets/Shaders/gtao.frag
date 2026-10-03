#version 450

// ========================================
// GTAO（Ground Truth Ambient Occlusion。Jimenez et al. 2016「Practical Realtime Strategies for
// Accurate Indirect Occlusion」）
// ========================================
// 画素ごとに画面上の向きの違う2枚のスライス（視線を含む平面）を取り、各スライスの両側へ数段だけ
// 深度を辿って地平線の角度を求め、法線をスライスへ投影した余弦重みの可視率を解析的に積分する。
// 半径は世界の長さ（m）で与え、半径の外側へ向けて遮蔽の重みを0へ落とすので、部屋の大きさの
// 面（壁全体）は遮蔽にならず、隅・接地部だけが暗くなる。
// 出力は雑音除去の前の可視率（0〜1を少し超えうる。R16F）で、gtao_denoise.frag が4×4の
// 深度を考慮した平均を取り、雑音の4×4の周期を消す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outVisibility;

// GBufferテクスチャ（画素の中心を texelFetch で読む）
layout(set = 0, binding = 0) uniform sampler2D gbufferDepth;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;

layout(set = 0, binding = 2) uniform GTAOParams
{
    mat4 projection;     // ビュー → クリップ（ジッタ込み。GBufferを描いた投影と同じ）
    mat4 invProjection;  // クリップ → ビュー
    mat4 view;           // ワールド → ビュー（法線の変換に使う）
    vec4 screenSize;     // xy=寸法, zw=1/寸法
    vec4 radiusParams;   // x=半径(m), y=減衰を始める距離(m), z=減衰の幅(m), w=画面上の半径の上限(画素)
    vec4 noiseParams;    // x=スライスの向きの時間のずらし, y=段の位置の時間のずらし, z=可視率の指数, w=未使用
} params;

const float PI = 3.14159265358979;
const float HALF_PI = 1.57079632679490;
const int SLICE_COUNT = 2;
const int STEPS_PER_SIDE = 4;
// 中心の画素そのものを読まないよう、最初の段は少なくともこの画素数だけ離す
const float MIN_STEP_PIXELS = 1.3;

vec3 ReconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = params.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// 4×4 のBayer行列（0〜15）。画素の位置で決まる雑音で、雑音除去の4×4の平均で周期が消える。
float Bayer4x4(ivec2 pixel)
{
    const float bayer[16] = float[](
        0.0, 8.0, 2.0, 10.0,
        12.0, 4.0, 14.0, 6.0,
        3.0, 11.0, 1.0, 9.0,
        15.0, 7.0, 13.0, 5.0);
    return bayer[(pixel.y & 3) * 4 + (pixel.x & 3)];
}

// 1方向を辿り、その側の地平線の余弦（視線との内積の最大）を返す
float TraceHorizonCos(vec2 originUV,
                      vec2 directionPixels,
                      vec3 centerPos,
                      vec3 viewDir,
                      float lowHorizonCos,
                      float radiusPixels,
                      float stepNoise,
                      ivec2 depthSize)
{
    float horizonCos = lowHorizonCos;
    float falloffMul = -1.0 / max(params.radiusParams.z, 1.0e-4);
    float falloffAdd = params.radiusParams.y / max(params.radiusParams.z, 1.0e-4) + 1.0;
    for (int stepIndex = 0; stepIndex < STEPS_PER_SIDE; ++stepIndex)
    {
        // 段の位置は2乗で中心寄りに詰め、近い遮蔽（接地部）を細かく拾う
        float t = (float(stepIndex) + stepNoise) / float(STEPS_PER_SIDE);
        t *= t;
        float offsetPixels = MIN_STEP_PIXELS + t * max(radiusPixels - MIN_STEP_PIXELS, 0.0);
        vec2 samplePixel = originUV * vec2(depthSize) + directionPixels * offsetPixels;
        ivec2 sampleTexel = ivec2(floor(samplePixel));
        if (any(lessThan(sampleTexel, ivec2(0))) || any(greaterThanEqual(sampleTexel, depthSize)))
        {
            break;
        }
        float sampleDepth = texelFetch(gbufferDepth, sampleTexel, 0).r;
        // 画素の中心のUVで復元し、平らな面が自分自身を遮らないようにする
        vec2 sampleUV = (vec2(sampleTexel) + 0.5) / vec2(depthSize);
        vec3 samplePos = ReconstructViewPos(sampleUV, sampleDepth);
        vec3 delta = samplePos - centerPos;
        float distance = length(delta);
        if (distance <= 1.0e-6)
        {
            continue;
        }
        float sampleCos = dot(delta / distance, viewDir);
        // 半径の外側ほど遮蔽の重みを0へ落とす（遠い面は地平線を上げない）
        float weight = clamp(distance * falloffMul + falloffAdd, 0.0, 1.0);
        sampleCos = mix(lowHorizonCos, sampleCos, weight);
        horizonCos = max(horizonCos, sampleCos);
    }
    return horizonCos;
}

void main()
{
    ivec2 depthSize = textureSize(gbufferDepth, 0);
    ivec2 centerTexel = clamp(ivec2(fragUV * vec2(depthSize)), ivec2(0), depthSize - 1);
    float depth = texelFetch(gbufferDepth, centerTexel, 0).r;

    // 空（深度=1.0）は遮蔽なし
    if (depth >= 0.9999)
    {
        outVisibility = 1.0;
        return;
    }

    vec2 centerUV = (vec2(centerTexel) + 0.5) / vec2(depthSize);
    vec3 centerPos = ReconstructViewPos(centerUV, depth);
    // 面からカメラへの向き。透視・正射影のどちらでも、同じ画素の近い面の点から求める。
    vec3 viewDir = normalize(ReconstructViewPos(centerUV, 0.0) - centerPos);

    vec3 worldNormal = texelFetch(gbufferNormal, centerTexel, 0).rgb;
    if (dot(worldNormal, worldNormal) < 1.0e-8)
    {
        outVisibility = 1.0;
        return;
    }
    vec3 normal = normalize((params.view * vec4(normalize(worldNormal), 0.0)).xyz);

    // 世界の半径が、この深度で画面に占める画素数
    vec4 clipCenter = params.projection * vec4(centerPos, 1.0);
    vec4 clipOffset = params.projection * vec4(centerPos + vec3(params.radiusParams.x, 0.0, 0.0), 1.0);
    vec2 ndcDelta = clipOffset.xy / clipOffset.w - clipCenter.xy / clipCenter.w;
    float radiusPixels = length(ndcDelta * 0.5 * vec2(depthSize));
    radiusPixels = min(radiusPixels, params.radiusParams.w);
    if (!(radiusPixels >= MIN_STEP_PIXELS))
    {
        outVisibility = 1.0;
        return;
    }

    float sliceNoise = fract((Bayer4x4(centerTexel) + 0.5) / 16.0 + params.noiseParams.x);
    float stepNoise = fract((Bayer4x4(centerTexel + ivec2(2, 1)) + 0.5) / 16.0 + params.noiseParams.y);

    float visibility = 0.0;
    for (int slice = 0; slice < SLICE_COUNT; ++slice)
    {
        float phi = (float(slice) + sliceNoise) * PI / float(SLICE_COUNT);
        vec2 directionPixels = vec2(cos(phi), sin(phi));

        // 画面上の向きをビュー空間の向きへ直す（同じ深度の隣の画素との差）
        vec2 neighborUV = centerUV + directionPixels / vec2(depthSize);
        vec3 directionVec = ReconstructViewPos(neighborUV, depth) - centerPos;
        vec3 orthoDirection = directionVec - dot(directionVec, viewDir) * viewDir;
        float orthoLength = length(orthoDirection);
        if (orthoLength <= 1.0e-8)
        {
            visibility += 1.0;
            continue;
        }
        orthoDirection /= orthoLength;
        vec3 axis = cross(orthoDirection, viewDir);
        vec3 projectedNormal = normal - axis * dot(normal, axis);
        float projectedNormalLength = length(projectedNormal);
        if (projectedNormalLength <= 1.0e-6)
        {
            visibility += 1.0;
            continue;
        }

        float signNormal = dot(orthoDirection, projectedNormal) >= 0.0 ? 1.0 : -1.0;
        float cosNormal = clamp(dot(projectedNormal, viewDir) / projectedNormalLength, 0.0, 1.0);
        float n = signNormal * acos(cosNormal);

        // 接平面より下は数えない（各側の地平線の下限）
        float lowHorizonCosPositive = cos(n + HALF_PI);
        float lowHorizonCosNegative = cos(n - HALF_PI);
        float horizonCosPositive = TraceHorizonCos(centerUV, directionPixels, centerPos, viewDir,
                                                   lowHorizonCosPositive, radiusPixels, stepNoise,
                                                   depthSize);
        float horizonCosNegative = TraceHorizonCos(centerUV, -directionPixels, centerPos, viewDir,
                                                   lowHorizonCosNegative, radiusPixels, stepNoise,
                                                   depthSize);

        float h0 = -acos(clamp(horizonCosNegative, -1.0, 1.0));
        float h1 = acos(clamp(horizonCosPositive, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -HALF_PI, HALF_PI);
        h1 = n + clamp(h1 - n, -HALF_PI, HALF_PI);

        // スライス内の余弦重みの可視率の解析積分
        float sinN = sin(n);
        float arc0 = (cosNormal + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) * 0.25;
        float arc1 = (cosNormal + 2.0 * h1 * sinN - cos(2.0 * h1 - n)) * 0.25;
        visibility += projectedNormalLength * (arc0 + arc1);
    }

    visibility /= float(SLICE_COUNT);
    // 雑音除去の平均が偏らないよう、ここでは上を切らない（下だけ0で止める）
    visibility = max(visibility, 0.0);
    outVisibility = pow(visibility, max(params.noiseParams.z, 1.0e-3));
}
