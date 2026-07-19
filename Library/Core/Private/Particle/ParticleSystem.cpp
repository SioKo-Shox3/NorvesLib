#include "Particle/ParticleSystem.h"

#include "Rendering/SceneProxy.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        bool IsFinite(const Math::Vector2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFinite(const Math::Vector4& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z) && std::isfinite(value.w);
        }
    } // namespace

    ParticleSystem::ParticleSystem(uint32_t seed)
        : m_Seed(seed)
    {
    }

    ParticleEmitterHandle ParticleSystem::CreateEmitter(const ParticleEmitterDesc& desc)
    {
        if (!IsValidDesc(desc))
        {
            return {};
        }

        uint32_t index = 0u;
        if (!m_FreeEmitterIndices.empty())
        {
            index = m_FreeEmitterIndices.back();
            m_FreeEmitterIndices.pop_back();
        }
        else
        {
            index = static_cast<uint32_t>(m_Emitters.size());
            m_Emitters.emplace_back();
        }

        EmitterSlot& slot = m_Emitters[index];
        slot.Desc = desc;
        slot.Particles.clear();
        slot.SpawnAccumulator = 0.0f;
        slot.RandomState = ComputeInitialRandomState(m_Seed, index, slot.Generation);
        slot.bActive = true;
        return {index, slot.Generation};
    }

    bool ParticleSystem::UpdateEmitter(ParticleEmitterHandle handle, const ParticleEmitterDesc& desc)
    {
        if (!IsLiveHandle(handle) || !IsValidDesc(desc))
        {
            return false;
        }

        EmitterSlot& slot = m_Emitters[handle.Index];
        slot.Desc = desc;
        RemoveExpiredParticles(slot);
        if (slot.Particles.size() > desc.MaxCount)
        {
            slot.Particles.resize(desc.MaxCount);
        }
        return true;
    }

    bool ParticleSystem::SetEmitterEnabled(ParticleEmitterHandle handle, bool bEnabled)
    {
        if (!IsLiveHandle(handle))
        {
            return false;
        }

        m_Emitters[handle.Index].Desc.bEnabled = bEnabled;
        return true;
    }

    bool ParticleSystem::DestroyEmitter(ParticleEmitterHandle handle)
    {
        if (!IsLiveHandle(handle))
        {
            return false;
        }

        ReleaseSlot(handle.Index);
        return true;
    }

    void ParticleSystem::Clear()
    {
        for (uint32_t index = 0u; index < m_Emitters.size(); ++index)
        {
            if (m_Emitters[index].bActive)
            {
                ReleaseSlot(index);
            }
        }
    }

    void ParticleSystem::Tick(float deltaTime)
    {
        if (!std::isfinite(deltaTime) || deltaTime < 0.0f)
        {
            return;
        }

        for (EmitterSlot& slot : m_Emitters)
        {
            if (!slot.bActive)
            {
                continue;
            }

            for (ParticleInstance& particle : slot.Particles)
            {
                particle.Velocity += slot.Desc.Gravity * deltaTime;
                particle.Position += particle.Velocity * deltaTime;
                particle.Age += deltaTime;
            }
            RemoveExpiredParticles(slot);

            if (!slot.Desc.bEnabled)
            {
                continue;
            }

            slot.SpawnAccumulator += slot.Desc.SpawnRate * deltaTime;
            const float wholeSpawnCount = std::floor(slot.SpawnAccumulator);
            if (wholeSpawnCount <= 0.0f)
            {
                continue;
            }

            slot.SpawnAccumulator -= wholeSpawnCount;
            const uint32_t availableCount = slot.Desc.MaxCount - static_cast<uint32_t>(slot.Particles.size());
            const uint32_t spawnCount = wholeSpawnCount >= static_cast<float>(availableCount)
                                            ? availableCount
                                            : static_cast<uint32_t>(wholeSpawnCount);
            for (uint32_t particleIndex = 0u; particleIndex < spawnCount; ++particleIndex)
            {
                ParticleInstance particle;
                particle.Position = slot.Desc.Position;
                particle.Velocity.x = slot.Desc.VelocityMin.x +
                                      (slot.Desc.VelocityMax.x - slot.Desc.VelocityMin.x) * NextUnitFloat(slot.RandomState);
                particle.Velocity.y = slot.Desc.VelocityMin.y +
                                      (slot.Desc.VelocityMax.y - slot.Desc.VelocityMin.y) * NextUnitFloat(slot.RandomState);
                particle.Velocity.z = slot.Desc.VelocityMin.z +
                                      (slot.Desc.VelocityMax.z - slot.Desc.VelocityMin.z) * NextUnitFloat(slot.RandomState);
                slot.Particles.push_back(particle);
            }
        }
    }

    void ParticleSystem::AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const
    {
        for (uint32_t emitterIndex = 0u; emitterIndex < m_Emitters.size(); ++emitterIndex)
        {
            const EmitterSlot& slot = m_Emitters[emitterIndex];
            if (!slot.bActive)
            {
                continue;
            }

            const uint64_t emitterId = MakeSyntheticEmitterId(emitterIndex, slot.Generation);
            for (uint32_t particleIndex = 0u; particleIndex < slot.Particles.size(); ++particleIndex)
            {
                const ParticleInstance& particle = slot.Particles[particleIndex];
                Rendering::BoardProxy proxy;
                proxy.ObjectId = emitterId;
                proxy.ComponentId = emitterId;
                proxy.SortKey = slot.Desc.SortKey;
                proxy.LayerPriority = static_cast<uint32_t>(slot.Desc.SortKey >> 32u);
                proxy.OrderInLayer = static_cast<uint32_t>(slot.Desc.SortKey);
                proxy.Texture = slot.Desc.Texture;
                proxy.WorldTransform.SetTranslationRow(particle.Position);
                proxy.PreviousWorldTransform = proxy.WorldTransform;
                proxy.LayerMask = slot.Desc.LayerMask;
                proxy.Space = Rendering::BoardSpace::ScreenSpace;
                proxy.BlendModeProp = slot.Desc.BlendMode;
                proxy.Tint = slot.Desc.Color;
                proxy.SizePx = slot.Desc.SizePx;
                proxy.bVisible = true;
                outProxies.push_back(proxy);
            }
        }
    }

    bool ParticleSystem::IsValidDesc(const ParticleEmitterDesc& desc)
    {
        return IsFinite(desc.Position) && IsFinite(desc.VelocityMin) && IsFinite(desc.VelocityMax) &&
               IsFinite(desc.Gravity) && IsFinite(desc.Color) && IsFinite(desc.SizePx) &&
               std::isfinite(desc.SpawnRate) && std::isfinite(desc.Lifetime) &&
               desc.VelocityMin.x <= desc.VelocityMax.x &&
               desc.VelocityMin.y <= desc.VelocityMax.y &&
               desc.VelocityMin.z <= desc.VelocityMax.z &&
               desc.SpawnRate >= 0.0f && desc.Lifetime > 0.0f && desc.MaxCount > 0u &&
               desc.SizePx.x > 0.0f && desc.SizePx.y > 0.0f;
    }

    uint32_t ParticleSystem::ComputeInitialRandomState(uint32_t seed, uint32_t index, uint32_t generation)
    {
        uint32_t state = seed ^ ((index + 1u) * 0x9E3779B9u) ^ (generation * 0x85EBCA6Bu);
        if (state == 0u)
        {
            state = 0xA341316Cu;
        }
        return state;
    }

    uint32_t ParticleSystem::NextRandom(uint32_t& state)
    {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        return state;
    }

    float ParticleSystem::NextUnitFloat(uint32_t& state)
    {
        return static_cast<float>(NextRandom(state) >> 8u) / 16777216.0f;
    }

    // BoardProxy needs Object/Component IDs although particles are value-owned.
    // Bit 63 is the synthetic tag; bits 32-62 carry the low 31 generation bits.
    // Low 32 bits carry index + 1 so slot 0 never yields a zero low word.
    uint64_t ParticleSystem::MakeSyntheticEmitterId(uint32_t index, uint32_t generation)
    {
        return (uint64_t{1} << 63u) |
               (static_cast<uint64_t>(generation) << 32u) |
               static_cast<uint64_t>(index + 1u);
    }

    bool ParticleSystem::IsLiveHandle(ParticleEmitterHandle handle) const
    {
        return handle.IsValid() && handle.Index < m_Emitters.size() &&
               m_Emitters[handle.Index].bActive &&
               m_Emitters[handle.Index].Generation == handle.Generation;
    }

    void ParticleSystem::RemoveExpiredParticles(EmitterSlot& slot)
    {
        uint32_t writeIndex = 0u;
        for (uint32_t readIndex = 0u; readIndex < slot.Particles.size(); ++readIndex)
        {
            if (slot.Particles[readIndex].Age < slot.Desc.Lifetime)
            {
                if (writeIndex != readIndex)
                {
                    slot.Particles[writeIndex] = slot.Particles[readIndex];
                }
                ++writeIndex;
            }
        }
        slot.Particles.resize(writeIndex);
    }

    void ParticleSystem::ReleaseSlot(uint32_t index)
    {
        EmitterSlot& slot = m_Emitters[index];
        slot.Particles.clear();
        slot.SpawnAccumulator = 0.0f;
        slot.RandomState = 0u;
        slot.bActive = false;
        if (slot.Generation == std::numeric_limits<uint32_t>::max())
        {
            slot.bRetired = true;
            return;
        }

        ++slot.Generation;
        m_FreeEmitterIndices.push_back(index);
    }
} // namespace NorvesLib::Core::Particle
