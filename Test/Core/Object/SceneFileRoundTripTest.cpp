#include "Component/BillboardComponent.h"
#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Component/TerrainComponent.h"
#include "FileStream/FileStream.h"
#include "Object/PrefabAsset.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core;
namespace Component = NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;
namespace Scene = NorvesLib::Core::Scene;

namespace
{
    constexpr float Epsilon = 0.0001f;
    constexpr const char* kSceneFilePath = "SceneFileRoundTripTest.scene.json";
    constexpr const char* kBrokenFilePath = "SceneFileRoundTripTest.broken.json";

    constexpr const char* kTerrainFilePath = "SceneFileRoundTripTest.terrain.scene.json";
    constexpr const char* kLegacyTerrainFilePath = "SceneFileRoundTripTest.terrain-legacy.scene.json";
    constexpr const char* kTerrainProperties[] =
    {
        "TerrainPath", "TilePathPrefix", "BaseColorR", "BaseColorG", "BaseColorB",
        "Roughness", "bCastShadow", "TilesPerFrame"
    };

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    void RegisterRequiredClasses()
    {
        (void)Entity::StaticClass();
        (void)Component::Component::StaticClass();
        (void)Component::PointLightComponent::StaticClass();
        (void)Component::BillboardComponent::StaticClass();
        (void)PrefabAsset::StaticClass();
    }

    bool IsRuntimeOnlyPropertyName(const Container::String& name)
    {
        return name == "ObjectId" ||
               name == "bPendingDestroy" ||
               name == "ComponentId" ||
               name == "bBegunPlay" ||
               name == "CurrentLODLevel";
    }

    const Scene::ScenePropertyRecord* FindPropertyRecord(const Scene::SceneObjectRecord& record, const Container::String& name)
    {
        for (const Scene::ScenePropertyRecord& property : record.Properties)
        {
            if (property.Name == name)
            {
                return &property;
            }
        }
        return nullptr;
    }

    void AssertObjectRecordEquivalent(const Scene::SceneObjectRecord& expected, const Scene::SceneObjectRecord& actual)
    {
        assert(expected.ClassName == actual.ClassName);
        assert(expected.ClassId == actual.ClassId);
        for (const Scene::ScenePropertyRecord& property : expected.Properties)
        {
            if (property.Name.empty() || IsRuntimeOnlyPropertyName(property.Name))
            {
                continue; // ObjectId等は再採番仕様（AssignFreshObjectIdsRecursive）のため比較対象外
            }
            const Scene::ScenePropertyRecord* actualProperty = FindPropertyRecord(actual, property.Name);
            assert(actualProperty != nullptr);
            assert(actualProperty->TypeName == property.TypeName);
            assert(actualProperty->Value == property.Value);
        }
    }

    void AssertEntityRecordEquivalent(const Scene::SceneEntityRecord& expected, const Scene::SceneEntityRecord& actual)
    {
        AssertObjectRecordEquivalent(expected.Object, actual.Object);
        assert(expected.Components.size() == actual.Components.size());
        for (size_t index = 0; index < expected.Components.size(); ++index)
        {
            AssertObjectRecordEquivalent(expected.Components[index].Object, actual.Components[index].Object);
        }
        assert(expected.Children.size() == actual.Children.size());
        for (size_t index = 0; index < expected.Children.size(); ++index)
        {
            AssertEntityRecordEquivalent(expected.Children[index], actual.Children[index]);
        }
    }

    void TestSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();

        World sourceWorld;
        sourceWorld.Initialize();

        // root1: Name(要JSONエスケープ) + Position + 子(PointLight Range=64.5) + 孫(pending destroy)
        Entity* root1 = sourceWorld.SpawnEntity<Entity>();
        assert(root1 != nullptr);
        root1->SetLocalPosition(Math::Vector3(1.0f, 2.0f, 3.0f));
        root1->getName() = Container::String("He said \"hi\"\n\tend");

        Entity* child = sourceWorld.SpawnEntity<Entity>(root1);
        assert(child != nullptr);
        Component::PointLightComponent* light = sourceWorld.CreateComponent<Component::PointLightComponent>(child);
        assert(light != nullptr);
        light->SetRange(64.5f);

        Entity* grandchild = sourceWorld.SpawnEntity<Entity>(child);
        assert(grandchild != nullptr);
        grandchild->MarkForDestroy(); // 保存時にフィルタされること

        // root2: pending destroy（保存時にルートごとフィルタされること）
        Entity* root2 = sourceWorld.SpawnEntity<Entity>();
        assert(root2 != nullptr);
        root2->MarkForDestroy();

        // root3: BillboardComponent SizeWorld
        Entity* root3 = sourceWorld.SpawnEntity<Entity>();
        assert(root3 != nullptr);
        Component::BillboardComponent* billboard = sourceWorld.CreateComponent<Component::BillboardComponent>(root3);
        assert(billboard != nullptr);
        billboard->SetSizeWorld(Math::Vector2(2.5f, 4.75f));

        // 保存
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(kSceneFilePath)));

        // 保存時点の期待スナップショット（pendingフィルタ後）を捕捉
        Container::VariableArray<EntitySubtreeSnapshot> sourceRoots;
        assert(Scene::SceneSerializer::CaptureWorld(sourceWorld, sourceRoots) == 2);
        assert(sourceRoots.size() == 2);            // root2はpendingで落ちる
        assert(sourceRoots[0].Root.Children.size() == 1);
        assert(sourceRoots[0].Root.Children[0].Children.empty()); // 孫(pending)は落ちる
        const Scene::SceneDocument sourceDocument = Scene::SceneSerializer::BuildDocument(sourceRoots);

        // 新しいWorldへロード
        World loadedWorld;
        loadedWorld.Initialize();
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(kSceneFilePath), &stats));
        assert(stats.LoadedRoots == 2);
        assert(stats.DroppedEntities == 0);
        assert(stats.DroppedComponents == 0);
        assert(stats.DroppedProperties == 0);

        Container::VariableArray<Entity*> loadedRoots = loadedWorld.GetRootEntities();
        assert(loadedRoots.size() == 2);

        // 値の復元確認
        Entity* loadedRoot1 = loadedRoots[0];
        const Container::String loadedName = loadedRoot1->getName();
        assert(loadedName == "He said \"hi\"\n\tend");
        assert(Near(loadedRoot1->GetLocalTransform().position.x, 1.0f));
        assert(Near(loadedRoot1->GetLocalTransform().position.y, 2.0f));
        assert(Near(loadedRoot1->GetLocalTransform().position.z, 3.0f));

        Container::VariableArray<Entity*> loadedChildren = loadedRoot1->GetChildEntities();
        assert(loadedChildren.size() == 1);
        assert(loadedChildren[0]->GetChildEntities().empty()); // pending孫は復元されない
        Component::PointLightComponent* loadedLight = loadedChildren[0]->GetComponent<Component::PointLightComponent>();
        assert(loadedLight != nullptr);
        assert(Near(loadedLight->GetRange(), 64.5f));

        Component::BillboardComponent* loadedBillboard = loadedRoots[1]->GetComponent<Component::BillboardComponent>();
        assert(loadedBillboard != nullptr);
        assert(loadedBillboard->GetSizeWorld() == Math::Vector2(2.5f, 4.75f));

        // 再スナップショット比較（保存→ロード→再キャプチャが名前つきレコードで一致）
        Container::VariableArray<EntitySubtreeSnapshot> loadedSnapshotRoots;
        assert(Scene::SceneSerializer::CaptureWorld(loadedWorld, loadedSnapshotRoots) == 2);
        const Scene::SceneDocument loadedDocument = Scene::SceneSerializer::BuildDocument(loadedSnapshotRoots);
        assert(loadedDocument.Roots.size() == sourceDocument.Roots.size());
        for (size_t index = 0; index < sourceDocument.Roots.size(); ++index)
        {
            AssertEntityRecordEquivalent(sourceDocument.Roots[index].Root, loadedDocument.Roots[index].Root);
        }

        // 加算ロード＋ObjectId再採番（同一Worldへロードすると既存と異なるIDが振られる）
        const uint64_t originalRoot1Id = root1->GetObjectId();
        assert(Scene::SceneSerializer::LoadIntoWorld(sourceWorld, Container::String(kSceneFilePath), nullptr));
        Container::VariableArray<Entity*> sourceRootsAfterLoad = sourceWorld.GetRootEntities();
        assert(sourceRootsAfterLoad.size() == 5); // root1,root2(pending残),root3 + ロード2
        assert(sourceRootsAfterLoad[3]->GetObjectId() != originalRoot1Id);
        assert(sourceRootsAfterLoad[3]->GetObjectId() != 0);

        loadedWorld.Finalize();
        sourceWorld.Finalize();
        std::remove(kSceneFilePath);
    }

    void AssertTerrainNotBegun(const Component::TerrainComponent& terrain)
    {
        const ClassProperty* property = terrain.GetClass()->GetProperty(Identity("bBegunPlay"));
        assert(property != nullptr);
        PropertyValue value;
        assert(property->GetPropertyValue(&terrain, value));
        const bool* bBegun = value.Get<bool>();
        assert(bBegun != nullptr && !*bBegun);
    }

    EntitySubtreeSnapshot ReadTerrainScene(const char* filePath, bool bLegacy)
    {
        auto stream = NorvesLib::FileStream::FileStream::CreateUnique(
            Container::String(filePath), NorvesLib::FileStream::FileMode::Read,
            NorvesLib::FileStream::FileAccess::Read);
        assert(stream && stream->IsOpen());
        const Container::String json = stream->ReadString();
        Scene::SceneDocument document;
        assert(Scene::SceneSerializer::TryParseJson(json, document));
        assert(document.Roots.size() == 1);
        assert(document.Roots[0].Root.Components.size() == 1);
        const Scene::SceneObjectRecord& terrainRecord = document.Roots[0].Root.Components[0].Object;
        assert(terrainRecord.ClassName == "TerrainComponent");
        for (size_t index = 0; index < sizeof(kTerrainProperties) / sizeof(kTerrainProperties[0]); ++index)
        {
            const bool bPresent = FindPropertyRecord(terrainRecord, kTerrainProperties[index]) != nullptr;
            assert(bPresent == (!bLegacy || index == 0));
        }

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> roots;
        assert(Scene::SceneSerializer::ReconcileWithSchema(document, roots, stats));
        assert(roots.size() == 1 && stats.LoadedRoots == 1);
        assert(stats.DroppedEntities == 0 && stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
        assert(roots[0].Root.Components.size() == 1);
        return roots[0];
    }

    Component::TerrainComponent* RestoreTerrainWithoutBeginPlay(
        Entity& owner, const EntitySubtreeSnapshot& root, bool bLegacy)
    {
        assert(root.Root.Components.size() == 1);
        const ComponentSubtreeSnapshot& component = root.Root.Components[0];
        assert(component.OwnerAlias == root.Root.Alias);
        const IClass* cls = Component::TerrainComponent::StaticClass();
        assert(component.Object.Class == MakeStableSchemaId("NorvesLib", "Class", cls->GetClassName().GetView()));
        auto* terrain = CastTo<Component::TerrainComponent>(cls->NewInstance(nullptr));
        assert(terrain != nullptr);
        assert(owner.AddInner(terrain));
        AssertTerrainNotBegun(*terrain);

        // World への配置は BeginPlay を呼ぶため、同じリフレクション経路で値だけ復元する。
        for (size_t index = 0; index < sizeof(kTerrainProperties) / sizeof(kTerrainProperties[0]); ++index)
        {
            const Identity name(kTerrainProperties[index]);
            const ClassProperty* property = cls->GetProperty(name);
            assert(property != nullptr);
            const StablePropertyId propertyId = MakeStableSchemaId(
                "NorvesLib", "Property", cls->GetClassName().GetView(), name.GetView());
            const ProjectedPropertyValue* projected = nullptr;
            for (const ProjectedPropertyValue& candidate : component.Object.Properties)
            {
                if (candidate.Property == propertyId)
                {
                    assert(projected == nullptr);
                    projected = &candidate;
                }
            }
            if (bLegacy && index != 0)
            {
                assert(projected == nullptr);
                continue;
            }
            assert(projected != nullptr);
            PropertyValue value;
            assert(value.DeserializeStable(projected->Type, projected->SerializedValue));
            assert(property->ApplyValue(terrain, value));
        }
        AssertTerrainNotBegun(*terrain);
        return terrain;
    }

    // 地形の全設定をシーンファイルへ保存し、読み戻した値を確かめる。
    void TestTerrainPropertiesSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();
        (void)Component::TerrainComponent::StaticClass();

        World sourceWorld;
        sourceWorld.Initialize();
        Entity* sourceRoot = sourceWorld.SpawnEntity<Entity>();
        assert(sourceRoot != nullptr);
        auto* source = new Component::TerrainComponent();
        // AddComponent は BeginPlay を呼ぶため、値の保存だけを対象に Inner として付ける。
        assert(sourceRoot->AddInner(source));
        source->SetTerrainPath("Terrain/Example.terrain");
        source->SetTilePathPrefix("Terrain/Example/Tiles/Tile_");
        source->SetBaseColor(0.125f, 0.5f, 0.75f);
        source->SetRoughness(0.375f);
        source->SetCastShadow(false);
        source->SetTilesPerFrame(7u);
        AssertTerrainNotBegun(*source);
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(kTerrainFilePath)));
        AssertTerrainNotBegun(*source);

        const EntitySubtreeSnapshot savedRoot = ReadTerrainScene(kTerrainFilePath, false);
        Entity loadedRoot;
        Component::TerrainComponent* loaded = RestoreTerrainWithoutBeginPlay(loadedRoot, savedRoot, false);
        assert(loaded->GetTerrainPath() == "Terrain/Example.terrain");
        assert(loaded->GetTilePathPrefix() == "Terrain/Example/Tiles/Tile_");
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        loaded->GetBaseColor(r, g, b);
        assert(Near(r, 0.125f) && Near(g, 0.5f) && Near(b, 0.75f));
        assert(Near(loaded->GetRoughness(), 0.375f));
        assert(!loaded->GetCastShadow());
        assert(loaded->GetTilesPerFrame() == 7u);
        AssertTerrainNotBegun(*loaded);

        assert(loadedRoot.RemoveInner(loaded));
        delete loaded;
        sourceWorld.Finalize();
        assert(std::remove(kTerrainFilePath) == 0);
    }

    // 旧ファイルに無い地形設定が既定値になることを確かめる。
    void TestLegacyTerrainPropertiesUseDefaults()
    {
        RegisterRequiredClasses();
        (void)Component::TerrainComponent::StaticClass();

        // 旧形式は TerrainPath だけを持ち、残りの地形設定を省略する。
        const char* legacyJson = R"json({
            "formatVersion": 1, "module": "NorvesLib", "roots": [{
                "formatVersion": 1, "rootAlias": "1", "rootPath": "", "root": {
                    "alias": "1", "parentAlias": "0",
                    "object": {"class": "Entity", "classId": "0", "path": "", "properties": []},
                    "components": [{"alias": "2", "ownerAlias": "1", "object": {
                        "class": "TerrainComponent", "classId": "0", "path": "", "properties": [{
                            "name": "TerrainPath", "propertyId": "0", "type": "String", "typeId": "0",
                            "value": "Terrain/Legacy.terrain"
                        }]
                    }}], "children": []
                }
            }]
        })json";
        std::FILE* file = nullptr;
        fopen_s(&file, kLegacyTerrainFilePath, "wb");
        assert(file != nullptr);
        assert(std::fwrite(legacyJson, 1, std::strlen(legacyJson), file) == std::strlen(legacyJson));
        assert(std::fclose(file) == 0);

        const EntitySubtreeSnapshot savedRoot = ReadTerrainScene(kLegacyTerrainFilePath, true);
        Entity loadedRoot;
        Component::TerrainComponent* loaded = RestoreTerrainWithoutBeginPlay(loadedRoot, savedRoot, true);
        assert(loaded->GetTerrainPath() == "Terrain/Legacy.terrain");
        assert(loaded->GetTilePathPrefix().empty());
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        loaded->GetBaseColor(r, g, b);
        assert(Near(r, 0.30f) && Near(g, 0.27f) && Near(b, 0.22f));
        assert(Near(loaded->GetRoughness(), 0.9f));
        assert(loaded->GetCastShadow());
        assert(loaded->GetTilesPerFrame() == 2u);
        AssertTerrainNotBegun(*loaded);

        assert(loadedRoot.RemoveInner(loaded));
        delete loaded;
        assert(std::remove(kLegacyTerrainFilePath) == 0);
    }

    void TestLoadFailures()
    {
        RegisterRequiredClasses();

        World world;
        world.Initialize();

        // 存在しないファイル
        assert(!Scene::SceneSerializer::LoadIntoWorld(world, Container::String("NoSuchSceneFile.json"), nullptr));

        // 壊れたJSON
        {
            std::FILE* file = nullptr;
            fopen_s(&file, kBrokenFilePath, "wb");
            assert(file != nullptr);
            const char* garbage = "{this is not json";
            std::fwrite(garbage, 1, std::strlen(garbage), file);
            std::fclose(file);
        }
        assert(!Scene::SceneSerializer::LoadIntoWorld(world, Container::String(kBrokenFilePath), nullptr));
        assert(world.GetObjectCount() == 0); // 失敗時にWorldへ何も足されない

        world.Finalize();
        std::remove(kBrokenFilePath);
    }
}

int main()
{
    std::cout << "SceneFileRoundTripTest start\n";

    TestSaveLoadRoundTrip();
    TestTerrainPropertiesSaveLoadRoundTrip();
    TestLegacyTerrainPropertiesUseDefaults();
    TestLoadFailures();

    std::cout << "SceneFileRoundTripTest passed\n";
    return 0;
}
