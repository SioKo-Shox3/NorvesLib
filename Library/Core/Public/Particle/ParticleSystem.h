#pragma once

#include "CoreTypes.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/RenderTypes.h"

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    struct BoardProxy;
}

namespace NorvesLib::Core::Particle
{
    class ParticleSystemTestAccess;

    struct ParticleEmitterHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();
        uint32_t Generation = 0u;

        bool IsValid() const
        {
            return Index != std::numeric_limits<uint32_t>::max() && Generation != 0u;
        }

        bool operator==(const ParticleEmitterHandle& other) const = default;
    };

    struct ParticleEmitterDesc
    {
        Math::Vector3 Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        Math::Vector3 VelocityMin = Math::Vector3(0.0f, 0.0f, 0.0f);
        Math::Vector3 VelocityMax = Math::Vector3(0.0f, 0.0f, 0.0f);
        Math::Vector3 Gravity = Math::Vector3(0.0f, 0.0f, 0.0f);
        float SpawnRate = 0.0f;
        float Lifetime = 1.0f;
        uint32_t MaxCount = 128u;
        Math::Vector4 Color = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::Vector2 SizePx = Math::Vector2(8.0f, 8.0f);
        Rendering::TextureHandle Texture = Rendering::TextureHandle::Invalid();
        uint64_t SortKey = 0u;
        Rendering::RenderLayer LayerMask = Rendering::RenderLayer::UI;
        Rendering::BlendMode BlendMode = Rendering::BlendMode::Translucent;
        bool bEnabled = true;
    };

    class ParticleSystem
    {
    public:
        explicit ParticleSystem(uint32_t seed = 0xC001D00Du);

        ParticleEmitterHandle CreateEmitter(const ParticleEmitterDesc& desc);
        bool UpdateEmitter(ParticleEmitterHandle handle, const ParticleEmitterDesc& desc);
        bool SetEmitterEnabled(ParticleEmitterHandle handle, bool bEnabled);
        bool DestroyEmitter(ParticleEmitterHandle handle);
        void Clear();
        void Tick(float deltaTime);
        void AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const;

    private:
        struct ParticleInstance
        {
            Math::Vector3 Position;
            Math::Vector3 Velocity;
            float Age = 0.0f;
        };

        struct EmitterSlot
        {
            ParticleEmitterDesc Desc;
            VariableArray<ParticleInstance> Particles;
            float SpawnAccumulator = 0.0f;
            uint32_t Generation = 1u;
            uint32_t RandomState = 0u;
            bool bActive = false;
            bool bRetired = false;
        };

        static bool IsValidDesc(const ParticleEmitterDesc& desc);
        static uint32_t ComputeInitialRandomState(uint32_t seed, uint32_t index, uint32_t generation);
        static uint32_t NextRandom(uint32_t& state);
        static float NextUnitFloat(uint32_t& state);
        static uint64_t MakeSyntheticEmitterId(uint32_t index, uint32_t generation);
        bool IsLiveHandle(ParticleEmitterHandle handle) const;
        void RemoveExpiredParticles(EmitterSlot& slot);
        void ReleaseSlot(uint32_t index);

        uint32_t m_Seed;
        VariableArray<EmitterSlot> m_Emitters;
        VariableArray<uint32_t> m_FreeEmitterIndices;

        friend class ParticleSystemTestAccess;
    };
} // namespace NorvesLib::Core::Particle
