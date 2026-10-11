// World に付いた全 Entity の ECS の行の寿命の契約を確かめる。
// - 付け替え・破棄・World の出入りで行が作られて消え、階層（親・子の並び・深さ）が Outer/Inner と一致する
// - 解放の前に行が切られる（Object の解放が始まる時点で、行は孤児で Object への参照が null、または消えている）
// - Tick と LateTick の間の除去・DestroyNow・破棄予約の直後の除去・部分木の連鎖でも孤児や古い参照が残らない
// - 段の最中の作成は同期点まで予約され、再生の前の破棄で取り消される
// - Component の行は所有者の子の葉で、flags・時間倍率・Tick の設定は行が付いている間だけ列にあり、外れると Object に戻る
// - World が行の番号と世代から発行する EntityHandle は、破棄の後・世代違い・別の World では解決されない
#include "Component/Component.h"
#include "Ecs/EcsWorld.h"
#include "Object/Entity.h"
#include "Object/ObjectHeap.h"
#include "Object/World.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using NorvesLib::Core::Ecs::EcsComponentFlags;
using NorvesLib::Core::Ecs::EcsEntity;
using NorvesLib::Core::Ecs::EcsEntityFlags;
using NorvesLib::Core::Ecs::EcsHierarchy;
using NorvesLib::Core::Ecs::EcsLocalTransform;
using NorvesLib::Core::Ecs::EcsObjectRef;
using NorvesLib::Core::Ecs::EcsTickConfig;
using NorvesLib::Core::Ecs::EcsTimeDilation;
using NorvesLib::Core::Ecs::EcsWorld;
using NorvesLib::Core::Ecs::EcsWorldTransform;

namespace
{
    // 照合する: 結び付けが一致していて、Entity と Component の行の数が期待どおり
    void ExpectBound(World &world, uint32_t expectedEntities, uint32_t expectedComponents = 0)
    {
        uint32_t count = 0;
        assert(world.VerifyEcsBinding(&count));
        assert(count == expectedEntities);
        assert(world.GetEcsWorld().GetEntityCount() == expectedEntities + expectedComponents);
    }

    const EcsHierarchy &HierarchyOf(World &world, Entity *entity)
    {
        const EcsHierarchy *hierarchy = world.GetEcsWorld().GetColumnValue<EcsHierarchy>(entity->GetEcsEntity());
        assert(hierarchy);
        return *hierarchy;
    }

    // 解放が始まった時点（EndPlay）の行の状態。Component が解放された後も読めるよう、テスト側に置く
    struct ReleaseReport
    {
        int Calls = 0;
        bool bRowAlive = false;
        bool bRowOrphan = false;
        bool bRefCleared = false;
        bool bParentRowAlive = false;
        bool bParentRowOrphan = false;
    };

    // 解放が始まった時点（EndPlay）の行の状態を報告する Component
    class ReleaseProbe : public Component::Component
    {
    public:
        World *Owner = nullptr;
        EcsEntity Row;
        EcsEntity ParentRow;
        ReleaseReport *Out = nullptr;

        void EndPlay() override
        {
            if (!Owner || !Out)
            {
                return;
            }
            EcsWorld &ecs = Owner->GetEcsWorld();
            ++Out->Calls;
            Out->bRowAlive = ecs.IsAlive(Row);
            Out->bRowOrphan = Out->bRowAlive && ecs.IsOrphan(Row);
            const EcsObjectRef *ref = Out->bRowAlive ? ecs.GetColumnValue<EcsObjectRef>(Row) : nullptr;
            Out->bRefCleared = ref && ref->Object == nullptr;
            Out->bParentRowAlive = ecs.IsAlive(ParentRow);
            Out->bParentRowOrphan = Out->bParentRowAlive && ecs.IsOrphan(ParentRow);
            Component::Component::EndPlay();
        }
    };

    ReleaseProbe *Probe(World &world, Entity *entity, ReleaseReport &report, Entity *parent = nullptr)
    {
        auto *probe = world.CreateComponent<ReleaseProbe>(entity);
        assert(probe);
        probe->Owner = &world;
        probe->Out = &report;
        probe->Row = entity->GetEcsEntity();
        probe->ParentRow = parent ? parent->GetEcsEntity() : EcsEntity{};
        return probe;
    }

    // 木: root ── a ── a1
    //          └─ b
    void TestBindAndHierarchy()
    {
        World world;
        world.Initialize();
        ExpectBound(world, 0);

        Entity *root = world.SpawnEntity<Entity>();
        assert(root->GetEcsEntity().IsNull() == false);
        assert(root->GetEcsRow().IsBound());
        Entity *a = world.SpawnEntity<Entity>(root);
        Entity *b = world.SpawnEntity<Entity>(root);
        Entity *a1 = world.SpawnEntity<Entity>(a);
        ExpectBound(world, 4);

        const EcsHierarchy &rootH = HierarchyOf(world, root);
        assert(rootH.Parent.IsNull() && rootH.Depth == 0);
        assert(rootH.FirstChild == a->GetEcsEntity() && rootH.LastChild == b->GetEcsEntity());
        assert(HierarchyOf(world, a).NextSibling == b->GetEcsEntity());
        assert(HierarchyOf(world, b).PrevSibling == a->GetEcsEntity());
        assert(HierarchyOf(world, a1).Depth == 2 && HierarchyOf(world, a1).Parent == a->GetEcsEntity());

        // 行は Object を指し、行 → Object と Object → 行が往復する
        const EcsObjectRef *ref = world.GetEcsWorld().GetColumnValue<EcsObjectRef>(a->GetEcsEntity());
        assert(ref && ref->Object == static_cast<void *>(a));

        // 2 つ目の根は兄弟の連結を持たない
        Entity *root2 = world.SpawnEntity<Entity>();
        assert(HierarchyOf(world, root2).PrevSibling.IsNull() && HierarchyOf(world, root).NextSibling.IsNull());
        ExpectBound(world, 5);

        world.Finalize();
        assert(world.GetEcsWorld().GetEntityCount() == 0);
    }

    // 付け替え: 子の並び・深さ・古い親の連結が直る
    void TestReparent()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *a = world.SpawnEntity<Entity>(root);
        Entity *b = world.SpawnEntity<Entity>(root);
        Entity *c = world.SpawnEntity<Entity>(root);
        Entity *a1 = world.SpawnEntity<Entity>(a);
        Entity *a11 = world.SpawnEntity<Entity>(a1);
        ExpectBound(world, 6);

        // 中ほどの兄弟を別の木へ移す
        assert(world.ReparentEntity(b, a1));
        ExpectBound(world, 6);
        assert(HierarchyOf(world, root).FirstChild == a->GetEcsEntity());
        assert(HierarchyOf(world, a).NextSibling == c->GetEcsEntity());
        assert(HierarchyOf(world, c).PrevSibling == a->GetEcsEntity());
        assert(HierarchyOf(world, b).Depth == 3 && HierarchyOf(world, b).Parent == a1->GetEcsEntity());
        // 子の並びは末尾に付く
        assert(HierarchyOf(world, a1).FirstChild == a11->GetEcsEntity() && HierarchyOf(world, a1).LastChild == b->GetEcsEntity());

        // 部分木ごと根へ。部分木の深さが全部直る
        assert(world.ReparentEntity(a, nullptr));
        ExpectBound(world, 6);
        assert(HierarchyOf(world, a).Parent.IsNull() && HierarchyOf(world, a).Depth == 0);
        assert(HierarchyOf(world, a11).Depth == 2 && HierarchyOf(world, b).Depth == 2);
        assert(HierarchyOf(world, root).FirstChild == c->GetEcsEntity() && HierarchyOf(world, root).LastChild == c->GetEcsEntity());

        // 根から子へ戻す
        assert(world.ReparentEntity(a, c));
        ExpectBound(world, 6);
        assert(HierarchyOf(world, a11).Depth == 4);
        world.Finalize();
        assert(world.GetEcsWorld().GetEntityCount() == 0);
    }

    // 段の外の除去: 解放の前に行が消えている。部分木は子から順、親の連結も直る
    void TestRemoveObjectBeforeRelease()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *a = world.SpawnEntity<Entity>(root);
        Entity *a1 = world.SpawnEntity<Entity>(a);
        Entity *b = world.SpawnEntity<Entity>(root);
        ReleaseReport rootReport;
        ReleaseReport a1Report;
        Probe(world, root, rootReport);
        Probe(world, a1, a1Report, a);
        const EcsEntity rootRow = root->GetEcsEntity();
        const EcsEntity a1Row = a1->GetEcsEntity();
        ExpectBound(world, 4, 2);

        // 子 a の部分木だけを除く（RemoveEntity）。親 root の連結は b だけになる
        const EcsEntity aRow = a->GetEcsEntity();
        assert(world.RemoveEntity(a));
        assert(a1Report.Calls == 1 && !a1Report.bRowAlive && !a1Report.bParentRowAlive);
        assert(!world.GetEcsWorld().IsAlive(aRow) && !world.GetEcsWorld().IsAlive(a1Row));
        ExpectBound(world, 2, 1);
        assert(HierarchyOf(world, root).FirstChild == b->GetEcsEntity() && HierarchyOf(world, root).LastChild == b->GetEcsEntity());

        world.RemoveObject(root);
        assert(rootReport.Calls == 1 && !rootReport.bRowAlive);
        assert(!world.GetEcsWorld().IsAlive(rootRow));
        ExpectBound(world, 0);
        world.Finalize();
    }

    // Tick と LateTick の間の除去・予約の直後の除去
    void TestRemoveBetweenTickAndLateTick()
    {
        World world;
        world.Initialize();
        Entity *keep = world.SpawnEntity<Entity>();
        Entity *victim = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(victim);
        Entity *reserved = world.SpawnEntity<Entity>(keep);
        const EcsEntity victimRow = victim->GetEcsEntity();
        const EcsEntity childRow = child->GetEcsEntity();
        const EcsEntity reservedRow = reserved->GetEcsEntity();
        ReleaseReport childReport;
        ReleaseReport reservedReport;
        Probe(world, child, childReport, victim);
        Probe(world, reserved, reservedReport, keep);
        ExpectBound(world, 4, 2);

        world.Tick(0.01f);
        ExpectBound(world, 4, 2);

        // Tick の後・LateTick の前に、段の外から除く
        world.RemoveObject(victim);
        assert(childReport.Calls == 1 && !childReport.bRowAlive);
        assert(!world.GetEcsWorld().IsAlive(victimRow) && !world.GetEcsWorld().IsAlive(childRow));
        ExpectBound(world, 2, 1);

        // 破棄を予約した直後（回収の前）に除く: 予約の一覧にも残らず、孤児も残らない
        reserved->MarkForDestroy();
        assert(world.RemoveEntity(reserved));
        assert(reservedReport.Calls == 1 && !reservedReport.bRowAlive);
        assert(!world.GetEcsWorld().IsAlive(reservedRow));
        ExpectBound(world, 1);
        assert(HierarchyOf(world, keep).FirstChild.IsNull() && HierarchyOf(world, keep).LastChild.IsNull());

        world.LateTick(0.01f);
        ExpectBound(world, 1);
        world.Tick(0.01f);
        world.LateTick(0.01f);
        ExpectBound(world, 1);
        world.Finalize();
    }

    // 段の最中の回収（cleanup）: 解放の時点で行は孤児で Object への参照は null。段の同期点で消える
    void TestCleanupInStageOrphansBeforeRelease()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *mid = world.SpawnEntity<Entity>(root);
        Entity *leaf = world.SpawnEntity<Entity>(mid);
        Entity *sibling = world.SpawnEntity<Entity>(root);
        ReleaseReport leafReport;
        ReleaseReport midReport;
        Probe(world, leaf, leafReport, mid);
        Probe(world, mid, midReport, root);
        const EcsEntity midRow = mid->GetEcsEntity();
        const EcsEntity leafRow = leaf->GetEcsEntity();
        ExpectBound(world, 4, 2);

        // 祖先と子孫を両方予約する（最上位だけが部分木ごと破棄される）
        leaf->MarkForDestroy();
        mid->MarkForDestroy();
        world.Tick(0.01f);

        assert(leafReport.Calls == 1 && leafReport.bRowAlive && leafReport.bRowOrphan && leafReport.bRefCleared);
        assert(leafReport.bParentRowAlive && leafReport.bParentRowOrphan);
        assert(midReport.Calls == 1 && midReport.bRowAlive && midReport.bRowOrphan && midReport.bRefCleared);
        // 同期点で孤児の行が消える
        assert(!world.GetEcsWorld().IsAlive(midRow) && !world.GetEcsWorld().IsAlive(leafRow));
        ExpectBound(world, 2);
        assert(HierarchyOf(world, root).FirstChild == sibling->GetEcsEntity());
        assert(HierarchyOf(world, root).LastChild == sibling->GetEcsEntity());
        world.LateTick(0.01f);
        ExpectBound(world, 2);
        world.Finalize();
    }

    // heap が持つ Component の遅延回収: Entity の行は除去の時点で消え、heap は後で回収する
    void TestHeapOwnedComponentDeferredCollection()
    {
        World world;
        world.Initialize();
        ObjectHeap heap;
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);

        const ObjectHandle rootHandle = heap.Create<ReleaseProbe>();
        const ObjectHandle childHandle = heap.Create<ReleaseProbe>();
        auto *rootProbe = heap.Resolve<ReleaseProbe>(rootHandle);
        auto *childProbe = heap.Resolve<ReleaseProbe>(childHandle);
        assert(rootProbe && childProbe);
        assert(root->AddComponent(rootProbe) && child->AddComponent(childProbe));
        ReleaseReport rootReport;
        ReleaseReport childReport;
        rootProbe->Owner = childProbe->Owner = &world;
        rootProbe->Out = &rootReport;
        childProbe->Out = &childReport;
        rootProbe->Row = root->GetEcsEntity();
        childProbe->Row = child->GetEcsEntity();
        childProbe->ParentRow = root->GetEcsEntity();
        ExpectBound(world, 2, 2);

        world.Tick(0.01f);
        world.RemoveObject(root);
        ExpectBound(world, 0);
        // heap の Component は PendingDestroy のまま残り、解放は heap の回収まで遅れる。行はもう無い
        assert(rootReport.Calls == 0 && childReport.Calls == 0);
        world.LateTick(0.01f);
        heap.CollectGarbage();
        assert(!heap.Resolve(rootHandle) && !heap.Resolve(childHandle));
        assert(rootReport.Calls == 1 && !rootReport.bRowAlive);
        assert(childReport.Calls == 1 && !childReport.bRowAlive && !childReport.bParentRowAlive);
        ExpectBound(world, 0);
        world.Finalize();
    }

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using StageProbeBase = Component::Component;

    // 型に Tick の本体を登録する（World の順序付きの系が呼ぶ）
    template <typename T>
    void RegisterStageTick(World &world, const char *name)
    {
        Component::ComponentTickRegistration registration;
        registration.Name = name;
        registration.Run = [](StageProbeBase &component, float) { static_cast<T &>(component).OnTick(); };
        world.RegisterComponentTick(T::StaticClass(), registration);
    }

    // 段の最中に Attach*/Reparent/Remove を行う Component。行の作成は同期点まで予約になる
    class StageMutator : public StageProbeBase
    {
        REFLECTION_CLASS(StageMutator, StageProbeBase)

    public:
        Entity *Parent = nullptr;
        Entity *Existing = nullptr;
        Entity *A = nullptr;
        Entity *B = nullptr;
        Entity *C = nullptr;
        bool bRan = false;
        // 段の最中に見えた状態
        bool bNewRowsPending = false;
        bool bCancelledRowless = false;
        bool bMoved = false;

        void OnTick()
        {
            if (bRan)
            {
                return;
            }
            bRan = true;
            World *world = GetOwner()->GetWorld();
            A = world->SpawnEntity<Entity>(Parent);
            B = world->SpawnEntity<Entity>(A);
            Entity *cancelled = world->SpawnEntity<Entity>(Parent);
            Entity *cancelledChild = world->SpawnEntity<Entity>(cancelled);
            C = world->SpawnEntity<Entity>(Parent);
            // 段の最中は予約だけで、行はまだ無い
            bNewRowsPending = A->GetEcsEntity().IsNull() && B->GetEcsEntity().IsNull() && C->GetEcsEntity().IsNull();
            bCancelledRowless = cancelled->GetEcsEntity().IsNull() && cancelledChild->GetEcsEntity().IsNull();
            // 既に行を持つ Entity を、予約中の親の下へ付け替える
            bMoved = world->ReparentEntity(Existing, B);
            // 再生の前に破棄: 予約は取り消され、行は作られない
            world->RemoveEntity(cancelled);
        }
    };
    IMPLEMENT_CLASS(StageMutator, StageProbeBase)

    // 段の最中の作成は予約され、同期点で再生されて行と階層ができる。再生の前に破棄した分は作られない
    void TestReservedCreateAndCancelInStage()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *holder = world.SpawnEntity<Entity>(root);
        Entity *existing = world.SpawnEntity<Entity>(holder);
        RegisterStageTick<StageMutator>(world, "Test.StageMutator");
        auto *mutator = world.CreateComponent<StageMutator>(root);
        assert(mutator);
        mutator->Parent = root;
        mutator->Existing = existing;
        ExpectBound(world, 3, 1);

        world.Tick(0.01f);
        assert(mutator->bRan && mutator->bNewRowsPending && mutator->bCancelledRowless && mutator->bMoved);

        // 同期点の後: A・B・C に行ができ、取り消した 2 つは作られていない（root・holder・existing・A・B・C の 6 つ）
        ExpectBound(world, 6, 1);
        Entity *a = mutator->A;
        Entity *b = mutator->B;
        Entity *c = mutator->C;
        assert(a->GetEcsRow().IsBound() && b->GetEcsRow().IsBound() && c->GetEcsRow().IsBound());
        // root の子は Inner の並び（holder, A, C）のとおりに連なる
        assert(HierarchyOf(world, root).FirstChild == holder->GetEcsEntity());
        assert(HierarchyOf(world, holder).NextSibling == a->GetEcsEntity());
        assert(HierarchyOf(world, a).NextSibling == c->GetEcsEntity());
        assert(HierarchyOf(world, root).LastChild == c->GetEcsEntity());
        assert(HierarchyOf(world, a).Depth == 1 && HierarchyOf(world, c).Depth == 1);
        // 予約中の親へ付け替えた Entity は、親の行ができてから結び直される
        assert(HierarchyOf(world, holder).FirstChild.IsNull() && HierarchyOf(world, holder).LastChild.IsNull());
        assert(HierarchyOf(world, b).Depth == 2 && HierarchyOf(world, b).FirstChild == existing->GetEcsEntity());
        assert(HierarchyOf(world, existing).Parent == b->GetEcsEntity() && HierarchyOf(world, existing).Depth == 3);

        world.LateTick(0.01f);
        ExpectBound(world, 6, 1);
        world.Finalize();
        assert(world.GetEcsWorld().GetEntityCount() == 0);
    }

    // Tick と LateTick の間の DestroyNow: heap の Component を即時に破棄しても、持ち主の行と階層は壊れない。
    // 持ち主を除去した後の DestroyNow でも、残った行や古い参照が無い
    void TestDestroyNowBetweenTickAndLateTick()
    {
        World world;
        world.Initialize();
        ObjectHeap heap;
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);
        const ObjectHandle liveHandle = heap.Create<ReleaseProbe>();
        const ObjectHandle detachedHandle = heap.Create<ReleaseProbe>();
        auto *liveProbe = heap.Resolve<ReleaseProbe>(liveHandle);
        auto *detachedProbe = heap.Resolve<ReleaseProbe>(detachedHandle);
        assert(liveProbe && detachedProbe);
        assert(child->AddComponent(liveProbe) && root->AddComponent(detachedProbe));
        ReleaseReport liveReport;
        ReleaseReport detachedReport;
        liveProbe->Owner = detachedProbe->Owner = &world;
        liveProbe->Out = &liveReport;
        detachedProbe->Out = &detachedReport;
        liveProbe->Row = child->GetEcsEntity();
        liveProbe->ParentRow = root->GetEcsEntity();
        detachedProbe->Row = root->GetEcsEntity();
        const EcsEntity rootRow = root->GetEcsEntity();
        const EcsEntity childRow = child->GetEcsEntity();
        ExpectBound(world, 2, 2);

        world.Tick(0.01f);
        // 持ち主が生きている間の DestroyNow: EndPlay の時点で行は生きていて孤児ではない。階層もそのまま
        assert(heap.DestroyNow(liveHandle));
        assert(liveReport.Calls == 1 && liveReport.bRowAlive && !liveReport.bRowOrphan && liveReport.bParentRowAlive);
        assert(!heap.Resolve(liveHandle));
        ExpectBound(world, 2, 1);
        assert(child->GetEcsEntity() == childRow && root->GetEcsEntity() == rootRow);
        assert(HierarchyOf(world, root).FirstChild == childRow && HierarchyOf(world, child).Parent == rootRow);

        // 持ち主を除去した後（heap の Component は PendingDestroy のまま残る）の DestroyNow
        world.RemoveObject(root);
        ExpectBound(world, 0);
        assert(detachedReport.Calls == 0);
        assert(heap.DestroyNow(detachedHandle));
        assert(detachedReport.Calls == 1 && !detachedReport.bRowAlive);
        assert(!world.GetEcsWorld().IsAlive(rootRow) && !world.GetEcsWorld().IsAlive(childRow));
        ExpectBound(world, 0);

        world.LateTick(0.01f);
        ExpectBound(world, 0);
        world.Finalize();
    }

    // 公開の RemoveInner で外した部分木は行を失い、AddObject で付け直すと行が作り直される
    void TestPublicRemoveInnerAndReattach()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);
        Entity *other = world.SpawnEntity<Entity>();
        const EcsEntity oldRoot = root->GetEcsEntity();
        const EcsEntity oldChild = child->GetEcsEntity();
        ExpectBound(world, 3);

        // 子 Entity を木から外す（ObjectHeap の解放が通る入口と同じ）。root の連結が空になる
        assert(root->RemoveInner(child));
        assert(child->GetEcsEntity().IsNull() && !world.GetEcsWorld().IsAlive(oldChild));
        assert(HierarchyOf(world, root).FirstChild.IsNull() && HierarchyOf(world, root).LastChild.IsNull());
        ExpectBound(world, 2);
        assert(world.AddObject(child));
        ExpectBound(world, 3);
        assert(HierarchyOf(world, child).Parent.IsNull());

        // World 直下から外す
        assert(world.RemoveInner(root));
        assert(root->GetEcsEntity().IsNull() && !world.GetEcsWorld().IsAlive(oldRoot));
        ExpectBound(world, 2);
        assert(world.AddObject(root));
        ExpectBound(world, 3);
        assert(root->GetEcsEntity() != oldRoot);
        (void)other;
        world.Finalize();
        assert(world.GetEcsWorld().GetEntityCount() == 0);
    }

    // EntityHandle: World が行の番号と世代から発行し、付け替えでは変わらない。
    // 破棄の後・世代の違う・別の World のハンドルは解決しない
    void TestEntityHandleIssueAndResolve()
    {
        World world;
        world.Initialize();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);
        const EntityHandle rootHandle = root->GetEntityHandle();
        const EntityHandle childHandle = world.GetEntityHandle(*child);
        assert(rootHandle.IsValid() && childHandle.IsValid() && rootHandle != childHandle);
        assert(rootHandle.SlotIndex == root->GetEcsEntity().Index && rootHandle.Generation == root->GetEcsEntity().Generation);
        assert(world.ResolveEntityHandle(rootHandle) == root);
        assert(world.ResolveEntityHandle(childHandle) == child);
        assert(world.ResolveEntityHandle(EntityHandle::Invalid()) == nullptr);

        // 付け替えでは行もハンドルも変わらない
        assert(world.ReparentEntity(child, nullptr));
        assert(child->GetEntityHandle() == childHandle);
        assert(world.ResolveEntityHandle(childHandle) == child);

        // 世代の違うハンドルは拒む
        EntityHandle wrongGeneration = childHandle;
        ++wrongGeneration.Generation;
        assert(world.ResolveEntityHandle(wrongGeneration) == nullptr);

        // 破棄（予約から同期点での除去）の後は解決しない。予約の間はまだ解決する
        child->MarkForDestroy();
        assert(world.ResolveEntityHandle(childHandle) == child);
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(world.ResolveEntityHandle(childHandle) == nullptr);

        // 同じ行の番号が再利用されても、古いハンドルは新しい Entity を指さない
        Entity *reused = world.SpawnEntity<Entity>();
        const EntityHandle reusedHandle = reused->GetEntityHandle();
        assert(reusedHandle.IsValid() && reusedHandle != childHandle);
        assert(world.ResolveEntityHandle(childHandle) == nullptr);
        assert(world.ResolveEntityHandle(reusedHandle) == reused);

        // 段の外の即時の除去でも解決しなくなる
        world.RemoveObject(root);
        assert(world.ResolveEntityHandle(rootHandle) == nullptr);

        // 別の World: 同じ行の番号と世代の Entity がいても、識別子が違うので解決しない
        World other;
        other.Initialize();
        Entity *otherEntity = other.SpawnEntity<Entity>();
        const EntityHandle otherHandle = otherEntity->GetEntityHandle();
        assert(otherHandle.IsValid() && otherHandle.WorldId != reusedHandle.WorldId);
        EntityHandle sameRowOtherWorld = otherHandle;
        sameRowOtherWorld.WorldId = reusedHandle.WorldId;
        assert(other.ResolveEntityHandle(sameRowOtherWorld) == nullptr);
        assert(other.ResolveEntityHandle(reusedHandle) == nullptr);
        assert(world.ResolveEntityHandle(otherHandle) == nullptr);
        assert(other.ResolveEntityHandle(otherHandle) == otherEntity);

        other.Finalize();
        world.Finalize();
        assert(world.ResolveEntityHandle(reusedHandle) == nullptr);
    }

    // 世代: 消した行の識別子は、同じ番号が再利用されても古い識別子として拒まれる
    void TestStaleRowRejected()
    {
        World world;
        world.Initialize();
        Entity *first = world.SpawnEntity<Entity>();
        const EcsEntity firstRow = first->GetEcsEntity();
        world.RemoveObject(first);
        Entity *second = world.SpawnEntity<Entity>();
        assert(second->GetEcsEntity() != firstRow);
        assert(!world.GetEcsWorld().IsAlive(firstRow));
        assert(world.GetEcsWorld().IsAlive(second->GetEcsEntity()));
        ExpectBound(world, 1);
        world.Finalize();
    }

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ComponentBase = Component::Component;
    namespace TickGroups = NorvesLib::Core::Component;

    // 段の最中に Component を足す Component。行の作成は同期点まで予約になり、Tick の設定は行ができた後も保たれる
    class StageComponentAdder : public ComponentBase
    {
        REFLECTION_CLASS(StageComponentAdder, ComponentBase)

    public:
        ComponentBase *Added = nullptr;
        ComponentBase *Cancelled = nullptr;
        bool bRan = false;
        bool bAddedRowlessInStage = false;
        bool bConfigKeptInStage = false;

        void OnTick()
        {
            if (bRan)
            {
                return;
            }
            bRan = true;
            Entity *owner = GetOwner();
            Added = new ReleaseProbe();
            Added->Disable();
            Added->SetTickGroup(TickGroups::ETickGroup::Camera);
            Added->SetTickPriority(-3);
            assert(owner->AddComponent(Added));
            // 段の最中は予約だけで、行はまだ無い。値は Object にあるので読める
            bAddedRowlessInStage = Added->GetEcsEntity().IsNull();
            bConfigKeptInStage = !Added->IsEnabled() && Added->GetTickGroup() == TickGroups::ETickGroup::Camera &&
                                 Added->GetTickPriority() == -3;
            // 再生の前に外す: 予約は取り消され、行は作られない
            Cancelled = new ReleaseProbe();
            assert(owner->AddComponent(Cancelled));
            owner->RemoveComponent(Cancelled);
        }
    };
    IMPLEMENT_CLASS(StageComponentAdder, ComponentBase)

    const EcsComponentFlags &ComponentFlagsOf(World &world, Component::Component *component)
    {
        const EcsComponentFlags *flags = world.GetEcsWorld().GetColumnValue<EcsComponentFlags>(component->GetEcsEntity());
        assert(flags);
        return *flags;
    }

    const EcsTickConfig &TickConfigOf(World &world, Component::Component *component)
    {
        const EcsTickConfig *config = world.GetEcsWorld().GetColumnValue<EcsTickConfig>(component->GetEcsEntity());
        assert(config);
        return *config;
    }

    // flags・時間倍率・Tick の設定: 行が付く前は Object の予備、付いたら列、外れたら Object に戻る。
    // Component の行は所有者の子の葉で、所有者の Component の連結に Inner の並びで入る
    void TestFlagsAndTickConfigColumns()
    {
        World world;
        world.Initialize();
        EcsWorld &ecs = world.GetEcsWorld();

        // World に付く前に設定した値は、行が付くと列にある
        Entity *entity = new Entity();
        auto *first = new ReleaseProbe();
        auto *second = new ReleaseProbe();
        entity->SetActive(false);
        entity->SetTickEnabled(false);
        assert(entity->SetCustomTimeDilation(2.0f));
        first->Disable();
        first->SetTickEnabled(false);
        assert(first->SetTickGroup(Component::ETickGroup::Animation));
        first->SetTickPriority(7);
        const Component::TickGroupMask firstMask = Component::TickGroupBit(Component::ETickGroup::Animation) |
                                                   Component::TickGroupBit(Component::ETickGroup::Camera);
        assert(first->SetTickGroupMask(firstMask));
        assert(entity->AddComponent(first) && entity->AddComponent(second));
        assert(world.AddObject(entity));
        ExpectBound(world, 1, 2);

        const EcsEntity entityRow = entity->GetEcsEntity();
        const EcsEntityFlags *entityFlags = ecs.GetColumnValue<EcsEntityFlags>(entityRow);
        assert(entityFlags && !entityFlags->bActive && !entityFlags->bTickEnabled && !entityFlags->bPendingDestroy);
        assert(ecs.GetColumnValue<EcsTimeDilation>(entityRow)->CustomTimeDilation == 2.0f);
        assert(!ComponentFlagsOf(world, first).bEnabled && !ComponentFlagsOf(world, first).bTickEnabled);
        assert(TickConfigOf(world, first).Group == static_cast<uint8_t>(Component::ETickGroup::Animation));
        assert(TickConfigOf(world, first).Priority == 7 && TickConfigOf(world, first).Mask == firstMask);
        // 何も設定しなかった Component は既定のまま（有効・Tick 有効・Default 群）
        assert(ComponentFlagsOf(world, second).bEnabled && ComponentFlagsOf(world, second).bTickEnabled);
        assert(TickConfigOf(world, second).Group == static_cast<uint8_t>(Component::ETickGroup::Default));
        assert(TickConfigOf(world, second).Mask == Component::TickGroupBit(Component::ETickGroup::Default));

        // Component の行は所有者の子の葉で、Inner の並びで連なる。子 Entity の連結には入らない
        const EcsHierarchy &entityHierarchy = HierarchyOf(world, entity);
        assert(entityHierarchy.FirstChild.IsNull() && entityHierarchy.LastChild.IsNull());
        assert(entityHierarchy.FirstComponent == first->GetEcsEntity() && entityHierarchy.LastComponent == second->GetEcsEntity());
        const EcsHierarchy *firstHierarchy = ecs.GetColumnValue<EcsHierarchy>(first->GetEcsEntity());
        assert(firstHierarchy && firstHierarchy->Parent == entityRow && firstHierarchy->Depth == 1);
        assert(firstHierarchy->Kind == Ecs::kEcsHierarchyKindComponent && firstHierarchy->NextSibling == second->GetEcsEntity());
        assert(ecs.GetColumnValue<EcsObjectRef>(first->GetEcsEntity())->Object == static_cast<void *>(first));

        // 行が付いている間は列が正: 書き込みは列に出て、読み口も列を読む
        entity->SetActive(true);
        entity->SetTickEnabled(true);
        assert(entity->SetCustomTimeDilation(0.5f));
        assert(!entity->SetCustomTimeDilation(-1.0f));
        first->Enable();
        first->SetTickEnabled(true);
        assert(first->SetTickGroup(Component::ETickGroup::Camera));
        assert(!first->SetTickGroupMask(Component::TickGroupBit(Component::ETickGroup::Input)));
        first->SetTickPriority(-2);
        assert(ecs.GetColumnValue<EcsEntityFlags>(entityRow)->bActive && ecs.GetColumnValue<EcsEntityFlags>(entityRow)->bTickEnabled);
        assert(ecs.GetColumnValue<EcsTimeDilation>(entityRow)->CustomTimeDilation == 0.5f);
        assert(entity->IsActive() && entity->IsTickEnabled() && entity->GetCustomTimeDilation() == 0.5f);
        assert(ComponentFlagsOf(world, first).bEnabled && ComponentFlagsOf(world, first).bTickEnabled);
        // 主群を変えると mask は 1 群へ戻る（従来の規則）
        assert(TickConfigOf(world, first).Group == static_cast<uint8_t>(Component::ETickGroup::Camera));
        assert(TickConfigOf(world, first).Mask == Component::TickGroupBit(Component::ETickGroup::Camera));
        assert(TickConfigOf(world, first).Priority == -2);
        assert(first->IsEnabled() && first->IsTickEnabled() && first->GetTickGroup() == Component::ETickGroup::Camera);
        assert(first->GetTickPriority() == -2);
        entity->MarkForDestroy();
        assert(ecs.GetColumnValue<EcsEntityFlags>(entityRow)->bPendingDestroy && entity->IsPendingDestroy());

        // 木から外すと値は Object に戻り、読み口はそのまま同じ値を返す
        assert(world.RemoveInner(entity));
        assert(entity->GetEcsEntity().IsNull() && first->GetEcsEntity().IsNull() && second->GetEcsEntity().IsNull());
        ExpectBound(world, 0, 0);
        assert(entity->IsActive() && entity->IsTickEnabled() && entity->GetCustomTimeDilation() == 0.5f);
        assert(first->IsEnabled() && first->IsTickEnabled() && first->GetTickGroup() == Component::ETickGroup::Camera);
        assert(first->GetTickPriority() == -2 && first->GetTickGroupMask() == Component::TickGroupBit(Component::ETickGroup::Camera));
        world.Finalize();
        delete entity;
    }

    // 変換の値: World に付く前の値は行が付くと列にあり、書き込みも読み口も列を通り、外すと Object に戻る
    void TestTransformColumns()
    {
        using NorvesLib::Math::Quaternion;
        using NorvesLib::Math::Vector3;
        World world;
        world.Initialize();
        EcsWorld &ecs = world.GetEcsWorld();

        Entity *entity = new Entity();
        entity->SetLocalPosition(1.0f, 2.0f, 3.0f);
        entity->SetLocalRotation(0.0f, 0.0f, 0.70710678f, 0.70710678f);
        entity->SetLocalScale(2.0f, 3.0f, 4.0f);
        assert(world.AddObject(entity));

        const EcsEntity row = entity->GetEcsEntity();
        const EcsLocalTransform *local = ecs.GetColumnValue<EcsLocalTransform>(row);
        assert(local && local->position == Vector3(1.0f, 2.0f, 3.0f) && local->scale == Vector3(2.0f, 3.0f, 4.0f));
        assert(local->rotation == Quaternion(0.0f, 0.0f, 0.70710678f, 0.70710678f));
        assert(ecs.GetColumnValue<EcsWorldTransform>(row) != nullptr);

        // 書き込みは列に出る（列の値を直接見る）。局所の変換の取得は値で返る
        entity->SetLocalPosition(5.0f, 6.0f, 7.0f);
        assert(ecs.GetColumnValue<EcsLocalTransform>(row)->position == Vector3(5.0f, 6.0f, 7.0f));
        assert(entity->GetLocalTransform().position == Vector3(5.0f, 6.0f, 7.0f));
        entity->SetPosition(8.0f, 9.0f, 10.0f);
        assert(ecs.GetColumnValue<EcsLocalTransform>(row)->position == Vector3(8.0f, 9.0f, 10.0f));

        // ワールドのキャッシュは列にあり、更新後に取得と列が一致する。子の値も列にある（World が持つ木で確かめる）
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);
        root->SetLocalPosition(8.0f, 9.0f, 10.0f);
        root->SetLocalScale(2.0f, 3.0f, 4.0f);
        child->SetLocalPosition(1.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        const EcsWorldTransform *cache = ecs.GetColumnValue<EcsWorldTransform>(root->GetEcsEntity());
        assert(cache && cache->position == root->GetWorldTransform().position && cache->scale == Vector3(2.0f, 3.0f, 4.0f));
        const EcsWorldTransform *childCache = ecs.GetColumnValue<EcsWorldTransform>(child->GetEcsEntity());
        assert(childCache && childCache->position == child->GetWorldTransform().position);
        assert(!(childCache->position == child->GetLocalTransform().position));

        // 親が動くと子のキャッシュが列の中で追従する（再帰は列を読み書きする）
        const NorvesLib::Math::Transform childBefore = child->GetWorldTransform();
        root->SetLocalPosition(0.0f, 0.0f, 0.0f);
        world.UpdateWorldTransforms();
        assert(!(child->GetWorldTransform() == childBefore));
        assert(ecs.GetColumnValue<EcsWorldTransform>(child->GetEcsEntity())->position == child->GetWorldTransform().position);

        // 木から外すと値は Object に戻り、読み口は同じ値を返す
        const NorvesLib::Math::Transform localBefore = entity->GetLocalTransform();
        const NorvesLib::Math::Transform worldBefore = entity->GetWorldTransform();
        assert(world.RemoveInner(entity));
        assert(entity->GetEcsEntity().IsNull());
        assert(entity->GetLocalTransform() == localBefore && entity->GetWorldTransform() == worldBefore);
        entity->SetLocalPosition(4.0f, 4.0f, 4.0f);
        assert(entity->GetLocalTransform().position == Vector3(4.0f, 4.0f, 4.0f));

        // 付け直すと、外している間に書いた値が列に入る
        assert(world.AddObject(entity));
        assert(ecs.GetColumnValue<EcsLocalTransform>(entity->GetEcsEntity())->position == Vector3(4.0f, 4.0f, 4.0f));
        assert(world.RemoveInner(entity));
        world.Finalize();
        delete entity;
    }

    // World の中での Component の追加・除去: 行が作られて消え、所有者の連結が直る。段の最中の追加は予約になる
    void TestComponentRowsAddRemoveAndReserve()
    {
        World world;
        world.Initialize();
        EcsWorld &ecs = world.GetEcsWorld();
        Entity *root = world.SpawnEntity<Entity>();
        Entity *child = world.SpawnEntity<Entity>(root);
        auto *a = world.CreateComponent<ReleaseProbe>(root);
        auto *b = world.CreateComponent<ReleaseProbe>(root);
        auto *c = world.CreateComponent<ReleaseProbe>(root);
        ExpectBound(world, 2, 3);
        assert(HierarchyOf(world, root).FirstChild == child->GetEcsEntity());
        assert(HierarchyOf(world, root).FirstComponent == a->GetEcsEntity() && HierarchyOf(world, root).LastComponent == c->GetEcsEntity());

        // 中ほどを外すと、前後がつながる
        const EcsEntity bRow = b->GetEcsEntity();
        root->RemoveComponent(b);
        assert(!ecs.IsAlive(bRow));
        ExpectBound(world, 2, 2);
        assert(ecs.GetColumnValue<EcsHierarchy>(a->GetEcsEntity())->NextSibling == c->GetEcsEntity());
        assert(ecs.GetColumnValue<EcsHierarchy>(c->GetEcsEntity())->PrevSibling == a->GetEcsEntity());

        // 末尾・先頭を外す
        root->RemoveComponent(c);
        assert(HierarchyOf(world, root).LastComponent == a->GetEcsEntity());
        root->RemoveComponent(a);
        assert(HierarchyOf(world, root).FirstComponent.IsNull() && HierarchyOf(world, root).LastComponent.IsNull());
        ExpectBound(world, 2, 0);

        // 部分木を付け替えても、Component の行の深さが直る
        auto *leaf = world.CreateComponent<ReleaseProbe>(child);
        ExpectBound(world, 2, 1);
        assert(ecs.GetColumnValue<EcsHierarchy>(leaf->GetEcsEntity())->Depth == 2);
        assert(world.ReparentEntity(child, nullptr));
        assert(ecs.GetColumnValue<EcsHierarchy>(leaf->GetEcsEntity())->Depth == 1);
        ExpectBound(world, 2, 1);

        // 段の最中に足した Component は同期点まで予約で、再生の前に外した分は作られない
        RegisterStageTick<StageComponentAdder>(world, "Test.StageComponentAdder");
        auto *adder = world.CreateComponent<StageComponentAdder>(root);
        ExpectBound(world, 2, 2);
        world.Tick(0.01f);
        assert(adder->bRan && adder->bAddedRowlessInStage && adder->bConfigKeptInStage);
        ExpectBound(world, 2, 3);
        Component::Component *added = adder->Added;
        assert(added->GetEcsRow().IsBound() && adder->Cancelled != nullptr);
        assert(!ComponentFlagsOf(world, added).bEnabled && ComponentFlagsOf(world, added).bTickEnabled);
        assert(TickConfigOf(world, added).Group == static_cast<uint8_t>(Component::ETickGroup::Camera));
        assert(TickConfigOf(world, added).Priority == -3);
        assert(HierarchyOf(world, root).FirstComponent == adder->GetEcsEntity());
        assert(HierarchyOf(world, root).LastComponent == added->GetEcsEntity());
        assert(ecs.GetColumnValue<EcsHierarchy>(added->GetEcsEntity())->Depth == 1);

        // 破棄予約した Component は、回収の時点で行が消える
        added->MarkForDestroy();
        world.LateTick(0.01f);
        world.Tick(0.01f);
        ExpectBound(world, 2, 2);
        assert(HierarchyOf(world, root).LastComponent == adder->GetEcsEntity());

        // Entity ごと破棄すると、Component の行も残らない
        root->MarkForDestroy();
        world.LateTick(0.01f);
        world.Tick(0.01f);
        ExpectBound(world, 1, 1);
        world.Finalize();
        assert(ecs.GetEntityCount() == 0);
    }

    // 周回: 作って消すのを繰り返しても、行・チャンクの数が戻る
    void TestCycleReturnsToZero()
    {
        World world;
        world.Initialize();
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            Entity *root = world.SpawnEntity<Entity>();
            for (int i = 0; i < 50; ++i)
            {
                Entity *child = world.SpawnEntity<Entity>(root);
                world.SpawnEntity<Entity>(child);
            }
            world.Tick(0.01f);
            world.LateTick(0.01f);
            ExpectBound(world, 101);
            root->MarkForDestroy();
            world.Tick(0.01f);
            world.LateTick(0.01f);
            ExpectBound(world, 0);
        }
        assert(world.GetEcsWorld().GetEntityCount() == 0);
        world.Finalize();
    }
}

int main()
{
    TestBindAndHierarchy();
    TestReparent();
    TestRemoveObjectBeforeRelease();
    TestRemoveBetweenTickAndLateTick();
    TestCleanupInStageOrphansBeforeRelease();
    TestHeapOwnedComponentDeferredCollection();
    TestReservedCreateAndCancelInStage();
    TestDestroyNowBetweenTickAndLateTick();
    TestPublicRemoveInnerAndReattach();
    TestStaleRowRejected();
    TestEntityHandleIssueAndResolve();
    TestFlagsAndTickConfigColumns();
    TestTransformColumns();
    TestComponentRowsAddRemoveAndReserve();
    TestCycleReturnsToZero();
    std::cout << "EcsLifetimeTest 合格\n";
    return 0;
}
