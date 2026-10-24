// 雲の層の式。cloud_layer.frag（描画）と cloud_layer_probe.frag（撮影の雲の書き出し --cloud-dump）が共有する。
// 惑星の球殻にした雲の層を視線で進め、3D ノイズの密度から太陽の散乱光（前方・後方の 2 つの位相と多重散乱の近似）と
// 空の環境光を積分し、雲までの区間の大気の空気遠近（地面と同じ高度ごとの表）を掛ける。定数の並び・式を変えたら、書き出しの契約（Docs/RenderingValidation/WastelandCloudDump.md）と
// Scripts/Terrain/CloudDumpCheck.py の再計算も合わせて直す。

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;

// 空気遠近の高度ごとの表の段の数（SkyAerialProfileSize。VolumetricsCommon.glsl の kAerialProfileSize と同じ）
const uint kCloudAerialProfileSize = 128u;

layout(std140, set = 0, binding = 2) uniform CloudLayerParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndPreExposure;
    vec4 sunDirectionAndFrameIndex;      // xyz=太陽へ向かう向き, w=時間方向の雑音の位相
    vec4 sunIlluminance;                 // rgb=太陽の地表の照度(lux)
    vec4 layerAltitudesAndCoverage;      // x=惑星の半径, y=雲の底, z=雲の頂(m), w=空を覆う割合
    vec4 extinctionAndInverseScales;     // x=消散係数(1/m), y=基本の形, z=細部, w=天気の模様の 1/周期(1/m)
    vec4 shapeAndWeatherOffset;          // xy=基本の形の風のずれ(m), zw=天気の模様(大きい方)のテクスチャ座標のずれ(0〜1)
    vec4 detailOffset;                   // xyz=細部の風のずれ(m)
    vec4 groundAlbedo;
    vec4 weatherHighOffset;              // xy=天気の模様(細かい方)のテクスチャ座標のずれ(0〜1)
    // 雲までの区間の空気遠近（VolumetricsPass が地面に掛けるのと同じ空の媒質と表）
    vec4 aerialPerspective;              // x=有効なら 1, y=距離の倍率, z=掛け始める距離(m), w=ワールドの y = 0 の海面からの高度(m)
    vec4 rayleighExtinctionAndScaleHeight; // rgb=高度 0 の消散係数(1/m), w=スケールハイト(m)
    vec4 mieExtinctionAndScattering;     // x=高度 0 の消散係数(1/m), y=スケールハイト(m), z=高度 0 の散乱係数(1/m), w=位相の異方性
    vec4 aerialProfileRange;             // x=高度ごとの表のいちばん上の段の海面からの高度(m)
    // 高度ごとの表: [i] = 太陽の照度 × 透過率(lux), [kCloudAerialProfileSize + i] = 太陽の照度 × 多重散乱の表 Ψ_ms(cd/m²)
    vec4 aerialProfile[2u * kCloudAerialProfileSize];
} params;

// 基本の形: R=Perlin-Worley, G・B・A=反転 Worley の fbm（低い周波数から）。細部: R・G・B=反転 Worley の fbm
layout(set = 0, binding = 3) uniform sampler3D shapeNoise;
layout(set = 0, binding = 4) uniform sampler3D detailNoise;

const float PI = 3.14159265359;
// 視線は雲の層の球殻に入る所から出る所まで進める（固定の距離で打ち切らない）。遠い雲は雲までの区間の空気遠近
// （IntegrateCloudAerial）で地平線の霞に溶けるので、距離で溶かす割合も掛けない
const int kMinPrimarySteps = 32;
// 地平線の向きの区間（荒野は層の底から頂まで約 36 km）は最大の歩数で進む（1 歩 約 190 m）。溶かしを外して見えるようになった
// 地平線の近くの遠い雲の、歩の端数による時間方向のちらつきを、溶かしていた前より大きくしない歩数（SCN-WL-CLOUD-FAR-RANGE）
const int kMaxPrimarySteps = 192;
// 1 歩の長さの目安(m)。真上の 2.5 km は最少の歩数、地平線の方は最大の歩数で進む
const float kPrimaryStepTarget = 90.0;
// 太陽へ向かう光学的深さを数える回数と最初の歩幅(m)。歩幅は 1 回ごとに 1.8 倍
const int kLightSteps = 6;
const float kLightFirstStep = 70.0;
// 細部で縁を削るのはこの距離までにする（遠い雲は画面で小さく、細部は見えない）
const float kDetailMaxDistance = 18000.0;
// 雲までの区間の空気遠近を積む歩数（VolumetricsCommon.glsl の kAtmosphereSteps と同じ中点則）
const int kCloudAerialSteps = 32;
// 透過率がこれを下回ったら進むのをやめる
const float kMinTransmittance = 0.01;
// 天気の模様の覆いの閾値（1 - 被覆）の下と上の幅。塊の縁の薄い膜の広がりを決める
const float kCoverageEdgeBelow = 0.03;
const float kCoverageEdgeAbove = 0.08;
// 形のノイズの密度の倍率（1 で頭打ち）。塊の本体を光学的に厚くし、縁の薄い所だけを半透明に残す
const float kCloudDensityGain = 3.0;
// 天気の模様を引く水平の位置を、高さでも変わる 3D の歪みでずらす幅(m)。天気の模様は x・z だけで決まるので、
// ずらさないと塊の側面が鉛直に押し出した壁になり、横から見ると縦にまっすぐな縁になる
const float kWeatherWarpMeters = 400.0;
// 歪みを引く形のノイズのテクスチャ座標のずれ（同じ点の形と別の模様にする）
const vec3 kWeatherWarpPhase = vec3(0.37, 0.53, 0.19);

// 遠い雲の消える原因を分ける診断の切り替え（撮影の雲の書き出しの CloudProbePass が視線ごとに立てる。開発ビルドだけ。
// 描画の cloud_layer.frag は常に 0 で、既定の描画は変わらない）
// 細かい歩で積む（歩数の上限を kDiagnosticMaxPrimarySteps へ上げ、1 歩を kPrimaryStepTarget に近づける）。
// SCN-WL-CLOUD-FAR-RANGE の前は 45 km の打ち切りを外す切り替えだった
const uint kCloudDiagnosticNoMarchLimit = 1u;
// 距離の溶かしを外す（SCN-WL-CLOUD-FAR-RANGE で溶かしそのものを外したので、今は何も変えない）
const uint kCloudDiagnosticNoFade = 2u;
// 雲までの区間の空気遠近を外す
const uint kCloudDiagnosticNoHaze = 4u;
// 細かい歩で積むときの歩数の上限（地平線の向きの雲の層の区間 約 36 km を 90 m の歩で進められる数）
const int kDiagnosticMaxPrimarySteps = 1024;

bool IsFiniteFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

vec2 EquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989);
    uv += 0.5;
    return uv;
}

vec3 SampleSky(vec3 direction)
{
    return max(texture(skyAtmosphereRadiance, EquirectangularUV(direction)).rgb, vec3(0.0));
}

float Remap(float value, float low, float high, float newLow, float newHigh)
{
    return newLow + (value - low) / max(high - low, 1.0e-5) * (newHigh - newLow);
}

float HenyeyGreenstein(float cosine, float anisotropy)
{
    float g2 = anisotropy * anisotropy;
    float denominator = max(1.0 + g2 - 2.0 * anisotropy * cosine, 1.0e-4);
    return (1.0 - g2) / (4.0 * PI * denominator * sqrt(denominator));
}

// 画面の位置ごとに散らした 0〜1 の値（interleaved gradient noise）。フレームごとに位相をずらす
float InterleavedGradientNoise(vec2 pixel, float frameIndex)
{
    pixel += 5.588238 * frameIndex;
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// 惑星の中心を原点に、高さ originY の点から direction へ進む光線と半径 radius の球の交点の距離（遠い方・近い方）
// 交わらなければ x > y を返す
vec2 IntersectSphere(float originY, vec3 direction, float radius)
{
    float b = originY * direction.y;
    float c = originY * originY - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
    {
        return vec2(1.0, -1.0);
    }
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

// 雲の密度（0〜1）。heightFraction は雲の層の中の高さの割合（0=底, 1=頂）
float SampleCloudDensity(vec3 position, float heightFraction, bool bDetail)
{
    // 形のノイズの格子を世界の軸から傾けて回す。格子が世界の軸に沿うと、Perlin-Worley の格子の面が
    // 雲の縦にまっすぐな縁と四角い角になる。四元数 (4, 1, 2, 2) の回転で成分が 1/25 の整数倍なので、
    // 風のずれを形の大きさの 25 倍で折り返すと回した後の座標は整数だけずれ、見た目は飛ばない（CloudLayerPass.inl）
    const mat3 shapeRotation = mat3(9.0, 20.0, -12.0,
                                    -12.0, 15.0, 16.0,
                                    20.0, 0.0, 15.0) / 25.0;
    vec3 shapePosition = position + vec3(params.shapeAndWeatherOffset.x, 0.0, params.shapeAndWeatherOffset.y);
    vec3 shapeCoordinate = shapeRotation * (shapePosition * params.extinctionAndInverseScales.y);

    // 天気の模様: どこに雲があるか。基本の形のノイズを、別の大きさ・向きで水平に 2 つ引いて重ねる
    // 風のずれは回転・倍率の後のテクスチャ座標で CPU が折り返して渡す（CloudLayerPass.inl と同じ回転・倍率・位相）
    // 引く位置は形のノイズの別の模様で高さごとに水平にずらし、塊の側面を鉛直の壁にしない
    vec2 weatherWarp = (textureLod(shapeNoise, shapeCoordinate + kWeatherWarpPhase, 0.0).gb - 0.5) * (2.0 * kWeatherWarpMeters);
    const mat2 weatherRotation = mat2(0.7986, 0.6018, -0.6018, 0.7986);
    vec2 weatherUV = weatherRotation * (position.xz + weatherWarp) * params.extinctionAndInverseScales.w;
    vec4 weatherLow = textureLod(shapeNoise,
                                 vec3(weatherUV.x + params.shapeAndWeatherOffset.z, 0.37,
                                      weatherUV.y + params.shapeAndWeatherOffset.w),
                                 0.0);
    vec4 weatherHigh = textureLod(shapeNoise,
                                  vec3(weatherUV.y * 2.7 + params.weatherHighOffset.x, 0.71,
                                       weatherUV.x * 2.7 + params.weatherHighOffset.y),
                                  0.0);
    float weather = clamp(weatherLow.r * 0.55 + weatherLow.g * 0.2 + weatherHigh.r * 0.25, 0.0, 1.0);
    float coverage = params.layerAltitudesAndCoverage.w;
    // 覆いの閾値の幅は狭く取り、塊の外形を天気の模様の等値線で締める（幅 0.30 では塊の外側に空の 1 割を超える薄い膜が広がった）
    float coverageMask = smoothstep(1.0 - coverage - kCoverageEdgeBelow, 1.0 - coverage + kCoverageEdgeAbove, weather);
    if (coverageMask <= 0.001)
    {
        return 0.0;
    }

    // 高さの形: 底は平らに急に立ち上がり、頂は丸く細る。天気の薄い所ほど頂を低くする
    float topLimit = mix(0.35, 1.0, coverageMask);
    float profile = clamp(Remap(heightFraction, 0.0, 0.07, 0.0, 1.0), 0.0, 1.0) *
                    clamp(Remap(heightFraction, topLimit * 0.55, topLimit, 1.0, 0.0), 0.0, 1.0);
    if (profile <= 0.0)
    {
        return 0.0;
    }

    vec4 shape = textureLod(shapeNoise, shapeCoordinate, 0.0);
    float lowFrequencyFbm = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = clamp(Remap(shape.r, lowFrequencyFbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    base = clamp(Remap(base * profile, 1.0 - coverageMask, 1.0, 0.0, 1.0) * coverageMask * kCloudDensityGain, 0.0, 1.0);
    if (base <= 0.0 || !bDetail)
    {
        return base;
    }

    // 細部で縁を削る。底の方は渦を巻く形、上の方はもくもくした形にする
    vec3 detail = textureLod(detailNoise,
                             (position + params.detailOffset.xyz) * params.extinctionAndInverseScales.z,
                             0.0).rgb;
    float detailFbm = detail.r * 0.625 + detail.g * 0.25 + detail.b * 0.125;
    float detailModifier = mix(detailFbm, 1.0 - detailFbm, clamp(heightFraction * 4.0, 0.0, 1.0));
    return clamp(Remap(base, detailModifier * 0.35, 1.0, 0.0, 1.0), 0.0, 1.0);
}

// 1 本の視線を雲の層で進めた結果
struct CloudRayResult
{
    // 雲の層を進めたか（偽なら雲は無く、ほかの値は 0 か 1）
    bool bMarched;
    // 雲の層を進めた区間(m)と、決めた歩数・1 歩の長さ(m)
    float StartDistance;
    float EndDistance;
    int StepCount;
    float StepLength;
    // 透過率の打ち切り（kMinTransmittance）までに進めた歩数
    int StepsTaken;
    float Transmittance;
    // 視線の光学的深さ（進めた歩の消散係数 × 歩の長さの和）
    float OpticalDepth;
    // 雲の散乱光（事前露出の前、cd/m²）と、光を返した所の距離の重み付きの平均(m)
    vec3 Scattered;
    float CloudDistance;
    // 環境光に使った上の空の平均の放射輝度と、地面の照り返しの放射輝度（cd/m²）
    vec3 SkyAbove;
    vec3 GroundRadiance;
};

CloudRayResult MakeEmptyCloudRayResult()
{
    CloudRayResult result;
    result.bMarched = false;
    result.StartDistance = 0.0;
    result.EndDistance = 0.0;
    result.StepCount = 0;
    result.StepLength = 0.0;
    result.StepsTaken = 0;
    result.Transmittance = 1.0;
    result.OpticalDepth = 0.0;
    result.Scattered = vec3(0.0);
    result.CloudDistance = 0.0;
    result.SkyAbove = vec3(0.0);
    result.GroundRadiance = vec3(0.0);
    return result;
}

// 画面の位置 uv（0〜1、左上が原点）のカメラからの視線の向き。求められなければ偽
bool ComputeCloudRayDirection(vec2 uv, out vec3 rayDirection)
{
    rayDirection = vec3(0.0, 1.0, 0.0);
    vec4 clipPosition = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 farPosition = params.inverseViewProjection * clipPosition;
    if (!IsFiniteFloat(farPosition.w) || abs(farPosition.w) <= 1.0e-8)
    {
        return false;
    }
    vec3 cameraPosition = params.cameraPositionAndPreExposure.xyz;
    rayDirection = normalize(farPosition.xyz / farPosition.w - cameraPosition);
    return true;
}

// カメラの位置から rayDirection へ雲の層を進める。jitter（0〜1）は歩の位置の端数、diagnosticFlags は診断の切り替え
// （kCloudDiagnostic*。描画は 0）
CloudRayResult MarchCloudLayerWithDiagnostics(vec3 rayDirection, float jitter, uint diagnosticFlags)
{
    CloudRayResult result = MakeEmptyCloudRayResult();
    vec3 cameraPosition = params.cameraPositionAndPreExposure.xyz;
    float planetRadius = params.layerAltitudesAndCoverage.x;
    float bottomRadius = planetRadius + params.layerAltitudesAndCoverage.y;
    float topRadius = planetRadius + params.layerAltitudesAndCoverage.z;
    float cameraAltitude = max(cameraPosition.y, 1.0);
    float originY = planetRadius + cameraAltitude;

    // 地面に当たる視線には雲が無い
    vec2 planetHit = IntersectSphere(originY, rayDirection, planetRadius);
    if (planetHit.x <= planetHit.y && planetHit.x > 0.0)
    {
        return result;
    }

    vec2 topHit = IntersectSphere(originY, rayDirection, topRadius);
    vec2 bottomHit = IntersectSphere(originY, rayDirection, bottomRadius);
    float startDistance;
    float endDistance;
    if (originY < bottomRadius)
    {
        startDistance = bottomHit.y;
        endDistance = topHit.y;
    }
    else if (originY <= topRadius)
    {
        startDistance = 0.0;
        endDistance = topHit.y;
        if (bottomHit.x <= bottomHit.y && bottomHit.x > 0.0)
        {
            endDistance = min(endDistance, bottomHit.x);
        }
    }
    else
    {
        if (topHit.x > topHit.y || topHit.y <= 0.0)
        {
            return result;
        }
        startDistance = max(topHit.x, 0.0);
        endDistance = (bottomHit.x <= bottomHit.y && bottomHit.x > 0.0) ? bottomHit.x : topHit.y;
    }
    bool bFineSteps = (diagnosticFlags & kCloudDiagnosticNoMarchLimit) != 0u;
    int maxPrimarySteps = bFineSteps ? kDiagnosticMaxPrimarySteps : kMaxPrimarySteps;
    if (!(endDistance > startDistance))
    {
        return result;
    }

    float segmentLength = endDistance - startDistance;
    int stepCount = clamp(int(segmentLength / kPrimaryStepTarget), kMinPrimarySteps, maxPrimarySteps);
    float stepLength = segmentLength / float(stepCount);

    vec3 sunDirection = normalize(params.sunDirectionAndFrameIndex.xyz);
    vec3 sunIlluminance = params.sunIlluminance.rgb;
    float sunCosine = dot(rayDirection, sunDirection);
    float extinction = params.extinctionAndInverseScales.x;
    float layerThickness = params.layerAltitudesAndCoverage.z - params.layerAltitudesAndCoverage.y;

    // 空の環境光: 上の半球の空の平均（天頂と仰角 30° の 4 方向）と、太陽と空に照らされた地面の照り返し
    vec3 skyAbove = SampleSky(vec3(0.0, 1.0, 0.0)) * 0.2 +
                    (SampleSky(vec3(0.866, 0.5, 0.0)) + SampleSky(vec3(-0.866, 0.5, 0.0)) +
                     SampleSky(vec3(0.0, 0.5, 0.866)) + SampleSky(vec3(0.0, 0.5, -0.866))) * 0.2;
    vec3 groundRadiance = params.groundAlbedo.rgb *
                          (sunIlluminance * max(sunDirection.y, 0.0) / PI + skyAbove);

    // 位相: 太陽の側を明るくする前方の山と、太陽を背にした面を少し明るくする後方の山を混ぜる
    float phases[3];
    for (int octave = 0; octave < 3; ++octave)
    {
        float scale = pow(0.5, float(octave));
        phases[octave] = mix(HenyeyGreenstein(sunCosine, 0.8 * scale),
                             HenyeyGreenstein(sunCosine, -0.3 * scale),
                             0.3);
    }

    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    float opticalDepth = 0.0;
    int stepsTaken = 0;
    float weightedDistance = 0.0;
    float distanceWeight = 0.0;
    for (int stepIndex = 0; stepIndex < maxPrimarySteps; ++stepIndex)
    {
        if (stepIndex >= stepCount || transmittance < kMinTransmittance)
        {
            break;
        }
        stepsTaken = stepIndex + 1;
        float sampleDistance = startDistance + (float(stepIndex) + jitter) * stepLength;
        vec3 samplePosition = cameraPosition + rayDirection * sampleDistance;
        vec3 fromCenter = vec3(samplePosition.x - cameraPosition.x,
                               originY + rayDirection.y * sampleDistance,
                               samplePosition.z - cameraPosition.z);
        float altitude = length(fromCenter) - planetRadius;
        float heightFraction = (altitude - params.layerAltitudesAndCoverage.y) / layerThickness;
        if (heightFraction < 0.0 || heightFraction > 1.0)
        {
            continue;
        }
        // ノイズは雲の層の中の高さで引く（球殻の曲がりに沿って同じ形が並ぶ）
        vec3 noisePosition = vec3(samplePosition.x, altitude, samplePosition.z);
        float density = SampleCloudDensity(noisePosition, heightFraction, sampleDistance < kDetailMaxDistance);
        if (density <= 0.001)
        {
            continue;
        }

        // 太陽へ向かう光学的深さ（後半の遠い点は細部を省く）。後半の歩は 1 点で数百 m〜1.3 km を代表するので、点を区間の
        // 中点に固定すると、点が塊の急な縁をまたぐ所で光学的深さが跳び、陰が硬い縁の平らな段になる。点を主の歩と同じ
        // 端数で区間の中に散らし、時間の蓄積で区間の平均に均す
        float lightOpticalDepth = 0.0;
        float lightDistance = 0.0;
        float lightStep = kLightFirstStep;
        for (int lightIndex = 0; lightIndex < kLightSteps; ++lightIndex)
        {
            float lightSampleDistance = lightDistance + lightStep * jitter;
            vec3 lightPosition = noisePosition + sunDirection * lightSampleDistance;
            float lightHeightFraction = (lightPosition.y - params.layerAltitudesAndCoverage.y) / layerThickness;
            if (lightHeightFraction > 1.0)
            {
                break;
            }
            if (lightHeightFraction >= 0.0)
            {
                lightOpticalDepth += SampleCloudDensity(lightPosition, lightHeightFraction, lightIndex < 3) * lightStep;
            }
            lightDistance += lightStep;
            lightStep *= 1.8;
        }
        lightOpticalDepth *= extinction;

        // 多重散乱の近似: 消散・寄与・位相の偏りを 0.5 倍ずつにした 3 回分を足す
        float sunTerm = 0.0;
        float contribution = 1.0;
        float attenuation = 1.0;
        for (int octave = 0; octave < 3; ++octave)
        {
            sunTerm += contribution * exp(-lightOpticalDepth * attenuation) * phases[octave];
            contribution *= 0.5;
            attenuation *= 0.5;
        }

        // 環境光: 厚い雲は多重散乱で受けた空の光をほぼ返すので、上の空の平均の放射輝度を頂でそのまま、
        // 底で 6 割受ける。地面の照り返しは底ほど届く
        vec3 ambient = skyAbove * mix(0.6, 1.0, heightFraction) +
                       0.5 * groundRadiance * mix(1.0, 0.3, heightFraction);

        float sigma = density * extinction;
        vec3 source = sigma * (sunIlluminance * sunTerm + ambient);
        float stepTransmittance = exp(-sigma * stepLength);
        // 1 歩の中で散乱光が透過率で減る分を解析的に積分する
        vec3 integrated = (source - source * stepTransmittance) / max(sigma, 1.0e-7);
        float absorbed = transmittance * (1.0 - stepTransmittance);
        scattered += transmittance * integrated;
        weightedDistance += absorbed * sampleDistance;
        distanceWeight += absorbed;
        transmittance *= stepTransmittance;
        opticalDepth += sigma * stepLength;
    }

    result.bMarched = true;
    result.StartDistance = startDistance;
    result.EndDistance = endDistance;
    result.StepCount = stepCount;
    result.StepLength = stepLength;
    result.StepsTaken = stepsTaken;
    result.Transmittance = transmittance;
    result.OpticalDepth = opticalDepth;
    result.Scattered = scattered;
    result.CloudDistance = distanceWeight > 0.0 ? weightedDistance / distanceWeight : startDistance;
    result.SkyAbove = skyAbove;
    result.GroundRadiance = groundRadiance;
    return result;
}

CloudRayResult MarchCloudLayer(vec3 rayDirection, float jitter)
{
    return MarchCloudLayerWithDiagnostics(rayDirection, jitter, 0u);
}

// 空気遠近の高度ごとの表を、海面からの高度 altitude（m）で線形に引く（row 0 = 太陽の照度 × 透過率、row 1 = 太陽の照度 × Ψ_ms）。
// VolumetricsCommon.glsl の SampleAerialProfile と同じ
vec3 SampleCloudAerialProfile(uint row, float altitude)
{
    float position = clamp(altitude / params.aerialProfileRange.x, 0.0, 1.0) * float(kCloudAerialProfileSize - 1u);
    uint index = min(uint(position), kCloudAerialProfileSize - 2u);
    float weight = position - float(index);
    uint base = row * kCloudAerialProfileSize + index;
    return mix(params.aerialProfile[base].rgb, params.aerialProfile[base + 1u].rgb, weight);
}

// カメラから rayDirection の向きに rayDistance m の区間の空気遠近（RGB の透過率と散乱光。事前露出の前）。
// VolumetricsCommon.glsl の MarchFogAndAerial の空の媒質の分（霧を除く）と同じ式・同じ歩数: 高度は球の上で数え、各点の散乱の源は
// 太陽の単一散乱（Rayleigh と Mie の位相 × 表の太陽の照度 × 透過率）と多重散乱（散乱係数 × 表の Ψ_ms）
void IntegrateCloudAerial(vec3 rayDirection, float rayDistance, out vec3 transmittance, out vec3 scattering)
{
    float startDistance = max(params.aerialPerspective.z, 0.0);
    float distanceScale = params.aerialPerspective.y;
    float planetRadius = params.layerAltitudesAndCoverage.x;
    float originHeight = params.cameraPositionAndPreExposure.y + params.aerialPerspective.w;
    float viewSunCosine = clamp(dot(rayDirection, params.sunDirectionAndFrameIndex.xyz), -1.0, 1.0);
    float rayleighPhase = 0.0596831 * (1.0 + viewSunCosine * viewSunCosine);
    float anisotropy = params.mieExtinctionAndScattering.w;
    float mieDenominator = max(1.0 + anisotropy * anisotropy - 2.0 * anisotropy * viewSunCosine, 1.0e-4);
    float miePhase = (1.0 - anisotropy * anisotropy) / (12.5663706 * pow(mieDenominator, 1.5));

    float stepLength = rayDistance / float(kCloudAerialSteps);
    vec3 airDepth = vec3(0.0);
    scattering = vec3(0.0);
    for (int stepIndex = 0; stepIndex < kCloudAerialSteps; ++stepIndex)
    {
        float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
        vec3 fromCenter = vec3(rayDirection.x * sampleDistance,
                               planetRadius + originHeight + rayDirection.y * sampleDistance,
                               rayDirection.z * sampleDistance);
        float altitude = length(fromCenter) - planetRadius;
        float airWeight = sampleDistance >= startDistance ? distanceScale : 0.0;
        float rayleighDensity = exp(clamp(-altitude / params.rayleighExtinctionAndScaleHeight.w, -80.0, 80.0));
        float mieDensity = exp(clamp(-altitude / params.mieExtinctionAndScattering.y, -80.0, 80.0));
        vec3 rayleighScattering = params.rayleighExtinctionAndScaleHeight.rgb * (rayleighDensity * airWeight);
        float mieScattering = params.mieExtinctionAndScattering.z * mieDensity * airWeight;
        vec3 stepAirDepth = (rayleighScattering + vec3(params.mieExtinctionAndScattering.x * mieDensity * airWeight)) *
                            stepLength;
        // その歩の中点までの透過率
        vec3 sampleTransmittance = exp(-(airDepth + 0.5 * stepAirDepth));
        vec3 sunIlluminance = SampleCloudAerialProfile(0u, altitude);
        vec3 multipleScattering = SampleCloudAerialProfile(1u, altitude);
        scattering += sampleTransmittance *
                      ((rayleighScattering * rayleighPhase + vec3(mieScattering * miePhase)) * sunIlluminance +
                       (rayleighScattering + vec3(mieScattering)) * multipleScattering) * stepLength;
        airDepth += stepAirDepth;
    }
    transmittance = exp(-clamp(airDepth, vec3(0.0), vec3(80.0)));
    scattering = max(scattering, vec3(0.0));
}

// 雲の放射輝度（事前露出の前）。fade は距離で溶かす割合で、今は常に 1（書き出しの列の並びを保つために返す）。opacity は 1 - 透過率、
// diagnosticFlags は診断の切り替え（kCloudDiagnostic*。描画は 0）。aerialVisibility は雲までの空気遠近の透過率の RGB の平均
vec3 ComposeCloudRadianceWithDiagnostics(CloudRayResult ray, vec3 rayDirection, float opacity, uint diagnosticFlags,
                                         out float aerialVisibility, out float fade)
{
    // 雲までの区間の大気: 雲の光を区間の透過率で減らし、区間の散乱光を雲が覆う割合（opacity）だけ足す。雲の後ろの空の画素
    // （空の LUT）は同じ区間の散乱光をすでに含み、合成で 1 - opacity を掛けて残るので、雲の分だけ足せば手前の大気を二重に数えない。
    // 空気遠近が無効なら地面と同じく何も掛けない
    vec3 aerialTransmittance = vec3(1.0);
    vec3 aerialScattering = vec3(0.0);
    if ((diagnosticFlags & kCloudDiagnosticNoHaze) == 0u && params.aerialPerspective.x > 0.5 && ray.CloudDistance > 0.0)
    {
        IntegrateCloudAerial(rayDirection, ray.CloudDistance, aerialTransmittance, aerialScattering);
    }
    aerialVisibility = dot(aerialTransmittance, vec3(1.0 / 3.0));
    fade = 1.0;
    return ray.Scattered * aerialTransmittance + aerialScattering * opacity;
}

vec3 ComposeCloudRadiance(CloudRayResult ray, vec3 rayDirection, float opacity,
                          out float aerialVisibility, out float fade)
{
    return ComposeCloudRadianceWithDiagnostics(ray, rayDirection, opacity, 0u, aerialVisibility, fade);
}
