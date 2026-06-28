#include "Component/MeshComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;

namespace
{
    MeshComponent* AddMesh(Entity& entity)
    {
        MeshComponent* mesh = new MeshComponent();
        assert(entity.AddComponent(mesh));
        return mesh;
    }

    Entity* SpawnMeshEntity(World& world, float x, float y, float z)
    {
        Entity* entity = world.SpawnObject<Entity>();
        assert(entity);
        AddMesh(*entity);
        entity->SetPosition(x, y, z);
        return entity;
    }

    void TestWorldRebuildRaycast()
    {
        World world;
        world.Initialize();

        Entity* first = SpawnMeshEntity(world, 0.0f, 0.0f, 0.0f);
        Entity* second = SpawnMeshEntity(world, 10.0f, 0.0f, 0.0f);
        Entity* third = SpawnMeshEntity(world, 20.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();

        Scene::SceneQuery sceneQuery;
        sceneQuery.Rebuild(world);
        assert(sceneQuery.GetEntryCount() == 3);

        Scene::RaycastHit hit;
        const NorvesLib::Math::Ray rayFromLeft(
            NorvesLib::Math::Vector3(-5.0f, 0.0f, 0.0f),
            NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(sceneQuery.Raycast(rayFromLeft, hit));
        assert(hit.bHit);
        assert(hit.HitEntity == first);
        assert(hit.Distance > 0.0f);

        const NorvesLib::Math::Ray missRay(
            NorvesLib::Math::Vector3(-5.0f, 0.0f, 0.0f),
            NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f));
        assert(!sceneQuery.Raycast(missRay, hit));
        assert(!hit.bHit);
        assert(hit.HitEntity == nullptr);

        const NorvesLib::Math::Ray rayFromMiddle(
            NorvesLib::Math::Vector3(5.0f, 0.0f, 0.0f),
            NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(sceneQuery.Raycast(rayFromMiddle, hit));
        assert(hit.bHit);
        assert(hit.HitEntity == second);
        assert(hit.HitEntity != third);

        world.Finalize();
    }

    void TestExplicitSpanRebuild()
    {
        World world;
        world.Initialize();

        Entity* first = SpawnMeshEntity(world, 0.0f, 0.0f, 0.0f);
        Entity* second = SpawnMeshEntity(world, 10.0f, 0.0f, 0.0f);
        Entity* third = SpawnMeshEntity(world, 20.0f, 0.0f, 0.0f);
        (void)first;
        world.UpdateWorldTransforms();

        Container::VariableArray<Entity*> explicitEntities;
        explicitEntities.push_back(second);
        explicitEntities.push_back(third);

        Scene::SceneQuery sceneQuery;
        sceneQuery.Rebuild(Container::Span<Entity* const>(explicitEntities.data(), explicitEntities.size()));
        assert(sceneQuery.GetEntryCount() == 2);

        Scene::RaycastHit hit;
        const NorvesLib::Math::Ray ray(
            NorvesLib::Math::Vector3(5.0f, 0.0f, 0.0f),
            NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(sceneQuery.Raycast(ray, hit));
        assert(hit.bHit);
        assert(hit.HitEntity == second);
        assert(hit.Distance > 0.0f);

        world.Finalize();
    }

    void TestWorldRebuildSkipsInactiveAndPendingSubtrees()
    {
        World world;
        world.Initialize();

        Entity* active = SpawnMeshEntity(world, 0.0f, 0.0f, 0.0f);
        Entity* inactiveParent = SpawnMeshEntity(world, 10.0f, 0.0f, 0.0f);
        Entity* inactiveChild = world.SpawnEntity<Entity>(inactiveParent);
        assert(inactiveChild);
        AddMesh(*inactiveChild);
        inactiveChild->SetLocalPosition(10.0f, 0.0f, 0.0f);
        inactiveParent->SetActive(false);

        Entity* pending = SpawnMeshEntity(world, 30.0f, 0.0f, 0.0f);
        pending->MarkForDestroy();

        world.UpdateWorldTransforms();

        Scene::SceneQuery sceneQuery;
        sceneQuery.Rebuild(world);
        assert(sceneQuery.GetEntryCount() == 1);

        Scene::RaycastHit hit;
        const NorvesLib::Math::Ray ray(
            NorvesLib::Math::Vector3(-5.0f, 0.0f, 0.0f),
            NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(sceneQuery.Raycast(ray, hit));
        assert(hit.HitEntity == active);

        world.Finalize();
    }
}

int main()
{
    std::cout << "SceneQueryRaycastTest start\n";

    TestWorldRebuildRaycast();
    TestExplicitSpanRebuild();
    TestWorldRebuildSkipsInactiveAndPendingSubtrees();

    std::cout << "SceneQueryRaycastTest passed\n";
    return 0;
}
