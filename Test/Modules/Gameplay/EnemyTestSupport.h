// 敵の状態試験で定義、アニメ要求と Gameplay の World を共有する。
#pragma once

#include "CombatTestSupport.h"
#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Enemy/EnemyComponent.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"

namespace NorvesLib::Test::EnemySupport
{
    namespace C = NorvesLib::Core;
    namespace G = NorvesLib::Modules::Gameplay;
    namespace E = NorvesLib::Modules::Gameplay::Enemy;
    namespace W = NorvesLib::Modules::Gameplay::World;
    namespace Combat = NorvesLib::Modules::Gameplay::Combat;
    namespace CS = NorvesLib::Test::CombatSupport;

    class StubAnimator : public E::IEnemyAnimator
    {
    public:
        bool RequestState(C::Identity state, float) override
        {
            Requests.push_back(state);
            return true;
        }
        double NormalizedTime() const override { return 0.0; }
        bool IsFinished() const override { return bFinished; }
        void SetEventListener(C::Delegate<void, const C::Animation::AnimEventInfo&> listener) override
        {
            Listener = listener;
        }
        bool HasRequest(C::Identity state) const
        {
            for (const auto& request : Requests) if (request == state) return true;
            return false;
        }
        C::Container::VariableArray<C::Identity> Requests;
        C::Delegate<void, const C::Animation::AnimEventInfo&> Listener;
        bool bFinished = false;
    };

    inline E::EnemyDefinition MakeDefinition()
    {
        E::EnemyDefinition definition;
        definition.Id = "enemy.test";
        definition.MaxHealth = 100.0f;
        definition.StaggerThreshold = 10.0f;
        definition.AnimMachine = C::Identity("enemy.machine");
        definition.AnimStates.Idle = C::Identity("idle");
        definition.AnimStates.Alerted = C::Identity("alerted");
        definition.AnimStates.Collapsed = C::Identity("collapsed");
        definition.AnimStates.Recover = C::Identity("recover");
        definition.AlertDelaySeconds = 0.25f;
        definition.AttackIntervalSeconds = 0.4f;
        definition.Collapse.RecoverSeconds = 0.5f;
        definition.RepeatPenalty = 0.5f;
        E::EnemyAttackProfileDef profile;
        profile.Id = "profile";
        profile.Damage = 1.0f;
        profile.Stagger = 1.0f;
        profile.MaxDurationSeconds = 2.0f;
        definition.AttackProfiles.push_back(profile);
        for (int i = 0; i < 2; ++i)
        {
            E::EnemyAttackPattern pattern;
            pattern.Id = i == 0 ? "attack.a" : "attack.b";
            pattern.AnimState = C::Identity(i == 0 ? "attack.a" : "attack.b");
            pattern.AttackProfileId = "profile";
            pattern.Weight = 1.0f;
            pattern.MinDistance = 0.0f;
            pattern.MaxDistance = 100.0f;
            pattern.Phases.push_back({"prepare", 0.2f, false});
            pattern.Phases.push_back({"hit", 0.2f, true});
            pattern.Phases.push_back({"finish", 0.2f, false});
            definition.Patterns.push_back(pattern);
        }
        return definition;
    }

    class TestEnemyComponent : public E::EnemyComponent
    {
    public:
        bool TryTransition(E::EEnemyState state) { return RequestTransition(state); }
    };

    struct Fixture
    {
        StubAnimator Animator;
        C::World& World;
        G::IGameplayModule& Gameplay;
        C::Entity* Owner = nullptr;
        C::Entity* Sensor = nullptr;
        C::Container::VariableArray<C::Entity*> Zones;
        Combat::HealthComponent* Health = nullptr;
        Combat::HitboxComponent* Hitbox = nullptr;
        Combat::HurtboxSetComponent* Hurtbox = nullptr;
        TestEnemyComponent* Enemy = nullptr;

        explicit Fixture(const E::EnemyDefinition& definition, uint64_t seed = 42)
            : World(CS::GetWorld()), Gameplay(CS::GetGameplay())
        {
            Gameplay.SetRunSeed(seed);
            Owner = World.SpawnEntity<C::Entity>();
            COMBAT_CHECK(Owner != nullptr);
            Health = World.CreateComponent<Combat::HealthComponent>(Owner);
            Hitbox = World.CreateComponent<Combat::HitboxComponent>(Owner);
            Hurtbox = World.CreateComponent<Combat::HurtboxSetComponent>(Owner);
            COMBAT_CHECK(Health && Hitbox && Hurtbox);
            Enemy = new TestEnemyComponent();
            COMBAT_CHECK(Enemy->SetDefinition(definition));
            Enemy->SetAnimator(&Animator);
            COMBAT_CHECK(Owner->AddComponent(Enemy));
            COMBAT_CHECK(static_cast<float>(Health->getMaxHealth()) == definition.MaxHealth);
            COMBAT_CHECK(static_cast<float>(Health->getHealth()) == definition.MaxHealth);
            COMBAT_CHECK(static_cast<float>(Health->getStaggerThreshold()) == definition.StaggerThreshold);
        }

        ~Fixture()
        {
            if (Owner) World.RemoveEntity(Owner);
            if (Sensor) World.RemoveEntity(Sensor);
            for (auto* zone : Zones) World.RemoveEntity(zone);
        }

        void AddZone(const char* zoneId, const char* enemyId, float x)
        {
            auto* entity = World.SpawnEntity<C::Entity>();
            COMBAT_CHECK(entity != nullptr);
            entity->SetLocalPosition(x, 0.0f, 0.0f);
            auto* zone = World.CreateComponent<W::EncounterZoneComponent>(entity);
            COMBAT_CHECK(zone != nullptr);
            zone->getZoneId() = C::Container::String(zoneId);
            zone->getEnemyId() = C::Container::String(enemyId);
            zone->getRadius() = 3.0f;
            Gameplay.GetEncounterService().RequestRegisterZone(entity->GetEntityHandle());
            Zones.push_back(entity);
        }

        void AddSensor(float x)
        {
            Sensor = World.SpawnEntity<C::Entity>();
            COMBAT_CHECK(Sensor != nullptr);
            Sensor->SetLocalPosition(x, 0.0f, 0.0f);
            COMBAT_CHECK(World.CreateComponent<W::ZoneSensorComponent>(Sensor) != nullptr);
            Gameplay.GetEncounterService().RequestRegisterSensor(Sensor->GetEntityHandle());
        }

        void Evaluate() { Gameplay.GetEncounterService().Evaluate(World, 0.0f); }
        void StartIdle()
        {
            Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(Enemy->GetState() == E::EEnemyState::Dormant);
            Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(Enemy->GetState() == E::EEnemyState::Idle);
        }
        void StartAlerted()
        {
            StartIdle();
            AddZone("zone.test", "enemy.test", 0.0f);
            AddSensor(0.0f);
            Evaluate();
            Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(Enemy->GetState() == E::EEnemyState::Alerted);
        }
        void StartAttack()
        {
            StartAlerted();
            Enemy->TickEnemy(0.25f);
            COMBAT_CHECK(Enemy->GetState() == E::EEnemyState::Alerted);
            Enemy->TickEnemy(0.0f);
            COMBAT_CHECK(Enemy->GetState() == E::EEnemyState::Attack);
        }
    };
} // namespace NorvesLib::Test::EnemySupport
