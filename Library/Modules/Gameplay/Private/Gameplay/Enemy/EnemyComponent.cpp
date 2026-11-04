#include "Gameplay/Enemy/EnemyComponent.h"

#include "Component/AnimatorComponent.h"
#include "Ecs/EcsTypes.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/Enemy/AnimatorEnemyAnimator.h"
#include "Gameplay/GameplayModule.h"
#include "Logging/LogMacros.h"
#include "Object/World.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Modules::Gameplay::Enemy
{
    IMPLEMENT_CLASS(EnemyComponent, Core::Component::Component)

    EnemyComponent::EnemyComponent() = default;
    EnemyComponent::~EnemyComponent()
    {
        Unsubscribe();
    }

    void EnemyComponent::Initialize()
    {
        Super::Initialize();
        SetTickGroup(Core::Component::ETickGroup::Default);
        SetTickGroupMask(Core::Component::TickGroupBit(Core::Component::ETickGroup::Default));
    }

    bool EnemyComponent::SetDefinition(const EnemyDefinition& definition)
    {
        if (m_bPlaying || definition.Id.empty() || !std::isfinite(definition.MaxHealth) ||
            definition.MaxHealth <= 0.0f || !std::isfinite(definition.StaggerThreshold) ||
            definition.StaggerThreshold < 0.0f || !std::isfinite(definition.AlertDelaySeconds) ||
            definition.AlertDelaySeconds < 0.0f || !std::isfinite(definition.AttackIntervalSeconds) ||
            definition.AttackIntervalSeconds < 0.0f || !std::isfinite(definition.Collapse.RecoverSeconds) ||
            definition.Collapse.RecoverSeconds <= 0.0f)
        {
            return false;
        }
        Core::Container::VariableArray<Core::Container::TSharedPtr<const Combat::AttackProfile>> profiles;
        for (const auto& pattern : definition.Patterns)
        {
            const auto* profile = FindAttackProfile(definition, pattern.AttackProfileId);
            if (profile == nullptr || pattern.Phases.empty())
            {
                return false;
            }
            for (const auto& phase : pattern.Phases)
            {
                if (!std::isfinite(phase.Seconds) || phase.Seconds <= 0.0f)
                {
                    return false;
                }
            }
            profiles.push_back(MakeAttackProfile(*profile));
        }
        m_Definition = definition;
        m_PatternProfiles = std::move(profiles);
        m_bHasDefinition = true;
        ApplyHealthDefinition();
        return true;
    }

    void EnemyComponent::SetAnimator(IEnemyAnimator* animator)
    {
        if (animator != m_OwnedAnimator.get())
        {
            m_OwnedAnimator.reset();
        }
        m_Animator = animator;
    }

    const Core::Container::String& EnemyComponent::GetEnemyId() const
    {
        return EnemyId.Get().empty() ? m_Definition.Id : EnemyId.Get();
    }

    EEnemyState EnemyComponent::GetState() const
    {
        return m_Machine && m_Machine->HasCurrentState() ? m_Machine->GetCurrentState() : EEnemyState::Dormant;
    }

    void EnemyComponent::ApplyHealthDefinition()
    {
        if (m_bHasDefinition)
        {
            if (auto* health = FindHealth())
            {
                health->getMaxHealth() = m_Definition.MaxHealth;
                health->getStaggerThreshold() = m_Definition.StaggerThreshold;
                health->ResetHealth();
            }
        }
    }

    void EnemyComponent::BeginPlay()
    {
        if (m_bPlaying)
        {
            return;
        }
        // World 未参加の Entity は、参加時にもう一度 BeginPlay が呼ばれる。
        if (!GetOwner() || !GetOwner()->GetWorld())
        {
            return;
        }
        if (!m_bHasDefinition && !DefinitionPath.Get().empty())
        {
            EnemyDefinition definition;
            Core::Container::String error;
            if (!LoadEnemyDefinitionFromAsset(DefinitionPath.Get(), definition, error) || !SetDefinition(definition))
            {
                NORVES_LOG_WARNING("Gameplay", "敵定義を適用できませんでした: %s (%s)",
                                   DefinitionPath.Get().c_str(), error.c_str());
            }
        }
        Super::BeginPlay();
        m_bPlaying = true;
        m_bFirstAttack = true;
        m_bHealthBelowTriggered = false;
        m_bZoneEnterPending = false;
        m_bDefeatStarted = false;
        m_bDefeatGatePending = false;
        m_bDefeatCompletionPending = false;
        m_bDefeatCompleted = false;
        m_DefeatHold = {};
        m_PartHits = 0;
        m_CurrentPatternIndex = -1;
        m_CurrentPhaseIndex = 0;
        m_EncounterZoneId.clear();
        ApplyHealthDefinition();

        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        m_BoundWorld = world;
        m_BoundOwner = owner ? owner->GetEntityHandle() : Core::EntityHandle{};
        m_BoundModule = world ? GameplayModule::FindBoundModule(*world) : nullptr;
        if (m_bHasDefinition)
        {
            const uint64_t seed = m_BoundModule ? m_BoundModule->GetRunSeed() : 0;
            m_Selector.Reset(m_Definition, MakeEnemyRandomSeed(seed, GetEnemyId()));
            TryBindAnimator();
        }
        Subscribe();
        m_Machine = Core::Container::MakeUnique<StateMachine>();
        for (size_t index = 0; index < static_cast<size_t>(EEnemyState::Count); ++index)
        {
            m_Machine->RegisterState(static_cast<EEnemyState>(index), {&EnterCallback, &UpdateCallback, &ExitCallback});
        }
        m_LastRequested = EEnemyState::Dormant;
        m_Machine->Start(EEnemyState::Dormant);
    }

    void EnemyComponent::EndPlay()
    {
        Unsubscribe();
        if (m_Machine)
        {
            m_Machine->Shutdown(*this);
        }
        m_Animator = nullptr;
        m_OwnedAnimator.reset();
        m_bPlaying = false;
        Super::EndPlay();
    }

    void EnemyComponent::Finalize()
    {
        Unsubscribe();
        EndPlay();
        Super::Finalize();
    }

    Core::Entity* EnemyComponent::FindOwner() const
    {
        if (auto* owner = const_cast<EnemyComponent*>(this)->GetOwner())
        {
            return owner;
        }
        // Component が先に木から外れた場合だけ、同じ稼働中の World から所有者を引き直す。
        auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        if (m_BoundWorld && current && current == m_BoundModule &&
            GameplayModule::FindBoundModule(*m_BoundWorld) == current)
        {
            return m_BoundWorld->ResolveEntityHandle(m_BoundOwner);
        }
        return nullptr;
    }

    Combat::HealthComponent* EnemyComponent::FindHealth() const
    {
        auto* owner = FindOwner();
        auto* health = owner ? owner->GetComponent<Combat::HealthComponent>() : nullptr;
        return health && !health->IsPendingDestroy() ? health : nullptr;
    }

    Combat::HitboxComponent* EnemyComponent::FindHitbox() const
    {
        auto* owner = FindOwner();
        auto* hitbox = owner ? owner->GetComponent<Combat::HitboxComponent>() : nullptr;
        return hitbox;
    }

    void EnemyComponent::TryBindAnimator()
    {
        auto* owner = GetOwner();
        if (!m_bHasDefinition || m_Animator || !owner) return;
        if (auto* animator = owner->GetComponent<Core::Component::AnimatorComponent>())
        {
            auto adapter = Core::Container::MakeUnique<AnimatorEnemyAnimator>();
            if (adapter->Bind(animator, m_Definition.AnimMachine))
            {
                m_Animator = adapter.get();
                m_OwnedAnimator = std::move(adapter);
            }
        }
    }

    void EnemyComponent::Subscribe()
    {
        auto* owner = GetOwner();
        m_Subscription = Core::Container::MakeShared<Subscription>();
        m_Subscription->Target = this;
        const auto subscription = m_Subscription;
        m_DamagedCallback = [subscription](const Combat::HealthComponent& health, const Combat::DamageResult& result)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnDamaged(health, result);
            --subscription->DispatchDepth;
        };
        m_StaggerCallback = [subscription](const Combat::HealthComponent& health)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnStaggerBroken(health);
            --subscription->DispatchDepth;
        };
        m_DefeatedCallback = [subscription](const Combat::HealthComponent& health)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnDefeated(health);
            --subscription->DispatchDepth;
        };
        m_HurtCallback = [subscription](const Combat::CombatHitEvent& event)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnHurt(event);
            --subscription->DispatchDepth;
        };
        m_ZoneCallback = [subscription](const World::EncounterZoneEvent& event)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnZoneEntered(event);
            --subscription->DispatchDepth;
        };
        m_ZoneResetCallback = [subscription](const World::EncounterZoneEvent& event)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnZoneReset(event);
            --subscription->DispatchDepth;
        };
        m_DefeatCompletedCallback = [subscription](const Core::Container::String& enemyId)
        {
            ++subscription->DispatchDepth;
            if (subscription->Target) subscription->Target->OnGateDefeatCompleted(enemyId);
            --subscription->DispatchDepth;
        };
        if (auto* health = owner ? owner->GetComponent<Combat::HealthComponent>() : nullptr)
        {
            m_SubscribedHealth = health;
            m_HealthId = health->GetComponentId();
            health->OnDamaged.Add(m_DamagedCallback);
            health->OnStaggerBroken.Add(m_StaggerCallback);
            health->OnDefeated.Add(m_DefeatedCallback);
        }
        if (auto* hurtbox = owner ? owner->GetComponent<Combat::HurtboxSetComponent>() : nullptr)
        {
            m_SubscribedHurtbox = hurtbox;
            m_HurtboxId = hurtbox->GetComponentId();
            hurtbox->OnHurt.Add(m_HurtCallback);
        }
        if (m_BoundModule)
        {
            m_BoundModule->GetEncounterService().OnZoneEntered.Add(m_ZoneCallback);
            m_BoundModule->GetEncounterService().OnZoneReset.Add(m_ZoneResetCallback);
            m_BoundModule->GetDefeatGate().OnDefeatCompleted.Add(m_DefeatCompletedCallback);
        }
    }

    void EnemyComponent::Unsubscribe()
    {
        const bool bCanRemoveSubscriptions = !m_Subscription || m_Subscription->DispatchDepth == 0;
        if (m_Subscription)
        {
            m_Subscription->Target = nullptr;
        }
        // 配送中の配列は変更せず、残った受け手は Target が空のまま無効にする。
        if (bCanRemoveSubscriptions)
        {
            if (auto* owner = FindOwner())
            {
                for (auto* component : owner->GetComponents())
                {
                    if (component == m_SubscribedHealth && component->GetComponentId() == m_HealthId)
                    {
                        m_SubscribedHealth->OnDamaged.Remove(m_DamagedCallback);
                        m_SubscribedHealth->OnStaggerBroken.Remove(m_StaggerCallback);
                        m_SubscribedHealth->OnDefeated.Remove(m_DefeatedCallback);
                    }
                    if (component == m_SubscribedHurtbox && component->GetComponentId() == m_HurtboxId)
                    {
                        m_SubscribedHurtbox->OnHurt.Remove(m_HurtCallback);
                    }
                }
            }
        }
        auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        if (current && current == m_BoundModule)
        {
            if (bCanRemoveSubscriptions)
            {
                current->GetEncounterService().OnZoneEntered.Remove(m_ZoneCallback);
                current->GetEncounterService().OnZoneReset.Remove(m_ZoneResetCallback);
                current->GetDefeatGate().OnDefeatCompleted.Remove(m_DefeatCompletedCallback);
            }
            if (m_DefeatHold.IsValid()) current->GetDefeatGate().Release(m_DefeatHold);
        }
        m_bDefeatGatePending = false;
        m_DefeatHold = {};
        m_SubscribedHealth = nullptr;
        m_SubscribedHurtbox = nullptr;
        m_HealthId = 0;
        m_HurtboxId = 0;
        m_Subscription.reset();
        m_DamagedCallback.Clear();
        m_StaggerCallback.Clear();
        m_DefeatedCallback.Clear();
        m_HurtCallback.Clear();
        m_ZoneCallback.Clear();
        m_ZoneResetCallback.Clear();
        m_DefeatCompletedCallback.Clear();
    }

    Core::Component::ComponentTickRegistration EnemyComponent::MakeTickRegistration()
    {
        using namespace Core::Component;
        ComponentTickRegistration registration;
        registration.Name = "Enemy.Tick";
        registration.Run = [](Component& component, float dt) { static_cast<EnemyComponent&>(component).TickEnemy(dt); };
        registration.bPrimaryGroupOnly = true;
        registration.Groups = TickGroupBit(ETickGroup::Default);
        registration.TimeChannel = Core::Engine::TimeChannel::World;
        registration.Reads.push_back(Core::Ecs::EcsColumnInfoOf<Core::Ecs::EcsWorldTransform>().Type);
        return registration;
    }

    void EnemyComponent::RegisterTick(Core::World& world)
    {
        world.RegisterComponentTick(StaticClass(), MakeTickRegistration());
    }

    void EnemyComponent::TickEnemy(float worldDeltaSeconds)
    {
        if (!m_bPlaying || !m_Machine)
        {
            return;
        }
        // 領域の Cleared は次の評価で確定する。ゲートの配送も終えてから完了を配る。
        if (m_bDefeatCompletionPending)
        {
            auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
            if (current && current == m_BoundModule &&
                (m_EncounterZoneId.empty() || current->GetEncounterService().GetZoneState(m_EncounterZoneId) ==
                    World::EEncounterZoneState::Cleared))
            {
                CompleteDefeat();
                return;
            }
        }
        if (auto* owner = GetOwner(); owner && owner->GetWorld() == m_BoundWorld && !m_BoundOwner.IsValid())
        {
            // 更新中に作られた Entity の行は、作成時にはまだ予約状態のことがある。
            m_BoundOwner = owner->GetEntityHandle();
        }
        if (m_bDefeatGatePending && m_BoundOwner.IsValid())
        {
            auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
            if (current && current == m_BoundModule)
            {
                BeginDefeatGate(*current, m_BoundOwner);
                m_bDefeatGatePending = false;
            }
        }
        if (!m_Animator && m_BoundOwner.IsValid()) TryBindAnimator();
        const float dt = std::isfinite(worldDeltaSeconds) && worldDeltaSeconds >= 0.0f ? worldDeltaSeconds : 0.0f;
        m_Selector.Advance(dt);
        m_Machine->Update(*this, dt);
    }

    bool EnemyComponent::IsTransitionAllowed(EEnemyState from, EEnemyState to)
    {
        switch (from)
        {
        case EEnemyState::Dormant: return to == EEnemyState::Idle || to == EEnemyState::Defeated;
        case EEnemyState::Idle: return to == EEnemyState::Alerted || to == EEnemyState::Defeated;
        case EEnemyState::Alerted: return to == EEnemyState::Attack || to == EEnemyState::Collapsed ||
                                         to == EEnemyState::Idle || to == EEnemyState::Defeated;
        case EEnemyState::Attack: return to == EEnemyState::Alerted || to == EEnemyState::Collapsed ||
                                        to == EEnemyState::Idle || to == EEnemyState::Defeated;
        case EEnemyState::Collapsed: return to == EEnemyState::Alerted || to == EEnemyState::Idle ||
                                           to == EEnemyState::Defeated;
        default: return false;
        }
    }

    bool EnemyComponent::RequestTransition(EEnemyState to)
    {
        if (!m_bPlaying || !m_Machine || !IsTransitionAllowed(m_LastRequested, to))
        {
            ++m_RejectedTransitionCount;
            NORVES_LOG_WARNING("Gameplay", "敵の無効な状態遷移を無視しました: %u → %u",
                               static_cast<unsigned>(m_LastRequested), static_cast<unsigned>(to));
            return false;
        }
        const size_t rejected = m_Machine->GetRejectedRequestCount();
        m_Machine->RequestChange(to);
        if (m_Machine->GetRejectedRequestCount() != rejected)
        {
            ++m_RejectedTransitionCount;
            NORVES_LOG_WARNING("Gameplay", "敵の状態遷移を保留できませんでした。");
            return false;
        }
        m_LastRequested = to;
        return true;
    }

    bool EnemyComponent::CanLeaveDormant() const
    {
        if (!m_bHasDefinition) return false;
        if (!m_Definition.bIsFinal) return true;
        auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        return current && current == m_BoundModule && current->GetProgressionState().IsFinalUnlocked();
    }
    void EnemyComponent::EnterCallback(StateMachine& machine, EnemyComponent& context)
    {
        context.EnterState(machine.GetCurrentState());
    }
    void EnemyComponent::UpdateCallback(StateMachine& machine, EnemyComponent& context, float deltaSeconds)
    {
        context.UpdateState(machine.GetCurrentState(), deltaSeconds);
    }
    void EnemyComponent::ExitCallback(StateMachine& machine, EnemyComponent& context)
    {
        context.ExitState(machine.GetCurrentState());
    }

    void EnemyComponent::RequestAnimation(Core::Identity state)
    {
        if (m_Animator && state.IsValid()) m_Animator->RequestState(state, 0.0f);
    }

    void EnemyComponent::EnterState(EEnemyState state)
    {
        m_StateSeconds = 0.0f;
        switch (state)
        {
        case EEnemyState::Idle: RequestAnimation(m_Definition.AnimStates.Idle); break;
        case EEnemyState::Alerted: RequestAnimation(m_Definition.AnimStates.Alerted); break;
        case EEnemyState::Attack:
            if (m_CurrentPatternIndex >= 0 && static_cast<size_t>(m_CurrentPatternIndex) < m_Definition.Patterns.size())
            {
                if (auto* hitbox = FindHitbox()) hitbox->SetProfile(m_PatternProfiles[m_CurrentPatternIndex]);
                RequestAnimation(m_Definition.Patterns[m_CurrentPatternIndex].AnimState);
                m_CurrentPhaseIndex = 0;
                m_PhaseSeconds = 0.0f;
                EnterAttackPhase();
            }
            break;
        case EEnemyState::Collapsed: RequestAnimation(m_Definition.AnimStates.Collapsed); break;
        case EEnemyState::Defeated: RequestAnimation(m_Definition.AnimStates.Defeated); break;
        default: break;
        }
    }

    void EnemyComponent::UpdateState(EEnemyState state, float deltaSeconds)
    {
        m_StateSeconds += deltaSeconds;
        switch (state)
        {
        case EEnemyState::Dormant:
            if (CanLeaveDormant()) RequestTransition(EEnemyState::Idle);
            break;
        case EEnemyState::Idle:
            if (m_bZoneEnterPending)
            {
                m_bZoneEnterPending = false;
                auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
                if (current && current == m_BoundModule &&
                    current->GetEncounterService().GetZoneState(m_EncounterZoneId) == World::EEncounterZoneState::Engaged)
                {
                    RequestTransition(EEnemyState::Alerted);
                }
            }
            break;
        case EEnemyState::Alerted:
            if (m_StateSeconds >= (m_bFirstAttack ? m_Definition.AlertDelaySeconds : m_Definition.AttackIntervalSeconds))
            {
                const int32_t selected = SelectPattern();
                if (selected >= 0)
                {
                    m_CurrentPatternIndex = selected;
                    m_bFirstAttack = false;
                    RequestTransition(EEnemyState::Attack);
                }
            }
            break;
        case EEnemyState::Attack: UpdateAttack(deltaSeconds); break;
        case EEnemyState::Defeated:
            if (m_DefeatHold.IsValid() &&
                ((m_Animator && m_Animator->IsFinished()) || m_StateSeconds >= m_Definition.DefeatHoldSeconds))
            {
                auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
                if (current && current == m_BoundModule)
                {
                    current->GetDefeatGate().Release(m_DefeatHold);
                    m_DefeatHold = {};
                }
            }
            break;
        case EEnemyState::Collapsed:
            if (m_StateSeconds >= m_Definition.Collapse.RecoverSeconds)
            {
                RequestAnimation(m_Definition.AnimStates.Recover);
                if (auto* health = FindHealth()) health->ResetStagger();
                RequestTransition(EEnemyState::Alerted);
            }
            break;
        default: break;
        }
    }

    void EnemyComponent::ExitState(EEnemyState state)
    {
        if (state == EEnemyState::Attack)
        {
            if (auto* hitbox = FindHitbox(); hitbox && hitbox->IsWindowOpen()) hitbox->EndAttack();
        }
    }

    int32_t EnemyComponent::SelectPattern()
    {
        if (m_bFirstAttack)
        {
            const int32_t opening = m_Selector.SelectOpening();
            if (opening >= 0) return opening;
        }
        auto* owner = FindOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        auto* target = world ? world->ResolveEntityHandle(m_Target) : nullptr;
        if (owner && target && !target->IsPendingDestroy() && target->IsActive())
        {
            const float distance = (target->GetWorldTransform().position - owner->GetWorldTransform().position).Length();
            if (std::isfinite(distance)) return m_Selector.Select(distance, true);
        }
        return m_Selector.Select(0.0f, false);
    }

    void EnemyComponent::EnterAttackPhase()
    {
        const auto& phases = m_Definition.Patterns[m_CurrentPatternIndex].Phases;
        if (m_CurrentPhaseIndex < phases.size() && phases[m_CurrentPhaseIndex].bHitWindow)
        {
            if (auto* hitbox = FindHitbox()) hitbox->BeginAttack();
        }
    }

    void EnemyComponent::ExitAttackPhase()
    {
        const auto& phases = m_Definition.Patterns[m_CurrentPatternIndex].Phases;
        if (m_CurrentPhaseIndex < phases.size() && phases[m_CurrentPhaseIndex].bHitWindow)
        {
            if (auto* hitbox = FindHitbox()) hitbox->EndAttack();
        }
    }

    void EnemyComponent::UpdateAttack(float deltaSeconds)
    {
        if (m_CurrentPatternIndex < 0 || static_cast<size_t>(m_CurrentPatternIndex) >= m_Definition.Patterns.size()) return;
        const auto& phases = m_Definition.Patterns[m_CurrentPatternIndex].Phases;
        m_PhaseSeconds += deltaSeconds;
        while (m_CurrentPhaseIndex < phases.size() && m_PhaseSeconds >= phases[m_CurrentPhaseIndex].Seconds)
        {
            m_PhaseSeconds -= phases[m_CurrentPhaseIndex].Seconds;
            ExitAttackPhase();
            ++m_CurrentPhaseIndex;
            if (m_CurrentPhaseIndex < phases.size()) EnterAttackPhase();
        }
        if (m_CurrentPhaseIndex == phases.size()) RequestTransition(EEnemyState::Alerted);
    }

    void EnemyComponent::OnZoneEntered(const World::EncounterZoneEvent& event)
    {
        if (event.EnemyId != GetEnemyId() || m_bDefeatStarted || m_LastRequested == EEnemyState::Defeated) return;
        if (GetState() == EEnemyState::Dormant)
        {
            if (!CanLeaveDormant()) return;
            // 初回 Tick と同じフレームの領域進入を、Idle への保留中に取り落とさない。
            m_EncounterZoneId = event.ZoneId;
            m_bZoneEnterPending = true;
        }
        else if (GetState() == EEnemyState::Idle && m_LastRequested == EEnemyState::Idle)
        {
            m_EncounterZoneId = event.ZoneId;
            RequestTransition(EEnemyState::Alerted);
        }
    }

    void EnemyComponent::OnZoneReset(const World::EncounterZoneEvent& event)
    {
        if (event.EnemyId != GetEnemyId() || m_bDefeatStarted || m_LastRequested == EEnemyState::Defeated) return;
        if (auto* health = FindHealth())
        {
            if (event.ResetPolicy == World::EEncounterResetPolicy::FullHeal) health->ResetHealth();
            health->ResetStagger();
        }
        m_PartHits = 0;
        m_bHealthBelowTriggered = false;
        m_bZoneEnterPending = false;
        // 有限のクールダウンだけを消し、選択器の乱数列を進めない。
        m_Selector.Advance(std::numeric_limits<float>::max());
        if (m_LastRequested != EEnemyState::Idle &&
            (m_LastRequested != EEnemyState::Dormant || CanLeaveDormant()))
        {
            RequestTransition(EEnemyState::Idle);
        }
    }

    void EnemyComponent::OnDefeated(const Combat::HealthComponent&)
    {
        if (m_bDefeatStarted) return;
        m_bDefeatStarted = true;
        if (auto* hitbox = FindHitbox()) hitbox->EndAttack();
        if (auto* owner = FindOwner())
        {
            if (auto* hurtbox = owner->GetComponent<Combat::HurtboxSetComponent>()) hurtbox->Disable();
        }
        if (auto* health = FindHealth()) health->SetInvulnerable(true);

        auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        if (current && current == m_BoundModule)
        {
            auto* owner = FindOwner();
            const auto ownerHandle = owner ? owner->GetEntityHandle() : Core::EntityHandle{};
            if (ownerHandle.IsValid())
            {
                BeginDefeatGate(*current, ownerHandle);
            }
            else
            {
                m_bDefeatGatePending = true;
            }
        }
        else
        {
            NORVES_LOG_WARNING("Gameplay", "撃破ゲートがないため、敵は撃破状態に留まります。");
        }
        RequestTransition(EEnemyState::Defeated);
        const auto started = OnDefeatStarted;
        started.Broadcast(*this);
    }

    void EnemyComponent::BeginDefeatGate(GameplayModule& module, Core::EntityHandle owner)
    {
        const auto enemyId = GetEnemyId();
        module.GetDefeatGate().BeginDefeat(enemyId);
        m_DefeatHold = module.GetDefeatGate().AddHold(enemyId, owner);
        if (!m_DefeatHold.IsValid())
        {
            NORVES_LOG_WARNING("Gameplay", "敵の撃破保留を取得できませんでした: %s", enemyId.c_str());
        }
    }

    void EnemyComponent::OnGateDefeatCompleted(const Core::Container::String& enemyId)
    {
        if (enemyId != GetEnemyId() || !m_bDefeatStarted || m_bDefeatCompletionPending || m_bDefeatCompleted) return;
        m_bDefeatCompletionPending = true;
        auto* current = dynamic_cast<GameplayModule*>(FindGameplayModule(Core::Module::GetModuleRegistry()));
        if (current && current == m_BoundModule && !m_EncounterZoneId.empty())
        {
            current->GetEncounterService().MarkCleared(m_EncounterZoneId);
        }
    }

    void EnemyComponent::CompleteDefeat()
    {
        m_bDefeatCompletionPending = false;
        m_bDefeatCompleted = true;
        const auto enemyId = GetEnemyId();
        const auto completed = OnDefeatCompleted;
        auto* owner = FindOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        const auto ownerHandle = owner ? owner->GetEntityHandle() : Core::EntityHandle{};
        completed.Broadcast(enemyId);
        if (auto* liveOwner = world ? world->ResolveEntityHandle(ownerHandle) : nullptr)
        {
            liveOwner->MarkForDestroy();
        }
    }

    void EnemyComponent::RequestCollapse()
    {
        if (!m_bDefeatStarted && m_LastRequested != EEnemyState::Defeated &&
            (GetState() == EEnemyState::Alerted || GetState() == EEnemyState::Attack))
        {
            RequestTransition(EEnemyState::Collapsed);
        }
    }

    void EnemyComponent::OnDamaged(const Combat::HealthComponent& health, const Combat::DamageResult&)
    {
        if (!m_bDefeatStarted && m_LastRequested != EEnemyState::Defeated && m_bHasDefinition &&
            m_Definition.Collapse.Condition == EEnemyCollapseCondition::HealthBelow &&
            !m_bHealthBelowTriggered && static_cast<float>(health.getMaxHealth()) > 0.0f &&
            static_cast<float>(health.getHealth()) / static_cast<float>(health.getMaxHealth()) < m_Definition.Collapse.HealthFraction)
        {
            m_bHealthBelowTriggered = true;
            RequestCollapse();
        }
    }

    void EnemyComponent::OnStaggerBroken(const Combat::HealthComponent&)
    {
        if (!m_bDefeatStarted && m_LastRequested != EEnemyState::Defeated && m_bHasDefinition &&
            m_Definition.Collapse.Condition == EEnemyCollapseCondition::Stagger) RequestCollapse();
    }

    void EnemyComponent::OnHurt(const Combat::CombatHitEvent& event)
    {
        if (!m_bDefeatStarted && m_LastRequested != EEnemyState::Defeated && m_bHasDefinition &&
            m_Definition.Collapse.Condition == EEnemyCollapseCondition::PartHits &&
            event.PartId == m_Definition.Collapse.PartId && m_Definition.Collapse.HitCount > 0)
        {
            if (++m_PartHits >= m_Definition.Collapse.HitCount)
            {
                m_PartHits = 0;
                RequestCollapse();
            }
        }
    }
} // namespace NorvesLib::Modules::Gameplay::Enemy
