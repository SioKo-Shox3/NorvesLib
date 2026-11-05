#include "Component/BillboardComponent.h"
#include "Component/Component.h"
#include "Component/GrassFieldComponent.h"
#include "Component/PointLightComponent.h"
#include "Component/SkyOccluderComponent.h"
#include "Component/SpringArmComponent.h"
#include "Component/TerrainComponent.h"
#include "Component/ViewClearanceComponent.h"
#include "Engine/Engine.h"
#include "FileStream/FileStream.h"
#include "GameMode/GameModeScope.h"
#include "Object/PrefabAsset.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include "Scene/SceneSerializer.h"
#include "Text/JsonDocument.h"
#include <cassert>
#include <cerrno>
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

    constexpr const char* kGrassFilePath = "SceneFileRoundTripTest.grass.scene.json";
    constexpr const char* kLegacyGrassFilePath = "SceneFileRoundTripTest.grass-legacy.scene.json";
    constexpr const char* kGrassProperties[] =
    {
        "CookedFieldPath", "SpeciesTablePath", "bUseTerrainGround"
    };

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

    // 切り抜きと近接フェードの全設定を保存し、読み戻した値を確かめる。
    void TestViewClearancePropertiesSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();
        (void)Component::ViewClearanceComponent::StaticClass();
        constexpr const char* filePath = "SceneFileRoundTripTest.view-clearance.scene.json";

        World sourceWorld;
        sourceWorld.Initialize();
        Entity* sourceRoot = sourceWorld.SpawnEntity<Entity>();
        assert(sourceRoot != nullptr);
        auto* source = sourceWorld.CreateComponent<Component::ViewClearanceComponent>(sourceRoot);
        assert(source != nullptr);
        source->getTargetRadius() = 1.25f;
        source->getCameraRadius() = 0.45f;
        source->getFeatherRatio() = 0.75f;
        source->getNearFadeStart() = 0.25f;
        source->getNearFadeEnd() = 0.95f;
        source->getMinHeightScale() = 0.35f;
        source->getbAffectGrass() = false;
        source->getbAffectOccluders() = false;
        source->getbHeroSilhouette() = true;
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(filePath)));

        World loadedWorld;
        loadedWorld.Initialize();
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(filePath), &stats));
        assert(stats.LoadedRoots == 1);
        assert(stats.DroppedEntities == 0);
        assert(stats.DroppedComponents == 0);
        assert(stats.DroppedProperties == 0);
        const Container::VariableArray<Entity*> loadedRoots = loadedWorld.GetRootEntities();
        assert(loadedRoots.size() == 1);
        const auto* loaded = loadedRoots[0]->GetComponent<Component::ViewClearanceComponent>();
        assert(loaded != nullptr);
        assert(Near(loaded->getTargetRadius(), 1.25f));
        assert(Near(loaded->getCameraRadius(), 0.45f));
        assert(Near(loaded->getFeatherRatio(), 0.75f));
        assert(Near(loaded->getNearFadeStart(), 0.25f));
        assert(Near(loaded->getNearFadeEnd(), 0.95f));
        assert(Near(loaded->getMinHeightScale(), 0.35f));
        assert(!loaded->getbAffectGrass());
        assert(!loaded->getbAffectOccluders());
        assert(loaded->getbHeroSilhouette());

        loadedWorld.Finalize();
        sourceWorld.Finalize();
        assert(std::remove(filePath) == 0);
    }

    // 空と雨の遮蔽物の全設定を保存し、読み戻した値を確かめる。
    void TestSkyOccluderPropertiesSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();
        (void)Component::SkyOccluderComponent::StaticClass();
        constexpr const char* filePath = "SceneFileRoundTripTest.sky-occluder.scene.json";

        World sourceWorld;
        sourceWorld.Initialize();
        Entity* sourceRoot = sourceWorld.SpawnEntity<Entity>();
        assert(sourceRoot != nullptr);
        auto* source = sourceWorld.CreateComponent<Component::SkyOccluderComponent>(sourceRoot);
        assert(source != nullptr);
        source->getShape() = Rendering::SkyOccluderShape::Box;
        source->getOccluderClass() = Rendering::SkyOccluderClass::Canopy;
        source->getHalfExtents() = Math::Vector3(2.5f, 3.25f, 4.75f);
        source->getCenterOffset() = Math::Vector3(-0.5f, 1.25f, 2.75f);
        source->getYawOffsetRadians() = 0.625f;
        source->getBaseOffset() = 0.375f;
        source->getOpacity() = 0.45f;
        source->getbDynamic() = true;
        source->getbBlocksRain() = false;
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(filePath)));

        World loadedWorld;
        loadedWorld.Initialize();
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(filePath), &stats));
        assert(stats.LoadedRoots == 1);
        assert(stats.DroppedEntities == 0);
        assert(stats.DroppedComponents == 0);
        assert(stats.DroppedProperties == 0);
        const Container::VariableArray<Entity*> loadedRoots = loadedWorld.GetRootEntities();
        assert(loadedRoots.size() == 1);
        const auto* loaded = loadedRoots[0]->GetComponent<Component::SkyOccluderComponent>();
        assert(loaded != nullptr);
        assert(static_cast<Rendering::SkyOccluderShape>(loaded->getShape()) == Rendering::SkyOccluderShape::Box);
        assert(static_cast<Rendering::SkyOccluderClass>(loaded->getOccluderClass()) == Rendering::SkyOccluderClass::Canopy);
        const Math::Vector3 extents = loaded->getHalfExtents();
        const Math::Vector3 offset = loaded->getCenterOffset();
        assert(Near(extents.x, 2.5f) && Near(extents.y, 3.25f) && Near(extents.z, 4.75f));
        assert(Near(offset.x, -0.5f) && Near(offset.y, 1.25f) && Near(offset.z, 2.75f));
        assert(Near(loaded->getYawOffsetRadians(), 0.625f));
        assert(Near(loaded->getBaseOffset(), 0.375f));
        assert(Near(loaded->getOpacity(), 0.45f));
        assert(loaded->getbDynamic());
        assert(!loaded->getbBlocksRain());

        loadedWorld.Finalize();
        sourceWorld.Finalize();
        assert(std::remove(filePath) == 0);
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

    void AssertGrassNotBegun(const Component::GrassFieldComponent& grass)
    {
        const ClassProperty* property = grass.GetClass()->GetProperty(Identity("bBegunPlay"));
        assert(property != nullptr);
        PropertyValue value;
        assert(property->GetPropertyValue(&grass, value));
        const bool* bBegun = value.Get<bool>();
        assert(bBegun != nullptr && !*bBegun);
    }

    EntitySubtreeSnapshot ReadGrassScene(const char* filePath, bool bLegacy)
    {
        auto stream = NorvesLib::FileStream::FileStream::CreateUnique(
            Container::String(filePath), NorvesLib::FileStream::FileMode::Read,
            NorvesLib::FileStream::FileAccess::Read);
        assert(stream && stream->IsOpen());
        const Container::String json = stream->ReadString();

        // v1 は読み込み時に v2 へ移行するため、移行前のファイル形式も確認する。
        JsonDocument jsonDocument;
        assert(JsonDocument::TryParse(json, jsonDocument));
        const JsonValue jsonRoot = jsonDocument.GetRoot();
        assert(jsonRoot.FindMember("formatVersion").AsUInt32() == (bLegacy ? 1u : Scene::SceneSerializer::SceneFileFormatVersion));
        const JsonValue jsonRoots = jsonRoot.FindMember("roots");
        assert(jsonRoots.IsArray() && jsonRoots.GetArraySize() == 1);
        assert(jsonRoots.GetArrayElement(0).FindMember("formatVersion").AsUInt32() == (bLegacy ? 1u : Scene::SceneSerializer::SceneFileFormatVersion));

        Scene::SceneDocument document;
        assert(Scene::SceneSerializer::TryParseJson(json, document));
        assert(document.FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion && document.Roots.size() == 1);
        assert(document.Roots[0].FormatVersion == Scene::SceneSerializer::SceneFileFormatVersion);
        assert(document.Roots[0].Root.Components.size() == 1);
        const Scene::SceneObjectRecord& grassRecord = document.Roots[0].Root.Components[0].Object;
        assert(grassRecord.ClassName == "GrassFieldComponent");
        for (const char* name : kGrassProperties)
        {
            assert((FindPropertyRecord(grassRecord, name) != nullptr) == !bLegacy);
        }
        if (bLegacy)
        {
            assert(grassRecord.Properties.empty());
        }

        Scene::SceneLoadStats stats;
        Container::VariableArray<EntitySubtreeSnapshot> roots;
        assert(Scene::SceneSerializer::ReconcileWithSchema(document, roots, stats));
        assert(roots.size() == 1 && stats.LoadedRoots == 1);
        assert(stats.DroppedEntities == 0 && stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
        assert(roots[0].Root.Components.size() == 1);
        return roots[0];
    }

    Component::GrassFieldComponent* RestoreGrassWithoutBeginPlay(
        Entity& owner, const EntitySubtreeSnapshot& root, bool bLegacy)
    {
        assert(root.Root.Components.size() == 1);
        const ComponentSubtreeSnapshot& component = root.Root.Components[0];
        assert(component.OwnerAlias == root.Root.Alias);
        const IClass* cls = Component::GrassFieldComponent::StaticClass();
        assert(component.Object.Class == MakeStableSchemaId("NorvesLib", "Class", cls->GetClassName().GetView()));
        auto* grass = CastTo<Component::GrassFieldComponent>(cls->NewInstance(nullptr));
        assert(grass != nullptr);
        assert(owner.AddInner(grass));
        AssertGrassNotBegun(*grass);

        // World への通常の配置は BeginPlay を呼ぶため、リフレクションで値だけ復元する。
        for (const char* propertyName : kGrassProperties)
        {
            const Identity name(propertyName);
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
            if (bLegacy)
            {
                assert(projected == nullptr);
                continue;
            }
            assert(projected != nullptr);
            PropertyValue value;
            assert(value.DeserializeStable(projected->Type, projected->SerializedValue));
            assert(property->ApplyValue(grass, value));
        }
        AssertGrassNotBegun(*grass);
        return grass;
    }

    // 草原の設定を v2 のシーンファイルへ保存し、読み戻した値を確かめる。
    void TestGrassPropertiesSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();
        (void)Component::GrassFieldComponent::StaticClass();

        Engine::Engine engine;
        Engine::Engine* previousEngine = Engine::GEngine;
        Engine::GEngine = &engine;
        assert(engine.GetVegetation().GetGrassField() == nullptr);

        World sourceWorld;
        sourceWorld.Initialize();
        Entity* sourceRoot = sourceWorld.SpawnEntity<Entity>();
        assert(sourceRoot != nullptr);
        auto* source = new Component::GrassFieldComponent();
        assert(sourceRoot->AddInner(source));
        source->SetCookedFieldPath("Assets/Vegetation/Meadow.grass");
        source->SetSpeciesTablePath("Assets/Vegetation/MeadowSpecies.json");
        source->SetUseTerrainGround(false);
        assert(source->GetCookedFieldPath() == "Assets/Vegetation/Meadow.grass");
        assert(source->GetSpeciesTablePath() == "Assets/Vegetation/MeadowSpecies.json");
        assert(!source->GetUseTerrainGround());
        AssertGrassNotBegun(*source);
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(kGrassFilePath)));
        AssertGrassNotBegun(*source);
        assert(engine.GetVegetation().GetGrassField() == nullptr);

        const EntitySubtreeSnapshot savedRoot = ReadGrassScene(kGrassFilePath, false);
        World loadedWorld;
        loadedWorld.Initialize();
        Entity* loadedRoot = loadedWorld.SpawnEntity<Entity>();
        assert(loadedRoot != nullptr);
        Component::GrassFieldComponent* loaded = RestoreGrassWithoutBeginPlay(*loadedRoot, savedRoot, false);
        assert(loaded->GetCookedFieldPath() == source->GetCookedFieldPath());
        assert(loaded->GetSpeciesTablePath() == source->GetSpeciesTablePath());
        assert(loaded->GetUseTerrainGround() == source->GetUseTerrainGround());
        AssertGrassNotBegun(*loaded);
        assert(engine.GetVegetation().GetGrassField() == nullptr);

        assert(loadedRoot->RemoveInner(loaded));
        delete loaded;
        loadedWorld.Finalize();
        sourceWorld.Finalize();
        assert(engine.GetVegetation().GetGrassField() == nullptr);
        Engine::GEngine = previousEngine;
        assert(std::remove(kGrassFilePath) == 0);
    }

    // 旧ファイルに草原の設定が無い場合、コンストラクタの既定値が残ることを確かめる。
    void TestLegacyGrassPropertiesUseDefaults()
    {
        RegisterRequiredClasses();
        (void)Component::GrassFieldComponent::StaticClass();

        // 旧形式のコンポーネントにプロパティを一つも含めない。
        const char* legacyJson = R"json({
            "formatVersion": 1, "module": "NorvesLib", "roots": [{
                "formatVersion": 1, "rootAlias": "1", "rootPath": "", "root": {
                    "alias": "1", "parentAlias": "0",
                    "object": {"class": "Entity", "classId": "0", "path": "", "properties": []},
                    "components": [{"alias": "2", "ownerAlias": "1", "object": {
                        "class": "GrassFieldComponent", "classId": "0", "path": "", "properties": []
                    }}], "children": []
                }
            }]
        })json";
        std::FILE* file = nullptr;
        fopen_s(&file, kLegacyGrassFilePath, "wb");
        assert(file != nullptr);
        assert(std::fwrite(legacyJson, 1, std::strlen(legacyJson), file) == std::strlen(legacyJson));
        assert(std::fclose(file) == 0);

        const EntitySubtreeSnapshot savedRoot = ReadGrassScene(kLegacyGrassFilePath, true);
        Entity loadedRoot;
        Component::GrassFieldComponent* loaded = RestoreGrassWithoutBeginPlay(loadedRoot, savedRoot, true);
        assert(loaded->GetCookedFieldPath().empty());
        assert(loaded->GetSpeciesTablePath() == "Assets/Vegetation/GrassSpecies.json");
        assert(loaded->GetUseTerrainGround());
        AssertGrassNotBegun(*loaded);

        assert(loadedRoot.RemoveInner(loaded));
        delete loaded;
        assert(std::remove(kLegacyGrassFilePath) == 0);
    }

    // 一部の書き込み後に失敗しても元ファイルの全バイトを保ち、一時ファイルを残さない。
    void TestSaveWriteFailurePreservesOriginal()
    {
        RegisterRequiredClasses();
        const Container::String filePath("SceneFileRoundTripTest.atomic.scene.json");
        const Container::String temporaryPath = filePath + ".tmp";
        World world;
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        Scene::SceneSaveStats stats;
        stats.SavedRoots = 99;
        stats.DroppedProperties = 99;
        assert(Scene::SceneSerializer::SaveToFile(world, filePath, &stats));
        assert(stats.SavedRoots == 1 && stats.DroppedProperties == 0);

        auto readBytes = [&filePath]()
        {
            auto stream = NorvesLib::FileStream::FileStream::CreateUnique(
                filePath, NorvesLib::FileStream::FileMode::Read, NorvesLib::FileStream::FileAccess::Read);
            assert(stream && stream->IsOpen());
            Container::VariableArray<unsigned char> bytes;
            bytes.resize(static_cast<size_t>(stream->GetSize()));
            assert(stream->Read(bytes.data(), bytes.size()) == bytes.size());
            return bytes;
        };
        auto assertNoTemporaryFile = [&temporaryPath]()
        {
            std::FILE* file = nullptr;
            const errno_t result = fopen_s(&file, temporaryPath.c_str(), "rb");
            if (file)
            {
                std::fclose(file);
            }
            assert(result == ENOENT && file == nullptr);
        };
        const auto original = readBytes();
        assert(!original.empty());
        assertNoTemporaryFile();
        root->getName() = Container::String("変更後のシーン");
        Scene::SceneSaveOptions options;
        options.bInjectWriteFailureForTesting = true;
        stats.SavedRoots = 99;
        stats.DroppedProperties = 99;
        assert(!Scene::SceneSerializer::SaveToFile(world, filePath, &stats, options));
        assert(stats.SavedRoots == 1 && stats.DroppedProperties == 0);
        const auto preserved = readBytes();
        assert(preserved.size() == original.size());
        assert(std::memcmp(original.data(), preserved.data(), original.size()) == 0);
        assertNoTemporaryFile();
        assert(Scene::SceneSerializer::SaveToFile(world, filePath, &stats));
        const auto replaced = readBytes();
        assert(replaced.size() != original.size() ||
            std::memcmp(replaced.data(), original.data(), original.size()) != 0);
        assertNoTemporaryFile();
        world.Finalize();
        assert(std::remove(filePath.c_str()) == 0);
    }

    // 同じルート内とルート間の参照が、ObjectIdの再採番後も同じEntityを指すことを確かめる。
    void TestEntityReferencesSaveLoadRoundTrip()
    {
        RegisterRequiredClasses();
        (void)Component::SpringArmComponent::StaticClass();
        const Container::String filePath("SceneFileRoundTripTest.entity-ref.scene.json");
        for (const bool bCrossRoot : {true, false})
        {
            World sourceWorld;
            sourceWorld.Initialize();
            Entity* owner = sourceWorld.SpawnEntity<Entity>();
            assert(owner != nullptr);
            auto* arm = sourceWorld.CreateComponent<Component::SpringArmComponent>(owner);
            assert(arm != nullptr);
            Entity* pivot = sourceWorld.SpawnEntity<Entity>(bCrossRoot ? nullptr : owner);
            assert(pivot != nullptr);
            assert(arm->SetPivot(pivot));
            const uint64_t savedPivotId = pivot->GetObjectId();
            assert(Scene::SceneSerializer::SaveToFile(sourceWorld, filePath));

            {
                auto stream = NorvesLib::FileStream::FileStream::CreateUnique(
                    filePath, NorvesLib::FileStream::FileMode::Read, NorvesLib::FileStream::FileAccess::Read);
                assert(stream && stream->IsOpen());
                Scene::SceneDocument document;
                assert(Scene::SceneSerializer::TryParseJson(stream->ReadString(), document));
                assert(document.Roots.size() == (bCrossRoot ? 2u : 1u));
                const auto& ownerRecord = document.Roots[0].Root;
                assert(ownerRecord.Components.size() == 1);
                const auto* reference = FindPropertyRecord(ownerRecord.Components[0].Object, "PivotObjectId");
                assert(reference != nullptr && reference->TypeName == "EntityRefId");
                const auto& pivotRecord = bCrossRoot ? document.Roots[1].Root : ownerRecord.Children[0];
                char expected[64] = {};
                std::snprintf(expected, sizeof(expected), "%u:%llu", bCrossRoot ? 1u : 0u,
                              static_cast<unsigned long long>(pivotRecord.Alias));
                assert(reference->Value == expected);
            }

            World loadedWorld;
            loadedWorld.Initialize();
            Entity* existing = loadedWorld.SpawnEntity<Entity>();
            assert(existing != nullptr);
            Scene::SceneLoadStats stats;
            assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, filePath, &stats));
            assert(stats.LoadedRoots == (bCrossRoot ? 2u : 1u));
            assert(stats.DroppedEntities == 0 && stats.DroppedComponents == 0 && stats.DroppedProperties == 0);
            const auto loadedRoots = loadedWorld.GetRootEntities();
            assert(loadedRoots.size() == (bCrossRoot ? 3u : 2u) && loadedRoots[0] == existing);
            Entity* loadedOwner = loadedRoots[1];
            Entity* loadedPivot = bCrossRoot ? loadedRoots[2] : loadedOwner->GetChildEntities()[0];
            const auto* loadedArm = loadedOwner->GetComponent<Component::SpringArmComponent>();
            assert(loadedArm != nullptr);
            assert(loadedPivot->GetObjectId() != savedPivotId);
            assert(loadedArm->GetPivotObjectId() == loadedPivot->GetObjectId());
            assert(loadedArm->ResolvePivot() == loadedPivot);
            loadedWorld.Finalize();
            sourceWorld.Finalize();
        }
        assert(std::remove(filePath.c_str()) == 0);
    }

    // v2のObjectId参照を移行し、文書と生成後の両方で参照先を保つことを確かめる。
    void TestV2EntityReferenceMigration()
    {
        RegisterRequiredClasses();
        (void)Component::SpringArmComponent::StaticClass();
        const char* filePath = "SceneFileRoundTripTest.entity-ref-v2.scene.json";
        const char* legacyJson = R"json({
            "formatVersion": 2, "module": "NorvesLib", "roots": [{
                "formatVersion": 2, "rootAlias": "41", "rootPath": "", "root": {
                    "alias": "41", "parentAlias": "0",
                    "object": {"class": "Entity", "classId": "0", "path": "", "properties": [{
                        "name": "ObjectId", "propertyId": "0", "type": "uint64", "typeId": "0", "value": "123"
                    }]},
                    "components": [{"alias": "89", "ownerAlias": "41", "object": {
                        "class": "SpringArmComponent", "classId": "0", "path": "", "properties": [{
                            "name": "PivotObjectId", "propertyId": "0", "type": "uint64", "typeId": "0", "value": "456"
                        }]
                    }}], "children": []
                }
            }, {
                "formatVersion": 2, "rootAlias": "77", "rootPath": "", "root": {
                    "alias": "77", "parentAlias": "0",
                    "object": {"class": "Entity", "classId": "0", "path": "", "properties": [{
                        "name": "ObjectId", "propertyId": "0", "type": "uint64", "typeId": "0", "value": "456"
                    }]}, "components": [], "children": []
                }
            }]
        })json";
        std::FILE* file = nullptr;
        fopen_s(&file, filePath, "wb");
        assert(file != nullptr);
        assert(std::fwrite(legacyJson, 1, std::strlen(legacyJson), file) == std::strlen(legacyJson));
        assert(std::fclose(file) == 0);

        Scene::SceneDocument document;
        assert(Scene::SceneSerializer::TryParseJson(Container::String(legacyJson), document));
        assert(document.FormatVersion == 3 && document.Roots.size() == 2);
        assert(document.Roots[0].FormatVersion == 3 && document.Roots[1].FormatVersion == 3);
        const auto* reference = FindPropertyRecord(document.Roots[0].Root.Components[0].Object, "PivotObjectId");
        const TypeInfo* referenceType = TypeRegistry::Get().Find(TypeRegistry::Get().GetTypeId<EntityRefId>());
        assert(referenceType != nullptr && reference != nullptr);
        assert(reference->TypeName == "EntityRefId" && reference->TypeId == referenceType->StableId);
        assert(reference->Value == "1:77");

        World loadedWorld;
        loadedWorld.Initialize();
        assert(loadedWorld.SpawnEntity<Entity>() != nullptr);
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(filePath), &stats));
        assert(stats.LoadedRoots == 2 && stats.DroppedProperties == 0);
        const auto loadedRoots = loadedWorld.GetRootEntities();
        assert(loadedRoots.size() == 3);
        const auto* loadedArm = loadedRoots[1]->GetComponent<Component::SpringArmComponent>();
        assert(loadedArm != nullptr);
        assert(loadedArm->GetPivotObjectId() == loadedRoots[2]->GetObjectId());
        assert(loadedArm->ResolvePivot() == loadedRoots[2]);
        loadedWorld.Finalize();
        assert(std::remove(filePath) == 0);
    }

    void TestLoadedRootsCleanup()
    {
        RegisterRequiredClasses();
        constexpr const char* filePath = "SceneFileRoundTripTest.scope.scene.json";

        World sourceWorld;
        sourceWorld.Initialize();
        Entity* first = sourceWorld.SpawnEntity<Entity>();
        Entity* second = sourceWorld.SpawnEntity<Entity>();
        assert(first != nullptr && second != nullptr);
        first->getName() = Container::String("FirstRoot");
        second->getName() = Container::String("SecondRoot");
        Entity* child = sourceWorld.SpawnEntity<Entity>(first);
        assert(child != nullptr);
        child->getName() = Container::String("Child");
        assert(Scene::SceneSerializer::SaveToFile(sourceWorld, Container::String(filePath)));

        World loadedWorld;
        loadedWorld.Initialize();
        Entity* existing = loadedWorld.SpawnEntity<Entity>();
        assert(existing != nullptr);
        const uint64_t existingId = existing->GetObjectId();
        const size_t originalCount = loadedWorld.GetObjectCount();

        Container::VariableArray<Entity*> roots;
        roots.push_back(existing);
        Scene::SceneLoadStats stats;
        assert(Scene::SceneSerializer::LoadIntoWorld(loadedWorld, Container::String(filePath), &stats, &roots));
        assert(stats.LoadedRoots == 2);
        assert(roots.size() == 2);
        assert(roots[0] != nullptr && roots[1] != nullptr);
        assert(roots[0] != existing && roots[1] != existing && roots[0] != roots[1]);
        const Container::String firstName = roots[0]->getName();
        const Container::String secondName = roots[1]->getName();
        assert(firstName == "FirstRoot");
        assert(secondName == "SecondRoot");
        assert(roots[0]->GetParentEntity() == nullptr && roots[1]->GetParentEntity() == nullptr);
        const Container::VariableArray<Entity*> loadedChildren = roots[0]->GetChildEntities();
        assert(loadedChildren.size() == 1 && loadedChildren[0] != nullptr);
        const Container::String childName = loadedChildren[0]->getName();
        assert(childName == "Child");
        assert(loadedChildren[0]->GetParentEntity() == roots[0]);
        assert(roots[1]->GetChildEntities().empty());

        const uint64_t firstId = roots[0]->GetObjectId();
        const uint64_t secondId = roots[1]->GetObjectId();
        const uint64_t childId = loadedChildren[0]->GetObjectId();
        assert(loadedWorld.FindEntityByObjectId(firstId) == roots[0]);
        assert(loadedWorld.FindEntityByObjectId(secondId) == roots[1]);
        assert(loadedWorld.FindEntityByObjectId(childId) == loadedChildren[0]);
        assert(loadedWorld.GetObjectCount() == originalCount + 2);

        GameMode::GameModeScope scope(&loadedWorld, nullptr);
        for (Entity* root : roots)
        {
            scope.TrackObject(root);
        }
        scope.Cleanup();
        assert(scope.IsEmpty());
        assert(loadedWorld.FindEntityByObjectId(firstId) == nullptr);
        assert(loadedWorld.FindEntityByObjectId(secondId) == nullptr);
        assert(loadedWorld.FindEntityByObjectId(childId) == nullptr);
        assert(loadedWorld.FindEntityByObjectId(existingId) == existing);
        assert(loadedWorld.GetObjectCount() == originalCount);

        loadedWorld.Finalize();
        sourceWorld.Finalize();
        assert(std::remove(filePath) == 0);
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
    TestViewClearancePropertiesSaveLoadRoundTrip();
    TestSkyOccluderPropertiesSaveLoadRoundTrip();
    TestTerrainPropertiesSaveLoadRoundTrip();
    TestLegacyTerrainPropertiesUseDefaults();
    TestGrassPropertiesSaveLoadRoundTrip();
    TestLegacyGrassPropertiesUseDefaults();
    TestSaveWriteFailurePreservesOriginal();
    TestEntityReferencesSaveLoadRoundTrip();
    TestV2EntityReferenceMigration();
    TestLoadedRootsCleanup();
    TestLoadFailures();

    std::cout << "SceneFileRoundTripTest passed\n";
    return 0;
}
