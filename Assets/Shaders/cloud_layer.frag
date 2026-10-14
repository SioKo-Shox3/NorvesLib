#version 450

// 空の画素に雲の層を描く。惑星の球殻にした雲の層を視線で進め、3D ノイズの密度から太陽の散乱光（前方・後方の
// 2 つの位相と多重散乱の近似）と空の環境光を積分し、前乗算のアルファ（散乱光、1 - 透過率）で出す。

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyAtmosphereRadiance;

layout(std140, set = 0, binding = 2) uniform CloudLayerParams
{
    mat4 inverseViewProjection;
    vec4 cameraPositionAndPreExposure;
    vec4 sunDirectionAndFrameIndex;      // xyz=太陽へ向かう向き, w=時間方向の雑音の位相
    vec4 sunIlluminance;                 // rgb=太陽の地表の照度(lux)
    vec4 layerAltitudesAndCoverage;      // x=惑星の半径, y=雲の底, z=雲の頂(m), w=空を覆う割合
    vec4 extinctionAndInverseScales;     // x=消散係数(1/m), y=基本の形, z=細部, w=天気の模様の 1/周期(1/m)
    vec4 shapeAndWeatherOffset;          // xy=基本の形の風のずれ, zw=天気の模様の風のずれ(m)
    vec4 detailOffset;                   // xyz=細部の風のずれ(m)
    vec4 groundAlbedo;
} params;

// 基本の形: R=Perlin-Worley, G・B・A=反転 Worley の fbm（低い周波数から）。細部: R・G・B=反転 Worley の fbm
layout(set = 0, binding = 3) uniform sampler3D shapeNoise;
layout(set = 0, binding = 4) uniform sampler3D detailNoise;

const float PI = 3.14159265359;
// 視線を進める最長の距離(m)。これより先の雲は地平線の霞に溶けて見えない
const float kMaxMarchDistance = 45000.0;
const int kMinPrimarySteps = 32;
const int kMaxPrimarySteps = 96;
// 1 歩の長さの目安(m)。真上の 2.5 km は最少の歩数、地平線の方は最大の歩数で進む
const float kPrimaryStepTarget = 90.0;
// 太陽へ向かう光学的深さを数える回数と最初の歩幅(m)。歩幅は 1 回ごとに 1.8 倍
const int kLightSteps = 6;
const float kLightFirstStep = 70.0;
// 細部で縁を削るのはこの距離までにする（遠い雲は画面で小さく、細部は見えない）
const float kDetailMaxDistance = 18000.0;
// 雲の向こうの霞: この距離で雲の光の 1/e が空の色へ置き換わる(m)
const float kCloudHazeDistance = 22000.0;
// 透過率がこれを下回ったら進むのをやめる
const float kMinTransmittance = 0.01;

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
    // 天気の模様: どこに雲があるか。基本の形のノイズを、別の大きさ・向きで水平に 2 つ引いて重ねる
    const mat2 weatherRotation = mat2(0.7986, 0.6018, -0.6018, 0.7986);
    vec2 weatherUV = weatherRotation * (position.xz + params.shapeAndWeatherOffset.zw) *
                     params.extinctionAndInverseScales.w;
    vec4 weatherLow = textureLod(shapeNoise, vec3(weatherUV.x, 0.37, weatherUV.y), 0.0);
    vec4 weatherHigh = textureLod(shapeNoise, vec3(weatherUV.y * 2.7 + 0.31, 0.71, weatherUV.x * 2.7 + 0.53), 0.0);
    float weather = clamp(weatherLow.r * 0.55 + weatherLow.g * 0.2 + weatherHigh.r * 0.25, 0.0, 1.0);
    float coverage = params.layerAltitudesAndCoverage.w;
    float coverageMask = smoothstep(1.0 - coverage - 0.08, 1.0 - coverage + 0.22, weather);
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

    vec3 shapePosition = position + vec3(params.shapeAndWeatherOffset.x, 0.0, params.shapeAndWeatherOffset.y);
    vec4 shape = textureLod(shapeNoise, shapePosition * params.extinctionAndInverseScales.y, 0.0);
    float lowFrequencyFbm = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
    float base = clamp(Remap(shape.r, lowFrequencyFbm - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
    base = clamp(Remap(base * profile, 1.0 - coverageMask, 1.0, 0.0, 1.0), 0.0, 1.0) * coverageMask;
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

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    if (IsFiniteFloat(depth) && depth < 0.999999)
    {
        // 不透明物の画素。雲の層は地形より高いので描かない
        outColor = vec4(0.0);
        return;
    }

    vec4 clipPosition = vec4(fragUV * 2.0 - 1.0, 1.0, 1.0);
    vec4 farPosition = params.inverseViewProjection * clipPosition;
    if (!IsFiniteFloat(farPosition.w) || abs(farPosition.w) <= 1.0e-8)
    {
        outColor = vec4(0.0);
        return;
    }
    vec3 cameraPosition = params.cameraPositionAndPreExposure.xyz;
    vec3 rayDirection = normalize(farPosition.xyz / farPosition.w - cameraPosition);

    float planetRadius = params.layerAltitudesAndCoverage.x;
    float bottomRadius = planetRadius + params.layerAltitudesAndCoverage.y;
    float topRadius = planetRadius + params.layerAltitudesAndCoverage.z;
    float cameraAltitude = max(cameraPosition.y, 1.0);
    float originY = planetRadius + cameraAltitude;

    // 地面に当たる視線には雲が無い
    vec2 planetHit = IntersectSphere(originY, rayDirection, planetRadius);
    if (planetHit.x <= planetHit.y && planetHit.x > 0.0)
    {
        outColor = vec4(0.0);
        return;
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
            outColor = vec4(0.0);
            return;
        }
        startDistance = max(topHit.x, 0.0);
        endDistance = (bottomHit.x <= bottomHit.y && bottomHit.x > 0.0) ? bottomHit.x : topHit.y;
    }
    if (!(endDistance > startDistance) || startDistance > kMaxMarchDistance)
    {
        outColor = vec4(0.0);
        return;
    }
    endDistance = min(endDistance, startDistance + kMaxMarchDistance);

    float segmentLength = endDistance - startDistance;
    int stepCount = clamp(int(segmentLength / kPrimaryStepTarget), kMinPrimarySteps, kMaxPrimarySteps);
    float stepLength = segmentLength / float(stepCount);
    float jitter = InterleavedGradientNoise(gl_FragCoord.xy, params.sunDirectionAndFrameIndex.w);

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
    float weightedDistance = 0.0;
    float distanceWeight = 0.0;
    for (int stepIndex = 0; stepIndex < kMaxPrimarySteps; ++stepIndex)
    {
        if (stepIndex >= stepCount || transmittance < kMinTransmittance)
        {
            break;
        }
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

        // 太陽へ向かう光学的深さ（後半の遠い点は細部を省く）
        float lightOpticalDepth = 0.0;
        float lightDistance = 0.0;
        float lightStep = kLightFirstStep;
        for (int lightIndex = 0; lightIndex < kLightSteps; ++lightIndex)
        {
            float lightSampleDistance = lightDistance + lightStep * 0.5;
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
    }

    float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
    if (opacity <= 0.0)
    {
        outColor = vec4(0.0);
        return;
    }

    // 遠い雲ほど、手前の空気の散乱で空の色に溶ける
    float cloudDistance = distanceWeight > 0.0 ? weightedDistance / distanceWeight : startDistance;
    float hazeVisibility = exp(-cloudDistance / kCloudHazeDistance);
    vec3 cloudRadiance = scattered * hazeVisibility + SampleSky(rayDirection) * opacity * (1.0 - hazeVisibility);
    cloudRadiance = min(cloudRadiance * params.cameraPositionAndPreExposure.w, vec3(65504.0));
    outColor = vec4(cloudRadiance, opacity);
}
