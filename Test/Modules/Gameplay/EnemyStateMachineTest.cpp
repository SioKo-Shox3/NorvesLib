// 敵の遭遇、保留遷移、攻撃窓、崩れと復帰を反証する。
#include "EnemyTestSupport.h"
#include "Gameplay/Progression/ProgressionState.h"

#include <cstdio>

namespace
{
    using namespace NorvesLib::Test::EnemySupport;

    void EncounterAndTransitionBoundary()
    {
        Fixture fixture(MakeDefinition());
        const auto* registration = fixture.World.FindComponentTick(E::EnemyComponent::StaticClass());
        COMBAT_CHECK(registration && registration->Run && registration->bPrimaryGroupOnly &&
                     registration->TimeChannel == C::Engine::TimeChannel::World &&
                     registration->Groups == C::Component::TickGroupBit(C::Component::ETickGroup::Default));
        fixture.StartIdle();
        COMBAT_CHECK(fixture.Animator.HasRequest(C::Identity("idle")));
        fixture.AddZone("zone.other", "enemy.other", 20.0f);
        fixture.AddZone("zone.test", "enemy.test", 0.0f);
        fixture.AddSensor(20.0f);
        fixture.Evaluate();
        fixture.Enemy->TickEnemy(1.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Idle);
        fixture.Sensor->SetLocalPosition(0.0f, 0.0f, 0.0f);
        fixture.Evaluate();
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Idle);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
        COMBAT_CHECK(fixture.Animator.HasRequest(C::Identity("alerted")));
        fixture.Enemy->TickEnemy(0.25f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);

        // 初回 Tick の直後に領域へ入っても、Idle への保留を越えて警戒できる。
        Fixture early(MakeDefinition());
        early.AddZone("zone.early", "enemy.test", 50.0f);
        early.AddSensor(50.0f);
        early.Enemy->TickEnemy(0.0f);
        early.Evaluate();
        early.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(early.Enemy->GetState() == E::EEnemyState::Idle);
        early.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(early.Enemy->GetState() == E::EEnemyState::Alerted);
    }

    C::Container::VariableArray<int32_t> PatternSequence()
    {
        auto definition = MakeDefinition();
        definition.OpeningPatternId = "attack.b";
        Fixture fixture(definition, 0x123456789abcdef0ULL);
        fixture.StartAlerted();
        C::Container::VariableArray<int32_t> result;
        for (int i = 0; i < 12; ++i)
        {
            fixture.Enemy->TickEnemy(i == 0 ? 0.25f : 0.4f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);
            const int32_t index = fixture.Enemy->GetCurrentPatternIndex();
            COMBAT_CHECK(index >= 0 && static_cast<size_t>(index) < definition.Patterns.size());
            COMBAT_CHECK(fixture.Animator.HasRequest(definition.Patterns[index].AnimState));
            result.push_back(index);
            fixture.Enemy->TickEnemy(1.0f);
            COMBAT_CHECK(!fixture.Hitbox->IsWindowOpen());
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
        }
        COMBAT_CHECK(result[0] == 1);
        return result;
    }

    void SeededAttackSequence()
    {
        const auto first = PatternSequence();
        const auto second = PatternSequence();
        COMBAT_CHECK(first.size() == second.size());
        for (size_t i = 0; i < first.size(); ++i) COMBAT_CHECK(first[i] == second[i]);
    }

    void StaggerInterruptAndRecovery()
    {
        Fixture fixture(MakeDefinition());
        fixture.StartAttack();
        COMBAT_CHECK(fixture.Enemy->GetCurrentPhaseIndex() == 0 && !fixture.Hitbox->IsWindowOpen());
        fixture.Enemy->TickEnemy(0.2f);
        COMBAT_CHECK(fixture.Enemy->GetCurrentPhaseIndex() == 1 && fixture.Hitbox->IsWindowOpen());
        fixture.Health->ApplyDamage({1.0f, 10.0f});
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed && !fixture.Hitbox->IsWindowOpen());
        COMBAT_CHECK(fixture.Animator.HasRequest(C::Identity("collapsed")));
        fixture.Health->ApplyDamage({0.0f, 4.0f});
        for (int i = 0; i < 3; ++i) fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
        fixture.Enemy->TickEnemy(0.49f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
        fixture.Enemy->TickEnemy(0.01f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
        COMBAT_CHECK(fixture.Animator.HasRequest(C::Identity("recover")) && fixture.Health->GetStaggerMeter() == 0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
    }

    void OtherCollapseConditions()
    {
        {
            auto definition = MakeDefinition();
            definition.Collapse.Condition = E::EEnemyCollapseCondition::HealthBelow;
            definition.Collapse.HealthFraction = 0.5f;
            Fixture fixture(definition);
            fixture.StartAlerted();
            fixture.Health->ApplyDamage({50.0f, 0.0f});
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
            fixture.Health->ApplyDamage({1.0f, 0.0f});
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
            fixture.Enemy->TickEnemy(0.5f);
            fixture.Enemy->TickEnemy(0.0f);
            fixture.Health->ApplyDamage({1.0f, 0.0f});
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
        }
        {
            auto definition = MakeDefinition();
            definition.Collapse.Condition = E::EEnemyCollapseCondition::PartHits;
            definition.Collapse.PartId = 7;
            definition.Collapse.HitCount = 2;
            Fixture fixture(definition);
            fixture.StartAlerted();
            Combat::CombatHitEvent hit;
            hit.PartId = 8;
            fixture.Hurtbox->OnHurt.Broadcast(hit);
            hit.PartId = 7;
            fixture.Hurtbox->OnHurt.Broadcast(hit);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Alerted);
            fixture.Hurtbox->OnHurt.Broadcast(hit);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
        }
    }

    void RejectedAndUnsubscribed()
    {
        Fixture fixture(MakeDefinition());
        fixture.StartIdle();
        const size_t before = fixture.Enemy->GetRejectedTransitionCount();
        COMBAT_CHECK(!fixture.Enemy->TryTransition(E::EEnemyState::Attack));
        COMBAT_CHECK(!fixture.Enemy->TryTransition(E::EEnemyState::Collapsed));
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Idle &&
                     fixture.Enemy->GetRejectedTransitionCount() == before + 2);
        const size_t zoneCount = fixture.Gameplay.GetEncounterService().OnZoneEntered.GetSize();
        COMBAT_CHECK(zoneCount > 0 && fixture.Health->OnDamaged.GetSize() == 1 &&
                     fixture.Health->OnStaggerBroken.GetSize() == 1 && fixture.Hurtbox->OnHurt.GetSize() == 1);
        fixture.Owner->RemoveComponent(fixture.Enemy);
        fixture.Enemy = nullptr;
        COMBAT_CHECK(fixture.Gameplay.GetEncounterService().OnZoneEntered.GetSize() == zoneCount - 1);
        COMBAT_CHECK(fixture.Health->OnDamaged.IsEmpty() && fixture.Health->OnStaggerBroken.IsEmpty() &&
                     fixture.Hurtbox->OnHurt.IsEmpty());
        fixture.Health->ApplyDamage({1.0f, 20.0f});
        fixture.AddZone("zone.test", "enemy.test", 0.0f);
        fixture.AddSensor(0.0f);
        fixture.Evaluate();

        Fixture interrupted(MakeDefinition());
        interrupted.StartAttack();
        interrupted.Enemy->TickEnemy(0.2f);
        COMBAT_CHECK(interrupted.Hitbox->IsWindowOpen());
        const size_t subscribed = interrupted.Gameplay.GetEncounterService().OnZoneEntered.GetSize();
        interrupted.Enemy->EndPlay();
        COMBAT_CHECK(!interrupted.Hitbox->IsWindowOpen());
        COMBAT_CHECK(interrupted.Health->OnDamaged.IsEmpty() && interrupted.Hurtbox->OnHurt.IsEmpty());
        COMBAT_CHECK(interrupted.Gameplay.GetEncounterService().OnZoneEntered.GetSize() == subscribed - 1);
        interrupted.Owner->RemoveComponent(interrupted.Enemy);
        interrupted.Enemy = nullptr;
    }
    void DefeatedCannotLeave()
    {
        auto definition = MakeDefinition();
        definition.Id = "enemy.terminal";
        Fixture fixture(definition);
        auto& progression = fixture.Gameplay.GetProgressionState();
        COMBAT_CHECK(progression.LoadDefinitionFromJson(
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["enemy.terminal"]})"));
        COMBAT_CHECK(progression.LoadStateFromJson(R"({"version":1,"defeatedEnemyIds":[]})"));
        fixture.StartIdle();
        fixture.Health->ApplyDamage({100.0f, 0.0f});
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Defeated);
        const size_t rejected = fixture.Enemy->GetRejectedTransitionCount();
        COMBAT_CHECK(!fixture.Enemy->TryTransition(E::EEnemyState::Alerted));
        COMBAT_CHECK(!fixture.Enemy->TryTransition(E::EEnemyState::Idle));
        W::EncounterZoneEvent reset;
        reset.EnemyId = definition.Id;
        reset.ResetPolicy = W::EEncounterResetPolicy::FullHeal;
        fixture.Gameplay.GetEncounterService().OnZoneReset.Broadcast(reset);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Defeated);
        COMBAT_CHECK(fixture.Enemy->GetRejectedTransitionCount() == rejected + 2);
        COMBAT_CHECK(static_cast<float>(fixture.Health->getHealth()) == 0.0f);
        fixture.Enemy->TickEnemy(1.0f);
        fixture.Gameplay.Tick(0.0f);
        COMBAT_CHECK(!progression.IsDefeated(definition.Id));
        fixture.Enemy->TickEnemy(0.5f);
        fixture.Gameplay.Tick(0.0f);
        COMBAT_CHECK(progression.IsDefeated(definition.Id));
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Owner->IsPendingDestroy());

        const auto zoneCount = fixture.Gameplay.GetEncounterService().OnZoneReset.GetSize();
        const auto gateCount = fixture.Gameplay.GetDefeatGate().OnDefeatCompleted.GetSize();
        fixture.Enemy->EndPlay();
        COMBAT_CHECK(fixture.Health->OnDefeated.IsEmpty());
        COMBAT_CHECK(fixture.Gameplay.GetEncounterService().OnZoneReset.GetSize() == zoneCount - 1);
        COMBAT_CHECK(fixture.Gameplay.GetDefeatGate().OnDefeatCompleted.GetSize() == gateCount - 1);
    }

    void DefeatOrder()
    {
        auto definition = MakeDefinition();
        definition.Id = "enemy.order";
        Fixture fixture(definition);
        auto& progression = fixture.Gameplay.GetProgressionState();
        COMBAT_CHECK(progression.LoadDefinitionFromJson(
            R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["enemy.order"]})"));
        COMBAT_CHECK(progression.LoadStateFromJson(R"({"version":1,"defeatedEnemyIds":[]})"));
        fixture.StartIdle();
        fixture.AddZone("zone.order", definition.Id.c_str(), 0.0f);
        fixture.AddSensor(0.0f);
        fixture.Evaluate();
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Enemy->TickEnemy(0.25f);
        fixture.Enemy->TickEnemy(0.0f);
        fixture.Enemy->TickEnemy(0.2f);
        COMBAT_CHECK(fixture.Hitbox->IsWindowOpen());

        unsigned started = 0;
        unsigned startedAfterRemoval = 0;
        C::Delegate<void, const E::EnemyComponent&> firstStarted;
        firstStarted = [&](const E::EnemyComponent& enemy)
        {
            ++started;
            COMBAT_CHECK(&enemy == fixture.Enemy && !fixture.Hitbox->IsWindowOpen());
            COMBAT_CHECK(!fixture.Hurtbox->IsEnabled() && fixture.Health->IsInvulnerable());
            fixture.Enemy->OnDefeatStarted.Remove(firstStarted);
        };
        fixture.Enemy->OnDefeatStarted.Add(firstStarted);
        fixture.Enemy->OnDefeatStarted.Add([&](const E::EnemyComponent&) { ++startedAfterRemoval; });
        unsigned completed = 0;
        unsigned completedAfterRemoval = 0;
        C::Delegate<void, const C::Container::String&> firstCompleted;
        firstCompleted = [&](const C::Container::String& enemyId)
        {
            ++completed;
            COMBAT_CHECK(enemyId == definition.Id && progression.IsDefeated(enemyId));
            COMBAT_CHECK(fixture.Gameplay.GetEncounterService().GetZoneState("zone.order") == W::EEncounterZoneState::Cleared);
            COMBAT_CHECK(!fixture.Owner->IsPendingDestroy());
            fixture.Enemy->OnDefeatCompleted.Remove(firstCompleted);
        };
        fixture.Enemy->OnDefeatCompleted.Add(firstCompleted);
        fixture.Enemy->OnDefeatCompleted.Add([&](const C::Container::String&) { ++completedAfterRemoval; });
        fixture.Health->ApplyDamage({100.0f, 0.0f});
        COMBAT_CHECK(started == 1 && startedAfterRemoval == 1 && completed == 0);
        COMBAT_CHECK(!fixture.Owner->IsPendingDestroy());
        fixture.Animator.bFinished = true;
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Defeated);
        COMBAT_CHECK(fixture.Animator.HasRequest(C::Identity("defeated")));
        fixture.Gameplay.Tick(0.0f);
        COMBAT_CHECK(progression.IsDefeated(definition.Id) && completed == 0);
        COMBAT_CHECK(fixture.Gameplay.GetEncounterService().GetZoneState("zone.order") != W::EEncounterZoneState::Cleared);
        fixture.Evaluate();
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(completed == 1 && completedAfterRemoval == 1 && fixture.Owner->IsPendingDestroy());
        fixture.Gameplay.Tick(0.0f);
        fixture.Enemy->TickEnemy(0.0f);
        COMBAT_CHECK(completed == 1 && completedAfterRemoval == 1);
    }

    void ResetPolicies()
    {
        for (const auto policy : {W::EEncounterResetPolicy::FullHeal, W::EEncounterResetPolicy::KeepDamage})
        {
            auto definition = MakeDefinition();
            definition.Patterns.resize(1);
            definition.Patterns[0].CooldownSeconds = 100.0f;
            if (policy == W::EEncounterResetPolicy::FullHeal)
            {
                definition.Collapse.Condition = E::EEnemyCollapseCondition::HealthBelow;
                definition.Collapse.HealthFraction = 0.9f;
            }
            else
            {
                definition.Collapse.Condition = E::EEnemyCollapseCondition::PartHits;
                definition.Collapse.PartId = 7;
                definition.Collapse.HitCount = 2;
            }
            Fixture fixture(definition);
            fixture.StartAttack();
            fixture.Enemy->TickEnemy(0.2f);
            COMBAT_CHECK(fixture.Hitbox->IsWindowOpen());
            fixture.Health->ApplyDamage({25.0f, 3.0f});
            Combat::CombatHitEvent hit;
            hit.PartId = 7;
            if (policy == W::EEncounterResetPolicy::KeepDamage) fixture.Hurtbox->OnHurt.Broadcast(hit);
            W::EncounterZoneEvent reset;
            reset.EnemyId = definition.Id;
            reset.ResetPolicy = policy;
            fixture.Gameplay.GetEncounterService().OnZoneReset.Broadcast(reset);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Idle && !fixture.Hitbox->IsWindowOpen());
            COMBAT_CHECK(static_cast<float>(fixture.Health->getHealth()) ==
                         (policy == W::EEncounterResetPolicy::FullHeal ? 100.0f : 75.0f));
            COMBAT_CHECK(fixture.Health->GetStaggerMeter() == 0.0f);

            W::EncounterZoneEvent entered;
            entered.EnemyId = definition.Id;
            entered.ZoneId = "zone.test";
            fixture.Gameplay.GetEncounterService().OnZoneEntered.Broadcast(entered);
            fixture.Enemy->TickEnemy(0.0f);
            fixture.Enemy->TickEnemy(0.4f);
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);
            if (policy == W::EEncounterResetPolicy::FullHeal)
            {
                fixture.Health->ApplyDamage({15.0f, 0.0f});
            }
            else
            {
                fixture.Hurtbox->OnHurt.Broadcast(hit);
                fixture.Enemy->TickEnemy(0.0f);
                COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Attack);
                fixture.Hurtbox->OnHurt.Broadcast(hit);
            }
            fixture.Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(fixture.Enemy->GetState() == E::EEnemyState::Collapsed);
        }
    }
} // namespace

int main()
{
    EncounterAndTransitionBoundary();
    SeededAttackSequence();
    StaggerInterruptAndRecovery();
    OtherCollapseConditions();
    RejectedAndUnsubscribed();
    DefeatedCannotLeave();
    DefeatOrder();
    ResetPolicies();
    std::puts("EnemyStateMachineTest: PASS");
    return 0;
}
