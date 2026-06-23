#include "Component/Component.h"
#include "Component/PointLightComponent.h"
#include "Component/BoardComponent.h"
#include "Object/RuntimeSchema.h"
#include "Object/SchemaProjection.h"
#include "Object/World.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
namespace Component = NorvesLib::Core::Component;
namespace Math = NorvesLib::Math;

namespace
{
    StableClassId MakeClassId(const IClass* cls)
    {
        assert(cls != nullptr);
        return MakeStableSchemaId("NorvesLib", "Class", cls->GetClassName().GetView());
    }

    StablePropertyId MakePropertyId(const IClass* cls, const char* name)
    {
        assert(cls != nullptr);
        return MakeStableSchemaId(
            "NorvesLib",
            "Property",
            cls->GetClassName().GetView(),
            Identity(name).GetView());
    }

    StableTypeId MakeTypeStableId(const char* name)
    {
        return MakeStableSchemaId("NorvesLib", "Type", Container::StringView(name));
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

    void AssertProjectedValue(
        const ObjectSnapshot& snapshot,
        const IClass* cls,
        const char* propertyName,
        const char* expectedSerialized)
    {
        const ProjectedPropertyValue* value = FindProjectedValue(snapshot, MakePropertyId(cls, propertyName));
        assert(value != nullptr);
        assert(value->SerializedValue == expectedSerialized);
    }

    void AssertProjectedType(
        const ObjectSnapshot& snapshot,
        const IClass* cls,
        const char* propertyName,
        const char* expectedTypeName)
    {
        const ProjectedPropertyValue* value = FindProjectedValue(snapshot, MakePropertyId(cls, propertyName));
        assert(value != nullptr);
        assert(value->Type == MakeTypeStableId(expectedTypeName));
    }

    void AssertObjectPath(const ObjectSnapshot& snapshot, const char* expectedPath)
    {
        assert(snapshot.Path == expectedPath);
        assert(snapshot.Ref.Path == expectedPath);
    }

    void AddUniqueAlias(
        Container::VariableArray<SubtreeSnapshotAliasId>& aliases,
        SubtreeSnapshotAliasId alias)
    {
        assert(alias != InvalidSubtreeSnapshotAliasId);
        for (SubtreeSnapshotAliasId existing : aliases)
        {
            assert(existing != alias);
        }
        aliases.push_back(alias);
    }

    void CollectAliases(
        const EntitySubtreeSnapshotNode& node,
        Container::VariableArray<SubtreeSnapshotAliasId>& aliases)
    {
        AddUniqueAlias(aliases, node.Alias);

        for (const ComponentSubtreeSnapshot& component : node.Components)
        {
            AddUniqueAlias(aliases, component.Alias);
        }

        for (const EntitySubtreeSnapshotNode& child : node.Children)
        {
            CollectAliases(child, aliases);
        }
    }

    void AssertAliasSet(const EntitySubtreeSnapshot& snapshot, size_t expectedCount)
    {
        Container::VariableArray<SubtreeSnapshotAliasId> aliases;
        CollectAliases(snapshot.Root, aliases);
        assert(aliases.size() == expectedCount);
    }

    void TestEntitySubtreeSnapshot()
    {
        World world;
        world.Initialize();

        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        root->SetLocalPosition(Math::Vector3(1.0f, 2.0f, 3.0f));

        Entity* child = world.SpawnEntity<Entity>(root);
        assert(child != nullptr);
        child->SetLocalPosition(Math::Vector3(4.0f, 5.0f, 6.0f));
        child->SetActive(false);
        child->SetTickEnabled(false);

        Component::Component* rootComponentA = world.CreateComponent<Component::Component>(root);
        assert(rootComponentA != nullptr);
        rootComponentA->Disable();

        Entity* grandchild = world.SpawnEntity<Entity>(child);
        assert(grandchild != nullptr);
        grandchild->SetLocalScale(Math::Vector3(2.0f, 3.0f, 4.0f));

        Component::PointLightComponent* childLight = world.CreateComponent<Component::PointLightComponent>(child);
        assert(childLight != nullptr);
        childLight->SetRange(42.5f);

        Component::Component* rootComponentB = world.CreateComponent<Component::Component>(root);
        assert(rootComponentB != nullptr);

        grandchild->MarkForDestroy();

        StableObjectRef rootRef;
        rootRef.Id = 123;
        rootRef.SceneId = 77;
        rootRef.Path = "Root";

        EntitySubtreeSnapshot snapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root, rootRef);
        assert(snapshot.FormatVersion == 1);
        assert(snapshot.RootAlias == 1);
        assert(snapshot.RootPath == "Root");
        assert(snapshot.Root.Alias == 1);
        assert(snapshot.Root.ParentAlias == InvalidSubtreeSnapshotAliasId);
        assert(snapshot.Root.Components.size() == 2);
        assert(snapshot.Root.Children.size() == 1);
        AssertAliasSet(snapshot, 6);

        const ComponentSubtreeSnapshot& rootComponentSnapshotA = snapshot.Root.Components[0];
        const ComponentSubtreeSnapshot& rootComponentSnapshotB = snapshot.Root.Components[1];
        const EntitySubtreeSnapshotNode& childNode = snapshot.Root.Children[0];
        const ComponentSubtreeSnapshot& childLightSnapshot = childNode.Components[0];
        const EntitySubtreeSnapshotNode& grandchildNode = childNode.Children[0];

        // The child was created before the root components, but aliases group
        // direct components first and then recurse into child entity subtrees.
        assert(rootComponentSnapshotA.Alias == 2);
        assert(rootComponentSnapshotB.Alias == 3);
        assert(childNode.Alias == 4);
        assert(childLightSnapshot.Alias == 5);
        assert(grandchildNode.Alias == 6);

        assert(rootComponentSnapshotA.OwnerAlias == snapshot.Root.Alias);
        assert(rootComponentSnapshotB.OwnerAlias == snapshot.Root.Alias);
        assert(childNode.ParentAlias == snapshot.Root.Alias);
        assert(childLightSnapshot.OwnerAlias == childNode.Alias);
        assert(grandchildNode.ParentAlias == childNode.Alias);

        AssertObjectPath(snapshot.Root.Object, "Root");
        AssertObjectPath(rootComponentSnapshotA.Object, "Root/Components/Component[0]");
        AssertObjectPath(rootComponentSnapshotB.Object, "Root/Components/Component[1]");
        AssertObjectPath(childNode.Object, "Root/Children/Entity[0]");
        AssertObjectPath(childLightSnapshot.Object, "Root/Children/Entity[0]/Components/PointLightComponent[0]");
        AssertObjectPath(grandchildNode.Object, "Root/Children/Entity[0]/Children/Entity[0]");

        assert(snapshot.Root.Object.Ref.Id == rootRef.Id);
        assert(snapshot.Root.Object.Ref.SceneId == rootRef.SceneId);
        assert(rootComponentSnapshotA.Object.Ref.SceneId == rootRef.SceneId);
        assert(childNode.Object.Ref.SceneId == rootRef.SceneId);

        assert(snapshot.Root.Object.Class == MakeClassId(Entity::StaticClass()));
        assert(rootComponentSnapshotA.Object.Class == MakeClassId(Component::Component::StaticClass()));
        assert(childLightSnapshot.Object.Class == MakeClassId(Component::PointLightComponent::StaticClass()));

        AssertProjectedValue(snapshot.Root.Object, Entity::StaticClass(), "Position", "Vector3(1,2,3)");
        AssertProjectedValue(childNode.Object, Entity::StaticClass(), "Position", "Vector3(4,5,6)");
        AssertProjectedValue(grandchildNode.Object, Entity::StaticClass(), "Scale", "Vector3(2,3,4)");
        AssertProjectedType(snapshot.Root.Object, Entity::StaticClass(), "Position", "Math::Vector3");
        AssertProjectedType(grandchildNode.Object, Entity::StaticClass(), "Scale", "Math::Vector3");

        assert(FindProjectedValue(snapshot.Root.Object, MakePropertyId(Entity::StaticClass(), "ObjectId")) != nullptr);
        assert(FindProjectedValue(rootComponentSnapshotA.Object, MakePropertyId(Component::Component::StaticClass(), "ComponentId")) != nullptr);
        AssertProjectedValue(rootComponentSnapshotA.Object, Component::Component::StaticClass(), "bEnabled", "0");
        AssertProjectedValue(rootComponentSnapshotA.Object, Component::Component::StaticClass(), "bBegunPlay", "1");
        AssertProjectedValue(childLightSnapshot.Object, Component::PointLightComponent::StaticClass(), "Range", "42.5");

        assert(childNode.Components.size() == 1);
        assert(childNode.Children.size() == 1);
        AssertProjectedValue(childNode.Object, Entity::StaticClass(), "bActive", "0");
        AssertProjectedValue(childNode.Object, Entity::StaticClass(), "bTickEnabled", "0");
        AssertProjectedValue(grandchildNode.Object, Entity::StaticClass(), "bPendingDestroy", "1");

        // Runtime ObjectId/ComponentId/lifecycle properties are captured as
        // PROPERTY data for visibility, but Phase 4c restore must use aliases
        // as persisted identity and allocate fresh runtime ids instead.
        ObjectSnapshot singleObjectSnapshot = RuntimeSchemaProjector::BuildObjectSnapshot(*root, rootRef);
        AssertObjectPath(singleObjectSnapshot, "Root");
        assert(singleObjectSnapshot.Properties.size() == snapshot.Root.Object.Properties.size());
        assert(FindProjectedValue(singleObjectSnapshot, MakePropertyId(Component::Component::StaticClass(), "ComponentId")) == nullptr);

        EntitySubtreeSnapshot defaultPathSnapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root);
        assert(defaultPathSnapshot.RootPath == "Entity");
        AssertObjectPath(defaultPathSnapshot.Root.Object, "Entity");
        AssertObjectPath(defaultPathSnapshot.Root.Components[0].Object, "Entity/Components/Component[0]");

        world.Finalize();
    }

    void TestBoardComponentSnapshotIncludesVisualProperties()
    {
        World world;
        world.Initialize();

        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);

        Component::BoardComponent* board = world.CreateComponent<Component::BoardComponent>(root);
        assert(board != nullptr);
        board->SetTint(Math::Vector4(0.2f, 0.4f, 0.6f, 0.8f));
        board->SetPivot(Math::Vector2(0.5f, 0.25f));
        board->SetSizePx(Math::Vector2(128.0f, 96.0f));
        board->SetUVRect(Math::Vector4(0.25f, 0.5f, 0.125f, 0.25f));
        board->SetFrameCount(3);
        board->SetFramesPerSecond(12.0f);
        board->SetLoop(false);
        assert(board->SetFlipbookGrid(128, 64, 32, 32, 4u));
        board->Play();
        board->Tick(1.0f);

        EntitySubtreeSnapshot snapshot = RuntimeSchemaProjector::BuildEntitySubtreeSnapshot(*root);
        assert(snapshot.Root.Components.size() == 1);

        const ComponentSubtreeSnapshot& boardSnapshot = snapshot.Root.Components[0];
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "Tint", "Math::Vector4");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "Pivot", "Math::Vector2");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "SizePx", "Math::Vector2");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "UVRectProp", "Math::Vector4");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FrameCount", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FramesPerSecond", "float");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "bLoop", "bool");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "bPlayOnBeginPlay", "bool");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "InitialFrame", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasTextureWidth", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasTextureHeight", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasCellWidth", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasCellHeight", "uint32");
        AssertProjectedType(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FirstFrameIndex", "uint32");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "Tint", "Vector4(0.200000003,0.400000006,0.600000024,0.800000012)");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "Pivot", "Vector2(0.5,0.25)");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "SizePx", "Vector2(128,96)");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "UVRectProp", "Vector4(0.5,0.5,0.25,0.5)");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FrameCount", "3");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FramesPerSecond", "12");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "bLoop", "0");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "bPlayOnBeginPlay", "0");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "InitialFrame", "0");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasTextureWidth", "128");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasTextureHeight", "64");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasCellWidth", "32");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "AtlasCellHeight", "32");
        AssertProjectedValue(boardSnapshot.Object, Component::BoardComponent::StaticClass(), "FirstFrameIndex", "4");
        assert(FindProjectedValue(boardSnapshot.Object, MakePropertyId(Component::BoardComponent::StaticClass(), "CurrentFrame")) == nullptr);
        assert(FindProjectedValue(boardSnapshot.Object, MakePropertyId(Component::BoardComponent::StaticClass(), "bPlaying")) == nullptr);

        world.Finalize();
    }
}

int main()
{
    std::cout << "EntitySubtreeSnapshotTest start\n";

    TestEntitySubtreeSnapshot();
    TestBoardComponentSnapshotIncludesVisualProperties();

    std::cout << "EntitySubtreeSnapshotTest passed\n";
    return 0;
}
