// 遭遇領域とセンサーの全設定の保存復元と、復元後の遅延登録を反証する。
#include "Gameplay/IGameplayModule.h"
#include "Gameplay/Progression/ProgressionState.h"
#include "Gameplay/World/EncounterService.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/PrefabAsset.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace G = NorvesLib::Modules::Gameplay;
    using namespace G::World;
    using String = C::Container::String;
    constexpr const char* kSceneFilePath = "EncounterSerializationTest.scene.json";

#define SERIALIZATION_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "EncounterSerialization %d: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= 0.0001f;
    }

    void ExpectVectorNear(const M::Vector3& lhs, const M::Vector3& rhs)
    {
        SERIALIZATION_CHECK(Near(lhs.x, rhs.x) && Near(lhs.y, rhs.y) && Near(lhs.z, rhs.z));
    }

    EncounterZoneComponent* AddZone(C::World& world, const char* id, EEncounterZoneShape shape, float x)
    {
        auto* entity = world.SpawnEntity<C::Entity>();
        SERIALIZATION_CHECK(entity != nullptr);
        entity->SetLocalPosition(x, 2.0f, 3.0f);
        auto* zone = world.CreateComponent<EncounterZoneComponent>(entity);
        SERIALIZATION_CHECK(zone != nullptr);
        zone->getZoneId() = String(id);
        zone->getShape() = shape;
        zone->getHalfExtents() = M::Vector3(4.5f, 5.25f, 6.75f);
        zone->getRadius() = 7.5f;
        zone->getHalfHeight() = 8.25f;
        zone->getEnterMargin() = 0.25f;
        zone->getExitMargin() = 0.75f;
        zone->getResetDelaySeconds() = 2.5f;
        zone->getResetPolicy() = EEncounterResetPolicy::KeepDamage;
        zone->getEnemyId() = String("shisha.round_trip");
        zone->getProfileId() = String("profile.round_trip");
        zone->getProfileBlendSeconds() = 0.625f;
        zone->getbRequiresProgression() = true;
        zone->getPriority() = 17;
        zone->getSensorMask() = 0x24u;
        return zone;
    }

    void ExpectZoneProperties(EncounterZoneComponent& expected, EncounterZoneComponent& actual)
    {
        SERIALIZATION_CHECK(String(actual.getZoneId()) == String(expected.getZoneId()));
        SERIALIZATION_CHECK(actual.getShape() == expected.getShape());
        ExpectVectorNear(actual.getHalfExtents(), expected.getHalfExtents());
        SERIALIZATION_CHECK(Near(actual.getRadius(), expected.getRadius()));
        SERIALIZATION_CHECK(Near(actual.getHalfHeight(), expected.getHalfHeight()));
        SERIALIZATION_CHECK(Near(actual.getEnterMargin(), expected.getEnterMargin()));
        SERIALIZATION_CHECK(Near(actual.getExitMargin(), expected.getExitMargin()));
        SERIALIZATION_CHECK(Near(actual.getResetDelaySeconds(), expected.getResetDelaySeconds()));
        SERIALIZATION_CHECK(actual.getResetPolicy() == expected.getResetPolicy());
        SERIALIZATION_CHECK(String(actual.getEnemyId()) == String(expected.getEnemyId()));
        SERIALIZATION_CHECK(String(actual.getProfileId()) == String(expected.getProfileId()));
        SERIALIZATION_CHECK(Near(actual.getProfileBlendSeconds(), expected.getProfileBlendSeconds()));
        SERIALIZATION_CHECK(actual.getbRequiresProgression() == expected.getbRequiresProgression());
        SERIALIZATION_CHECK(actual.getPriority() == expected.getPriority());
        SERIALIZATION_CHECK(actual.getSensorMask() == expected.getSensorMask());
    }

    void SceneRoundTripAndDeferredRegistration()
    {
        (void)C::Entity::StaticClass();
        (void)C::Component::Component::StaticClass();
        (void)C::PrefabAsset::StaticClass();
        auto engine = C::Container::MakeUnique<C::Engine::Engine>();
        auto& registry = C::Module::GetModuleRegistry();
        auto* module = G::RegisterGameplayModule(registry);
        SERIALIZATION_CHECK(module != nullptr && registry.InstallAll(*engine));
        {
            C::World sourceWorld;
            sourceWorld.Initialize();
            auto* box = AddZone(sourceWorld, "zone.box", EEncounterZoneShape::Box, 0.0f);
            auto* cylinder = AddZone(sourceWorld, "zone.cylinder", EEncounterZoneShape::CylinderY, 40.0f);
            auto* sensorEntity = sourceWorld.SpawnEntity<C::Entity>();
            SERIALIZATION_CHECK(sensorEntity != nullptr);
            sensorEntity->SetLocalPosition(100.0f, 0.0f, 0.0f);
            auto* sensor = sourceWorld.CreateComponent<ZoneSensorComponent>(sensorEntity);
            SERIALIZATION_CHECK(sensor != nullptr);
            sensor->getOffset() = M::Vector3(0.5f, 1.25f, -0.75f);
            sensor->getKindBits() = 0x20u;
            SERIALIZATION_CHECK(C::Scene::SceneSerializer::SaveToFile(sourceWorld, String(kSceneFilePath)));

            C::World loadedWorld;
            loadedWorld.Initialize();
            auto& service = module->GetEncounterService();
            service.Reset();
            // 空の World を評価済みにし、初回走査だけでは復元を拾えない条件を作る。
            service.Evaluate(loadedWorld, 0.0f);
            C::Scene::SceneLoadStats stats;
            SERIALIZATION_CHECK(C::Scene::SceneSerializer::LoadIntoWorld(loadedWorld, String(kSceneFilePath), &stats));
            SERIALIZATION_CHECK(stats.LoadedRoots == 3 && stats.DroppedEntities == 0 &&
                                stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
            const auto roots = loadedWorld.GetRootEntities();
            SERIALIZATION_CHECK(roots.size() == 3);
            EncounterZoneComponent* loadedBox = nullptr;
            EncounterZoneComponent* loadedCylinder = nullptr;
            ZoneSensorComponent* loadedSensor = nullptr;
            for (auto* entity : roots)
            {
                if (auto* zone = entity->GetComponent<EncounterZoneComponent>())
                {
                    if (String(zone->getZoneId()) == "zone.box")
                    {
                        SERIALIZATION_CHECK(loadedBox == nullptr);
                        loadedBox = zone;
                    }
                    else if (String(zone->getZoneId()) == "zone.cylinder")
                    {
                        SERIALIZATION_CHECK(loadedCylinder == nullptr);
                        loadedCylinder = zone;
                    }
                }
                if (auto* candidate = entity->GetComponent<ZoneSensorComponent>())
                {
                    SERIALIZATION_CHECK(loadedSensor == nullptr);
                    loadedSensor = candidate;
                }
            }
            SERIALIZATION_CHECK(loadedBox != nullptr && loadedCylinder != nullptr && loadedSensor != nullptr);
            ExpectZoneProperties(*box, *loadedBox);
            ExpectZoneProperties(*cylinder, *loadedCylinder);
            ExpectVectorNear(loadedSensor->getOffset(), sensor->getOffset());
            SERIALIZATION_CHECK(loadedSensor->getKindBits() == sensor->getKindBits());
            ExpectVectorNear(loadedBox->GetOwner()->GetLocalTransform().position, box->GetOwner()->GetLocalTransform().position);
            ExpectVectorNear(loadedCylinder->GetOwner()->GetLocalTransform().position, cylinder->GetOwner()->GetLocalTransform().position);
            ExpectVectorNear(loadedSensor->GetOwner()->GetLocalTransform().position, sensorEntity->GetLocalTransform().position);

            auto& progression = module->GetProgressionState();
            SERIALIZATION_CHECK(progression.LoadDefinitionFromJson(
                R"({"version":1,"finalShishaId":"shisha.final","requiredShishaIds":["shisha.round_trip"]})"));
            progression.MarkDefeated("shisha.round_trip");
            SERIALIZATION_CHECK(progression.IsFinalUnlocked());
            unsigned boxEntered = 0;
            unsigned cylinderEntered = 0;
            service.OnZoneEntered.Add([&](const EncounterZoneEvent& event)
            {
                SERIALIZATION_CHECK(event.ZoneEntity.WorldId == loadedWorld.GetEntityHandle(*loadedSensor->GetOwner()).WorldId);
                SERIALIZATION_CHECK(event.ResetPolicy == EEncounterResetPolicy::KeepDamage);
                if (event.ZoneId == "zone.box")
                {
                    ++boxEntered;
                    SERIALIZATION_CHECK(event.ZoneEntity == loadedWorld.GetEntityHandle(*loadedBox->GetOwner()));
                }
                else
                {
                    SERIALIZATION_CHECK(event.ZoneId == "zone.cylinder");
                    ++cylinderEntered;
                    SERIALIZATION_CHECK(event.ZoneEntity == loadedWorld.GetEntityHandle(*loadedCylinder->GetOwner()));
                }
            });
            // RequestRegister は呼ばず、復元された Component の経路だけで参加させる。
            service.Evaluate(loadedWorld, 0.0f);
            SERIALIZATION_CHECK(boxEntered == 0 && cylinderEntered == 0);
            loadedSensor->GetOwner()->SetLocalPosition(0.0f, 2.0f, 3.0f);
            service.Evaluate(loadedWorld, 0.0f);
            SERIALIZATION_CHECK(boxEntered == 1 && cylinderEntered == 0);
            SERIALIZATION_CHECK(loadedBox->GetState() == EEncounterZoneState::Engaged);
            loadedSensor->GetOwner()->SetLocalPosition(40.0f, 2.0f, 3.0f);
            service.Evaluate(loadedWorld, 0.0f);
            SERIALIZATION_CHECK(boxEntered == 1 && cylinderEntered == 1);
            SERIALIZATION_CHECK(loadedCylinder->GetState() == EEncounterZoneState::Engaged);
            service.OnZoneEntered.Clear();
            loadedWorld.Finalize();
            sourceWorld.Finalize();
            SERIALIZATION_CHECK(std::remove(kSceneFilePath) == 0);
        }
        registry.ShutdownAll(*engine);
    }
} // 無名名前空間

int main()
{
    SceneRoundTripAndDeferredRegistration();
    std::puts("EncounterSerializationTest: PASS");
    return 0;
}
