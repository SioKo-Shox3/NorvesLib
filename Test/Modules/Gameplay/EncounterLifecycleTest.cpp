// 遭遇サービスの破棄、通知中の世代交代、再初期化、所有スレッドの境界を反証する。
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Object/World.h"
#include "Thread/Thread.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    namespace C = NorvesLib::Core;
    using namespace NorvesLib::Modules::Gameplay::World;
    using String = C::Container::String;

#define LIFECYCLE_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "EncounterLifecycle %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    struct Fixture
    {
        C::World World;
        NorvesLib::Modules::Gameplay::ProgressionState Progression;
        EncounterService Service{Progression};
        unsigned Entered = 0;
        unsigned Exited = 0;
        unsigned Reset = 0;
        unsigned Cleared = 0;
        uint64_t LastEnterSequence = 0;

        Fixture()
        {
            World.Initialize();
            Service.OnZoneEntered.Add([this](const EncounterZoneEvent& event)
            {
                ++Entered;
                LastEnterSequence = event.EvaluationSequence;
            });
            Service.OnZoneExited.Add([this](const EncounterZoneEvent&) { ++Exited; });
            Service.OnZoneReset.Add([this](const EncounterZoneEvent&) { ++Reset; });
            Service.OnZoneCleared.Add([this](const EncounterZoneEvent&) { ++Cleared; });
        }

        ~Fixture()
        {
            World.Finalize();
        }

        EncounterZoneComponent* AddZone(const char* id)
        {
            auto* entity = World.SpawnEntity<C::Entity>();
            LIFECYCLE_CHECK(entity != nullptr);
            auto* zone = World.CreateComponent<EncounterZoneComponent>(entity);
            LIFECYCLE_CHECK(zone != nullptr);
            zone->getZoneId() = String(id);
            zone->getRadius() = 10.0f;
            zone->getResetDelaySeconds() = 1.0f;
            zone->getProfileId() = String(id);
            Service.RequestRegisterZone(World.GetEntityHandle(*entity));
            return zone;
        }

        C::Entity* AddSensor(float x = 0.0f)
        {
            auto* entity = World.SpawnEntity<C::Entity>();
            LIFECYCLE_CHECK(entity != nullptr);
            entity->SetLocalPosition(x, 0.0f, 0.0f);
            LIFECYCLE_CHECK(World.CreateComponent<ZoneSensorComponent>(entity) != nullptr);
            Service.RequestRegisterSensor(World.GetEntityHandle(*entity));
            return entity;
        }

        void CollectDestroyed()
        {
            World.Tick(0.0f);
            World.LateTick(0.0f);
        }
    };

    void DestroyedZoneDoesNotReceiveEvents()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("destroyed");
        auto* sensor = fixture.AddSensor(30.0f);
        const auto handle = fixture.World.GetEntityHandle(*zone->GetOwner());
        fixture.Service.Evaluate(fixture.World, 0.0f);
        zone->GetOwner()->MarkForDestroy();
        sensor->SetLocalPosition(0.0f, 0.0f, 0.0f);
        fixture.Service.MarkCleared("destroyed");
        fixture.Service.Evaluate(fixture.World, 2.0f);
        fixture.CollectDestroyed();
        LIFECYCLE_CHECK(fixture.World.ResolveEntityHandle(handle) == nullptr);
        fixture.Service.Evaluate(fixture.World, 2.0f);
        LIFECYCLE_CHECK(fixture.Entered == 0 && fixture.Exited == 0 && fixture.Reset == 0 && fixture.Cleared == 0);
        LIFECYCLE_CHECK(fixture.Service.GetActiveProfileId().empty());
    }

    // センサーがゼロなら全て外側と扱い、最初の評価で離脱時計を開始する。
    void DestroyedSensorExitsAfterDelay()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("sensor-loss");
        auto* sensor = fixture.AddSensor();
        const auto handle = fixture.World.GetEntityHandle(*sensor);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(fixture.Entered == 1 && zone->GetState() == EEncounterZoneState::Engaged);
        sensor->MarkForDestroy();
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Resetting);
        fixture.CollectDestroyed();
        LIFECYCLE_CHECK(fixture.World.ResolveEntityHandle(handle) == nullptr);
        fixture.Service.Evaluate(fixture.World, 0.5f);
        LIFECYCLE_CHECK(fixture.Exited == 0 && fixture.Reset == 0);
        fixture.Service.Evaluate(fixture.World, 0.5f);
        LIFECYCLE_CHECK(fixture.Exited == 1 && fixture.Reset == 1);
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Outside);
        LIFECYCLE_CHECK(fixture.Service.GetActiveProfileId().empty());
        fixture.Service.Evaluate(fixture.World, 5.0f);
        LIFECYCLE_CHECK(fixture.Entered == 1 && fixture.Exited == 1 && fixture.Reset == 1 && fixture.Cleared == 0);
    }

    void DestroyDuringDeliveryRevalidatesHandles()
    {
        Fixture fixture;
        auto* first = fixture.AddZone("first")->GetOwner();
        auto* second = fixture.AddZone("second")->GetOwner();
        fixture.AddZone("survivor");
        fixture.AddSensor();
        const auto firstHandle = fixture.World.GetEntityHandle(*first);
        const auto secondHandle = fixture.World.GetEntityHandle(*second);
        C::EntityHandle replacementHandle;
        unsigned delivered = 0;
        fixture.Service.OnZoneEntered.Add([&](const EncounterZoneEvent& event)
        {
            auto* entity = fixture.World.ResolveEntityHandle(event.ZoneEntity);
            LIFECYCLE_CHECK(entity != nullptr && !entity->IsPendingDestroy());
            ++delivered;
            LIFECYCLE_CHECK(event.ZoneId != "second");
            if (event.ZoneId == "first")
            {
                first->MarkForDestroy();
                second->MarkForDestroy();
                fixture.CollectDestroyed();
                LIFECYCLE_CHECK(fixture.World.ResolveEntityHandle(firstHandle) == nullptr);
                LIFECYCLE_CHECK(fixture.World.ResolveEntityHandle(secondHandle) == nullptr);
                auto* replacement = fixture.AddZone("replacement");
                replacementHandle = fixture.World.GetEntityHandle(*replacement->GetOwner());
                LIFECYCLE_CHECK(replacementHandle != firstHandle && replacementHandle != secondHandle);
                const auto recycled = replacementHandle.SlotIndex == firstHandle.SlotIndex ? firstHandle : secondHandle;
                LIFECYCLE_CHECK(replacementHandle.SlotIndex == recycled.SlotIndex &&
                                replacementHandle.Generation != recycled.Generation && replacementHandle.WorldId == recycled.WorldId);
            }
        });
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(fixture.Entered == 2 && delivered == 2);
        LIFECYCLE_CHECK(replacementHandle.IsValid());
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(fixture.Entered == 3 && delivered == 3);
        LIFECYCLE_CHECK(fixture.Service.GetZoneState("replacement") == EEncounterZoneState::Engaged);
        LIFECYCLE_CHECK(fixture.Exited == 0 && fixture.Reset == 0 && fixture.Cleared == 0);
    }

    // GameplayModule::Shutdown と同じ Reset 経路で、購読と Component を残して拾い直す。
    void ResetClearsRuntimeAndRescansComponents()
    {
        Fixture fixture;
        auto* cleared = fixture.AddZone("cleared");
        auto* active = fixture.AddZone("active");
        auto* sensor = fixture.AddSensor();
        fixture.Service.Evaluate(fixture.World, 0.0f);
        fixture.Service.MarkCleared("cleared");
        fixture.Service.MarkCleared("remembered");
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(cleared->GetState() == EEncounterZoneState::Cleared && fixture.Cleared == 1);
        LIFECYCLE_CHECK(fixture.Service.GetZoneState("remembered") == EEncounterZoneState::Cleared);
        LIFECYCLE_CHECK(fixture.Service.GetActiveProfileId() == "active");
        fixture.Service.MarkCleared("active");
        fixture.Service.Reset();
        LIFECYCLE_CHECK(fixture.Service.GetZoneState("cleared") == EEncounterZoneState::Outside);
        LIFECYCLE_CHECK(fixture.Service.GetZoneState("active") == EEncounterZoneState::Outside);
        LIFECYCLE_CHECK(fixture.Service.GetZoneState("remembered") == EEncounterZoneState::Outside);
        LIFECYCLE_CHECK(fixture.Service.GetActiveProfileId().empty());
        sensor->SetLocalPosition(30.0f, 0.0f, 0.0f);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(cleared->GetState() == EEncounterZoneState::Outside && active->GetState() == EEncounterZoneState::Outside);
        LIFECYCLE_CHECK(fixture.Entered == 2 && fixture.Cleared == 1 && fixture.Exited == 0 && fixture.Reset == 0);
        sensor->SetLocalPosition(0.0f, 0.0f, 0.0f);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(fixture.Entered == 4 && fixture.LastEnterSequence == 2);
        LIFECYCLE_CHECK(cleared->GetState() == EEncounterZoneState::Engaged && active->GetState() == EEncounterZoneState::Engaged);
        LIFECYCLE_CHECK(fixture.Service.GetActiveProfileId() == "active");
        fixture.Service.MarkCleared("cleared");
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(fixture.Cleared == 2);
    }

    void ForeignThreadCannotEvaluateOrClear()
    {
        Fixture fixture;
        auto* zone = fixture.AddZone("owner-thread");
        auto* sensor = fixture.AddSensor(30.0f);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        sensor->SetLocalPosition(0.0f, 0.0f, 0.0f);
        NorvesLib::Thread::Thread enterWorker([&]()
        {
            fixture.Service.Evaluate(fixture.World, 5.0f);
            fixture.Service.MarkCleared("owner-thread");
        });
        enterWorker.Join();
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Outside && fixture.Entered == 0 && fixture.Cleared == 0);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Engaged && fixture.Entered == 1 && fixture.LastEnterSequence == 2);
        sensor->SetLocalPosition(30.0f, 0.0f, 0.0f);
        NorvesLib::Thread::Thread exitWorker([&]()
        {
            fixture.Service.Evaluate(fixture.World, 5.0f);
            fixture.Service.MarkCleared("owner-thread");
        });
        exitWorker.Join();
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Engaged && fixture.Exited == 0 && fixture.Cleared == 0);
        fixture.Service.Evaluate(fixture.World, 0.0f);
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Resetting);
        fixture.Service.Evaluate(fixture.World, 1.0f);
        LIFECYCLE_CHECK(zone->GetState() == EEncounterZoneState::Outside && fixture.Exited == 1 && fixture.Reset == 1 && fixture.Cleared == 0);
    }
} // 無名名前空間

int main()
{
    DestroyedZoneDoesNotReceiveEvents();
    DestroyedSensorExitsAfterDelay();
    DestroyDuringDeliveryRevalidatesHandles();
    ResetClearsRuntimeAndRescansComponents();
    ForeignThreadCannotEvaluateOrClear();
    std::puts("EncounterLifecycleTest: PASS");
    return 0;
}
