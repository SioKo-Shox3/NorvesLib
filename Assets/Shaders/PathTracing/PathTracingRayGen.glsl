// 決定論的な1試料を追跡し、前回までの平均と合成する。
// 各表面頂点で点・spot・方向光と発光三角形・太陽円盤を光源標本し（NEE）、BSDF標本で経路を延ばす。
// 発光三角形と太陽円盤はpower heuristic（β=2）で2つの標本を合成し、環境光はBSDF標本だけで評価する。
#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
#include "PathTracing/PathTracingCommon.glsl"
#include "PathTracing/PathTracingScene.glsl"
#include "Common/PbrMaterialEvaluation.glsl"
#include "PathTracing/PathTracingBsdf.glsl"

layout(set = 0, binding = 3) uniform sampler2D previousAverage;
layout(set = 0, binding = 4, rgba32f) uniform writeonly image2D currentAverage;
layout(set = 0, binding = 5) uniform sampler2D skyRadiance;
layout(set = 0, binding = 6) uniform sampler2D skyTransmittance;
layout(set = 0, binding = 7) uniform sampler2D skySunDisk;
layout(set = 0, binding = 10) uniform sampler2D environmentTexture;

// ラスタのlighting.fragと同じ並び（LightingPassの詰め方）。
struct PathLight
{
    vec4 position; // xyz=位置、w=種類（0=方向、1=点、2=spot）
    vec4 direction; // xyz=進行方向、w=内側円錐の余弦
    vec4 chromaticityAndIntensity; // xyz=Y=1の色度、w=lux（方向）またはcd
    vec4 attenuation; // x=範囲、y=外側円錐の余弦
};
layout(set = 0, binding = 11, std430) readonly buffer PathLights
{
    PathLight values[];
} lights;
layout(set = 0, binding = 12, std430) readonly buffer PathEmissiveInstances
{
    uvec4 values[]; // x=instance番号、y=三角形数、z=先頭三角形の通し番号
} emissiveInstances;

layout(location = 0) rayPayloadEXT PathPayload payload;

uint NextRandom(inout uint state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float Random01(inout uint state)
{
    return float(NextRandom(state) & 0x00ffffffu) * (1.0 / 16777216.0);
}

vec2 EquirectangularUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    return uv * vec2(0.15915494, 0.31830989) + 0.5;
}

uint BsdfMode()
{
    return parameters.lightState.w & 3u;
}

uint SamplingMode()
{
    return (parameters.lightState.w >> 2u) & 3u;
}

uint TransportScope()
{
    return parameters.sampleState.w;
}

float PreExposure()
{
    return parameters.exposureAndDebug.x;
}

// 太陽円盤の立体角。円盤判定と同じ余弦値から求める（1-cosはfloatで誤差なく引ける）。
float SunSolidAngle()
{
    return 6.28318530718 * (1.0 - parameters.skySunDirectionAndCosRadius.w);
}

bool IsSunAvailable()
{
    return parameters.skyState.z > 0.5 && parameters.skyState.y > 0.0 &&
           SunSolidAngle() > 0.0;
}

// 空が無効なときの環境光（物理値）。
vec3 EnvironmentRadiance(vec3 direction)
{
    uint mode = uint(parameters.environmentRadiance.w + 0.5);
    if (mode == PATH_ENVIRONMENT_UNIFORM)
    {
        return parameters.environmentRadiance.rgb;
    }
    if (mode == PATH_ENVIRONMENT_EQUIRECT)
    {
        return max(textureLod(environmentTexture, EquirectangularUV(direction), 0.0).rgb,
                   vec3(0.0)) * parameters.environmentRadiance.r;
    }
    return vec3(0.0);
}

// 不交差の放射輝度（事前露出済み）。bsdfPdfは2次以降の太陽円盤のMIS重みに使う。
vec3 MissRadiance(vec3 direction, bool primaryRay, float bsdfPdf)
{
    if (parameters.skyState.z < 0.5)
    {
        // 空を要求したがLUTがないフレームは黒へ固定する。
        return parameters.skyState.w > 0.5 ? vec3(0.0)
                                            : EnvironmentRadiance(direction) * PreExposure();
    }
    vec3 sky = textureLod(skyRadiance, EquirectangularUV(direction), 0.0).rgb;
    vec4 disk = textureLod(skySunDisk, vec2(0.5), 0.0);
    if (disk.a > 0.5)
    {
        sky *= clamp(textureLod(skyTransmittance,
                                vec2(clamp(direction.y, 0.0, 1.0), 0.0),
                                0.0).rgb, vec3(0.0), vec3(1.0));
    }
    sky *= parameters.skyState.x;
    bool bInsideSun = dot(direction, parameters.skySunDirectionAndCosRadius.xyz) >=
                      parameters.skySunDirectionAndCosRadius.w;
    if (bInsideSun && primaryRay && disk.a > 0.5)
    {
        // 1次レイは表示用の円盤（fp16飽和を含む）を見せる。
        sky += disk.rgb;
    }
    else if (bInsideSun && !primaryRay && IsSunAvailable())
    {
        // 2次以降は照明用の飽和しない太陽放射輝度（照度/立体角）をBSDF側の重みで足す。
        uint sampling = SamplingMode();
        float weight = sampling == PATH_SAMPLING_BSDF_ONLY ? 1.0 :
                       sampling == PATH_SAMPLING_LIGHT_ONLY ? 0.0 :
                       PowerHeuristic(bsdfPdf, 1.0 / SunSolidAngle());
        sky += vec3(parameters.skyState.y / SunSolidAngle()) * weight;
    }
    return max(sky, vec3(0.0));
}

vec3 SampleSolarDirection(inout uint state)
{
    vec3 sun = parameters.skySunDirectionAndCosRadius.xyz;
    float cosine = mix(parameters.skySunDirectionAndCosRadius.w, 1.0,
                       Random01(state));
    float phi = 6.28318530718 * Random01(state);
    float radius = sqrt(max(0.0, 1.0 - cosine * cosine));
    vec3 tangent = normalize(cross(abs(sun.z) < 0.9 ? vec3(0.0, 0.0, 1.0) :
                                   vec3(0.0, 1.0, 0.0), sun));
    vec3 bitangent = cross(sun, tangent);
    return normalize(sun * cosine + tangent * (radius * cos(phi)) +
                     bitangent * (radius * sin(phi)));
}

bool IsFiniteFloat(float value)
{
    return !isnan(value) && !isinf(value);
}

float FogTransmittance(float originHeight, float directionY, float distance)
{
    float density = max(parameters.fogDensityHeightFalloffAndEnabled.x, 0.0);
    float baseHeight = parameters.fogDensityHeightFalloffAndEnabled.y;
    float falloff = max(parameters.fogDensityHeightFalloffAndEnabled.z, 0.0);
    if (density <= 0.0 || distance <= 0.0 ||
        !IsFiniteFloat(originHeight) || !IsFiniteFloat(directionY) ||
        !IsFiniteFloat(distance))
    {
        return 1.0;
    }

    float originExponent = -falloff * (originHeight - baseHeight);
    float verticalRate = falloff * clamp(directionY, -1.0, 1.0);
    float exponentChange = verticalRate * distance;
    float logIntegral;
    if (abs(verticalRate) < 1.0e-8 || abs(exponentChange) < 1.0e-4)
    {
        logIntegral = log(distance);
    }
    else
    {
        float logNumerator;
        if (exponentChange > 80.0)
        {
            logNumerator = 0.0;
        }
        else if (exponentChange > 0.0)
        {
            logNumerator = log(max(1.0 - exp(-exponentChange), 1.0e-35));
        }
        else if (exponentChange < -80.0)
        {
            logNumerator = -exponentChange;
        }
        else
        {
            logNumerator = log(max(exp(-exponentChange) - 1.0, 1.0e-35));
        }
        logIntegral = logNumerator - log(abs(verticalRate));
    }
    float logOpticalDepth = log(density) + originExponent + logIntegral;
    if (!IsFiniteFloat(logOpticalDepth))
    {
        return logOpticalDepth > 0.0 ? exp(-80.0) : 1.0;
    }
    if (logOpticalDepth >= log(80.0))
    {
        return exp(-80.0);
    }
    return exp(-clamp(exp(logOpticalDepth), 0.0, 80.0));
}

vec3 FogSingleScattering(vec3 origin, vec3 direction, float distance)
{
    if (parameters.fogLightRadianceAndEnabled.w < 0.5)
    {
        return vec3(0.0);
    }
    float anisotropy = clamp(parameters.fogLightDirectionAndAnisotropy.w,
                             -0.95, 0.95);
    float cosineTheta = clamp(dot(parameters.fogLightDirectionAndAnisotropy.xyz,
                                  -direction), -1.0, 1.0);
    float denominator = max(1.0 + anisotropy * anisotropy -
                            2.0 * anisotropy * cosineTheta, 1.0e-4);
    float phase = (1.0 - anisotropy * anisotropy) /
                  (12.5663706 * pow(denominator, 1.5));
    float stepLength = distance / 24.0;
    vec3 lightDirection = -parameters.fogLightDirectionAndAnisotropy.xyz;
    vec3 scattering = vec3(0.0);
    for (int stepIndex = 0; stepIndex < 24; ++stepIndex)
    {
        float sampleDistance = (float(stepIndex) + 0.5) * stepLength;
        vec3 samplePosition = origin + direction * sampleDistance;
        float densityExponent = -parameters.fogDensityHeightFalloffAndEnabled.z *
            (samplePosition.y - parameters.fogDensityHeightFalloffAndEnabled.y);
        float localDensity = parameters.fogDensityHeightFalloffAndEnabled.x *
            exp(clamp(densityExponent, -80.0, 80.0));
        float viewTransmittance = FogTransmittance(
            origin.y, direction.y, sampleDistance);
        if (!IsFiniteFloat(localDensity) || !IsFiniteFloat(viewTransmittance) ||
            localDensity <= 0.0 || viewTransmittance <= 0.0)
        {
            continue;
        }
        payload.Hit = 0u;
        traceRayEXT(scene, gl_RayFlagsOpaqueEXT |
                     gl_RayFlagsTerminateOnFirstHitEXT,
                    0xffu, 0u, 0u, 0u,
                    samplePosition, 0.001, lightDirection, 100000.0, 0);
        if (payload.Hit == 0u)
        {
            scattering += parameters.fogLightRadianceAndEnabled.rgb *
                (phase * localDensity * viewTransmittance * stepLength);
        }
    }
    return min(scattering * parameters.fogColorAndPreExposure.w,
               vec3(65504.0));
}

void ApplyFogSegment(vec3 origin, vec3 direction, vec3 surfacePosition,
                     inout vec3 throughput, inout vec3 radiance)
{
    if (parameters.fogDensityHeightFalloffAndEnabled.w < 0.5)
    {
        return;
    }
    float distance = length(surfacePosition - origin);
    if (!IsFiniteFloat(distance) || distance <= 0.0)
    {
        return;
    }
    float transmittance = FogTransmittance(origin.y, direction.y, distance);
    vec3 fogColor = parameters.fogColorAndPreExposure.rgb;
    if (parameters.skyState.z > 0.5)
    {
        fogColor = textureLod(skyRadiance, EquirectangularUV(direction), 0.0).rgb;
    }
    fogColor = max(fogColor, vec3(0.0)) *
               parameters.fogColorAndPreExposure.w;
    vec3 scattering = FogSingleScattering(origin, direction, distance);
    radiance += throughput *
        (fogColor * clamp(1.0 - transmittance, 0.0, 1.0) + scattering);
    throughput *= transmittance;
}

// 影レイ。最初の命中で打ち切り、命中シェーダーは実行しない（不交差シェーダーだけがHitを0にする）。
bool IsVisible(vec3 origin, vec3 direction, float maxDistance)
{
    if (maxDistance <= 0.001)
    {
        return true;
    }
    payload.Hit = 1u;
    traceRayEXT(scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT |
                           gl_RayFlagsSkipClosestHitShaderEXT,
                0xffu, 0u, 0u, 0u, origin, 0.001, direction, maxDistance, 0);
    return payload.Hit == 0u;
}

// 面から少し浮かせた原点から目標点までの可視性。方向と距離を浮かせた原点から測り直し、
// 目標点まで全区間を調べる。面上の点から測った方向のままだと、浅い角度で原点のずれが
// 光線方向へ伸び、目標の発光面を手前で横切って自分で遮る。点・spot光源の位置は幾何ではない。
bool IsPointVisible(vec3 shadowOrigin, vec3 target)
{
    vec3 toTarget = target - shadowOrigin;
    float targetDistance = length(toTarget);
    if (targetDistance <= 1.0e-6)
    {
        return true;
    }
    return IsVisible(shadowOrigin, toTarget / targetDistance, targetDistance);
}

float RangeWindow(float distance, float range)
{
    float factor = max(1.0 - pow(distance / max(range, 0.0001), 4.0), 0.0);
    return factor * factor;
}

// 点・spot・方向光の直接照明（光源標本だけ、重み1）。単位・減衰・spot円錐はラスタと同じ。
vec3 EvaluatePunctualLights(PathSurface surface, vec3 position, vec3 geometricNormal)
{
    vec3 sum = vec3(0.0);
    vec3 shadowOrigin = position + geometricNormal * 0.002;
    for (uint index = 0u; index < parameters.lightState.x; ++index)
    {
        PathLight light = lights.values[index];
        float lightType = light.position.w;
        vec3 L;
        float attenuation = 1.0;
        float maxDistance = 100000.0;
        if (lightType < 0.5)
        {
            float directionLength = length(light.direction.xyz);
            if (directionLength <= 1.0e-8)
            {
                continue;
            }
            L = -light.direction.xyz / directionLength;
        }
        else
        {
            vec3 toLight = light.position.xyz - position;
            float distance = length(toLight);
            if (distance <= 1.0e-6)
            {
                continue;
            }
            L = toLight / distance;
            attenuation = RangeWindow(distance, light.attenuation.x) /
                          max(distance * distance, 0.01 * 0.01);
            maxDistance = -1.0;
            if (lightType >= 1.5)
            {
                float directionLength = length(light.direction.xyz);
                if (directionLength <= 1.0e-8)
                {
                    continue;
                }
                float theta = dot(L, -light.direction.xyz / directionLength);
                float innerCosine = light.direction.w;
                float outerCosine = light.attenuation.y;
                attenuation *= clamp((theta - outerCosine) /
                                         max(innerCosine - outerCosine, 0.001),
                                     0.0, 1.0);
            }
        }
        float NdotL = dot(surface.Normal, L);
        if (attenuation <= 0.0 || NdotL <= 0.0 || dot(geometricNormal, L) <= 0.0)
        {
            continue;
        }
        vec3 contribution = EvaluatePathBsdf(surface, L) * NdotL * attenuation *
            light.chromaticityAndIntensity.rgb * light.chromaticityAndIntensity.w;
        bool bVisible = maxDistance < 0.0
            ? IsPointVisible(shadowOrigin, light.position.xyz)
            : IsVisible(shadowOrigin, L, maxDistance);
        if (max(contribution.r, max(contribution.g, contribution.b)) <= 0.0 || !bVisible)
        {
            continue;
        }
        sum += contribution;
    }
    return sum * PreExposure();
}

// 発光三角形上の点の可視性。目標点の少し先まで最も近い命中を求め、それが標本化した
// 発光三角形そのもの（instanceと三角形番号が一致）なら見える。発光面の直前にある別の遮蔽物は、
// 距離の差がどれほど小さくても遮蔽として扱う。
bool IsEmitterPointVisible(vec3 shadowOrigin, vec3 target, uint instanceIndex, uint primitiveIndex)
{
    vec3 toTarget = target - shadowOrigin;
    float targetDistance = length(toTarget);
    if (targetDistance <= 1.0e-6)
    {
        return true;
    }
    payload.Hit = 0u;
    traceRayEXT(scene, gl_RayFlagsOpaqueEXT, 0xffu, 0u, 0u, 0u, shadowOrigin, 0.001,
                toTarget / targetDistance, targetDistance * 1.001 + 0.001, 0);
    return payload.Hit == 0u ||
           (payload.InstanceIndex == instanceIndex && payload.PrimitiveIndex == primitiveIndex);
}

// 発光三角形の光源標本。三角形を通し番号で一様に選び、面上を一様に標本化する。
vec3 SampleEmissiveTriangles(PathSurface surface, vec3 position, vec3 geometricNormal,
                             inout uint state)
{
    uint totalTriangles = parameters.lightState.z;
    uint sampling = SamplingMode();
    if (totalTriangles == 0u || parameters.lightState.y == 0u ||
        sampling == PATH_SAMPLING_BSDF_ONLY)
    {
        return vec3(0.0);
    }
    uint triangleNumber = min(uint(Random01(state) * float(totalTriangles)),
                              totalTriangles - 1u);
    float u1 = Random01(state);
    float u2 = Random01(state);
    // 先頭三角形の通し番号が昇順の表を二分探索する。
    uint low = 0u;
    uint high = parameters.lightState.y - 1u;
    while (low < high)
    {
        uint middle = (low + high + 1u) / 2u;
        if (emissiveInstances.values[middle].z <= triangleNumber)
        {
            low = middle;
        }
        else
        {
            high = middle - 1u;
        }
    }
    uvec4 entry = emissiveInstances.values[low];
    if (triangleNumber < entry.z || triangleNumber - entry.z >= entry.y ||
        entry.x >= parameters.imageState.w)
    {
        return vec3(0.0);
    }
    PathInstance instance = instances.values[entry.x];
    vec3 p0;
    vec3 p1;
    vec3 p2;
    if (!ReadWorldTriangle(instance, triangleNumber - entry.z, p0, p1, p2))
    {
        return vec3(0.0);
    }
    vec3 edgeCross = cross(p1 - p0, p2 - p0);
    float twiceArea = length(edgeCross);
    if (twiceArea <= 1.0e-12)
    {
        return vec3(0.0);
    }
    float rootU1 = sqrt(u1);
    vec3 lightPoint = p0 * (1.0 - rootU1) + p1 * (rootU1 * (1.0 - u2)) + p2 * (rootU1 * u2);
    vec3 toLight = lightPoint - position;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared <= 1.0e-12)
    {
        return vec3(0.0);
    }
    float distance = sqrt(distanceSquared);
    vec3 L = toLight / distance;
    // 発光は両面（命中側と同じ規則）。
    float lightCosine = abs(dot(edgeCross / twiceArea, L));
    float NdotL = dot(surface.Normal, L);
    if (lightCosine <= 1.0e-6 || NdotL <= 0.0 || dot(geometricNormal, L) <= 0.0)
    {
        return vec3(0.0);
    }
    float lightPdf = distanceSquared /
        (lightCosine * 0.5 * twiceArea * float(totalTriangles));
    float weight = sampling == PATH_SAMPLING_LIGHT_ONLY
        ? 1.0
        : PowerHeuristic(lightPdf, PathBsdfPdf(surface, L));
    vec3 emission = instance.emission.rgb * instance.emission.a * PreExposure();
    vec3 contribution = EvaluatePathBsdf(surface, L) * NdotL * emission * (weight / lightPdf);
    if (max(contribution.r, max(contribution.g, contribution.b)) <= 0.0 ||
        !IsEmitterPointVisible(position + geometricNormal * 0.002, lightPoint, entry.x,
                               triangleNumber - entry.z))
    {
        return vec3(0.0);
    }
    return contribution;
}

// BSDF標本で当たった発光三角形の重み。1次命中は常に1。
float EmissionHitWeight(float bsdfPdf, vec3 toHit, vec3 hitNormal, float triangleArea)
{
    uint sampling = SamplingMode();
    if (sampling == PATH_SAMPLING_BSDF_ONLY)
    {
        return 1.0;
    }
    if (sampling == PATH_SAMPLING_LIGHT_ONLY)
    {
        return 0.0;
    }
    float distanceSquared = dot(toHit, toHit);
    float lightCosine = abs(dot(hitNormal, toHit * inversesqrt(max(distanceSquared, 1.0e-24))));
    if (parameters.lightState.z == 0u || triangleArea <= 0.0 || lightCosine <= 1.0e-6)
    {
        return 1.0;
    }
    float lightPdf = distanceSquared /
        (lightCosine * triangleArea * float(parameters.lightState.z));
    return PowerHeuristic(bsdfPdf, lightPdf);
}

// 太陽円盤の光源標本。放射輝度=照度/立体角、pdf=1/立体角なので寄与はf·cos·照度·重み。
vec3 SampleSun(PathSurface surface, vec3 position, vec3 geometricNormal, inout uint state)
{
    uint sampling = SamplingMode();
    if (!IsSunAvailable() || sampling == PATH_SAMPLING_BSDF_ONLY)
    {
        return vec3(0.0);
    }
    vec3 L = SampleSolarDirection(state);
    float NdotL = dot(surface.Normal, L);
    if (NdotL <= 0.0 || dot(geometricNormal, L) <= 0.0)
    {
        return vec3(0.0);
    }
    float weight = sampling == PATH_SAMPLING_LIGHT_ONLY
        ? 1.0
        : PowerHeuristic(1.0 / SunSolidAngle(), PathBsdfPdf(surface, L));
    vec3 contribution = EvaluatePathBsdf(surface, L) * NdotL * parameters.skyState.y * weight;
    if (max(contribution.r, max(contribution.g, contribution.b)) <= 0.0 ||
        !IsVisible(position + geometricNormal * 0.002, L, 100000.0))
    {
        return vec3(0.0);
    }
    return contribution;
}

// 1試料を追跡し、放射輝度（または検証出力）とalphaを返す。alphaは検証mode 252のときだけ
// 1次命中で0.5（ラスタの深度alphaと同じく幾何は1未満、背景は1）、それ以外は常に1。
vec4 TracePixelSample(ivec2 pixel, ivec2 extent, uint sampleIndex)
{
    uint state = uint(pixel.x + 1) * 0x9e3779b9u ^
                 uint(pixel.y + 1) * 0x85ebca6bu ^
                 (sampleIndex + 1u) * 0xc2b2ae35u ^ 0x6a09e667u;
    // 画素内のずれは画素中心の設定でも乱数を同じだけ消費し、以降の乱数列を揃える。
    vec2 pixelOffset = vec2(Random01(state), Random01(state));
    if ((parameters.sampleState.y & 2u) != 0u)
    {
        pixelOffset = vec2(0.5);
    }
    vec2 uv = (vec2(pixel) + pixelOffset) / vec2(extent);
    vec4 farPoint = parameters.inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    if (isnan(farPoint.w) || isinf(farPoint.w) || abs(farPoint.w) < 0.000001)
    {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    vec3 origin = parameters.cameraPosition.xyz;
    vec3 direction = normalize(farPoint.xyz / farPoint.w - origin);
    if ((parameters.sampleState.y & 1u) != 0u)
    {
        // 正射影は画素ごとに近平面上の点から視線方向へ平行に飛ばす（深度は近平面0・遠平面1）。
        vec4 nearPoint = parameters.inverseViewProjection * vec4(uv * 2.0 - 1.0, 0.0, 1.0);
        if (isnan(nearPoint.w) || isinf(nearPoint.w) || abs(nearPoint.w) < 0.000001)
        {
            return vec4(0.0, 0.0, 0.0, 1.0);
        }
        origin = nearPoint.xyz / nearPoint.w;
        direction = normalize(farPoint.xyz / farPoint.w - origin);
    }
    if (any(isnan(direction)) || any(isinf(direction)))
    {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    float coverageAlpha = 1.0;
    vec3 throughput = vec3(1.0);
    vec3 radiance = vec3(0.0);
    uint debugOutput = uint(parameters.exposureAndDebug.y + 0.5);
    vec3 debugValue = vec3(0.0);
    uint bsdfMode = BsdfMode();
    // 直前の頂点でBSDF標本を引いたときの立体角pdf（発光命中と太陽のMIS重みに使う）。
    float previousBsdfPdf = 0.0;

    for (uint bounce = 0u; bounce < 8u; ++bounce)
    {
        payload.Hit = 0u;
        traceRayEXT(scene, gl_RayFlagsOpaqueEXT, 0xffu, 0u, 0u, 0u,
                    origin, 0.001, direction, 100000.0, 0);
        if (payload.Hit == 0u)
        {
            radiance += throughput * MissRadiance(direction, bounce == 0u, previousBsdfPdf);
            break;
        }

        // 影レイが同じpayloadを使うため、命中面の値を先に取り出す。
        if (bounce == 0u && parameters.sampleState.z != 0u)
        {
            coverageAlpha = 0.5;
        }
        vec3 surfacePosition = payload.Position;
        vec3 geometricNormal = payload.GeometricNormal;
        vec3 shadingNormal = payload.ShadingNormal;
        vec3 surfaceAlbedo = clamp(payload.Albedo, vec3(0.0), vec3(1.0));
        float surfaceMetallic = payload.Metallic;
        float surfaceRoughness = payload.Roughness;
        float triangleArea = payload.TriangleArea;
        // 発光はGBufferと同じく色×nitsの物理値に、カメラのプリエクスポージャを掛ける。
        vec3 surfaceEmission = payload.Emission * PreExposure();
        if (bounce == 0u && debugOutput != PATH_DEBUG_NONE)
        {
            debugValue = debugOutput == PATH_DEBUG_ALBEDO ? payload.Albedo :
                         debugOutput == PATH_DEBUG_SHADING_NORMAL ? payload.ShadingNormal :
                         debugOutput == PATH_DEBUG_HIT_DISTANCE
                             ? vec3(distance(origin, surfacePosition))
                             : vec3(payload.Metallic, payload.Roughness, 0.0);
            break;
        }
        ApplyFogSegment(origin, direction, surfacePosition, throughput, radiance);
        if (max(surfaceEmission.r, max(surfaceEmission.g, surfaceEmission.b)) > 0.0)
        {
            float emissionWeight = bounce == 0u
                ? 1.0
                : EmissionHitWeight(previousBsdfPdf, surfacePosition - origin,
                                    geometricNormal, triangleArea);
            radiance += throughput * surfaceEmission * emissionWeight;
        }

        // 視線がシェーディング法線の裏または接平面上にある（法線マップや頂点法線の補間で起きる）
        // ときは、幾何法線をシェーディング法線に使う。評価・pdf・標本化を同じ表側の法線で行う。
        // 幾何法線でも接平面上なら散乱と光源標本を打ち切る（BSDFのNdotVの下限に掛からないようにする）。
        if (dot(shadingNormal, -direction) < 1.0e-6)
        {
            shadingNormal = geometricNormal;
        }
        if (dot(shadingNormal, -direction) < 1.0e-6)
        {
            break;
        }
        PathSurface surface = MakePathSurface(shadingNormal, -direction, surfaceAlbedo,
                                              surfaceMetallic, surfaceRoughness, bsdfMode);
        radiance += throughput * EvaluatePunctualLights(surface, surfacePosition, geometricNormal);
        radiance += throughput * SampleEmissiveTriangles(surface, surfacePosition,
                                                         geometricNormal, state);
        radiance += throughput * SampleSun(surface, surfacePosition, geometricNormal, state);

        // 輸送範囲を限る参照: 直接光だけなら1次命中の光源標本で、拡散1バウンスなら2つ目の命中の
        // 光源標本で打ち切る（発光三角形と太陽円盤は光源標本だけなので、散乱光線の命中側では数えない）。
        uint transportScope = TransportScope();
        if (transportScope == PATH_TRANSPORT_DIRECT_ONLY ||
            (transportScope == PATH_TRANSPORT_SINGLE_DIFFUSE_BOUNCE && bounce >= 1u))
        {
            break;
        }

        vec3 u = vec3(Random01(state), Random01(state), Random01(state));
        vec3 nextDirection;
        float bsdfPdf;
        if (transportScope == PATH_TRANSPORT_SINGLE_DIFFUSE_BOUNCE)
        {
            // 拡散葉だけをコサイン分布で標本化する。重みは拡散のBRDF×π（拡散葉の方向反射率）。
            float phi = 2.0 * PI * u.y;
            float radius = sqrt(u.z);
            nextDirection = normalize(surface.Tangent * (radius * cos(phi)) +
                                      surface.Bitangent * (radius * sin(phi)) +
                                      surface.Normal * sqrt(max(0.0, 1.0 - u.z)));
            float cosine = dot(surface.Normal, nextDirection);
            if (cosine <= 0.0 || dot(nextDirection, geometricNormal) <= 0.0)
            {
                break;
            }
            bsdfPdf = cosine / PI;
            throughput *= surface.DiffuseBrdf * PI;
        }
        else
        {
            // 面の裏へ抜ける方向は光漏れになるため経路を打ち切る。
            if (!SamplePathBsdf(surface, u, nextDirection) ||
                dot(nextDirection, geometricNormal) <= 0.0)
            {
                break;
            }
            bsdfPdf = PathBsdfPdf(surface, nextDirection);
            if (!(bsdfPdf > 0.0))
            {
                break;
            }
            throughput *= EvaluatePathBsdf(surface, nextDirection) *
                          (dot(surface.Normal, nextDirection) / bsdfPdf);
        }
        if (any(isnan(throughput)) || any(isinf(throughput)) ||
            max(throughput.r, max(throughput.g, throughput.b)) <= 0.0)
        {
            break;
        }
        if (bounce >= 3u)
        {
            float survival = clamp(max(throughput.r, max(throughput.g, throughput.b)),
                                   0.05, 0.95);
            if (Random01(state) > survival)
            {
                break;
            }
            throughput /= survival;
        }
        previousBsdfPdf = bsdfPdf;
        origin = surfacePosition + geometricNormal * 0.002;
        direction = nextDirection;
    }

    if (debugOutput != PATH_DEBUG_NONE)
    {
        radiance = debugValue;
    }
    if (any(isnan(radiance)) || any(isinf(radiance)))
    {
        radiance = vec3(0.0);
    }
    // 累積画像はfloat32。太陽円盤などの大きな試料値を切ると期待値が偏るため、有限値は切らない。
    radiance = debugOutput == PATH_DEBUG_SHADING_NORMAL ? clamp(radiance, vec3(-1.0), vec3(1.0))
                                                        : max(radiance, vec3(0.0));
    return vec4(radiance, coverageAlpha);
}

void main()
{
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    ivec2 extent = imageSize(currentAverage);
    if (any(greaterThanEqual(pixel, extent)))
    {
        return;
    }

    // このdispatchでsamplesPerFrame試料を引き、前回までの平均と試料数の比で合成する。
    uint baseSample = parameters.imageState.z;
    uint samplesPerFrame = max(parameters.sampleState.x, 1u);
    vec4 sum = vec4(0.0);
    for (uint sampleOffset = 0u; sampleOffset < samplesPerFrame; ++sampleOffset)
    {
        sum += TracePixelSample(pixel, extent, baseSample + sampleOffset);
    }
    vec4 average = sum / float(samplesPerFrame);
    if (baseSample > 0u)
    {
        vec4 previous = texelFetch(previousAverage, pixel, 0);
        average = previous + (average - previous) *
            (float(samplesPerFrame) / float(baseSample + samplesPerFrame));
    }
    imageStore(currentAverage, pixel, average);
}
