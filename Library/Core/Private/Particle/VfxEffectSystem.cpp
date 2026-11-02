#include "Particle/VfxEffectSystem.h"

#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Particle/VfxSchema.h"
#include "Rendering/SceneProxy.h"

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

        const char* ArgumentName(VfxModuleType type, uint32_t index)
        {
            switch (type)
            {
            case VfxModuleType::SpawnRate:
                return "Rate";
            case VfxModuleType::SpawnBurst:
                return index == 0u ? "Count" : "Time";
            case VfxModuleType::SpawnOnEvent:
                return index == 0u ? "EventName" : "Count";
            case VfxModuleType::InitPosition:
                return "Offset";
            case VfxModuleType::InitVelocityBox:
                return index == 0u ? "Min" : "Max";
            case VfxModuleType::InitLifetime:
                return "Lifetime";
            case VfxModuleType::InitColor:
                return "Color";
            case VfxModuleType::InitSize:
                return "Size";
            case VfxModuleType::Acceleration:
                return "Acceleration";
            case VfxModuleType::SpriteRenderer:
            {
                constexpr const char* names[] = {"Layer", "Blend", "SortKey", "Flipbook"};
                return names[index];
            }
            case VfxModuleType::MeshRenderer:
            {
                constexpr const char* names[] = {"Mesh", "AlignToVelocity", "DissolveStart", "DissolveDuration", "EdgeWidth", "EdgeColor"};
                return names[index];
            }
            default:
                return "";
            }
        }
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
        slot.Events.clear();
        slot.Emitters.clear();
        slot.Emitters.resize(asset.Emitters.size());
        CacheBindings(slot);
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
        const uint32_t maxParticles = emitter.Properties[slot.PropertyIndices[0]].UInt;
        if (slot.Emitters[emitterIndex].Particles.size() > maxParticles)
        {
            slot.Emitters[emitterIndex].Particles.resize(maxParticles);
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

    void VfxEffectSystem::Tick(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            return;
        }
        for (InstanceSlot& slot : m_Instances)
        {
            if (!slot.bActive)
            {
                continue;
            }
            const double start = slot.Time;
            const double end = start + static_cast<double>(deltaTime);
            for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
            {
                RefreshEmitter(slot, emitterIndex);
                EmitterState& emitter = slot.Emitters[emitterIndex];
                uint32_t writeIndex = 0u;
                for (uint32_t readIndex = 0u; readIndex < emitter.Particles.size(); ++readIndex)
                {
                    VfxParticleState& particle = emitter.Particles[readIndex];
                    particle.Velocity += emitter.Acceleration * deltaTime;
                    particle.Position += particle.Velocity * deltaTime;
                    particle.Age += deltaTime;
                    if (particle.Age < particle.Lifetime)
                    {
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
                // 通常の端数演算は既存の粒子系と同じ float 精度を保つ。
                const float accumulated = emitter.SpawnAccumulator + rate * deltaTime;
                const double total = std::isfinite(accumulated) ? static_cast<double>(accumulated) :
                    static_cast<double>(emitter.SpawnAccumulator) + static_cast<double>(rate) * deltaTime;
                const double wholeCount = std::floor(total);
                emitter.SpawnAccumulator = static_cast<float>(total - wholeCount);
                const uint32_t available = emitter.MaxParticles - static_cast<uint32_t>(emitter.Particles.size());
                const uint32_t continuousCount = wholeCount >= available ? available : static_cast<uint32_t>(wholeCount);
                SpawnParticles(slot, emitter, continuousCount, slot.Desc.Position, end, 0.0f);

                for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
                {
                    const VfxModule& module = asset.Modules[moduleIndex];
                    const ModuleBinding& binding = emitter.Bindings[moduleIndex];
                    if (module.Type == VfxModuleType::SpawnBurst)
                    {
                        const double time = module.Values[binding.Arguments[1]].Vector.x;
                        if (start <= time && time < end)
                        {
                            SpawnParticles(slot, emitter, module.Values[binding.Arguments[0]].UInt, slot.Desc.Position, end, 0.0f);
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
                                event.Position, event.Time, static_cast<float>(end - event.Time),
                                event.bOriented ? &event.Normal : nullptr);
                        }
                    }
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

    void VfxEffectSystem::CacheBindings(InstanceSlot& slot)
    {
        const VfxObjectSchema& schema = GetVfxEmitterSchema();
        slot.PropertyIndices[0] = static_cast<uint32_t>(FindVfxParameterIndex(schema, "MaxParticles"));
        slot.PropertyIndices[1] = static_cast<uint32_t>(FindVfxParameterIndex(schema, "Space"));
        slot.PropertyIndices[2] = static_cast<uint32_t>(FindVfxParameterIndex(schema, "Seed"));
        for (uint32_t emitterIndex = 0u; emitterIndex < slot.Emitters.size(); ++emitterIndex)
        {
            EmitterState& emitter = slot.Emitters[emitterIndex];
            const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
            emitter.Bindings.resize(asset.Modules.size());
            for (uint32_t moduleIndex = 0u; moduleIndex < asset.Modules.size(); ++moduleIndex)
            {
                const VfxModule& module = asset.Modules[moduleIndex];
                const VfxObjectSchema& moduleSchema = *FindVfxModuleSchema(module.Type);
                for (uint32_t index = 0u; index < moduleSchema.ParameterCount; ++index)
                {
                    emitter.Bindings[moduleIndex].Arguments[index] =
                        static_cast<uint32_t>(FindVfxParameterIndex(moduleSchema, ArgumentName(module.Type, index)));
                }
            }
        }
    }

    void VfxEffectSystem::RefreshEmitter(InstanceSlot& slot, uint32_t emitterIndex)
    {
        EmitterState& emitter = slot.Emitters[emitterIndex];
        const VfxEmitterAsset& asset = slot.Asset.Emitters[emitterIndex];
        emitter.MaxParticles = asset.Properties[slot.PropertyIndices[0]].UInt;
        emitter.Space = asset.Properties[slot.PropertyIndices[1]].UInt;
        const uint32_t seed = slot.Desc.Seed ^ asset.Properties[slot.PropertyIndices[2]].UInt;
        if (emitter.RandomState == 0u || seed != emitter.Seed)
        {
            emitter.Seed = seed;
            emitter.RandomState = InitialRandomState(seed, emitterIndex);
        }
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
            case VfxModuleType::Acceleration:
                emitter.Acceleration += ToVector3(first);
                break;
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

    void VfxEffectSystem::SpawnParticles(InstanceSlot& slot, EmitterState& emitter, uint32_t count,
        const Math::Vector3& position, double birthTime, float age, const Math::Vector3* orientationNormal)
    {
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
            particle.Position = position + emitter.Offset;
            particle.Velocity = Math::Vector3(0.0f, 0.0f, 0.0f);
            if (emitter.bHasVelocity)
            {
                particle.Velocity.x = emitter.VelocityMin.x + (emitter.VelocityMax.x - emitter.VelocityMin.x) * NextUnitFloat(emitter.RandomState);
                particle.Velocity.y = emitter.VelocityMin.y + (emitter.VelocityMax.y - emitter.VelocityMin.y) * NextUnitFloat(emitter.RandomState);
                particle.Velocity.z = emitter.VelocityMin.z + (emitter.VelocityMax.z - emitter.VelocityMin.z) * NextUnitFloat(emitter.RandomState);
                if (orientationNormal != nullptr)
                {
                    const Math::Vector3 localVelocity = particle.Velocity;
                    particle.Velocity = tangent * localVelocity.x + bitangent * localVelocity.y + *orientationNormal * localVelocity.z;
                }
            }
            particle.Color = emitter.Color;
            particle.Size = emitter.Size;
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
                emitter.Particles.push_back(particle);
            }
        }
    }

    void VfxEffectSystem::ReleaseSlot(uint32_t index)
    {
        InstanceSlot& slot = m_Instances[index];
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
