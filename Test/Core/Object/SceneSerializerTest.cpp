#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Object/PrefabAsset.h"
#include "Component/DirectionalLightComponent.h"
#include "Component/CameraComponent.h"
#include "Component/ScriptComponent.h"
#include "Component/SpotLightComponent.h"
#include "Object/ResourceRegistry.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "FileStream/FileStream.h"
#include "Scene/SceneSerializer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace NorvesLib::Core;
namespace Component = NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;
namespace Rendering = NorvesLib::Core::Rendering;
namespace Scene = NorvesLib::Core::Scene;

namespace
{
    constexpr float Epsilon = 0.0001f;

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    bool Near(float lhs, float rhs, float tolerance)
    {
        return std::fabs(lhs - rhs) <= tolerance;
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

    void RemovePropertyRecordByName(Scene::SceneObjectRecord& record, const char* name)
    {
        for (size_t index = 0; index < record.Properties.size(); ++index)
        {
            if (record.Properties[index].Name == name)
            {
                record.Properties.erase(record.Properties.begin() + static_cast<std::ptrdiff_t>(index));
                return;
            }
        }
    }

    void ZeroPhysicalLegacyIds(Scene::SceneObjectRecord& record)
    {
        record.ClassId = 0;
        for (Scene::ScenePropertyRecord& property : record.Properties)
        {
            property.PropertyId = 0;
            property.TypeId = 0;
        }
    }

    void RegisterRequiredClasses()
    {
        (void)Entity::StaticClass();
        (void)Component::Component::StaticClass();
        (void)Component::PointLightComponent::StaticClass();
        (void)Component::DirectionalLightComponent::StaticClass();
        (void)Component::CameraComponent::StaticClass();
        (void)Component::ScriptComponent::StaticClass();
        (void)Component::SpotLightComponent::StaticClass();
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

    enum class TestLightKind
    {
        Directional,
        Point,
        Spot
    };

    Component::LightComponent* CreateTestLight(World& world, Entity* owner, TestLightKind kind)
    {
        switch (kind)
        {
        case TestLightKind::Directional:
            return world.CreateComponent<Component::DirectionalLightComponent>(owner);
        case TestLightKind::Point:
            return world.CreateComponent<Component::PointLightComponent>(owner);
        case TestLightKind::Spot:
            return world.CreateComponent<Component::SpotLightComponent>(owner);
        }
        return nullptr;
    }

    Component::LightComponent* FindTestLight(Entity& owner, TestLightKind kind)
    {
        switch (kind)
        {
        case TestLightKind::Directional:
            return owner.GetComponent<Component::DirectionalLightComponent>();
        case TestLightKind::Point:
            return owner.GetComponent<Component::PointLightComponent>();
        case TestLightKind::Spot:
            return owner.GetComponent<Component::SpotLightComponent>();
        }
        return nullptr;
    }

    Rendering::LightType ExpectedLightType(TestLightKind kind)
    {
        switch (kind)
        {
        case TestLightKind::Directional:
            return Rendering::LightType::Directional;
        case TestLightKind::Point:
            return Rendering::LightType::Point;
        case TestLightKind::Spot:
            return Rendering::LightType::Spot;
        }
        return Rendering::LightType::Directional;
    }

    Scene::SceneDocument BuildLightDocument(
        TestLightKind kind,
        float intensity,
        Component::LightIntensityUnit unit,
        const Math::Vector3& color)
    {
        World world;
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        Component::LightComponent* light = CreateTestLight(world, root, kind);
        assert(light != nullptr);
        light->SetIntensity(intensity);
        assert(light->SetIntensityUnit(unit));
        light->SetLightColor(color.x, color.y, color.z);

        Container::VariableArray<EntitySubtreeSnapshot> roots;
        roots.push_back(RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root));
        Scene::SceneDocument document = Scene::SceneSerializer::BuildDocument(roots);
        world.Finalize();
        return document;
    }

    Scene::SceneDocument BuildCameraDocument()
    {
        World world;
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        assert(world.CreateComponent<Component::CameraComponent>(root) != nullptr);

        Container::VariableArray<EntitySubtreeSnapshot> roots;
        roots.push_back(RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root));
        Scene::SceneDocument document = Scene::SceneSerializer::BuildDocument(roots);
        world.Finalize();
        return document;
    }

    Scene::SceneDocument ConvertLightDocumentToV1(Scene::SceneDocument document)
    {
        assert(document.Roots.size() == 1);
        assert(document.Roots[0].Root.Components.size() == 1);
        document.FormatVersion = 1;
        document.Roots[0].FormatVersion = 1;
        Scene::SceneObjectRecord& lightRecord = document.Roots[0].Root.Components[0].Object;
        RemovePropertyRecordByName(lightRecord, "IntensityUnit");
        RemovePropertyRecordByName(lightRecord, "LightColor");
        ZeroPhysicalLegacyIds(lightRecord);
        return document;
    }

    Scene::SceneDocument ConvertCameraDocumentToV1(Scene::SceneDocument document)
    {
        assert(document.Roots.size() == 1);
        assert(document.Roots[0].Root.Components.size() == 1);
        document.FormatVersion = 1;
        document.Roots[0].FormatVersion = 1;
        Scene::SceneObjectRecord& cameraRecord = document.Roots[0].Root.Components[0].Object;
        RemovePropertyRecordByName(cameraRecord, "Aperture");
        RemovePropertyRecordByName(cameraRecord, "ShutterSpeed");
        RemovePropertyRecordByName(cameraRecord, "ISO");
        RemovePropertyRecordByName(cameraRecord, "ExposureCompensation");
        ZeroPhysicalLegacyIds(cameraRecord);
        return document;
    }

    Entity* SpawnSingleRoot(World& world, const Scene::SceneDocument& document)
    {
        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        assert(Scene::SceneSerializer::ReconcileWithSchema(document, reconciled, stats));
        assert(reconciled.size() == 1);

        ResourceRegistry registry;
        assert(registry.Initialize());
        auto prefab = registry.CreateTransient<PrefabAsset>("SceneSerializerPhysicalFixture");
        assert(prefab != nullptr);
        prefab->SetTree(reconciled[0]);
        Entity* spawned = world.SpawnPrefab(*prefab);
        prefab.reset();
        registry.Shutdown();
        return spawned;
    }

    void ExpectJsonRejectedAtomically(const Container::String& json)
    {
        Scene::SceneDocument parsed;
        Container::String error;
        const bool parsedSuccessfully = Scene::SceneSerializer::TryParseJson(json, parsed, &error);

        const Container::String invalidPath("SceneSerializerTestInvalidPhysical.scene.json");
        (void)std::remove(invalidPath.c_str());
        bool wroteFile = false;
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream =
                NorvesLib::FileStream::FileStream::CreateUnique(
                    invalidPath,
                    NorvesLib::FileStream::FileMode::Write,
                    NorvesLib::FileStream::FileAccess::Write);
            if (stream != nullptr && stream->IsOpen())
            {
                wroteFile = stream->WriteString(json) == json.size();
                stream->Flush();
            }
        }

        World world;
        world.Initialize();
        Entity* sentinel = world.SpawnEntity<Entity>();
        assert(sentinel != nullptr);
        Scene::SceneLoadStats loadStats;
        const bool loaded = wroteFile &&
            Scene::SceneSerializer::LoadIntoWorld(world, invalidPath, &loadStats);
        const Container::VariableArray<Entity*> rootsAfter = world.GetRootEntities();
        const bool worldUnchanged = rootsAfter.size() == 1 && rootsAfter[0] == sentinel;
        const int removeResult = std::remove(invalidPath.c_str());
        world.Finalize();

        assert(wroteFile);
        assert(removeResult == 0);
        assert(!parsedSuccessfully);
        assert(!loaded);
        assert(worldUnchanged);
    }

    void ExpectDocumentRejectedAtomically(const Scene::SceneDocument& document)
    {
        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        const bool reconciledSuccessfully =
            Scene::SceneSerializer::ReconcileWithSchema(document, reconciled, stats);
        ExpectJsonRejectedAtomically(Scene::SceneSerializer::ToJson(document));
        assert(!reconciledSuccessfully);
        assert(reconciled.empty());
    }

    Container::String ReplaceRootFormatVersion(
        const Scene::SceneDocument& document,
        const char* currentVersion,
        const char* replacementVersion)
    {
        Container::String json = Scene::SceneSerializer::ToJson(document);
        Container::String token("\"formatVersion\": ");
        token += currentVersion;
        const size_t topVersion = json.find(token);
        assert(topVersion != Container::String::npos);
        const size_t rootVersion = json.find(token, topVersion + token.size());
        assert(rootVersion != Container::String::npos);
        Container::String replacement("\"formatVersion\": ");
        replacement += replacementVersion;
        json.replace(rootVersion, token.size(), replacement);
        return json;
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
            versioned.Roots[0].FormatVersion = 3;
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
            Container::String("{\"formatVersion\":3,\"roots\":[]}"), document, &error));
        assert(!error.empty());

        error = Container::String();
        assert(!Scene::SceneSerializer::TryParseJson(
            Container::String("{\"formatVersion\":1}"), document, &error));
        assert(!error.empty());
    }

    void TestExactFormatVersionValidation()
    {
        const char* invalidTopLevelVersions[] = {
            "{\"formatVersion\":1.5,\"roots\":[]}",
            "{\"formatVersion\":-1,\"roots\":[]}",
            "{\"formatVersion\":4294967296,\"roots\":[]}"};
        for (const char* json : invalidTopLevelVersions)
        {
            Scene::SceneDocument document;
            Container::String error;
            assert(!Scene::SceneSerializer::TryParseJson(Container::String(json), document, &error));
        }

        const char* invalidRootLevelVersions[] = {
            "{\"formatVersion\":2,\"roots\":[{\"formatVersion\":2.9,\"rootAlias\":\"1\",\"rootPath\":\"\",\"root\":{\"alias\":\"1\",\"parentAlias\":\"0\",\"object\":{\"class\":\"Entity\",\"classId\":\"0\",\"path\":\"\",\"properties\":[]},\"components\":[],\"children\":[]}}]}",
            "{\"formatVersion\":2,\"roots\":[{\"formatVersion\":-1,\"rootAlias\":\"1\",\"rootPath\":\"\",\"root\":{\"alias\":\"1\",\"parentAlias\":\"0\",\"object\":{\"class\":\"Entity\",\"classId\":\"0\",\"path\":\"\",\"properties\":[]},\"components\":[],\"children\":[]}}]}",
            "{\"formatVersion\":2,\"roots\":[{\"formatVersion\":4294967296,\"rootAlias\":\"1\",\"rootPath\":\"\",\"root\":{\"alias\":\"1\",\"parentAlias\":\"0\",\"object\":{\"class\":\"Entity\",\"classId\":\"0\",\"path\":\"\",\"properties\":[]},\"components\":[],\"children\":[]}}]}"};
        for (const char* json : invalidRootLevelVersions)
        {
            Scene::SceneDocument document;
            Container::String error;
            assert(!Scene::SceneSerializer::TryParseJson(Container::String(json), document, &error));
        }
    }

    void TestPhysicalValidationRejectsBlockingRows()
    {
        RegisterRequiredClasses();

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            aperture->Value = "3.4028235e+38";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* lightType = FindPropertyRecordByName(lightRecord, "LightTypeProp");
            assert(lightType != nullptr);
            lightType->Value = "0";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Spot,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* inner = FindPropertyRecordByName(lightRecord, "InnerConeAngle");
            Scene::ScenePropertyRecord* outer = FindPropertyRecordByName(lightRecord, "OuterConeAngle");
            assert(inner != nullptr);
            assert(outer != nullptr);
            inner->Value = "40";
            outer->Value = "20";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            aperture->PropertyId = MakePropertyId(Component::CameraComponent::StaticClass(), "ISO");
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            aperture->TypeName = "double";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            aperture->TypeId = TypeRegistry::Get().Find(TypeRegistry::Get().GetTypeId<double>())->StableId;
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            cameraRecord.Properties.push_back(*aperture);
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* aperture = FindPropertyRecordByName(cameraRecord, "Aperture");
            assert(aperture != nullptr);
            aperture->Name = "ISO";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            const TestLightKind kinds[] = {
                TestLightKind::Directional,
                TestLightKind::Point,
                TestLightKind::Spot};
            const char* invalidIntensityValues[] = {"-1", "nan", "inf", "-inf"};
            for (TestLightKind kind : kinds)
            {
                for (const char* value : invalidIntensityValues)
                {
                    Scene::SceneDocument invalid = BuildLightDocument(
                        kind,
                        1.0f,
                        kind == TestLightKind::Directional
                            ? Component::LightIntensityUnit::Lux
                            : Component::LightIntensityUnit::Candela,
                        Math::Vector3(1.0f, 1.0f, 1.0f));
                    Scene::ScenePropertyRecord* intensity = FindPropertyRecordByName(
                        invalid.Roots[0].Root.Components[0].Object,
                        "Intensity");
                    assert(intensity != nullptr);
                    intensity->Value = value;
                    ExpectDocumentRejectedAtomically(invalid);
                }
            }
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* color = FindPropertyRecordByName(lightRecord, "LightColor");
            assert(color != nullptr);
            color->Value = "Vector3(0,0,0)";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalidDirectional = BuildLightDocument(
                TestLightKind::Directional,
                1.0f,
                Component::LightIntensityUnit::Lux,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::ScenePropertyRecord* directionalUnit = FindPropertyRecordByName(
                invalidDirectional.Roots[0].Root.Components[0].Object,
                "IntensityUnit");
            assert(directionalUnit != nullptr);
            directionalUnit->Value = "1";
            ExpectDocumentRejectedAtomically(invalidDirectional);

            Scene::SceneDocument invalidPoint = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::ScenePropertyRecord* pointUnit = FindPropertyRecordByName(
                invalidPoint.Roots[0].Root.Components[0].Object,
                "IntensityUnit");
            assert(pointUnit != nullptr);
            pointUnit->Value = "0";
            ExpectDocumentRejectedAtomically(invalidPoint);
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            lightRecord.ClassId = MakeStableSchemaId(
                "NorvesLib",
                "Class",
                "DirectionalLightComponent");
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* intensity = FindPropertyRecordByName(lightRecord, "Intensity");
            assert(intensity != nullptr);
            lightRecord.Properties.push_back(*intensity);
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* intensity = FindPropertyRecordByName(lightRecord, "Intensity");
            assert(intensity != nullptr);
            intensity->Name = "IntensityUnit";
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            const char* invalidConePairs[][2] = {
                {"-1", "20"},
                {"20", "20"},
                {"20", "180"},
                {"20", "nan"}};
            for (const auto& conePair : invalidConePairs)
            {
                Scene::SceneDocument invalid = BuildLightDocument(
                    TestLightKind::Spot,
                    1.0f,
                    Component::LightIntensityUnit::Candela,
                    Math::Vector3(1.0f, 1.0f, 1.0f));
                Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
                Scene::ScenePropertyRecord* inner = FindPropertyRecordByName(lightRecord, "InnerConeAngle");
                Scene::ScenePropertyRecord* outer = FindPropertyRecordByName(lightRecord, "OuterConeAngle");
                assert(inner != nullptr);
                assert(outer != nullptr);
                inner->Value = conePair[0];
                outer->Value = conePair[1];
                ExpectDocumentRejectedAtomically(invalid);
            }
        }

        {
            Scene::SceneDocument invalid = BuildCameraDocument();
            Scene::SceneObjectRecord& cameraRecord = invalid.Roots[0].Root.Components[0].Object;
            cameraRecord.ClassId = MakeStableSchemaId(
                "NorvesLib",
                "Class",
                "PointLightComponent");
            ExpectDocumentRejectedAtomically(invalid);
        }

        {
            struct InvalidCameraValue
            {
                const char* PropertyName;
                const char* Value;
            };
            const InvalidCameraValue invalidValues[] = {
                {"Aperture", "-1"},
                {"ShutterSpeed", "0"},
                {"ISO", "nan"},
                {"ExposureCompensation", "inf"}};
            for (const InvalidCameraValue& invalidValue : invalidValues)
            {
                Scene::SceneDocument invalid = BuildCameraDocument();
                Scene::ScenePropertyRecord* property = FindPropertyRecordByName(
                    invalid.Roots[0].Root.Components[0].Object,
                    invalidValue.PropertyName);
                assert(property != nullptr);
                property->Value = invalidValue.Value;
                ExpectDocumentRejectedAtomically(invalid);
            }
        }

        {
            Scene::SceneDocument invalid = BuildLightDocument(
                TestLightKind::Point,
                1.0f,
                Component::LightIntensityUnit::Candela,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            invalid.FormatVersion = 1;
            invalid.Roots[0].FormatVersion = 1;
            Scene::SceneObjectRecord& lightRecord = invalid.Roots[0].Root.Components[0].Object;
            Scene::ScenePropertyRecord* intensity = FindPropertyRecordByName(lightRecord, "Intensity");
            assert(intensity != nullptr);
            lightRecord.Properties.push_back(*intensity);

            const Container::String invalidJson = Scene::SceneSerializer::ToJson(invalid);
            Scene::SceneDocument parsed;
            Container::String error;
            assert(!Scene::SceneSerializer::TryParseJson(invalidJson, parsed, &error));
            assert(error.find("duplicate") != Container::String::npos);
            ExpectJsonRejectedAtomically(invalidJson);
        }
    }

    void TestPhysicalCameraSceneRoundTrip()
    {
        RegisterRequiredClasses();

        World world;
        world.Initialize();

        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        Component::CameraComponent* camera = world.CreateComponent<Component::CameraComponent>(root);
        assert(camera != nullptr);
        assert(camera->SetAperture(2.8f));
        assert(camera->SetShutterSpeed(1.0f / 125.0f));
        assert(camera->SetISO(200.0f));
        assert(camera->SetExposureCompensation(1.0f));

        Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
        sourceRoots.push_back(RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root));
        Scene::SceneDocument document = Scene::SceneSerializer::BuildDocument(sourceRoots);
        assert(document.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);
        assert(document.Roots.size() == 1);
        assert(document.Roots[0].Root.Components.size() == 1);

        Scene::SceneObjectRecord& cameraRecord = document.Roots[0].Root.Components[0].Object;
        assert(FindPropertyRecordByName(cameraRecord, "Aperture") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ShutterSpeed") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ISO") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ExposureCompensation") != nullptr);

        Scene::SceneDocument parsed;
        Container::String error;
        assert(Scene::SceneSerializer::TryParseJson(Scene::SceneSerializer::ToJson(document), parsed, &error));

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        assert(Scene::SceneSerializer::ReconcileWithSchema(parsed, reconciled, stats));
        assert(reconciled.size() == 1);

        ResourceRegistry registry;
        assert(registry.Initialize());
        auto prefab = registry.CreateTransient<PrefabAsset>("SceneSerializerPhysicalCamera");
        assert(prefab != nullptr);
        prefab->SetTree(reconciled[0]);
        Entity* spawned = world.SpawnPrefab(*prefab);
        assert(spawned != nullptr);
        Component::CameraComponent* spawnedCamera = spawned->GetComponent<Component::CameraComponent>();
        assert(spawnedCamera != nullptr);
        assert(Near(spawnedCamera->GetAperture(), 2.8f));
        assert(Near(spawnedCamera->GetShutterSpeed(), 1.0f / 125.0f));
        assert(Near(spawnedCamera->GetISO(), 200.0f));
        assert(Near(spawnedCamera->GetExposureCompensation(), 1.0f));

        world.Finalize();
        prefab.reset();
        registry.Shutdown();
    }

    void TestPhysicalCameraV1MigrationAndSpawn()
    {
        RegisterRequiredClasses();

        const Scene::SceneDocument v1 = ConvertCameraDocumentToV1(BuildCameraDocument());
        const Container::String v1Json = Scene::SceneSerializer::ToJson(v1);
        assert(!v1Json.empty());

        Scene::SceneDocument migrated;
        Container::String error;
        assert(Scene::SceneSerializer::TryParseJson(v1Json, migrated, &error));
        assert(migrated.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);
        assert(migrated.Roots[0].FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);

        Scene::SceneObjectRecord& cameraRecord = migrated.Roots[0].Root.Components[0].Object;
        assert(cameraRecord.ClassId == MakeStableSchemaId(
            "NorvesLib",
            "Class",
            "CameraComponent"));
        assert(FindPropertyRecordByName(cameraRecord, "Aperture") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ShutterSpeed") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ISO") != nullptr);
        assert(FindPropertyRecordByName(cameraRecord, "ExposureCompensation") != nullptr);

        World world;
        world.Initialize();
        Entity* spawned = SpawnSingleRoot(world, migrated);
        assert(spawned != nullptr);
        Component::CameraComponent* camera = spawned->GetComponent<Component::CameraComponent>();
        assert(camera != nullptr);
        assert(Near(camera->GetAperture(), 4.0f));
        assert(Near(camera->GetShutterSpeed(), 1.0f / 60.0f));
        assert(Near(camera->GetISO(), 100.0f));
        assert(Near(camera->GetExposureCompensation(), 0.0f));

        Rendering::CameraProxy proxy;
        assert(camera->BuildCameraProxy(proxy));
        assert(Near(proxy.Aperture, 4.0f));
        assert(Near(proxy.ShutterSpeed, 1.0f / 60.0f));
        assert(Near(proxy.ISO, 100.0f));
        assert(Near(proxy.ExposureCompensation, 0.0f));

        world.Finalize();
    }

    void TestPhysicalPointLumenRoundTrip()
    {
        RegisterRequiredClasses();

        Scene::SceneDocument document = BuildLightDocument(
            TestLightKind::Point,
            100.0f,
            Component::LightIntensityUnit::Lumen,
            Math::Vector3(1.0f, 0.25f, 0.1f));
        assert(document.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);

        Scene::SceneObjectRecord& sourceRecord = document.Roots[0].Root.Components[0].Object;
        Scene::ScenePropertyRecord* sourceIntensity = FindPropertyRecordByName(sourceRecord, "Intensity");
        Scene::ScenePropertyRecord* sourceUnit = FindPropertyRecordByName(sourceRecord, "IntensityUnit");
        Scene::ScenePropertyRecord* sourceColor = FindPropertyRecordByName(sourceRecord, "LightColor");
        assert(sourceIntensity != nullptr);
        assert(sourceUnit != nullptr);
        assert(sourceColor != nullptr);
        assert(sourceIntensity->Value == "100");
        assert(sourceUnit->Value == "2");
        PropertyValue sourceColorValue;
        assert(sourceColorValue.Deserialize<Math::Vector3>(sourceColor->Value));
        const Math::Vector3* sourceColorVector = sourceColorValue.Get<Math::Vector3>();
        assert(sourceColorVector != nullptr);
        assert(Near(sourceColorVector->x, 1.0f));
        assert(Near(sourceColorVector->y, 0.25f));
        assert(Near(sourceColorVector->z, 0.1f));

        const Container::String json = Scene::SceneSerializer::ToJson(document);
        assert(!json.empty());

        Scene::SceneDocument parsed;
        Container::String error;
        assert(Scene::SceneSerializer::TryParseJson(json, parsed, &error));

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        assert(Scene::SceneSerializer::ReconcileWithSchema(parsed, reconciled, stats));
        assert(reconciled.size() == 1);
        const ObjectSnapshot& reconciledLight = reconciled[0].Root.Components[0].Object;
        const ProjectedPropertyValue* intensity = FindProjectedValue(
            reconciledLight,
            MakePropertyId(Component::PointLightComponent::StaticClass(), "Intensity"));
        const ProjectedPropertyValue* unit = FindProjectedValue(
            reconciledLight,
            MakePropertyId(Component::PointLightComponent::StaticClass(), "IntensityUnit"));
        const ProjectedPropertyValue* color = FindProjectedValue(
            reconciledLight,
            MakePropertyId(Component::PointLightComponent::StaticClass(), "LightColor"));
        assert(intensity != nullptr);
        assert(unit != nullptr);
        assert(color != nullptr);
        assert(intensity->SerializedValue == "100");
        assert(unit->SerializedValue == "2");
        PropertyValue reconciledColorValue;
        assert(reconciledColorValue.Deserialize<Math::Vector3>(color->SerializedValue));
        const Math::Vector3* reconciledColorVector = reconciledColorValue.Get<Math::Vector3>();
        assert(reconciledColorVector != nullptr);
        assert(Near(reconciledColorVector->x, 1.0f));
        assert(Near(reconciledColorVector->y, 0.25f));
        assert(Near(reconciledColorVector->z, 0.1f));

        World world;
        world.Initialize();
        Entity* spawned = SpawnSingleRoot(world, parsed);
        assert(spawned != nullptr);
        Component::PointLightComponent* point = spawned->GetComponent<Component::PointLightComponent>();
        assert(point != nullptr);
        assert(Near(point->GetIntensity(), 100.0f));
        assert(point->GetIntensityUnit() == Component::LightIntensityUnit::Lumen);

        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
        point->GetLightColor(red, green, blue);
        assert(Near(red, 1.0f));
        assert(Near(green, 0.25f));
        assert(Near(blue, 0.1f));

        Rendering::LightProxy proxy;
        assert(point->BuildLightProxy(proxy));
        assert(proxy.Type == Rendering::LightType::Point);
        const float luminance = 0.2126f * 1.0f + 0.7152f * 0.25f + 0.0722f * 0.1f;
        const float expectedCanonicalIntensity = 100.0f / (4.0f * Math::Constants::PI);
        assert(Near(proxy.CanonicalIntensity, expectedCanonicalIntensity, 1.0e-4f));
        assert(Near(proxy.ColorR, 1.0f / luminance, 1.0e-4f));
        assert(Near(proxy.ColorG, 0.25f / luminance, 1.0e-4f));
        assert(Near(proxy.ColorB, 0.1f / luminance, 1.0e-4f));

        world.Finalize();
    }

    void TestPhysicalLightV1MigrationAndSpawn()
    {
        RegisterRequiredClasses();

        struct LightMigrationCase
        {
            TestLightKind Kind;
            Component::LightIntensityUnit Unit;
            float Intensity;
            const char* SerializedIntensity;
        };
        const LightMigrationCase cases[] = {
            {TestLightKind::Directional, Component::LightIntensityUnit::Lux, 4.0f, "4"},
            {TestLightKind::Point, Component::LightIntensityUnit::Candela, 7.0f, "7"},
            {TestLightKind::Spot, Component::LightIntensityUnit::Candela, 11.0f, "11"}};

        for (const LightMigrationCase& testCase : cases)
        {
            World sourceWorld;
            sourceWorld.Initialize();
            Entity* sourceRoot = sourceWorld.SpawnEntity<Entity>();
            assert(sourceRoot != nullptr);
            Component::LightComponent* sourceLight = CreateTestLight(sourceWorld, sourceRoot, testCase.Kind);
            assert(sourceLight != nullptr);
            sourceLight->SetIntensity(testCase.Intensity);
            assert(sourceLight->SetIntensityUnit(testCase.Unit));
            sourceLight->SetLightColor(1.0f, 1.0f, 1.0f);
            if (testCase.Kind == TestLightKind::Spot)
            {
                Component::SpotLightComponent* spot =
                    sourceRoot->GetComponent<Component::SpotLightComponent>();
                assert(spot != nullptr);
                spot->SetInnerConeAngle(15.0f);
                spot->SetOuterConeAngle(35.0f);
            }

            Rendering::LightProxy sourceProxy;
            assert(sourceLight->BuildLightProxy(sourceProxy));
            assert(sourceProxy.Type == ExpectedLightType(testCase.Kind));
            assert(Near(sourceProxy.CanonicalIntensity, testCase.Intensity, 1.0e-4f));
            assert(Near(sourceProxy.ColorR, 1.0f));
            assert(Near(sourceProxy.ColorG, 1.0f));
            assert(Near(sourceProxy.ColorB, 1.0f));

            Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
            sourceRoots.push_back(RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*sourceRoot));
            Scene::SceneDocument v1 = ConvertLightDocumentToV1(
                Scene::SceneSerializer::BuildDocument(sourceRoots));
            const Container::String v1Json = Scene::SceneSerializer::ToJson(v1);

            Scene::SceneDocument migrated;
            Container::String error;
            assert(Scene::SceneSerializer::TryParseJson(v1Json, migrated, &error));
            assert(migrated.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);
            Scene::SceneLoadStats stats;
            Container::VariableArray<EntitySubtreeSnapshot> reconciled;
            assert(Scene::SceneSerializer::ReconcileWithSchema(migrated, reconciled, stats));
            assert(reconciled.size() == 1);

            const ObjectSnapshot& migratedLight = reconciled[0].Root.Components[0].Object;
            const IClass* lightClass = nullptr;
            switch (testCase.Kind)
            {
            case TestLightKind::Directional:
                lightClass = Component::DirectionalLightComponent::StaticClass();
                break;
            case TestLightKind::Point:
                lightClass = Component::PointLightComponent::StaticClass();
                break;
            case TestLightKind::Spot:
                lightClass = Component::SpotLightComponent::StaticClass();
                break;
            }
            assert(lightClass != nullptr);
            const ProjectedPropertyValue* intensity = FindProjectedValue(
                migratedLight,
                MakePropertyId(lightClass, "Intensity"));
            const ProjectedPropertyValue* unit = FindProjectedValue(
                migratedLight,
                MakePropertyId(lightClass, "IntensityUnit"));
            const ProjectedPropertyValue* color = FindProjectedValue(
                migratedLight,
                MakePropertyId(lightClass, "LightColor"));
            assert(intensity != nullptr);
            assert(unit != nullptr);
            assert(color != nullptr);
            assert(intensity->SerializedValue == testCase.SerializedIntensity);
            assert(unit->SerializedValue == (testCase.Kind == TestLightKind::Directional ? "0" : "1"));
            assert(color->SerializedValue == "Vector3(1,1,1)");

            World targetWorld;
            targetWorld.Initialize();
            Entity* spawned = SpawnSingleRoot(targetWorld, migrated);
            assert(spawned != nullptr);
            Component::LightComponent* spawnedLight = FindTestLight(*spawned, testCase.Kind);
            assert(spawnedLight != nullptr);
            assert(Near(spawnedLight->GetIntensity(), testCase.Intensity));
            assert(spawnedLight->GetIntensityUnit() == testCase.Unit);
            float red = 0.0f;
            float green = 0.0f;
            float blue = 0.0f;
            spawnedLight->GetLightColor(red, green, blue);
            assert(Near(red, 1.0f));
            assert(Near(green, 1.0f));
            assert(Near(blue, 1.0f));

            Rendering::LightProxy spawnedProxy;
            assert(spawnedLight->BuildLightProxy(spawnedProxy));
            assert(spawnedProxy.Type == ExpectedLightType(testCase.Kind));
            assert(Near(spawnedProxy.CanonicalIntensity, testCase.Intensity, 1.0e-4f));

            targetWorld.Finalize();
            sourceWorld.Finalize();
        }
    }

    void TestTrackedV1FixtureLoad()
    {
        RegisterRequiredClasses();

        Container::String fixturePath(NORVES_SOURCE_ROOT);
        fixturePath += "/Assets/Scenes/M6AngelScriptDemo.scene.json";

        World world;
        world.Initialize();
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(world, fixturePath, &stats));
        assert(stats.LoadedRoots == 1);

        Container::VariableArray<Entity*> roots = world.GetRootEntities();
        assert(roots.size() == 1);
        assert(roots[0]->GetComponent<Component::ScriptComponent>() != nullptr);

        world.Finalize();
    }

    void TestPhysicalLightSceneMigration()
    {
        RegisterRequiredClasses();

        World world;
        world.Initialize();
        Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
        sourceRoots.push_back(BuildSourceSnapshot(world));

        const Scene::SceneDocument v2 = Scene::SceneSerializer::BuildDocument(sourceRoots);
        assert(v2.FormatVersion == 2);
        assert(v2.Roots[0].FormatVersion == 2);
        assert(FindPropertyRecordByName(
                   const_cast<Scene::SceneObjectRecord&>(v2.Roots[0].Root.Components[0].Object),
                   "IntensityUnit") != nullptr);
        assert(FindPropertyRecordByName(
                   const_cast<Scene::SceneObjectRecord&>(v2.Roots[0].Root.Components[0].Object),
                   "LightColor") != nullptr);

        Scene::SceneDocument v1 = v2;
        v1.FormatVersion = 1;
        v1.Roots[0].FormatVersion = 1;
        RemovePropertyRecordByName(v1.Roots[0].Root.Components[0].Object, "IntensityUnit");
        RemovePropertyRecordByName(v1.Roots[0].Root.Components[0].Object, "LightColor");
        const Container::String v1Json = Scene::SceneSerializer::ToJson(v1);
        Scene::SceneDocument migrated;
        Container::String error;
        assert(Scene::SceneSerializer::TryParseJson(v1Json, migrated, &error));
        assert(migrated.FormatVersion == 2);
        assert(migrated.Roots[0].FormatVersion == 2);
        Scene::SceneObjectRecord& migratedLight = migrated.Roots[0].Root.Components[0].Object;
        Scene::ScenePropertyRecord* unit = FindPropertyRecordByName(migratedLight, "IntensityUnit");
        Scene::ScenePropertyRecord* color = FindPropertyRecordByName(migratedLight, "LightColor");
        assert(unit != nullptr);
        assert(color != nullptr);
        assert(unit->Value == "1");
        assert(color->Value == "Vector3(1,1,1)");

        Scene::SceneDocument invalid = v2;
        Scene::ScenePropertyRecord* invalidUnit = FindPropertyRecordByName(
            invalid.Roots[0].Root.Components[0].Object, "IntensityUnit");
        assert(invalidUnit != nullptr);
        invalidUnit->Value = "0";
        assert(!Scene::SceneSerializer::TryParseJson(Scene::SceneSerializer::ToJson(invalid), migrated, &error));

        const Container::String invalidJson = Scene::SceneSerializer::ToJson(invalid);
        assert(!invalidJson.empty());
        const Container::String invalidPath("SceneSerializerTestInvalidPhysical.scene.json");
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream =
                NorvesLib::FileStream::FileStream::CreateUnique(
                    invalidPath,
                    NorvesLib::FileStream::FileMode::Write,
                    NorvesLib::FileStream::FileAccess::Write);
            assert(stream != nullptr);
            assert(stream->IsOpen());
            assert(stream->WriteString(invalidJson) == invalidJson.size());
            stream->Flush();
        }
        Container::VariableArray<Entity*> rootsBefore = world.GetRootEntities();
        Scene::SceneLoadStats loadStats;
        assert(!Scene::SceneSerializer::LoadIntoWorld(world, invalidPath, &loadStats));
        Container::VariableArray<Entity*> rootsAfter = world.GetRootEntities();
        assert(rootsAfter.size() == rootsBefore.size());
        assert(rootsAfter[0] == rootsBefore[0]);
        assert(std::remove(invalidPath.c_str()) == 0);

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> reconciled;
        assert(!Scene::SceneSerializer::ReconcileWithSchema(invalid, reconciled, stats));
        assert(reconciled.empty());

        world.Finalize();
    }
}

int main()
{
    std::cout << "SceneSerializerTest start\n";

    TestJsonRoundTripAndSpawn();
    TestReconcileResilience();
    TestParseFailures();
    TestExactFormatVersionValidation();
    TestPhysicalValidationRejectsBlockingRows();
    TestPhysicalCameraSceneRoundTrip();
    TestPhysicalCameraV1MigrationAndSpawn();
    TestPhysicalPointLumenRoundTrip();
    TestPhysicalLightV1MigrationAndSpawn();
    TestTrackedV1FixtureLoad();
    TestPhysicalLightSceneMigration();

    std::cout << "SceneSerializerTest passed\n";
    return 0;
}
