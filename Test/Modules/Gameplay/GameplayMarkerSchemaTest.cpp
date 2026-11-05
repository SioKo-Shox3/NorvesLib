// レベル配置マーカーの登録、スキーマ、検証と保存復元の契約を反証する。
#include "Gameplay/Level/AnimalSpawnPointComponent.h"
#include "Gameplay/Level/GameplayComponentClasses.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/LevelMarkerValidation.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Object/PrefabAsset.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace L = NorvesLib::Modules::Gameplay::Level;
    namespace GWorld = NorvesLib::Modules::Gameplay::World;
    using String = C::Container::String;

    constexpr const char* kHandwrittenPath = "GameplayMarkerSchemaTest.handwritten.scene.json";
    constexpr const char* kRoundTripPath = "GameplayMarkerSchemaTest.roundtrip.scene.json";

#define MARKER_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "GameplayMarkerSchemaTest: %d 行目の検査に失敗: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= 0.0001f;
    }

    void ExpectVector(const M::Vector3& actual, const M::Vector3& expected, const char* context)
    {
        if (!Near(actual.x, expected.x) || !Near(actual.y, expected.y) || !Near(actual.z, expected.z))
        {
            std::fprintf(stderr, "位置が一致しません（%s）: 実際=(%g,%g,%g), 期待=(%g,%g,%g)\n",
                         context, actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
            std::abort();
        }
    }

    void ReadBeforeRegistration()
    {
        // マーカーの型には触れず、保存形式の文書だけを作る。
        constexpr const char* json = R"json({
            "formatVersion": 3, "module": "NorvesLib", "assets": [], "roots": [{
                "formatVersion": 3, "rootAlias": "1", "rootPath": "", "root": {
                    "alias": "1", "parentAlias": "0",
                    "object": {"class": "Entity", "classId": "0", "path": "", "properties": []},
                    "components": [{"alias": "2", "ownerAlias": "1", "object": {
                        "class": "LevelMarkerComponent", "classId": "0", "path": "", "properties": [
                            {"name": "Key", "propertyId": "0", "type": "String", "typeId": "0", "value": "marker.handwritten"},
                            {"name": "Tag", "propertyId": "0", "type": "String", "typeId": "0", "value": "rest.tag"},
                            {"name": "bSnapToGround", "propertyId": "0", "type": "bool", "typeId": "0", "value": "0"},
                            {"name": "GroundOffset", "propertyId": "0", "type": "float", "typeId": "0", "value": "1.25"}
                        ]
                    }}, {"alias": "3", "ownerAlias": "1", "object": {
                        "class": "RestPointComponent", "classId": "0", "path": "", "properties": [
                            {"name": "PointId", "propertyId": "0", "type": "String", "typeId": "0", "value": "rest.handwritten"},
                            {"name": "NameKey", "propertyId": "0", "type": "String", "typeId": "0", "value": "rest.title"},
                            {"name": "bFastTravel", "propertyId": "0", "type": "bool", "typeId": "0", "value": "0"},
                            {"name": "bSavePoint", "propertyId": "0", "type": "bool", "typeId": "0", "value": "1"},
                            {"name": "bAdvancesTime", "propertyId": "0", "type": "bool", "typeId": "0", "value": "1"},
                            {"name": "ArrivalYaw", "propertyId": "0", "type": "float", "typeId": "0", "value": "75.5"}
                        ]
                    }}], "children": []
                }
            }]
        })json";
        std::FILE* file = nullptr;
        MARKER_CHECK(fopen_s(&file, kHandwrittenPath, "wb") == 0 && file != nullptr);
        const size_t length = std::strlen(json);
        MARKER_CHECK(std::fwrite(json, 1, length, file) == length);
        MARKER_CHECK(std::fclose(file) == 0);

        (void)C::Entity::StaticClass();
        (void)C::Component::Component::StaticClass();
        (void)C::PrefabAsset::StaticClass();
        const char* markerClasses[] = {
            "LevelMarkerComponent", "TerritoryMarkerComponent", "RestPointComponent", "PoiMarkerComponent",
            "AnimalSpawnPointComponent", "ScentTrailComponent", "ScentTrailPointComponent"};
        for (const char* name : markerClasses)
        {
            MARKER_CHECK(C::ClassRegistry::Get().FindClass(C::Identity(name)) == nullptr);
        }
        {
            C::World unknownWorld;
            unknownWorld.Initialize();
            C::Scene::SceneLoadStats stats;
            MARKER_CHECK(C::Scene::SceneSerializer::LoadIntoWorld(unknownWorld, String(kHandwrittenPath), &stats));
            MARKER_CHECK(stats.LoadedRoots == 1 && stats.DroppedEntities == 0 && stats.DroppedComponents == 2 &&
                         stats.DroppedProperties == 0);
            unknownWorld.Finalize();
        }

        L::RegisterGameplayComponentClasses();
        C::World loadedWorld;
        loadedWorld.Initialize();
        C::Scene::SceneLoadStats stats;
        MARKER_CHECK(C::Scene::SceneSerializer::LoadIntoWorld(loadedWorld, String(kHandwrittenPath), &stats));
        MARKER_CHECK(stats.LoadedRoots == 1 && stats.DroppedEntities == 0 && stats.DroppedComponents == 0 &&
                     stats.DroppedProperties == 0);
        const auto roots = loadedWorld.GetRootEntities();
        MARKER_CHECK(roots.size() == 1);
        const auto* marker = roots[0]->GetComponent<L::LevelMarkerComponent>();
        const auto* rest = roots[0]->GetComponent<L::RestPointComponent>();
        MARKER_CHECK(marker != nullptr && rest != nullptr);
        MARKER_CHECK(String(marker->getKey()) == "marker.handwritten");
        MARKER_CHECK(String(marker->getTag()) == "rest.tag");
        MARKER_CHECK(!marker->getbSnapToGround() && Near(marker->getGroundOffset(), 1.25f));
        MARKER_CHECK(String(rest->getPointId()) == "rest.handwritten");
        MARKER_CHECK(String(rest->getNameKey()) == "rest.title");
        MARKER_CHECK(!rest->getbFastTravel() && rest->getbSavePoint() && rest->getbAdvancesTime());
        MARKER_CHECK(Near(rest->getArrivalYaw(), 75.5f));
        loadedWorld.Finalize();
        MARKER_CHECK(std::remove(kHandwrittenPath) == 0);
    }

    void CheckSchema(const C::IClass& cls, const char* const* expectedNames, size_t expectedCount)
    {
        auto& registry = C::TypeRegistry::Get();
        const C::TypeId boolType = registry.GetTypeId<bool>();
        const C::TypeId uintType = registry.GetTypeId<uint32_t>();
        const C::TypeId floatType = registry.GetTypeId<float>();
        const C::TypeId stringType = registry.GetTypeId<String>();
        const C::TypeId vectorType = registry.GetTypeId<M::Vector3>();
        const C::ClassSchemaProjection schema = C::RuntimeSchemaProjector::ProjectClass(cls);
        const C::IClass* parent = cls.GetParentClass();
        MARKER_CHECK(parent != nullptr);
        size_t ownPropertyCount = 0;
        for (const auto& property : schema.Properties)
        {
            const C::Identity name(property.Name);
            const C::ClassProperty* inherited = parent->GetProperty(name);
            if (inherited != nullptr && inherited == cls.GetProperty(name))
            {
                continue;
            }
            ++ownPropertyCount;
            const C::TypeInfo* type = registry.FindStable(property.Type);
            MARKER_CHECK(type != nullptr);
            const bool bAllowed =
                (type->Kind == C::TypeKind::Bool && type->Id == boolType) ||
                (type->Kind == C::TypeKind::Integer && type->Id == uintType) ||
                (type->Kind == C::TypeKind::Float && type->Id == floatType) ||
                (type->Kind == C::TypeKind::String && type->Id == stringType) ||
                (type->Kind == C::TypeKind::Struct && type->Id == vectorType);
            MARKER_CHECK(bAllowed);
        }
        MARKER_CHECK(ownPropertyCount != 0 && ownPropertyCount == expectedCount);
        for (size_t index = 0; index < expectedCount; ++index)
        {
            bool bFound = false;
            for (const auto& property : schema.Properties)
            {
                if (property.Name == expectedNames[index])
                {
                    const C::Identity name(property.Name);
                    MARKER_CHECK(parent->GetProperty(name) != cls.GetProperty(name));
                    MARKER_CHECK(!bFound);
                    bFound = true;
                }
            }
            MARKER_CHECK(bFound);
        }
    }

    template<size_t N>
    void CheckSchema(const C::IClass& cls, const char* const (&expectedNames)[N])
    {
        CheckSchema(cls, expectedNames, N);
    }

    void CheckAllSchemas()
    {
        const char* marker[] = {"Key", "Tag", "bSnapToGround", "GroundOffset"};
        const char* territory[] = {"OwnerKey", "EngageRadius"};
        const char* rest[] = {"PointId", "NameKey", "bFastTravel", "bSavePoint", "bAdvancesTime", "ArrivalYaw"};
        const char* poi[] = {"PoiId", "Category", "NameKey", "bShownOnMap", "DiscoverRadius"};
        const char* animal[] = {"SpeciesId", "Count", "Radius", "BehaviorId"};
        const char* trail[] = {"TargetKey", "Width"};
        const char* point[] = {"Intensity"};
        CheckSchema(*L::LevelMarkerComponent::StaticClass(), marker);
        CheckSchema(*L::TerritoryMarkerComponent::StaticClass(), territory);
        CheckSchema(*L::RestPointComponent::StaticClass(), rest);
        CheckSchema(*L::PoiMarkerComponent::StaticClass(), poi);
        CheckSchema(*L::AnimalSpawnPointComponent::StaticClass(), animal);
        CheckSchema(*L::ScentTrailComponent::StaticClass(), trail);
        CheckSchema(*L::ScentTrailPointComponent::StaticClass(), point);
    }

    L::LevelMarkerComponent* AddMarker(C::World& world, const char* key)
    {
        auto* entity = world.SpawnEntity<C::Entity>();
        MARKER_CHECK(entity != nullptr);
        auto* marker = world.CreateComponent<L::LevelMarkerComponent>(entity);
        MARKER_CHECK(marker != nullptr);
        marker->getKey() = String(key);
        return marker;
    }

    bool HasIssue(const C::Container::VariableArray<L::LevelMarkerIssue>& issues,
                  L::ELevelMarkerIssue kind, const char* key)
    {
        for (const auto& issue : issues)
        {
            if (issue.Kind == kind && issue.Key == key && issue.Target != nullptr)
            {
                return true;
            }
        }
        return false;
    }

    void CheckValidation()
    {
        C::Container::VariableArray<L::LevelMarkerIssue> issues;
        {
            C::World world;
            world.Initialize();
            AddMarker(world, "");
            MARKER_CHECK(!L::ValidateLevelMarkers(world, issues));
            MARKER_CHECK(issues.size() == 1 && HasIssue(issues, L::ELevelMarkerIssue::EmptyKey, ""));
            world.Finalize();
        }
        {
            C::World world;
            world.Initialize();
            AddMarker(world, "marker.duplicate");
            AddMarker(world, "marker.duplicate");
            MARKER_CHECK(!L::ValidateLevelMarkers(world, issues));
            MARKER_CHECK(issues.size() == 1 && HasIssue(issues, L::ELevelMarkerIssue::DuplicateKey, "marker.duplicate"));
            world.Finalize();
        }
        {
            C::World world;
            world.Initialize();
            auto* territoryMarker = AddMarker(world, "marker.territory");
            auto* territory = world.CreateComponent<L::TerritoryMarkerComponent>(territoryMarker->GetOwner());
            auto* zone = world.CreateComponent<GWorld::EncounterZoneComponent>(territoryMarker->GetOwner());
            auto* restMarker = AddMarker(world, "marker.rest");
            auto* rest = world.CreateComponent<L::RestPointComponent>(restMarker->GetOwner());
            MARKER_CHECK(territory != nullptr && zone != nullptr && rest != nullptr);
            MARKER_CHECK(L::ValidateLevelMarkers(world, issues) && issues.empty());
            world.Finalize();
        }
    }

    void CheckRoundTrip()
    {
        const M::Vector3 positions[] = {
            M::Vector3(1.0f, 2.0f, 3.0f), M::Vector3(-4.0f, 5.5f, 6.0f), M::Vector3(7.0f, -8.0f, 9.25f)};
        const float intensities[] = {0.25f, 0.5f, 0.875f};
        C::World sourceWorld;
        sourceWorld.Initialize();
        auto* marker = AddMarker(sourceWorld, "marker.roundtrip");
        C::Entity* root = marker->GetOwner();
        root->SetLocalPosition(10.0f, 20.0f, -5.0f);
        marker->getTag() = String("trail.tag");
        marker->getbSnapToGround() = false;
        marker->getGroundOffset() = 1.375f;
        auto* territory = sourceWorld.CreateComponent<L::TerritoryMarkerComponent>(root);
        auto* zone = sourceWorld.CreateComponent<GWorld::EncounterZoneComponent>(root);
        auto* rest = sourceWorld.CreateComponent<L::RestPointComponent>(root);
        auto* poi = sourceWorld.CreateComponent<L::PoiMarkerComponent>(root);
        auto* animal = sourceWorld.CreateComponent<L::AnimalSpawnPointComponent>(root);
        auto* trail = sourceWorld.CreateComponent<L::ScentTrailComponent>(root);
        MARKER_CHECK(territory != nullptr && zone != nullptr && rest != nullptr && poi != nullptr &&
                     animal != nullptr && trail != nullptr);
        territory->getOwnerKey() = String("owner.wolf");
        territory->getEngageRadius() = 27.5f;
        rest->getPointId() = String("rest.alpha");
        rest->getNameKey() = String("rest.name");
        rest->getbFastTravel() = false;
        rest->getbSavePoint() = true;
        rest->getbAdvancesTime() = true;
        rest->getArrivalYaw() = 42.5f;
        poi->getPoiId() = String("poi.alpha");
        poi->getCategory() = String("landmark");
        poi->getNameKey() = String("poi.name");
        poi->getbShownOnMap() = false;
        poi->getDiscoverRadius() = 31.25f;
        animal->getSpeciesId() = String("wolf");
        animal->getCount() = 7u;
        animal->getRadius() = 12.75f;
        animal->getBehaviorId() = String("behavior.roam");
        trail->getTargetKey() = String("target.wolf");
        trail->getWidth() = 3.75f;
        for (size_t index = 0; index < 3; ++index)
        {
            C::Entity* child = sourceWorld.SpawnEntity<C::Entity>(root);
            MARKER_CHECK(child != nullptr);
            child->SetLocalPosition(positions[index]);
            auto* point = sourceWorld.CreateComponent<L::ScentTrailPointComponent>(child);
            MARKER_CHECK(point != nullptr);
            point->getIntensity() = intensities[index];
        }
        C::Container::VariableArray<L::LevelMarkerIssue> issues;
        MARKER_CHECK(L::ValidateLevelMarkers(sourceWorld, issues) && issues.empty());
        C::Scene::SceneSaveStats saveStats;
        MARKER_CHECK(C::Scene::SceneSerializer::SaveToFile(sourceWorld, String(kRoundTripPath), &saveStats));
        MARKER_CHECK(saveStats.SavedRoots == 1 && saveStats.DroppedProperties == 0);

        C::World loadedWorld;
        loadedWorld.Initialize();
        C::Scene::SceneLoadStats loadStats;
        MARKER_CHECK(C::Scene::SceneSerializer::LoadIntoWorld(loadedWorld, String(kRoundTripPath), &loadStats));
        MARKER_CHECK(loadStats.LoadedRoots == 1 && loadStats.DroppedEntities == 0 &&
                     loadStats.DroppedComponents == 0 && loadStats.DroppedProperties == 0);
        const auto roots = loadedWorld.GetRootEntities();
        MARKER_CHECK(roots.size() == 1);
        C::Entity* loadedRoot = roots[0];
        ExpectVector(loadedRoot->GetLocalTransform().position, M::Vector3(10.0f, 20.0f, -5.0f), "ルート");
        const auto* loadedMarker = loadedRoot->GetComponent<L::LevelMarkerComponent>();
        const auto* loadedTerritory = loadedRoot->GetComponent<L::TerritoryMarkerComponent>();
        const auto* loadedZone = loadedRoot->GetComponent<GWorld::EncounterZoneComponent>();
        const auto* loadedRest = loadedRoot->GetComponent<L::RestPointComponent>();
        const auto* loadedPoi = loadedRoot->GetComponent<L::PoiMarkerComponent>();
        const auto* loadedAnimal = loadedRoot->GetComponent<L::AnimalSpawnPointComponent>();
        const auto* loadedTrail = loadedRoot->GetComponent<L::ScentTrailComponent>();
        MARKER_CHECK(loadedMarker != nullptr && loadedTerritory != nullptr && loadedZone != nullptr &&
                     loadedRest != nullptr && loadedPoi != nullptr && loadedAnimal != nullptr && loadedTrail != nullptr);
        MARKER_CHECK(String(loadedMarker->getKey()) == "marker.roundtrip");
        MARKER_CHECK(String(loadedMarker->getTag()) == "trail.tag");
        MARKER_CHECK(!loadedMarker->getbSnapToGround() && Near(loadedMarker->getGroundOffset(), 1.375f));
        MARKER_CHECK(String(loadedTerritory->getOwnerKey()) == "owner.wolf");
        MARKER_CHECK(Near(loadedTerritory->getEngageRadius(), 27.5f));
        MARKER_CHECK(String(loadedRest->getPointId()) == "rest.alpha");
        MARKER_CHECK(String(loadedRest->getNameKey()) == "rest.name");
        MARKER_CHECK(!loadedRest->getbFastTravel() && loadedRest->getbSavePoint() && loadedRest->getbAdvancesTime());
        MARKER_CHECK(Near(loadedRest->getArrivalYaw(), 42.5f));
        MARKER_CHECK(String(loadedPoi->getPoiId()) == "poi.alpha");
        MARKER_CHECK(String(loadedPoi->getCategory()) == "landmark");
        MARKER_CHECK(String(loadedPoi->getNameKey()) == "poi.name");
        MARKER_CHECK(!loadedPoi->getbShownOnMap() && Near(loadedPoi->getDiscoverRadius(), 31.25f));
        MARKER_CHECK(String(loadedAnimal->getSpeciesId()) == "wolf" && loadedAnimal->getCount() == 7u);
        MARKER_CHECK(Near(loadedAnimal->getRadius(), 12.75f));
        MARKER_CHECK(String(loadedAnimal->getBehaviorId()) == "behavior.roam");
        MARKER_CHECK(String(loadedTrail->getTargetKey()) == "target.wolf" && Near(loadedTrail->getWidth(), 3.75f));
        const auto children = loadedRoot->GetChildEntities();
        MARKER_CHECK(children.size() == 3);
        for (size_t index = 0; index < 3; ++index)
        {
            ExpectVector(children[index]->GetLocalTransform().position, positions[index], "子のローカル位置");
            const auto* point = children[index]->GetComponent<L::ScentTrailPointComponent>();
            MARKER_CHECK(point != nullptr && Near(point->getIntensity(), intensities[index]));
        }
        C::Container::VariableArray<M::Vector3> gatheredPositions;
        C::Container::VariableArray<float> gatheredIntensities;
        // 子のワールド位置は World の更新でキャッシュに反映される。
        loadedWorld.UpdateWorldTransforms();
        loadedTrail->GatherPoints(gatheredPositions, gatheredIntensities);
        MARKER_CHECK(gatheredPositions.size() == 3 && gatheredIntensities.size() == 3);
        for (size_t index = 0; index < 3; ++index)
        {
            ExpectVector(gatheredPositions[index], M::Vector3(10.0f, 20.0f, -5.0f) + positions[index], "痕跡のワールド位置");
            MARKER_CHECK(Near(gatheredIntensities[index], intensities[index]));
        }
        MARKER_CHECK(L::ValidateLevelMarkers(loadedWorld, issues) && issues.empty());
        loadedWorld.Finalize();
        sourceWorld.Finalize();
        MARKER_CHECK(std::remove(kRoundTripPath) == 0);
    }
} // 無名名前空間

int main()
{
    ReadBeforeRegistration();
    CheckAllSchemas();
    CheckValidation();
    CheckRoundTrip();
    std::puts("GameplayMarkerSchemaTest: 合格");
    return 0;
}
