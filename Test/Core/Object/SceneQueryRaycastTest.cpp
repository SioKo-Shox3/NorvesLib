#include "Component/MeshComponent.h"
#include "Math/GeometryIntersection.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Scene/SceneQuery.h"
#include <cassert>
#include <cfloat>
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

    bool AreNearlyEqual(float lhs, float rhs)
    {
        const float delta = lhs - rhs;
        return delta >= -0.0001f && delta <= 0.0001f;
    }

    bool RaycastBruteForce(
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Ray& ray,
        Scene::RaycastHit& outHit)
    {
        outHit = Scene::RaycastHit{};

        float bestT = FLT_MAX;
        for (Entity* entity : entities)
        {
            if (!entity)
            {
                continue;
            }

            NorvesLib::Math::AABB bounds;
            if (!entity->GetWorldAABB(bounds))
            {
                continue;
            }

            float t = 0.0f;
            if (NorvesLib::Math::RayIntersectsAABB(ray, bounds, t) && t < bestT)
            {
                bestT = t;
                outHit.HitEntity = entity;
                outHit.Distance = t;
                outHit.bHit = true;
            }
        }

        return outHit.bHit;
    }

    void AssertMatchesBruteForce(
        Scene::SceneQuery& sceneQuery,
        Container::Span<Entity* const> entities,
        const NorvesLib::Math::Ray& ray)
    {
        Scene::RaycastHit bvhHit;
        Scene::RaycastHit bruteForceHit;
        const bool bBVHHit = sceneQuery.Raycast(ray, bvhHit);
        const bool bBruteForceHit = RaycastBruteForce(entities, ray, bruteForceHit);

        assert(bBVHHit == bBruteForceHit);
        assert(bvhHit.bHit == bruteForceHit.bHit);
        if (!bBruteForceHit)
        {
            return;
        }

        assert(bvhHit.HitEntity == bruteForceHit.HitEntity);
        assert(AreNearlyEqual(bvhHit.Distance, bruteForceHit.Distance));
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

    void TestBVHRaycastMatchesBruteForce()
    {
        World world;
        world.Initialize();

        Container::VariableArray<Entity*> entities;
        entities.reserve(64);
        for (int index = 0; index < 64; ++index)
        {
            const int column = index % 8;
            const int row = index / 8;
            const float x = static_cast<float>(column) * 5.0f;
            const float y = static_cast<float>(row % 4) * 6.0f;
            const float z = static_cast<float>(row / 4) * 7.0f;
            entities.push_back(SpawnMeshEntity(world, x, y, z));
        }

        world.UpdateWorldTransforms();

        Scene::SceneQuery sceneQuery;
        sceneQuery.Rebuild(world);
        assert(sceneQuery.GetEntryCount() == 64);

        Container::Span<Entity* const> entitySpan(entities.data(), entities.size());
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(-10.0f, 0.0f, 0.0f),
                NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f)));
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(40.0f, 6.0f, 0.0f),
                NorvesLib::Math::Vector3(-1.0f, 0.0f, 0.0f)));
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(10.0f, -10.0f, 7.0f),
                NorvesLib::Math::Vector3(0.0f, 1.0f, 0.0f)));
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(25.0f, 12.0f, -10.0f),
                NorvesLib::Math::Vector3(0.0f, 0.0f, 1.0f)));
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(-10.0f, -10.0f, -10.0f),
                NorvesLib::Math::Vector3(1.0f, 1.0f, 1.0f)));
        AssertMatchesBruteForce(
            sceneQuery,
            entitySpan,
            NorvesLib::Math::Ray(
                NorvesLib::Math::Vector3(100.0f, 100.0f, 100.0f),
                NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f)));

        world.Finalize();
    }

    // 増分の更新（Update）は、全量で作った結果と同じ Entry・照会の結果を保ち、静止では何もしない
    void TestIncrementalUpdateMatchesRebuild()
    {
        World world;
        world.Initialize();
        Rendering::SceneView view;
        Rendering::SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);
        Rendering::RenderResources renderResources;

        Container::VariableArray<Entity*> entities;
        for (int i = 0; i < 40; ++i)
        {
            entities.push_back(SpawnMeshEntity(
                world, static_cast<float>(i % 8) * 6.0f, static_cast<float>(i / 8) * 6.0f, 0.0f));
        }
        Entity* parent = entities[0];
        Entity* child = world.SpawnEntity<Entity>(parent);
        assert(child);
        AddMesh(*child);
        child->SetLocalPosition(0.0f, 0.0f, 3.0f);

        Scene::SceneQuery sceneQuery;
        const auto syncOnce = [&]()
        {
            world.SyncToSceneView(&renderResources.Materials());
            sceneQuery.Update(world);
        };
        const auto checkAgainstRebuild = [&]()
        {
            uint32_t examined = 0;
            char detail[200] = {};
            const bool bPass = sceneQuery.VerifyAgainstRebuild(world, examined, detail, sizeof(detail));
            if (!bPass)
            {
                std::cout << "mismatch: " << detail << "\n";
            }
            assert(bPass);
            assert(examined > 0);
        };

        // 最初は全量で作る
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 1);
        assert(sceneQuery.GetEntryCount() == 41);
        checkAgainstRebuild();

        // 静止: 履歴が落ち着いたあとは、更新も追加も削除も作り直しも 0
        syncOnce();
        syncOnce();
        syncOnce();
        {
            const Scene::SceneQuery::UpdateStats& stats = sceneQuery.GetLastUpdateStats();
            assert(stats.Updated == 0 && stats.Added == 0 && stats.Removed == 0 && stats.Rebuilt == 0);
        }
        checkAgainstRebuild();

        // 1 つ動かす: 動かした分だけ refit する
        entities[10]->SetPosition(100.0f, 50.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Updated == 1);
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 0);
        checkAgainstRebuild();

        // 親を動かすと子孫の境界も変わる
        parent->SetPosition(-40.0f, 0.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Updated == 2);
        checkAgainstRebuild();

        // 非アクティブ化で外れ、戻すと入る
        entities[3]->SetActive(false);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Removed == 1);
        assert(sceneQuery.GetEntryCount() == 40);
        checkAgainstRebuild();
        entities[3]->SetActive(true);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Added == 1);
        assert(sceneQuery.GetEntryCount() == 41);
        checkAgainstRebuild();

        // 破棄で外れる
        entities[20]->MarkForDestroy();
        world.Tick(0.016f);
        syncOnce();
        assert(sceneQuery.GetEntryCount() == 40);
        checkAgainstRebuild();

        // 追加
        Entity* added = SpawnMeshEntity(world, -20.0f, -20.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Added == 1);
        assert(sceneQuery.GetEntryCount() == 41);
        Scene::RaycastHit hit;
        assert(sceneQuery.Raycast(
            NorvesLib::Math::Ray(NorvesLib::Math::Vector3(-30.0f, -20.0f, 0.0f), NorvesLib::Math::Vector3(1.0f, 0.0f, 0.0f)),
            hit));
        assert(hit.HitEntity == added);
        checkAgainstRebuild();

        // 大量に追加すると保留があふれて作り直す
        for (int i = 0; i < 100; ++i)
        {
            SpawnMeshEntity(world, static_cast<float>(i) * 4.0f, 200.0f, 0.0f);
        }
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 1);
        assert(sceneQuery.GetEntryCount() == 141);
        checkAgainstRebuild();

        // 描画の同期が旧の全走査のときは、毎回全量で作る
        world.SetLegacyRenderSync(true);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 1);
        assert(sceneQuery.GetEntryCount() == 141);
        world.SetLegacyRenderSync(false);
        syncOnce();
        checkAgainstRebuild();

        sceneQuery.Clear();
        world.Finalize();
    }
}

int main()
{
    std::cout << "SceneQueryRaycastTest start\n";

    TestWorldRebuildRaycast();
    TestExplicitSpanRebuild();
    TestWorldRebuildSkipsInactiveAndPendingSubtrees();
    TestBVHRaycastMatchesBruteForce();
    TestIncrementalUpdateMatchesRebuild();

    std::cout << "SceneQueryRaycastTest passed\n";
    return 0;
}
