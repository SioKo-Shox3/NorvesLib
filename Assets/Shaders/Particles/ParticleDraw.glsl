// GPU の粒の描画パスとシェーダーが共有する資源・定数・見た目の評価を定義する。
#ifndef NORVES_PARTICLE_DRAW_GLSL
#define NORVES_PARTICLE_DRAW_GLSL

#include "Particles/ParticleCommon.glsl"
#include "Common/HeightFog.glsl"
#include "Common/DepthConvention.glsl"

// GpuParticleDraw.h の kGpuParticleDrawBindingConstants（0）と同じ並び。
layout(std140, set = 0, binding = 0) uniform GpuParticleDrawConstants
{
    mat4 view;
    mat4 projection;
    vec4 cameraPositionPreExposure;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraForward;
    vec4 cameraVelocity;
    vec4 targetSizeInvSize;
    vec4 nearFarParams;
    vec4 ambientColorIblIntensity;
    uvec4 flags;
    HeightFogParams heightFog;
} drawConstants;

// GpuParticleDraw.h の kGpuParticleDrawBindingPool（1）。
layout(std430, set = 0, binding = 1) readonly buffer GpuParticleDrawPool
{
    GpuParticle particles[];
} drawPool;

// GpuParticleDraw.h の kGpuParticleDrawBindingEmitters（2）。
layout(std430, set = 0, binding = 2) readonly buffer GpuParticleDrawEmitters
{
    GpuEmitterParams emitters[];
} drawEmitters;

// GpuParticleDraw.h の kGpuParticleDrawBindingSlotToEmitter（3）。
layout(std430, set = 0, binding = 3) readonly buffer GpuParticleDrawSlotToEmitter
{
    uint slotToEmitter[];
} drawSlotToEmitter;

// GpuParticleDraw.h の kGpuParticleDrawBindingCurveKeys（4）。
layout(std430, set = 0, binding = 4) readonly buffer GpuParticleDrawCurveKeys
{
    GpuCurveKey keys[];
} drawCurveKeys;

// GpuParticleDraw.h の kGpuParticleDrawBindingSceneDepth（5）。
layout(set = 0, binding = 5) uniform sampler2D sceneDepthTexture;
// GpuParticleDraw.h の kGpuParticleDrawBindingDiffuseIrradiance（6）。
layout(set = 0, binding = 6) uniform sampler2D diffuseIrradianceTexture;
// GpuParticleDraw.h の kGpuParticleDrawBindingHeightFogSky（7）。
layout(set = 0, binding = 7) uniform sampler2D heightFogSkyTexture;

// GpuParticleDraw.h の kGpuParticleDrawBindingDrawList（8）。
layout(std430, set = 0, binding = 8) readonly buffer GpuParticleDrawList
{
    uint slots[];
} drawList;

// GpuParticleDraw.h と同じ描画段・出力方式。
const uint kGpuParticleDrawPassPreTAA = 0u;
const uint kGpuParticleDrawPassPostTAA = 1u;
const uint kGpuParticleDrawOutputColor = 0u;
const uint kGpuParticleDrawOutputReactive = 1u;
// gl_InstanceIndex をそのまま粒のスロットに使う。
const uint kGpuParticleDrawSourceDirect = 0u;

// エミッタの描画設定から各項目を取り出す。
uint GpuParticleOrientation(GpuEmitterParams e)
{
    return (e.renderFlags >> kGpuEmitterRenderOrientationShift) & kGpuEmitterRenderOrientationMask;
}

uint GpuParticleBlend(GpuEmitterParams e)
{
    return (e.renderFlags >> kGpuEmitterRenderBlendShift) & kGpuEmitterRenderBlendMask;
}

uint GpuParticlePass(GpuEmitterParams e)
{
    return (e.renderFlags >> kGpuEmitterRenderPassShift) & kGpuEmitterRenderPassMask;
}

uint GpuParticleLighting(GpuEmitterParams e)
{
    return (e.renderFlags >> kGpuEmitterRenderLightingShift) & kGpuEmitterRenderLightingMask;
}

bool GpuParticleSubPixelCompensation(GpuEmitterParams e)
{
    return ((e.renderFlags >> kGpuEmitterRenderSubPixelCompensationShift) & kGpuEmitterRenderSubPixelCompensationMask) != 0u;
}

// target は Size・Color・Opacity・Stretch の順で 0〜3。
uint GpuParticleCurveKeyCount(GpuEmitterParams e, uint target)
{
    return (e.serialSpawnCurveOffsetCounts.w >> (target * 8u)) & kGpuCurveKeyCountMask;
}

// 同じエミッタの前の target のキー数を足して開始位置を求める。
uint GpuParticleCurveKeyStart(GpuEmitterParams e, uint target)
{
    uint start = e.serialSpawnCurveOffsetCounts.z;
    for (uint previous = 0u; previous < target; ++previous)
    {
        start += GpuParticleCurveKeyCount(e, previous);
    }
    return start;
}

// 検証済みのキー列を区分線形に評価し、範囲外では端の値を保持する。空は 0。
float GpuParticleEvaluateCurve(uint start, uint count, float t)
{
    if (count == 0u)
    {
        return 0.0;
    }

    GpuCurveKey left = drawCurveKeys.keys[start];
    if (t <= left.time)
    {
        return left.value;
    }

    for (uint index = 1u; index < count; ++index)
    {
        GpuCurveKey right = drawCurveKeys.keys[start + index];
        if (t <= right.time)
        {
            if (t == right.time)
            {
                return right.value;
            }
            float alpha = (t - left.time) / (right.time - left.time);
            // 異符号の値は差のオーバーフローを避けて補間する。
            if ((left.value <= 0.0 && right.value >= 0.0) ||
                (left.value >= 0.0 && right.value <= 0.0))
            {
                return (1.0 - alpha) * left.value + alpha * right.value;
            }
            return left.value + (right.value - left.value) * alpha;
        }
        left = right;
    }
    return left.value;
}

// 寿命曲線と有効化を反映した粒の見た目。
struct GpuParticleLook
{
    vec2 size;
    vec4 color;
    float stretch;
    float age01;
};

GpuParticleLook GpuParticleEvaluateLook(GpuEmitterParams e, GpuParticle p)
{
    GpuParticleLook look;
    look.age01 = p.velLife.w > 0.0 ? clamp(p.posAge.w / p.velLife.w, 0.0, 1.0) : 1.0;
    look.size = e.sizeTurbulenceStrengthFrequency.xy;
    look.color = e.color;
    look.stretch = 1.0;

    uint flags = e.poolCapacitySeedFlags.w;
    const uint sizeTarget = kGpuCurveKeyCountSizeShift / 8u;
    const uint colorTarget = kGpuCurveKeyCountColorShift / 8u;
    const uint opacityTarget = kGpuCurveKeyCountOpacityShift / 8u;
    const uint stretchTarget = kGpuCurveKeyCountStretchShift / 8u;
    uint sizeCount = GpuParticleCurveKeyCount(e, sizeTarget);
    uint colorCount = GpuParticleCurveKeyCount(e, colorTarget);
    uint opacityCount = GpuParticleCurveKeyCount(e, opacityTarget);
    uint stretchCount = GpuParticleCurveKeyCount(e, stretchTarget);

    if ((flags & kGpuEmitterFlagCurveSize) != 0u && sizeCount > 0u)
    {
        look.size *= GpuParticleEvaluateCurve(GpuParticleCurveKeyStart(e, sizeTarget), sizeCount, look.age01);
    }
    if ((flags & kGpuEmitterFlagCurveColor) != 0u && colorCount > 0u)
    {
        float value = GpuParticleEvaluateCurve(GpuParticleCurveKeyStart(e, colorTarget), colorCount, look.age01);
        look.color = look.color + (e.curveColor - look.color) * value;
    }
    if ((flags & kGpuEmitterFlagCurveOpacity) != 0u && opacityCount > 0u)
    {
        look.color.a *= GpuParticleEvaluateCurve(GpuParticleCurveKeyStart(e, opacityTarget), opacityCount, look.age01);
    }
    if ((flags & kGpuEmitterFlagCurveStretch) != 0u && stretchCount > 0u)
    {
        look.stretch = GpuParticleEvaluateCurve(GpuParticleCurveKeyStart(e, stretchTarget), stretchCount, look.age01);
    }
    if ((flags & kGpuEmitterFlagActivation) != 0u)
    {
        look.color.a *= VfxActivationWeight(e.activationFractionRampGroundKillY.x,
            e.activationFractionRampGroundKillY.y, VfxActivationHash(e.poolCapacitySeedFlags.z, p.meta.x));
    }
    return look;
}

// 位相と角速度は生成用の乱数を消費せず、通し番号から別々に求める。
float GpuParticleSpinAngle(GpuEmitterParams e, GpuParticle p)
{
    const float twoPi = 6.28318530717958647692;
    float phase = twoPi * VfxHashToUnitFloat(VfxPcgHash(p.meta.x ^ 0x50484153u));
    float speed = mix(e.lifetimeMaxSpinSpeedMinMaxMaxSize.y, e.lifetimeMaxSpinSpeedMinMaxMaxSize.z,
        VfxHashToUnitFloat(VfxPcgHash(p.meta.x ^ 0x5350494Eu)));
    float age = (p.meta.z & kGpuParticleFlagStuck) != 0u ? p.velLife.x : p.posAge.w;
    return phase + speed * age;
}

// 半径だけを揺らして頂点の角度順を保ち、粒ごとに同じ多角形を再現する。
vec2 GpuParticlePlateVertex(GpuEmitterParams e, uint serial, uint index)
{
    const float twoPi = 6.28318530717958647692;
    float angle = twoPi * float(index) / float(e.plateSides);
    float u = VfxHashToUnitFloat(VfxPcgHash(serial ^ VfxPcgHash(index + 0x504C4154u)));
    float radius = 1.0 - clamp(e.plateJitter, 0.0, 1.0) * u;
    return radius * vec2(cos(angle), sin(angle));
}

// 半径の揺らぎで凹んだ輪郭にも対応し、線分への最短距離と交差数で符号つき距離を求める。
float GpuParticlePlateDistance(GpuEmitterParams e, uint serial, vec2 point)
{
    float distanceSquared = 8.0;
    bool inside = false;
    vec2 previous = GpuParticlePlateVertex(e, serial, e.plateSides - 1u);
    for (uint index = 0u; index < e.plateSides; ++index)
    {
        vec2 current = GpuParticlePlateVertex(e, serial, index);
        vec2 edge = current - previous;
        vec2 offset = point - previous;
        float t = clamp(dot(offset, edge) / max(dot(edge, edge), 1.0e-12), 0.0, 1.0);
        vec2 nearest = offset - t * edge;
        distanceSquared = min(distanceSquared, dot(nearest, nearest));
        if ((previous.y > point.y) != (current.y > point.y))
        {
            float crossingX = previous.x + (point.y - previous.y) * edge.x / edge.y;
            if (point.x < crossingX)
            {
                inside = !inside;
            }
        }
        previous = current;
    }
    float distanceToEdge = sqrt(distanceSquared);
    return inside ? -distanceToEdge : distanceToEdge;
}

// 深度をカメラの前方距離に直す。クリア値は十分遠い背景として扱う。
float GpuParticleSceneLinearDepth(float deviceDepth)
{
    if (deviceDepth == kDepthClear)
    {
        return 1.0e30;
    }
    return drawConstants.nearFarParams.x / LinearDepthDenominator(deviceDepth, drawConstants.nearFarParams.z);
}

// ワールド位置からカメラの前方距離を求める。
float GpuParticleViewDepth(vec3 worldPos)
{
    return dot(worldPos - drawConstants.cameraPositionPreExposure.xyz, drawConstants.cameraForward.xyz);
}

#endif // NORVES_PARTICLE_DRAW_GLSL
