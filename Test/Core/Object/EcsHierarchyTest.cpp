// 階層のシステムの契約を確かめる。
// - 局所の変換・親・Active・PendingDestroy の変更から、ワールドの変換（EcsWorldTransform）と EffectiveActive を伝播で求め、
//   根から全部たどって求め直した値と全行が bit まで一致する（深い木・付け替え・非アクティブの伝播・シーンの出入りを含む）
// - UpdateWorldTransforms は伝播を呼び、手で求めた値になり、版はワールドの変換が変わった回だけ進む
// - 段の最中に作成を予約した Entity も、同期点の前から親の値で求まる
// - 変更の無い行は処理せず、変わらなかった行の子孫へは伝播しない
// - 並列と直列で、列の値も処理した順も同じ（決定的）
#include "Ecs/EcsTransformSystem.h"
#include "Ecs/EcsWorld.h"
#include "Object/Entity.h"
#include "Object/ObjectHeap.h"
#include "Object/World.h"
#include "Thread/JobSystem.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using NorvesLib::Core::Ecs::EcsEntity;
using NorvesLib::Core::Ecs::EcsPropagatedTransform;
using NorvesLib::Core::Ecs::EcsTransformSystem;
using NorvesLib::Core::Ecs::EcsWorld;
using NorvesLib::Core::Ecs::EcsWorldTransform;

namespace
{
    // 更新（伝播）を走らせてから、根から求め直した値と全行を照合する。照合した Entity の数を返す
    uint32_t ExpectMatchesReference(World &world)
    {
        world.UpdateWorldTransforms();
        uint32_t count = 0;
        assert(world.VerifyEcsTransform(&count));
        assert(count == world.GetEcsWorld().GetEntityCount());
        return count;
    }

    // 階層のシステムを 1 回走らせた結果を返す（UpdateWorldTransforms を通さないので、変更の数を数えられる。照合は別に行う）
    EcsTransformSystem::RunResult UpdateAndRun(World &world)
    {
        return world.GetEcsTransformSystem().Run();
    }

    bool EffectiveActiveOf(World &world, Entity *entity)
    {
        const EcsPropagatedTransform *propagated =
            world.GetEcsWorld().GetColumnValue<EcsPropagatedTransform>(entity->GetEcsEntity());
        assert(propagated && propagated->bValid == 1);
        return propagated->bEffectiveActive != 0;
    }

    // 決定的な擬似乱数（シードごとに同じ列を返す）
    struct Lcg
    {
        uint32_t State = 12345u;

        uint32_t Next()
        {
            State = State * 1664525u + 1013904223u;
            return State >> 8;
        }

        float Unit()
        {
            return static_cast<float>(Next() % 2000u) / 1000.0f - 1.0f;
        }
    };

    void Randomize(Entity *entity, Lcg &random)
    {
        entity->SetLocalPosition(random.Unit() * 10.0f, random.Unit() * 10.0f, random.Unit() * 10.0f);
        const float half = random.Unit() * 1.5f;
        entity->SetLocalRotation(0.0f, std::sin(half), 0.0f, std::cos(half));
        // 深い鎖でも拡縮が発散しないよう、1 に近い値にする
        entity->SetLocalScale(1.0f + 0.01f * std::fabs(random.Unit()), 1.0f + 0.01f * std::fabs(random.Unit()),
                              1.0f + 0.01f * std::fabs(random.Unit()));
    }

    // 作った Entity の一覧（作成順）。同じ手順で作った World どうしは、同じ添字が同じ位置の Entity を指す
    struct Scene
    {
        Container::VariableArray<Entity *> All;
        // 深い鎖: 根 → 鎖の各段（各段に葉を 1 つ）
        Container::VariableArray<Entity *> Chain;
        // 幅広い木: 根 → 子 × 幅 → 孫 × 3
        Entity *WideRoot = nullptr;
        Container::VariableArray<Entity *> WideChildren;
    };

    Entity *Spawn(World &world, Scene &scene, Entity *parent, Lcg &random)
    {
        Entity *entity = world.SpawnEntity<Entity>(parent);
        Randomize(entity, random);
        scene.All.push_back(entity);
        return entity;
    }

    void BuildScene(World &world, Scene &scene, uint32_t chainDepth, uint32_t wide)
    {
        Lcg random;
        Entity *parent = nullptr;
        for (uint32_t i = 0; i < chainDepth; ++i)
        {
            parent = Spawn(world, scene, parent, random);
            scene.Chain.push_back(parent);
            Spawn(world, scene, parent, random);
        }
        scene.WideRoot = Spawn(world, scene, nullptr, random);
        for (uint32_t i = 0; i < wide; ++i)
        {
            Entity *child = Spawn(world, scene, scene.WideRoot, random);
            scene.WideChildren.push_back(child);
            for (uint32_t j = 0; j < 3; ++j)
            {
                Spawn(world, scene, child, random);
            }
        }
    }

    // 深い木: 鎖の段ごとに Depth が 1 つずつ進み、1 段ずつ処理される
    void TestDeepChain()
    {
        World world;
        world.Initialize();
        Scene scene;
        const uint32_t depth = 400;
        BuildScene(world, scene, depth, 0);
        assert(scene.Chain.size() == depth);

        // 初めての Run は全行をきっかけにする。鎖は 1 段ずつ（最後の段の葉を含めて深さ + 1 段）処理される
        const EcsTransformSystem::RunResult first = world.GetEcsTransformSystem().Run();
        assert(first.Levels == depth + 1);
        assert(first.Processed == world.GetEcsWorld().GetEntityCount());
        assert(first.Unresolved == 0);
        assert(ExpectMatchesReference(world) == world.GetEcsWorld().GetEntityCount());

        // 鎖の根を動かすと、全部の子孫へ伝わる
        scene.Chain[0]->SetLocalPosition(100.0f, 0.0f, 0.0f);
        const EcsTransformSystem::RunResult moved = UpdateAndRun(world);
        assert(moved.Seeds == 1);
        // 別の根（幅広い木の根 1 つ）を除く全部
        assert(moved.Processed == world.GetEcsWorld().GetEntityCount() - 1);
        assert(moved.Changed == moved.Processed);
        assert(world.VerifyEcsTransform());
        world.Finalize();
    }

    // 変更の無い行は処理せず、変わらなかった行の子孫へは伝播しない
    void TestIncrementalWork()
    {
        World world;
        world.Initialize();
        // 先にシステムを作る（消費者を登録してから Entity を作る経路）
        (void)world.GetEcsTransformSystem();
        Scene scene;
        BuildScene(world, scene, 20, 6);
        const uint32_t total = ExpectMatchesReference(world);
        assert(total == scene.All.size());

        // 何も変えなければ何も処理しない
        EcsTransformSystem::RunResult idle = UpdateAndRun(world);
        assert(idle.Seeds == 0 && idle.Processed == 0 && idle.Levels == 0);

        // 葉を動かす: その行だけ
        Entity *leaf = scene.WideChildren[2]->GetChildEntities()[1];
        leaf->SetLocalPosition(1.0f, 2.0f, 3.0f);
        EcsTransformSystem::RunResult onlyLeaf = UpdateAndRun(world);
        assert(onlyLeaf.Seeds == 1 && onlyLeaf.Processed == 1 && onlyLeaf.Changed == 1);
        assert(world.VerifyEcsTransform());

        // 同じ値を列へ書き直す（setter は同じ値を書かないので列を直接書く）: 処理はするが、結果が変わらないので子孫へは伝わらない
        Entity *middle = scene.WideChildren[3];
        const NorvesLib::Math::Transform middleLocal = middle->GetLocalTransform();
        assert(world.GetEcsWorld().WriteColumnValueOf(
            middle->GetEcsEntity(), Ecs::EcsLocalTransform{middleLocal.position, middleLocal.rotation, middleLocal.scale}));
        EcsTransformSystem::RunResult same = UpdateAndRun(world);
        assert(same.Processed == 1 && same.Changed == 0);
        assert(world.VerifyEcsTransform());

        // 途中の行を動かす: その行と子孫（孫 3 つ）
        middle->SetLocalPosition(-5.0f, 0.0f, 0.0f);
        EcsTransformSystem::RunResult subtree = UpdateAndRun(world);
        assert(subtree.Seeds == 1 && subtree.Processed == 4 && subtree.Changed == 4);
        assert(world.VerifyEcsTransform());

        // 親と子を同じ回で動かしても、子は 1 回しか処理しない（訪れた番号で重複を除く）
        scene.WideChildren[4]->SetLocalPosition(7.0f, 7.0f, 7.0f);
        scene.WideChildren[4]->GetChildEntities()[0]->SetLocalPosition(8.0f, 8.0f, 8.0f);
        EcsTransformSystem::RunResult both = UpdateAndRun(world);
        assert(both.Seeds == 2 && both.Processed == 4);
        assert(world.VerifyEcsTransform());
        world.Finalize();
    }

    // 付け替え: 世界の位置を保つために局所の変換が変わり、新しい親の下で求め直される
    void TestReparent()
    {
        World world;
        world.Initialize();
        Scene scene;
        BuildScene(world, scene, 12, 5);
        ExpectMatchesReference(world);

        // 幅広い木の子（と孫）を、鎖の途中の葉の下へ
        Entity *moving = scene.WideChildren[1];
        assert(world.ReparentEntity(moving, scene.Chain[6]));
        ExpectMatchesReference(world);
        assert(moving->GetParentEntity() == scene.Chain[6]);

        // 根へ出す
        assert(world.ReparentEntity(moving, nullptr));
        ExpectMatchesReference(world);

        // 鎖ごと別の枝の下へ（深さが変わる）
        assert(world.ReparentEntity(scene.Chain[3], scene.WideChildren[0]));
        ExpectMatchesReference(world);
        assert(world.VerifyEcsBinding());
        world.Finalize();
    }

    // 非アクティブの伝播: 祖先が 1 つでも非アクティブか破棄予約なら EffectiveActive は false
    void TestActivePropagation()
    {
        World world;
        world.Initialize();
        Scene scene;
        BuildScene(world, scene, 6, 4);
        ExpectMatchesReference(world);
        for (Entity *entity : scene.All)
        {
            assert(EffectiveActiveOf(world, entity));
        }

        // 途中の行を止めると、その部分木だけが false
        Entity *branch = scene.WideChildren[1];
        Entity *grandchild = branch->GetChildEntities()[0];
        branch->SetActive(false);
        ExpectMatchesReference(world);
        assert(!EffectiveActiveOf(world, branch) && !EffectiveActiveOf(world, grandchild));
        assert(EffectiveActiveOf(world, scene.WideChildren[0]) && EffectiveActiveOf(world, scene.WideRoot));

        // 孫も止めてから親を戻すと、孫だけが止まったまま
        grandchild->SetActive(false);
        ExpectMatchesReference(world);
        branch->SetActive(true);
        ExpectMatchesReference(world);
        assert(EffectiveActiveOf(world, branch) && !EffectiveActiveOf(world, grandchild));
        assert(EffectiveActiveOf(world, branch->GetChildEntities()[1]));
        grandchild->SetActive(true);
        ExpectMatchesReference(world);
        assert(EffectiveActiveOf(world, grandchild));

        // 根を止めると全部（根の部分木）が false。鎖は別の根なので変わらない
        scene.WideRoot->SetActive(false);
        ExpectMatchesReference(world);
        for (Entity *child : scene.WideChildren)
        {
            assert(!EffectiveActiveOf(world, child));
        }
        assert(EffectiveActiveOf(world, scene.Chain[0]));
        scene.WideRoot->SetActive(true);
        ExpectMatchesReference(world);

        // 破棄予約も伝わる（回収は Tick の cleanup）。回収されると行は消え、残りは一致したまま
        Entity *doomed = scene.WideChildren[2];
        doomed->MarkForDestroy();
        ExpectMatchesReference(world);
        assert(!EffectiveActiveOf(world, doomed) && !EffectiveActiveOf(world, doomed->GetChildEntities()[0]));
        assert(EffectiveActiveOf(world, scene.WideChildren[3]));
        world.Tick(0.01f);
        world.LateTick(0.01f);
        ExpectMatchesReference(world);
        world.Finalize();
    }

    // シーンの出入り: 部分木の作成・除去・付け直しで、行が出入りしても一致し、死んだ行は処理しない
    void TestSceneEntryAndExit()
    {
        World world;
        world.Initialize();
        (void)world.GetEcsTransformSystem();
        Scene scene;
        BuildScene(world, scene, 8, 4);
        ExpectMatchesReference(world);
        const uint32_t before = static_cast<uint32_t>(world.GetEcsWorld().GetEntityCount());

        // 出る: 部分木を除去する（変更ログには死んだ行の項目が残るが、読むときに除かれる）
        Entity *leaving = scene.WideChildren[1];
        leaving->SetLocalPosition(3.0f, 3.0f, 3.0f);
        assert(world.RemoveEntity(leaving));
        world.Tick(0.01f);
        world.LateTick(0.01f);
        const uint32_t after = ExpectMatchesReference(world);
        assert(after == before - 4);

        // 入る: 新しい部分木を足す
        Lcg random;
        random.State = 777u;
        Entity *entering = Spawn(world, scene, scene.WideRoot, random);
        Spawn(world, scene, entering, random);
        Spawn(world, scene, entering, random);
        assert(ExpectMatchesReference(world) == after + 3);

        // 鎖の途中を根へ出して動かす（部分木ごと深さが変わる）
        Entity *detached = scene.Chain[2];
        const uint32_t beforeDetach = static_cast<uint32_t>(world.GetEcsWorld().GetEntityCount());
        Entity *parentOfDetached = detached->GetParentEntity();
        assert(parentOfDetached == scene.Chain[1]);
        assert(world.ReparentEntity(detached, nullptr));
        detached->SetLocalPosition(40.0f, 0.0f, 0.0f);
        ExpectMatchesReference(world);
        assert(world.GetEcsWorld().GetEntityCount() == beforeDetach);

        // 全部出す: World を空にしても落ちない
        world.Finalize();
        assert(world.GetEcsWorld().GetEntityCount() == 0);
    }

    // 後から作った階層のシステムは、すでにある行を全部きっかけにする
    void TestLateSystemSeedsExistingRows()
    {
        World world;
        world.Initialize();
        Scene scene;
        BuildScene(world, scene, 10, 5);
        // この時点で初めて作る
        EcsTransformSystem &system = world.GetEcsTransformSystem();
        const EcsTransformSystem::RunResult first = system.Run();
        assert(first.Seeds == scene.All.size() && first.Processed == scene.All.size());
        uint32_t count = 0;
        assert(world.VerifyEcsTransform(&count) && count == scene.All.size());

        // 全行の再同期を求めても結果は同じ
        system.RequestFullSeed();
        const EcsTransformSystem::RunResult again = system.Run();
        assert(again.Seeds == scene.All.size() && again.Changed == 0);
        assert(world.VerifyEcsTransform());
        world.Finalize();
    }

    // 同じ手順で作った 2 つの World の列（ワールドの変換・EffectiveActive）が、全行 bit まで一致するか
    void ExpectSameColumns(World &lhs, const Scene &lhsScene, World &rhs, const Scene &rhsScene)
    {
        assert(lhsScene.All.size() == rhsScene.All.size());
        for (size_t i = 0; i < lhsScene.All.size(); ++i)
        {
            const EcsEntity left = lhsScene.All[i]->GetEcsEntity();
            const EcsEntity right = rhsScene.All[i]->GetEcsEntity();
            assert(left == right);
            const EcsPropagatedTransform *a = lhs.GetEcsWorld().GetColumnValue<EcsPropagatedTransform>(left);
            const EcsPropagatedTransform *b = rhs.GetEcsWorld().GetColumnValue<EcsPropagatedTransform>(right);
            assert(a && b);
            assert(a->bValid == b->bValid && a->bEffectiveActive == b->bEffectiveActive);
            const EcsWorldTransform *worldA = lhs.GetEcsWorld().GetColumnValue<EcsWorldTransform>(left);
            const EcsWorldTransform *worldB = rhs.GetEcsWorld().GetColumnValue<EcsWorldTransform>(right);
            assert(worldA && worldB);
            assert(Ecs::EcsEqualWorldTransform(*worldA, *worldB));
        }
    }

    // 決まった変更の列。step ごとに同じ変更を World へ加える
    void Mutate(World &world, Scene &scene, uint32_t step)
    {
        Lcg random;
        random.State = 9000u + step * 31u;
        switch (step % 6)
        {
        case 0:
            scene.Chain[step % scene.Chain.size()]->SetLocalPosition(random.Unit() * 5.0f, random.Unit() * 5.0f, 0.0f);
            break;
        case 1:
            scene.WideChildren[step % scene.WideChildren.size()]->SetLocalScale(1.5f, 1.25f, 0.75f);
            break;
        case 2:
            scene.WideChildren[step % scene.WideChildren.size()]->SetActive(step % 4 == 2 ? false : true);
            break;
        case 3:
            scene.WideRoot->SetLocalRotation(0.0f, 0.38268343f, 0.0f, 0.92387953f);
            break;
        case 4:
            assert(world.ReparentEntity(scene.WideChildren[step % scene.WideChildren.size()],
                                        scene.Chain[(step * 3) % scene.Chain.size()]));
            break;
        default:
            for (uint32_t i = 0; i < 5; ++i)
            {
                scene.All[(step * 7 + i * 13) % scene.All.size()]->SetLocalPosition(random.Unit(), random.Unit(), random.Unit());
            }
            break;
        }
    }

    // 並列と直列で、列の値も処理した順も同じ。同じ並列を繰り返しても同じ
    void TestParallelMatchesSerialAndOrderIsDeterministic()
    {
        World serial;
        World parallel;
        World parallelAgain;
        for (World *world : {&serial, &parallel, &parallelAgain})
        {
            world->Initialize();
        }
        serial.GetEcsTransformSystem().SetParallel(false);
        // 行が 1 つのバケツでも並列の経路を通し、1 行ずつ取り出させて仕事の順が乱れやすくする
        parallel.GetEcsTransformSystem().SetParallel(true, 0, 1);
        parallelAgain.GetEcsTransformSystem().SetParallel(true, 0, 3);
        for (World *world : {&serial, &parallel, &parallelAgain})
        {
            world->GetEcsTransformSystem().SetRecordOrder(true);
        }
        Scene serialScene;
        Scene parallelScene;
        Scene parallelAgainScene;
        BuildScene(serial, serialScene, 30, 40);
        BuildScene(parallel, parallelScene, 30, 40);
        BuildScene(parallelAgain, parallelAgainScene, 30, 40);

        for (uint32_t step = 0; step <= 40; ++step)
        {
            if (step > 0)
            {
                Mutate(serial, serialScene, step);
                Mutate(parallel, parallelScene, step);
                Mutate(parallelAgain, parallelAgainScene, step);
            }
            const EcsTransformSystem::RunResult a = UpdateAndRun(serial);
            const EcsTransformSystem::RunResult b = UpdateAndRun(parallel);
            const EcsTransformSystem::RunResult c = UpdateAndRun(parallelAgain);
            assert(a.Seeds == b.Seeds && a.Processed == b.Processed && a.Changed == b.Changed && a.Levels == b.Levels);
            assert(a.Processed == c.Processed && a.Unresolved == 0 && b.Unresolved == 0);

            // 処理した順（EcsEntity の列）が全部同じ
            const auto &orderA = serial.GetEcsTransformSystem().GetProcessedOrder();
            const auto &orderB = parallel.GetEcsTransformSystem().GetProcessedOrder();
            const auto &orderC = parallelAgain.GetEcsTransformSystem().GetProcessedOrder();
            assert(orderA.size() == orderB.size() && orderA.size() == orderC.size());
            for (size_t i = 0; i < orderA.size(); ++i)
            {
                assert(orderA[i] == orderB[i] && orderA[i] == orderC[i]);
            }
            ExpectSameColumns(serial, serialScene, parallel, parallelScene);
            ExpectSameColumns(serial, serialScene, parallelAgain, parallelAgainScene);
            // 根から求め直した値とも一致する（Run はすでに済んでいるので、この照合の Run は何もしない）
            assert(serial.VerifyEcsTransform() && parallel.VerifyEcsTransform() && parallelAgain.VerifyEcsTransform());
        }
        for (World *world : {&serial, &parallel, &parallelAgain})
        {
            world->Finalize();
        }
    }

    // 符号付きゼロ: 数値として等しければキャッシュを更新しないので、+0 と -0 の違いだけでは不一致にならない
    void TestSignedZeroMatchesReference()
    {
        // 比較の関係そのもの: +0 と -0 は同じ、NaN は同じ bit のときだけ同じ、違う値は違う
        const float nan = std::numeric_limits<float>::quiet_NaN();
        assert(Ecs::EcsSameBits(0.0f, -0.0f));
        assert(Ecs::EcsSameBits(nan, nan));
        assert(!Ecs::EcsSameBits(nan, 0.0f));
        assert(!Ecs::EcsSameBits(1.0f, 2.0f));

        World world;
        world.Initialize();
        (void)world.GetEcsTransformSystem();
        Entity *root = world.SpawnEntity<Entity>(nullptr);
        Entity *child = world.SpawnEntity<Entity>(root);
        assert(root && child);

        // 局所のスケールの x を +0 にして求める
        root->SetLocalScale(0.0f, 1.0f, 1.0f);
        const NorvesLib::Math::Transform local = root->GetLocalTransform();
        UpdateAndRun(world);
        assert(world.VerifyEcsTransform());

        // 列を直接 -0 に書き換える（setter は数値として等しい値を書かない）
        assert(world.GetEcsWorld().WriteColumnValueOf(
            root->GetEcsEntity(), Ecs::EcsLocalTransform{local.position, local.rotation, NorvesLib::Math::Vector3(-0.0f, 1.0f, 1.0f)}));
        const EcsTransformSystem::RunResult negative = UpdateAndRun(world);
        assert(negative.Processed >= 1);
        assert(world.VerifyEcsTransform());

        // 子の局所の変換を動かしても、根の -0 を挟んで求め直した値と一致する
        child->SetLocalPosition(1.0f, 2.0f, 3.0f);
        UpdateAndRun(world);
        assert(world.VerifyEcsTransform());

        // setter 経由でも一致する
        root->SetLocalScale(-0.0f, 1.0f, 1.0f);
        UpdateAndRun(world);
        assert(world.VerifyEcsTransform());
        world.Finalize();
    }

    // 並列の中は列を読むだけ: 並列の区間にいる間も、階層のシステムは ECS_PROPERTY の検査に触れない
    void TestParallelRunReportsNoPropertyAccess()
    {
        const uint32_t before = Ecs::EcsGetParallelAccessReportCount();
        World world;
        world.Initialize();
        world.GetEcsTransformSystem().SetParallel(true, 0, 2);
        Scene scene;
        BuildScene(world, scene, 10, 20);
        scene.WideRoot->SetLocalPosition(1.0f, 1.0f, 1.0f);
        ExpectMatchesReference(world);
        assert(Ecs::EcsGetParallelAccessReportCount() == before);
        world.Finalize();
    }

    // 位置の x が期待値か（スケール・回転は既定のまま）
    void ExpectWorldX(const Entity *entity, float expected)
    {
        const NorvesLib::Math::Transform world = entity->GetWorldTransform();
        assert(world.position.x == expected);
        assert(world.position.y == 0.0f && world.position.z == 0.0f);
    }

    // UpdateWorldTransforms: 局所の変換の変更・付け替え・非アクティブの後のワールドの変換が手で求めた値になり、
    // 版はワールドの変換が変わった Entity だけ 1 つ進む
    void TestUpdateMatchesExpectedValuesAndVersions()
    {
        World world;
        world.Initialize();
        Entity *rootA = world.SpawnEntity<Entity>();
        Entity *rootB = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(rootA);
        Entity *grandChild = world.SpawnEntity<Entity>(child);
        assert(rootA && rootB && child && grandChild);
        rootA->SetLocalPosition(1.0f, 0.0f, 0.0f);
        rootB->SetLocalPosition(10.0f, 0.0f, 0.0f);
        child->SetLocalPosition(2.0f, 0.0f, 0.0f);
        grandChild->SetLocalPosition(4.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        ExpectWorldX(rootA, 1.0f);
        ExpectWorldX(rootB, 10.0f);
        ExpectWorldX(child, 3.0f);
        ExpectWorldX(grandChild, 7.0f);

        struct Versions
        {
            uint64_t RootA, RootB, Child, GrandChild;
            bool operator==(const Versions &other) const
            {
                return RootA == other.RootA && RootB == other.RootB && Child == other.Child &&
                       GrandChild == other.GrandChild;
            }
        };
        const auto versions = [&]()
        {
            return Versions{rootA->GetTransformVersion(), rootB->GetTransformVersion(), child->GetTransformVersion(),
                            grandChild->GetTransformVersion()};
        };

        // 何も変えない回は、版を進めない
        const Versions settled = versions();
        world.UpdateWorldTransforms();
        world.UpdateWorldTransforms();
        assert(versions() == settled);

        // 親を動かす: 親と子孫だけ版が 1 つ進み、値は親の移動の分だけ動く
        rootA->SetLocalPosition(5.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        ExpectWorldX(rootA, 5.0f);
        ExpectWorldX(child, 7.0f);
        ExpectWorldX(grandChild, 11.0f);
        const Versions moved = versions();
        assert(moved.RootA == settled.RootA + 1);
        assert(moved.RootB == settled.RootB);
        assert(moved.Child == settled.Child + 1);
        assert(moved.GrandChild == settled.GrandChild + 1);

        // 同じ値を書き直しても、ワールドの変換は変わらないので版は進まない
        child->SetLocalPosition(2.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        assert(versions() == moved);

        // 付け替えはワールドの位置を保つ: 新しい親の下の局所の変換が 7 - 10 = -3 になり、ワールドの値は変わらない
        assert(world.ReparentEntity(child, rootB));
        world.UpdateWorldTransforms();
        ExpectWorldX(child, 7.0f);
        ExpectWorldX(grandChild, 11.0f);
        assert(child->GetLocalTransform().position.x == -3.0f);
        // 前の親を動かしても、付け替えた部分木は動かない。新しい親を動かすと付いてくる
        rootA->SetLocalPosition(0.0f, 0.0f, 0.0f);
        rootB->SetLocalPosition(20.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        ExpectWorldX(rootA, 0.0f);
        ExpectWorldX(child, 17.0f);
        ExpectWorldX(grandChild, 21.0f);

        // 非アクティブにしても値は求め続ける（描画と Tick だけが止まる）。EffectiveActive は子孫まで落ちる
        child->SetActive(false);
        rootB->SetLocalPosition(30.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        ExpectWorldX(grandChild, 31.0f);
        assert(EffectiveActiveOf(world, rootB));
        assert(!EffectiveActiveOf(world, child));
        assert(!EffectiveActiveOf(world, grandChild));
        child->SetActive(true);
        world.UpdateWorldTransforms();
        assert(EffectiveActiveOf(world, grandChild));
        assert(world.VerifyEcsTransform());
        world.Finalize();
    }

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using HierarchyProbeBase = Component::Component;

    // 型に Tick の本体を登録する（World の順序付きの系が呼ぶ）
    template <typename T>
    void RegisterProbeTick(World &world, const char *name)
    {
        Component::ComponentTickRegistration registration;
        registration.Name = name;
        registration.Run = [](HierarchyProbeBase &component, float) { static_cast<T &>(component).OnTick(); };
        world.RegisterComponentTick(T::StaticClass(), registration);
    }

    // 段の最中に作った子（行の作成を予約中）を、局所の位置を決めてすぐ根へ付け替えても、ワールドの座標が保たれる
    class HierarchySpawnAndDetachProbe : public HierarchyProbeBase
    {
        REFLECTION_CLASS(HierarchySpawnAndDetachProbe, HierarchyProbeBase)

    public:
        Entity *Spawned = nullptr;
        bool bDone = false;

        void OnTick()
        {
            if (bDone)
            {
                return;
            }
            bDone = true;
            Entity *owner = GetOwner();
            World *world = owner->GetWorld();
            Spawned = world->SpawnEntity<Entity>(owner);
            Spawned->SetLocalPosition(2.0f, 0.0f, 0.0f);
            assert(world->ReparentEntity(Spawned, nullptr));
        }
    };
    IMPLEMENT_CLASS(HierarchySpawnAndDetachProbe, HierarchyProbeBase)

    float RunSpawnAndDetach()
    {
        World world;
        world.Initialize();
        RegisterProbeTick<HierarchySpawnAndDetachProbe>(world, "Test.SpawnAndDetach");
        auto *root = world.SpawnEntity<Entity>();
        auto *probe = world.CreateComponent<HierarchySpawnAndDetachProbe>(root);
        assert(probe != nullptr);
        root->SetLocalPosition(10.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(probe->Spawned != nullptr);
        // 付け替えは世界の位置を保つ: 親の 10 + 局所の 2
        const float x = probe->Spawned->GetWorldTransform().position.x;
        assert(probe->Spawned->GetParentEntity() == nullptr);
        world.UpdateWorldTransforms();
        assert(probe->Spawned->GetWorldTransform().position.x == x);
        world.Finalize();
        return x;
    }

    void TestSpawnedChildReparentKeepsPosition()
    {
        // 付け替えの前のワールドの位置（親の 10 + 局所の 2）が保たれる
        assert(RunSpawnAndDetach() == 12.0f);
    }

    // 同じ群の中で祖先が止まる・破棄予約になったら、後ろの子はそのとき呼ばれない
    enum class StopKind
    {
        SetActive,
        MarkForDestroy,
        Destroy,
        // ObjectHeap::EnqueueDestroy と同じ、Object の仮想呼び出し経由
        DestroyViaObject,
        // Object のフラグだけ立てて World の予約登録を直接呼ぶ経路
        RegisterOnly,
    };

    // 所有者の Entity を止める（同じ群で後ろに並ぶ子・孫の Component より先に呼ばれる）
    class HierarchyStopOwnerProbe : public HierarchyProbeBase
    {
        REFLECTION_CLASS(HierarchyStopOwnerProbe, HierarchyProbeBase)

    public:
        StopKind Kind = StopKind::SetActive;

        void OnTick()
        {
            Entity *owner = GetOwner();
            switch (Kind)
            {
            case StopKind::MarkForDestroy:
                owner->MarkForDestroy();
                break;
            case StopKind::Destroy:
                owner->Destroy();
                break;
            case StopKind::DestroyViaObject:
                static_cast<Object *>(owner)->Destroy();
                break;
            case StopKind::RegisterOnly:
                owner->SetFlag(OF_PendingDestroy, true);
                owner->GetWorld()->RegisterPendingDestroy(*owner);
                break;
            default:
                owner->SetActive(false);
                break;
            }
        }
    };
    IMPLEMENT_CLASS(HierarchyStopOwnerProbe, HierarchyProbeBase)

    // 破棄予約が ECS の破棄の印（プロパティ経由で読む値）まで立てるかを見るための窓口
    class FlagProbeEntity : public Entity
    {
    public:
        bool EcsPendingDestroyFlag() { return getbPendingDestroy().Get(); }
    };

    class HierarchyCountProbe : public HierarchyProbeBase
    {
        REFLECTION_CLASS(HierarchyCountProbe, HierarchyProbeBase)

    public:
        int *Counter = nullptr;

        void OnTick() { ++*Counter; }
    };
    IMPLEMENT_CLASS(HierarchyCountProbe, HierarchyProbeBase)

    int RunAncestorStop(StopKind kind)
    {
        World world;
        world.Initialize();
        RegisterProbeTick<HierarchyStopOwnerProbe>(world, "Test.StopOwner");
        RegisterProbeTick<HierarchyCountProbe>(world, "Test.Count");
        auto *parent = world.SpawnEntity<Entity>();
        auto *stopper = world.CreateComponent<HierarchyStopOwnerProbe>(parent);
        stopper->Kind = kind;
        int count = 0;
        auto *child = world.SpawnEntity<Entity>(parent);
        world.CreateComponent<HierarchyCountProbe>(child)->Counter = &count;
        auto *grandChild = world.SpawnEntity<Entity>(child);
        world.CreateComponent<HierarchyCountProbe>(grandChild)->Counter = &count;
        // 先に伝播して、全員が EffectiveActive を求めた状態にする
        world.UpdateWorldTransforms();
        world.UpdateWorldTransforms();
        world.Tick(0.01f);
        world.Finalize();
        return count;
    }

    void TestAncestorStopSuppressesDescendantsImmediately()
    {
        // 公開の Destroy()・Heap と同じ Object 経由の呼び出し・World の予約登録の直接呼び出しも、MarkForDestroy() と同じく
        // ECS の印と変更ログを更新して子・孫の呼び出しを止める
        for (const StopKind kind : {StopKind::SetActive, StopKind::MarkForDestroy, StopKind::Destroy,
                                    StopKind::DestroyViaObject, StopKind::RegisterOnly})
        {
            assert(RunAncestorStop(kind) == 0);
        }
    }

    // ObjectHeap 経由の予約も、公開の Destroy() と同じ入口を通って ECS の破棄の印が立つ
    void TestHeapEnqueueDestroySetsEcsFlag()
    {
        ObjectHeap heap;
        const ObjectHandle handle = heap.Create<FlagProbeEntity>();
        auto *entity = heap.Resolve<FlagProbeEntity>(handle);
        assert(entity != nullptr);
        assert(!entity->EcsPendingDestroyFlag());
        assert(heap.EnqueueDestroy(handle));
        assert(entity->EcsPendingDestroyFlag());
        assert(entity->IsPendingDestroy());
        heap.ProcessDestroyQueue();

        // 公開の Destroy() も同じ
        FlagProbeEntity direct;
        assert(!direct.EcsPendingDestroyFlag());
        direct.Destroy();
        assert(direct.EcsPendingDestroyFlag());
    }
}

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(4);

    TestDeepChain();
    TestIncrementalWork();
    TestReparent();
    TestActivePropagation();
    TestSceneEntryAndExit();
    TestLateSystemSeedsExistingRows();
    TestParallelMatchesSerialAndOrderIsDeterministic();
    TestParallelRunReportsNoPropertyAccess();
    TestSignedZeroMatchesReference();
    TestUpdateMatchesExpectedValuesAndVersions();
    TestSpawnedChildReparentKeepsPosition();
    TestAncestorStopSuppressesDescendantsImmediately();
    TestHeapEnqueueDestroySetsEcsFlag();

    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "EcsHierarchyTest 合格\n";
    return 0;
}
