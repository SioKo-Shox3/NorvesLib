#include "Component/MegaGeometryComponent.h"
#include "Component/MeshComponent.h"
#include "Ecs/EcsWorld.h"
#include "Math/GeometryIntersection.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneView.h"
#include "Scene/SceneQuery.h"
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <functional>
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

    // 既定の境界（MeshComponent の ±1 の箱）を包む球の半径。ワールドの境界は中心からこの半径の立方体になる
    const float kDefaultBoundsRadius = std::sqrt(3.0f);
    // 期待値の光線は、狙う Entity の境界の中心から 10 離れた点から撃つ（当たりの距離は 10 - 半径）
    constexpr float kRayStandOff = 10.0f;

    /// 照会の Entry の集合が expected と同じか（全体を覆う箱の照会で取り出して比べる）
    void AssertEntrySet(const Scene::SceneQuery& sceneQuery, Container::VariableArray<Entity*> expected)
    {
        assert(sceneQuery.GetEntryCount() == expected.size());
        Container::VariableArray<Entity*> actual;
        sceneQuery.OverlapBox(
            NorvesLib::Math::AABB(NorvesLib::Math::Vector3(-10000.0f, -10000.0f, -10000.0f),
                                  NorvesLib::Math::Vector3(10000.0f, 10000.0f, 10000.0f)),
            actual);
        std::sort(actual.begin(), actual.end(), std::less<Entity*>{});
        std::sort(expected.begin(), expected.end(), std::less<Entity*>{});
        assert(actual.size() == expected.size());
        assert(std::equal(actual.begin(), actual.end(), expected.begin()));
    }

    /// center から direction の逆へ kRayStandOff 離れた点から direction へ撃つと expected に当たり、距離が 10 - 半径
    void AssertHitAt(const Scene::SceneQuery& sceneQuery, const NorvesLib::Math::Vector3& center,
                     const NorvesLib::Math::Vector3& direction, Entity* expected)
    {
        Scene::RaycastHit hit;
        const NorvesLib::Math::Ray ray(center - direction * kRayStandOff, direction);
        assert(sceneQuery.Raycast(ray, hit));
        assert(hit.bHit);
        assert(hit.HitEntity == expected);
        assert(AreNearlyEqual(hit.Distance, kRayStandOff - kDefaultBoundsRadius));
    }

    /// center の真上（+Z）から真下へ撃っても何にも当たらない
    void AssertMissFromAbove(const Scene::SceneQuery& sceneQuery, const NorvesLib::Math::Vector3& center)
    {
        Scene::RaycastHit hit;
        const NorvesLib::Math::Ray ray(center + NorvesLib::Math::Vector3(0.0f, 0.0f, kRayStandOff),
                                       NorvesLib::Math::Vector3(0.0f, 0.0f, -1.0f));
        assert(!sceneQuery.Raycast(ray, hit));
        assert(!hit.bHit && hit.HitEntity == nullptr);
    }

    const NorvesLib::Math::Vector3 kDown(0.0f, 0.0f, -1.0f);
    const NorvesLib::Math::Vector3 kUp(0.0f, 0.0f, 1.0f);

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

    // 増分の更新（Update）は、追加・値の変更・親の動き・非アクティブ・破棄の後に、手で決めた Entry と照会の結果を保ち、静止では何もしない
    void TestIncrementalUpdateMatchesExpected()
    {
        World world;
        world.Initialize();
        Rendering::SceneView view;
        Rendering::SceneViewSettings settings;
        assert(view.Initialize(settings));
        world.SetSceneView(&view);
        Rendering::RenderResources renderResources;

        // 6 間隔の格子（境界の立方体は半辺 √3 なので隣と重ならない）
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
        using NorvesLib::Math::Vector3;

        Scene::SceneQuery sceneQuery;
        const auto syncOnce = [&]()
        {
            world.SyncToSceneView(&renderResources.Materials());
            sceneQuery.Update(world);
        };
        Container::VariableArray<Entity*> expected = entities;
        expected.push_back(child);
        const auto removeExpected = [&](Entity* entity)
        {
            expected.erase(std::find(expected.begin(), expected.end(), entity));
        };

        // 最初は全量で作る。子は親の真上（Z+3）にあるので、親は下から、子は上から狙う
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 1);
        AssertEntrySet(sceneQuery, expected);
        AssertHitAt(sceneQuery, Vector3(12.0f, 6.0f, 0.0f), kDown, entities[10]);
        AssertHitAt(sceneQuery, Vector3(0.0f, 0.0f, 3.0f), kDown, child);
        AssertHitAt(sceneQuery, Vector3(0.0f, 0.0f, 0.0f), kUp, parent);

        // 静止: 履歴が落ち着いたあとは、更新も追加も削除も作り直しも 0
        syncOnce();
        syncOnce();
        syncOnce();
        {
            const Scene::SceneQuery::UpdateStats& stats = sceneQuery.GetLastUpdateStats();
            assert(stats.Updated == 0 && stats.Added == 0 && stats.Removed == 0 && stats.Rebuilt == 0);
        }
        AssertEntrySet(sceneQuery, expected);

        // 1 つ動かす: 動かした分だけ refit し、元の場所は外れて新しい場所で当たる
        entities[10]->SetPosition(100.0f, 50.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Updated == 1);
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 0);
        AssertEntrySet(sceneQuery, expected);
        AssertMissFromAbove(sceneQuery, Vector3(12.0f, 6.0f, 0.0f));
        AssertHitAt(sceneQuery, Vector3(100.0f, 50.0f, 0.0f), kDown, entities[10]);

        // 親を動かすと子孫の境界も変わる
        parent->SetPosition(-40.0f, 0.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Updated == 2);
        AssertEntrySet(sceneQuery, expected);
        AssertMissFromAbove(sceneQuery, Vector3(0.0f, 0.0f, 3.0f));
        AssertHitAt(sceneQuery, Vector3(-40.0f, 0.0f, 3.0f), kDown, child);
        AssertHitAt(sceneQuery, Vector3(-40.0f, 0.0f, 0.0f), kUp, parent);

        // 非アクティブ化で外れ、戻すと入る
        entities[3]->SetActive(false);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Removed == 1);
        removeExpected(entities[3]);
        AssertEntrySet(sceneQuery, expected);
        AssertMissFromAbove(sceneQuery, Vector3(18.0f, 0.0f, 0.0f));
        entities[3]->SetActive(true);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Added == 1);
        expected.push_back(entities[3]);
        AssertEntrySet(sceneQuery, expected);
        AssertHitAt(sceneQuery, Vector3(18.0f, 0.0f, 0.0f), kDown, entities[3]);

        // 破棄で外れる
        entities[20]->MarkForDestroy();
        world.Tick(0.016f);
        syncOnce();
        removeExpected(entities[20]);
        AssertEntrySet(sceneQuery, expected);
        AssertMissFromAbove(sceneQuery, Vector3(24.0f, 12.0f, 0.0f));

        // 追加
        Entity* added = SpawnMeshEntity(world, -20.0f, -20.0f, 0.0f);
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Added == 1);
        expected.push_back(added);
        AssertEntrySet(sceneQuery, expected);
        AssertHitAt(sceneQuery, Vector3(-20.0f, -20.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), added);

        // 大量に追加すると保留があふれて作り直す（4 間隔でも半辺 √3 の立方体は重ならない）
        Entity* bulk[100] = {};
        for (int i = 0; i < 100; ++i)
        {
            bulk[i] = SpawnMeshEntity(world, static_cast<float>(i) * 4.0f, 200.0f, 0.0f);
            expected.push_back(bulk[i]);
        }
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 1);
        assert(expected.size() == 141);
        AssertEntrySet(sceneQuery, expected);

        // 作り直した後も、静止のフレームは何も変えず、置いた位置の Entity に当たる
        syncOnce();
        assert(sceneQuery.GetLastUpdateStats().Rebuilt == 0);
        assert(sceneQuery.GetLastUpdateStats().Added == 0);
        AssertEntrySet(sceneQuery, expected);
        AssertHitAt(sceneQuery, Vector3(40.0f, 200.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), bulk[10]);
        AssertHitAt(sceneQuery, Vector3(-20.0f, -20.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), added);
        AssertHitAt(sceneQuery, Vector3(-40.0f, 0.0f, 3.0f), kDown, child);

        sceneQuery.Clear();
        world.Finalize();
    }

    // 増分の更新の共通の道具。Update の前に描画の同期を 1 回回す
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
        Entity* other = SpawnMeshEntity(world, 30.0f, 0.0f, 0.0f);
        using NorvesLib::Math::Vector3;

        // Mesh を持たない親は Entry にならず、MegaGeometry の子と別の Mesh の Entity だけが入る
        Scene::SceneQuery sceneQuery;
        const IncrementalHarness harness{world, sceneQuery, renderResources};
        harness.Sync();
        AssertEntrySet(sceneQuery, {child, other});
        AssertHitAt(sceneQuery, Vector3(0.0f, 0.0f, 0.0f), kDown, child);
        harness.Sync();
        harness.Sync();
        harness.Sync();

        parent->SetPosition(-60.0f, 10.0f, 0.0f);
        harness.Sync();
        assert(sceneQuery.GetLastUpdateStats().Updated >= 1);
        AssertEntrySet(sceneQuery, {child, other});
        AssertMissFromAbove(sceneQuery, Vector3(0.0f, 0.0f, 0.0f));
        AssertHitAt(sceneQuery, Vector3(-60.0f, 10.0f, 0.0f), kDown, child);
        AssertHitAt(sceneQuery, Vector3(30.0f, 0.0f, 0.0f), kDown, other);

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
        const NorvesLib::Math::Vector3 leafCenter(5.0f, 0.0f, 0.0f);
        harness.Sync();
        harness.Sync();
        harness.Sync();
        AssertEntrySet(sceneQuery, {leaf});
        AssertHitAt(sceneQuery, leafCenter, kDown, leaf);

        assert(world.ReparentEntity(subtree, rootB));
        harness.Sync();
        AssertEntrySet(sceneQuery, {});
        AssertMissFromAbove(sceneQuery, leafCenter);

        // 戻すと入る
        assert(world.ReparentEntity(subtree, rootA));
        harness.Sync();
        AssertEntrySet(sceneQuery, {leaf});
        AssertHitAt(sceneQuery, leafCenter, kDown, leaf);

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
        Entity* nearB = SpawnMeshEntity(worldB, 0.0f, 0.0f, 0.0f);
        Entity* farB = SpawnMeshEntity(worldB, 10.0f, 0.0f, 0.0f);

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
        AssertEntrySet(sceneQuery, {nearB, farB});
        AssertHitAt(sceneQuery, NorvesLib::Math::Vector3(0.0f, 0.0f, 0.0f), kDown, nearB);
        AssertHitAt(sceneQuery, NorvesLib::Math::Vector3(10.0f, 0.0f, 0.0f), kDown, farB);

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
    TestIncrementalUpdateMatchesExpected();
    TestIncrementalUpdateFollowsMegaGeometryChild();
    TestIncrementalUpdateFollowsReparentToInactive();
    TestIncrementalUpdateIgnoresOldWorldHooksAfterSwitch();
    TestIncrementalUpdateSurvivesQueryDestroyedBeforeOldWorldRows();
    TestEcsColumnHooksReleaseUser();

    std::cout << "SceneQueryRaycastTest passed\n";
    return 0;
}
