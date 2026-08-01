// PhysicsLifecycleEventTest — Phase 5 の寿命安全なイベント契約を検証する。

#include "Physics/IPhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Thread/Thread.h"

#include <cassert>
#include <iostream>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Modules::Physics;

namespace NorvesLib::Modules::Physics
{
    class PhysicsModuleTestAccess
    {
    public:
        static Math::Vector3 GetPendingImpulse(const IPhysicsModule& module, Core::Scene::BodyHandle handle)
        {
            const PhysicsModule& concrete = GetConcrete(module);
            assert(handle.IsValid() && handle.Index < concrete.m_BodySlots.size());
            return concrete.m_BodySlots[handle.Index].PendingImpulse;
        }

        static uint32_t GetPreviousPairCount(const IPhysicsModule& module)
        {
            return static_cast<uint32_t>(GetConcrete(module).m_PreviousTriggerPairs.size());
        }

        static uint32_t GetDispatchedEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_DispatchedEventCount;
        }

        static uint32_t GetPendingEventCount(const IPhysicsModule& module)
        {
            return GetConcrete(module).m_PendingEventCount;
        }

    private:
        static const PhysicsModule& GetConcrete(const IPhysicsModule& module)
        {
            const auto* concrete = dynamic_cast<const PhysicsModule*>(&module);
            assert(concrete != nullptr);
            return *concrete;
        }
    };
} // namespace NorvesLib::Modules::Physics

namespace
{
    void ConfigureFailureReporting()
    {
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    }

    void TestTriggerDoesNotResolveSolid(IPhysicsModule& physics);
    void TestDispatchRevalidation(IPhysicsModule& physics);
    void TestUninstallClearsTransientState(IPhysicsModule& physics);
    void TestMarkOtherForDestroyDuringDispatch(IPhysicsModule& physics);
    void TestCallbackSnapshotAfterRemoval(IPhysicsModule& physics);
    void TestReceiverCallbacksSnapshotBeforeDispatch(IPhysicsModule& physics);
    void TestReceiverAddedCallbackWaitsForNextEvent(IPhysicsModule& physics);
    void TestNestedFixedTickDoesNotRedispatchPendingEvents(IPhysicsModule& physics);
    void TestColliderReuseDuringDispatch(IPhysicsModule& physics);
    void TestWrongThreadTicksDoNotMutate(IPhysicsModule& physics);

    Engine::Engine& GetEngine()
    {
        static Engine::Engine* engine = new Engine::Engine();
        return *engine;
    }

    void TestTriggerBeginAndEnd()
    {
        ModuleRegistry& registry = GetModuleRegistry();
        IPhysicsModule* physics = RegisterPhysicsModule(registry);
        assert(physics != nullptr);
        assert(registry.InstallAll(GetEngine()));

        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        auto* first = world.CreateComponent<ColliderComponent>(firstEntity);
        auto* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        uint32_t beginCount = 0;
        uint32_t endCount = 0;
        uint32_t secondBeginCount = 0;
        bool bCallbackSawPublishedQuery = false;
        PhysicsContactEvent firstBeginEvent;
        PhysicsContactEvent secondBeginEvent;
        PhysicsContactEvent endEvent;
        PhysicsCallbackHandle beginHandle;
        PhysicsCallbackHandle endHandle;
        assert(first->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&beginCount, &bCallbackSawPublishedQuery, &firstBeginEvent](const PhysicsContactEvent& event)
            {
                ++beginCount;
                firstBeginEvent = event;
                Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit> hits;
                bCallbackSawPublishedQuery = GetEngine().GetSceneQuery().OverlapSphere(
                    Math::Sphere(Math::Vector3(), 0.1f), hits) == Core::Scene::EPhysicsSceneQueryResult::Success
                    && hits.size() == 2;
            }), beginHandle) == EPhysicsResult::Success);
        assert(first->AddOnOverlapEnd(Core::Delegate<void, const PhysicsContactEvent&>(
            [&endCount, &endEvent](const PhysicsContactEvent& event)
            {
                ++endCount;
                endEvent = event;
            }), endHandle) == EPhysicsResult::Success);
        PhysicsCallbackHandle secondBeginHandle;
        assert(second->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&secondBeginCount, &secondBeginEvent](const PhysicsContactEvent& event)
            {
                ++secondBeginCount;
                secondBeginEvent = event;
            }), secondBeginHandle) == EPhysicsResult::Success);
        physics->PreFixedTick(1.0f / 60.0f);
        physics->FixedTick(1.0f / 60.0f);
        assert(beginCount == 1);
        assert(secondBeginCount == 1);
        assert(bCallbackSawPublishedQuery);
        assert(firstBeginEvent.Self == first->GetColliderHandle());
        assert(firstBeginEvent.Other == second->GetColliderHandle());
        assert(secondBeginEvent.Self == second->GetColliderHandle());
        assert(secondBeginEvent.Other == first->GetColliderHandle());
        assert(firstBeginEvent.Contact.Normal == secondBeginEvent.Contact.Normal * -1.0f);

        physics->Shutdown();
        assert(physics->Initialize());
        physics->PreFixedTick(1.0f / 60.0f);
        physics->FixedTick(1.0f / 60.0f);
        assert(beginCount == 2);
        assert(secondBeginCount == 2);
        assert(endCount == 0);

        secondEntity->SetLocalPosition(10.0f, 0.0f, 0.0f);
        physics->PreFixedTick(1.0f / 60.0f);
        physics->FixedTick(1.0f / 60.0f);
        assert(endCount == 1);
        assert(endEvent.Contact.Depth == 0.0f);
        assert(endEvent.NormalImpulse == 0.0f);

        secondEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        physics->PreFixedTick(1.0f / 60.0f);
        physics->FixedTick(1.0f / 60.0f);
        assert(beginCount == 3);
        assert(secondBeginCount == 3);

        world.Finalize();
        TestTriggerDoesNotResolveSolid(*physics);
        TestDispatchRevalidation(*physics);
        TestUninstallClearsTransientState(*physics);
        TestMarkOtherForDestroyDuringDispatch(*physics);
        TestCallbackSnapshotAfterRemoval(*physics);
        TestReceiverCallbacksSnapshotBeforeDispatch(*physics);
        TestReceiverAddedCallbackWaitsForNextEvent(*physics);
        TestNestedFixedTickDoesNotRedispatchPendingEvents(*physics);
        TestColliderReuseDuringDispatch(*physics);
        TestWrongThreadTicksDoNotMutate(*physics);
        registry.ShutdownAll(GetEngine());
    }

    void TestTriggerDoesNotResolveSolid(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* triggerEntity = world.SpawnEntity<Entity>();
        Entity* dynamicEntity = world.SpawnEntity<Entity>();
        assert(triggerEntity != nullptr && dynamicEntity != nullptr);
        ColliderComponent* trigger = world.CreateComponent<ColliderComponent>(triggerEntity);
        ColliderComponent* solid = world.CreateComponent<ColliderComponent>(dynamicEntity);
        RigidBodyComponent* dynamicBody = world.CreateComponent<RigidBodyComponent>(dynamicEntity);
        assert(trigger != nullptr && solid != nullptr && dynamicBody != nullptr);
        assert(trigger->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(trigger->SetTrigger(true) == EPhysicsResult::Success);
        assert(solid->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(dynamicBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(dynamicBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(dynamicBody->SetLinearVelocity(Math::Vector3(1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        uint32_t hitCount = 0;
        PhysicsCallbackHandle hitHandle;
        assert(solid->AddOnHit(Core::Delegate<void, const PhysicsContactEvent&>(
            [&hitCount](const PhysicsContactEvent&) { ++hitCount; }), hitHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f);
        assert(dynamicBody->GetLinearVelocity() == Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(dynamicEntity->GetLocalTransform().position == Math::Vector3(1.0f, 0.0f, 0.0f));
        assert(hitCount == 0);
        world.Finalize();
    }

    void TestDispatchRevalidation(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        ColliderComponent* destroyer = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* victim = destroyer == first ? second : first;
        Entity* destroyerEntity = destroyer == first ? firstEntity : secondEntity;
        assert(destroyer->GetColliderHandle() < victim->GetColliderHandle());
        assert(destroyer->SetTrigger(true) == EPhysicsResult::Success);
        uint32_t firstCallbackCount = 0;
        uint32_t laterCallbackCount = 0;
        uint32_t otherCallbackCount = 0;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle laterHandle;
        PhysicsCallbackHandle otherHandle;
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&firstCallbackCount, destroyerEntity, destroyer](const PhysicsContactEvent&)
            {
                ++firstCallbackCount;
                destroyerEntity->RemoveComponent(destroyer);
            }), firstHandle) == EPhysicsResult::Success);
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&laterCallbackCount](const PhysicsContactEvent&) { ++laterCallbackCount; }), laterHandle) == EPhysicsResult::Success);
        assert(victim->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&otherCallbackCount](const PhysicsContactEvent&) { ++otherCallbackCount; }), otherHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCallbackCount == 1);
        assert(laterCallbackCount == 0);
        assert(otherCallbackCount == 0);
        world.Finalize();
    }

    void TestUninstallClearsTransientState(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        Entity* dynamicEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr && dynamicEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        ColliderComponent* dynamicCollider = world.CreateComponent<ColliderComponent>(dynamicEntity);
        RigidBodyComponent* dynamicBody = world.CreateComponent<RigidBodyComponent>(dynamicEntity);
        assert(first != nullptr && second != nullptr && dynamicCollider != nullptr && dynamicBody != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(dynamicCollider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(dynamicBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(dynamicBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        uint32_t beginCount = 0;
        PhysicsCallbackHandle beginHandle;
        assert(first->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&beginCount](const PhysicsContactEvent&) { ++beginCount; }), beginHandle) == EPhysicsResult::Success);
        assert(dynamicBody->AddImpulse(Math::Vector3(3.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.PreFixedTick(1.0f);
        physics.FixedTick(1.0f);
        assert(beginCount == 1);
        assert(dynamicBody->GetLinearVelocity() == Math::Vector3(3.0f, 0.0f, 0.0f));
        assert(dynamicBody->AddImpulse(Math::Vector3(3.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.PreFixedTick(1.0f);

        physics.Uninstall(GetEngine());
        assert(physics.Install(GetEngine()));
        assert(physics.Initialize());
        physics.FixedTick(1.0f);
        assert(beginCount == 2);
        assert(dynamicBody->GetLinearVelocity() == Math::Vector3(3.0f, 0.0f, 0.0f));
        world.Finalize();
    }

    void TestMarkOtherForDestroyDuringDispatch(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        firstEntity->SetLocalPosition(-0.5f, 0.0f, 0.0f);
        secondEntity->SetLocalPosition(0.5f, 0.0f, 0.0f);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        ColliderComponent* destroyer = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* victim = destroyer == first ? second : first;
        Entity* victimEntity = destroyer == first ? secondEntity : firstEntity;
        assert(destroyer->GetColliderHandle() < victim->GetColliderHandle());
        assert(destroyer->SetTrigger(true) == EPhysicsResult::Success);
        uint32_t firstCount = 0;
        uint32_t laterFirstCount = 0;
        uint32_t secondCount = 0;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle laterFirstHandle;
        PhysicsCallbackHandle secondHandle;
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&firstCount, victimEntity, destroyer](const PhysicsContactEvent& event)
            {
                ++firstCount;
                assert(event.Self == destroyer->GetColliderHandle());
                victimEntity->MarkForDestroy();
                assert(victimEntity->IsPendingDestroy());
            }), firstHandle) == EPhysicsResult::Success);
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&laterFirstCount](const PhysicsContactEvent&) { ++laterFirstCount; }), laterFirstHandle) == EPhysicsResult::Success);
        assert(victim->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&secondCount](const PhysicsContactEvent&) { ++secondCount; }), secondHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 1);
        assert(laterFirstCount == 0);
        assert(secondCount == 0);
        world.Finalize();
    }

    void TestCallbackSnapshotAfterRemoval(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        ColliderComponent* destroyer = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* victim = destroyer == first ? second : first;
        Entity* destroyerEntity = destroyer == first ? firstEntity : secondEntity;
        assert(destroyer->GetColliderHandle() < victim->GetColliderHandle());
        assert(destroyer->SetTrigger(true) == EPhysicsResult::Success);
        uint32_t firstCount = 0;
        uint32_t removedTokenCount = 0;
        EPhysicsResult removalResult = EPhysicsResult::NotRegistered;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle removedHandle;
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&firstCount, &removalResult, destroyer, &removedHandle](const PhysicsContactEvent&)
            {
                ++firstCount;
                removalResult = destroyer->RemoveOnOverlapBegin(removedHandle);
            }), firstHandle) == EPhysicsResult::Success);
        assert(destroyer->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&removedTokenCount](const PhysicsContactEvent&) { ++removedTokenCount; }), removedHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 1);
        assert(removalResult == EPhysicsResult::Success);
        assert(removedTokenCount == 1);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 1);
        assert(removedTokenCount == 1);
        secondEntity->SetLocalPosition(4.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        secondEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 2);
        assert(removedTokenCount == 1);
        world.Finalize();
    }

    void TestReceiverCallbacksSnapshotBeforeDispatch(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        ColliderComponent* dispatcher = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* target = dispatcher == first ? second : first;

        uint32_t firstCount = 0;
        uint32_t secondCount = 0;
        EPhysicsResult removalResult = EPhysicsResult::NotRegistered;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle secondHandle;
        assert(dispatcher->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&firstCount, &removalResult, target, &secondHandle](const PhysicsContactEvent&)
            {
                ++firstCount;
                removalResult = target->RemoveOnOverlapBegin(secondHandle);
            }), firstHandle) == EPhysicsResult::Success);
        assert(target->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&secondCount](const PhysicsContactEvent&) { ++secondCount; }), secondHandle) == EPhysicsResult::Success);

        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 1);
        assert(removalResult == EPhysicsResult::Success);
        assert(secondCount == 1);

        secondEntity->SetLocalPosition(4.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        firstEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        secondEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 2);
        assert(secondCount == 1);
        world.Finalize();
    }

    void TestReceiverAddedCallbackWaitsForNextEvent(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        ColliderComponent* dispatcher = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* target = dispatcher == first ? second : first;

        uint32_t firstCount = 0;
        uint32_t existingSecondCount = 0;
        uint32_t addedSecondCount = 0;
        bool bAdded = false;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle existingSecondHandle;
        PhysicsCallbackHandle addedSecondHandle;
        assert(dispatcher->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&firstCount, &bAdded, target, &addedSecondCount, &addedSecondHandle](const PhysicsContactEvent&)
            {
                ++firstCount;
                if (!bAdded)
                {
                    bAdded = true;
                    assert(target->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
                        [&addedSecondCount](const PhysicsContactEvent&) { ++addedSecondCount; }), addedSecondHandle)
                        == EPhysicsResult::Success);
                }
            }), firstHandle) == EPhysicsResult::Success);
        assert(target->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&existingSecondCount](const PhysicsContactEvent&) { ++existingSecondCount; }), existingSecondHandle)
            == EPhysicsResult::Success);

        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 1);
        assert(existingSecondCount == 1);
        assert(addedSecondCount == 0);

        secondEntity->SetLocalPosition(4.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        secondEntity->SetLocalPosition(0.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        assert(firstCount == 2);
        assert(existingSecondCount == 2);
        assert(addedSecondCount == 1);
        world.Finalize();
    }

    void TestNestedFixedTickDoesNotRedispatchPendingEvents(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        ColliderComponent* dispatcher = first->GetColliderHandle() < second->GetColliderHandle() ? first : second;
        ColliderComponent* target = dispatcher == first ? second : first;

        uint32_t firstCount = 0;
        uint32_t secondCount = 0;
        bool bNestedCalled = false;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle secondHandle;
        assert(dispatcher->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&physics, &firstCount, &bNestedCalled](const PhysicsContactEvent&)
            {
                ++firstCount;
                if (!bNestedCalled)
                {
                    bNestedCalled = true;
                    physics.FixedTick(1.0f / 60.0f);
                }
            }), firstHandle) == EPhysicsResult::Success);
        assert(target->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&secondCount](const PhysicsContactEvent&) { ++secondCount; }), secondHandle) == EPhysicsResult::Success);

        physics.FixedTick(1.0f / 60.0f);
        assert(bNestedCalled);
        assert(firstCount == 1);
        assert(secondCount == 1);
        world.Finalize();
    }

    void TestColliderReuseDuringDispatch(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        ColliderComponent* first = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* second = world.CreateComponent<ColliderComponent>(secondEntity);
        assert(first != nullptr && second != nullptr);
        assert(first->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(second->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        const Core::Scene::ColliderHandle oldHandle = first->GetColliderHandle();
        ColliderComponent* replacement = nullptr;
        uint32_t replacementBeginCount = 0;
        uint32_t replacementEndCount = 0;
        uint32_t survivorEndCount = 0;
        PhysicsCallbackHandle firstHandle;
        PhysicsCallbackHandle survivorEndHandle;
        assert(first->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&world, firstEntity, first, &replacement, &replacementBeginCount, &replacementEndCount](const PhysicsContactEvent&)
            {
                firstEntity->RemoveComponent(first);
                replacement = world.CreateComponent<ColliderComponent>(firstEntity);
                assert(replacement != nullptr);
                assert(replacement->SetSphere(1.0f) == EPhysicsResult::Success);
                assert(replacement->SetTrigger(true) == EPhysicsResult::Success);
                PhysicsCallbackHandle replacementBeginHandle;
                PhysicsCallbackHandle replacementEndHandle;
                assert(replacement->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
                    [&replacementBeginCount](const PhysicsContactEvent&) { ++replacementBeginCount; }), replacementBeginHandle)
                    == EPhysicsResult::Success);
                assert(replacement->AddOnOverlapEnd(Core::Delegate<void, const PhysicsContactEvent&>(
                    [&replacementEndCount](const PhysicsContactEvent&) { ++replacementEndCount; }), replacementEndHandle)
                    == EPhysicsResult::Success);
            }), firstHandle) == EPhysicsResult::Success);
        assert(second->AddOnOverlapEnd(Core::Delegate<void, const PhysicsContactEvent&>(
            [&survivorEndCount](const PhysicsContactEvent&) { ++survivorEndCount; }), survivorEndHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f / 60.0f);
        assert(replacement != nullptr);
        const Core::Scene::ColliderHandle replacementHandle = replacement->GetColliderHandle();
        assert(replacementHandle.Index == oldHandle.Index);
        assert(replacementHandle.Generation != oldHandle.Generation);
        assert(replacementBeginCount == 0);
        secondEntity->SetLocalPosition(4.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f / 60.0f);
        assert(replacementEndCount == 0);
        assert(survivorEndCount == 1);
        secondEntity->RemoveComponent(second);
        physics.FixedTick(1.0f / 60.0f);
        assert(replacementEndCount == 0);
        assert(survivorEndCount == 1);
        world.Finalize();
    }

    void TestWrongThreadTicksDoNotMutate(IPhysicsModule& physics)
    {
        physics.Shutdown();
        assert(physics.Initialize());
        World world;
        world.Initialize();
        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity != nullptr);
        ColliderComponent* collider = world.CreateComponent<ColliderComponent>(entity);
        RigidBodyComponent* body = world.CreateComponent<RigidBodyComponent>(entity);
        assert(collider != nullptr && body != nullptr);
        assert(collider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0.0f) == EPhysicsResult::Success);
        physics.FixedTick(1.0f);
        assert(body->AddImpulse(Math::Vector3(2.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.PreFixedTick(1.0f);
        const Math::Vector3 positionBefore = entity->GetLocalTransform().position;
        const Math::Vector3 velocityBefore = body->GetLinearVelocity();
        const Math::Vector3 pendingImpulseBefore = PhysicsModuleTestAccess::GetPendingImpulse(physics, body->GetBodyHandle());
        const uint32_t previousPairsBefore = PhysicsModuleTestAccess::GetPreviousPairCount(physics);
        const uint32_t dispatchedBefore = PhysicsModuleTestAccess::GetDispatchedEventCount(physics);
        const uint32_t pendingEventsBefore = PhysicsModuleTestAccess::GetPendingEventCount(physics);
        Core::Scene::PhysicsRaycastHit hitBefore;
        const Core::Scene::EPhysicsSceneQueryResult queryBefore = GetEngine().GetSceneQuery().Raycast(
            Math::Ray(Math::Vector3(-3.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 10.0f, hitBefore);
        Thread::Thread worker([&physics]()
        {
            physics.PreFixedTick(1.0f);
            physics.FixedTick(1.0f);
        });
        worker.Join();
        Core::Scene::PhysicsRaycastHit hitAfter;
        const Core::Scene::EPhysicsSceneQueryResult queryAfter = GetEngine().GetSceneQuery().Raycast(
            Math::Ray(Math::Vector3(-3.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 10.0f, hitAfter);
        assert(entity->GetLocalTransform().position == positionBefore);
        assert(body->GetLinearVelocity() == velocityBefore);
        assert(PhysicsModuleTestAccess::GetPendingImpulse(physics, body->GetBodyHandle()) == pendingImpulseBefore);
        assert(PhysicsModuleTestAccess::GetPreviousPairCount(physics) == previousPairsBefore);
        assert(PhysicsModuleTestAccess::GetDispatchedEventCount(physics) == dispatchedBefore);
        assert(PhysicsModuleTestAccess::GetPendingEventCount(physics) == pendingEventsBefore);
        assert(queryAfter == queryBefore);
        assert(hitAfter.Collider == hitBefore.Collider);
        world.Finalize();
    }
} // namespace

int main()
{
    ConfigureFailureReporting();

    std::cout << "PhysicsLifecycleEventTest start\n";
    TestTriggerBeginAndEnd();
    std::cout << "PhysicsLifecycleEventTest passed\n";
    return 0;
}
