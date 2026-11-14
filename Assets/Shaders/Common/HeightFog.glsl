// 高さフォグの解析の透過率・霧の色・前向きの物への評価と合成（記述子・UBO を宣言しない純関数だけ。体積の霧と前向きのシェーダーが同じ式を使う）。
// 定数の並びは C++ の GPUHeightFogParams（Library/Core/Public/Rendering/HeightFogGpuParams.h）と同じ。
#ifndef HEIGHT_FOG_GLSL
#define HEIGHT_FOG_GLSL

// GPUHeightFogParams と同じ並びの std140 の 5 × vec4（80 バイト）。
struct HeightFogParams
{
    vec4 densityBaseHeightFalloffSkyAvailable; // x=基準の高さの密度（無効なら 0）, y=基準の高さ, z=高さの減衰, w=空の放射輝度 LUT が使えれば 1
    vec4 tintAndSkyBlend;                      // rgb=霧の色味（既定 1）, w=空への寄せ（既定 0）
    vec4 fallbackColorAndAmbientScale;         // rgb=LUT が無いときの霧の色, w=周りの光の倍率
    vec4 cameraPositionAndPreExposure;         // xyz=カメラの位置（ワールド）, w=プリエクスポージャ
    vec4 maxDistanceAndReserved;               // x=霧を積む距離の上限（m、0 以下で視線全体）, yzw=予約
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

vec3 HeightFogRadiance(sampler2D skyRadiance, bool bSkyAvailable, vec3 fallbackColor,
                       vec3 rayDir, vec4 tintBlend)
{
    vec3 radiance = fallbackColor;
    if (bSkyAvailable)
    {
        radiance = texture(skyRadiance, HeightFogEquirectUV(rayDir)).rgb;
    }
    // tintBlend.w は G7-GR50-FOG-TINT が基準の仰角の LUT と混ぜる際に使う。
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
                                            rayDir, params.tintAndSkyBlend) *
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

#endif // HEIGHT_FOG_GLSL
