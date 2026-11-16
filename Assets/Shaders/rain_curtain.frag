// HDR のシーンに、遠景の雨のカーテンを層ごとに重ねる。
// 深度と仰角で雨を薄め、高さフォグと同じ周りの光の色を使う。
#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "Common/DepthConvention.glsl"
#include "Common/HeightFog.glsl"

// 雨を薄め始める仰角と、完全に消す仰角の正弦。
const float kRainCurtainFadeStartSin = 0.25;
const float kRainCurtainFadeEndSin = 0.7;
// 列の角度幅と、縦の周期のうち筋が占める割合。
const float kRainCurtainColumnAngle = 0.003;
const float kRainCurtainDuty = 0.35;
// 方位を一周する角度と、真上・真下付近の水平成分の下限。
const float kRainCurtainTwoPi = 6.28318530717958647692;
const float kRainCurtainMinHorizontal = 1.0e-4;
// 層の距離に対する筋の長さの範囲と、縦の端をぼかす周期の割合。
const float kRainCurtainLengthMin = 0.02;
const float kRainCurtainLengthMax = 0.06;
const float kRainCurtainDashEdge = 0.01;
// 列の中央から、横方向にぼかし始める位置と完全に消える位置。
const float kRainCurtainProfileInner = 0.15;
const float kRainCurtainProfileOuter = 0.5;
// 層の距離に対し、深度による表示を開始・完了する倍率。
const float kRainCurtainVisibilityStart = 0.8;
const float kRainCurtainVisibilityEnd = 1.2;
// ハッシュの上位 24 ビットを 0 以上 1 未満の値に変換する倍率。
const float kRainCurtainHashScale = 1.0 / 16777216.0;

layout(set = 0, binding = 0) uniform sampler2D sceneDepthTexture;
layout(set = 0, binding = 1) uniform sampler2D skyRadianceTexture;

layout(std140, set = 0, binding = 2) uniform RainCurtainParams
{
    mat4 cameraRelativeInverseViewProjection;
    vec4 depthLinearization;
    HeightFogParams fog;
    vec4 layerDistance;
    vec4 layerOpacity;
    vec4 layerScroll;
    vec4 windShear;
} params;

// 列と層の番号を混ぜて、各筋に使う整数を作る。
uint RainCurtainHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RainCurtainHashUnit(uint value)
{
    return float(RainCurtainHash(value) >> 8u) * kRainCurtainHashScale;
}

void main()
{
    float depth = textureLod(sceneDepthTexture, fragUV, 0.0).r;
    bool bSky = !HeightFogIsFinite(depth) || IsBackgroundDepth(depth);

    vec4 p = params.cameraRelativeInverseViewProjection *
             vec4(fragUV * 2.0 - 1.0, kDepthNearest, 1.0);
    if (!HeightFogIsFinite(p.w) || p.w == 0.0)
    {
        discard;
    }

    vec3 viewVector = p.xyz / p.w;
    float viewLength = length(viewVector);
    if (!HeightFogIsFinite(viewLength) || viewLength <= 0.0)
    {
        discard;
    }
    vec3 dir = viewVector / viewLength;

    float sceneDistance = 0.0;
    if (!bSky)
    {
        sceneDistance = params.depthLinearization.x /
                        LinearDepthDenominator(depth, params.depthLinearization.y);
    }

    float elevationFade = 1.0 - smoothstep(kRainCurtainFadeStartSin,
                                            kRainCurtainFadeEndSin, dir.y);
    if (!(elevationFade > 0.0))
    {
        discard;
    }

    float horiz = max(length(dir.xz), kRainCurtainMinHorizontal);
    // 真下では方位が決まらないため、正の X 方向を使う。
    float u = dir.x == 0.0 && dir.z == 0.0
                  ? 0.0
                  : atan(dir.z, dir.x) / kRainCurtainTwoPi;
    vec2 tangent = vec2(-dir.z, dir.x) / horiz;
    float shear = dot(params.windShear.xy, tangent);
    float columnCount = max(1.0, round(kRainCurtainTwoPi / kRainCurtainColumnAngle));
    float transmittance = 1.0;

    for (int layerIndex = 0; layerIndex < 3; ++layerIndex)
    {
        float distance = params.layerDistance[layerIndex];
        float opacity = params.layerOpacity[layerIndex];
        if (opacity <= 0.0 || distance <= 0.0)
        {
            continue;
        }

        float v = dir.y / horiz * distance;
        float columnMeters = distance * kRainCurtainColumnAngle;
        float column = (u + 0.5) * columnCount + shear * v / columnMeters;
        uint columnIndex = uint(mod(floor(column), columnCount));
        uint seed = columnIndex ^ (uint(layerIndex) * 0x9e3779b9u);
        float h1 = RainCurtainHashUnit(seed);
        float h2 = RainCurtainHashUnit(seed ^ 0x68bc21ebu);

        float streakLength = distance * mix(kRainCurtainLengthMin, kRainCurtainLengthMax, h1);
        float period = streakLength / kRainCurtainDuty;
        // 上向きの高さにスクロール量を足し、筋を下向きに動かす。
        float along = fract((v + params.layerScroll[layerIndex]) / period + h2);
        float dash = smoothstep(0.0, kRainCurtainDashEdge, along) *
                     (1.0 - smoothstep(kRainCurtainDuty - kRainCurtainDashEdge,
                                       kRainCurtainDuty, along));
        float profile = 1.0 - smoothstep(kRainCurtainProfileInner,
                                         kRainCurtainProfileOuter, abs(fract(column) - 0.5));
        float visibility = bSky ? 1.0 :
                           smoothstep(distance * kRainCurtainVisibilityStart,
                                      distance * kRainCurtainVisibilityEnd, sceneDistance);
        float layerAlpha = opacity * dash * profile * visibility * elevationFade;
        transmittance *= 1.0 - clamp(layerAlpha, 0.0, 1.0);
    }

    float alpha = 1.0 - transmittance;
    if (!HeightFogIsFinite(alpha) || alpha <= 0.0)
    {
        discard;
    }

    vec3 color = HeightFogRadiance(skyRadianceTexture,
                                    params.fog.densityBaseHeightFalloffSkyAvailable.w > 0.5,
                                    params.fog.fallbackColorAndAmbientScale.rgb,
                                    dir, params.fog.tintAndSkyBlend,
                                    params.fog.referenceElevationSinCos) *
                 (params.fog.fallbackColorAndAmbientScale.w *
                  params.fog.cameraPositionAndPreExposure.w);
    outColor = vec4(color, alpha);
}
