#include "Component/MeshComponent.h"
#include "Math/GeometryIntersection.h"
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

    NorvesLib::Math::AABB MakeAABB(
        float minX,
        float minY,
        float minZ,
        float maxX,
        float maxY,
        float maxZ)
    {
        return NorvesLib::Math::AABB(
            NorvesLib::Math::Vector3(minX, minY, minZ),
            NorvesLib::Math::Vector3(maxX, maxY, maxZ));
    }

    NorvesLib::Math::AABB MakeCenteredAABB(float x, float y, float z, float halfExtent)
    {
        return NorvesLib::Math::AABB::FromCenterExtents(
            NorvesLib::Math::Vector3(x, y, z),
            NorvesLib::Math::Vector3(halfExtent, halfExtent, halfExtent));
    }

    NorvesLib::Math::Frustum CreateBoxFrustum(const NorvesLib::Math::AABB& bounds)
    {
        NorvesLib::Math::Frustum frustum;
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Left)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f), bounds.Min.x);
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Right)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(-1.0f, 0.0f, 0.0f), -bounds.Max.x);
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Bottom)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f), bounds.Min.y);
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Top)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(0.0f, -1.0f, 0.0f), -bounds.Max.y);
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Near)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f), bounds.Min.z);
        frustum.Planes[static_cast<int>(NorvesLib::Math::FrustumPlane::Far)] =
            NorvesLib::Math::Plane(NorvesLib::Math::Vector3(0.0f, 0.0f, -1.0f), -bounds.Max.z);
        return frustum;
    }

    bool ContainsEntity(const Container::VariableArray<Entity*>& entities, Entity* target)
    {
        for (Entity* entity : entities)
        {
            if (entity == target)
            {
                return true;
            }
        }

        return false;
    }

    void AssertSameEntitySet(
        const Container::VariableArray<Entity*>& actual,
        const Container::VariableArray<Entity*>& expected)
    {
        assert(actual.size() == expected.size());

        for (Entity* entity : actual)
        {
            assert(ContainsEntity(expected, entity));
        }

        for (Entity* entity : expected)
        {
            assert(ContainsEntity(actual, entity));
        }
    }

    void CollectSphereBruteForce(
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Sphere& sphere,
        Container::VariableArray<Entity*>& outEntities)
    {
        outEntities.clear();
        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            NorvesLib::Math::AABB bounds;
            if (entity->GetWorldAABB(bounds) && NorvesLib::Math::SphereIntersectsAABB(sphere, bounds))
            {
                outEntities.push_back(entity);
            }
        }
    }

    void CollectBoxBruteForce(
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::AABB& box,
        Container::VariableArray<Entity*>& outEntities)
    {
        outEntities.clear();
        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            NorvesLib::Math::AABB bounds;
            if (entity->GetWorldAABB(bounds) && NorvesLib::Math::AABBIntersectsAABB(box, bounds))
            {
                outEntities.push_back(entity);
            }
        }
    }

    void CollectFrustumBruteForce(
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Frustum& frustum,
        Container::VariableArray<Entity*>& outEntities)
    {
        outEntities.clear();
        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            NorvesLib::Math::AABB bounds;
            if (entity->GetWorldAABB(bounds) && NorvesLib::Math::FrustumIntersectsAABB(frustum, bounds))
            {
                outEntities.push_back(entity);
            }
        }
    }

    void AssertOverlapSphereMatches(
        Scene::SceneQuery& sceneQuery,
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Sphere& sphere,
        bool bExpectedAny)
    {
        Container::VariableArray<Entity*> sceneQueryResults;
        Container::VariableArray<Entity*> bruteForceResults;

        sceneQuery.OverlapSphere(sphere, sceneQueryResults);
        CollectSphereBruteForce(entities, sphere, bruteForceResults);
        AssertSameEntitySet(sceneQueryResults, bruteForceResults);

        if (bExpectedAny)
        {
            assert(sceneQueryResults.size() > 0);
            return;
        }

        assert(sceneQueryResults.size() == 0);
    }

    void AssertOverlapBoxMatches(
        Scene::SceneQuery& sceneQuery,
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::AABB& box,
        bool bExpectedAny)
    {
        Container::VariableArray<Entity*> sceneQueryResults;
        Container::VariableArray<Entity*> bruteForceResults;

        sceneQuery.OverlapBox(box, sceneQueryResults);
        CollectBoxBruteForce(entities, box, bruteForceResults);
        AssertSameEntitySet(sceneQueryResults, bruteForceResults);

        if (bExpectedAny)
        {
            assert(sceneQueryResults.size() > 0);
            return;
        }

        assert(sceneQueryResults.size() == 0);
    }

    void AssertQueryFrustumMatches(
        Scene::SceneQuery& sceneQuery,
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Frustum& frustum,
        bool bExpectedAny)
    {
        Container::VariableArray<Entity*> sceneQueryResults;
        Container::VariableArray<Entity*> bruteForceResults;

        sceneQuery.QueryFrustum(frustum, sceneQueryResults);
        CollectFrustumBruteForce(entities, frustum, bruteForceResults);
        AssertSameEntitySet(sceneQueryResults, bruteForceResults);

        if (bExpectedAny)
        {
            assert(sceneQueryResults.size() > 0);
            return;
        }

        assert(sceneQueryResults.size() == 0);
    }

    void TestFrustumAABBPlaneDirections()
    {
        const NorvesLib::Math::Frustum frustum =
            CreateBoxFrustum(MakeAABB(-10.0f, -10.0f, -10.0f, 10.0f, 10.0f, 10.0f));

        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(0.0f, 0.0f, 0.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(-100.0f, 0.0f, 0.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(100.0f, 0.0f, 0.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(0.0f, 100.0f, 0.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(0.0f, -100.0f, 0.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(0.0f, 0.0f, -100.0f, 1.0f)));
        assert(!NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeCenteredAABB(0.0f, 0.0f, 100.0f, 1.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(-11.0f, -1.0f, -1.0f, -9.0f, 1.0f, 1.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(9.0f, -1.0f, -1.0f, 11.0f, 1.0f, 1.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(-1.0f, -11.0f, -1.0f, 1.0f, -9.0f, 1.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(-1.0f, 9.0f, -1.0f, 1.0f, 11.0f, 1.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(-1.0f, -1.0f, -11.0f, 1.0f, 1.0f, -9.0f)));
        assert(NorvesLib::Math::FrustumIntersectsAABB(frustum, MakeAABB(-1.0f, -1.0f, 9.0f, 1.0f, 1.0f, 11.0f)));
    }

    void TestBVHOverlapMatchesBruteForce()
    {
        World world;
        world.Initialize();

        Container::VariableArray<Entity*> entities;
        entities.reserve(64);
        for (int z = 0; z < 4; ++z)
        {
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    entities.push_back(SpawnMeshEntity(
                        world,
                        static_cast<float>(x) * 5.0f,
                        static_cast<float>(y) * 6.0f,
                        static_cast<float>(z) * 7.0f));
                }
            }
        }

        world.UpdateWorldTransforms();

        Scene::SceneQuery sceneQuery;
        sceneQuery.Rebuild(world);
        assert(sceneQuery.GetEntryCount() == 64);

        Container::Span<Entity* const> entitySpan(entities.data(), entities.size());

        AssertOverlapSphereMatches(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Sphere(NorvesLib::Math::Vector3(8.0f, 9.0f, 7.0f), 8.0f),
            true);
        AssertOverlapSphereMatches(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Sphere(NorvesLib::Math::Vector3(1000.0f, 1000.0f, 1000.0f), 1.0f),
            false);

        AssertOverlapBoxMatches(
            sceneQuery,
            entitySpan,
            MakeAABB(4.0f, 4.0f, -2.0f, 17.0f, 15.0f, 9.0f),
            true);
        AssertOverlapBoxMatches(
            sceneQuery,
            entitySpan,
            MakeAABB(-100.0f, -100.0f, -100.0f, -90.0f, -90.0f, -90.0f),
            false);

        AssertQueryFrustumMatches(
            sceneQuery,
            entitySpan,
            CreateBoxFrustum(MakeAABB(8.0f, 5.0f, 4.0f, 24.0f, 19.0f, 18.0f)),
            true);
        AssertQueryFrustumMatches(
            sceneQuery,
            entitySpan,
            CreateBoxFrustum(MakeAABB(-100.0f, -100.0f, -100.0f, -90.0f, -90.0f, -90.0f)),
            false);

        world.Finalize();
    }
}

int main()
{
    std::cout << "SceneQueryOverlapTest start\n";

    TestFrustumAABBPlaneDirections();
    TestBVHOverlapMatchesBruteForce();

    std::cout << "SceneQueryOverlapTest passed\n";
    return 0;
}
