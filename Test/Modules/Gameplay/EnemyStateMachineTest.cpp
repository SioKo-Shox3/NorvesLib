// 敵の遭遇、保留遷移、攻撃窓、崩れと復帰を反証する。
#include "EnemyTestSupport.h"

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
} // namespace

int main()
{
    EncounterAndTransitionBoundary();
    SeededAttackSequence();
    StaggerInterruptAndRecovery();
    OtherCollapseConditions();
    RejectedAndUnsubscribed();
    std::puts("EnemyStateMachineTest: PASS");
    return 0;
}
