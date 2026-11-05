// Entity の階層内の有効状態と、子の Tick の可否を検証する。
#include "Component/Component.h"
#include "Ecs/EcsTransformSystem.h"
#include "Object/Entity.h"
#include "Object/World.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;

namespace
{
    using ProbeBase = Component::Component;

    // 期待値は Entity の親をたどって独立に求める。
    bool WalkActive(const Entity& entity)
    {
        for (const Entity* current = &entity; current; current = current->GetParentEntity())
        {
            if (!current->IsActive() || current->IsPendingDestroy())
            {
                return false;
            }
        }
        return true;
    }

    void ExpectActive(const Entity& entity, bool bExpected)
    {
        assert(WalkActive(entity) == bExpected);
        assert(entity.IsActiveInHierarchy() == bExpected);
    }

    void ExpectCachedValue(World& world, const Entity& entity, bool bExpected)
    {
        const Ecs::EcsPropagatedTransform* cached =
            world.GetEcsWorld().GetColumnValue<Ecs::EcsPropagatedTransform>(entity.GetEcsEntity());
        assert(cached && cached->bValid != 0);
        assert((cached->bEffectiveActive != 0) == bExpected);
    }

    void ExpectFresh(World& world, const Entity& entity, bool bExpected)
    {
        assert(!world.GetEcsTransformSystem().HasUnpropagatedActiveChanges());
        ExpectCachedValue(world, entity, bExpected);
        ExpectActive(entity, bExpected);
    }

    void Tick(World& world)
    {
        world.Tick(0.01f);
        world.LateTick(0.01f);
    }

    class ActiveHierarchyCountProbe : public ProbeBase
    {
        REFLECTION_CLASS(ActiveHierarchyCountProbe, ProbeBase)

    public:
        int Calls = 0;

        static void RunTick(ProbeBase& component, float)
        {
            ++static_cast<ActiveHierarchyCountProbe&>(component).Calls;
        }
    };
    IMPLEMENT_CLASS(ActiveHierarchyCountProbe, ProbeBase)

    class ActiveHierarchySpawnProbe : public ProbeBase
    {
        REFLECTION_CLASS(ActiveHierarchySpawnProbe, ProbeBase)

    public:
        Entity* ActiveParent = nullptr;
        Entity* InactiveParent = nullptr;
        Entity* Relinked = nullptr;
        Entity* ActiveChild = nullptr;
        Entity* InactiveChild = nullptr;
        bool bDone = false;

        static void RunTick(ProbeBase& component, float)
        {
            auto& self = static_cast<ActiveHierarchySpawnProbe&>(component);
            if (self.bDone)
            {
                return;
            }
            World& world = *self.GetOwner()->GetWorld();
            ExpectFresh(world, *self.Relinked, true);

            self.InactiveChild = world.SpawnEntity<Entity>(self.InactiveParent);
            self.ActiveChild = world.SpawnEntity<Entity>(self.ActiveParent);
            assert(self.InactiveChild && self.ActiveChild);
            // 同期点より前で、両方とも ECS の行の作成が予約中である。
            assert(self.InactiveChild->GetEcsEntity().IsNull());
            assert(self.ActiveChild->GetEcsEntity().IsNull());
            ExpectActive(*self.InactiveChild, false);
            ExpectActive(*self.ActiveChild, true);

            // 作成予約中の親へ既存の行を付け替え、古いキャッシュを読まないことも確かめる。
            assert(world.ReparentEntity(self.Relinked, self.InactiveChild));
            assert(self.Relinked->GetParentEntity() == self.InactiveChild);
            assert(!self.Relinked->GetEcsEntity().IsNull());
            assert(!world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
            assert(world.GetEcsTransformSystem().HasUnpropagatedActiveChanges());
            ExpectCachedValue(world, *self.Relinked, true);
            ExpectActive(*self.Relinked, false);
            self.bDone = true;
        }
    };
    IMPLEMENT_CLASS(ActiveHierarchySpawnProbe, ProbeBase)

    class ActiveHierarchyDestroyProbe : public ProbeBase
    {
        REFLECTION_CLASS(ActiveHierarchyDestroyProbe, ProbeBase)

    public:
        Entity* Parent = nullptr;
        Entity* Child = nullptr;
        bool bObserved = false;

        static void RunTick(ProbeBase& component, float)
        {
            auto& self = static_cast<ActiveHierarchyDestroyProbe&>(component);
            World& world = *self.GetOwner()->GetWorld();
            // Tick の先頭で伝播した後、回収する前に破棄予約中の部分木を調べる。
            assert(self.Parent->IsPendingDestroy());
            ExpectFresh(world, *self.Parent, false);
            ExpectFresh(world, *self.Child, false);
            self.bObserved = true;
        }
    };
    IMPLEMENT_CLASS(ActiveHierarchyDestroyProbe, ProbeBase)

    template <typename T>
    void RegisterTick(World& world, const char* name)
    {
        Component::ComponentTickRegistration registration;
        registration.Name = name;
        registration.Run = &T::RunTick;
        world.RegisterComponentTick(T::StaticClass(), registration);
    }

    void TestActivationAndTick()
    {
        World world;
        world.Initialize();
        RegisterTick<ActiveHierarchyCountProbe>(world, "Test.ActiveHierarchyCount");
        Entity* a = world.SpawnEntity<Entity>();
        Entity* b = world.SpawnEntity<Entity>(a);
        Entity* c = world.SpawnEntity<Entity>(b);
        assert(a && b && c);
        auto* counter = world.CreateComponent<ActiveHierarchyCountProbe>(c);
        assert(counter);

        ExpectActive(*a, true);
        ExpectActive(*b, true);
        ExpectActive(*c, true);
        Tick(world);
        ExpectFresh(world, *a, true);
        ExpectFresh(world, *b, true);
        ExpectFresh(world, *c, true);
        assert(counter->Calls == 1);

        a->SetActive(false);
        assert(world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
        ExpectCachedValue(world, *c, true);
        ExpectActive(*a, false);
        ExpectActive(*b, false);
        ExpectActive(*c, false);
        Tick(world);
        ExpectFresh(world, *a, false);
        ExpectFresh(world, *b, false);
        ExpectFresh(world, *c, false);
        assert(counter->Calls == 1);

        a->SetActive(true);
        assert(world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
        ExpectCachedValue(world, *c, false);
        ExpectActive(*a, true);
        ExpectActive(*b, true);
        ExpectActive(*c, true);
        Tick(world);
        ExpectFresh(world, *a, true);
        ExpectFresh(world, *b, true);
        ExpectFresh(world, *c, true);
        assert(counter->Calls == 2);

        b->SetActive(false);
        assert(world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
        ExpectCachedValue(world, *c, true);
        ExpectActive(*a, true);
        ExpectActive(*b, false);
        ExpectActive(*c, false);
        Tick(world);
        ExpectFresh(world, *a, true);
        ExpectFresh(world, *b, false);
        ExpectFresh(world, *c, false);
        assert(counter->Calls == 2);

        b->SetActive(true);
        assert(world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
        ExpectCachedValue(world, *c, false);
        ExpectActive(*a, true);
        ExpectActive(*b, true);
        ExpectActive(*c, true);
        Tick(world);
        ExpectFresh(world, *a, true);
        ExpectFresh(world, *b, true);
        ExpectFresh(world, *c, true);
        assert(counter->Calls == 3);
        world.Finalize();
    }

    void TestPendingCreation()
    {
        World world;
        world.Initialize();
        RegisterTick<ActiveHierarchySpawnProbe>(world, "Test.ActiveHierarchySpawn");
        Entity* activeParent = world.SpawnEntity<Entity>();
        Entity* inactiveParent = world.SpawnEntity<Entity>();
        Entity* relinked = world.SpawnEntity<Entity>(activeParent);
        assert(activeParent && inactiveParent && relinked);
        inactiveParent->SetActive(false);
        Tick(world);
        ExpectFresh(world, *activeParent, true);
        ExpectFresh(world, *inactiveParent, false);
        ExpectFresh(world, *relinked, true);

        auto* probe = world.CreateComponent<ActiveHierarchySpawnProbe>(activeParent);
        assert(probe);
        probe->ActiveParent = activeParent;
        probe->InactiveParent = inactiveParent;
        probe->Relinked = relinked;
        Tick(world);
        assert(probe->bDone);
        assert(!probe->InactiveChild->GetEcsEntity().IsNull());
        assert(!probe->ActiveChild->GetEcsEntity().IsNull());
        Tick(world);
        ExpectFresh(world, *probe->InactiveChild, false);
        ExpectFresh(world, *probe->ActiveChild, true);
        ExpectFresh(world, *relinked, false);
        world.Finalize();
    }

    void TestPendingDestroy()
    {
        World world;
        world.Initialize();
        RegisterTick<ActiveHierarchyDestroyProbe>(world, "Test.ActiveHierarchyDestroy");
        Entity* a = world.SpawnEntity<Entity>();
        Entity* b = world.SpawnEntity<Entity>(a);
        Entity* c = world.SpawnEntity<Entity>(b);
        Entity* observer = world.SpawnEntity<Entity>();
        assert(a && b && c && observer);
        Tick(world);
        ExpectFresh(world, *b, true);
        ExpectFresh(world, *c, true);

        auto* probe = world.CreateComponent<ActiveHierarchyDestroyProbe>(observer);
        assert(probe);
        probe->Parent = b;
        probe->Child = c;
        b->MarkForDestroy();
        assert(b->IsPendingDestroy());
        assert(world.GetEcsTransformSystem().HasUnpropagatedFlagChanges());
        ExpectCachedValue(world, *c, true);
        ExpectActive(*a, true);
        ExpectActive(*b, false);
        ExpectActive(*c, false);
        world.Tick(0.01f);
        assert(probe->bObserved);
        // Tick の回収後は b と c を参照しない。
        world.LateTick(0.01f);
        world.Finalize();
    }

    void TestReparent()
    {
        World world;
        world.Initialize();
        Entity* a = world.SpawnEntity<Entity>();
        Entity* b = world.SpawnEntity<Entity>(a);
        Entity* c = world.SpawnEntity<Entity>(b);
        Entity* d = world.SpawnEntity<Entity>();
        assert(a && b && c && d);
        b->SetActive(false);
        Tick(world);
        ExpectFresh(world, *c, false);
        ExpectFresh(world, *d, true);

        assert(world.ReparentEntity(c, d));
        assert(c->GetParentEntity() == d);
        assert(world.GetEcsTransformSystem().HasUnpropagatedActiveChanges());
        const bool bActiveAfterMove = c->IsActiveInHierarchy();
        assert(WalkActive(*c));
        Tick(world);
        ExpectFresh(world, *c, true);

        assert(world.ReparentEntity(c, b));
        assert(c->GetParentEntity() == b);
        assert(world.GetEcsTransformSystem().HasUnpropagatedActiveChanges());
        const bool bActiveAfterReturn = c->IsActiveInHierarchy();
        assert(!WalkActive(*c));
        Tick(world);
        ExpectFresh(world, *c, false);
        world.Finalize();

        // 双方向の結果を保存してから検査し、片方が失敗しても両方の伝播後を確認する。
        if (!bActiveAfterMove)
        {
            std::cerr << "有効な親への付け替え直後に false を返した\n";
        }
        if (bActiveAfterReturn)
        {
            std::cerr << "休眠中の親への付け替え直後に true を返した\n";
        }
        assert(bActiveAfterMove);
        assert(!bActiveAfterReturn);
    }
}

int main()
{
    std::cout << "EntityActiveInHierarchyTest 開始\n";
    TestActivationAndTick();
    std::cout << "有効状態と Tick を確認\n";
    TestPendingCreation();
    std::cout << "作成予約を確認\n";
    TestPendingDestroy();
    std::cout << "破棄予約を確認\n";
    TestReparent();
    std::cout << "EntityActiveInHierarchyTest 合格\n";
    return 0;
}
