#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Object/PrefabAsset.h"
#include "Object/ResourceRegistry.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
namespace Component = NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;
namespace Scene = NorvesLib::Core::Scene;

namespace
{
    constexpr float Epsilon = 0.0001f;

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    StablePropertyId MakePropertyId(const IClass* cls, const char* propertyName)
    {
        assert(cls != nullptr);
        return MakeStableSchemaId(
            "NorvesLib",
            "Property",
            cls->GetClassName().GetView(),
            Identity(propertyName).GetView());
    }

    const ProjectedPropertyValue* FindProjectedValue(const ObjectSnapshot& snapshot, StablePropertyId propertyId)
    {
        for (const ProjectedPropertyValue& value : snapshot.Properties)
        {
            if (value.Property == propertyId)
            {
                return &value;
            }
        }
        return nullptr;
    }

    Scene::ScenePropertyRecord* FindPropertyRecordByName(Scene::SceneObjectRecord& record, const char* name)
    {
        for (Scene::ScenePropertyRecord& property : record.Properties)
        {
            if (property.Name == name)
            {
                return &property;
            }
        }
        return nullptr;
    }

    void RegisterRequiredClasses()
    {
        (void)Entity::StaticClass();
        (void)Component::Component::StaticClass();
        (void)Component::PointLightComponent::StaticClass();
        (void)PrefabAsset::StaticClass();
    }

    // root(Position=1,2,3 / PointLight Range=64.5) - child(Scale=2,3,4) の1ルートを作る
    EntitySubtreeSnapshot BuildSourceSnapshot(World& world)
    {
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        root->SetLocalPosition(Math::Vector3(1.0f, 2.0f, 3.0f));

        Component::PointLightComponent* light = world.CreateComponent<Component::PointLightComponent>(root);
        assert(light != nullptr);
        light->SetRange(64.5f);

        Entity* child = world.SpawnEntity<Entity>(root);
        assert(child != nullptr);
        child->SetLocalScale(Math::Vector3(2.0f, 3.0f, 4.0f));

        return RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root);
    }

    void TestJsonRoundTripAndSpawn()
    {
        RegisterRequiredClasses();

        ResourceRegistry registry;
        assert(registry.Initialize());

        World world;
        world.Initialize();

        Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
        sourceRoots.push_back(BuildSourceSnapshot(world));

        // snapshot → document → JSON → document → snapshot
        const Scene::SceneDocument document = Scene::SceneSerializer::BuildDocument(sourceRoots);
        assert(document.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);
        assert(document.Roots.size() == 1);
        assert(document.Roots[0].Root.Object.ClassName == "Entity");
        assert(document.Roots[0].Root.Components.size() == 1);
        assert(document.Roots[0].Root.Components[0].Object.ClassName == "PointLightComponent");

        const Container::String json = Scene::SceneSerializer::ToJson(document);
        assert(!json.empty());

        Scene::SceneDocument parsed;
        Container::String parseError;
        assert(Scene::SceneSerializer::TryParseJson(json, parsed, &parseError));
        assert(parsed.Roots.size() == 1);

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        assert(Scene::SceneSerializer::ReconcileWithSchema(parsed, reconciled, stats));
        assert(reconciled.size() == 1);
        assert(stats.DroppedEntities == 0);
        assert(stats.DroppedComponents == 0);
        assert(stats.DroppedProperties == 0);
        assert(stats.LoadedRoots == 1);

        // alias再採番の整合（SpawnPrefabのValidate条件と同じ形）
        assert(reconciled[0].RootAlias == 1);
        assert(reconciled[0].Root.Alias == 1);
        assert(reconciled[0].Root.ParentAlias == InvalidSubtreeSnapshotAliasId);
        assert(reconciled[0].Root.Components.size() == 1);
        assert(reconciled[0].Root.Components[0].OwnerAlias == reconciled[0].Root.Alias);
        assert(reconciled[0].Root.Children.size() == 1);
        assert(reconciled[0].Root.Children[0].ParentAlias == reconciled[0].Root.Alias);

        // 値の保存性
        const ProjectedPropertyValue* position = FindProjectedValue(
            reconciled[0].Root.Object, MakePropertyId(Entity::StaticClass(), "Position"));
        assert(position != nullptr);
        assert(position->SerializedValue == "Vector3(1,2,3)");

        const ProjectedPropertyValue* range = FindProjectedValue(
            reconciled[0].Root.Components[0].Object,
            MakePropertyId(Component::PointLightComponent::StaticClass(), "Range"));
        assert(range != nullptr);
        assert(range->SerializedValue == "64.5");

        const ProjectedPropertyValue* scale = FindProjectedValue(
            reconciled[0].Root.Children[0].Object, MakePropertyId(Entity::StaticClass(), "Scale"));
        assert(scale != nullptr);
        assert(scale->SerializedValue == "Vector3(2,3,4)");

        // ランタイム専用プロパティはフィルタ段階で除去されている
        assert(FindProjectedValue(reconciled[0].Root.Object, MakePropertyId(Entity::StaticClass(), "ObjectId")) == nullptr);
        assert(FindProjectedValue(reconciled[0].Root.Object, MakePropertyId(Entity::StaticClass(), "bPendingDestroy")) == nullptr);

        // reconcile結果がSpawnPrefabで全か無かロールバックを起こさないこと
        auto prefab = registry.CreateTransient<PrefabAsset>("SceneSerializerTest");
        assert(prefab != nullptr);
        prefab->SetTree(reconciled[0]);

        Entity* spawned = world.SpawnPrefab(*prefab);
        assert(spawned != nullptr);
        assert(Near(spawned->GetLocalTransform().position.x, 1.0f));
        Component::PointLightComponent* spawnedLight = spawned->GetComponent<Component::PointLightComponent>();
        assert(spawnedLight != nullptr);
        assert(Near(spawnedLight->GetRange(), 64.5f));

        world.Finalize();
        prefab.reset();
        registry.Shutdown();
    }

    void TestReconcileResilience()
    {
        RegisterRequiredClasses();

        ResourceRegistry registry;
        assert(registry.Initialize());

        World world;
        world.Initialize();

        Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
        sourceRoots.push_back(BuildSourceSnapshot(world));
        const Scene::SceneDocument document = Scene::SceneSerializer::BuildDocument(sourceRoots);

        // (a) classId破損でも名前で解決できる（改名耐性の名前優先）
        {
            Scene::SceneDocument byName = document;
            byName.Roots[0].Root.Object.ClassId = 123456789u;
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(byName, reconciled, stats));
            assert(reconciled.size() == 1);
            assert(stats.DroppedEntities == 0);
        }

        // (b) クラス名が空でもStableIdの全走査で解決できる
        {
            Scene::SceneDocument byId = document;
            byId.Roots[0].Root.Object.ClassName = Container::String();
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(byId, reconciled, stats));
            assert(reconciled.size() == 1);
            assert(stats.DroppedEntities == 0);
        }

        // (c) 名前もIDも解決不能な未知Entityクラス → ルート部分木ごとdrop（root+child=2, component=1）
        {
            Scene::SceneDocument broken = document;
            broken.Roots[0].Root.Object.ClassName = "NoSuchEntityClass";
            broken.Roots[0].Root.Object.ClassId = 42u;
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(broken, reconciled, stats));
            assert(reconciled.empty());
            assert(stats.DroppedEntities == 2);
            assert(stats.DroppedComponents == 1);
            assert(stats.LoadedRoots == 0);
        }

        // (d) 未知コンポーネントクラス → そのコンポーネントのみdropし残りは生きる
        {
            Scene::SceneDocument broken = document;
            broken.Roots[0].Root.Components[0].Object.ClassName = "NoSuchComponentClass";
            broken.Roots[0].Root.Components[0].Object.ClassId = 42u;
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(broken, reconciled, stats));
            assert(reconciled.size() == 1);
            assert(reconciled[0].Root.Components.empty());
            assert(stats.DroppedComponents == 1);
            assert(stats.DroppedEntities == 0);
        }

        // (e) 未知プロパティ・型不一致・値破損 → 各プロパティのみdropし、結果はSpawn可能
        {
            Scene::SceneDocument mutated = document;
            Scene::SceneEntityRecord& mutatedRoot = mutated.Roots[0].Root;

            Scene::ScenePropertyRecord unknown;
            unknown.Name = "NoSuchProperty";
            unknown.PropertyId = 999u;
            unknown.TypeName = "float";
            unknown.TypeId = 1u;
            unknown.Value = "1";
            mutatedRoot.Object.Properties.push_back(unknown);

            Scene::ScenePropertyRecord* position = FindPropertyRecordByName(mutatedRoot.Object, "Position");
            assert(position != nullptr);
            position->TypeName = "float"; // 型不一致
            position->TypeId = 1u;

            Scene::ScenePropertyRecord* scale = FindPropertyRecordByName(mutatedRoot.Children[0].Object, "Scale");
            assert(scale != nullptr);
            scale->Value = "Vector3(1,2"; // 値破損

            Scene::ScenePropertyRecord* range = FindPropertyRecordByName(mutatedRoot.Components[0].Object, "Range");
            assert(range != nullptr);
            range->Value = "64.5junk"; // 末尾ゴミ（フェーズ2の厳格化によりDeserializeStableプローブが拒否）

            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(mutated, reconciled, stats));
            assert(reconciled.size() == 1);
            assert(stats.DroppedProperties == 4);

            auto prefab = registry.CreateTransient<PrefabAsset>("SceneSerializerResilience");
            assert(prefab != nullptr);
            prefab->SetTree(reconciled[0]);
            Entity* spawned = world.SpawnPrefab(*prefab);
            assert(spawned != nullptr); // 事前フィルタ済みなのでロールバックしない
            prefab.reset();
        }

        // (f) root単位のformatVersion不一致 → 警告付きでルート部分木ごとdrop（トップレベルと同方針の寛容版）
        {
            Scene::SceneDocument versioned = document;
            versioned.Roots[0].FormatVersion = 2;
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(versioned, reconciled, stats));
            assert(reconciled.empty());
            assert(stats.DroppedEntities == 2);
            assert(stats.DroppedComponents == 1);
            assert(stats.LoadedRoots == 0);
        }

        world.Finalize();
        registry.Shutdown();
    }

    void TestParseFailures()
    {
        Scene::SceneDocument document;
        Container::String error;

        assert(!Scene::SceneSerializer::TryParseJson(Container::String("{not json"), document, &error));
        assert(!error.empty());

        error = Container::String();
        assert(!Scene::SceneSerializer::TryParseJson(
            Container::String("{\"formatVersion\":2,\"roots\":[]}"), document, &error));
        assert(!error.empty());

        error = Container::String();
        assert(!Scene::SceneSerializer::TryParseJson(
            Container::String("{\"formatVersion\":1}"), document, &error));
        assert(!error.empty());
    }
}

int main()
{
    std::cout << "SceneSerializerTest start\n";

    TestJsonRoundTripAndSpawn();
    TestReconcileResilience();
    TestParseFailures();

    std::cout << "SceneSerializerTest passed\n";
    return 0;
}
