// World に付いた全 Entity の ECS の行の寿命の契約を確かめる。
// - 付け替え・破棄・World の出入りで行が作られて消え、階層（親・子の並び・深さ）が Outer/Inner と一致する
// - 解放の前に行が切られる（Object の解放が始まる時点で、行は孤児で Object への参照が null、または消えている）
// - Tick と LateTick の間の除去・破棄予約の直後の除去・部分木の連鎖でも孤児や古い参照が残らない
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
using NorvesLib::Core::Ecs::EcsEntity;
using NorvesLib::Core::Ecs::EcsHierarchy;
using NorvesLib::Core::Ecs::EcsObjectRef;
using NorvesLib::Core::Ecs::EcsWorld;

namespace
{
    // 照合する: 結び付けが一致していて、数が期待どおり
    void ExpectBound(World &world, uint32_t expectedEntities)
    {
        uint32_t count = 0;
        assert(world.VerifyEcsBinding(&count));
        assert(count == expectedEntities);
        assert(world.GetEcsWorld().GetEntityCount() == expectedEntities);
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
        ExpectBound(world, 4);

        // 子 a の部分木だけを除く（RemoveEntity）。親 root の連結は b だけになる
        const EcsEntity aRow = a->GetEcsEntity();
        assert(world.RemoveEntity(a));
        assert(a1Report.Calls == 1 && !a1Report.bRowAlive && !a1Report.bParentRowAlive);
        assert(!world.GetEcsWorld().IsAlive(aRow) && !world.GetEcsWorld().IsAlive(a1Row));
        ExpectBound(world, 2);
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
        ExpectBound(world, 4);

        world.Tick(0.01f);
        ExpectBound(world, 4);

        // Tick の後・LateTick の前に、段の外から除く
        world.RemoveObject(victim);
        assert(childReport.Calls == 1 && !childReport.bRowAlive);
        assert(!world.GetEcsWorld().IsAlive(victimRow) && !world.GetEcsWorld().IsAlive(childRow));
        ExpectBound(world, 2);

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
        ExpectBound(world, 4);

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
        ExpectBound(world, 2);

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
    TestPublicRemoveInnerAndReattach();
    TestStaleRowRejected();
    TestCycleReturnsToZero();
    std::cout << "EcsLifetimeTest passed\n";
    return 0;
}
