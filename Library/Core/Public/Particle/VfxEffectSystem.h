#pragma once

#include "Particle/VfxEffectTypes.h"
#include "Rendering/RenderTypes.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    struct BoardProxy;
}

namespace NorvesLib::Core::Particle
{
    struct VfxEffectHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();
        uint32_t Generation = 0u;

        bool IsValid() const
        {
            return Index != std::numeric_limits<uint32_t>::max() && Generation != 0u;
        }

        bool operator==(const VfxEffectHandle& other) const = default;
    };

    struct VfxInstanceDesc
    {
        Math::Vector3 Position = Math::Vector3(0.0f, 0.0f, 0.0f);
        uint32_t Seed = 0xC001D00Du;
        Rendering::TextureHandle Texture = Rendering::TextureHandle::Invalid();
        bool bEnabled = true;
    };

    struct VfxParticleState
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
        Math::Vector4 Color;
        Math::Vector2 Size;
        Math::Vector4 UVRect = Math::Vector4(0.0f, 0.0f, 1.0f, 1.0f);
        float Age = 0.0f;
        float Lifetime = 1.0f;
        double BirthTime = 0.0;
        uint32_t Frame = 0u;
    };

    class VfxEffectSystem
    {
    public:
        VfxEffectSystem() = default;

        VfxEffectHandle CreateInstance(const VfxEffectAsset& asset, const VfxInstanceDesc& desc, Container::String* outError = nullptr);
        bool DestroyInstance(VfxEffectHandle handle);
        void Clear();
        bool IsLiveInstance(VfxEffectHandle handle) const;
        bool SetInstancePosition(VfxEffectHandle handle, const Math::Vector3& position);
        bool SetInstanceEnabled(VfxEffectHandle handle, bool bEnabled);
        bool SetModuleParameter(VfxEffectHandle handle, uint32_t emitterIndex, uint32_t moduleIndex, const char* parameterName, const VfxValue& value, Container::String* outError = nullptr);
        bool SetEmitterProperty(VfxEffectHandle handle, uint32_t emitterIndex, const char* propertyName, const VfxValue& value, Container::String* outError = nullptr);
        bool RaiseEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position, double timeSeconds);
        void Tick(float deltaTime);
        double GetInstanceTime(VfxEffectHandle handle) const;
        uint32_t GetEmitterCount(VfxEffectHandle handle) const;
        bool CopyParticles(VfxEffectHandle handle, uint32_t emitterIndex, VariableArray<VfxParticleState>& outParticles) const;
        void AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const;
        static uint32_t ComputeFlipbookFrame(const VfxFlipbookImportSettings& flipbook, float age, float lifetime);
        static Math::Vector4 ComputeFlipbookUVRect(const VfxFlipbookImportSettings& flipbook, uint32_t frame);
    };
} // namespace NorvesLib::Core::Particle
