// 足の接地イベントから地面の状態を調べ、計画器で決めた GPU 粒子の単発放出を積む。
#pragma once

#include "Component.h"
#include "Container/FixedArray.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Particle/VfxEffectSystem.h"

namespace NorvesLib::Core::Animation
{
    struct AnimEventInfo;
}

namespace NorvesLib::Core::Engine
{
    class Engine;
}

namespace NorvesLib::Core::Component
{
    /// 接地イベントと足先のソケット・代替関節の対応。
    struct FootSplashLeg
    {
        Identity Name;
        Identity DownEvent;
        Identity Socket;
        Identity FallbackJoint;
        Math::Vector3 FallbackOffset = Math::Vector3::Zero;
    };

    class FootSplashComponent : public Component
    {
        REFLECTION_CLASS(FootSplashComponent, Component)

    public:
        FootSplashComponent();
        explicit FootSplashComponent(const FieldInitializer* initializer);
        explicit FootSplashComponent(const IUnknown* sourceObject);
        ~FootSplashComponent() override;

        void BeginPlay() override;
        void EndPlay() override;

        void SetEffectPath(const Container::String& path);
        const Container::String& GetEffectPath() const { return EffectPath; }
        void SetSeed(uint32_t seed);
        uint32_t GetSeed() const { return Seed; }
        void SetEnabled(bool bEnabled);
        bool IsEnabled() const { return Enabled; }
        Particle::VfxEffectHandle GetEffectHandle() const;

        /// 最大 8 本。上限を超えたら false を返して現在の定義を保つ。成功時は追跡を初期化する。
        bool SetLegs(Container::Span<const FootSplashLeg> legs);
        Container::Span<const FootSplashLeg> GetLegs() const { return {m_Legs.data(), m_LegCount}; }

        PROPERTY(Container::String, EffectPath)
        PROPERTY(uint32_t, Seed)
        PROPERTY(bool, Enabled)
        PROPERTY(uint32_t, BaseCount)
        PROPERTY(uint32_t, MaxCount)
        PROPERTY(float, ReferenceSpeed)
        PROPERTY(float, MinSpeedScale)
        PROPERTY(float, MaxSpeedScale)
        PROPERTY(float, WetnessExponent)
        PROPERTY(float, PuddleWeight)
        PROPERTY(float, CountJitter)
        PROPERTY(float, FootVelocityScale)
        PROPERTY(float, NormalSpeed)
        PROPERTY(uint32_t, EmitterIndex)
        PROPERTY(float, GroundProbeUp)
        PROPERTY(float, GroundProbeDown)
        PROPERTY(float, MaxFootHeight)

    private:
        struct LegState
        {
            Math::Vector3 PreviousPosition = Math::Vector3::Zero;
            uint64_t Sequence = 0;
            bool bHasPreviousPosition = false;
            bool bPending = false;
        };

        void SetDefaults();
        void BindEvents();
        void UnbindEvents();
        void OnAnimationEvent(const Animation::AnimEventInfo& event);
        void TickEmit(float dt);
        void RecreateInstance();
        void ReleaseInstance();
        Particle::VfxEffectSystem* GetLiveSystem() const;

        Container::FixedArray<FootSplashLeg, 8> m_Legs;
        Container::FixedArray<LegState, 8> m_LegStates;
        size_t m_LegCount = 0;
        uint64_t m_BoundAnimator = 0;
        Engine::Engine* m_RegisteredEngine = nullptr;
        Particle::VfxEffectSystem* m_RegisteredSystem = nullptr;
        Particle::VfxEffectHandle m_EffectHandle;
    };
} // コンポーネントの名前空間
