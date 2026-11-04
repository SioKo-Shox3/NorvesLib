#pragma once

#include "Component/Component.h"
#include "Container/PointerTypes.h"
#include "Delegate/Delegate.h"
#include "Delegate/MulticastDelegate.h"
#include "Gameplay/Enemy/EnemyDefinition.h"
#include "Gameplay/Enemy/EnemyPatternSelector.h"
#include "Gameplay/Enemy/IEnemyAnimator.h"
#include "Gameplay/Progression/DefeatGate.h"
#include "Gameplay/World/EncounterZoneTypes.h"
#include "GameMode/TStateMachine.h"
#include "Object/EntityHandle.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay
{
    class GameplayModule;
    namespace Combat
    {
        class HealthComponent;
        class HitboxComponent;
        class HurtboxSetComponent;
        struct DamageResult;
        struct CombatHitEvent;
    }
}

namespace NorvesLib::Modules::Gameplay::Enemy
{
    class AnimatorEnemyAnimator;

    enum class EEnemyState : uint8_t
    {
        Dormant, Idle, Alerted, Attack, Collapsed, Defeated, Count
    };

    /// 遭遇と定義に従い敵の状態と攻撃窓を進める。操作は GameThread 専用。
    class EnemyComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(EnemyComponent, Core::Component::Component)

    public:
        EnemyComponent();
        ~EnemyComponent() override;
        void Initialize() override;
        void BeginPlay() override;
        void EndPlay() override;
        void Finalize() override;

        /// BeginPlay 前に定義を写す。所有 Entity が付いていれば体力にも反映する。
        bool SetDefinition(const EnemyDefinition& definition);
        /// 借用先は解除または EndPlay まで生存させる。
        void SetAnimator(IEnemyAnimator* animator);
        void SetTarget(Core::EntityHandle target) { m_Target = target; }

        const Core::Container::String& GetEnemyId() const;
        EEnemyState GetState() const;
        size_t GetRejectedTransitionCount() const { return m_RejectedTransitionCount; }
        int32_t GetCurrentPatternIndex() const { return m_CurrentPatternIndex; }
        size_t GetCurrentPhaseIndex() const { return m_CurrentPhaseIndex; }
        void TickEnemy(float worldDeltaSeconds);

        /// HP が 0 になった瞬間、HealthComponent の通知中に配る。受け手は同期で破棄せず MarkForDestroy を使う。
        Core::MulticastDelegate<const EnemyComponent&> OnDefeatStarted;
        /// 撃破ゲート完了後、領域があれば次の領域評価で Cleared が確定してから、次の TickEnemy で配る。
        /// 領域がなければゲート完了後の次の TickEnemy で配る。進行状態への記録と領域の Cleared は通知前に済み、
        /// 通知後に Entity の破棄を予約する。
        Core::MulticastDelegate<const Core::Container::String&> OnDefeatCompleted;

        /// Default は Animation より前なので、Enter のアニメ要求は同じフレームに反映される。
        /// 型の照合は完全一致のため、派生型はこの登録を自分の型に対して行う。
        static Core::Component::ComponentTickRegistration MakeTickRegistration();
        static void RegisterTick(Core::World& world);

        PROPERTY(Core::Container::String, DefinitionPath)
        PROPERTY(Core::Container::String, EnemyId)

    protected:
        /// 保留中を含む最後の要求から遷移表を検証する。
        bool RequestTransition(EEnemyState to);
        virtual bool CanLeaveDormant() const;
        virtual void EnterState(EEnemyState state);
        virtual void UpdateState(EEnemyState state, float deltaSeconds);
        virtual void ExitState(EEnemyState state);
        const EnemyDefinition& GetDefinition() const { return m_Definition; }
        const Core::Container::String& GetEncounterZoneId() const { return m_EncounterZoneId; }
        float GetStateSeconds() const { return m_StateSeconds; }
        Combat::HealthComponent* FindHealth() const;
        Combat::HitboxComponent* FindHitbox() const;
        void RequestAnimation(Core::Identity state);

    private:
        using StateMachine = Core::GameMode::TStateMachine<EnemyComponent, EEnemyState, 8,
                                                         static_cast<size_t>(EEnemyState::Count)>;
        struct Subscription
        {
            EnemyComponent* Target = nullptr;
            size_t DispatchDepth = 0;
        };

        static void EnterCallback(StateMachine& machine, EnemyComponent& context);
        static void UpdateCallback(StateMachine& machine, EnemyComponent& context, float deltaSeconds);
        static void ExitCallback(StateMachine& machine, EnemyComponent& context);
        static bool IsTransitionAllowed(EEnemyState from, EEnemyState to);
        void ApplyHealthDefinition();
        void TryBindAnimator();
        void Subscribe();
        void Unsubscribe();
        Core::Entity* FindOwner() const;
        void OnZoneEntered(const World::EncounterZoneEvent& event);
        void OnZoneReset(const World::EncounterZoneEvent& event);
        void OnDefeated(const Combat::HealthComponent& health);
        void BeginDefeatGate(GameplayModule& module, Core::EntityHandle owner);
        void OnGateDefeatCompleted(const Core::Container::String& enemyId);
        void CompleteDefeat();
        void OnDamaged(const Combat::HealthComponent& health, const Combat::DamageResult& result);
        void OnStaggerBroken(const Combat::HealthComponent& health);
        void OnHurt(const Combat::CombatHitEvent& event);
        void RequestCollapse();
        void EnterAttackPhase();
        void ExitAttackPhase();
        void UpdateAttack(float deltaSeconds);
        int32_t SelectPattern();

        EnemyDefinition m_Definition;
        EnemyPatternSelector m_Selector;
        Core::Container::VariableArray<Core::Container::TSharedPtr<const Combat::AttackProfile>> m_PatternProfiles;
        Core::Container::TUniquePtr<StateMachine> m_Machine;
        Core::Container::TUniquePtr<AnimatorEnemyAnimator> m_OwnedAnimator;
        IEnemyAnimator* m_Animator = nullptr;
        Core::EntityHandle m_Target;
        Core::Container::String m_EncounterZoneId;
        DefeatHoldHandle m_DefeatHold;
        float m_StateSeconds = 0.0f;
        float m_PhaseSeconds = 0.0f;
        size_t m_CurrentPhaseIndex = 0;
        int32_t m_CurrentPatternIndex = -1;
        size_t m_RejectedTransitionCount = 0;
        uint32_t m_PartHits = 0;
        EEnemyState m_LastRequested = EEnemyState::Dormant;
        bool m_bHasDefinition = false;
        bool m_bFirstAttack = true;
        bool m_bHealthBelowTriggered = false;
        bool m_bZoneEnterPending = false;
        bool m_bPlaying = false;
        bool m_bDefeatStarted = false;
        bool m_bDefeatGatePending = false;
        bool m_bDefeatCompletionPending = false;
        bool m_bDefeatCompleted = false;

        Core::World* m_BoundWorld = nullptr;
        Core::EntityHandle m_BoundOwner;
        GameplayModule* m_BoundModule = nullptr;
        Combat::HealthComponent* m_SubscribedHealth = nullptr;
        Combat::HurtboxSetComponent* m_SubscribedHurtbox = nullptr;
        uint64_t m_HealthId = 0;
        uint64_t m_HurtboxId = 0;
        Core::Container::TSharedPtr<Subscription> m_Subscription;
        Core::Delegate<void, const Combat::HealthComponent&, const Combat::DamageResult&> m_DamagedCallback;
        Core::Delegate<void, const Combat::HealthComponent&> m_StaggerCallback;
        Core::Delegate<void, const Combat::HealthComponent&> m_DefeatedCallback;
        Core::Delegate<void, const Combat::CombatHitEvent&> m_HurtCallback;
        Core::Delegate<void, const World::EncounterZoneEvent&> m_ZoneCallback;
        Core::Delegate<void, const World::EncounterZoneEvent&> m_ZoneResetCallback;
        Core::Delegate<void, const Core::Container::String&> m_DefeatCompletedCallback;
    };
} // namespace NorvesLib::Modules::Gameplay::Enemy
