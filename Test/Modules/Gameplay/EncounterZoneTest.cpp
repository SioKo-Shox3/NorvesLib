// 遭遇領域の通知、履歴依存の境界、プロファイルと物理への非干渉を反証する。
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"
#include "../Physics/PhysicsModuleTestAccess.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace P = NorvesLib::Modules::Physics;
    using namespace NorvesLib::Modules::Gameplay::World;
    using String = C::Container::String;
    template <typename T>
    using Array = C::Container::VariableArray<T>;

#define ENCOUNTER_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "EncounterZone %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    enum class EEventKind
    {
        Enter,
        Exit,
        Reset,
        Cleared
    };

    struct RecordedEvent
    {
        EEventKind Kind;
        EncounterZoneEvent Event;
    };

    struct Fixture
    {
        C::World World;
        NorvesLib::Modules::Gameplay::ProgressionState Progression;
        EncounterService Service{Progression};
        Array<RecordedEvent> Events;
        C::Entity* SensorEntity = nullptr;
        ZoneSensorComponent* Sensor = nullptr;

        Fixture()
        {
            World.Initialize();
            Service.OnZoneEntered.Add([this](const EncounterZoneEvent& event)
            {
                Events.push_back({EEventKind::Enter, event});
            });
            Service.OnZoneExited.Add([this](const EncounterZoneEvent& event)
            {
                Events.push_back({EEventKind::Exit, event});
            });
            Service.OnZoneReset.Add([this](const EncounterZoneEvent& event)
            {
                Events.push_back({EEventKind::Reset, event});
            });
            Service.OnZoneCleared.Add([this](const EncounterZoneEvent& event)
            {
                Events.push_back({EEventKind::Cleared, event});
            });
        }

        ~Fixture()
        {
            World.Finalize();
        }

        EncounterZoneComponent* AddZone(const char* id, float radius = 10.0f)
        {
            auto* entity = World.SpawnEntity<C::Entity>();
            ENCOUNTER_CHECK(entity != nullptr);
            auto* zone = World.CreateComponent<EncounterZoneComponent>(entity);
            ENCOUNTER_CHECK(zone != nullptr);
            zone->getZoneId() = String(id);
            zone->getRadius() = radius;
            zone->getHalfExtents() = M::Vector3(radius);
            zone->getHalfHeight() = radius;
            zone->getEnterMargin() = 1.0f;
            zone->getExitMargin() = 3.0f;
            zone->getResetDelaySeconds() = 1.0f;
            Service.RequestRegisterZone(World.GetEntityHandle(*entity));
            return zone;
        }

        void AddSensor(float x)
        {
            SensorEntity = World.SpawnEntity<C::Entity>();
            ENCOUNTER_CHECK(SensorEntity != nullptr);
            SensorEntity->SetLocalPosition(x, 0.0f, 0.0f);
            Sensor = World.CreateComponent<ZoneSensorComponent>(SensorEntity);
            ENCOUNTER_CHECK(Sensor != nullptr);
            Service.RequestRegisterSensor(World.GetEntityHandle(*SensorEntity));
        }

        void Sample(float x, float dt)
        {
            ENCOUNTER_CHECK(SensorEntity != nullptr);
            SensorEntity->SetLocalPosition(x, 0.0f, 0.0f);
            Service.Evaluate(World, dt);
        }

        size_t Count(EEventKind kind) const
        {
            size_t count = 0;
            for (const auto& recorded : Events)
            {
                if (recorded.Kind == kind)
                {
                    ++count;
                }
            }
            return count;
        }
    };

    // 進入前は帯で武装せず、進入後は帯の往復で解除されない。
    void HysteresisBand()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("band");
        fixture.AddSensor(20.0f);
        const float positions[] = {20.0f, 12.75f, 11.25f, 12.75f, 10.5f};
        for (float x : positions)
        {
            fixture.Sample(x, 0.1f);
            ENCOUNTER_CHECK(fixture.Count(EEventKind::Enter) == (x <= 11.0f ? 1u : 0u));
        }
        for (unsigned i = 0; i < 12; ++i)
        {
            fixture.Sample(i % 2 == 0 ? 11.25f : 12.75f, 0.2f);
            ENCOUNTER_CHECK(fixture.Count(EEventKind::Enter) == 1);
            ENCOUNTER_CHECK(fixture.Count(EEventKind::Exit) == 0);
            ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Engaged);
        }
        ENCOUNTER_CHECK(fixture.Events.size() == 1);
        ENCOUNTER_CHECK(fixture.Events[0].Event.ZoneId == "band");
    }

    // 短い離脱から戻ると時計を消し、連続した長い離脱だけを通知する。
    void DelayedExitAndResetPolicy()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("delay");
        zone->getShape() = EEncounterZoneShape::Box;
        zone->getResetPolicy() = EEncounterResetPolicy::KeepDamage;
        fixture.AddSensor(0.0f);
        fixture.Sample(0.0f, 0.0f);
        fixture.Sample(20.0f, 0.0f);
        fixture.Sample(20.0f, 0.5f);
        ENCOUNTER_CHECK(fixture.Events.size() == 1);
        fixture.Sample(0.0f, 0.1f);
        ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Engaged);
        ENCOUNTER_CHECK(fixture.Count(EEventKind::Exit) == 0);
        fixture.Sample(20.0f, 0.0f);
        fixture.Sample(20.0f, 0.75f);
        ENCOUNTER_CHECK(fixture.Events.size() == 1);
        ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Resetting);
        fixture.Sample(20.0f, 0.3f);
        ENCOUNTER_CHECK(fixture.Events.size() == 3);
        ENCOUNTER_CHECK(fixture.Events[1].Kind == EEventKind::Exit);
        ENCOUNTER_CHECK(fixture.Events[2].Kind == EEventKind::Reset);
        for (const auto& recorded : fixture.Events)
        {
            ENCOUNTER_CHECK(recorded.Event.ZoneId == "delay");
            ENCOUNTER_CHECK(recorded.Event.ResetPolicy == EEncounterResetPolicy::KeepDamage);
        }
        ENCOUNTER_CHECK(fixture.Events[1].Event.EvaluationSequence == fixture.Events[2].Event.EvaluationSequence);
        fixture.Sample(20.0f, 5.0f);
        ENCOUNTER_CHECK(fixture.Count(EEventKind::Exit) == 1 && fixture.Count(EEventKind::Reset) == 1);
        ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Outside);
    }

    // 経過時間ゼロでも、テレポートした評価の中で退出する。
    void TeleportExitsImmediately()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("teleport");
        zone->getShape() = EEncounterZoneShape::CylinderY;
        zone->getResetDelaySeconds() = 30.0f;
        fixture.AddSensor(0.0f);
        fixture.Sample(0.0f, 0.0f);
        fixture.SensorEntity->SetLocalPosition(20.0f, 0.0f, 0.0f);
        fixture.Sensor->MarkTeleported();
        fixture.Service.Evaluate(fixture.World, 0.0f);
        ENCOUNTER_CHECK(fixture.Count(EEventKind::Enter) == 1 && fixture.Count(EEventKind::Exit) == 1);
        ENCOUNTER_CHECK(fixture.Events[1].Kind == EEventKind::Exit);
        ENCOUNTER_CHECK(fixture.Events[1].Event.ZoneId == "teleport");
        ENCOUNTER_CHECK(fixture.Events[1].Event.EvaluationSequence == fixture.Events[0].Event.EvaluationSequence + 1);
        ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Outside);
    }

    // 撃破通知は一度だけで、領域への再進入や重複依頼でも再武装しない。
    void ClearedNeverRearms()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("cleared");
        fixture.AddSensor(0.0f);
        fixture.Sample(0.0f, 0.0f);
        ENCOUNTER_CHECK(fixture.Count(EEventKind::Enter) == 1);
        fixture.Service.MarkCleared("cleared");
        fixture.Service.Evaluate(fixture.World, 0.0f);
        ENCOUNTER_CHECK(fixture.Events.size() == 2);
        ENCOUNTER_CHECK(fixture.Events[1].Kind == EEventKind::Cleared);
        ENCOUNTER_CHECK(fixture.Events[1].Event.ZoneId == "cleared");
        fixture.Service.MarkCleared("cleared");
        for (unsigned i = 0; i < 6; ++i)
        {
            fixture.Sample(i % 2 == 0 ? 20.0f : 0.0f, 2.0f);
            ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Cleared);
            ENCOUNTER_CHECK(fixture.Service.GetZoneState("cleared") == EEncounterZoneState::Cleared);
            ENCOUNTER_CHECK(fixture.Events.size() == 2);
        }
        ENCOUNTER_CHECK(fixture.Count(EEventKind::Cleared) == 1 && fixture.Count(EEventKind::Enter) == 1);
    }

    // 優先領域から抜けると外側へ戻り、同じ ID の所有領域の交代は通知しない。
    void NestedAndSharedProfiles()
    {
        struct ProfileChange
        {
            String Old;
            String New;
        };
        Fixture fixture;
        auto* outer = fixture.AddZone("outer-zone", 10.0f);
        outer->getPriority() = 0;
        outer->getProfileId() = String("outer");
        auto* inner = fixture.AddZone("inner-zone", 3.0f);
        inner->getPriority() = 10;
        inner->getProfileId() = String("inner");
        fixture.AddSensor(20.0f);
        Array<ProfileChange> changes;
        fixture.Service.OnActiveProfileChanged.Add([&](const String& oldId, const String& newId, float)
        {
            changes.push_back({oldId, newId});
        });
        fixture.Sample(20.0f, 0.0f);
        ENCOUNTER_CHECK(changes.empty() && fixture.Service.GetActiveProfileId().empty());
        fixture.Sample(8.0f, 0.0f);
        ENCOUNTER_CHECK(changes.size() == 1 && fixture.Service.GetActiveProfileId() == "outer");
        fixture.Sample(0.0f, 0.0f);
        ENCOUNTER_CHECK(changes.size() == 2 && fixture.Service.GetActiveProfileId() == "inner");
        fixture.Sample(8.0f, 0.0f);
        fixture.Sample(8.0f, 1.1f);
        ENCOUNTER_CHECK(changes.size() == 3 && fixture.Service.GetActiveProfileId() == "outer");
        fixture.Sample(20.0f, 0.0f);
        fixture.Sample(20.0f, 1.1f);
        ENCOUNTER_CHECK(changes.size() == 4 && fixture.Service.GetActiveProfileId().empty());
        const char* expectedOld[] = {"", "outer", "inner", "outer"};
        const char* expectedNew[] = {"outer", "inner", "outer", ""};
        for (size_t i = 0; i < changes.size(); ++i)
        {
            ENCOUNTER_CHECK(changes[i].Old == expectedOld[i] && changes[i].New == expectedNew[i]);
        }

        Fixture shared;
        auto* first = shared.AddZone("shared-outer", 10.0f);
        first->getProfileId() = String("shared");
        auto* second = shared.AddZone("shared-inner", 3.0f);
        second->getProfileId() = String("shared");
        second->getPriority() = 10;
        shared.AddSensor(20.0f);
        unsigned sharedChanges = 0;
        shared.Service.OnActiveProfileChanged.Add([&](const String& oldId, const String& newId, float)
        {
            ENCOUNTER_CHECK(oldId.empty() && newId == "shared");
            ++sharedChanges;
        });
        shared.Sample(8.0f, 0.0f);
        shared.Sample(0.0f, 0.0f);
        shared.Sample(8.0f, 0.0f);
        shared.Sample(8.0f, 1.1f);
        ENCOUNTER_CHECK(shared.Count(EEventKind::Enter) == 2 && shared.Count(EEventKind::Exit) == 1);
        ENCOUNTER_CHECK(sharedChanges == 1 && shared.Service.GetActiveProfileId() == "shared");
    }

    // 同じ連続軌道を整数のフレーム番号から標本化し、刻み幅で通知列が変わらない。
    void EventSequenceIndependentOfStep()
    {
        const auto positionAt = [](double time)
        {
            const double times[] = {0.0, 0.5, 1.0, 1.25, 1.5, 1.75, 2.0, 2.1, 2.3, 2.4, 3.0, 3.5, 5.5, 6.0, 7.0};
            const float positions[] = {20, 0, 11.25f, 12.75f, 11.25f, 12.75f, 11.25f, 20, 20, 0, 0, 20, 20, 0, 0};
            for (size_t i = 1; i < sizeof(times) / sizeof(times[0]); ++i)
            {
                if (time <= times[i])
                {
                    const float alpha = float((time - times[i - 1]) / (times[i] - times[i - 1]));
                    return positions[i - 1] + (positions[i] - positions[i - 1]) * alpha;
                }
            }
            return 0.0f;
        };
        const auto run = [&](unsigned rate)
        {
            Fixture fixture;
            auto* zone = fixture.AddZone("trajectory");
            zone->getResetDelaySeconds() = 0.75f;
            fixture.AddSensor(positionAt(0.0));
            fixture.Sample(positionAt(0.0), 0.0f);
            for (unsigned frame = 1; frame <= 7 * rate; ++frame)
            {
                fixture.Sample(positionAt(double(frame) / rate), 1.0f / rate);
            }
            ENCOUNTER_CHECK(zone->GetState() == EEncounterZoneState::Engaged);
            return fixture.Events;
        };
        const auto slow = run(20);
        const auto fast = run(144);
        const EEventKind expected[] = {EEventKind::Enter, EEventKind::Exit, EEventKind::Reset, EEventKind::Enter};
        ENCOUNTER_CHECK(slow.size() == 4 && fast.size() == slow.size());
        for (size_t i = 0; i < slow.size(); ++i)
        {
            ENCOUNTER_CHECK(slow[i].Kind == expected[i] && fast[i].Kind == slow[i].Kind);
            ENCOUNTER_CHECK(slow[i].Event.ZoneId == "trajectory" && fast[i].Event.ZoneId == slow[i].Event.ZoneId);
        }
    }

    // 物理を公開し直しても、領域・センサーは proxy や物理クエリの対象に加わらない。
    void EncounterDoesNotChangePhysics()
    {
        auto engine = C::Container::MakeUnique<C::Engine::Engine>();
        auto& registry = C::Module::GetModuleRegistry();
        auto* physics = P::RegisterPhysicsModule(registry);
        ENCOUNTER_CHECK(physics != nullptr && registry.InstallAll(*engine));
        {
            Fixture fixture;
            C::Scene::ColliderHandle colliders[2];
            for (unsigned i = 0; i < 2; ++i)
            {
                auto* entity = fixture.World.SpawnEntity<C::Entity>();
                ENCOUNTER_CHECK(entity != nullptr);
                entity->SetLocalPosition(float(i) * 4.0f, 0.0f, 0.0f);
                auto* collider = fixture.World.CreateComponent<P::ColliderComponent>(entity);
                ENCOUNTER_CHECK(collider != nullptr && collider->SetSphere(1.0f) == P::EPhysicsResult::Success);
                colliders[i] = collider->GetColliderHandle();
            }
            physics->StepPreFixed(1.0f / 60.0f);
            physics->StepFixed(1.0f / 60.0f);
            const auto proxyCount = P::PhysicsModuleTestAccess::GetActiveColliderCount(*physics);
            ENCOUNTER_CHECK(proxyCount == 2);
            for (const auto handle : colliders)
            {
                P::PhysicsShapeProxy proxy;
                ENCOUNTER_CHECK(P::PhysicsModuleTestAccess::CopyPublishedProxy(*physics, handle, proxy));
            }
            auto& query = engine->GetSceneQuery();
            const M::Ray ray(M::Vector3(-5, 0, 0), M::Vector3::UnitX);
            const M::Sphere sphere(M::Vector3::Zero, 1000.0f);
            C::Scene::PhysicsRaycastHit beforeRay;
            Array<C::Scene::PhysicsOverlapHit> beforeOverlap;
            ENCOUNTER_CHECK(query.Raycast(ray, 30.0f, beforeRay) == C::Scene::EPhysicsSceneQueryResult::Success);
            ENCOUNTER_CHECK(beforeRay.Collider == colliders[0]);
            ENCOUNTER_CHECK(query.OverlapSphere(sphere, beforeOverlap) == C::Scene::EPhysicsSceneQueryResult::Success);
            ENCOUNTER_CHECK(beforeOverlap.size() == proxyCount);
            for (const auto handle : colliders)
            {
                unsigned matches = 0;
                for (const auto& hit : beforeOverlap)
                {
                    matches += hit.Collider == handle ? 1u : 0u;
                }
                ENCOUNTER_CHECK(matches == 1);
            }

            fixture.AddZone("physics-zone");
            fixture.AddSensor(20.0f);
            const float positions[] = {20, 0, 12, 20, 20, 0};
            for (float x : positions)
            {
                fixture.Sample(x, 1.1f);
                physics->StepPreFixed(1.0f / 60.0f);
                physics->StepFixed(1.0f / 60.0f);
                ENCOUNTER_CHECK(P::PhysicsModuleTestAccess::GetActiveColliderCount(*physics) == proxyCount);
                C::Scene::PhysicsRaycastHit afterRay;
                Array<C::Scene::PhysicsOverlapHit> afterOverlap;
                ENCOUNTER_CHECK(query.Raycast(ray, 30.0f, afterRay) == C::Scene::EPhysicsSceneQueryResult::Success);
                ENCOUNTER_CHECK(afterRay.Collider == beforeRay.Collider && afterRay.Entity == beforeRay.Entity);
                ENCOUNTER_CHECK(afterRay.Distance == beforeRay.Distance && afterRay.Point == beforeRay.Point &&
                                afterRay.Normal == beforeRay.Normal && afterRay.Body == beforeRay.Body);
                ENCOUNTER_CHECK(query.OverlapSphere(sphere, afterOverlap) == C::Scene::EPhysicsSceneQueryResult::Success);
                ENCOUNTER_CHECK(afterOverlap.size() == beforeOverlap.size());
                for (const auto& before : beforeOverlap)
                {
                    unsigned matches = 0;
                    for (const auto& after : afterOverlap)
                    {
                        if (after.Collider == before.Collider && after.Entity == before.Entity && after.Body == before.Body)
                        {
                            ++matches;
                        }
                    }
                    ENCOUNTER_CHECK(matches == 1);
                }
            }
            ENCOUNTER_CHECK(fixture.Count(EEventKind::Enter) == 2 && fixture.Count(EEventKind::Exit) == 1 &&
                            fixture.Count(EEventKind::Reset) == 1);
        }
        registry.ShutdownAll(*engine);
    }
} // 無名名前空間

int main()
{
    HysteresisBand();
    DelayedExitAndResetPolicy();
    TeleportExitsImmediately();
    ClearedNeverRearms();
    NestedAndSharedProfiles();
    EventSequenceIndependentOfStep();
    EncounterDoesNotChangePhysics();
    std::puts("EncounterZoneTest: PASS");
    return 0;
}
