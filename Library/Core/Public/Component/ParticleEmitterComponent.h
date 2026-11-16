// Entity に付けて効果のインスタンスを 1 つ持ち、所有者の描画の位置へ追従させる Component
#pragma once

#include "Component.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Component
{
    class ParticleEmitterComponent : public Component
    {
        REFLECTION_CLASS(ParticleEmitterComponent, Component)

    public:
        ParticleEmitterComponent();
        explicit ParticleEmitterComponent(const FieldInitializer* initializer);
        explicit ParticleEmitterComponent(const IUnknown* sourceObject);
        ~ParticleEmitterComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        void SetEffectPath(const Container::String& path);
        const Container::String& GetEffectPath() const { return EffectPath; }
        void SetSeed(uint32_t seed);
        uint32_t GetSeed() const { return Seed; }
        void SetEnabled(bool bEnabled);
        bool IsEnabled() const { return Enabled; }
        Particle::VfxEffectHandle GetEffectHandle() const;

        // 再生前は値を保持し、再生中は適用に成功した値だけを保持する。同じ鍵は置き換える。
        bool SetEmitterPropertyOverride(uint32_t emitterIndex, const char* propertyName, const Particle::VfxValue& value);
        bool SetModuleParameterOverride(uint32_t emitterIndex, uint32_t moduleIndex, const char* parameterName,
                                        const Particle::VfxValue& value);
        // 保持した値を消し、再生中なら資産の既定値で作り直す。
        void ClearOverrides();

        PROPERTY(Container::String, EffectPath)
        PROPERTY(uint32_t, Seed)
        PROPERTY(bool, Enabled)

    private:
        struct EffectOverride
        {
            uint32_t EmitterIndex = 0u;
            uint32_t ModuleIndex = 0u;
            Container::AnsiString Name;
            Particle::VfxValue Value;
            bool bModuleParameter = false;
        };

        void SetDefaults();
        void RecreateInstance();
        void ReleaseInstance();
        void FollowOwner();
        Particle::VfxEffectSystem* GetLiveSystem() const;
        bool StoreOverride(const EffectOverride& entry);
        bool ApplyOverride(const EffectOverride& entry, Container::String& error);

        Engine::Engine* m_RegisteredEngine = nullptr;
        Particle::VfxEffectSystem* m_RegisteredSystem = nullptr;
        Particle::VfxEffectHandle m_EffectHandle;
        Container::VariableArray<EffectOverride> m_Overrides;
    };
} // コンポーネントの名前空間
