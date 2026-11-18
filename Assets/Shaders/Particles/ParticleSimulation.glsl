// GPU の粒の生成・更新・詰めで共有する入力と初期化処理。
#ifndef NORVES_PARTICLE_SIMULATION_GLSL
#define NORVES_PARTICLE_SIMULATION_GLSL

#include "Particles/ParticleCommon.glsl"
#include "Common/OverheadHeightMap.glsl"

const uint kGpuEmitterExtraFlagReset = 1u;
const uint kVfxShapePoint = 0u;
const uint kVfxShapeSphere = 1u;
const uint kVfxShapeCone = 2u;
const uint kVfxShapeBox = 3u;
const uint kVfxShapeCameraBox = 4u;
const uint kVfxCollisionKill = 0u;
const uint kVfxCollisionStick = 1u;
const uint kVfxCollisionBounce = 2u;
const uint kVfxCollisionSpawnChild = 3u;
const uint kVfxCollisionReturnToCameraBoxTop = 4u;

// CPU の GpuParticleBurst と同じ 64 バイトの並び。
struct GpuParticleBurst
{
    vec3 position;
    uint count;
    vec3 normal;
    uint emitterIndex;
    vec3 velocity;
    uint firstSerial;
    uint outputOffset;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

// CPU の GpuEmitterFrameExtra と同じ 16 バイトの並び。
struct GpuEmitterFrameExtra
{
    uint poolSlot;
    uint updateOffset;
    uint flags;
    uint reserved;
};

layout(std140, set = 0, binding = 0) uniform GpuParticleSimulationConstants
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec3 cameraPosition;
    float deltaTime;
    float unscaledDeltaTime;
    uint substepIndex;
    uint substepCount;
    uint emitterCount;
    uint updateCount;
    uint spawnJobCount;
    uint jobSpawnCount;
    uint burstCount;
    uint totalSpawnCount;
    uint depthCollisionEnabled;
    uint heightFieldBound;
    uint compactPhase; // 詰めは 0、仕上げは 1。
    vec2 screenSize;
    vec2 invScreenSize;
    vec4 windA;
    vec4 windB;
    vec4 heightFieldWindow;
    vec4 heightFieldDepth;
} simulation;

layout(std430, set = 0, binding = 1) buffer GpuParticlePool
{
    GpuParticle particles[];
};

layout(std430, set = 0, binding = 2) readonly buffer GpuEmitterBuffer
{
    GpuEmitterParams emitters[];
};

layout(std430, set = 0, binding = 3) readonly buffer GpuEmitterExtraBuffer
{
    GpuEmitterFrameExtra emitterExtras[];
};

layout(std430, set = 0, binding = 4) readonly buffer GpuSpawnJobBuffer
{
    GpuSpawnJob spawnJobs[];
};

layout(std430, set = 0, binding = 5) readonly buffer GpuParticleBurstBuffer
{
    GpuParticleBurst bursts[];
};

layout(std430, set = 0, binding = 6) buffer GpuEmitterHeadBuffer
{
    uint emitterHeads[];
};

layout(set = 0, binding = 7) uniform sampler2D gbufferDepth;
layout(set = 0, binding = 8) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 9) uniform sampler2D heightField;

// 有効な更新位置を含む最後の開始位置を求める。
uint FindUpdateEmitter(uint updateIndex)
{
    uint first = 0u;
    uint last = simulation.emitterCount;
    while (first < last)
    {
        uint middle = first + (last - first) / 2u;
        if (emitterExtras[middle].updateOffset <= updateIndex)
        {
            first = middle + 1u;
        }
        else
        {
            last = middle;
        }
    }
    return first - 1u;
}

uint ParticleShape(GpuEmitterParams emitter)
{
    uint flags = emitter.poolCapacitySeedFlags.w;
    return (flags & kGpuEmitterFlagShape) != 0u ?
        (flags >> kGpuEmitterShapeShift) & kGpuEmitterShapeMask : kVfxShapePoint;
}

// カメラの箱で折り返す粒は寿命まで使い回す。
bool ParticleIsRecycled(GpuEmitterParams emitter)
{
    return ParticleShape(emitter) == kVfxShapeCameraBox &&
        (emitter.poolCapacitySeedFlags.w & kGpuEmitterFlagWrapInCameraBox) != 0u;
}

// 無効なマップや窓の外は屋根が無いものとして扱う。
bool ParticleSampleRoofY(vec3 position, out float roofY)
{
    roofY = 0.0;
    return simulation.heightFieldBound != 0u &&
        OverheadHeightMapSampleRoofY(heightField,
            OverheadHeightMapView(simulation.heightFieldWindow, simulation.heightFieldDepth), position.xz, roofY);
}

// 生成・折り返しなど、経路を持たない配置先で遮蔽状態を決める。
void ClassifyParticleAtPlacement(GpuEmitterParams emitter, inout GpuParticle particle)
{
    if ((emitter.poolCapacitySeedFlags.w & kGpuEmitterFlagCollisionHeightField) == 0u)
    {
        return;
    }
    float roofY;
    if (ParticleSampleRoofY(particle.posAge.xyz, roofY) &&
        particle.posAge.y < roofY - OVERHEAD_RAIN_SURFACE_BIAS_METERS)
    {
        if (ParticleIsRecycled(emitter))
        {
            particle.meta.z |= kGpuParticleFlagSheltered;
        }
        else
        {
            particle.meta.z &= ~kGpuParticleFlagAlive;
        }
    }
    else
    {
        particle.meta.z &= ~kGpuParticleFlagSheltered;
    }
}

// エミッタの現在の有効割合から描画対象の旗を更新する。
void UpdateParticleActivation(GpuEmitterParams emitter, inout GpuParticle particle)
{
    uint flags = emitter.poolCapacitySeedFlags.w;
    bool isActive = (flags & kGpuEmitterFlagActivation) == 0u ||
        VfxActivationWeight(emitter.activationFractionRampGroundKillY.x,
            emitter.activationFractionRampGroundKillY.y,
            VfxActivationHash(emitter.poolCapacitySeedFlags.z, particle.meta.x)) > 0.0;
    if (isActive)
    {
        particle.meta.z |= kGpuParticleFlagActive;
    }
    else
    {
        particle.meta.z &= ~kGpuParticleFlagActive;
    }
}

// CPU と同じ乱数の種類と接線の選び方で粒を初期化する。
GpuParticle InitParticle(GpuEmitterParams emitter, GpuEmitterFrameExtra extra, uint serial,
    vec3 basePosition, vec3 normal, vec3 addVelocity)
{
    uint seed = emitter.poolCapacitySeedFlags.z;
    vec3 draws = vec3(VfxCounterUnitFloat(seed, serial, kVfxRandomDrawVelocityX),
        VfxCounterUnitFloat(seed, serial, kVfxRandomDrawVelocityY),
        VfxCounterUnitFloat(seed, serial, kVfxRandomDrawVelocityZ));
    vec3 velocity = emitter.velocityMinDrag.xyz +
        (emitter.velocityMaxWindCoupling.xyz - emitter.velocityMinDrag.xyz) * draws;

    float largestNormalComponent = max(max(abs(normal.x), abs(normal.y)), abs(normal.z));
    if (largestNormalComponent > 0.0)
    {
        vec3 orientation = normalize(normal / largestNormalComponent);
        vec3 helper = abs(orientation.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent = normalize(cross(helper, orientation));
        vec3 bitangent = cross(orientation, tangent);
        velocity = tangent * velocity.x + bitangent * velocity.y + orientation * velocity.z;
    }
    velocity += addVelocity;

    uint shape = ParticleShape(emitter);
    vec3 position = basePosition;
    if (shape != kVfxShapePoint && shape != kGpuEmitterShapeSkinSurface && shape != kGpuEmitterShapeBone)
    {
        float a = VfxCounterUnitFloat(seed, serial, kVfxRandomDrawShapeA);
        float b = VfxCounterUnitFloat(seed, serial, kVfxRandomDrawShapeB);
        float c = VfxCounterUnitFloat(seed, serial, kVfxRandomDrawShapeC);
        const float twoPi = 6.28318530717958647692;
        vec3 samplePosition = vec3(0.0);
        if (shape == kVfxShapeSphere)
        {
            float y = 1.0 - 2.0 * a;
            float horizontal = sqrt(max(0.0, 1.0 - y * y));
            float phi = twoPi * b;
            float radius = emitter.radiusConeAngleLengthDepthThickness.x * pow(c, 1.0 / 3.0);
            samplePosition = vec3(horizontal * cos(phi), y, horizontal * sin(phi)) * radius;
        }
        else if (shape == kVfxShapeCone)
        {
            float height = emitter.radiusConeAngleLengthDepthThickness.z * pow(a, 1.0 / 3.0);
            float radius = height * tan(emitter.radiusConeAngleLengthDepthThickness.y) * sqrt(b);
            float phi = twoPi * c;
            samplePosition = vec3(radius * cos(phi), height, radius * sin(phi));
        }
        else if (shape == kVfxShapeBox || shape == kVfxShapeCameraBox)
        {
            samplePosition = (2.0 * vec3(a, b, c) - 1.0) * emitter.boxHalfExtentsNearFadeDistance.xyz;
        }
        position += samplePosition;
    }

    GpuParticle particle;
    particle.posAge = vec4(position, 0.0);
    particle.velLife = vec4(velocity, emitter.positionLifetime.w);
    particle.meta = uvec4(serial, extra.poolSlot, kGpuParticleFlagAlive, 0u);
    UpdateParticleActivation(emitter, particle);
    return particle;
}

#endif // NORVES_PARTICLE_SIMULATION_GLSL
