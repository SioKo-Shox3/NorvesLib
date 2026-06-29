#include "Component/MeshComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;

namespace
{
    constexpr float Epsilon = 0.0001f;

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

    void ExpectAABBCenterAndHalfExtent(
        const NorvesLib::Math::AABB& aabb,
        const NorvesLib::Math::Vector3& expectedCenter,
        float expectedHalfExtent)
    {
        ExpectVectorNear(aabb.Center(), expectedCenter);
        ExpectVectorNear(
            aabb.HalfExtents(),
            NorvesLib::Math::Vector3(expectedHalfExtent, expectedHalfExtent, expectedHalfExtent));
    }

    float DefaultMeshWorldRadius(float maxScale)
    {
        return std::sqrt(3.0f) * maxScale;
    }

    MeshComponent* AddMesh(Entity& entity)
    {
        MeshComponent* mesh = new MeshComponent();
        assert(entity.AddComponent(mesh));
        return mesh;
    }

    void TestEmptyEntityReturnsFalse()
    {
        World world;
        world.Initialize();

        Entity* entity = world.SpawnObject<Entity>();
        assert(entity);

        NorvesLib::Math::AABB aabb;
        assert(!entity->GetWorldAABB(aabb));

        world.Finalize();
    }

    void TestSingleMeshAABBAfterWorldTransformUpdate()
    {
        World world;
        world.Initialize();

        Entity* entity = world.SpawnObject<Entity>();
        assert(entity);
        AddMesh(*entity);

        entity->SetPosition(4.0f, 5.0f, 6.0f);
        entity->SetScale(2.0f, 3.0f, 4.0f);
        world.UpdateWorldTransforms();

        NorvesLib::Math::AABB aabb;
        assert(entity->GetWorldAABB(aabb));
        ExpectAABBCenterAndHalfExtent(
            aabb,
            NorvesLib::Math::Vector3(4.0f, 5.0f, 6.0f),
            DefaultMeshWorldRadius(4.0f));

        world.Finalize();
    }

    void TestDirtyWorldTransformWithoutSceneSync()
    {
        World world;
        world.Initialize();

        Entity* entity = world.SpawnObject<Entity>();
        assert(entity);
        AddMesh(*entity);

        world.UpdateWorldTransforms();
        entity->SetPosition(8.0f, 9.0f, 10.0f);

        NorvesLib::Math::AABB aabb;
        assert(entity->GetWorldAABB(aabb));
        ExpectAABBCenterAndHalfExtent(
            aabb,
            NorvesLib::Math::Vector3(8.0f, 9.0f, 10.0f),
            DefaultMeshWorldRadius(1.0f));

        world.Finalize();
    }

    void TestChildUsesParentWorldTransform()
    {
        World world;
        world.Initialize();

        Entity* parent = world.SpawnObject<Entity>();
        assert(parent);
        Entity* child = world.SpawnEntity<Entity>(parent);
        assert(child);
        AddMesh(*child);

        parent->SetLocalPosition(10.0f, 0.0f, 0.0f);
        parent->SetLocalScale(2.0f, 3.0f, 1.0f);
        child->SetLocalPosition(1.0f, 2.0f, 3.0f);
        child->SetLocalScale(0.5f, 2.0f, 4.0f);
        world.UpdateWorldTransforms();

        NorvesLib::Math::AABB aabb;
        assert(child->GetWorldAABB(aabb));
        ExpectAABBCenterAndHalfExtent(
            aabb,
            NorvesLib::Math::Vector3(12.0f, 6.0f, 3.0f),
            DefaultMeshWorldRadius(6.0f));

        world.Finalize();
    }
}

int main()
{
    std::cout << "EntityWorldAABBTest start\n";

    TestEmptyEntityReturnsFalse();
    TestSingleMeshAABBAfterWorldTransformUpdate();
    TestDirtyWorldTransformWithoutSceneSync();
    TestChildUsesParentWorldTransform();

    std::cout << "EntityWorldAABBTest passed\n";
    return 0;
}
