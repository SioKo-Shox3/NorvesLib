// 高さフォグの解析の透過率・霧の色・前向きの物への評価と合成（記述子・UBO を宣言しない純関数だけ。体積の霧と前向きのシェーダーが同じ式を使う）。
// 楕円体の局所霧の解析の透過率と、風で流れる濃淡を散乱の放射だけに掛ける共通関数。
// 定数の並びは C++ の GPUHeightFogParams（Library/Core/Public/Rendering/HeightFogGpuParams.h）と同じ。
#ifndef HEIGHT_FOG_GLSL
#define HEIGHT_FOG_GLSL

#include "Common/Wind.glsl"

// GPUHeightFogParams と同じ並びの std140 の 6 × vec4（96 バイト）。
struct HeightFogParams
{
    vec4 densityBaseHeightFalloffSkyAvailable; // x=基準の高さの密度（無効なら 0）, y=基準の高さ, z=高さの減衰, w=空の放射輝度 LUT が使えれば 1
    vec4 tintAndSkyBlend;                      // rgb=霧の色味（既定 1）, w=空への寄せ（既定 0）
    vec4 fallbackColorAndAmbientScale;         // rgb=LUT が無いときの霧の色, w=周りの光の倍率
    vec4 cameraPositionAndPreExposure;         // xyz=カメラの位置（ワールド）, w=プリエクスポージャ
    vec4 maxDistanceAndReserved;               // x=霧を積む距離の上限（m、0 以下で視線全体）, yzw=予約
    vec4 referenceElevationSinCos;             // x=基準の仰角の正弦, y=余弦, zw=予約
};

// 前向きの物 1 点に掛ける高さフォグの結果。
struct HeightFogForwardResult
{
    float Transmittance; // カメラからその点までの霧の透過率
    vec3 Radiance;       // 事前露出を掛けた霧の光
};

bool HeightFogIsFinite(float value)
{
    return !isnan(value) && !isinf(value);
}

vec2 HeightFogEquirectUV(vec3 direction)
{
    vec2 uv = vec2(atan(direction.z, direction.x),
                   asin(clamp(-direction.y, -1.0, 1.0)));
    uv *= vec2(0.15915494, 0.31830989);
    uv += 0.5;
    return uv;
}

float HeightFogTransmittance(float density, float baseHeight, float falloff,
                             float originHeight, float rayDirY, float rayDistance)
{
    float clampedDensity = max(density, 0.0);
    float clampedFalloff = max(falloff, 0.0);
    if (clampedDensity <= 0.0 || rayDistance <= 0.0 ||
        !HeightFogIsFinite(originHeight) || !HeightFogIsFinite(rayDirY) ||
        !HeightFogIsFinite(rayDistance))
    {
        return 1.0;
    }

    float originExponent = -clampedFalloff * (originHeight - baseHeight);
    float verticalRate = clampedFalloff * clamp(rayDirY, -1.0, 1.0);
    float exponentChange = verticalRate * rayDistance;
    float logIntegral;
    if (abs(verticalRate) < 1.0e-8 || abs(exponentChange) < 1.0e-4)
    {
        logIntegral = log(rayDistance);
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

    float logOpticalDepth = log(clampedDensity) + originExponent + logIntegral;
    if (!HeightFogIsFinite(logOpticalDepth))
    {
        return logOpticalDepth > 0.0 ? exp(-80.0) : 1.0;
    }
    if (logOpticalDepth >= log(80.0))
    {
        return exp(-80.0);
    }

    float opticalDepth = clamp(exp(logOpticalDepth), 0.0, 80.0);
    return exp(-opticalDepth);
}

// 視線の方位のまま、仰角を基準の仰角へ傾けた単位ベクトル。真上・真下の視線は方位が決まらないので +X の方位を使う。
vec3 HeightFogReferenceDirection(vec3 rayDir, vec4 referenceElevationSinCos)
{
    vec2 horizontal = rayDir.xz;
    float horizontalLength = length(horizontal);
    horizontal = horizontalLength > 1.0e-6 ? horizontal / horizontalLength : vec2(1.0, 0.0);
    return vec3(horizontal.x * referenceElevationSinCos.y,
                referenceElevationSinCos.x,
                horizontal.y * referenceElevationSinCos.y);
}

// 視線方向と基準の仰角方向の空の放射輝度を SkyBlend で混ぜ、Tint を掛けて負を 0 にする。
// LUT が無いときは既定の色に Tint だけを掛け、SkyBlend が 0 なら基準方向の LUT を引かない。
vec3 HeightFogRadiance(sampler2D skyRadiance, bool bSkyAvailable, vec3 fallbackColor,
                       vec3 rayDir, vec4 tintBlend, vec4 referenceElevationSinCos)
{
    vec3 radiance = fallbackColor;
    if (bSkyAvailable)
    {
        radiance = texture(skyRadiance, HeightFogEquirectUV(rayDir)).rgb;
        float skyBlend = clamp(tintBlend.w, 0.0, 1.0);
        if (skyBlend > 0.0)
        {
            vec3 referenceRadiance = texture(skyRadiance,
                HeightFogEquirectUV(HeightFogReferenceDirection(rayDir, referenceElevationSinCos))).rgb;
            radiance = mix(radiance, referenceRadiance, skyBlend);
        }
    }
    return max(radiance * tintBlend.rgb, vec3(0.0));
}

// 前向きの物 1 点の高さフォグを求める。
// 画素ごとの 32 歩の積分を避けるため、空気遠近と太陽の単一散乱は含めない。
HeightFogForwardResult EvaluateHeightFogForward(HeightFogParams params, sampler2D skyRadiance, vec3 worldPos)
{
    HeightFogForwardResult result;
    result.Transmittance = 1.0;
    result.Radiance = vec3(0.0);

    vec3 viewVector = worldPos - params.cameraPositionAndPreExposure.xyz;
    float rayDistance = length(viewVector);
    if (!HeightFogIsFinite(rayDistance) || rayDistance <= 0.0)
    {
        return result;
    }
    vec3 rayDir = viewVector / rayDistance;
    float maxDistance = params.maxDistanceAndReserved.x;
    float fogDistance = maxDistance > 0.0 ? min(rayDistance, maxDistance) : rayDistance;
    vec4 p = params.densityBaseHeightFalloffSkyAvailable;
    result.Transmittance = HeightFogTransmittance(p.x, p.y, p.z,
                                                 params.cameraPositionAndPreExposure.y,
                                                 rayDir.y, fogDistance);
    float opacity = clamp(1.0 - result.Transmittance, 0.0, 1.0);
    if (opacity > 0.0)
    {
        result.Radiance = HeightFogRadiance(skyRadiance, p.w > 0.5,
                                            params.fallbackColorAndAmbientScale.rgb,
                                            rayDir, params.tintAndSkyBlend, params.referenceElevationSinCos) *
                          (opacity * params.fallbackColorAndAmbientScale.w *
                           params.cameraPositionAndPreExposure.w);
    }
    return result;
}

vec4 ApplyHeightFogStraightAlpha(vec4 color, HeightFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance + fog.Radiance, color.a);
}

vec4 ApplyHeightFogPremultiplied(vec4 color, HeightFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance + color.a * fog.Radiance, color.a);
}

vec4 ApplyHeightFogAdditive(vec4 color, HeightFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance, color.a);
}

// GPULocalFogVolume は 80 バイト、GPULocalFogParams は std140 で 688 バイトの同じ並び。
const int kLocalFogMaxVolumes = 8;

struct LocalFogVolume
{
    vec4 centerAndDensity; // xyz=中心（ワールド、m）, w=中心の消散係数（1/m）
    vec4 inverseAxisX;     // xyz=回転したローカルの X 軸 ÷ s_x（1/m）, w=濃淡の量（0..1）
    vec4 inverseAxisY;     // xyz=回転したローカルの Y 軸 ÷ s_y, w=濃淡の雑音の周波数（1/m）
    vec4 inverseAxisZ;     // xyz=回転したローカルの Z 軸 ÷ s_z, w=0
    vec4 colorAndReserved; // rgb=散乱の色, w=0
};

struct LocalFogParams
{
    vec4 countAndWindClock;      // x=個数（0..8）, y=風の時計（秒）, z=突風で流れをずらす時間（秒）, w=風が有効なら 1
    vec4 windDirectionSpeedGust; // xy=風の向き（XZ の単位ベクトル）, z=平均風速（m/s）, w=突風の比
    vec4 windGustShape;          // x=突風の周波数（Hz）, y=突風の塊の大きさ（m）, z=乱流の比, w=0
    LocalFogVolume volumes[kLocalFogMaxVolumes];
};

int LocalFogCount(LocalFogParams p)
{
    return clamp(int(p.countAndWindClock.x + 0.5), 0, kLocalFogMaxVolumes);
}

bool LocalFogIsFinite(float value)
{
    return !isnan(value) && !isinf(value);
}

bool LocalFogIsFinite(vec3 value)
{
    return !any(isnan(value)) && !any(isinf(value));
}

float LocalFogErf(float x)
{
    float t = 1.0 / (1.0 + 0.3275911 * abs(x));
    float y = 1.0 - (((((1.061405429 * t - 1.453152027) * t +
                        1.421413741) * t - 0.284496736) * t +
                        0.254829592) * t) * exp(-x * x);
    return sign(x) * y;
}

// ガウス密度の線積分。雑音と風は消散に含めず、放射の評価だけで使う。
float LocalFogVolumeOpticalDepth(LocalFogVolume v, vec3 origin, vec3 dir, float t0, float t1)
{
    float density = v.centerAndDensity.w;
    if (!LocalFogIsFinite(t0) || !LocalFogIsFinite(t1) || t1 <= t0 ||
        !LocalFogIsFinite(density) || density <= 0.0 ||
        !LocalFogIsFinite(origin) || !LocalFogIsFinite(dir))
    {
        return 0.0;
    }

    vec3 relativeOrigin = origin - v.centerAndDensity.xyz;
    vec3 a = vec3(dot(v.inverseAxisX.xyz, relativeOrigin),
                  dot(v.inverseAxisY.xyz, relativeOrigin),
                  dot(v.inverseAxisZ.xyz, relativeOrigin));
    vec3 b = vec3(dot(v.inverseAxisX.xyz, dir),
                  dot(v.inverseAxisY.xyz, dir),
                  dot(v.inverseAxisZ.xyz, dir));
    float B = dot(b, b);
    float aa = dot(a, a);
    if (!LocalFogIsFinite(a) || !LocalFogIsFinite(b) ||
        !LocalFogIsFinite(B) || !LocalFogIsFinite(aa))
    {
        return 0.0;
    }

    float opticalDepth;
    if (B < 1.0e-12)
    {
        opticalDepth = density * exp(-0.5 * aa) * (min(t1, 1.0e7) - t0);
    }
    else
    {
        float ab = dot(a, b);
        float tau0 = ab / B;
        float hSquared = max(aa - ab * ab / B, 0.0);
        float k = sqrt(0.5 * B);
        float endErf = t1 >= 1.0e30 ? 1.0 : LocalFogErf(k * (t1 + tau0));
        opticalDepth = density * exp(-0.5 * hSquared) * sqrt(3.141592653589793 / (2.0 * B)) *
                       (endErf - LocalFogErf(k * (t0 + tau0)));
    }
    return LocalFogIsFinite(opticalDepth) ? max(opticalDepth, 0.0) : 0.0;
}

float LocalFogOpticalDepth(LocalFogParams p, vec3 origin, vec3 dir, float t0, float t1)
{
    float opticalDepth = 0.0;
    int count = LocalFogCount(p);
    for (int i = 0; i < count; ++i)
    {
        opticalDepth += LocalFogVolumeOpticalDepth(p.volumes[i], origin, dir, t0, t1);
    }
    return opticalDepth;
}

// 3σ の楕円体に入る区間。交わらない場合は始点が終点より後になる。
vec2 LocalFogVolumeRange(LocalFogVolume v, vec3 origin, vec3 dir)
{
    vec3 relativeOrigin = origin - v.centerAndDensity.xyz;
    vec3 a = vec3(dot(v.inverseAxisX.xyz, relativeOrigin),
                  dot(v.inverseAxisY.xyz, relativeOrigin),
                  dot(v.inverseAxisZ.xyz, relativeOrigin));
    vec3 b = vec3(dot(v.inverseAxisX.xyz, dir),
                  dot(v.inverseAxisY.xyz, dir),
                  dot(v.inverseAxisZ.xyz, dir));
    float B = dot(b, b);
    float aa = dot(a, a);
    float ab = dot(a, b);
    if (!LocalFogIsFinite(a) || !LocalFogIsFinite(b) || !LocalFogIsFinite(B) ||
        !LocalFogIsFinite(aa) || !LocalFogIsFinite(ab))
    {
        return vec2(1.0, 0.0);
    }
    if (B == 0.0)
    {
        return aa <= 9.0 ? vec2(-1.0e30, 1.0e30) : vec2(1.0, 0.0);
    }
    float discriminant = ab * ab - B * (aa - 9.0);
    if (!LocalFogIsFinite(discriminant) || discriminant < 0.0)
    {
        return vec2(1.0, 0.0);
    }
    float root = sqrt(discriminant);
    return vec2((-ab - root) / B, (-ab + root) / B);
}

vec3 LocalFogFlowOffset(LocalFogParams p, vec3 center)
{
    if (p.countAndWindClock.w <= 0.5)
    {
        return vec3(0.0);
    }
    vec2 dir = p.windDirectionSpeedGust.xy;
    float t = p.countAndWindClock.y;
    vec3 meanVelocity = vec3(dir.x, 0.0, dir.y) * p.windDirectionSpeedGust.z;
    vec3 velocity = WindVelocity(center, t, dir, p.windDirectionSpeedGust.z,
                                p.windDirectionSpeedGust.w, p.windGustShape.x,
                                p.windGustShape.y, p.windGustShape.z);
    return meanVelocity * t + (velocity - meanVelocity) * p.countAndWindClock.z;
}

float LocalFogLatticeValue(ivec3 cell)
{
    uint hash = WindHash(uint(cell.x) ^ WindHash(uint(cell.y) ^ WindHash(uint(cell.z))));
    return float(hash >> 8) * (1.0 / 16777216.0);
}

// 格子の 8 頂点を 5 次の補間でつなぐ 1 オクターブの値の雑音。
float LocalFogValueNoise3D(vec3 x)
{
    vec3 cell = floor(x);
    vec3 f = x - cell;
    vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    ivec3 c = ivec3(cell);
    float z0 = mix(mix(LocalFogLatticeValue(c), LocalFogLatticeValue(c + ivec3(1, 0, 0)), u.x),
                   mix(LocalFogLatticeValue(c + ivec3(0, 1, 0)),
                       LocalFogLatticeValue(c + ivec3(1, 1, 0)), u.x), u.y);
    float z1 = mix(mix(LocalFogLatticeValue(c + ivec3(0, 0, 1)),
                       LocalFogLatticeValue(c + ivec3(1, 0, 1)), u.x),
                   mix(LocalFogLatticeValue(c + ivec3(0, 1, 1)),
                       LocalFogLatticeValue(c + ivec3(1, 1, 1)), u.x), u.y);
    return mix(z0, z1, u.z);
}

float LocalFogNoise3D(vec3 x)
{
    return clamp(0.65 * LocalFogValueNoise3D(x) + 0.35 * LocalFogValueNoise3D(x * 2.03), 0.0, 1.0);
}

float LocalFogNoiseFactor(LocalFogParams p, LocalFogVolume v, vec3 position)
{
    float amount = v.inverseAxisX.w;
    if (amount <= 0.0)
    {
        return 1.0;
    }
    float n = LocalFogNoise3D((position - LocalFogFlowOffset(p, v.centerAndDensity.xyz)) * v.inverseAxisY.w);
    return max(1.0 + amount * (2.0 * n - 1.0), 0.0);
}

struct LocalFogForwardResult
{
    float Transmittance;
    vec3 Radiance; // 事前露出を掛けた霧の光
};

LocalFogForwardResult EvaluateLocalFogForward(LocalFogParams lf, HeightFogParams hf,
                                             sampler2D skyRadiance, vec3 worldPos)
{
    LocalFogForwardResult result;
    result.Transmittance = 1.0;
    result.Radiance = vec3(0.0);
    int count = LocalFogCount(lf);
    if (count == 0)
    {
        return result;
    }
    vec3 camera = hf.cameraPositionAndPreExposure.xyz;
    vec3 viewVector = worldPos - camera;
    float rayDistance = length(viewVector);
    if (!LocalFogIsFinite(rayDistance) || rayDistance <= 0.0)
    {
        return result;
    }
    vec3 dir = viewVector / rayDistance;
    vec4 heightParams = hf.densityBaseHeightFalloffSkyAvailable;
    vec3 ambient = HeightFogRadiance(skyRadiance, heightParams.w > 0.5,
                                     hf.fallbackColorAndAmbientScale.rgb, dir,
                                     vec4(1.0, 1.0, 1.0, hf.tintAndSkyBlend.w),
                                     hf.referenceElevationSinCos) * hf.fallbackColorAndAmbientScale.w;
    float opticalDepth = 0.0;
    vec3 weightedRadiance = vec3(0.0);
    for (int i = 0; i < count; ++i)
    {
        LocalFogVolume v = lf.volumes[i];
        float volumeDepth = LocalFogVolumeOpticalDepth(v, camera, dir, 0.0, rayDistance);
        if (volumeDepth <= 0.0)
        {
            continue;
        }
        vec3 relativeOrigin = camera - v.centerAndDensity.xyz;
        vec3 a = vec3(dot(v.inverseAxisX.xyz, relativeOrigin),
                      dot(v.inverseAxisY.xyz, relativeOrigin),
                      dot(v.inverseAxisZ.xyz, relativeOrigin));
        vec3 b = vec3(dot(v.inverseAxisX.xyz, dir),
                      dot(v.inverseAxisY.xyz, dir),
                      dot(v.inverseAxisZ.xyz, dir));
        float B = dot(b, b);
        float closestDistance = B > 0.0 ? clamp(-dot(a, b) / B, 0.0, rayDistance) : 0.0;
        vec3 closestPosition = camera + dir * closestDistance;
        float noiseFactor = LocalFogNoiseFactor(lf, v, closestPosition);
        float maxDistance = hf.maxDistanceAndReserved.x;
        float heightDistance = maxDistance > 0.0 ? min(closestDistance, maxDistance) : closestDistance;
        float heightTransmittance = HeightFogTransmittance(heightParams.x, heightParams.y, heightParams.z,
                                                          camera.y, dir.y, heightDistance);
        opticalDepth += volumeDepth;
        weightedRadiance += volumeDepth * v.colorAndReserved.rgb * noiseFactor * heightTransmittance;
    }
    result.Transmittance = exp(-min(opticalDepth, 80.0));
    if (opticalDepth > 0.0)
    {
        result.Radiance = (1.0 - result.Transmittance) * (weightedRadiance / opticalDepth) *
                          ambient * hf.cameraPositionAndPreExposure.w;
    }
    return result;
}

// 高さフォグを掛けた色に対し、局所霧を手前に置いて合成する。
vec4 ApplyLocalFogStraightAlpha(vec4 color, LocalFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance + fog.Radiance, color.a);
}

vec4 ApplyLocalFogPremultiplied(vec4 color, LocalFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance + color.a * fog.Radiance, color.a);
}

vec4 ApplyLocalFogAdditive(vec4 color, LocalFogForwardResult fog)
{
    return vec4(color.rgb * fog.Transmittance, color.a);
}

#endif // HEIGHT_FOG_GLSL
