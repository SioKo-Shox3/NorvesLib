#include "Object/World.h"
#include "Object/ObjectHeap.h"
#include "Component/Component.h"
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

    class TickEntity : public Entity
    {
    public:
        Events* Output = nullptr;
        int Code = 0;
        void Tick(float) override
        {
            if (Output)
            {
                Output->Add(Code);
            }
        }
    };

    class TickComponent : public Component::Component
    {
    public:
        Events* Output = nullptr;
        int Code = 0;
        int* DestroyCount = nullptr;
        TickComponent* RemoveTarget = nullptr;
        Entity* RemoveEntityTarget = nullptr;
        TickComponent* ChangeTarget = nullptr;
        bool bAddCamera = false;
        bool bRemoveDuringFixed = false;
        bool bRemoveComponentDuringFixed = false;
        bool bAddDuringFixed = false;
        int* FixedCount = nullptr;
        int* AddedFixedCount = nullptr;
        ObjectHeap* SelfHeap = nullptr;
        ObjectHandle SelfHandle;
        bool bDestroySelfDuringFixed = false;
        ObjectHeap* OtherHeapOnEnd = nullptr;
        ObjectHandle OtherHandleOnEnd;

        ~TickComponent() override
        {
            if (DestroyCount)
            {
                ++*DestroyCount;
            }
        }
        void EndPlay() override
        {
            NorvesLib::Core::Component::Component::EndPlay();
            if (OtherHeapOnEnd)
            {
                OtherHeapOnEnd->DestroyNow(OtherHandleOnEnd);
            }
        }
        void Tick(float) override
        {
            if (SelfHeap && !bDestroySelfDuringFixed)
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
            if (RemoveTarget && !bRemoveComponentDuringFixed)
            {
                GetOwner()->RemoveComponent(RemoveTarget);
                RemoveTarget = nullptr;
            }
            if (bAddCamera)
            {
                bAddCamera = false;
                auto* added = GetOwner()->GetWorld()->CreateComponent<TickComponent>(GetOwner());
                assert(added);
                added->Output = Output;
                added->Code = 99;
                added->SetTickGroup(ETickGroup::Camera);
            }
        }
        void FixedTick(float) override
        {
            if (FixedCount)
            {
                ++*FixedCount;
            }
            if (SelfHeap && bDestroySelfDuringFixed)
            {
                ObjectHeap* heap = SelfHeap;
                const ObjectHandle handle = SelfHandle;
                heap->DestroyNow(handle);
                return;
            }
            if (bRemoveComponentDuringFixed && RemoveTarget)
            {
                GetOwner()->RemoveComponent(RemoveTarget);
                RemoveTarget = nullptr;
            }
            if (bAddDuringFixed)
            {
                bAddDuringFixed = false;
                auto* added = GetOwner()->GetWorld()->CreateComponent<TickComponent>(GetOwner());
                assert(added);
                added->FixedCount = AddedFixedCount;
            }
            if (bRemoveDuringFixed && RemoveEntityTarget)
            {
                GetOwner()->GetWorld()->RemoveEntity(RemoveEntityTarget);
                RemoveEntityTarget = nullptr;
            }
        }
    };

    TickComponent* Add(World& world, Entity* owner, Events& events, int code, ETickGroup group = ETickGroup::Default)
    {
        auto* component = world.CreateComponent<TickComponent>(owner);
        assert(component);
        component->Output = &events;
        component->Code = code;
        component->SetTickGroup(group);
        return component;
    }

    class TransformBoundaryComponent : public Component::Component
    {
    public:
        Entity* ReadTarget = nullptr;
        Entity* MoveTarget = nullptr;
        float Position = 0;
        float Observed = -999;
        int Calls = 0;
        void Tick(float) override
        {
            ++Calls;
            if (ReadTarget)
            {
                Observed = ReadTarget->GetWorldTransform().position.x;
            }
            if (MoveTarget)
            {
                MoveTarget->SetPosition(Position, 0, 0);
            }
        }
    };

    void TestTransformPublicationAtGroupBoundaries()
    {
        World world;
        world.Initialize();
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto* grandchild = world.SpawnEntity<Entity>(child);
        NorvesLib::Math::Transform local = NorvesLib::Math::Transform::Identity;
        local.position.x = 1;
        child->SetLocalTransform(local);
        local.position.x = 2;
        grandchild->SetLocalTransform(local);
        world.UpdateWorldTransforms();
        assert(grandchild->GetWorldTransform().position.x == 3);
        // フレーム前の変更も最初のInput群から読む。親ありdirtyは確定までは旧値。
        root->SetPosition(7, 0, 0);
        assert(grandchild->GetWorldTransform().position.x == 3);
        TransformBoundaryComponent* movers[8]{};
        TransformBoundaryComponent* observers[8]{};
        for (int index = 7; index >= 0; --index)
        {
            const auto group = static_cast<ETickGroup>(index);
            movers[index] = world.CreateComponent<TransformBoundaryComponent>(root);
            observers[index] = world.CreateComponent<TransformBoundaryComponent>(grandchild);
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
        world.Initialize();
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto local = NorvesLib::Math::Transform::Identity;
        local.position.x = 2;
        child->SetLocalTransform(local);
        auto* mover = world.CreateComponent<TransformBoundaryComponent>(root);
        mover->SetTickGroup(ETickGroup::Movement);
        mover->MoveTarget = root;
        mover->Position = 10;
        auto* reader = world.CreateComponent<TransformBoundaryComponent>(child);
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
        world.Initialize();
        Events events;
        auto* root = world.SpawnEntity<TickEntity>();
        auto* child = world.SpawnEntity<TickEntity>(root);
        root->Output = &events;
        root->Code = 10;
        child->Output = &events;
        child->Code = 20;
        auto* first = Add(world, root, events, 11);
        Add(world, child, events, 21);
        world.Tick(0.01f);
        assert(events.Count == 4 && events.Codes[0] == 10 && events.Codes[1] == 11 && events.Codes[2] == 20 && events.Codes[3] == 21);
        world.LateTick(0.01f);
        events.Clear();
        first->SetTickPriority(-5);
        world.Tick(0.01f);
        assert(events.Codes[0] == 10 && events.Codes[1] == 11);
        world.LateTick(0.01f);
        events.Clear();
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
        world.Initialize();
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

    void TestFixedCleanupAndInactiveParent()
    {
        World world;
        world.Initialize();
        Events events;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        auto* child = world.SpawnEntity<Entity>(root);
        auto* late = Add(world, child, events, 2, ETickGroup::Camera);
        late->DestroyCount = &destroyed;
        auto* remover = Add(world, root, events, 1);
        remover->bRemoveDuringFixed = true;
        remover->RemoveEntityTarget = child;
        world.Tick(0.01f);
        WorldTickGroupTestAccess::Fixed(world);
        assert(child->IsPendingDestroy() && destroyed == 0);
        WorldTickGroupTestAccess::Cleanup(world);
        assert(destroyed == 1);
        WorldTickGroupTestAccess::Fixed(world);
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
        world.Initialize();
        ObjectHeap heap;
        Events events;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        const auto lateHandle = heap.Create<TickComponent>();
        auto* late = heap.Resolve<TickComponent>(lateHandle);
        assert(root->AddComponent(late));
        late->SetTickGroup(ETickGroup::Camera);
        late->Output = &events;
        late->Code = 7;
        late->DestroyCount = &destroyed;
        world.Tick(0.01f);
        assert(heap.DestroyNow(lateHandle));
        world.LateTick(0.01f);
        assert(destroyed == 1 && !events.Has(7));

        const auto selfHandle = heap.Create<TickComponent>();
        auto* self = heap.Resolve<TickComponent>(selfHandle);
        assert(root->AddComponent(self));
        self->SelfHeap = &heap;
        self->SelfHandle = selfHandle;
        self->DestroyCount = &destroyed;
        world.Tick(0.01f);
        world.LateTick(0.01f);
        assert(!heap.Resolve(selfHandle) && destroyed == 2);

        const auto fixedHandle = heap.Create<TickComponent>();
        auto* fixed = heap.Resolve<TickComponent>(fixedHandle);
        assert(root->AddComponent(fixed));
        fixed->SetTickGroup(ETickGroup::Camera);
        fixed->SelfHeap = &heap;
        fixed->SelfHandle = fixedHandle;
        fixed->bDestroySelfDuringFixed = true;
        fixed->DestroyCount = &destroyed;
        world.Tick(0.01f);
        WorldTickGroupTestAccess::Fixed(world);
        WorldTickGroupTestAccess::Cleanup(world);
        world.LateTick(0.01f);
        assert(!heap.Resolve(fixedHandle) && destroyed == 3);

        // cleanupの途中で別のheap Componentが消えても、待ち参照を再利用しない。
        auto* trigger = Add(world, root, events, 9);
        const auto otherHandle = heap.Create<TickComponent>();
        auto* other = heap.Resolve<TickComponent>(otherHandle);
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

    void TestStandaloneFixedAndFixedMutation()
    {
        World world;
        world.Initialize();
        Events events;
        int fixedCount = 0;
        int addedCount = 0;
        int destroyed = 0;
        auto* root = world.SpawnEntity<Entity>();
        auto* first = Add(world, root, events, 1);
        first->FixedCount = &fixedCount;
        WorldTickGroupTestAccess::Fixed(world);
        WorldTickGroupTestAccess::Cleanup(world);
        assert(fixedCount == 1);
        auto* victim = Add(world, root, events, 2);
        victim->DestroyCount = &destroyed;
        first->RemoveTarget = victim;
        first->bRemoveComponentDuringFixed = true;
        first->bAddDuringFixed = true;
        first->AddedFixedCount = &addedCount;
        world.Tick(0.01f);
        WorldTickGroupTestAccess::Fixed(world);
        assert(victim->IsPendingDestroy() && destroyed == 0 && addedCount == 0);
        WorldTickGroupTestAccess::Cleanup(world);
        assert(destroyed == 1);
        WorldTickGroupTestAccess::Fixed(world);
        assert(addedCount == 0);
        world.LateTick(0.01f);
        world.Tick(0.01f);
        WorldTickGroupTestAccess::Fixed(world);
        assert(addedCount == 1);
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
    TestFixedCleanupAndInactiveParent();
    TestHeapDetachmentAndSelfRemoval();
    TestStandaloneFixedAndFixedMutation();
    std::cout << "WorldTickGroupTest passed\n";
    return 0;
}
