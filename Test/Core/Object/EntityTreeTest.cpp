#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Component/PointLightComponent.h"
#include "GameMode/GameModeScope.h"
#include "Object/ObjectHeap.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
    constexpr float Epsilon = 0.0001f;

    MeshDataHandle MakeMeshHandle(uint64_t id)
    {
        MeshDataHandle handle;
        handle.Id = id;
        return handle;
    }

    MegaGeometry::MegaMeshHandle MakeMegaMeshHandle(uint64_t id)
    {
        MegaGeometry::MegaMeshHandle handle;
        handle.Id = id;
        return handle;
    }

    bool Near(float lhs, float rhs)
    {
        return std::fabs(lhs - rhs) <= Epsilon;
    }

    void ExpectVectorNear(const NorvesLib::Math::Vector3& lhs, const NorvesLib::Math::Vector3& rhs)
    {
        assert(Near(lhs.x, rhs.x));
        assert(Near(lhs.y, rhs.y));
        assert(Near(lhs.z, rhs.z));
    }

    class TrackingEntity : public Entity
    {
    public:
        int Code = 0;
        Container::VariableArray<int>* AddedEvents = nullptr;
        Container::VariableArray<int>* RemovedEvents = nullptr;
        Container::VariableArray<int>* TickEvents = nullptr;

        void OnAddedToWorld() override
        {
            assert(GetWorld() != nullptr);
            if (AddedEvents)
            {
                AddedEvents->push_back(Code);
            }
        }

        void OnRemovedFromWorld() override
        {
            assert(GetWorld() != nullptr);
            if (RemovedEvents)
            {
                RemovedEvents->push_back(Code);
            }
        }

        void Tick(float deltaTime) override
        {
            (void)deltaTime;
            if (TickEvents)
            {
                TickEvents->push_back(Code);
            }
        }
    };

    class TrackingComponent : public NorvesLib::Core::Component::Component
    {
    public:
        int Code = 0;
        int* BeginCount = nullptr;
        int* EndCount = nullptr;
        Container::VariableArray<int>* TickEvents = nullptr;

        void BeginPlay() override
        {
            NorvesLib::Core::Component::Component::BeginPlay();
            if (BeginCount)
            {
                ++(*BeginCount);
            }
        }

        void EndPlay() override
        {
            if (EndCount)
            {
                ++(*EndCount);
            }
            NorvesLib::Core::Component::Component::EndPlay();
        }

        void Tick(float deltaTime) override
        {
            (void)deltaTime;
            if (TickEvents)
            {
                TickEvents->push_back(Code);
            }
        }
    };

    MeshProxy* FindMeshProxy(SceneView& view, uint64_t componentId)
    {
        auto& proxies = const_cast<Container::VariableArray<MeshProxy>&>(view.GetMeshProxies());
        for (MeshProxy& proxy : proxies)
        {
            if (proxy.ComponentId == componentId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    uint32_t CountMeshProxiesForObjectId(SceneView& view, uint64_t objectId)
    {
        uint32_t count = 0;
        auto& proxies = const_cast<Container::VariableArray<MeshProxy>&>(view.GetMeshProxies());
        for (MeshProxy& proxy : proxies)
        {
            if (proxy.ObjectId == objectId)
            {
                ++count;
            }
        }
        return count;
    }

    MegaGeometryProxy* FindMegaGeometryProxy(SceneView& view, uint64_t objectId)
    {
        auto& proxies = const_cast<Container::VariableArray<MegaGeometryProxy>&>(view.GetMegaGeometryProxies());
        for (MegaGeometryProxy& proxy : proxies)
        {
            if (proxy.ObjectId == objectId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    LightProxy* FindLightProxy(SceneView& view, uint64_t lightId)
    {
        auto& proxies = const_cast<Container::VariableArray<LightProxy>&>(view.GetLightProxies());
        for (LightProxy& proxy : proxies)
        {
            if (proxy.LightId == lightId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    bool ContainsChild(Entity* parent, Entity* child)
    {
        auto children = parent->GetChildEntities();
        for (Entity* existing : children)
        {
            if (existing == child)
            {
                return true;
            }
        }
        return false;
    }

    void TestPublicAttachPolicyAndAdoption()
    {
        World world;
        world.Initialize();

        Entity* rejectedRoot = new Entity();
        assert(!world.AddInner(rejectedRoot));
        assert(rejectedRoot->GetOuter() == nullptr);
        delete rejectedRoot;

        Entity* liveParent = world.SpawnEntity<Entity>();
        assert(liveParent != nullptr);
        Entity* rejectedChild = new Entity();
        assert(!liveParent->AddInner(rejectedChild));
        assert(rejectedChild->GetOuter() == nullptr);
        delete rejectedChild;

        Container::VariableArray<int> addedEvents;
        Container::VariableArray<int> removedEvents;
        int beginCount = 0;
        int endCount = 0;

        TrackingEntity* offlineParent = new TrackingEntity();
        TrackingEntity* offlineChild = new TrackingEntity();
        TrackingComponent* offlineComponent = new TrackingComponent();
        offlineParent->Code = 1;
        offlineChild->Code = 2;
        offlineParent->AddedEvents = &addedEvents;
        offlineChild->AddedEvents = &addedEvents;
        offlineParent->RemovedEvents = &removedEvents;
        offlineChild->RemovedEvents = &removedEvents;
        offlineComponent->BeginCount = &beginCount;
        offlineComponent->EndCount = &endCount;

        assert(offlineChild->AddInner(offlineComponent));
        assert(offlineParent->AddInner(offlineChild));
        assert(!offlineParent->HasFlag(OF_Initialized));
        assert(!offlineChild->HasFlag(OF_Initialized));
        assert(!offlineComponent->HasFlag(OF_Initialized));

        assert(world.AddObject(offlineParent));
        assert(offlineParent->HasFlag(OF_Initialized));
        assert(offlineChild->HasFlag(OF_Initialized));
        assert(offlineComponent->HasFlag(OF_Initialized));
        assert(beginCount == 1);
        assert(offlineParent->GetWorld() == &world);
        assert(offlineChild->GetWorld() == &world);
        assert(offlineChild->IsInWorld());
        assert(offlineParent->GetObjectId() != 0);
        assert(offlineChild->GetObjectId() != 0);
        assert(offlineParent->GetObjectId() != offlineChild->GetObjectId());
        assert(ContainsChild(offlineParent, offlineChild));
        assert(addedEvents.size() == 2);
        assert(addedEvents[0] == 1);
        assert(addedEvents[1] == 2);

        TrackingComponent* createdOnChild = world.CreateComponent<TrackingComponent>(offlineChild);
        assert(createdOnChild != nullptr);
        createdOnChild->EndCount = &endCount;

        world.RemoveObject(offlineParent);
        assert(removedEvents.size() == 2);
        assert(removedEvents[0] == 1);
        assert(removedEvents[1] == 2);
        assert(endCount == 2);

        world.Finalize();
    }

    void TestObjectHeapCreateRejected()
    {
        World world;
        world.Initialize();

        ObjectHeap heap;
        ObjectHandle rootHandle = heap.Create(Entity::StaticClass(), &world);
        assert(!rootHandle.IsValid());
        assert(world.GetObjectCount() == 0);

        ObjectHandle detachedRootHandle = heap.Create<Entity>();
        Entity* detachedRoot = heap.Resolve<Entity>(detachedRootHandle);
        assert(detachedRoot != nullptr);
        assert(detachedRoot->HasFlag(OF_HeapOwned));
        assert(!world.AddObject(detachedRoot));
        assert(detachedRoot->GetOuter() == nullptr);
        assert(world.GetObjectCount() == 0);

        Entity* parent = world.SpawnEntity<Entity>();
        assert(parent != nullptr);

        ObjectHandle detachedChildHandle = heap.Create<Entity>();
        Entity* detachedChild = heap.Resolve<Entity>(detachedChildHandle);
        assert(detachedChild != nullptr);
        assert(!parent->AddInner(detachedChild));
        assert(detachedChild->GetOuter() == nullptr);
        assert(parent->GetChildEntities().empty());

        ObjectHandle childHandle = heap.Create(Entity::StaticClass(), parent);
        assert(!childHandle.IsValid());
        assert(parent->GetChildEntities().empty());

        ObjectHandle heapSubtreeChildHandle = heap.Create<Entity>();
        Entity* heapSubtreeChild = heap.Resolve<Entity>(heapSubtreeChildHandle);
        assert(heapSubtreeChild != nullptr);

        Entity* offlineParent = new Entity();
        assert(offlineParent->AddInner(heapSubtreeChild));
        assert(!world.AddObject(offlineParent));
        assert(offlineParent->GetOuter() == nullptr);
        assert(heapSubtreeChild->GetOuter() == offlineParent);
        assert(world.GetObjectCount() == 1);
        offlineParent->Finalize();
        delete offlineParent;

        world.Finalize();
    }

    void TestRecursiveSceneSyncAndActiveState()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr);
        Entity* child = world.SpawnEntity<Entity>(root);
        assert(child != nullptr);
        assert(root->GetObjectId() != child->GetObjectId());

        MeshComponent* rootMesh = world.CreateComponent<MeshComponent>(root);
        assert(rootMesh != nullptr);
        rootMesh->SetMeshHandle(MakeMeshHandle(10));

        MeshComponent* childMesh = world.CreateComponent<MeshComponent>(child);
        assert(childMesh != nullptr);
        childMesh->SetMeshHandle(MakeMeshHandle(20));

        MegaGeometryComponent* childMega = world.CreateComponent<MegaGeometryComponent>(child);
        assert(childMega != nullptr);
        childMega->SetMegaMeshHandle(MakeMegaMeshHandle(30));

        PointLightComponent* childLight = world.CreateComponent<PointLightComponent>(child);
        assert(childLight != nullptr);

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 2);
        assert(view.GetMegaGeometryProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);
        assert(FindMeshProxy(view, rootMesh->GetComponentId()) != nullptr);
        assert(FindMeshProxy(view, childMesh->GetComponentId()) != nullptr);
        assert(FindMeshProxy(view, rootMesh->GetComponentId())->ObjectId == root->GetObjectId());
        assert(FindMeshProxy(view, childMesh->GetComponentId())->ObjectId == child->GetObjectId());
        assert(FindMegaGeometryProxy(view, child->GetObjectId()) != nullptr);
        assert(FindLightProxy(view, childLight->GetComponentId()) != nullptr);

        world.SyncToSceneView();
        assert(FindMeshProxy(view, childMesh->GetComponentId()) != nullptr);
        assert(FindMegaGeometryProxy(view, child->GetObjectId()) != nullptr);
        assert(FindLightProxy(view, childLight->GetComponentId()) != nullptr);

        root->SetActive(false);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().empty());
        assert(view.GetMegaGeometryProxies().empty());
        assert(view.GetLightProxies().empty());

        root->SetActive(true);
        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 2);
        assert(view.GetMegaGeometryProxies().size() == 1);
        assert(view.GetLightProxies().size() == 1);

        world.Finalize();
    }

    void TestSceneSyncRemovesOnlyDeletedMeshComponentProxy()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity != nullptr);

        MeshComponent* firstMesh = world.CreateComponent<MeshComponent>(entity);
        MeshComponent* secondMesh = world.CreateComponent<MeshComponent>(entity);
        assert(firstMesh != nullptr);
        assert(secondMesh != nullptr);
        firstMesh->SetMeshHandle(MakeMeshHandle(101));
        secondMesh->SetMeshHandle(MakeMeshHandle(102));

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 2);
        assert(CountMeshProxiesForObjectId(view, entity->GetObjectId()) == 2);
        assert(FindMeshProxy(view, firstMesh->GetComponentId()) != nullptr);
        assert(FindMeshProxy(view, secondMesh->GetComponentId()) != nullptr);

        const uint64_t removedComponentId = firstMesh->GetComponentId();
        entity->RemoveComponent(firstMesh);
        world.SyncToSceneView();

        assert(view.GetMeshProxies().size() == 1);
        assert(CountMeshProxiesForObjectId(view, entity->GetObjectId()) == 1);
        assert(FindMeshProxy(view, removedComponentId) == nullptr);
        assert(FindMeshProxy(view, secondMesh->GetComponentId()) != nullptr);
        assert(FindMeshProxy(view, secondMesh->GetComponentId())->ObjectId == entity->GetObjectId());

        world.Finalize();
    }

    void TestRemoveEntitySubtreeClearsAllMeshComponentProxies()
    {
        World world;
        world.Initialize();

        SceneView view;
        SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);

        Entity* root = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(root);
        assert(root != nullptr);
        assert(child != nullptr);

        MeshComponent* rootMeshA = world.CreateComponent<MeshComponent>(root);
        MeshComponent* rootMeshB = world.CreateComponent<MeshComponent>(root);
        MeshComponent* childMeshA = world.CreateComponent<MeshComponent>(child);
        MeshComponent* childMeshB = world.CreateComponent<MeshComponent>(child);
        assert(rootMeshA != nullptr);
        assert(rootMeshB != nullptr);
        assert(childMeshA != nullptr);
        assert(childMeshB != nullptr);

        rootMeshA->SetMeshHandle(MakeMeshHandle(201));
        rootMeshB->SetMeshHandle(MakeMeshHandle(202));
        childMeshA->SetMeshHandle(MakeMeshHandle(203));
        childMeshB->SetMeshHandle(MakeMeshHandle(204));

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 4);
        assert(CountMeshProxiesForObjectId(view, root->GetObjectId()) == 2);
        assert(CountMeshProxiesForObjectId(view, child->GetObjectId()) == 2);

        const uint64_t rootMeshAId = rootMeshA->GetComponentId();
        const uint64_t rootMeshBId = rootMeshB->GetComponentId();
        const uint64_t childMeshAId = childMeshA->GetComponentId();
        const uint64_t childMeshBId = childMeshB->GetComponentId();

        assert(world.RemoveEntity(root));
        assert(view.GetMeshProxies().empty());
        assert(FindMeshProxy(view, rootMeshAId) == nullptr);
        assert(FindMeshProxy(view, rootMeshBId) == nullptr);
        assert(FindMeshProxy(view, childMeshAId) == nullptr);
        assert(FindMeshProxy(view, childMeshBId) == nullptr);

        world.Finalize();
    }

    void TestRecursiveTickAndPendingCleanup()
    {
        World world;
        world.Initialize();

        Container::VariableArray<int> tickEvents;

        TrackingEntity* root = world.SpawnEntity<TrackingEntity>();
        assert(root != nullptr);
        root->Code = 1;
        root->TickEvents = &tickEvents;

        TrackingEntity* child = world.SpawnEntity<TrackingEntity>(root);
        assert(child != nullptr);
        child->Code = 2;
        child->TickEvents = &tickEvents;

        TrackingEntity* grandChild = world.SpawnEntity<TrackingEntity>(child);
        assert(grandChild != nullptr);
        grandChild->Code = 3;
        grandChild->TickEvents = &tickEvents;

        TrackingComponent* component = world.CreateComponent<TrackingComponent>(child);
        assert(component != nullptr);
        component->Code = 20;
        component->TickEvents = &tickEvents;

        world.Tick(0.016f);
        assert(tickEvents.size() == 4);
        assert(tickEvents[0] == 1);
        assert(tickEvents[1] == 2);
        assert(tickEvents[2] == 20);
        assert(tickEvents[3] == 3);

        tickEvents.clear();
        child->SetTickEnabled(false);
        world.Tick(0.016f);
        assert(tickEvents.size() == 2);
        assert(tickEvents[0] == 1);
        assert(tickEvents[1] == 3);
        child->SetTickEnabled(true);

        tickEvents.clear();
        root->SetActive(false);
        world.Tick(0.016f);
        assert(tickEvents.empty());
        root->SetActive(true);

        tickEvents.clear();
        child->MarkForDestroy();
        world.Tick(0.016f);
        assert(tickEvents.size() == 1);
        assert(tickEvents[0] == 1);
        assert(root->GetChildEntities().empty());

        world.Finalize();
    }

    void TestRemoveEntityChildDirectly()
    {
        World world;
        world.Initialize();

        Container::VariableArray<int> removedEvents;
        TrackingEntity* root = world.SpawnEntity<TrackingEntity>();
        assert(root != nullptr);
        root->Code = 1;
        root->RemovedEvents = &removedEvents;

        TrackingEntity* child = world.SpawnEntity<TrackingEntity>(root);
        assert(child != nullptr);
        child->Code = 2;
        child->RemovedEvents = &removedEvents;

        assert(world.RemoveEntity(child));
        assert(root->GetWorld() == &world);
        assert(root->GetChildEntities().empty());
        assert(removedEvents.size() == 1);
        assert(removedEvents[0] == 2);

        world.Finalize();
    }

    void TestReparentEntity()
    {
        World world;
        world.Initialize();

        World otherWorld;
        otherWorld.Initialize();

        Entity* parentA = world.SpawnEntity<Entity>();
        Entity* parentB = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parentA);
        assert(parentA != nullptr);
        assert(parentB != nullptr);
        assert(child != nullptr);

        parentA->SetPosition(10.0f, 0.0f, 0.0f);
        parentB->SetPosition(100.0f, 0.0f, 0.0f);
        child->SetLocalPosition(2.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();

        const uint64_t childObjectId = child->GetObjectId();
        const NorvesLib::Math::Vector3 savedPosition = child->GetWorldTransform().position;

        assert(world.ReparentEntity(child, parentB));
        world.UpdateWorldTransforms();
        assert(child->GetParentEntity() == parentB);
        assert(child->GetObjectId() == childObjectId);
        ExpectVectorNear(child->GetWorldTransform().position, savedPosition);

        assert(!world.ReparentEntity(parentB, child));

        Entity* pending = world.SpawnEntity<Entity>(parentB);
        assert(pending != nullptr);
        pending->MarkForDestroy();
        assert(!world.ReparentEntity(pending, parentA));

        Entity* pendingAncestor = world.SpawnEntity<Entity>(parentB);
        Entity* pendingDescendant = world.SpawnEntity<Entity>(pendingAncestor);
        assert(pendingAncestor != nullptr);
        assert(pendingDescendant != nullptr);
        pendingAncestor->MarkForDestroy();
        assert(!world.ReparentEntity(pendingDescendant, parentA));
        assert(!world.ReparentEntity(child, pendingDescendant));

        Entity* otherParent = otherWorld.SpawnEntity<Entity>();
        assert(otherParent != nullptr);
        assert(!world.ReparentEntity(child, otherParent));
        assert(!otherWorld.ReparentEntity(child, otherParent));

        assert(world.ReparentEntity(child, nullptr));
        world.UpdateWorldTransforms();
        assert(child->GetParentEntity() == nullptr);
        assert(child->GetObjectId() == childObjectId);
        ExpectVectorNear(child->GetWorldTransform().position, savedPosition);

        world.Finalize();
        otherWorld.Finalize();
    }

    void TestGameModeScopeTreeCleanup()
    {
        World world;
        world.Initialize();

        Container::VariableArray<int> tickEvents;

        TrackingEntity* externalRoot = world.SpawnEntity<TrackingEntity>();
        TrackingEntity* unrelated = world.SpawnEntity<TrackingEntity>();
        assert(externalRoot != nullptr);
        assert(unrelated != nullptr);
        unrelated->Code = 99;
        unrelated->TickEvents = &tickEvents;

        GameMode::GameModeScope scope(&world, nullptr);
        TrackingEntity* externalChild = scope.SpawnObject<TrackingEntity>(externalRoot);
        TrackingEntity* pendingExternalChild = scope.SpawnObject<TrackingEntity>(externalRoot);
        TrackingEntity* trackedRoot = scope.SpawnObject<TrackingEntity>();
        TrackingEntity* trackedChild = scope.SpawnObject<TrackingEntity>(trackedRoot);
        assert(externalChild != nullptr);
        assert(pendingExternalChild != nullptr);
        assert(trackedRoot != nullptr);
        assert(trackedChild != nullptr);

        scope.TrackObject(trackedChild);
        pendingExternalChild->MarkForDestroy();

        scope.Cleanup();
        assert(scope.IsEmpty());
        assert(tickEvents.empty());
        assert(externalRoot->GetWorld() == &world);
        assert(unrelated->GetWorld() == &world);
        assert(externalRoot->GetChildEntities().empty());
        assert(world.GetObjectCount() == 2);

        world.Finalize();
    }
}

int main()
{
    std::cout << "EntityTreeTest start\n";

    TestPublicAttachPolicyAndAdoption();
    TestObjectHeapCreateRejected();
    TestRecursiveSceneSyncAndActiveState();
    TestSceneSyncRemovesOnlyDeletedMeshComponentProxy();
    TestRemoveEntitySubtreeClearsAllMeshComponentProxies();
    TestRecursiveTickAndPendingCleanup();
    TestRemoveEntityChildDirectly();
    TestReparentEntity();
    TestGameModeScopeTreeCleanup();

    std::cout << "EntityTreeTest passed\n";
    return 0;
}
