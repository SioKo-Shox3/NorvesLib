#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Component/BoardComponent.h"
#include "Object/PrefabAsset.h"
#include "Object/ResourceRegistry.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
namespace Component = NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;

namespace
{
    constexpr float Epsilon = 0.0001f;

    class TrackingBeginComponent : public NorvesLib::Core::Component::Component
    {
        REFLECTION_CLASS(TrackingBeginComponent, NorvesLib::Core::Component::Component)

    public:
        static int BeginTransitions;

        void BeginPlay() override
        {
            const bool bWasBegun = bBegunPlay;
            NorvesLib::Core::Component::Component::BeginPlay();
            if (!bWasBegun && bBegunPlay)
            {
                ++BeginTransitions;
            }
        }
    };

    IMPLEMENT_CLASS(TrackingBeginComponent, NorvesLib::Core::Component::Component)

    int TrackingBeginComponent::BeginTransitions = 0;

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    void ExpectVectorNear(const Math::Vector3& lhs, const Math::Vector3& rhs)
    {
        assert(Near(lhs.x, rhs.x));
        assert(Near(lhs.y, rhs.y));
        assert(Near(lhs.z, rhs.z));
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

    StableClassId MakeClassId(const IClass* cls)
    {
        assert(cls != nullptr);
        return MakeStableSchemaId("NorvesLib", "Class", cls->GetClassName().GetView());
    }

    StableTypeId MakeTypeId(const char* typeName)
    {
        return MakeStableSchemaId("NorvesLib", "Type", Container::StringView(typeName));
    }

    ProjectedPropertyValue MakeProjectedValue(
        const IClass* cls,
        const char* propertyName,
        const char* typeName,
        const char* serializedValue)
    {
        ProjectedPropertyValue value;
        value.Property = MakePropertyId(cls, propertyName);
        value.Type = MakeTypeId(typeName);
        value.SerializedValue = serializedValue;
        return value;
    }

    PrefabPropertyOverride MakeOverride(
        SubtreeSnapshotAliasId alias,
        const ProjectedPropertyValue& value)
    {
        PrefabPropertyOverride overrideValue;
        overrideValue.Target.Alias = alias;
        overrideValue.Value = value;
        return overrideValue;
    }

    PrefabPropertyOverride MakeNestedOverride(
        PrefabNestedInstanceId nestedInstanceId,
        SubtreeSnapshotAliasId alias,
        const ProjectedPropertyValue& value)
    {
        PrefabPropertyOverride overrideValue;
        overrideValue.Target.NestedInstances.push_back(nestedInstanceId);
        overrideValue.Target.Alias = alias;
        overrideValue.Value = value;
        return overrideValue;
    }

    bool ContainsRangeOverride(
        const PrefabOverrideSet& overrides,
        SubtreeSnapshotAliasId alias,
        const char* serializedValue)
    {
        const StablePropertyId rangeProperty = MakePropertyId(Component::PointLightComponent::StaticClass(), "Range");
        for (const PrefabPropertyOverride& overrideValue : overrides.Properties)
        {
            if (overrideValue.Target.Alias == alias &&
                overrideValue.Target.NestedInstances.empty() &&
                overrideValue.Value.Property == rangeProperty &&
                overrideValue.Value.SerializedValue == serializedValue)
            {
                return true;
            }
        }
        return false;
    }

    bool ContainsNestedRangeOverride(
        const PrefabOverrideSet& overrides,
        PrefabNestedInstanceId nestedInstanceId,
        SubtreeSnapshotAliasId alias,
        const char* serializedValue)
    {
        const StablePropertyId rangeProperty = MakePropertyId(Component::PointLightComponent::StaticClass(), "Range");
        for (const PrefabPropertyOverride& overrideValue : overrides.Properties)
        {
            if (overrideValue.Target.Alias == alias &&
                overrideValue.Target.NestedInstances.size() == 1 &&
                overrideValue.Target.NestedInstances[0] == nestedInstanceId &&
                overrideValue.Value.Property == rangeProperty &&
                overrideValue.Value.SerializedValue == serializedValue)
            {
                return true;
            }
        }
        return false;
    }

    ResourceRef<PrefabAsset> MakePrefabRef(ResourceRegistry& registry, const Container::TSharedPtr<PrefabAsset>& prefab)
    {
        ResourceRef<PrefabAsset> ref;
        ref.Set(registry, registry.GetHandle<PrefabAsset>(prefab->GetResourceId()));
        return ref;
    }

    void RegisterRequiredClasses()
    {
        (void)Entity::StaticClass();
        (void)Component::Component::StaticClass();
        (void)Component::BoardComponent::StaticClass();
        (void)Component::PointLightComponent::StaticClass();
        (void)PrefabAsset::StaticClass();
        (void)TrackingBeginComponent::StaticClass();
    }

    void TestSpawnPrefabRegistersPropertyTypes()
    {
        RegisterRequiredClasses();

        ResourceRegistry registry;
        assert(registry.Initialize());

        World world;
        world.Initialize();

        EntitySubtreeSnapshot snapshot;
        snapshot.RootAlias = 1;
        snapshot.Root.Alias = snapshot.RootAlias;
        snapshot.Root.Object.Class = MakeClassId(Entity::StaticClass());
        snapshot.Root.Object.Properties.push_back(MakeProjectedValue(
            Entity::StaticClass(),
            "Position",
            "Math::Vector3",
            "Vector3(10,20,30)"));
        snapshot.Root.Object.Properties.push_back(MakeProjectedValue(
            Entity::StaticClass(),
            "Rotation",
            "Math::Quaternion",
            "Quaternion(0,0,0,1)"));

        ComponentSubtreeSnapshot lightSnapshot;
        lightSnapshot.Alias = 2;
        lightSnapshot.OwnerAlias = snapshot.Root.Alias;
        lightSnapshot.Object.Class = MakeClassId(Component::PointLightComponent::StaticClass());
        lightSnapshot.Object.Properties.push_back(MakeProjectedValue(
            Component::PointLightComponent::StaticClass(),
            "Range",
            "float",
            "17"));
        snapshot.Root.Components.push_back(lightSnapshot);

        auto prefab = registry.CreateTransient<PrefabAsset>("TypePrewarmPrefab");
        assert(prefab != nullptr);
        prefab->SetTree(snapshot);

        Entity* spawnedRoot = world.SpawnPrefab(*prefab);
        assert(spawnedRoot != nullptr);
        ExpectVectorNear(spawnedRoot->GetLocalTransform().position, Math::Vector3(10.0f, 20.0f, 30.0f));

        Component::PointLightComponent* spawnedLight = spawnedRoot->GetComponent<Component::PointLightComponent>();
        assert(spawnedLight != nullptr);
        assert(Near(spawnedLight->GetRange(), 17.0f));

        world.Finalize();
        prefab.reset();
        registry.Shutdown();
    }

    void TestSpawnPrefabRestoresBoardComponentVisualProperties()
    {
        RegisterRequiredClasses();

        ResourceRegistry registry;
        assert(registry.Initialize());

        World world;
        world.Initialize();

        EntitySubtreeSnapshot snapshot;
        snapshot.RootAlias = 1;
        snapshot.Root.Alias = snapshot.RootAlias;
        snapshot.Root.Object.Class = MakeClassId(Entity::StaticClass());

        ComponentSubtreeSnapshot boardSnapshot;
        boardSnapshot.Alias = 2;
        boardSnapshot.OwnerAlias = snapshot.Root.Alias;
        boardSnapshot.Object.Class = MakeClassId(Component::BoardComponent::StaticClass());
        boardSnapshot.Object.Properties.push_back(MakeProjectedValue(
            Component::BoardComponent::StaticClass(),
            "Tint",
            "Math::Vector4",
            "Vector4(0.1,0.2,0.3,0.4)"));
        boardSnapshot.Object.Properties.push_back(MakeProjectedValue(
            Component::BoardComponent::StaticClass(),
            "Pivot",
            "Math::Vector2",
            "Vector2(0.25,0.75)"));
        boardSnapshot.Object.Properties.push_back(MakeProjectedValue(
            Component::BoardComponent::StaticClass(),
            "SizePx",
            "Math::Vector2",
            "Vector2(96,48)"));
        boardSnapshot.Object.Properties.push_back(MakeProjectedValue(
            Component::BoardComponent::StaticClass(),
            "UVRectProp",
            "Math::Vector4",
            "Vector4(0.5,0,0.25,0.5)"));
        snapshot.Root.Components.push_back(boardSnapshot);

        auto prefab = registry.CreateTransient<PrefabAsset>("BoardVisualPrefab");
        assert(prefab != nullptr);
        prefab->SetTree(snapshot);

        Entity* spawnedRoot = world.SpawnPrefab(*prefab);
        assert(spawnedRoot != nullptr);

        Component::BoardComponent* spawnedBoard = spawnedRoot->GetComponent<Component::BoardComponent>();
        assert(spawnedBoard != nullptr);
        assert(spawnedBoard->GetTint() == Math::Vector4(0.1f, 0.2f, 0.3f, 0.4f));
        assert(spawnedBoard->GetPivot() == Math::Vector2(0.25f, 0.75f));
        assert(spawnedBoard->GetSizePx() == Math::Vector2(96.0f, 48.0f));
        assert(spawnedBoard->GetUVRect() == Math::Vector4(0.5f, 0.0f, 0.25f, 0.5f));

        world.Finalize();
        prefab.reset();
        registry.Shutdown();
    }

    void TestPrefabRoundTrip()
    {
        RegisterRequiredClasses();

        ResourceRegistry registry;
        assert(registry.Initialize());

        World world;
        world.Initialize();

        Entity* sourceRoot = world.SpawnEntity<Entity>();
        assert(sourceRoot != nullptr);
        sourceRoot->SetLocalPosition(Math::Vector3(1.0f, 2.0f, 3.0f));

        TrackingBeginComponent* sourceTracker = world.CreateComponent<TrackingBeginComponent>(sourceRoot);
        assert(sourceTracker != nullptr);

        Entity* sourceChild = world.SpawnEntity<Entity>(sourceRoot);
        assert(sourceChild != nullptr);
        sourceChild->SetLocalPosition(Math::Vector3(4.0f, 5.0f, 6.0f));

        Component::PointLightComponent* sourceLight = world.CreateComponent<Component::PointLightComponent>(sourceChild);
        assert(sourceLight != nullptr);
        sourceLight->SetRange(12.0f);

        Entity* sourceGrandchild = world.SpawnEntity<Entity>(sourceChild);
        assert(sourceGrandchild != nullptr);
        sourceGrandchild->SetLocalScale(Math::Vector3(2.0f, 3.0f, 4.0f));
        sourceGrandchild->MarkForDestroy();

        const uint64_t sourceRootId = sourceRoot->GetObjectId();
        const uint64_t sourceChildId = sourceChild->GetObjectId();
        const uint64_t sourceGrandchildId = sourceGrandchild->GetObjectId();
        const uint64_t sourceTrackerId = sourceTracker->GetComponentId();
        const uint64_t sourceLightId = sourceLight->GetComponentId();

        EntitySubtreeSnapshot sourceSnapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*sourceRoot);
        assert(sourceSnapshot.Root.Components.size() == 1);
        assert(sourceSnapshot.Root.Children.size() == 1);
        const EntitySubtreeSnapshotNode& sourceChildNode = sourceSnapshot.Root.Children[0];
        assert(sourceChildNode.Components.size() == 1);
        assert(sourceChildNode.Children.size() == 1);
        const ComponentSubtreeSnapshot& sourceLightSnapshot = sourceChildNode.Components[0];

        auto prefab = registry.CreateTransient<PrefabAsset>("RoundTripPrefab");
        assert(prefab != nullptr);
        prefab->SetTree(sourceSnapshot);

        World nestedWorld;
        nestedWorld.Initialize();

        Entity* nestedSourceRoot = nestedWorld.SpawnEntity<Entity>();
        assert(nestedSourceRoot != nullptr);
        nestedSourceRoot->SetLocalPosition(Math::Vector3(7.0f, 8.0f, 9.0f));

        Component::PointLightComponent* nestedSourceLight = nestedWorld.CreateComponent<Component::PointLightComponent>(nestedSourceRoot);
        assert(nestedSourceLight != nullptr);
        nestedSourceLight->SetRange(5.0f);

        const uint64_t nestedSourceLightId = nestedSourceLight->GetComponentId();
        EntitySubtreeSnapshot nestedSnapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*nestedSourceRoot);
        assert(nestedSnapshot.Root.Components.size() == 1);
        const ComponentSubtreeSnapshot& nestedLightSnapshot = nestedSnapshot.Root.Components[0];

        auto nestedPrefab = registry.CreateTransient<PrefabAsset>("NestedPrefab");
        assert(nestedPrefab != nullptr);
        nestedPrefab->SetTree(nestedSnapshot);

        PrefabOverrideSet nestedStoredOverrides;
        nestedStoredOverrides.Properties.push_back(MakeOverride(
            nestedLightSnapshot.Alias,
            MakeProjectedValue(Component::PointLightComponent::StaticClass(), "Range", "float", "22")));

        const PrefabNestedInstanceId nestedInstanceId = prefab->AddNestedPrefab(
            sourceChildNode.Alias,
            MakePrefabRef(registry, nestedPrefab),
            nestedStoredOverrides);
        assert(nestedInstanceId != InvalidPrefabNestedInstanceId);

        PrefabOverrideSet callerOverrides;
        callerOverrides.Properties.push_back(MakeOverride(
            sourceLightSnapshot.Alias,
            MakeProjectedValue(Component::PointLightComponent::StaticClass(), "Range", "float", "64.5")));
        callerOverrides.Properties.push_back(MakeNestedOverride(
            nestedInstanceId,
            nestedLightSnapshot.Alias,
            MakeProjectedValue(Component::PointLightComponent::StaticClass(), "Range", "float", "33")));

        TrackingBeginComponent::BeginTransitions = 0;
        const size_t rootCountBeforeSpawn = world.GetObjectCount();
        Entity* spawnedRoot = world.SpawnPrefab(*prefab, nullptr, &callerOverrides);
        assert(spawnedRoot != nullptr);
        assert(world.GetObjectCount() == rootCountBeforeSpawn + 1);
        assert(TrackingBeginComponent::BeginTransitions == 1);

        assert(spawnedRoot->GetObjectId() != sourceRootId);
        assert(spawnedRoot->GetObjectId() != 0);

        TrackingBeginComponent* spawnedTracker = spawnedRoot->GetComponent<TrackingBeginComponent>();
        assert(spawnedTracker != nullptr);
        assert(spawnedTracker->GetComponentId() != sourceTrackerId);

        Container::VariableArray<Entity*> spawnedRootChildren = spawnedRoot->GetChildEntities();
        assert(spawnedRootChildren.size() == 1);
        Entity* spawnedChild = spawnedRootChildren[0];
        assert(spawnedChild->GetObjectId() != sourceChildId);
        assert(spawnedChild->GetObjectId() != spawnedRoot->GetObjectId());

        Component::PointLightComponent* spawnedLight = spawnedChild->GetComponent<Component::PointLightComponent>();
        assert(spawnedLight != nullptr);
        assert(spawnedLight->GetComponentId() != sourceLightId);
        assert(Near(spawnedLight->GetRange(), 64.5f));

        Container::VariableArray<Entity*> spawnedChildChildren = spawnedChild->GetChildEntities();
        assert(spawnedChildChildren.size() == 2);
        Entity* spawnedGrandchild = spawnedChildChildren[0];
        Entity* spawnedNestedRoot = spawnedChildChildren[1];
        assert(spawnedGrandchild->GetObjectId() != sourceGrandchildId);
        assert(spawnedGrandchild->GetObjectId() != spawnedChild->GetObjectId());
        assert(!spawnedGrandchild->IsPendingDestroy());

        Component::PointLightComponent* spawnedNestedLight = spawnedNestedRoot->GetComponent<Component::PointLightComponent>();
        assert(spawnedNestedLight != nullptr);
        assert(spawnedNestedLight->GetComponentId() != nestedSourceLightId);
        assert(Near(spawnedNestedLight->GetRange(), 33.0f));

        world.UpdateWorldTransforms();
        ExpectVectorNear(spawnedRoot->GetWorldTransform().position, Math::Vector3(1.0f, 2.0f, 3.0f));
        ExpectVectorNear(spawnedChild->GetWorldTransform().position, Math::Vector3(5.0f, 7.0f, 9.0f));
        ExpectVectorNear(spawnedGrandchild->GetWorldTransform().scale, Math::Vector3(2.0f, 3.0f, 4.0f));
        ExpectVectorNear(spawnedNestedRoot->GetWorldTransform().position, Math::Vector3(12.0f, 15.0f, 18.0f));

        PrefabOverrideSet diffOverrides = prefab->BuildOverridesFrom(*spawnedRoot);
        assert(ContainsRangeOverride(diffOverrides, sourceLightSnapshot.Alias, "64.5"));
        assert(ContainsNestedRangeOverride(diffOverrides, nestedInstanceId, nestedLightSnapshot.Alias, "33"));

        Entity* storedOnlyRoot = world.SpawnPrefab(*prefab);
        assert(storedOnlyRoot != nullptr);
        Container::VariableArray<Entity*> storedOnlyRootChildren = storedOnlyRoot->GetChildEntities();
        assert(storedOnlyRootChildren.size() == 1);
        Container::VariableArray<Entity*> storedOnlyChildChildren = storedOnlyRootChildren[0]->GetChildEntities();
        assert(storedOnlyChildChildren.size() == 2);
        Component::PointLightComponent* storedOnlyNestedLight =
            storedOnlyChildChildren[1]->GetComponent<Component::PointLightComponent>();
        assert(storedOnlyNestedLight != nullptr);
        assert(Near(storedOnlyNestedLight->GetRange(), 22.0f));

        PrefabOverrideSet storedOnlyDiffOverrides = prefab->BuildOverridesFrom(*storedOnlyRoot);
        assert(!ContainsNestedRangeOverride(storedOnlyDiffOverrides, nestedInstanceId, nestedLightSnapshot.Alias, "22"));

        auto invalidClassPrefab = registry.CreateTransient<PrefabAsset>("InvalidClassPrefab");
        assert(invalidClassPrefab != nullptr);
        EntitySubtreeSnapshot invalidSnapshot = sourceSnapshot;
        invalidSnapshot.Root.Object.Class = 999999999;
        invalidClassPrefab->SetTree(invalidSnapshot);

        const size_t rootCountBeforeInvalidSpawn = world.GetObjectCount();
        assert(world.SpawnPrefab(*invalidClassPrefab) == nullptr);
        assert(world.GetObjectCount() == rootCountBeforeInvalidSpawn);

        auto cyclePrefab = registry.CreateTransient<PrefabAsset>("CyclePrefab");
        assert(cyclePrefab != nullptr);
        cyclePrefab->SetTree(nestedSnapshot);
        const PrefabNestedInstanceId cycleInstanceId = cyclePrefab->AddNestedPrefab(
            cyclePrefab->GetTree().RootAlias,
            MakePrefabRef(registry, cyclePrefab),
            PrefabOverrideSet());
        assert(cycleInstanceId == InvalidPrefabNestedInstanceId);
        assert(cyclePrefab->GetNestedPrefabs().empty());

        const size_t rootCountBeforeCycleSpawn = world.GetObjectCount();
        Entity* spawnedCycleRoot = world.SpawnPrefab(*cyclePrefab);
        assert(spawnedCycleRoot != nullptr);
        assert(world.GetObjectCount() == rootCountBeforeCycleSpawn + 1);

        auto indirectParentPrefab = registry.CreateTransient<PrefabAsset>("IndirectParentPrefab");
        assert(indirectParentPrefab != nullptr);
        indirectParentPrefab->SetTree(nestedSnapshot);

        auto indirectChildPrefab = registry.CreateTransient<PrefabAsset>("IndirectChildPrefab");
        assert(indirectChildPrefab != nullptr);
        indirectChildPrefab->SetTree(nestedSnapshot);

        const PrefabNestedInstanceId indirectChildInstanceId = indirectParentPrefab->AddNestedPrefab(
            indirectParentPrefab->GetTree().RootAlias,
            MakePrefabRef(registry, indirectChildPrefab),
            PrefabOverrideSet());
        assert(indirectChildInstanceId != InvalidPrefabNestedInstanceId);

        const PrefabNestedInstanceId indirectCycleInstanceId = indirectChildPrefab->AddNestedPrefab(
            indirectChildPrefab->GetTree().RootAlias,
            MakePrefabRef(registry, indirectParentPrefab),
            PrefabOverrideSet());
        assert(indirectCycleInstanceId == InvalidPrefabNestedInstanceId);
        assert(indirectChildPrefab->GetNestedPrefabs().empty());

        Container::VariableArray<Container::TSharedPtr<PrefabAsset>> deepPrefabs;
        for (size_t index = 0; index < 17; ++index)
        {
            auto deepPrefab = registry.CreateTransient<PrefabAsset>("DeepPrefab");
            assert(deepPrefab != nullptr);
            deepPrefab->SetTree(nestedSnapshot);
            deepPrefabs.push_back(deepPrefab);
        }

        for (size_t index = deepPrefabs.size() - 1; index > 0; --index)
        {
            const PrefabNestedInstanceId deepInstanceId = deepPrefabs[index - 1]->AddNestedPrefab(
                deepPrefabs[index - 1]->GetTree().RootAlias,
                MakePrefabRef(registry, deepPrefabs[index]),
                PrefabOverrideSet());
            assert(deepInstanceId != InvalidPrefabNestedInstanceId);
        }

        const size_t rootCountBeforeDeepSpawn = world.GetObjectCount();
        assert(world.SpawnPrefab(*deepPrefabs[0]) == nullptr);
        assert(world.GetObjectCount() == rootCountBeforeDeepSpawn);

        world.Finalize();
        nestedWorld.Finalize();

        deepPrefabs.clear();
        indirectChildPrefab.reset();
        indirectParentPrefab.reset();
        cyclePrefab.reset();
        invalidClassPrefab.reset();
        nestedPrefab.reset();
        prefab.reset();
        registry.Shutdown();
    }
}

int main()
{
    std::cout << "PrefabRoundTripTest start\n";

    TestSpawnPrefabRegistersPropertyTypes();
    TestSpawnPrefabRestoresBoardComponentVisualProperties();
    TestPrefabRoundTrip();

    std::cout << "PrefabRoundTripTest passed\n";
    return 0;
}
