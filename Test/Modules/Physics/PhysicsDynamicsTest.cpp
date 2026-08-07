// PhysicsDynamicsTest — Phase 5 の離散剛体契約を検証する。

#include "Physics/IPhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Physics/PhysicsBroadphase.h"
#include "Physics/RigidBodyComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Modules::Physics;

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

    void TestKinematicChildUsesFreshHierarchyTransform(IPhysicsModule& physics);
    void TestGravityDeterminism(IPhysicsModule& physics);
    void TestKinematicSnapshotOwnership(IPhysicsModule& physics);
    void TestInactiveBodyDiscardsPendingImpulse(IPhysicsModule& physics);
    void TestRigidBodySelfInactiveStopsSimulation(IPhysicsModule& physics);
    void TestHeterogeneousContactOrder();
    void TestFloorAndStackStability(IPhysicsModule& physics);
    void TestDynamicMassRatios(IPhysicsModule& physics);
    void RunHeadOnCollision(IPhysicsModule& physics, float firstMass, float secondMass, float& outFirstVelocity, float& outSecondVelocity);
    void TestSolidHitContract(IPhysicsModule& physics);

    bool NearlyEqual(float first, float second, float tolerance = 0.0001f)
    {
        return std::fabs(first - second) <= tolerance;
    }

    Engine::Engine& GetEngine()
    {
        static Engine::Engine* engine = new Engine::Engine();
        return *engine;
    }

    void TestDeferredImpulseAndFixedDeltaValidation()
    {
        ModuleRegistry& registry = GetModuleRegistry();
        IPhysicsModule* physics = RegisterPhysicsModule(registry);
        assert(physics != nullptr);
        assert(registry.InstallAll(GetEngine()));

        World world;
        world.Initialize();
        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity != nullptr);
        auto* collider = world.CreateComponent<ColliderComponent>(entity);
        auto* body = world.CreateComponent<RigidBodyComponent>(entity);
        assert(collider != nullptr && body != nullptr);
        assert(collider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(body->SetMass(2.0f) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics->FixedTick(1.0f);
        assert(entity->GetLocalTransform().position == Math::Vector3());
        assert(body->AddImpulse(Math::Vector3(2.0f, 0.0f, 0.0f)) == EPhysicsResult::InvalidState);
        assert(body->GetLinearVelocity() == Math::Vector3(1.0f, 0.0f, 0.0f));

        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(body->AddImpulse(Math::Vector3(2.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        assert(body->GetLinearVelocity() == Math::Vector3(1.0f, 0.0f, 0.0f));
        physics->PreFixedTick(1.0f);
        physics->FixedTick(1.0f);
        assert(body->GetLinearVelocity() == Math::Vector3(2.0f, 0.0f, 0.0f));
        physics->PreFixedTick(1.0f);
        physics->FixedTick(1.0f);
        assert(body->GetLinearVelocity() == Math::Vector3(2.0f, 0.0f, 0.0f));

        assert(body->AddImpulse(Math::Vector3(2.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(3.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics->PreFixedTick(1.0f);
        physics->FixedTick(1.0f);
        assert(body->GetLinearVelocity() == Math::Vector3(4.0f, 0.0f, 0.0f));

        const Math::Vector3 positionBeforeInvalidDelta = entity->GetLocalTransform().position;
        physics->PreFixedTick(0.0f);
        physics->FixedTick(0.0f);
        physics->PreFixedTick(std::numeric_limits<float>::quiet_NaN());
        physics->FixedTick(std::numeric_limits<float>::infinity());
        assert(entity->GetLocalTransform().position == positionBeforeInvalidDelta);

        world.Finalize();
        TestGravityDeterminism(*physics);
        TestFloorAndStackStability(*physics);
        TestDynamicMassRatios(*physics);
        TestSolidHitContract(*physics);
        TestKinematicChildUsesFreshHierarchyTransform(*physics);
        TestKinematicSnapshotOwnership(*physics);
        TestInactiveBodyDiscardsPendingImpulse(*physics);
        TestRigidBodySelfInactiveStopsSimulation(*physics);
        TestHeterogeneousContactOrder();
        registry.ShutdownAll(GetEngine());
    }

    void TestGravityDeterminism(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        secondEntity->SetLocalPosition(10.0f, 0.0f, 0.0f);
        ColliderComponent* firstCollider = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* secondCollider = world.CreateComponent<ColliderComponent>(secondEntity);
        RigidBodyComponent* firstBody = world.CreateComponent<RigidBodyComponent>(firstEntity);
        RigidBodyComponent* secondBody = world.CreateComponent<RigidBodyComponent>(secondEntity);
        assert(firstCollider != nullptr && secondCollider != nullptr && firstBody != nullptr && secondBody != nullptr);
        assert(firstCollider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(secondCollider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(firstBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(secondBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        const float deltaTime = 1.0f / 60.0f;
        float expectedVelocity = 0.0f;
        float expectedPosition = 0.0f;
        for (uint32_t step = 0; step < 3; ++step)
        {
            expectedVelocity += -9.81f * deltaTime;
            expectedPosition += expectedVelocity * deltaTime;
            physics.FixedTick(deltaTime);
        }
        assert(NearlyEqual(firstBody->GetLinearVelocity().y, expectedVelocity));
        assert(NearlyEqual(firstEntity->GetLocalTransform().position.y, expectedPosition));
        assert(firstBody->GetLinearVelocity() == secondBody->GetLinearVelocity());
        assert(NearlyEqual(firstEntity->GetLocalTransform().position.y, secondEntity->GetLocalTransform().position.y));
        world.Finalize();
    }

    void TestKinematicChildUsesFreshHierarchyTransform(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* parent = world.SpawnEntity<Entity>();
        Entity* child = world.SpawnEntity<Entity>(parent);
        assert(parent != nullptr && child != nullptr);
        ColliderComponent* collider = world.CreateComponent<ColliderComponent>(child);
        RigidBodyComponent* body = world.CreateComponent<RigidBodyComponent>(child);
        assert(collider != nullptr && body != nullptr);
        assert(collider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(body->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success);
        physics.FixedTick(1.0f);

        physics.PreFixedTick(1.0f);
        parent->SetLocalScale(2.0f, 2.0f, 2.0f);
        child->SetLocalPosition(1.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f);
        assert(body->GetLinearVelocity() == Math::Vector3(2.0f, 0.0f, 0.0f));

        Core::Container::VariableArray<Core::Scene::PhysicsOverlapHit> hits;
        assert(GetEngine().GetSceneQuery().OverlapSphere(Math::Sphere(Math::Vector3(2.0f, 0.0f, 0.0f), 0.1f), hits)
            == Core::Scene::EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 1);
        assert(hits[0].Collider == collider->GetColliderHandle());

        world.Finalize();
    }

    void TestKinematicSnapshotOwnership(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* kinematicEntity = world.SpawnEntity<Entity>();
        Entity* dynamicEntity = world.SpawnEntity<Entity>();
        assert(kinematicEntity != nullptr && dynamicEntity != nullptr);
        dynamicEntity->SetLocalPosition(4.0f, 0.0f, 0.0f);
        ColliderComponent* kinematicCollider = world.CreateComponent<ColliderComponent>(kinematicEntity);
        ColliderComponent* dynamicCollider = world.CreateComponent<ColliderComponent>(dynamicEntity);
        RigidBodyComponent* kinematicBody = world.CreateComponent<RigidBodyComponent>(kinematicEntity);
        RigidBodyComponent* dynamicBody = world.CreateComponent<RigidBodyComponent>(dynamicEntity);
        assert(kinematicCollider != nullptr && dynamicCollider != nullptr && kinematicBody != nullptr && dynamicBody != nullptr);
        assert(kinematicCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(dynamicCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(kinematicBody->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success);
        assert(dynamicBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(dynamicBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(kinematicBody->SetLinearVelocity(Math::Vector3(100.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.FixedTick(1.0f);
        assert(kinematicBody->GetLinearVelocity() == Math::Vector3());
        assert(dynamicBody->GetLinearVelocity() == Math::Vector3());

        physics.PreFixedTick(1.0f);
        kinematicEntity->SetLocalPosition(1.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f);
        assert(kinematicBody->GetLinearVelocity() == Math::Vector3(1.0f, 0.0f, 0.0f));
        physics.PreFixedTick(0.0f);
        kinematicEntity->SetLocalPosition(2.0f, 0.0f, 0.0f);
        physics.FixedTick(1.0f);
        assert(kinematicBody->GetLinearVelocity() == Math::Vector3());
        world.Finalize();
    }

    void TestInactiveBodyDiscardsPendingImpulse(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity != nullptr);
        ColliderComponent* collider = world.CreateComponent<ColliderComponent>(entity);
        RigidBodyComponent* body = world.CreateComponent<RigidBodyComponent>(entity);
        assert(collider != nullptr && body != nullptr);
        assert(collider->SetSphere(0.5f) == EPhysicsResult::Success);
        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(body->AddImpulse(Math::Vector3(4.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        entity->SetActive(false);
        physics.FixedTick(1.0f);
        entity->SetActive(true);
        physics.FixedTick(1.0f);
        assert(body->GetLinearVelocity() == Math::Vector3());
        assert(entity->GetLocalTransform().position == Math::Vector3());
        world.Finalize();
    }

    void TestRigidBodySelfInactiveStopsSimulation(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();

        Entity* disabledEntity = world.SpawnEntity<Entity>();
        Entity* destroyedEntity = world.SpawnEntity<Entity>();
        assert(disabledEntity != nullptr && destroyedEntity != nullptr);
        destroyedEntity->SetLocalPosition(10.0f, 0.0f, 0.0f);

        ColliderComponent* disabledCollider = world.CreateComponent<ColliderComponent>(disabledEntity);
        RigidBodyComponent* disabledBody = world.CreateComponent<RigidBodyComponent>(disabledEntity);
        ColliderComponent* destroyedCollider = world.CreateComponent<ColliderComponent>(destroyedEntity);
        RigidBodyComponent* destroyedBody = world.CreateComponent<RigidBodyComponent>(destroyedEntity);
        assert(disabledCollider != nullptr && disabledBody != nullptr);
        assert(destroyedCollider != nullptr && destroyedBody != nullptr);
        assert(disabledCollider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(destroyedCollider->SetSphere(0.25f) == EPhysicsResult::Success);
        assert(disabledBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(destroyedBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(disabledBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(destroyedBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(disabledBody->SetLinearVelocity(Math::Vector3(1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        assert(destroyedBody->SetLinearVelocity(Math::Vector3(1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.FixedTick(1.0f);

        assert(disabledBody->AddImpulse(Math::Vector3(4.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        assert(destroyedBody->AddImpulse(Math::Vector3(4.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        disabledBody->Disable();
        destroyedBody->Destroy();
        const Math::Vector3 disabledPosition = disabledEntity->GetLocalTransform().position;
        const Math::Vector3 destroyedPosition = destroyedEntity->GetLocalTransform().position;

        physics.PreFixedTick(1.0f);
        physics.FixedTick(1.0f);
        const bool bDisabledBodyStopped = disabledEntity->GetLocalTransform().position == disabledPosition;
        const bool bDestroyedBodyStopped = destroyedEntity->GetLocalTransform().position == destroyedPosition;

        disabledBody->Enable();
        physics.PreFixedTick(1.0f);
        physics.FixedTick(1.0f);
        const bool bDisabledImpulseDiscarded =
            disabledEntity->GetLocalTransform().position == disabledPosition + Math::Vector3(1.0f, 0.0f, 0.0f);

        std::cout << "[Probe] RigidBody inactive disabled=" << bDisabledBodyStopped
                  << " destroyed=" << bDestroyedBodyStopped
                  << " impulseDiscarded=" << bDisabledImpulseDiscarded << '\n';
        assert(bDisabledBodyStopped);
        assert(bDestroyedBodyStopped);
        assert(bDisabledImpulseDiscarded);
        world.Finalize();
    }

    void TestHeterogeneousContactOrder()
    {
        PhysicsShapeProxy proxies[3];
        proxies[0].Shape = EPhysicsProxyShape::Sphere;
        proxies[0].Sphere = Math::Sphere(Math::Vector3(), 1.0f);
        proxies[1].Shape = EPhysicsProxyShape::Box;
        proxies[1].Box = Math::OBB(
            Math::Vector3(0.5f, 0.0f, 0.0f),
            Math::Vector3(1.0f, 1.0f, 1.0f),
            Math::Vector3::UnitX,
            Math::Vector3::UnitY,
            Math::Vector3::UnitZ);
        proxies[2].Shape = EPhysicsProxyShape::Capsule;
        proxies[2].Capsule = Math::Capsule(
            Math::Vector3(0.5f, -0.5f, 0.0f),
            Math::Vector3(0.5f, 0.5f, 0.0f),
            1.0f);

        for (uint32_t firstIndex = 0; firstIndex < 3; ++firstIndex)
        {
            for (uint32_t secondIndex = firstIndex + 1; secondIndex < 3; ++secondIndex)
            {
                Math::GeometryContact forwardContact;
                Math::GeometryContact reverseContact;
                assert(PhysicsBroadphase::ComputeContact(proxies[firstIndex], proxies[secondIndex], forwardContact));
                assert(PhysicsBroadphase::ComputeContact(proxies[secondIndex], proxies[firstIndex], reverseContact));
                assert(forwardContact.Depth > 0.0f);
                assert(forwardContact.Normal == reverseContact.Normal * -1.0f);
            }
        }
    }

    void TestFloorAndStackStability(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* floorEntity = world.SpawnEntity<Entity>();
        Entity* lowerEntity = world.SpawnEntity<Entity>();
        Entity* upperEntity = world.SpawnEntity<Entity>();
        assert(floorEntity != nullptr && lowerEntity != nullptr && upperEntity != nullptr);
        floorEntity->SetLocalPosition(0.0f, -1.0f, 0.0f);
        lowerEntity->SetLocalPosition(0.0f, 2.0f, 0.0f);
        upperEntity->SetLocalPosition(0.0f, 4.0f, 0.0f);
        ColliderComponent* floorCollider = world.CreateComponent<ColliderComponent>(floorEntity);
        ColliderComponent* lowerCollider = world.CreateComponent<ColliderComponent>(lowerEntity);
        ColliderComponent* upperCollider = world.CreateComponent<ColliderComponent>(upperEntity);
        RigidBodyComponent* lowerBody = world.CreateComponent<RigidBodyComponent>(lowerEntity);
        RigidBodyComponent* upperBody = world.CreateComponent<RigidBodyComponent>(upperEntity);
        assert(floorCollider != nullptr && lowerCollider != nullptr && upperCollider != nullptr);
        assert(lowerBody != nullptr && upperBody != nullptr);
        assert(floorCollider->SetBox(Math::Vector3(5.0f, 0.5f, 5.0f)) == EPhysicsResult::Success);
        assert(lowerCollider->SetSphere(0.5f) == EPhysicsResult::Success);
        assert(upperCollider->SetSphere(0.5f) == EPhysicsResult::Success);
        assert(lowerBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(upperBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        for (uint32_t step = 0; step < 180; ++step)
        {
            physics.FixedTick(1.0f / 60.0f);
        }
        assert(std::isfinite(lowerEntity->GetLocalTransform().position.y));
        assert(std::isfinite(upperEntity->GetLocalTransform().position.y));
        assert(lowerEntity->GetLocalTransform().position.y >= -0.01f);
        assert(upperEntity->GetLocalTransform().position.y >= lowerEntity->GetLocalTransform().position.y + 0.9f);
        assert(NearlyEqual(lowerBody->GetLinearVelocity().y, 0.0f, 0.2f));
        world.Finalize();
    }

    void TestDynamicMassRatios(IPhysicsModule& physics)
    {
        float equalFirstVelocity = 0.0f;
        float equalSecondVelocity = 0.0f;
        RunHeadOnCollision(physics, 1.0f, 1.0f, equalFirstVelocity, equalSecondVelocity);
        assert(NearlyEqual(equalFirstVelocity, 0.0f));
        assert(NearlyEqual(equalSecondVelocity, 0.0f));

        float unequalFirstVelocity = 0.0f;
        float unequalSecondVelocity = 0.0f;
        RunHeadOnCollision(physics, 1.0f, 3.0f, unequalFirstVelocity, unequalSecondVelocity);
        assert(NearlyEqual(unequalFirstVelocity, -0.5f));
        assert(NearlyEqual(unequalSecondVelocity, -0.5f));
    }

    void RunHeadOnCollision(
        IPhysicsModule& physics,
        float firstMass,
        float secondMass,
        float& outFirstVelocity,
        float& outSecondVelocity)
    {
        World world;
        world.Initialize();
        Entity* firstEntity = world.SpawnEntity<Entity>();
        Entity* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        firstEntity->SetLocalPosition(-0.5f, 0.0f, 0.0f);
        secondEntity->SetLocalPosition(0.5f, 0.0f, 0.0f);
        ColliderComponent* firstCollider = world.CreateComponent<ColliderComponent>(firstEntity);
        ColliderComponent* secondCollider = world.CreateComponent<ColliderComponent>(secondEntity);
        RigidBodyComponent* firstBody = world.CreateComponent<RigidBodyComponent>(firstEntity);
        RigidBodyComponent* secondBody = world.CreateComponent<RigidBodyComponent>(secondEntity);
        assert(firstCollider != nullptr && secondCollider != nullptr && firstBody != nullptr && secondBody != nullptr);
        assert(firstCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(secondCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(firstBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(secondBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(firstBody->SetMass(firstMass) == EPhysicsResult::Success);
        assert(secondBody->SetMass(secondMass) == EPhysicsResult::Success);
        assert(firstBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(secondBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        assert(firstBody->SetLinearVelocity(Math::Vector3(1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        assert(secondBody->SetLinearVelocity(Math::Vector3(-1.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.FixedTick(0.01f);
        outFirstVelocity = firstBody->GetLinearVelocity().x;
        outSecondVelocity = secondBody->GetLinearVelocity().x;
        world.Finalize();
    }

    void TestSolidHitContract(IPhysicsModule& physics)
    {
        World world;
        world.Initialize();
        Entity* staticEntity = world.SpawnEntity<Entity>();
        Entity* dynamicEntity = world.SpawnEntity<Entity>();
        assert(staticEntity != nullptr && dynamicEntity != nullptr);
        dynamicEntity->SetLocalPosition(1.5f, 0.0f, 0.0f);
        ColliderComponent* staticCollider = world.CreateComponent<ColliderComponent>(staticEntity);
        ColliderComponent* dynamicCollider = world.CreateComponent<ColliderComponent>(dynamicEntity);
        RigidBodyComponent* staticBody = world.CreateComponent<RigidBodyComponent>(staticEntity);
        RigidBodyComponent* dynamicBody = world.CreateComponent<RigidBodyComponent>(dynamicEntity);
        assert(staticCollider != nullptr && dynamicCollider != nullptr && staticBody != nullptr && dynamicBody != nullptr);
        assert(staticCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(dynamicCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        assert(dynamicBody->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(dynamicBody->SetGravityScale(0.0f) == EPhysicsResult::Success);
        uint32_t staticHitCount = 0;
        uint32_t dynamicHitCount = 0;
        PhysicsContactEvent staticEvent;
        PhysicsContactEvent dynamicEvent;
        PhysicsCallbackHandle staticHandle;
        PhysicsCallbackHandle dynamicHandle;
        assert(staticCollider->AddOnHit(Core::Delegate<void, const PhysicsContactEvent&>(
            [&staticHitCount, &staticEvent](const PhysicsContactEvent& event)
            {
                ++staticHitCount;
                staticEvent = event;
            }), staticHandle) == EPhysicsResult::Success);
        assert(dynamicCollider->AddOnHit(Core::Delegate<void, const PhysicsContactEvent&>(
            [&dynamicHitCount, &dynamicEvent](const PhysicsContactEvent& event)
            {
                ++dynamicHitCount;
                dynamicEvent = event;
            }), dynamicHandle) == EPhysicsResult::Success);
        physics.FixedTick(1.0f / 60.0f);
        assert(staticHitCount == 0);
        assert(dynamicHitCount == 0);

        dynamicEntity->SetLocalPosition(3.0f, 0.0f, 0.0f);
        assert(dynamicBody->SetLinearVelocity(Math::Vector3(-150.0f, 0.0f, 0.0f)) == EPhysicsResult::Success);
        physics.FixedTick(0.01f);
        assert(staticHitCount == 1);
        assert(dynamicHitCount == 1);
        assert(staticEvent.Self == staticCollider->GetColliderHandle());
        assert(staticEvent.Other == dynamicCollider->GetColliderHandle());
        assert(dynamicEvent.Self == dynamicCollider->GetColliderHandle());
        assert(dynamicEvent.Other == staticCollider->GetColliderHandle());
        assert(staticEvent.Contact.Normal == dynamicEvent.Contact.Normal * -1.0f);
        assert(staticEvent.NormalImpulse > 0.0f);
        assert(dynamicEvent.NormalImpulse == staticEvent.NormalImpulse);
        world.Finalize();
    }
} // namespace

int main()
{
    ConfigureFailureReporting();
    std::cout << "PhysicsDynamicsTest start\n";
    TestDeferredImpulseAndFixedDeltaValidation();
    std::cout << "PhysicsDynamicsTest passed\n";
    return 0;
}
