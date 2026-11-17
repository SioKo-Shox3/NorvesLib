#include "Particle/VfxEffectSystem.h"

#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Particle/VfxSchema.h"
#include "Particle/VfxSimulationMath.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/GpuParticle/GpuParticleFrame.h"
#include "Rendering/GpuParticle/GpuParticleEmitterAllocator.h"
#include "Rendering/GpuParticle/GpuParticleSpawnPlanner.h"

#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Fail(Container::String* outError, const char* message)
        {
            if (outError != nullptr)
            {
                *outError = message;
            }
            return false;
        }

        Math::Vector3 ToVector3(const VfxValue& value)
        {
            return Math::Vector3(value.Vector.x, value.Vector.y, value.Vector.z);
        }

        enum EmitterPropertyIndex : uint32_t
        {
            MaxParticlesProperty,
            SpaceProperty,
            SeedProperty,
            ExecutorProperty,
            RandomStreamProperty,
            CapacityProperty,
            TimeChannelProperty,
            WrapPolicyProperty,
            EmitterPropertyCount
        };

        template <size_t N>
        const char* ArgumentAt(const char* const (&names)[N], uint32_t index)
        {
            return index < N ? names[index] : nullptr;
        }

        const char* ArgumentName(VfxModuleType type, uint32_t index)
        {
            switch (type)
            {
            case VfxModuleType::SpawnRate:
            {
                constexpr const char* names[] = {"Rate"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::SpawnBurst:
            {
                constexpr const char* names[] = {"Count", "Time"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::SpawnOnEvent:
            {
                constexpr const char* names[] = {"EventName", "Count"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitPosition:
            {
                constexpr const char* names[] = {"Offset"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitVelocityBox:
            {
                constexpr const char* names[] = {"Min", "Max"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitLifetime:
            {
                constexpr const char* names[] = {"Lifetime"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitColor:
            {
                constexpr const char* names[] = {"Color"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitSize:
            {
                constexpr const char* names[] = {"Size"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::InitShape:
            {
                constexpr const char* names[] = {"Shape", "Radius", "ConeAngle", "ConeLength", "BoxHalfExtents", "CameraBoxHalfWidth", "CameraBoxHeight"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::Acceleration:
            {
                constexpr const char* names[] = {"Acceleration"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::Drag:
            {
                constexpr const char* names[] = {"Coefficient"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::WindInfluence:
            {
                constexpr const char* names[] = {"Coupling", "Scale"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::Turbulence:
            {
                constexpr const char* names[] = {"Strength", "Frequency"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::CurveOverLife:
            {
                constexpr const char* names[] = {"Target", "KeyCount", "Key0", "Key1", "Key2", "Key3", "Key4", "Key5", "Key6", "Key7", "Color"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::ActivationFraction:
            {
                constexpr const char* names[] = {"Fraction", "Ramp"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::CollisionPlane:
            {
                constexpr const char* names[] = {"GroundY", "KillBelowY", "Response", "Restitution", "ChildEmitter", "ChildProbability"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::CollisionHeightField:
            {
                constexpr const char* names[] = {"Response", "Restitution", "ChildEmitter", "ChildProbability"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::CollisionDepth:
            {
                constexpr const char* names[] = {"Thickness", "Response", "Restitution", "ChildEmitter", "ChildProbability"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::SpriteRenderer:
            {
                constexpr const char* names[] = {"Layer", "Blend", "SortKey", "Flipbook"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::MeshRenderer:
            {
                constexpr const char* names[] = {"Mesh", "AlignToVelocity", "DissolveStart", "DissolveDuration", "EdgeWidth", "EdgeColor"};
                return ArgumentAt(names, index);
            }
            case VfxModuleType::GpuSpriteRenderer:
            {
                constexpr const char* names[] = {"Orientation", "Blend", "Pass", "SoftDistance", "Lighting", "FogInfluence",
                    "StreakShutterSeconds", "SubPixelCompensation", "NearFadeDistance", "Texture"};
                return ArgumentAt(names, index);
            }
            default:
                return nullptr;
            }
        }

        float WrapAxis(float position, float center, float halfSize)
        {
            const float minimum = center - halfSize;
            const float maximum = center + halfSize;
            if (halfSize == 0.0f)
            {
                return minimum;
            }
            if (position >= minimum && position <= maximum)
            {
                return position;
            }
            const float size = halfSize * 2.0f;
            const float offset = position - minimum;
            return minimum + offset - size * std::floor(offset / size);
        }

        bool IsOutsideCameraBox(const Math::Vector3& position, const Math::Vector3& center, float halfWidth, float halfHeight)
        {
            return position.x < center.x - halfWidth || position.x > center.x + halfWidth ||
                position.y < center.y - halfHeight || position.y > center.y + halfHeight ||
                position.z < center.z - halfWidth || position.z > center.z + halfWidth;
        }

        bool SampleHeightField(const VfxHeightFieldView& field, const Math::Vector3& position, float& outHeight)
        {
            if (!field.IsBound() || !std::isfinite(field.WorldMin.x) || !std::isfinite(field.WorldMin.y) ||
                !std::isfinite(position.x) || !std::isfinite(position.z))
            {
                return false;
            }
            const double u = std::fmax(0.0, std::fmin(1.0,
                (static_cast<double>(position.x) - field.WorldMin.x) / field.WorldSize.x));
            const double v = std::fmax(0.0, std::fmin(1.0,
                (static_cast<double>(position.z) - field.WorldMin.y) / field.WorldSize.y));
            const double x = u * (field.Width - 1u);
            const double z = v * (field.Height - 1u);
            const uint32_t x0 = static_cast<uint32_t>(x);
            const uint32_t z0 = static_cast<uint32_t>(z);
            const uint32_t x1 = x0 < field.Width - 1u ? x0 + 1u : x0;
            const uint32_t z1 = z0 < field.Height - 1u ? z0 + 1u : z0;
            const size_t row0 = static_cast<size_t>(z0) * field.Width;
            const size_t row1 = static_cast<size_t>(z1) * field.Width;
            const float tx = static_cast<float>(x - x0);
            const float tz = static_cast<float>(z - z0);
            const float h0 = std::lerp(field.Heights[row0 + x0], field.Heights[row0 + x1], tx);
            const float h1 = std::lerp(field.Heights[row1 + x0], field.Heights[row1 + x1], tx);
            outHeight = std::lerp(h0, h1, tz);
            return true;
        }

        Math::Vector3 EvaluateParticleWind(const Rendering::WindParameters& wind, const Math::Vector3& position, float time)
        {
            if (!wind.bEnabled)
            {
                return Math::Vector3(0.0f);
            }
            if (!IsFinite(position) || !std::isfinite(time))
            {
                return Math::Vector3(0.0f);
            }
            float dirX = 0.0f;
            float dirZ = 0.0f;
            Rendering::WindDirectionXZ(wind, dirX, dirZ);
            const float argument = (position.x * dirX + position.z * dirZ) / wind.GustScaleMeters - time * wind.GustFrequencyHz;
            const float turbulenceArgument = argument * 3.1f;
            constexpr float gridLimit = 2147483648.0f;
            if (!std::isfinite(argument) || !std::isfinite(turbulenceArgument) ||
                argument < -gridLimit || argument >= gridLimit ||
                turbulenceArgument < -gridLimit || turbulenceArgument >= gridLimit)
            {
                return Math::Vector3(0.0f);
            }
            return Rendering::EvaluateWind(wind, position, time);
        }
    }

    bool VfxHeightFieldView::IsBound() const
    {
        return Heights != nullptr && Width >= 2u && Height >= 2u &&
            std::isfinite(WorldSize.x) && std::isfinite(WorldSize.y) && WorldSize.x > 0.0f && WorldSize.y > 0.0f;
    }

    struct VfxEffectSystem::GpuExecutionState
    {
        struct PendingBurst
        {
            VfxEffectHandle Handle;
            uint32_t EmitterIndex = 0u;
            uint32_t Epoch = 0u;
            Rendering::GpuParticleBurst Burst{};
        };

        Rendering::GpuParticleEmitterAllocator Allocator{Rendering::kGpuParticlePoolCapacity};
        VariableArray<PendingBurst> Bursts;
        uint32_t OverflowCount = 0u;
        float PendingDeltaTime = 0.0f;
        float PendingUnscaledDeltaTime = 0.0f;
        double ParticleTime = 0.0;
        double UnscaledTime = 0.0;
    };

    VfxEffectSystem::VfxEffectSystem() = default;
    VfxEffectSystem::~VfxEffectSystem() = default;
    VfxEffectSystem::VfxEffectSystem(VfxEffectSystem&&) noexcept = default;
    VfxEffectSystem& VfxEffectSystem::operator=(VfxEffectSystem&&) noexcept = default;

    VfxEffectSystem::GpuExecutionState& VfxEffectSystem::EnsureGpuState()
    {
        if (!m_GpuState)
        {
            m_GpuState = MakeUnique<GpuExecutionState>();
        }
        return *m_GpuState;
    }

    VfxEffectHandle VfxEffectSystem::CreateInstance(const VfxEffectAsset& asset, const VfxInstanceDesc& desc, Container::String* outError)
    {
        if (!ValidateVfxEffect(asset, outError))
        {
            return {};
        }
        if (!IsFinite(desc.Position))
        {
            Fail(outError, "インスタンスの位置は有限値で指定してください。");
            return {};
        }

        uint32_t index = 0u;
        if (!m_FreeInstanceIndices.empty())
        {
            index = m_FreeInstanceIndices.back();
            m_FreeInstanceIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_Instances.size());
            m_Instances.emplace_back();
        }
        InstanceSlot& slot = m_Instances[index];
        slot.Asset = asset;
        slot.Desc = desc;
        slot.Time = 0.0;
        slot.bHasPreviousGpuPosition = false;
        slot.Events.clear();
        slot.Emitters.clear();
        slot.Emitters.resize(asset.Emitters.size());
        if (!CacheBindings(slot))
        {
            ReleaseSlot(index);
            Fail(outError, "エミッタまたはモジュールの引数を結び付けられません。");
            return {};
        }
        for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
        {
            RefreshEmitter(slot, emitterIndex);
        }
        slot.bActive = true;
        return {index, slot.Generation};
    }

    bool VfxEffectSystem::DestroyInstance(VfxEffectHandle handle)
    {
        if (!IsLiveInstance(handle))
        {
            return false;
        }
        ReleaseSlot(handle.Index);
        return true;
    }

    void VfxEffectSystem::Clear()
    {
        for (uint32_t index = 0u; index < m_Instances.size(); ++index)
        {
            if (m_Instances[index].bActive)
            {
                ReleaseSlot(index);
            }
        }
        if (m_GpuState)
        {
            m_GpuState->Bursts.clear();
            m_GpuState->OverflowCount = 0u;
        }
    }

    bool VfxEffectSystem::IsLiveInstance(VfxEffectHandle handle) const
    {
        return handle.IsValid() && handle.Index < m_Instances.size() &&
            m_Instances[handle.Index].bActive && m_Instances[handle.Index].Generation == handle.Generation;
    }

    bool VfxEffectSystem::SetInstancePosition(VfxEffectHandle handle, const Math::Vector3& position)
    {
        if (!IsLiveInstance(handle) || !IsFinite(position))
        {
            return false;
        }
        m_Instances[handle.Index].Desc.Position = position;
        return true;
    }

    bool VfxEffectSystem::SetInstanceEnabled(VfxEffectHandle handle, bool bEnabled)
    {
        if (!IsLiveInstance(handle))
        {
            return false;
        }
        m_Instances[handle.Index].Desc.bEnabled = bEnabled;
        return true;
    }

    bool VfxEffectSystem::SetModuleParameter(VfxEffectHandle handle, uint32_t emitterIndex, uint32_t moduleIndex,
        const char* parameterName, const VfxValue& value, Container::String* outError)
    {
        if (!IsLiveInstance(handle))
        {
            return Fail(outError, "インスタンスのハンドルが無効です。");
        }
        InstanceSlot& slot = m_Instances[handle.Index];
        if (emitterIndex >= slot.Asset.Emitters.size() || moduleIndex >= slot.Asset.Emitters[emitterIndex].Modules.size())
        {
            return Fail(outError, "エミッタまたはモジュールの添字が範囲外です。");
        }
        VfxModule& module = slot.Asset.Emitters[emitterIndex].Modules[moduleIndex];
        const VfxModule previous = module;
        if (!SetVfxModuleParameter(module, parameterName, value, outError))
        {
            return false;
        }
        if (!ValidateVfxEffect(slot.Asset, outError))
        {
            module = previous;
            return false;
        }
        return true;
    }

    bool VfxEffectSystem::SetEmitterProperty(VfxEffectHandle handle, uint32_t emitterIndex,
        const char* propertyName, const VfxValue& value, Container::String* outError)
    {
        if (!IsLiveInstance(handle))
        {
            return Fail(outError, "インスタンスのハンドルが無効です。");
        }
        InstanceSlot& slot = m_Instances[handle.Index];
        if (emitterIndex >= slot.Asset.Emitters.size())
        {
            return Fail(outError, "エミッタの添字が範囲外です。");
        }
        VfxEmitterAsset& emitter = slot.Asset.Emitters[emitterIndex];
        const VfxEmitterAsset previous = emitter;
        if (!SetVfxEmitterProperty(emitter, propertyName, value, outError))
        {
            return false;
        }
        if (!ValidateVfxEffect(slot.Asset, outError))
        {
            emitter = previous;
            return false;
        }
        const uint32_t capacity = emitter.Properties[slot.PropertyIndices[CapacityProperty]].UInt;
        const uint32_t maxParticles = capacity != 0u ? capacity : emitter.Properties[slot.PropertyIndices[MaxParticlesProperty]].UInt;
        slot.Emitters[emitterIndex].MaxParticles = maxParticles;
        if (slot.Emitters[emitterIndex].Particles.size() > maxParticles)
        {
            slot.Emitters[emitterIndex].Particles.resize(maxParticles);
        }
        if (slot.Emitters[emitterIndex].Executor == VfxExecutor::Gpu ||
            emitter.Properties[slot.PropertyIndices[ExecutorProperty]].UInt == static_cast<uint32_t>(VfxExecutor::Gpu))
        {
            RefreshEmitter(slot, emitterIndex);
        }
        return true;
    }

    bool VfxEffectSystem::RaiseEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position, double timeSeconds)
    {
        if (!IsLiveInstance(handle) || eventName == nullptr || !IsFinite(position) || !std::isfinite(timeSeconds))
        {
            return false;
        }
        InstanceSlot& slot = m_Instances[handle.Index];
        if (timeSeconds < slot.Time)
        {
            return false;
        }
        PendingEvent event;
        event.Name = eventName;
        event.Position = position;
        event.Time = timeSeconds;
        slot.Events.push_back(event);
        return true;
    }

    bool VfxEffectSystem::RaiseOrientedEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position,
        const Math::Vector3& normal, double timeSeconds)
    {
        if (!IsFinite(normal))
        {
            return false;
        }
        // 倍精度で長さを判定し、有限な大きい成分の二乗があふれないようにする。
        const double lengthSquared = static_cast<double>(normal.x) * normal.x +
            static_cast<double>(normal.y) * normal.y + static_cast<double>(normal.z) * normal.z;
        if (lengthSquared <= 1.0e-12)
        {
            return false;
        }
        const double largest = std::fmax(std::fabs(normal.x), std::fmax(std::fabs(normal.y), std::fabs(normal.z)));
        const Math::Vector3 scaledNormal(static_cast<float>(normal.x / largest),
            static_cast<float>(normal.y / largest), static_cast<float>(normal.z / largest));
        const float scaledLength = std::sqrt(Math::Vector3::Dot(scaledNormal, scaledNormal));
        if (!RaiseEvent(handle, eventName, position, timeSeconds))
        {
            return false;
        }
        PendingEvent& event = m_Instances[handle.Index].Events.back();
        event.Normal = scaledNormal / scaledLength;
        event.bOriented = true;
        return true;
    }

    bool VfxEffectSystem::RaiseGpuBurst(VfxEffectHandle handle, uint32_t emitterIndex, const Math::Vector3& position,
        const Math::Vector3& normal, const Math::Vector3& velocity, uint32_t count)
    {
        if (!IsLiveInstance(handle) || !IsFinite(position) || !IsFinite(normal) || !IsFinite(velocity))
        {
            return false;
        }
        InstanceSlot& slot = m_Instances[handle.Index];
        if (emitterIndex >= slot.Emitters.size() ||
            slot.Asset.Emitters[emitterIndex].Properties[slot.PropertyIndices[ExecutorProperty]].UInt != static_cast<uint32_t>(VfxExecutor::Gpu))
        {
            return false;
        }
        RefreshEmitter(slot, emitterIndex);
        const EmitterState& emitter = slot.Emitters[emitterIndex];
        const Math::Vector3 burstPosition = position + emitter.Offset;
        if (emitter.Executor != VfxExecutor::Gpu || !IsFinite(burstPosition))
        {
            return false;
        }
        GpuExecutionState& gpu = EnsureGpuState();
        if (!gpu.Allocator.IsLive({emitter.GpuPoolIndex, emitter.GpuPoolGeneration}))
        {
            return false;
        }
        if (gpu.Bursts.size() >= Rendering::kGpuParticleMaxBursts)
        {
            if (gpu.OverflowCount != std::numeric_limits<uint32_t>::max())
            {
                ++gpu.OverflowCount;
            }
            return false;
        }
        GpuExecutionState::PendingBurst pending;
        pending.Handle = handle;
        pending.EmitterIndex = emitterIndex;
        pending.Epoch = emitter.GpuEpoch;
        pending.Burst.Position[0] = burstPosition.x;
        pending.Burst.Position[1] = burstPosition.y;
        pending.Burst.Position[2] = burstPosition.z;
        pending.Burst.Normal[0] = normal.x;
        pending.Burst.Normal[1] = normal.y;
        pending.Burst.Normal[2] = normal.z;
        pending.Burst.Velocity[0] = velocity.x;
        pending.Burst.Velocity[1] = velocity.y;
        pending.Burst.Velocity[2] = velocity.z;
        pending.Burst.Count = count;
        gpu.Bursts.push_back(pending);
        return true;
    }

    void VfxEffectSystem::Tick(float deltaTime)
    {
        Tick(deltaTime, VfxTickContext{});
    }

    void VfxEffectSystem::Tick(float deltaTime, const VfxTickContext& context)
    {
        Tick(deltaTime, context, context.UnscaledDeltaTime);
    }

    void VfxEffectSystem::Tick(float deltaTime, const VfxTickContext& context, float gpuUnscaledDeltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            return;
        }
        const Rendering::WindParameters wind = Rendering::SanitizeWindParameters(context.Wind);
        const float unscaledDeltaTime = std::isfinite(context.UnscaledDeltaTime) && context.UnscaledDeltaTime >= 0.0f ?
            context.UnscaledDeltaTime : deltaTime;
        const float gpuDeltaTime = std::isfinite(gpuUnscaledDeltaTime) && gpuUnscaledDeltaTime >= 0.0f ?
            gpuUnscaledDeltaTime : unscaledDeltaTime;
        GpuExecutionState& gpu = EnsureGpuState();
        gpu.PendingDeltaTime += deltaTime;
        gpu.PendingUnscaledDeltaTime += gpuDeltaTime;
        gpu.ParticleTime += deltaTime;
        gpu.UnscaledTime += gpuDeltaTime;
        for (InstanceSlot& slot : m_Instances)
        {
            if (!slot.bActive)
            {
                continue;
            }
            const double start = slot.Time;
            const double end = start + static_cast<double>(deltaTime);
            VariableArray<ChildSpawn> children;
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                RefreshEmitter(slot, emitterIndex);
                EmitterState& emitter = slot.Emitters[emitterIndex];
                const float emitterDeltaTime = emitter.TimeChannel == VfxTimeChannel::Unscaled ? unscaledDeltaTime : deltaTime;
                if (emitter.Executor == VfxExecutor::Gpu)
                {
                    emitter.GpuPendingDeltaTime += emitter.TimeChannel == VfxTimeChannel::Unscaled ? gpuDeltaTime : deltaTime;
                    if (slot.Desc.bEnabled)
                    {
                        const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
                        for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                        {
                            const VfxModule& module = asset.Modules[moduleIndex];
                            const ModuleBinding& binding = emitter.Bindings[moduleIndex];
                            if (module.Type == VfxModuleType::SpawnBurst)
                            {
                                const double time = module.Values[binding.Arguments[1]].Vector.x;
                                if (start <= time && time < end)
                                {
                                    const uint32_t count = module.Values[binding.Arguments[0]].UInt;
                                    const uint32_t remaining = std::numeric_limits<uint32_t>::max() - emitter.GpuPendingBurstCount;
                                    emitter.GpuPendingBurstCount += count < remaining ? count : remaining;
                                }
                            }
                        }
                        for (const PendingEvent& event : slot.Events)
                        {
                            if (event.Time < start || event.Time >= end)
                            {
                                continue;
                            }
                            for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                            {
                                const VfxModule& module = asset.Modules[moduleIndex];
                                const ModuleBinding& binding = emitter.Bindings[moduleIndex];
                                if (module.Type == VfxModuleType::SpawnOnEvent && module.Values[binding.Arguments[0]].Name == event.Name)
                                {
                                    const VfxEffectHandle handle{static_cast<uint32_t>(&slot - m_Instances.data()), slot.Generation};
                                    RaiseGpuBurst(handle, emitterIndex, event.Position,
                                        event.bOriented ? event.Normal : Math::Vector3(0.0f), Math::Vector3(0.0f),
                                        module.Values[binding.Arguments[1]].UInt);
                                }
                            }
                        }
                    }
                    continue;
                }
                uint32_t writeIndex = 0u;
                for (uint32_t readIndex = 0u; readIndex < emitter.Particles.size(); ++readIndex)
                {
                    VfxParticleState& particle = emitter.Particles[readIndex];
                    bool bOutside = false;
                    if (!particle.bStuck)
                    {
                        Math::Vector3 acceleration = emitter.Acceleration;
                        if (emitter.bHasDrag)
                        {
                            acceleration -= particle.Velocity * emitter.Drag;
                        }
                        if (!emitter.WindInfluences.empty())
                        {
                            const Math::Vector3 windVelocity = EvaluateParticleWind(wind, particle.Position, context.WindTimeSeconds);
                            for (const WindInfluenceState& influence : emitter.WindInfluences)
                            {
                                acceleration += (windVelocity * influence.Scale - particle.Velocity) * influence.Coupling;
                            }
                        }
                        for (const TurbulenceState& turbulence : emitter.Turbulences)
                        {
                            acceleration += VfxTurbulenceAcceleration(particle.Position, turbulence.Frequency,
                                turbulence.Strength, emitter.StreamSeed);
                        }
                        particle.Velocity += acceleration * emitterDeltaTime;
                        particle.Position += particle.Velocity * emitterDeltaTime;
                        if (!emitter.Collisions.empty() &&
                            !ResolveCollisions(emitter, particle, context.CameraPosition, context.HeightField, children))
                        {
                            continue;
                        }
                        if (emitter.Shape == VfxSpawnShape::CameraBox && emitter.WrapPolicy == VfxWrapPolicy::WrapInCameraBox)
                        {
                            const float halfHeight = emitter.CameraBoxHeight * 0.5f;
                            particle.Position.x = WrapAxis(particle.Position.x, context.CameraPosition.x, emitter.CameraBoxHalfWidth);
                            particle.Position.y = WrapAxis(particle.Position.y, context.CameraPosition.y, halfHeight);
                            particle.Position.z = WrapAxis(particle.Position.z, context.CameraPosition.z, emitter.CameraBoxHalfWidth);
                        }
                        bOutside = emitter.Shape == VfxSpawnShape::CameraBox && emitter.WrapPolicy == VfxWrapPolicy::Die &&
                            IsOutsideCameraBox(particle.Position, context.CameraPosition, emitter.CameraBoxHalfWidth, emitter.CameraBoxHeight * 0.5f);
                    }
                    particle.Age += emitterDeltaTime;
                    if (!bOutside && particle.Age < particle.Lifetime)
                    {
                        UpdateOverLife(emitter, particle);
                        UpdateFlipbook(slot, emitter, particle);
                        if (writeIndex != readIndex)
                        {
                            emitter.Particles[writeIndex] = particle;
                        }
                        ++writeIndex;
                    }
                }
                emitter.Particles.resize(writeIndex);
                if (emitter.Particles.size() > emitter.MaxParticles)
                {
                    emitter.Particles.resize(emitter.MaxParticles);
                }
                if (!slot.Desc.bEnabled)
                {
                    continue;
                }

                const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
                float rate = 0.0f;
                for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                {
                    const VfxModule& module = asset.Modules[moduleIndex];
                    if (module.Type == VfxModuleType::SpawnRate)
                    {
                        rate += module.Values[emitter.Bindings[moduleIndex].Arguments[0]].Vector.x;
                    }
                }
                const uint32_t continuousCount = VfxAdvanceSpawnAccumulator(emitter.SpawnAccumulator, rate, emitterDeltaTime);
                SpawnParticles(slot, emitter, continuousCount, slot.Desc.Position, end, 0.0f, context);

                for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                {
                    const VfxModule& module = asset.Modules[moduleIndex];
                    const ModuleBinding& binding = emitter.Bindings[moduleIndex];
                    if (module.Type == VfxModuleType::SpawnBurst)
                    {
                        const double time = module.Values[binding.Arguments[1]].Vector.x;
                        if (start <= time && time < end)
                        {
                            SpawnParticles(slot, emitter, module.Values[binding.Arguments[0]].UInt, slot.Desc.Position, end, 0.0f, context);
                        }
                    }
                }
                for (const PendingEvent& event : slot.Events)
                {
                    if (event.Time < start || event.Time >= end)
                    {
                        continue;
                    }
                    for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                    {
                        const VfxModule& module = asset.Modules[moduleIndex];
                        const ModuleBinding& binding = emitter.Bindings[moduleIndex];
                        if (module.Type == VfxModuleType::SpawnOnEvent && module.Values[binding.Arguments[0]].Name == event.Name)
                        {
                            SpawnParticles(slot, emitter, module.Values[binding.Arguments[1]].UInt,
                                event.Position, event.Time, static_cast<float>(end - event.Time), context,
                                event.bOriented ? &event.Normal : nullptr);
                        }
                    }
                }
            }
            for (const ChildSpawn& child : children)
            {
                EmitterState& target = slot.Emitters[child.EmitterIndex];
                if (target.Executor == VfxExecutor::Cpu)
                {
                    SpawnParticles(slot, target, 1u, child.Position, end, 0.0f, context, nullptr, true);
                }
                else
                {
                    const VfxEffectHandle handle{static_cast<uint32_t>(&slot - m_Instances.data()), slot.Generation};
                    RaiseGpuBurst(handle, child.EmitterIndex, child.Position, Math::Vector3(0.0f), Math::Vector3(0.0f), 1u);
                }
            }
            uint32_t writeIndex = 0u;
            for (uint32_t readIndex = 0u; readIndex < slot.Events.size(); ++readIndex)
            {
                if (!(start <= slot.Events[readIndex].Time && slot.Events[readIndex].Time < end))
                {
                    if (writeIndex != readIndex)
                    {
                        slot.Events[writeIndex] = slot.Events[readIndex];
                    }
                    ++writeIndex;
                }
            }
            slot.Events.resize(writeIndex);
            slot.Time = end;
        }
    }

    Rendering::GpuEmitterParams VfxEffectSystem::PackGpuEmitterParams(const InstanceSlot& slot, uint32_t emitterIndex,
        EmitterState& emitter, const VariableArray<uint32_t>& frameIndices, Rendering::GpuParticleScene& out)
    {
        Rendering::GpuEmitterParams params{};
        params.PlaneCollision.ChildEmitter = Rendering::kGpuParticleInvalidEmitter;
        params.HeightFieldCollision.ChildEmitter = Rendering::kGpuParticleInvalidEmitter;
        params.DepthCollision.ChildEmitter = Rendering::kGpuParticleInvalidEmitter;
        params.Seed = emitter.StreamSeed;
        params.Lifetime = emitter.Lifetime;
        const Math::Vector3 position = emitter.Shape == VfxSpawnShape::CameraBox ? emitter.Offset : slot.Desc.Position + emitter.Offset;
        const auto copyVector3 = [](float* destination, const Math::Vector3& value)
        {
            destination[0] = value.x;
            destination[1] = value.y;
            destination[2] = value.z;
        };
        copyVector3(params.Position, position);
        copyVector3(params.VelocityMin, emitter.VelocityMin);
        copyVector3(params.VelocityMax, emitter.VelocityMax);
        copyVector3(params.Acceleration, emitter.Acceleration);
        params.Color[0] = emitter.Color.x;
        params.Color[1] = emitter.Color.y;
        params.Color[2] = emitter.Color.z;
        params.Color[3] = emitter.Color.w;
        params.Size[0] = emitter.Size.x;
        params.Size[1] = emitter.Size.y;
        params.Radius = emitter.ShapeRadius;
        params.ConeAngleRadians = emitter.ConeAngle * (Math::Constants::PI / 180.0f);
        params.ConeLength = emitter.ConeLength;
        const Math::Vector3 extents = emitter.Shape == VfxSpawnShape::CameraBox ?
            Math::Vector3(emitter.CameraBoxHalfWidth, emitter.CameraBoxHeight * 0.5f, emitter.CameraBoxHalfWidth) : emitter.BoxHalfExtents;
        copyVector3(params.BoxHalfExtents, extents);
        params.Flags = (static_cast<uint32_t>(emitter.Shape) & Rendering::kGpuEmitterShapeMask) << Rendering::kGpuEmitterShapeShift;
        if (emitter.Shape != VfxSpawnShape::Point)
        {
            params.Flags |= Rendering::kGpuEmitterFlagShape;
        }
        if (emitter.Shape == VfxSpawnShape::CameraBox && emitter.WrapPolicy == VfxWrapPolicy::WrapInCameraBox)
        {
            params.Flags |= Rendering::kGpuEmitterFlagWrapInCameraBox;
        }
        if (emitter.TimeChannel == VfxTimeChannel::Unscaled)
        {
            params.Flags |= Rendering::kGpuEmitterFlagUnscaledTime;
        }

        params.CurveKeyOffset = static_cast<uint32_t>(out.CurveKeys.size());
        constexpr uint32_t curveFlags[] = {Rendering::kGpuEmitterFlagCurveSize, Rendering::kGpuEmitterFlagCurveColor,
            Rendering::kGpuEmitterFlagCurveOpacity, Rendering::kGpuEmitterFlagCurveStretch};
        for (uint32_t target = 0u; target < 4u; ++target)
        {
            const LifeCurveState& curve = emitter.Curves[target];
            if (curve.KeyCount == 0u)
            {
                continue;
            }
            params.Flags |= curveFlags[target];
            params.CurveKeyCounts |= (curve.KeyCount & Rendering::kGpuCurveKeyCountMask) << (target * 8u);
            for (uint32_t index = 0u; index < curve.KeyCount; ++index)
            {
                out.CurveKeys.push_back({static_cast<float>(curve.Keys[index].Time), curve.Keys[index].Value});
            }
            if (target == static_cast<uint32_t>(VfxCurveTarget::Color))
            {
                params.CurveColor[0] = curve.Color.x;
                params.CurveColor[1] = curve.Color.y;
                params.CurveColor[2] = curve.Color.z;
                params.CurveColor[3] = curve.Color.w;
            }
        }

        const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
        uint32_t seen = 0u;
        const auto firstModule = [&](uint32_t bit)
        {
            if ((seen & bit) == 0u)
            {
                seen |= bit;
                return true;
            }
            if ((emitter.GpuDuplicateWarnings & bit) == 0u)
            {
                LOG_WARNING("GPU エミッタの同種モジュールが複数あるため先頭だけを使います");
                emitter.GpuDuplicateWarnings |= bit;
            }
            return false;
        };
        for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
        {
            const VfxModule& module = asset.Modules[moduleIndex];
            const ModuleBinding& binding = emitter.Bindings[moduleIndex];
            const auto value = [&](uint32_t argument) -> const VfxValue&
            {
                return module.Values[binding.Arguments[argument]];
            };
            switch (module.Type)
            {
            case VfxModuleType::Drag:
                if (firstModule(1u << 0u))
                {
                    params.Flags |= Rendering::kGpuEmitterFlagDrag;
                    params.Drag = value(0u).Vector.x;
                }
                break;
            case VfxModuleType::WindInfluence:
                if (firstModule(1u << 1u))
                {
                    params.Flags |= Rendering::kGpuEmitterFlagWind;
                    params.WindCoupling = value(0u).Vector.x;
                    params.WindScale = value(1u).Vector.x;
                }
                break;
            case VfxModuleType::Turbulence:
                if (firstModule(1u << 2u))
                {
                    params.Flags |= Rendering::kGpuEmitterFlagTurbulence;
                    params.TurbulenceStrength = value(0u).Vector.x;
                    params.TurbulenceFrequency = value(1u).Vector.x;
                }
                break;
            case VfxModuleType::ActivationFraction:
                if (firstModule(1u << 3u))
                {
                    params.Flags |= Rendering::kGpuEmitterFlagActivation;
                    params.ActivationFraction = value(0u).Vector.x;
                    params.ActivationRamp = value(1u).Vector.x;
                }
                break;
            case VfxModuleType::CollisionPlane:
            case VfxModuleType::CollisionHeightField:
            case VfxModuleType::CollisionDepth:
            {
                const bool bPlane = module.Type == VfxModuleType::CollisionPlane;
                const bool bDepth = module.Type == VfxModuleType::CollisionDepth;
                const uint32_t bit = bPlane ? (1u << 4u) : bDepth ? (1u << 6u) : (1u << 5u);
                if (!firstModule(bit))
                {
                    break;
                }
                Rendering::GpuCollisionParams& collision = bPlane ? params.PlaneCollision :
                    bDepth ? params.DepthCollision : params.HeightFieldCollision;
                const uint32_t responseIndex = bPlane ? 2u : bDepth ? 1u : 0u;
                if (bPlane)
                {
                    params.Flags |= Rendering::kGpuEmitterFlagCollisionPlane;
                    params.GroundY = value(0u).Vector.x;
                    params.KillBelowY = value(1u).Vector.x;
                }
                else if (bDepth)
                {
                    params.Flags |= Rendering::kGpuEmitterFlagCollisionDepth;
                    params.DepthThickness = value(0u).Vector.x;
                }
                else
                {
                    params.Flags |= Rendering::kGpuEmitterFlagCollisionHeightField;
                }
                static_assert(static_cast<uint32_t>(VfxCollisionResponse::ReturnToCameraBoxTop) == 4u,
                    "衝突応答の値は ParticleSimulation.glsl と一致させる");
                collision.Response = value(responseIndex).UInt;
                collision.Restitution = value(responseIndex + 1u).Vector.x;
                collision.ChildProbability = value(responseIndex + 3u).Vector.x;
                const Container::String& childName = value(responseIndex + 2u).Name;
                for (uint32_t childIndex = 0u; childIndex < slot.Asset.Emitters.size(); ++childIndex)
                {
                    if (slot.Asset.Emitters[childIndex].Name == childName)
                    {
                        collision.ChildEmitter = frameIndices[childIndex];
                        break;
                    }
                }
                break;
            }
            case VfxModuleType::GpuSpriteRenderer:
                if (firstModule(1u << 7u))
                {
                    params.RenderFlags =
                        ((value(0u).UInt & Rendering::kGpuEmitterRenderOrientationMask) << Rendering::kGpuEmitterRenderOrientationShift) |
                        ((value(1u).UInt & Rendering::kGpuEmitterRenderBlendMask) << Rendering::kGpuEmitterRenderBlendShift) |
                        ((value(2u).UInt & Rendering::kGpuEmitterRenderPassMask) << Rendering::kGpuEmitterRenderPassShift) |
                        ((value(4u).UInt & Rendering::kGpuEmitterRenderLightingMask) << Rendering::kGpuEmitterRenderLightingShift) |
                        ((value(7u).UInt & Rendering::kGpuEmitterRenderSubPixelCompensationMask) << Rendering::kGpuEmitterRenderSubPixelCompensationShift);
                    params.SoftDistance = value(3u).Vector.x;
                    params.FogInfluence = value(5u).Vector.x;
                    params.StreakShutterSeconds = value(6u).Vector.x;
                    params.NearFadeDistance = value(8u).Vector.x;
                }
                break;
            default:
                break;
            }
        }
        return params;
    }

    void VfxEffectSystem::BuildGpuParticleFrame(Rendering::GpuParticleScene& out)
    {
        out.Clear();
        GpuExecutionState& gpu = EnsureGpuState();
        struct FrameReference
        {
            uint32_t InstanceIndex;
            uint32_t EmitterIndex;
        };
        VariableArray<FrameReference> references;
        VariableArray<VariableArray<uint32_t>> frameIndices;
        frameIndices.resize(m_Instances.size());
        for (uint32_t instanceIndex = 0u; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            InstanceSlot& slot = m_Instances[instanceIndex];
            if (!slot.bActive)
            {
                continue;
            }
            frameIndices[instanceIndex].resize(slot.Emitters.size(), Rendering::kGpuParticleInvalidEmitter);
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                EmitterState& emitter = slot.Emitters[emitterIndex];
                const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
                if (emitter.Executor == VfxExecutor::Gpu ||
                    asset.Properties[slot.PropertyIndices[ExecutorProperty]].UInt == static_cast<uint32_t>(VfxExecutor::Gpu))
                {
                    RefreshEmitter(slot, emitterIndex);
                }
                Rendering::GpuParticleEmitterRange range;
                if (emitter.Executor != VfxExecutor::Gpu ||
                    !gpu.Allocator.TryGetRange({emitter.GpuPoolIndex, emitter.GpuPoolGeneration}, range))
                {
                    continue;
                }
                frameIndices[instanceIndex][emitterIndex] = static_cast<uint32_t>(references.size());
                references.push_back({instanceIndex, emitterIndex});
            }
        }

        VariableArray<Rendering::GpuParticleSpawnRequest> requests;
        VariableArray<Rendering::GpuParticleSpawnState> states;
        requests.reserve(references.size());
        states.reserve(references.size());
        out.Emitters.reserve(references.size());
        for (uint32_t frameIndex = 0u; frameIndex < references.size(); ++frameIndex)
        {
            const FrameReference& reference = references[frameIndex];
            InstanceSlot& slot = m_Instances[reference.InstanceIndex];
            EmitterState& emitter = slot.Emitters[reference.EmitterIndex];
            Rendering::GpuParticleEmitterRange range;
            gpu.Allocator.TryGetRange({emitter.GpuPoolIndex, emitter.GpuPoolGeneration}, range);
            Rendering::GpuParticleEmitterFrame frame;
            frame.InstanceId = (static_cast<uint64_t>(slot.Generation) << 32u) | reference.InstanceIndex;
            frame.EmitterIndex = reference.EmitterIndex;
            frame.PoolSlot = emitter.GpuPoolIndex;
            frame.AllocationGeneration = emitter.GpuPoolGeneration;
            frame.World = Math::MatrixUtils::CreateTranslation(slot.Desc.Position);
            frame.PreviousWorld = Math::MatrixUtils::CreateTranslation(
                slot.bHasPreviousGpuPosition ? slot.PreviousGpuPosition : slot.Desc.Position);
            frame.bEnabled = slot.Desc.bEnabled;
            frame.Params = PackGpuEmitterParams(slot, reference.EmitterIndex, emitter, frameIndices[reference.InstanceIndex], out);
            frame.Params.PoolOffset = range.Offset;
            frame.Params.Capacity = range.Capacity;
            out.Emitters.push_back(frame);

            float rate = 0.0f;
            if (slot.Desc.bEnabled)
            {
                const VfxEmitterAsset& asset = slot.Asset.Emitters[reference.EmitterIndex];
                for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                {
                    const VfxModule& module = asset.Modules[moduleIndex];
                    if (module.Type == VfxModuleType::SpawnRate)
                    {
                        rate += module.Values[emitter.Bindings[moduleIndex].Arguments[0]].Vector.x;
                    }
                }
            }
            requests.push_back({frameIndex, rate, slot.Desc.bEnabled ? emitter.GpuPendingDeltaTime : 0.0f,
                slot.Desc.bEnabled ? emitter.GpuPendingBurstCount : 0u, range.Capacity});
            states.push_back({emitter.SpawnAccumulator, emitter.NextSerial});
        }
        out.JobSpawnCount = Rendering::GpuParticleSpawnPlanner::Plan(
            Container::Span<const Rendering::GpuParticleSpawnRequest>(requests.data(), requests.size()),
            Container::Span<Rendering::GpuParticleSpawnState>(states.data(), states.size()), out.SpawnJobs);
        for (uint32_t frameIndex = 0u; frameIndex < references.size(); ++frameIndex)
        {
            const FrameReference& reference = references[frameIndex];
            InstanceSlot& slot = m_Instances[reference.InstanceIndex];
            EmitterState& emitter = slot.Emitters[reference.EmitterIndex];
            if (slot.Desc.bEnabled)
            {
                emitter.SpawnAccumulator = states[frameIndex].Accumulator;
                emitter.NextSerial = states[frameIndex].NextSerial;
            }
            out.Emitters[frameIndex].Params.SerialBase = emitter.NextSerial;
        }
        for (const Rendering::GpuSpawnJob& job : out.SpawnJobs)
        {
            out.Emitters[job.EmitterIndex].Params.SerialBase = job.FirstSerial;
            out.Emitters[job.EmitterIndex].Params.SpawnCount = job.Count;
        }
        out.TotalSpawnCount = out.JobSpawnCount;
        out.Bursts.OverflowCount = gpu.OverflowCount;
        for (const GpuExecutionState::PendingBurst& pending : gpu.Bursts)
        {
            if (!IsLiveInstance(pending.Handle))
            {
                continue;
            }
            InstanceSlot& slot = m_Instances[pending.Handle.Index];
            if (pending.EmitterIndex >= slot.Emitters.size())
            {
                continue;
            }
            EmitterState& emitter = slot.Emitters[pending.EmitterIndex];
            const uint32_t frameIndex = frameIndices[pending.Handle.Index][pending.EmitterIndex];
            if (frameIndex == Rendering::kGpuParticleInvalidEmitter || emitter.GpuEpoch != pending.Epoch)
            {
                continue;
            }
            Rendering::GpuParticleBurst burst = pending.Burst;
            const uint32_t requested = burst.Count;
            burst.Count = requested < out.Emitters[frameIndex].Params.Capacity ? requested : out.Emitters[frameIndex].Params.Capacity;
            burst.EmitterIndex = frameIndex;
            burst.FirstSerial = emitter.NextSerial;
            burst.OutputOffset = out.TotalSpawnCount;
            emitter.NextSerial += requested;
            if (out.Bursts.Push(burst))
            {
                out.TotalSpawnCount += burst.Count;
            }
        }
        out.DeltaTime = gpu.PendingDeltaTime;
        out.UnscaledDeltaTime = gpu.PendingUnscaledDeltaTime;
        out.ParticleTime = gpu.ParticleTime;
        out.UnscaledTime = gpu.UnscaledTime;
        gpu.PendingDeltaTime = 0.0f;
        gpu.PendingUnscaledDeltaTime = 0.0f;
        gpu.Bursts.clear();
        gpu.OverflowCount = 0u;
        for (uint32_t instanceIndex = 0u; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            InstanceSlot& slot = m_Instances[instanceIndex];
            if (!slot.bActive)
            {
                continue;
            }
            bool bHasGpuEmitter = false;
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                EmitterState& emitter = slot.Emitters[emitterIndex];
                emitter.GpuPendingDeltaTime = 0.0f;
                emitter.GpuPendingBurstCount = 0u;
                bHasGpuEmitter |= frameIndices[instanceIndex][emitterIndex] != Rendering::kGpuParticleInvalidEmitter;
            }
            if (bHasGpuEmitter)
            {
                slot.PreviousGpuPosition = slot.Desc.Position;
                slot.bHasPreviousGpuPosition = true;
            }
        }
    }

    double VfxEffectSystem::GetInstanceTime(VfxEffectHandle handle) const
    {
        return IsLiveInstance(handle) ? m_Instances[handle.Index].Time : -1.0;
    }

    uint32_t VfxEffectSystem::GetEmitterCount(VfxEffectHandle handle) const
    {
        return IsLiveInstance(handle) ? static_cast<uint32_t>(m_Instances[handle.Index].Emitters.size()) : 0u;
    }

    bool VfxEffectSystem::CopyParticles(VfxEffectHandle handle, uint32_t emitterIndex, VariableArray<VfxParticleState>& outParticles) const
    {
        if (!IsLiveInstance(handle) || emitterIndex >= m_Instances[handle.Index].Emitters.size())
        {
            return false;
        }
        outParticles = m_Instances[handle.Index].Emitters[emitterIndex].Particles;
        return true;
    }

    void VfxEffectSystem::AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const
    {
        constexpr Rendering::RenderLayer layers[] = {
            Rendering::RenderLayer::Default, Rendering::RenderLayer::Transparent, Rendering::RenderLayer::UI};
        for (uint32_t instanceIndex = 0u; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const InstanceSlot& slot = m_Instances[instanceIndex];
            if (!slot.bActive)
            {
                continue;
            }
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                const EmitterState& emitter = slot.Emitters[emitterIndex];
                if (!emitter.bHasRenderer)
                {
                    continue;
                }
                const uint64_t id = (uint64_t{3} << 62u) |
                    (static_cast<uint64_t>(slot.Generation & 0x3FFFFFFFu) << 32u) |
                    static_cast<uint64_t>((instanceIndex << 8u | emitterIndex) + 1u);
                for (const VfxParticleState& particle : emitter.Particles)
                {
                    if (particle.Activation <= 0.0f)
                    {
                        continue;
                    }
                    Rendering::BoardProxy proxy;
                    proxy.ObjectId = id;
                    proxy.ComponentId = id;
                    proxy.SortKey = emitter.SortKey;
                    proxy.LayerPriority = static_cast<uint32_t>(proxy.SortKey >> 32u);
                    proxy.OrderInLayer = emitter.SortKey;
                    proxy.Texture = slot.Desc.Texture;
                    proxy.WorldTransform.SetTranslationRow(particle.Position);
                    proxy.PreviousWorldTransform = proxy.WorldTransform;
                    proxy.LayerMask = layers[emitter.Layer];
                    proxy.Space = emitter.Space == static_cast<uint32_t>(VfxSimulationSpace::Screen) ?
                        Rendering::BoardSpace::ScreenSpace : Rendering::BoardSpace::WorldSpace;
                    proxy.BlendModeProp = static_cast<Rendering::BlendMode>(emitter.Blend);
                    proxy.Tint = particle.Color;
                    if (particle.Activation != 1.0f)
                    {
                        proxy.Tint.w *= particle.Activation;
                    }
                    if (proxy.Space == Rendering::BoardSpace::ScreenSpace)
                    {
                        proxy.SizePx = particle.Size;
                    }
                    else
                    {
                        proxy.SizeWorld = particle.Size;
                    }
                    proxy.UVRect = particle.UVRect;
                    proxy.bVisible = true;
                    outProxies.push_back(proxy);
                }
            }
        }
    }

    void VfxEffectSystem::AppendMeshInstances(VariableArray<VfxMeshInstance>& outInstances) const
    {
        uint64_t emitterOrdinal = 0u;
        for (uint32_t instanceIndex = 0u; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const InstanceSlot& slot = m_Instances[instanceIndex];
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                const uint64_t emitterId = emitterOrdinal++ << 16u;
                const EmitterState& emitter = slot.Emitters[emitterIndex];
                if (!slot.bActive || !slot.Desc.bEnabled || !emitter.bHasMeshRenderer)
                {
                    continue;
                }
                for (uint32_t particleIndex = 0u; particleIndex < emitter.Particles.size(); ++particleIndex)
                {
                    const VfxParticleState& particle = emitter.Particles[particleIndex];
                    if (particle.Activation <= 0.0f)
                    {
                        continue;
                    }
                    Math::Quaternion rotation = Math::Quaternion::Identity;
                    if (emitter.bAlignToVelocity)
                    {
                        const double x = particle.Velocity.x;
                        const double y = particle.Velocity.y;
                        const double z = particle.Velocity.z;
                        const double speed = std::hypot(x, y, z);
                        if (std::isfinite(speed) && speed > 1.0e-6)
                        {
                            const double horizontal = std::hypot(x, y);
                            if (horizontal > 0.0)
                            {
                                // 行ベクトル規約では、速度から局所の +Z への回転を指定する。
                                const Math::Vector3 axis(static_cast<float>(y / horizontal), static_cast<float>(-x / horizontal), 0.0f);
                                rotation = Math::QuaternionUtils::FromAxisAngle(axis, static_cast<float>(std::atan2(horizontal, z)));
                            }
                            else if (z < 0.0)
                            {
                                rotation = Math::QuaternionUtils::FromAxisAngle(Math::Vector3::UnitX, Math::Constants::PI);
                            }
                        }
                    }
                    const float size = particle.Size.x;
                    VfxMeshInstance instance;
                    // 板と異なる帯にエミッタの通し番号と粒の添字を置く。粒は最大 65536 個。
                    instance.Id = (uint64_t{1} << 62u) | emitterId | static_cast<uint64_t>(particleIndex);
                    instance.World = Math::MatrixUtils::CreateWorldRowVector(particle.Position, rotation, Math::Vector3(size, size, size));
                    instance.Color = particle.Color;
                    if (particle.Activation != 1.0f)
                    {
                        instance.Color.w *= particle.Activation;
                    }
                    instance.DissolveThreshold = ComputeDissolveThreshold(particle.Age, particle.Lifetime, emitter.DissolveStart, emitter.DissolveDuration);
                    instance.EdgeWidth = emitter.EdgeWidth;
                    instance.EdgeColor = emitter.EdgeColor;
                    instance.Mesh = slot.Desc.Mesh;
                    instance.Material = slot.Desc.Material;
                    instance.InstanceIndex = instanceIndex;
                    instance.EmitterIndex = emitterIndex;
                    instance.ParticleIndex = particleIndex;
                    outInstances.push_back(instance);
                }
            }
        }
    }

    float VfxEffectSystem::ComputeDissolveThreshold(float age, float lifetime, float dissolveStart, float dissolveDuration)
    {
        const double normalizedAge = lifetime > 0.0f ? static_cast<double>(age) / lifetime : 1.0;
        if (dissolveDuration > 0.0f)
        {
            const double threshold = (normalizedAge - static_cast<double>(dissolveStart)) / dissolveDuration;
            if (!(threshold > 0.0))
            {
                return 0.0f;
            }
            return threshold >= 1.0 ? 1.0f : static_cast<float>(threshold);
        }
        return normalizedAge >= dissolveStart ? 1.0f : 0.0f;
    }

    uint32_t VfxEffectSystem::ComputeFlipbookFrame(const VfxFlipbookImportSettings& flipbook, float age, float lifetime)
    {
        if (flipbook.FrameCount == 0u || !std::isfinite(age) || age < 0.0f)
        {
            return 0u;
        }
        const float progressFloat = flipbook.FramesPerSecond > 0.0f ? age * flipbook.FramesPerSecond :
            (lifetime > 0.0f ? age / lifetime * static_cast<float>(flipbook.FrameCount) : 0.0f);
        const double progress = std::isfinite(progressFloat) ? static_cast<double>(progressFloat) :
            (flipbook.FramesPerSecond > 0.0f ? static_cast<double>(age) * flipbook.FramesPerSecond :
                (lifetime > 0.0f ? static_cast<double>(age) / lifetime * flipbook.FrameCount : 0.0));
        const double frame = static_cast<double>(flipbook.StartFrame) + std::floor(progress);
        if (!std::isfinite(frame))
        {
            return flipbook.bLoop ? 0u : flipbook.FrameCount - 1u;
        }
        if (flipbook.bLoop)
        {
            return static_cast<uint32_t>(std::fmod(frame, static_cast<double>(flipbook.FrameCount)));
        }
        return frame >= flipbook.FrameCount ? flipbook.FrameCount - 1u : static_cast<uint32_t>(frame);
    }

    /**
     * @brief 板の UVRect は (オフセット u, オフセット v, 幅 u, 高さ v)。
     * シェーダーは UVRect.xy + 頂点 UV * UVRect.zw でサンプル位置を求める。
     */
    Math::Vector4 VfxEffectSystem::ComputeFlipbookUVRect(const VfxFlipbookImportSettings& flipbook, uint32_t frame)
    {
        if (flipbook.Columns == 0u || flipbook.Rows == 0u)
        {
            return Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        }
        const float width = 1.0f / static_cast<float>(flipbook.Columns);
        const float height = 1.0f / static_cast<float>(flipbook.Rows);
        return Math::Vector4(static_cast<float>(frame % flipbook.Columns) * width,
            static_cast<float>(frame / flipbook.Columns) * height, width, height);
    }

    uint32_t VfxEffectSystem::InitialRandomState(uint32_t seed, uint32_t emitterIndex)
    {
        const uint32_t state = seed ^ ((emitterIndex + 1u) * 0x9E3779B9u) ^ 0x85EBCA6Bu;
        return state == 0u ? 0xA341316Cu : state;
    }

    float VfxEffectSystem::NextUnitFloat(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return static_cast<float>(state >> 8u) / 16777216.0f;
    }

    bool VfxEffectSystem::CacheBindings(InstanceSlot& slot)
    {
        const VfxObjectSchema& schema = GetVfxEmitterSchema();
        constexpr const char* propertyNames[] = {"MaxParticles", "Space", "Seed", "Executor", "RandomStream", "Capacity", "TimeChannel", "WrapPolicy"};
        static_assert(sizeof(propertyNames) / sizeof(propertyNames[0]) == EmitterPropertyCount);
        static_assert(sizeof(slot.PropertyIndices) / sizeof(slot.PropertyIndices[0]) == EmitterPropertyCount);
        for (uint32_t index = 0u; index < EmitterPropertyCount; ++index)
        {
            const int32_t propertyIndex = FindVfxParameterIndex(schema, propertyNames[index]);
            if (propertyIndex < 0)
            {
                return false;
            }
            slot.PropertyIndices[index] = static_cast<uint32_t>(propertyIndex);
        }
        for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
        {
            EmitterState& emitter = slot.Emitters[emitterIndex];
            const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
            emitter.Bindings.resize(asset.Modules.size());
            for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
            {
                const VfxModule& module = asset.Modules[moduleIndex];
                const VfxObjectSchema* moduleSchema = FindVfxModuleSchema(module.Type);
                ModuleBinding& binding = emitter.Bindings[moduleIndex];
                if (moduleSchema == nullptr || moduleSchema->ParameterCount > sizeof(binding.Arguments) / sizeof(binding.Arguments[0]))
                {
                    return false;
                }
                for (uint32_t index = 0u; index < moduleSchema->ParameterCount; ++index)
                {
                    const char* name = ArgumentName(module.Type, index);
                    if (name == nullptr)
                    {
                        return false;
                    }
                    const int32_t argumentIndex = FindVfxParameterIndex(*moduleSchema, name);
                    if (argumentIndex < 0)
                    {
                        return false;
                    }
                    binding.Arguments[index] = static_cast<uint32_t>(argumentIndex);
                }
            }
        }
        return true;
    }

    void VfxEffectSystem::RefreshEmitter(InstanceSlot& slot, uint32_t emitterIndex)
    {
        EmitterState& emitter = slot.Emitters[emitterIndex];
        const VfxExecutor previousExecutor = emitter.Executor;
        const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
        const uint32_t capacity = asset.Properties[slot.PropertyIndices[CapacityProperty]].UInt;
        emitter.MaxParticles = capacity != 0u ? capacity : asset.Properties[slot.PropertyIndices[MaxParticlesProperty]].UInt;
        emitter.Space = asset.Properties[slot.PropertyIndices[SpaceProperty]].UInt;
        emitter.Executor = static_cast<VfxExecutor>(asset.Properties[slot.PropertyIndices[ExecutorProperty]].UInt);
        emitter.RandomStream = static_cast<VfxRandomStream>(asset.Properties[slot.PropertyIndices[RandomStreamProperty]].UInt);
        emitter.TimeChannel = static_cast<VfxTimeChannel>(asset.Properties[slot.PropertyIndices[TimeChannelProperty]].UInt);
        emitter.WrapPolicy = static_cast<VfxWrapPolicy>(asset.Properties[slot.PropertyIndices[WrapPolicyProperty]].UInt);
        const uint32_t seed = slot.Desc.Seed ^ asset.Properties[slot.PropertyIndices[SeedProperty]].UInt;
        if (emitter.RandomState == 0u || seed != emitter.Seed)
        {
            emitter.Seed = seed;
            emitter.RandomState = InitialRandomState(seed, emitterIndex);
            emitter.StreamSeed = emitter.RandomState;
        }
        emitter.Shape = VfxSpawnShape::Point;
        emitter.Drag = 0.0f;
        emitter.bHasDrag = false;
        emitter.WindInfluences.clear();
        emitter.Turbulences.clear();
        emitter.Collisions.clear();
        for (LifeCurveState& curve : emitter.Curves)
        {
            curve.KeyCount = 0u;
        }
        emitter.bHasCurves = false;
        emitter.bHasActivation = false;
        emitter.Offset = Math::Vector3(0.0f, 0.0f, 0.0f);
        emitter.Acceleration = Math::Vector3(0.0f, 0.0f, 0.0f);
        emitter.VelocityMin = Math::Vector3(0.0f, 0.0f, 0.0f);
        emitter.VelocityMax = Math::Vector3(0.0f, 0.0f, 0.0f);
        emitter.Lifetime = 1.0f;
        emitter.Color = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        emitter.Size = Math::Vector2(8.0f, 8.0f);
        emitter.bHasVelocity = false;
        emitter.bHasRenderer = false;
        emitter.bHasMeshRenderer = false;
        emitter.FlipbookIndex = -1;
        for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
        {
            const VfxModule& module = asset.Modules[moduleIndex];
            const ModuleBinding& binding = emitter.Bindings[moduleIndex];
            const VfxValue& first = module.Values[binding.Arguments[0]];
            switch (module.Type)
            {
            case VfxModuleType::InitPosition:
                emitter.Offset += ToVector3(first);
                break;
            case VfxModuleType::InitVelocityBox:
                emitter.bHasVelocity = true;
                emitter.VelocityMin = ToVector3(first);
                emitter.VelocityMax = ToVector3(module.Values[binding.Arguments[1]]);
                break;
            case VfxModuleType::InitLifetime:
                emitter.Lifetime = first.Vector.x;
                break;
            case VfxModuleType::InitColor:
                emitter.Color = first.Vector;
                break;
            case VfxModuleType::InitSize:
                emitter.Size = Math::Vector2(first.Vector.x, first.Vector.y);
                break;
            case VfxModuleType::InitShape:
                emitter.Shape = static_cast<VfxSpawnShape>(first.UInt);
                emitter.ShapeRadius = module.Values[binding.Arguments[1]].Vector.x;
                emitter.ConeAngle = module.Values[binding.Arguments[2]].Vector.x;
                emitter.ConeLength = module.Values[binding.Arguments[3]].Vector.x;
                emitter.BoxHalfExtents = ToVector3(module.Values[binding.Arguments[4]]);
                emitter.CameraBoxHalfWidth = module.Values[binding.Arguments[5]].Vector.x;
                emitter.CameraBoxHeight = module.Values[binding.Arguments[6]].Vector.x;
                break;
            case VfxModuleType::Acceleration:
                emitter.Acceleration += ToVector3(first);
                break;
            case VfxModuleType::Drag:
                emitter.bHasDrag = true;
                emitter.Drag += first.Vector.x;
                break;
            case VfxModuleType::WindInfluence:
                emitter.WindInfluences.push_back({first.Vector.x, module.Values[binding.Arguments[1]].Vector.x});
                break;
            case VfxModuleType::Turbulence:
                emitter.Turbulences.push_back({first.Vector.x, module.Values[binding.Arguments[1]].Vector.x});
                break;
            case VfxModuleType::CurveOverLife:
            {
                emitter.bHasCurves = true;
                LifeCurveState& curve = emitter.Curves[first.UInt];
                curve.KeyCount = module.Values[binding.Arguments[1]].UInt;
                for (uint32_t index = 0u; index < curve.KeyCount; ++index)
                {
                    const Math::Vector4& key = module.Values[binding.Arguments[2u + index]].Vector;
                    curve.Keys[index] = {static_cast<double>(key.x), key.y};
                }
                curve.Color = module.Values[binding.Arguments[10]].Vector;
                break;
            }
            case VfxModuleType::ActivationFraction:
                emitter.bHasActivation = true;
                emitter.ActivationFraction = first.Vector.x;
                emitter.ActivationRamp = module.Values[binding.Arguments[1]].Vector.x;
                break;
            case VfxModuleType::CollisionPlane:
            case VfxModuleType::CollisionHeightField:
            {
                CollisionState collision;
                collision.Type = module.Type;
                const uint32_t responseIndex = module.Type == VfxModuleType::CollisionPlane ? 2u : 0u;
                if (module.Type == VfxModuleType::CollisionPlane)
                {
                    collision.GroundY = first.Vector.x;
                    collision.KillBelowY = module.Values[binding.Arguments[1]].Vector.x;
                }
                collision.Response = static_cast<VfxCollisionResponse>(module.Values[binding.Arguments[responseIndex]].UInt);
                collision.Restitution = module.Values[binding.Arguments[responseIndex + 1u]].Vector.x;
                collision.ChildProbability = module.Values[binding.Arguments[responseIndex + 3u]].Vector.x;
                if (collision.Response == VfxCollisionResponse::SpawnChild)
                {
                    const Container::String& name = module.Values[binding.Arguments[responseIndex + 2u]].Name;
                    for (uint32_t index = 0u; index < slot.Asset.Emitters.size(); ++index)
                    {
                        if (slot.Asset.Emitters[index].Name == name)
                        {
                            collision.ChildEmitterIndex = index;
                            break;
                        }
                    }
                }
                emitter.Collisions.push_back(collision);
                break;
            }
            case VfxModuleType::SpriteRenderer:
                emitter.bHasRenderer = true;
                emitter.Layer = first.UInt;
                emitter.Blend = module.Values[binding.Arguments[1]].UInt;
                emitter.SortKey = module.Values[binding.Arguments[2]].UInt;
                for (uint32_t index = 0u; index < slot.Asset.Flipbooks.size(); ++index)
                {
                    if (slot.Asset.Flipbooks[index].Name == module.Values[binding.Arguments[3]].Name)
                    {
                        emitter.FlipbookIndex = static_cast<int32_t>(index);
                        break;
                    }
                }
                break;
            case VfxModuleType::MeshRenderer:
                emitter.bHasMeshRenderer = true;
                emitter.bAlignToVelocity = module.Values[binding.Arguments[1]].UInt != 0u;
                emitter.DissolveStart = module.Values[binding.Arguments[2]].Vector.x;
                emitter.DissolveDuration = module.Values[binding.Arguments[3]].Vector.x;
                emitter.EdgeWidth = module.Values[binding.Arguments[4]].Vector.x;
                emitter.EdgeColor = module.Values[binding.Arguments[5]].Vector;
                break;
            default:
                break;
            }
        }
        if (emitter.Executor != previousExecutor)
        {
            emitter.GpuPendingDeltaTime = 0.0f;
            emitter.GpuPendingBurstCount = 0u;
            if (emitter.Executor == VfxExecutor::Gpu)
            {
                emitter.Particles.clear();
                ++emitter.GpuEpoch;
            }
        }
        if (emitter.Executor == VfxExecutor::Gpu || emitter.GpuPoolGeneration != 0u)
        {
            GpuExecutionState& gpu = EnsureGpuState();
            Rendering::GpuParticleEmitterHandle handle{emitter.GpuPoolIndex, emitter.GpuPoolGeneration};
            Rendering::GpuParticleEmitterRange range;
            if (handle.IsValid() && (emitter.Executor != VfxExecutor::Gpu ||
                !gpu.Allocator.TryGetRange(handle, range) || range.Capacity != emitter.MaxParticles))
            {
                gpu.Allocator.Free(handle);
                handle = {};
            }
            if (emitter.Executor == VfxExecutor::Gpu && !handle.IsValid())
            {
                handle = gpu.Allocator.Allocate(emitter.MaxParticles);
                if (handle.IsValid() && handle.Index >= Rendering::kGpuParticleMaxEmitters)
                {
                    gpu.Allocator.Free(handle);
                    handle = {};
                }
                if (!handle.IsValid() && !emitter.bGpuAllocationWarned)
                {
                    LOG_WARNING("GPU エミッタの粒プールを確保できません");
                    emitter.bGpuAllocationWarned = true;
                }
            }
            emitter.GpuPoolIndex = handle.Index;
            emitter.GpuPoolGeneration = handle.Generation;
        }
    }

    void VfxEffectSystem::UpdateFlipbook(const InstanceSlot& slot, const EmitterState& emitter, VfxParticleState& particle)
    {
        particle.Frame = 0u;
        particle.UVRect = Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        if (emitter.FlipbookIndex >= 0)
        {
            const VfxFlipbookImportSettings& flipbook = slot.Asset.Flipbooks[static_cast<uint32_t>(emitter.FlipbookIndex)];
            particle.Frame = ComputeFlipbookFrame(flipbook, particle.Age, particle.Lifetime);
            particle.UVRect = ComputeFlipbookUVRect(flipbook, particle.Frame);
        }
    }

    void VfxEffectSystem::UpdateOverLife(const EmitterState& emitter, VfxParticleState& particle)
    {
        if (emitter.bHasCurves)
        {
            particle.Color = particle.BaseColor;
            particle.Size = particle.BaseSize;
            particle.Stretch = 1.0f;
            const float time = std::fmax(0.0f, std::fmin(1.0f, particle.Age / particle.Lifetime));
            const auto evaluate = [time](const LifeCurveState& curve)
            {
                return static_cast<float>(Math::EvaluatePiecewiseLinearUnchecked(
                    Container::Span<const Math::CurveKeyframe>(curve.Keys, curve.KeyCount), time));
            };
            const LifeCurveState& size = emitter.Curves[static_cast<uint32_t>(VfxCurveTarget::Size)];
            const LifeCurveState& color = emitter.Curves[static_cast<uint32_t>(VfxCurveTarget::Color)];
            const LifeCurveState& opacity = emitter.Curves[static_cast<uint32_t>(VfxCurveTarget::Opacity)];
            const LifeCurveState& stretch = emitter.Curves[static_cast<uint32_t>(VfxCurveTarget::Stretch)];
            if (size.KeyCount != 0u)
            {
                particle.Size = particle.BaseSize * evaluate(size);
            }
            if (color.KeyCount != 0u)
            {
                particle.Color = particle.BaseColor + (color.Color - particle.BaseColor) * evaluate(color);
            }
            if (opacity.KeyCount != 0u)
            {
                particle.Color.w *= evaluate(opacity);
            }
            if (stretch.KeyCount != 0u)
            {
                particle.Stretch = evaluate(stretch);
            }
        }
        if (emitter.bHasActivation)
        {
            const float hash = VfxActivationHash(emitter.StreamSeed, particle.Serial);
            particle.Activation = VfxActivationWeight(emitter.ActivationFraction, emitter.ActivationRamp, hash);
        }
    }

    bool VfxEffectSystem::ResolveCollisions(const EmitterState& emitter, VfxParticleState& particle,
        const Math::Vector3& cameraPosition, const VfxHeightFieldView& heightField, VariableArray<ChildSpawn>& children)
    {
        for (const CollisionState& collision : emitter.Collisions)
        {
            float surface = 0.0f;
            if (collision.Type == VfxModuleType::CollisionPlane)
            {
                if (particle.Position.y < collision.KillBelowY)
                {
                    return false;
                }
                surface = collision.GroundY;
            }
            else if (!SampleHeightField(heightField, particle.Position, surface))
            {
                continue;
            }
            if (!(particle.Position.y < surface))
            {
                continue;
            }
            switch (collision.Response)
            {
            case VfxCollisionResponse::Kill:
                return false;
            case VfxCollisionResponse::Stick:
                particle.Position.y = surface;
                particle.Velocity = Math::Vector3(0.0f, 0.0f, 0.0f);
                particle.bStuck = true;
                return true;
            case VfxCollisionResponse::Bounce:
                particle.Position.y = surface;
                if (particle.Velocity.y < 0.0f)
                {
                    particle.Velocity.y = -particle.Velocity.y * collision.Restitution;
                }
                break;
            case VfxCollisionResponse::SpawnChild:
                if (collision.ChildEmitterIndex != std::numeric_limits<uint32_t>::max() &&
                    VfxCounterUnitFloat(emitter.StreamSeed, particle.Serial, VfxRandomDraw::ChildSpawn) < collision.ChildProbability)
                {
                    ChildSpawn child;
                    child.EmitterIndex = collision.ChildEmitterIndex;
                    child.Position = particle.Position;
                    child.Position.y = surface;
                    children.push_back(child);
                }
                return false;
            case VfxCollisionResponse::ReturnToCameraBoxTop:
            {
                if (emitter.Shape != VfxSpawnShape::CameraBox)
                {
                    return false;
                }
                const float top = cameraPosition.y + emitter.CameraBoxHeight * 0.5f;
                if (surface < top)
                {
                    particle.Position.y = top;
                    return true;
                }
                continue;
            }
            }
        }
        return true;
    }

    void VfxEffectSystem::SpawnParticles(InstanceSlot& slot, EmitterState& emitter, uint32_t count,
        const Math::Vector3& position, double birthTime, float age, const VfxTickContext& context,
        const Math::Vector3* orientationNormal, bool bUsePositionForCameraBox)
    {
        const uint32_t firstSerial = emitter.NextSerial;
        emitter.NextSerial += count;
        const uint32_t available = emitter.MaxParticles - static_cast<uint32_t>(emitter.Particles.size());
        const uint32_t spawnCount = count < available ? count : available;
        Math::Vector3 tangent;
        Math::Vector3 bitangent;
        if (orientationNormal != nullptr && emitter.bHasVelocity && spawnCount > 0u)
        {
            const Math::Vector3 helper = std::fabs(orientationNormal->y) < 0.999f ?
                Math::Vector3(0.0f, 1.0f, 0.0f) : Math::Vector3(1.0f, 0.0f, 0.0f);
            tangent = Math::Vector3::Cross(helper, *orientationNormal).Normalized();
            bitangent = Math::Vector3::Cross(*orientationNormal, tangent);
        }
        for (uint32_t index = 0u; index < spawnCount; ++index)
        {
            VfxParticleState particle;
            particle.Serial = firstSerial + index;
            particle.Position = (emitter.Shape == VfxSpawnShape::CameraBox && !bUsePositionForCameraBox ?
                context.CameraPosition : position) + emitter.Offset;
            particle.Velocity = Math::Vector3(0.0f, 0.0f, 0.0f);
            const auto draw = [&](VfxRandomDraw kind)
            {
                return emitter.RandomStream == VfxRandomStream::Counter ?
                    VfxCounterUnitFloat(emitter.StreamSeed, particle.Serial, kind) : NextUnitFloat(emitter.RandomState);
            };
            if (emitter.bHasVelocity)
            {
                particle.Velocity.x = emitter.VelocityMin.x + (emitter.VelocityMax.x - emitter.VelocityMin.x) * draw(VfxRandomDraw::VelocityX);
                particle.Velocity.y = emitter.VelocityMin.y + (emitter.VelocityMax.y - emitter.VelocityMin.y) * draw(VfxRandomDraw::VelocityY);
                particle.Velocity.z = emitter.VelocityMin.z + (emitter.VelocityMax.z - emitter.VelocityMin.z) * draw(VfxRandomDraw::VelocityZ);
                if (orientationNormal != nullptr)
                {
                    const Math::Vector3 localVelocity = particle.Velocity;
                    particle.Velocity = tangent * localVelocity.x + bitangent * localVelocity.y + *orientationNormal * localVelocity.z;
                }
            }
            if (emitter.Shape != VfxSpawnShape::Point)
            {
                const float a = draw(VfxRandomDraw::ShapeA);
                const float b = draw(VfxRandomDraw::ShapeB);
                const float c = draw(VfxRandomDraw::ShapeC);
                Math::Vector3 sample;
                switch (emitter.Shape)
                {
                case VfxSpawnShape::Sphere:
                {
                    const float y = 1.0f - 2.0f * a;
                    const float horizontal = std::sqrt(std::fmax(0.0f, 1.0f - y * y));
                    const float phi = 2.0f * Math::Constants::PI * b;
                    const float radius = emitter.ShapeRadius * std::cbrt(c);
                    sample = Math::Vector3(horizontal * std::cos(phi), y, horizontal * std::sin(phi)) * radius;
                    break;
                }
                case VfxSpawnShape::Cone:
                {
                    const float height = emitter.ConeLength * std::cbrt(a);
                    const double radians = static_cast<double>(emitter.ConeAngle) * (3.14159265358979323846 / 180.0);
                    const float radius = height * static_cast<float>(std::tan(radians)) * std::sqrt(b);
                    const float phi = 2.0f * Math::Constants::PI * c;
                    sample = Math::Vector3(radius * std::cos(phi), height, radius * std::sin(phi));
                    break;
                }
                case VfxSpawnShape::Box:
                    sample = Math::Vector3((2.0f * a - 1.0f) * emitter.BoxHalfExtents.x,
                        (2.0f * b - 1.0f) * emitter.BoxHalfExtents.y, (2.0f * c - 1.0f) * emitter.BoxHalfExtents.z);
                    break;
                case VfxSpawnShape::CameraBox:
                    sample = Math::Vector3((2.0f * a - 1.0f) * emitter.CameraBoxHalfWidth,
                        (b - 0.5f) * emitter.CameraBoxHeight, (2.0f * c - 1.0f) * emitter.CameraBoxHalfWidth);
                    break;
                default:
                    break;
                }
                particle.Position += sample;
            }
            particle.Color = emitter.Color;
            particle.Size = emitter.Size;
            particle.BaseColor = particle.Color;
            particle.BaseSize = particle.Size;
            particle.Lifetime = emitter.Lifetime;
            particle.BirthTime = birthTime;
            particle.Age = age;
            if (age > 0.0f)
            {
                particle.Velocity += emitter.Acceleration * age;
                particle.Position += particle.Velocity * age;
            }
            UpdateFlipbook(slot, emitter, particle);
            if (particle.Age < particle.Lifetime)
            {
                UpdateOverLife(emitter, particle);
                emitter.Particles.push_back(particle);
            }
        }
    }

    void VfxEffectSystem::ReleaseSlot(uint32_t index)
    {
        InstanceSlot& slot = m_Instances[index];
        if (m_GpuState)
        {
            for (const EmitterState& emitter : slot.Emitters)
            {
                m_GpuState->Allocator.Free({emitter.GpuPoolIndex, emitter.GpuPoolGeneration});
            }
        }
        slot.bHasPreviousGpuPosition = false;
        slot.Asset = VfxEffectAsset{};
        slot.Emitters.clear();
        slot.Events.clear();
        slot.Time = 0.0;
        slot.bActive = false;
        if (slot.Generation == std::numeric_limits<uint32_t>::max())
        {
            slot.bRetired = true;
            return;
        }
        ++slot.Generation;
        m_FreeInstanceIndices.push_back(index);
    }
} // namespace NorvesLib::Core::Particle
