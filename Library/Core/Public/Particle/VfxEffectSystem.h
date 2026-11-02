#pragma once

#include "Math/Matrix4x4.h"
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
        Rendering::MeshDataHandle Mesh = Rendering::MeshDataHandle::Invalid();
        Rendering::MaterialHandle Material = Rendering::MaterialHandle::Invalid();
    };

    struct VfxMeshInstance
    {
        uint64_t Id = 0u;
        Math::Matrix4x4 World;
        Math::Vector4 Color;
        float DissolveThreshold = 0.0f;
        float EdgeWidth = 0.0f;
        Math::Vector4 EdgeColor;
        Rendering::MeshDataHandle Mesh = Rendering::MeshDataHandle::Invalid();
        Rendering::MaterialHandle Material = Rendering::MaterialHandle::Invalid();
        uint32_t InstanceIndex = 0u;
        uint32_t EmitterIndex = 0u;
        uint32_t ParticleIndex = 0u;
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
        /**
         * @brief 法線をローカルの +Z とするイベントを予約する。
         * @return イベントを受け付けた場合は true、入力が無効な場合は false。
         */
        bool RaiseOrientedEvent(VfxEffectHandle handle, const char* eventName, const Math::Vector3& position,
            const Math::Vector3& normal, double timeSeconds);
        void Tick(float deltaTime);
        double GetInstanceTime(VfxEffectHandle handle) const;
        uint32_t GetEmitterCount(VfxEffectHandle handle) const;
        bool CopyParticles(VfxEffectHandle handle, uint32_t emitterIndex, VariableArray<VfxParticleState>& outParticles) const;
        void AppendBoardProxies(VariableArray<Rendering::BoardProxy>& outProxies) const;
        void AppendMeshInstances(VariableArray<VfxMeshInstance>& outInstances) const;
        static float ComputeDissolveThreshold(float age, float lifetime, float dissolveStart, float dissolveDuration);
        static uint32_t ComputeFlipbookFrame(const VfxFlipbookImportSettings& flipbook, float age, float lifetime);
        static Math::Vector4 ComputeFlipbookUVRect(const VfxFlipbookImportSettings& flipbook, uint32_t frame);

    private:
        struct ModuleBinding
        {
            uint32_t Arguments[6] = {};
        };

        struct EmitterState
        {
            VariableArray<VfxParticleState> Particles;
            VariableArray<ModuleBinding> Bindings;
            float SpawnAccumulator = 0.0f;
            uint32_t RandomState = 0u;
            uint32_t Seed = 0u;
            uint32_t MaxParticles = 0u;
            uint32_t Space = 0u;
            uint32_t SortKey = 0u;
            uint32_t Layer = 0u;
            uint32_t Blend = 0u;
            int32_t FlipbookIndex = -1;
            bool bHasRenderer = false;
            bool bHasMeshRenderer = false;
            bool bAlignToVelocity = false;
            bool bHasVelocity = false;
            Math::Vector3 Offset;
            Math::Vector3 VelocityMin;
            Math::Vector3 VelocityMax;
            Math::Vector3 Acceleration;
            Math::Vector4 Color;
            Math::Vector2 Size;
            float Lifetime = 1.0f;
            float DissolveStart = 0.5f;
            float DissolveDuration = 0.5f;
            float EdgeWidth = 0.05f;
            Math::Vector4 EdgeColor = Math::Vector4(4.0f, 1.5f, 0.3f, 1.0f);
        };

        struct PendingEvent
        {
            Container::String Name;
            Math::Vector3 Position;
            Math::Vector3 Normal;
            double Time = 0.0;
            bool bOriented = false;
        };

        struct InstanceSlot
        {
            VfxEffectAsset Asset;
            VfxInstanceDesc Desc;
            VariableArray<EmitterState> Emitters;
            VariableArray<PendingEvent> Events;
            uint32_t PropertyIndices[3] = {};
            double Time = 0.0;
            uint32_t Generation = 1u;
            bool bActive = false;
            bool bRetired = false;
        };

        static uint32_t InitialRandomState(uint32_t seed, uint32_t emitterIndex);
        static float NextUnitFloat(uint32_t& state);
        static void CacheBindings(InstanceSlot& slot);
        static void RefreshEmitter(InstanceSlot& slot, uint32_t emitterIndex);
        static void UpdateFlipbook(const InstanceSlot& slot, const EmitterState& emitter, VfxParticleState& particle);
        static void SpawnParticles(InstanceSlot& slot, EmitterState& emitter, uint32_t count,
            const Math::Vector3& position, double birthTime, float age, const Math::Vector3* orientationNormal = nullptr);
        void ReleaseSlot(uint32_t index);

        VariableArray<InstanceSlot> m_Instances;
        VariableArray<uint32_t> m_FreeInstanceIndices;
    };
} // namespace NorvesLib::Core::Particle
