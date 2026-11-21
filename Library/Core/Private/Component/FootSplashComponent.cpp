// 足の接地イベントから地面の状態を調べ、計画器で決めた GPU 粒子の単発放出を積む。
#include "Component/FootSplashComponent.h"

#include "Animation/AnimationEvents.h"
#include "Component/AnimatorComponent.h"
#include "Component/GroundContactComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/TimeChannels.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"
#include "Particle/FootSplashPlanner.h"
#include "Particle/ParticleSystem.h"
#include "Particle/VfxEffectSerialization.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Scene/SceneQuery.h"

#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(FootSplashComponent, Component)

    FootSplashComponent::FootSplashComponent()
        : Component()
    {
        SetDefaults();
    }

    FootSplashComponent::FootSplashComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    FootSplashComponent::FootSplashComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    FootSplashComponent::~FootSplashComponent()
    {
        UnbindEvents();
        ReleaseInstance();
    }

    void FootSplashComponent::SetDefaults()
    {
        getEffectPath() = "Assets/Vfx/FootSplash.vfx.json";
        getSeed() = Particle::VfxInstanceDesc{}.Seed;
        getEnabled() = true;
        const Particle::FootSplashTuning tuning;
        getBaseCount() = tuning.BaseCount;
        getMaxCount() = tuning.MaxCount;
        getReferenceSpeed() = tuning.ReferenceSpeed;
        getMinSpeedScale() = tuning.MinSpeedScale;
        getMaxSpeedScale() = tuning.MaxSpeedScale;
        getWetnessExponent() = tuning.WetnessExponent;
        getPuddleWeight() = tuning.PuddleWeight;
        getCountJitter() = tuning.CountJitter;
        getFootVelocityScale() = tuning.FootVelocityScale;
        getNormalSpeed() = tuning.NormalSpeed;
        getEmitterIndex() = tuning.EmitterIndex;
        getGroundProbeUp() = 0.5f;
        getGroundProbeDown() = 1.0f;
        getMaxFootHeight() = 0.15f;
        const FootSplashLeg legs[] = {
            {Identity("LF"), Identity("Foot.LF.Down"), Identity("Toe.LF"), Identity("front_L_paw"), {}},
            {Identity("RF"), Identity("Foot.RF.Down"), Identity("Toe.RF"), Identity("front_R_paw"), {}},
            {Identity("LH"), Identity("Foot.LH.Down"), Identity("Toe.LH"), Identity("hind_L_paw"), {}},
            {Identity("RH"), Identity("Foot.RH.Down"), Identity("Toe.RH"), Identity("hind_R_paw"), {}}};
        SetLegs(legs);
        // 姿勢の確定とイベントの配送が終わった群で足先を読む。
        SetTickGroup(ETickGroup::PostPhysics);
        SetTickEnabled(false);
    }

    bool FootSplashComponent::SetLegs(Container::Span<const FootSplashLeg> legs)
    {
        if (legs.size() > m_Legs.size())
        {
            return false;
        }
        for (size_t i = 0; i < legs.size(); ++i)
        {
            m_Legs[i] = legs[i];
        }
        m_LegCount = legs.size();
        for (auto& state : m_LegStates)
        {
            state = {};
        }
        return true;
    }

    void FootSplashComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();
        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (world && !world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "FootSplash.Emit";
            tick.Run = [](Component& component, float dt)
            { static_cast<FootSplashComponent&>(component).TickEmit(dt); };
            tick.TimeChannel = Engine::TimeChannel::Particle;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        BindEvents();
        RecreateInstance();
    }

    void FootSplashComponent::EndPlay()
    {
        UnbindEvents();
        ReleaseInstance();
        for (auto& state : m_LegStates)
        {
            state = {};
        }
        Component::EndPlay();
    }

    void FootSplashComponent::UnbindEvents()
    {
        // 保存したポインタを使わず、まだ所有者にいる同じ id の Animator だけから外す。
        auto* owner = GetOwner();
        if (owner && m_BoundAnimator)
        {
            for (auto* inner : owner->GetInners())
            {
                auto* animator = CastTo<AnimatorComponent>(inner);
                if (animator && animator->GetComponentId() == m_BoundAnimator)
                {
                    animator->OnEvent.Remove(this, &FootSplashComponent::OnAnimationEvent);
                }
            }
        }
        m_BoundAnimator = 0;
    }

    void FootSplashComponent::BindEvents()
    {
        auto* owner = GetOwner();
        auto* animator = owner ? owner->GetComponent<AnimatorComponent>() : nullptr;
        if (!animator || animator->IsPendingDestroy() || !animator->IsActive())
        {
            UnbindEvents();
            return;
        }
        if (m_BoundAnimator == animator->GetComponentId())
        {
            return;
        }
        UnbindEvents();
        animator->OnEvent.Add(this, &FootSplashComponent::OnAnimationEvent);
        m_BoundAnimator = animator->GetComponentId();
    }

    void FootSplashComponent::OnAnimationEvent(const Animation::AnimEventInfo& event)
    {
        if (IsPendingDestroy() || event.Kind == Animation::AnimEventKind::End)
        {
            return;
        }
        for (size_t i = 0; i < m_LegCount; ++i)
        {
            if (event.Name == m_Legs[i].DownEvent)
            {
                m_LegStates[i].bPending = true;
                m_LegStates[i].Sequence = event.Sequence;
            }
        }
    }

    void FootSplashComponent::TickEmit(float dt)
    {
        BindEvents();
        auto* owner = GetOwner();
        auto* mesh = owner ? owner->GetComponent<SkinnedMeshComponent>() : nullptr;
        const auto* groundContact = owner ? owner->GetComponent<GroundContactComponent>() : nullptr;
        if (groundContact && (!groundContact->IsActive() || groundContact->IsPendingDestroy()))
        {
            groundContact = nullptr;
        }
        auto* engine = Engine::GEngine;
        auto* system = GetLiveSystem();
        if (system && owner)
        {
            system->SetInstancePosition(m_EffectHandle, owner->GetRenderWorldTransform().position);
            system->SetInstanceEnabled(m_EffectHandle, IsEnabled());
        }

        Particle::FootSplashTuning tuning;
        tuning.BaseCount = BaseCount;
        tuning.MaxCount = MaxCount;
        tuning.ReferenceSpeed = ReferenceSpeed;
        tuning.MinSpeedScale = MinSpeedScale;
        tuning.MaxSpeedScale = MaxSpeedScale;
        tuning.WetnessExponent = WetnessExponent;
        tuning.PuddleWeight = PuddleWeight;
        tuning.CountJitter = CountJitter;
        tuning.FootVelocityScale = FootVelocityScale;
        tuning.NormalSpeed = NormalSpeed;
        tuning.EmitterIndex = EmitterIndex;

        for (size_t i = 0; i < m_LegCount; ++i)
        {
            auto& state = m_LegStates[i];
            const auto& leg = m_Legs[i];
            Math::Transform foot;
            bool bHaveFoot = false;
            if (mesh && !mesh->IsPendingDestroy() && mesh->IsActive())
            {
                bHaveFoot = mesh->GetSocketWorldTransform(leg.Socket, foot);
                if (!bHaveFoot)
                {
                    const int32_t joint = mesh->FindJointIndex(leg.FallbackJoint);
                    if (joint >= 0 && mesh->TryGetJointWorldTransform(static_cast<uint32_t>(joint), foot))
                    {
                        foot.position += foot.rotation * leg.FallbackOffset;
                        bHaveFoot = true;
                    }
                }
            }
            if (!bHaveFoot)
            {
                state = {};
                continue;
            }

            Math::Vector3 velocity = Math::Vector3::Zero;
            if (state.bHasPreviousPosition && dt > 0.0f && std::isfinite(dt))
            {
                velocity = (foot.position - state.PreviousPosition) / dt;
            }
            state.PreviousPosition = foot.position;
            state.bHasPreviousPosition = true;
            const bool bPending = state.bPending;
            state.bPending = false;
            if (!bPending || !IsEnabled() || !engine || !system)
            {
                continue;
            }

            auto& terrainSystem = engine->GetTerrainSystem();
            Scene::GroundQueryHit hit;
            // 今の公開番号に対応する足の結果は、接地・未接地ともに採用する。
            // 公開番号が取れない、接地状態が古い、または対応する足がない場合だけ従来の照会を使う。
            bool bUseSharedContact = false;
            if (groundContact)
            {
                uint64_t sequence = 0;
                const auto& contacts = groundContact->GetGroundContactState();
                const int32_t index = groundContact->FindFootIndex(leg.FallbackJoint);
                if (engine->GetSceneQuery().GetPublishedSnapshotSequence(sequence) == Scene::EPhysicsSceneQueryResult::Success &&
                    sequence != 0 && contacts.StepSerial == sequence && index >= 0 &&
                    static_cast<uint32_t>(index) < contacts.FootCount)
                {
                    bUseSharedContact = true;
                    const auto& footContact = contacts.Foot[index];
                    if (!footContact.bHit)
                    {
                        continue;
                    }
                    hit.bHit = true;
                    hit.Point = footContact.Point;
                    hit.Normal = footContact.Normal;
                }
            }
            if (!bUseSharedContact)
            {
                const auto result = engine->GetSceneQuery().QueryGround(
                    foot.position + Math::Vector3(0.0f, GroundProbeUp, 0.0f), GroundProbeUp + GroundProbeDown, hit);
                if (result != Scene::EPhysicsSceneQueryResult::Success || !hit.bHit)
                {
                    // 物理の地面が取れない場合は、照会の始点以下の地形の高さ場を使う。
                    Rendering::Terrain::GroundSample sample;
                    if (!terrainSystem.SampleGround(foot.position.x, foot.position.z, sample) || !sample.bValid ||
                        sample.Height - foot.position.y > GroundProbeUp)
                    {
                        continue;
                    }
                    hit.bHit = true;
                    hit.Point = Math::Vector3(foot.position.x, sample.Height, foot.position.z);
                    hit.Normal = sample.Normal;
                }
            }
            if (!(foot.position.y - hit.Point.y <= MaxFootHeight))
            {
                continue;
            }
            auto& environment = engine->GetEnvironment();
            Particle::FootSplashContact contact;
            contact.LegIndex = static_cast<uint32_t>(i);
            contact.Sequence = state.Sequence;
            contact.GroundPoint = hit.Point;
            contact.GroundNormal = hit.Normal;
            contact.FootVelocity = velocity;
            contact.Wetness = environment.IsEnabled() ? environment.GetState().Wetness : terrainSystem.GetWetness();
            if (auto terrain = terrainSystem.FindTerrain(hit.Point.x, hit.Point.z))
            {
                contact.PuddleCavity = terrain->SamplePuddlePotential(hit.Point.x, hit.Point.z);
            }
            const auto burst = Particle::PlanFootSplash(tuning, contact, GetSeed());
            if (burst.Count > 0)
            {
                system->RaiseGpuBurst(m_EffectHandle, burst.EmitterIndex, burst.Position, burst.Normal,
                                      burst.Velocity, burst.Count);
            }
        }
    }

    void FootSplashComponent::RecreateInstance()
    {
        ReleaseInstance();
        // 効果が読めなくても足の追跡と遅れて付く Animator の購読を続ける。
        SetTickEnabled(bBegunPlay);
        auto* engine = Engine::GEngine;
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        if (!engine || !owner || !world)
        {
            LOG_WARNING("FOOT_SPLASH_COMPONENT_SKIPPED status=MissingEngineOrOwnerOrWorld エンジン・所有者・World のいずれかがありません");
            return;
        }
        if (GetEffectPath().empty())
        {
            LOG_WARNING("FOOT_SPLASH_COMPONENT_SKIPPED status=EmptyPath 効果のパスがありません");
            return;
        }
        Particle::VfxEffectAsset asset;
        Container::String error;
        if (!Particle::LoadVfxEffectFile(GetEffectPath(), asset, error))
        {
            LOG_WARNING("FOOT_SPLASH_COMPONENT_SKIPPED status=LoadFailed path=%s reason=%s 効果を読み込めません",
                        GetEffectPath().c_str(), error.c_str());
            return;
        }
        Particle::VfxInstanceDesc desc;
        desc.Position = owner->GetRenderWorldTransform().position;
        desc.Seed = GetSeed();
        desc.bEnabled = IsEnabled();
        auto& system = engine->GetParticleSystem().GetVfxEffects();
        const auto handle = system.CreateInstance(asset, desc, &error);
        if (!system.IsLiveInstance(handle))
        {
            LOG_WARNING("FOOT_SPLASH_COMPONENT_SKIPPED status=CreateFailed path=%s reason=%s 効果のインスタンスを作れません",
                        GetEffectPath().c_str(), error.c_str());
            return;
        }
        m_RegisteredEngine = engine;
        m_RegisteredSystem = &system;
        m_EffectHandle = handle;
    }

    Particle::VfxEffectSystem* FootSplashComponent::GetLiveSystem() const
    {
        if (!m_RegisteredEngine || Engine::GEngine != m_RegisteredEngine || !m_RegisteredSystem)
        {
            return nullptr;
        }
        return m_RegisteredSystem->IsLiveInstance(m_EffectHandle) ? m_RegisteredSystem : nullptr;
    }

    Particle::VfxEffectHandle FootSplashComponent::GetEffectHandle() const
    {
        return GetLiveSystem() ? m_EffectHandle : Particle::VfxEffectHandle{};
    }

    void FootSplashComponent::ReleaseInstance()
    {
        if (auto* system = GetLiveSystem())
        {
            system->DestroyInstance(m_EffectHandle);
        }
        m_EffectHandle = {};
        m_RegisteredSystem = nullptr;
        m_RegisteredEngine = nullptr;
        SetTickEnabled(false);
    }

    void FootSplashComponent::SetEffectPath(const Container::String& path)
    {
        if (GetEffectPath() == path)
        {
            return;
        }
        getEffectPath() = path;
        if (bBegunPlay)
        {
            RecreateInstance();
        }
    }

    void FootSplashComponent::SetSeed(uint32_t seed)
    {
        if (GetSeed() == seed)
        {
            return;
        }
        getSeed() = seed;
        if (bBegunPlay)
        {
            RecreateInstance();
        }
    }

    void FootSplashComponent::SetEnabled(bool bEnabled)
    {
        getEnabled() = bEnabled;
        if (!bEnabled)
        {
            for (auto& state : m_LegStates)
            {
                state.bPending = false;
            }
        }
        if (auto* system = GetLiveSystem())
        {
            system->SetInstanceEnabled(m_EffectHandle, bEnabled);
        }
    }
} // コンポーネントの名前空間
