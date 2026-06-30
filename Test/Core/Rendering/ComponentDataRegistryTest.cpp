#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Engine/NorvesEngine.h"
#include "Object/World.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;

namespace
{
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

    void ResetRegistry()
    {
        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        registry.SetEnabled(false);
        registry.BeginFrameCapture();
    }

    bool EnableRegistryOrSkip()
    {
        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        if (!registry.IsAvailable())
        {
            assert(!registry.SetEnabled(true));
            assert(!registry.IsEnabled());
            return false;
        }

        assert(registry.SetEnabled(true));
        assert(registry.IsEnabled());
        return true;
    }

    void InitializeSceneView(SceneView& view)
    {
        SceneViewSettings settings;
        assert(view.Initialize(settings));
    }

    const MeshProxy* FindMeshProxy(const SceneView& view, uint64_t objectId)
    {
        for (const MeshProxy& proxy : view.GetMeshProxies())
        {
            if (proxy.ObjectId == objectId)
            {
                return &proxy;
            }
        }
        return nullptr;
    }

    const EntityTransformData* FindTransformData(EntityHandle handle)
    {
        for (const EntityTransformData& data : GEngine.GetComponentDataRegistry().GetTransformData())
        {
            if (data.Handle == handle)
            {
                return &data;
            }
        }
        return nullptr;
    }

    const MeshComponentData* FindMeshData(EntityHandle handle)
    {
        for (const MeshComponentData& data : GEngine.GetComponentDataRegistry().GetMeshData())
        {
            if (data.Handle == handle)
            {
                return &data;
            }
        }
        return nullptr;
    }

    const MegaGeometryComponentData* FindMegaData(EntityHandle handle)
    {
        for (const MegaGeometryComponentData& data : GEngine.GetComponentDataRegistry().GetMegaGeometryData())
        {
            if (data.Handle == handle)
            {
                return &data;
            }
        }
        return nullptr;
    }

    void TestDefaultOffNoCaptureNoEngineInitialize()
    {
        ResetRegistry();

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity);
        MeshComponent* mesh = world.CreateComponent<MeshComponent>(entity);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(10));

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        assert(!registry.IsEnabled());
        if (registry.IsAvailable())
        {
            assert(registry.GetHandle(*entity).IsValid());
        }
        else
        {
            assert(!registry.GetHandle(*entity).IsValid());
        }

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(registry.GetTransformData().empty());
        assert(registry.GetMeshData().empty());
        assert(registry.GetMegaGeometryData().empty());

        world.Finalize();
        ResetRegistry();
    }

    void TestEnabledNoSyncPublishesWithoutTouchingSceneView()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity);
        MeshComponent* mesh = world.CreateComponent<MeshComponent>(entity);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(20));

        world.SyncToSceneView();
        assert(view.GetMeshProxies().size() == 1);
        assert(!mesh->IsRenderStateDirty());

        MeshProxy* sceneProxy = const_cast<MeshProxy*>(FindMeshProxy(view, entity->GetObjectId()));
        assert(sceneProxy);
        NorvesLib::Math::Vector3 sceneTranslation = sceneProxy->WorldTransform.GetTranslationRow();
        sceneTranslation.x = 777.0f;
        sceneProxy->WorldTransform.SetTranslationRow(sceneTranslation);

        world.SyncToSceneView();
        const MeshProxy* untouchedProxy = FindMeshProxy(view, entity->GetObjectId());
        assert(untouchedProxy);
        assert(untouchedProxy->WorldTransform.GetTranslationRow().x == 777.0f);

        EntityHandle handle = GEngine.GetComponentDataRegistry().GetHandle(*entity);
        const MeshComponentData* meshData = FindMeshData(handle);
        assert(meshData);
        assert(meshData->Proxy.WorldTransform.GetTranslationRow().x != 777.0f);
        assert(GEngine.GetComponentDataRegistry().GetTransformData().size() == 1);

        world.Finalize();
        ResetRegistry();
    }

    void TestEnableAfterExistingEntities()
    {
        ResetRegistry();
        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity);
        entity->SetPosition(2.0f, 0.0f, 0.0f);

        if (!registry.IsAvailable())
        {
            assert(!registry.GetHandle(*entity).IsValid());
            assert(!registry.SetEnabled(true));
            world.Finalize();
            ResetRegistry();
            return;
        }

        EntityHandle handleBeforeEnable = registry.GetHandle(*entity);
        assert(handleBeforeEnable.IsValid());
        assert(registry.SetEnabled(true));

        world.SyncToSceneView();
        const EntityTransformData* transformData = FindTransformData(handleBeforeEnable);
        assert(transformData);
        assert(transformData->WorldTransform.position.x == 2.0f);

        world.Finalize();
        ResetRegistry();
    }

    void TestAdoptedSubtreeRegistersChildren()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* parent = new Entity();
        Entity* child = new Entity();
        assert(parent->AddInner(child));
        assert(world.AddObject(parent));

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        EntityHandle parentHandle = registry.GetHandle(*parent);
        EntityHandle childHandle = registry.GetHandle(*child);
        assert(parentHandle.IsValid());
        assert(childHandle.IsValid());

        world.SyncToSceneView();
        assert(FindTransformData(parentHandle));
        assert(FindTransformData(childHandle));

        world.Finalize();
        ResetRegistry();
    }

    void TestSubtreeDestroyInvalidatesHandles()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* parent = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parent);
        assert(parent);
        assert(child);
        MeshComponent* mesh = world.CreateComponent<MeshComponent>(child);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(30));

        world.SyncToSceneView();
        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        EntityHandle parentHandle = registry.GetHandle(*parent);
        EntityHandle childHandle = registry.GetHandle(*child);
        assert(FindMeshData(childHandle));

        world.RemoveObject(parent);
        assert(registry.ResolveEntity(parentHandle) == nullptr);
        assert(registry.ResolveEntity(childHandle) == nullptr);
        assert(!FindMeshData(childHandle));

        world.Finalize();
        ResetRegistry();
    }

    void TestRemoveEntityHeapOwnedAndFinalizeInvalidate()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();

        {
            World world;
            world.Initialize();
            Entity* root = world.SpawnEntity<Entity>();
            Entity* child = world.SpawnEntity<Entity>(root);
            assert(root);
            assert(child);
            EntityHandle childHandle = registry.GetHandle(*child);
            assert(childHandle.IsValid());
            assert(world.RemoveEntity(child));
            assert(registry.ResolveEntity(childHandle) == nullptr);
            world.Finalize();
        }

        {
            World world;
            world.Initialize();
            Entity* heapOwned = world.SpawnEntity<Entity>();
            assert(heapOwned);
            EntityHandle handle = registry.GetHandle(*heapOwned);
            heapOwned->SetFlag(OF_HeapOwned, true);
            assert(world.RemoveEntity(heapOwned));
            assert(registry.ResolveEntity(handle) == nullptr);
            heapOwned->SetFlag(OF_HeapOwned, false);
            heapOwned->Finalize();
            delete heapOwned;
            world.Finalize();
        }

        {
            World world;
            world.Initialize();
            Entity* entity = world.SpawnEntity<Entity>();
            assert(entity);
            EntityHandle handle = registry.GetHandle(*entity);
            world.Finalize();
            assert(registry.ResolveEntity(handle) == nullptr);
        }

        ResetRegistry();
    }

    void TestReparentPreservesHandleAndUpdatesDenseData()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* parentA = world.SpawnEntity<Entity>();
        Entity* parentB = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parentA);
        assert(parentA);
        assert(parentB);
        assert(child);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(child);
        assert(mesh);
        mesh->SetMeshHandle(MakeMeshHandle(40));

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        EntityHandle handleBefore = registry.GetHandle(*child);
        child->SetPosition(4.0f, 0.0f, 0.0f);
        world.SyncToSceneView();

        assert(world.ReparentEntity(child, parentB));
        EntityHandle handleAfter = registry.GetHandle(*child);
        assert(handleAfter == handleBefore);

        child->SetPosition(9.0f, 0.0f, 0.0f);
        world.SyncToSceneView();

        const EntityTransformData* transformData = FindTransformData(handleAfter);
        const MeshComponentData* meshData = FindMeshData(handleAfter);
        assert(transformData);
        assert(meshData);
        assert(transformData->WorldTransform.position.x == 9.0f);
        assert(meshData->WorldBounds.CenterX == 9.0f);

        world.Finalize();
        ResetRegistry();
    }

    void TestInvisibleAndInvalidProxiesDoNotPublishStaleDenseEntries()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();
        SceneView view;
        InitializeSceneView(view);
        world.SetSceneView(&view);

        Entity* meshEntity = world.SpawnEntity<Entity>();
        Entity* megaEntity = world.SpawnEntity<Entity>();
        assert(meshEntity);
        assert(megaEntity);

        MeshComponent* mesh = world.CreateComponent<MeshComponent>(meshEntity);
        MegaGeometryComponent* mega = world.CreateComponent<MegaGeometryComponent>(megaEntity);
        assert(mesh);
        assert(mega);
        mesh->SetMeshHandle(MakeMeshHandle(50));
        mega->SetMegaMeshHandle(MakeMegaMeshHandle(60));

        world.SyncToSceneView();
        assert(GEngine.GetComponentDataRegistry().GetMeshData().size() == 1);
        assert(GEngine.GetComponentDataRegistry().GetMegaGeometryData().size() == 1);

        mesh->SetVisible(false);
        mega->SetVisible(false);
        world.SyncToSceneView();
        assert(GEngine.GetComponentDataRegistry().GetMeshData().empty());
        assert(GEngine.GetComponentDataRegistry().GetMegaGeometryData().empty());

        mesh->SetVisible(true);
        mega->SetVisible(true);
        mesh->SetMeshHandle(MeshDataHandle::Invalid());
        mega->SetMegaMeshHandle(MegaGeometry::MegaMeshHandle::Invalid());
        world.SyncToSceneView();
        assert(GEngine.GetComponentDataRegistry().GetMeshData().empty());
        assert(GEngine.GetComponentDataRegistry().GetMegaGeometryData().empty());

        world.Finalize();
        ResetRegistry();
    }

    void TestGenerationReuseRejectsStaleHandles()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World world;
        world.Initialize();

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        Entity* first = world.SpawnEntity<Entity>();
        assert(first);
        EntityHandle staleHandle = registry.GetHandle(*first);
        assert(staleHandle.IsValid());
        assert(world.RemoveEntity(first));

        Entity* second = world.SpawnEntity<Entity>();
        assert(second);
        EntityHandle reusedHandle = registry.GetHandle(*second);
        assert(reusedHandle.IsValid());
        assert(registry.ResolveEntity(staleHandle) == nullptr);
        assert(registry.ResolveEntity(reusedHandle) == second);
        assert(staleHandle != reusedHandle);

        world.Finalize();
        ResetRegistry();
    }

    void TestMultiWorldLatestSyncReplacesDenseScratch()
    {
        ResetRegistry();
        if (!EnableRegistryOrSkip())
        {
            return;
        }

        World worldA;
        World worldB;
        worldA.Initialize();
        worldB.Initialize();
        SceneView viewA;
        SceneView viewB;
        InitializeSceneView(viewA);
        InitializeSceneView(viewB);
        worldA.SetSceneView(&viewA);
        worldB.SetSceneView(&viewB);

        Entity* entityA = worldA.SpawnEntity<Entity>();
        Entity* entityB = worldB.SpawnEntity<Entity>();
        assert(entityA);
        assert(entityB);
        entityA->SetPosition(1.0f, 0.0f, 0.0f);
        entityB->SetPosition(2.0f, 0.0f, 0.0f);

        ComponentDataRegistry& registry = GEngine.GetComponentDataRegistry();
        EntityHandle handleA = registry.GetHandle(*entityA);
        EntityHandle handleB = registry.GetHandle(*entityB);

        worldA.SyncToSceneView();
        assert(registry.GetTransformData().size() == 1);
        assert(registry.GetTransformData()[0].Handle == handleA);

        worldB.SyncToSceneView();
        assert(registry.GetTransformData().size() == 1);
        assert(registry.GetTransformData()[0].Handle == handleB);
        assert(registry.GetTransformData()[0].WorldTransform.position.x == 2.0f);

        worldA.Finalize();
        worldB.Finalize();
        ResetRegistry();
    }
}

int main()
{
    std::cout << "ComponentDataRegistryTest start\n";

    TestDefaultOffNoCaptureNoEngineInitialize();
    TestEnabledNoSyncPublishesWithoutTouchingSceneView();
    TestEnableAfterExistingEntities();
    TestAdoptedSubtreeRegistersChildren();
    TestSubtreeDestroyInvalidatesHandles();
    TestRemoveEntityHeapOwnedAndFinalizeInvalidate();
    TestReparentPreservesHandleAndUpdatesDenseData();
    TestInvisibleAndInvalidProxiesDoNotPublishStaleDenseEntries();
    TestGenerationReuseRejectsStaleHandles();
    TestMultiWorldLatestSyncReplacesDenseScratch();

    std::cout << "ComponentDataRegistryTest passed\n";
    return 0;
}
