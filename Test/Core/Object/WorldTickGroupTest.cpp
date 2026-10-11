#include "Object/World.h"
#include "Object/ObjectHeap.h"
#include "Component/Component.h"
#include "Engine/NorvesEngine.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

namespace NorvesLib::Core
{
    struct WorldTickGroupTestAccess
    {
        static void Fixed(World& world) { world.DispatchFixedTick(1.0f / 60.0f); }
        static void Cleanup(World& world) { world.CleanupAfterFixedStep(); }
        static size_t PendingEntities(World& world) { return world.m_PendingDestroyEntities.size(); }
        static size_t PendingComponents(World& world) { return world.m_PendingDestroyComponents.size(); }
    };
}

using namespace NorvesLib::Core;
using NorvesLib::Core::Component::ETickGroup;

namespace
{
    struct Events
    {
        int Codes[128]{};
        int Count = 0;
        void Add(int value)
        {
            assert(Count < 128);
            Codes[Count++] = value;
        }
        void Clear() { Count = 0; }
        bool Has(int value) const
        {
            for (int i = 0; i < Count; ++i)
            {
                if (Codes[i] == value)
                {
                    return true;
                }
            }
            return false;
        }
    };

    // クラスの中では基底の注入名 Component が名前空間の別名を隠すので、外で別名を付ける
    using ProbeBase = Component::Component;

    class TickGroupProbe : public ProbeBase
    {
        REFLECTION_CLASS(TickGroupProbe, ProbeBase)

    public:
        Events* Output = nullptr;
        int Code = 0;
        int* DestroyCount = nullptr;
        TickGroupProbe* RemoveTarget = nullptr;
        Entity* RemoveEntityTarget = nullptr;
        TickGroupProbe* ChangeTarget = nullptr;
        bool bAddCamera = false;
        ObjectHeap* SelfHeap = nullptr;
        ObjectHandle SelfHandle;
        ObjectHeap* OtherHeapOnEnd = nullptr;
        ObjectHandle OtherHandleOnEnd;

        ~TickGroupProbe() override
        {
            if (DestroyCount)
            {
                ++*DestroyCount;
            }
        }
        void EndPlay() override
        {
            ProbeBase::EndPlay();
            if (OtherHeapOnEnd)
            {
                OtherHeapOnEnd->DestroyNow(OtherHandleOnEnd);
            }
        }
        // 型に登録する本体（World の順序付きの系が呼ぶ）
        static void RunTick(ProbeBase& component, float deltaTime)
        {
            static_cast<TickGroupProbe&>(component).OnTick(deltaTime);
        }
        void OnTick(float)
        {
            if (SelfHeap)
            {
                ObjectHeap* heap = SelfHeap;
                const ObjectHandle handle = SelfHandle;
                heap->DestroyNow(handle);
                return;
            }
            if (Output)
            {
                Output->Add(Code);
            }
            if (ChangeTarget)
            {
                ChangeTarget->SetTickGroup(ETickGroup::Camera);
                ChangeTarget = nullptr;
            }
            if (RemoveTarget)
            {
                GetOwner()->RemoveComponent(RemoveTarget);
                RemoveTarget = nullptr;
            }
            if (RemoveEntityTarget)
            {
                GetOwner()->GetWorld()->RemoveEntity(RemoveEntityTarget);
                RemoveEntityTarget = nullptr;
            }
            if (bAddCamera)
            {
                bAddCamera = false;
                auto* added = GetOwner()->GetWorld()->CreateComponent<TickGroupProbe>(GetOwner());
                assert(added);
                added->Output = Output;
                added->Code = 99;
                added->SetTickGroup(ETickGroup::Camera);
            }
        }
    };

    IMPLEMENT_CLASS(TickGroupProbe, ProbeBase)

    TickGroupProbe* Add(World& world, Entity* owner, Events& events, int code, ETickGroup group = ETickGroup::Default)
    {
        auto* component = world.CreateComponent<TickGroupProbe>(owner);
        assert(component);
        component->Output = &events;
        component->Code = code;
        component->SetTickGroup(group);
        return component;
    }

    class TickGroupBoundaryProbe : public ProbeBase
    {
        REFLECTION_CLASS(TickGroupBoundaryProbe, ProbeBase)

    public:
        Entity* ReadTarget = nullptr;
        Entity* MoveTarget = nullptr;
        float Position = 0;
        float Observed = -999;
        int Calls = 0;
        static void RunTick(ProbeBase& component, float)
        {
            auto& self = static_cast<TickGroupBoundaryProbe&>(component);
            ++self.Calls;
            if (self.ReadTarget)
            {
                self.Observed = self.ReadTarget->GetWorldTransform().position.x;
            }
            if (self.MoveTarget)
            {
                self.MoveTarget->SetPosition(self.Position, 0, 0);
            }
        }
    };

    IMPLEMENT_CLASS(TickGroupBoundaryProbe, ProbeBase)

    // 試験の型の本体を、主群で呼ぶ登録として World へ載せる
    void RegisterProbeTicks(World& world)
    {
        Component::ComponentTickRegistration probe;
        probe.Name = "Test.TickGroupProbe";
        probe.Run = &TickGroupProbe::RunTick;
        world.RegisterComponentTick(TickGroupProbe::StaticClass(), probe);
        Component::ComponentTickRegistration boundary;
        boundary.Name = "Test.TickGroupBoundaryProbe";
        boundary.Run = &TickGroupBoundaryProbe::RunTick;
        world.RegisterComponentTick(TickGroupBoundaryProbe::StaticClass(), boundary);
    }

    void InitializeWorld(World& world)
    {
        world.Initialize();
        RegisterProbeTicks(world);
    }

    void TestTransformPublicationAtGroupBoundaries()
    {
        World world;
        InitializeWorld(world);
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto* grandchild = world.SpawnEntity<Entity>(child);
        child->SetLocalPosition(1, 0, 0);
        grandchild->SetLocalPosition(2, 0, 0);
        world.UpdateWorldTransforms();
        assert(grandchild->GetWorldTransform().position.x == 3);
        // フレーム前の変更も最初のInput群から読む。親ありdirtyは確定までは旧値。
        root->SetPosition(7, 0, 0);
        assert(grandchild->GetWorldTransform().position.x == 3);
        TickGroupBoundaryProbe* movers[8]{};
        TickGroupBoundaryProbe* observers[8]{};
        for (int index = 7; index >= 0; --index)
        {
            const auto group = static_cast<ETickGroup>(index);
            movers[index] = world.CreateComponent<TickGroupBoundaryProbe>(root);
            observers[index] = world.CreateComponent<TickGroupBoundaryProbe>(grandchild);
            movers[index]->SetTickGroup(group);
            observers[index]->SetTickGroup(group);
            observers[index]->SetTickPriority(1);
            movers[index]->ReadTarget = grandchild;
            movers[index]->MoveTarget = root;
            movers[index]->Position = static_cast<float>((index+1)*10);
            observers[index]->ReadTarget = grandchild;
        }
        world.Tick(0.01f);
        for (int index = 0; index <= 4; ++index)
        {
            const float expected = index == 0 ? 10.0f : static_cast<float>(index*10+3);
            assert(movers[index]->Calls == 1 && observers[index]->Calls == 1);
            assert(movers[index]->Observed == expected && observers[index]->Observed == expected);
        }
        for (int index = 5; index < 8; ++index)
        {
            assert(movers[index]->Calls == 0 && observers[index]->Calls == 0);
        }
        world.LateTick(0.01f);
        for (int index = 5; index < 8; ++index)
        {
            const float expected = static_cast<float>(index*10+3);
            assert(movers[index]->Calls == 1 && observers[index]->Calls == 1);
            assert(movers[index]->Observed == expected && observers[index]->Observed == expected);
        }
        // 群内のsetter直後は自動同期しない。最終群の変更は次の明示境界で公開する。
        assert(grandchild->GetWorldTransform().position.x == 73);
        world.UpdateWorldTransforms();
        assert(grandchild->GetWorldTransform().position.x == 83);
        world.Finalize();
    }

    void TestSparseTransformBoundaries()
    {
        World world;
        InitializeWorld(world);
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        child->SetLocalPosition(2, 0, 0);
        auto* mover = world.CreateComponent<TickGroupBoundaryProbe>(root);
        mover->SetTickGroup(ETickGroup::Movement);
        mover->MoveTarget = root;
        mover->Position = 10;
        auto* reader = world.CreateComponent<TickGroupBoundaryProbe>(child);
        reader->SetTickGroup(ETickGroup::PoseFinalize);
        reader->ReadTarget = child;
        world.Tick(0.01f);
        assert(reader->Observed == 12);
        // Late対象が一つもなくても従来の明示Late境界確定は維持する。
        root->SetPosition(20, 0, 0);
        assert(child->GetWorldTransform().position.x == 12);
        world.LateTick(0.01f);
        assert(child->GetWorldTransform().position.x == 22);
        world.Finalize();
    }

    void TestOrderAndChanges()
    {
        World world;
        InitializeWorld(world);
        Events events;
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto* first = Add(world, root, events, 11);
        auto* second = Add(world, child, events, 21);
        world.Tick(0.01f);
        assert(events.Count == 2 && events.Codes[0] == 11 && events.Codes[1] == 21);
        world.LateTick(0.01f);
        events.Clear();
        // 優先度の変更は次の収集の順に効く（深さ優先の順より優先度が先）
        second->SetTickPriority(-5);
        world.Tick(0.01f);
        assert(events.Count == 2 && events.Codes[0] == 21 && events.Codes[1] == 11);
        world.LateTick(0.01f);
        events.Clear();
        // 収集の途中の群の変更は、このフレームの順に効かず、次の収集から効く
        auto* changer = Add(world, root, events, 1, ETickGroup::Input);
        changer->ChangeTarget = first;
        world.Tick(0.01f);
        assert(events.Has(11));
        events.Clear();
        world.LateTick(0.01f);
        assert(!events.Has(11));
        world.Tick(0.01f);
        assert(!events.Has(11));
        world.LateTick(0.01f);
        assert(events.Has(11));
        world.Finalize();
    }

    void TestAdditionAndRemoval()
    {
        World world;
        InitializeWorld(world);
        Events events;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        auto* remover = Add(world, root, events, 1);
        auto* camera = Add(world, root, events, 2, ETickGroup::Camera);
        camera->DestroyCount = &destroyed;
        remover->RemoveTarget = camera;
        remover->bAddCamera = true;
        world.Tick(0.01f);
        assert(destroyed == 1 && !events.Has(2) && !events.Has(99));
        world.LateTick(0.01f);
        assert(!events.Has(2) && !events.Has(99));
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(events.Has(99));
        world.Finalize();
    }

    void TestEntityRemovalCleanupAndInactiveParent()
    {
        World world;
        InitializeWorld(world);
        Events events;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto* late = Add(world, child, events, 2, ETickGroup::Camera);
        late->DestroyCount = &destroyed;
        auto* remover = Add(world, root, events, 1);
        remover->RemoveEntityTarget = child;
        // Tick の最中の除去は破棄予約になり、段の終わりの回収で解放される。後の群でも呼ばれない
        world.Tick(0.01f);
        assert(destroyed == 1);
        world.LateTick(0.01f);
        assert(!events.Has(2));
        auto* newChild = world.SpawnEntity<Entity>(root);
        Add(world, newChild, events, 3);
        events.Clear();
        root->SetActive(false);
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(events.Count == 0);
        world.Finalize();
    }
    void TestHeapDetachmentAndSelfRemoval()
    {
        World world;
        InitializeWorld(world);
        ObjectHeap heap;
        Events events;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        const auto lateHandle = heap.Create<TickGroupProbe>();
        auto* late = heap.Resolve<TickGroupProbe>(lateHandle);
        assert(root->AddComponent(late));
        late->SetTickGroup(ETickGroup::Camera);
        late->Output = &events;
        late->Code = 7;
        late->DestroyCount = &destroyed;
        world.Tick(0.01f);
        assert(heap.DestroyNow(lateHandle));
        world.LateTick(0.01f);
        assert(destroyed == 1 && !events.Has(7));

        const auto selfHandle = heap.Create<TickGroupProbe>();
        auto* self = heap.Resolve<TickGroupProbe>(selfHandle);
        assert(root->AddComponent(self));
        self->SelfHeap = &heap;
        self->SelfHandle = selfHandle;
        self->DestroyCount = &destroyed;
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(!heap.Resolve(selfHandle) && destroyed == 2);

        // LateTick の群の中で自身を破棄しても、回収は段の終わりまで待つ
        const auto lateSelfHandle = heap.Create<TickGroupProbe>();
        auto* lateSelf = heap.Resolve<TickGroupProbe>(lateSelfHandle);
        assert(root->AddComponent(lateSelf));
        lateSelf->SetTickGroup(ETickGroup::Camera);
        lateSelf->SelfHeap = &heap;
        lateSelf->SelfHandle = lateSelfHandle;
        lateSelf->DestroyCount = &destroyed;
        world.Tick(0.01f);
        assert(heap.Resolve(lateSelfHandle) && destroyed == 2);
        world.LateTick(0.01f);
        assert(!heap.Resolve(lateSelfHandle) && destroyed == 3);

        // cleanupの途中で別のheap Componentが消えても、待ち参照を再利用しない。
        auto* trigger = Add(world, root, events, 9);
        const auto otherHandle = heap.Create<TickGroupProbe>();
        auto* other = heap.Resolve<TickGroupProbe>(otherHandle);
        assert(root->AddComponent(other));
        other->DestroyCount = &destroyed;
        trigger->OtherHeapOnEnd = &heap;
        trigger->OtherHandleOnEnd = otherHandle;
        trigger->MarkForDestroy();
        other->MarkForDestroy();
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(!heap.Resolve(otherHandle) && destroyed == 4);
        world.Finalize();
    }

    // Tick の番号は収集（Tick から LateTick まで）ごとに 1 つ進み、収集の中の固定ステップは同じ番号を使う。
    // 収集の外の単独の固定ステップは、それぞれ 1 つ進める
    void TestTickSerialAcrossStandaloneFixed()
    {
        World world;
        InitializeWorld(world);
        const uint64_t initial = world.GetTickSerial();
        WorldTickGroupTestAccess::Fixed(world);
        assert(world.GetTickSerial() == initial + 1);
        WorldTickGroupTestAccess::Fixed(world);
        assert(world.GetTickSerial() == initial + 2);
        world.Tick(0.01f);
        assert(world.GetTickSerial() == initial + 3);
        WorldTickGroupTestAccess::Fixed(world);
        WorldTickGroupTestAccess::Fixed(world);
        assert(world.GetTickSerial() == initial + 3);
        world.LateTick(0.01f);
        assert(world.GetTickSerial() == initial + 3);
        WorldTickGroupTestAccess::Fixed(world);
        assert(world.GetTickSerial() == initial + 4);
        world.Finalize();
    }

    // 除去の通知の中で自身を破棄予約しても、削除後に一覧へ削除済みのポインタが残らない
    class ReserveOnRemovedEntity : public Entity
    {
    public:
        void OnRemovedFromWorld() override { MarkForDestroy(); }
    };

    void TestReserveInRemovalNotificationLeavesNoDanglingEntry()
    {
        World world;
        InitializeWorld(world);
        auto* entity = world.SpawnEntity<ReserveOnRemovedEntity>();
        assert(entity);
        assert(world.RemoveEntity(entity));
        assert(WorldTickGroupTestAccess::PendingEntities(world) == 0);
        assert(WorldTickGroupTestAccess::PendingComponents(world) == 0);
        world.Tick(0.01f);
        world.LateTick(0.01f);
        world.Finalize();
    }

    // ObjectHeap 経由の破棄予約も一覧へ積まれ、World の回収で木から外れる
    void TestHeapEnqueueDestroyIsListed()
    {
        World world;
        InitializeWorld(world);
        ObjectHeap heap;
        Events events;
        auto* root = world.SpawnEntity<Entity>();
        const auto handle = heap.Create<TickGroupProbe>();
        auto* component = heap.Resolve<TickGroupProbe>(handle);
        assert(component && root->AddComponent(component));
        component->Output = &events;
        assert(root->GetComponents().size() == 1);

        assert(heap.EnqueueDestroy(handle));
        assert(WorldTickGroupTestAccess::PendingComponents(world) == 1);
        world.Tick(0.01f);
        assert(WorldTickGroupTestAccess::PendingComponents(world) == 0);
        assert(root->GetComponents().size() == 0);
        heap.ProcessDestroyQueue();
        assert(!heap.Resolve(handle));
        world.LateTick(0.01f);
        world.Finalize();
    }

    // 公開の RemoveInner で切り離したものも一覧から外れ、削除後に回収が壊れない
    void TestWorldRemoveInnerForgetsReservation()
    {
        World world;
        InitializeWorld(world);
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        assert(child);
        child->MarkForDestroy();
        root->MarkForDestroy();
        assert(WorldTickGroupTestAccess::PendingEntities(world) == 2);

        assert(world.RemoveInner(root));
        assert(root->GetOuter() == nullptr);
        assert(WorldTickGroupTestAccess::PendingEntities(world) == 0);
        // 切り離した部分木は World の木にいないので、ハンドルは無効になる
        assert(!root->GetEntityHandle().IsValid() && !child->GetEntityHandle().IsValid());
        root->Finalize();
        delete root;

        world.Tick(0.01f);
        world.LateTick(0.01f);
        world.Finalize();
    }
}

int main()
{
    TestTransformPublicationAtGroupBoundaries();
    TestSparseTransformBoundaries();
    TestOrderAndChanges();
    TestAdditionAndRemoval();
    TestEntityRemovalCleanupAndInactiveParent();
    TestHeapDetachmentAndSelfRemoval();
    TestTickSerialAcrossStandaloneFixed();
    TestReserveInRemovalNotificationLeavesNoDanglingEntry();
    TestHeapEnqueueDestroyIsListed();
    TestWorldRemoveInnerForgetsReservation();
    std::cout << "WorldTickGroupTest passed\n";
    return 0;
}
