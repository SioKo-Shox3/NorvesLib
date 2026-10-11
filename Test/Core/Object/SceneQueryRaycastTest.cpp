#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Ecs/EcsWorld.h"
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
namespace Ecs = NorvesLib::Core::Ecs;

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
                std::cout << "全量との不一致: " << detail << "\n";
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

    // 増分の更新の共通の道具。Update の前に描画の同期を 1 回回し、全量で作った結果と照合する
    struct IncrementalHarness
    {
        World& TargetWorld;
        Scene::SceneQuery& Query;
        Rendering::RenderResources& Resources;

        void Sync() const
        {
            TargetWorld.SyncToSceneView(&Resources.Materials());
            Query.Update(TargetWorld);
        }

        void Check() const
        {
            uint32_t examined = 0;
            char detail[200] = {};
            const bool bPass = Query.VerifyAgainstRebuild(TargetWorld, examined, detail, sizeof(detail));
            if (!bPass)
            {
                std::cout << "全量との不一致: " << detail << "\n";
            }
            assert(bPass);
        }
    };

    // MegaGeometry を持つ子は、Mesh を持たない親が動いても境界を追う
    void TestIncrementalUpdateFollowsMegaGeometryChild()
    {
        World world;
        world.Initialize();
        Rendering::SceneView view;
        Rendering::SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);
        Rendering::RenderResources renderResources;

        Entity* parent = world.SpawnObject<Entity>();
        assert(parent);
        Entity* child = world.SpawnEntity<Entity>(parent);
        assert(child);
        MegaGeometryComponent* mega = world.CreateComponent<MegaGeometryComponent>(child);
        assert(mega);
        SpawnMeshEntity(world, 30.0f, 0.0f, 0.0f);

        Scene::SceneQuery sceneQuery;
        const IncrementalHarness harness{world, sceneQuery, renderResources};
        harness.Sync();
        harness.Check();
        harness.Sync();
        harness.Sync();
        harness.Sync();
        const size_t entriesBefore = sceneQuery.GetEntryCount();

        parent->SetPosition(-60.0f, 10.0f, 0.0f);
        harness.Sync();
        assert(sceneQuery.GetEntryCount() == entriesBefore);
        assert(sceneQuery.GetLastUpdateStats().Updated >= 1);
        harness.Check();

        sceneQuery.Clear();
        world.Finalize();
    }

    // 非アクティブな親へ付け替えると、子孫は照会対象から外れる
    void TestIncrementalUpdateFollowsReparentToInactive()
    {
        World world;
        world.Initialize();
        Rendering::SceneView view;
        Rendering::SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);
        Rendering::RenderResources renderResources;

        Entity* rootA = world.SpawnObject<Entity>();
        Entity* rootB = world.SpawnObject<Entity>();
        assert(rootA && rootB);
        Entity* subtree = world.SpawnEntity<Entity>(rootA);
        assert(subtree);
        Entity* leaf = world.SpawnEntity<Entity>(subtree);
        assert(leaf);
        AddMesh(*leaf);
        leaf->SetLocalPosition(5.0f, 0.0f, 0.0f);
        rootB->SetActive(false);

        Scene::SceneQuery sceneQuery;
        const IncrementalHarness harness{world, sceneQuery, renderResources};
        harness.Sync();
        harness.Sync();
        harness.Sync();
        assert(sceneQuery.GetEntryCount() == 1);
        harness.Check();

        assert(world.ReparentEntity(subtree, rootB));
        harness.Sync();
        assert(sceneQuery.GetEntryCount() == 0);
        harness.Check();

        // 戻すと入る
        assert(world.ReparentEntity(subtree, rootA));
        harness.Sync();
        assert(sceneQuery.GetEntryCount() == 1);
        harness.Check();

        sceneQuery.Clear();
        world.Finalize();
    }

    // World を切り替えたあと、古い World の行の削除は新しい World の Entry を外さない
    void TestIncrementalUpdateIgnoresOldWorldHooksAfterSwitch()
    {
        World worldA;
        World worldB;
        worldA.Initialize();
        worldB.Initialize();
        Rendering::SceneView viewA;
        Rendering::SceneView viewB;
        Rendering::SceneViewSettings settings;
        assert(viewA.Initialize(settings));
        assert(viewB.Initialize(settings));
        worldA.SetSceneView(&viewA);
        worldB.SetSceneView(&viewB);
        Rendering::RenderResources renderResources;

        Entity* doomed = SpawnMeshEntity(worldA, 0.0f, 0.0f, 0.0f);
        SpawnMeshEntity(worldA, 10.0f, 0.0f, 0.0f);
        SpawnMeshEntity(worldB, 0.0f, 0.0f, 0.0f);
        SpawnMeshEntity(worldB, 10.0f, 0.0f, 0.0f);

        Scene::SceneQuery sceneQuery;
        const IncrementalHarness harnessA{worldA, sceneQuery, renderResources};
        const IncrementalHarness harnessB{worldB, sceneQuery, renderResources};
        harnessA.Sync();
        harnessB.Sync();
        harnessB.Sync();
        assert(sceneQuery.GetEntryCount() == 2);

        doomed->MarkForDestroy();
        worldA.Tick(0.016f);
        harnessB.Sync();
        assert(sceneQuery.GetEntryCount() == 2);
        harnessB.Check();

        sceneQuery.Clear();
        worldA.Finalize();
        worldB.Finalize();
    }

    // 切り替えた前の World・追っていた World が生きたまま SceneQuery を先に破棄しても、
    // 前の World に残った削除のフックが解放済みの識別子を読まない（識別子は World が参照を持って生かす）
    void TestIncrementalUpdateSurvivesQueryDestroyedBeforeOldWorldRows()
    {
        World worldA;
        World worldB;
        worldA.Initialize();
        worldB.Initialize();
        Rendering::SceneView viewA;
        Rendering::SceneView viewB;
        Rendering::SceneViewSettings settings;
        assert(viewA.Initialize(settings));
        assert(viewB.Initialize(settings));
        worldA.SetSceneView(&viewA);
        worldB.SetSceneView(&viewB);
        Rendering::RenderResources renderResources;

        Entity* doomedA = SpawnMeshEntity(worldA, 0.0f, 0.0f, 0.0f);
        Entity* doomedB = SpawnMeshEntity(worldB, 0.0f, 0.0f, 0.0f);
        SpawnMeshEntity(worldA, 10.0f, 0.0f, 0.0f);
        SpawnMeshEntity(worldB, 10.0f, 0.0f, 0.0f);

        {
            Scene::SceneQuery sceneQuery;
            const IncrementalHarness harnessA{worldA, sceneQuery, renderResources};
            const IncrementalHarness harnessB{worldB, sceneQuery, renderResources};
            harnessA.Sync();
            harnessB.Sync();
            harnessB.Sync();
            assert(sceneQuery.GetEntryCount() == 2);
            sceneQuery.Clear();
        }

        // SceneQuery が消えたあと、A（切り替えた前）と B（最後に追っていた）の行を消す
        doomedA->MarkForDestroy();
        doomedB->MarkForDestroy();
        worldA.Tick(0.016f);
        worldB.Tick(0.016f);

        worldA.Finalize();
        worldB.Finalize();
    }

    // フックの相手の参照は、差し替え・解除・EcsWorld の破棄のそれぞれで 1 度ずつ手放される
    int g_ReleasedHookUsers = 0;

    void CountReleasedHookUser(void* user)
    {
        ++*static_cast<int*>(user);
        ++g_ReleasedHookUsers;
    }

    void NoOpRowHook(void*, Ecs::EcsEntity, const void*, Ecs::EcsHookReason) {}

    void TestEcsColumnHooksReleaseUser()
    {
        const auto type = Ecs::EcsColumnInfoOf<Ecs::EcsEntityFlags>().Type;
        int releasedFirst = 0;
        int releasedSecond = 0;
        int releasedThird = 0;
        g_ReleasedHookUsers = 0;

        {
            Ecs::EcsWorld ecs;
            Ecs::EcsColumnHooks hooks;
            hooks.OnDestroy = &NoOpRowHook;
            hooks.ReleaseUser = &CountReleasedHookUser;

            hooks.User = &releasedFirst;
            assert(ecs.SetColumnHooks(type, hooks));
            assert(releasedFirst == 0);

            // 差し替えで前の相手が手放される
            hooks.User = &releasedSecond;
            assert(ecs.SetColumnHooks(type, hooks));
            assert(releasedFirst == 1 && releasedSecond == 0);

            // 解除で手放される
            assert(ecs.SetColumnHooks(type, Ecs::EcsColumnHooks{}));
            assert(releasedSecond == 1);

            // EcsWorld の破棄で、残っている相手が手放される
            hooks.User = &releasedThird;
            assert(ecs.SetColumnHooks(type, hooks));
            assert(releasedThird == 0);
        }
        assert(releasedFirst == 1 && releasedSecond == 1 && releasedThird == 1);
        assert(g_ReleasedHookUsers == 3);
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
    TestIncrementalUpdateFollowsMegaGeometryChild();
    TestIncrementalUpdateFollowsReparentToInactive();
    TestIncrementalUpdateIgnoresOldWorldHooksAfterSwitch();
    TestIncrementalUpdateSurvivesQueryDestroyedBeforeOldWorldRows();
    TestEcsColumnHooksReleaseUser();

    std::cout << "SceneQueryRaycastTest passed\n";
    return 0;
}
