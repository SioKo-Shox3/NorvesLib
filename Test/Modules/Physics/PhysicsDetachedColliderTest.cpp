// Entity を持たない静的コライダーの登録・解除と部分木の再利用を検証する。

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "Physics/IPhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "PhysicsModuleTestAccess.h"
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
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Modules::Physics;

namespace
{
    constexpr float FixedDelta = 1.0f / 60.0f;

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

    void Step(IPhysicsModule& physics)
    {
        physics.StepPreFixed(FixedDelta);
        physics.StepFixed(FixedDelta);
    }

    size_t CountEntityTree(const Entity& entity)
    {
        size_t count = 1;
        for (const Entity* child : entity.GetChildEntities())
        {
            count += CountEntityTree(*child);
        }
        return count;
    }

    size_t CountEntities(const World& world)
    {
        size_t count = 0;
        for (const Entity* root : world.GetRootEntities())
        {
            count += CountEntityTree(*root);
        }
        return count;
    }

    void TestBulkRegistration(IPhysicsModule& physics, Engine::Engine& engine)
    {
        World& world = engine.GetWorld();
        world.Initialize();
        Entity* root = world.SpawnEntity<Entity>();
        assert(root != nullptr && world.SpawnEntity<Entity>(root) != nullptr);
        Step(physics);
        const size_t entitiesBefore = CountEntities(world);
        const size_t proxiesBefore = PhysicsModuleTestAccess::GetWorkingProxies(physics).size();
        const uint64_t treeItemsBefore = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount;

        Container::VariableArray<StaticColliderDesc> colliders(5000);
        for (uint32_t index = 0; index < colliders.size(); ++index)
        {
            StaticColliderDesc& desc = colliders[index];
            desc.Shape = index % 3 == 0 ? EStaticColliderShape::Box
                : index % 3 == 1 ? EStaticColliderShape::Sphere : EStaticColliderShape::Capsule;
            desc.WorldTransform = Math::Transform(Math::Vector3(
                3.0f * (index % 100), 0.0f, 3.0f * (index / 100)));
        }
        const StaticColliderChunkKey key{100};
        assert(physics.RegisterStaticColliderChunk(key, colliders) == EPhysicsResult::Success);
        assert(CountEntities(world) == entitiesBefore);
        assert(PhysicsModuleTestAccess::GetDetachedColliderCount(physics) == 5000);
        Step(physics);
        const size_t proxyIncrease = PhysicsModuleTestAccess::GetWorkingProxies(physics).size() - proxiesBefore;
        const uint64_t treeIncrease = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount - treeItemsBefore;
        assert(CountEntities(world) == entitiesBefore);
        assert(proxyIncrease == 5000 && treeIncrease == 5000);
        std::cout << "一括登録: プロキシ増加=" << proxyIncrease << "、静的木の項目増加=" << treeIncrease << std::endl;

        assert(physics.UnregisterStaticColliderChunk(key) == EPhysicsResult::Success);
        Step(physics);
        world.Finalize();
    }

    void TestFloorAndRemoval(IPhysicsModule& physics, Engine::Engine& engine)
    {
        World world;
        world.Initialize();
        Entity* entity = world.SpawnEntity<Entity>();
        assert(entity != nullptr);
        entity->SetLocalPosition(0.0f, 2.0f, 0.0f);
        auto* collider = world.CreateComponent<ColliderComponent>(entity);
        auto* body = world.CreateComponent<RigidBodyComponent>(entity);
        assert(collider != nullptr && body != nullptr);
        assert(collider->SetSphere(0.5f) == EPhysicsResult::Success);
        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);

        StaticColliderDesc floor;
        floor.Shape = EStaticColliderShape::Box;
        floor.HalfExtents = Math::Vector3(5.0f, 0.5f, 5.0f);
        floor.WorldTransform = Math::Transform(Math::Vector3(0.0f, -0.5f, 0.0f));
        floor.UserData = 0x12345678u;
        floor.SurfaceId = 17;
        const StaticColliderChunkKey key{101};
        assert(physics.RegisterStaticColliderChunk(key, Container::Span<const StaticColliderDesc>(&floor, 1))
            == EPhysicsResult::Success);
        const auto handles = PhysicsModuleTestAccess::GetDetachedColliderHandles(physics, key);
        assert(handles.size() == 1);
        const ColliderHandle oldHandle = handles[0];
        for (uint32_t step = 0; step < 180; ++step)
        {
            Step(physics);
        }
        const float height = entity->GetLocalTransform().position.y;
        assert(std::isfinite(height) && height >= 0.45f && height <= 0.55f);
        assert(body->GetLinearVelocity().Length() <= 0.2f);

        auto& query = engine.GetSceneQuery();
        const Math::Ray ray(Math::Vector3(3.0f, 3.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
        PhysicsRaycastHit hit;
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == oldHandle && !hit.bHasEntity && !hit.Entity.IsValid());
        assert(hit.UserData == floor.UserData && hit.SurfaceId == floor.SurfaceId);
        bool alive = false;
        assert(query.IsAlive(oldHandle, alive) == EPhysicsSceneQueryResult::Success && alive);
        const size_t proxiesBefore = PhysicsModuleTestAccess::GetWorkingProxies(physics).size();
        const size_t publishedBefore = PhysicsModuleTestAccess::GetPublishedProxyCount(physics);
        const uint64_t treeItemsBefore = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount;

        assert(physics.UnregisterStaticColliderChunk(key) == EPhysicsResult::Success);
        Step(physics);
        assert(PhysicsModuleTestAccess::GetWorkingProxies(physics).size() + 1 == proxiesBefore);
        assert(PhysicsModuleTestAccess::GetPublishedProxyCount(physics) + 1 == publishedBefore);
        assert(PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount + 1 == treeItemsBefore);
        assert(query.IsAlive(oldHandle, alive) == EPhysicsSceneQueryResult::Success && !alive);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);

        // 同じスロットを再使用しても、古い世代のハンドルは無効のままにする。
        floor.WorldTransform = Math::Transform(Math::Vector3(100.0f, -0.5f, 0.0f));
        const StaticColliderChunkKey replacementKey{102};
        assert(physics.RegisterStaticColliderChunk(replacementKey, Container::Span<const StaticColliderDesc>(&floor, 1))
            == EPhysicsResult::Success);
        const auto replacement = PhysicsModuleTestAccess::GetDetachedColliderHandles(physics, replacementKey);
        assert(replacement.size() == 1);
        if (replacement[0].Index == oldHandle.Index)
        {
            assert(replacement[0].Generation != oldHandle.Generation);
        }
        assert(query.IsAlive(oldHandle, alive) == EPhysicsSceneQueryResult::Success && !alive);
        assert(query.IsAlive(replacement[0], alive) == EPhysicsSceneQueryResult::Success && alive);
        assert(physics.UnregisterStaticColliderChunk(replacementKey) == EPhysicsResult::Success);
        Step(physics);
        world.Finalize();
    }

    Container::VariableArray<StaticColliderDesc> MakeChunk(float offset)
    {
        Container::VariableArray<StaticColliderDesc> colliders(6);
        for (uint32_t index = 0; index < colliders.size(); ++index)
        {
            colliders[index].WorldTransform = Math::Transform(Math::Vector3(offset + 3.0f * index, 0.0f, 0.0f));
        }
        return colliders;
    }

    void TestSubtreeReuse(IPhysicsModule& physics, Engine::Engine& engine)
    {
        const StaticColliderChunkKey a{201}, b{202}, c{203};
        auto chunkA = MakeChunk(0.0f);
        auto chunkB = MakeChunk(100.0f);
        auto chunkC = MakeChunk(200.0f);
        assert(physics.RegisterStaticColliderChunk(a, chunkA) == EPhysicsResult::Success);
        assert(physics.RegisterStaticColliderChunk(b, chunkB) == EPhysicsResult::Success);
        Step(physics);
        Step(physics);
        const auto& settled = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics);
        assert(settled.SubtreeRebuildCount == 0 && settled.SubtreeReuseCount == 2);
        const auto bHandles = PhysicsModuleTestAccess::GetDetachedColliderHandles(physics, b);
        assert(bHandles.size() == chunkB.size());
        const Math::Ray ray(Math::Vector3(100.0f, 3.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
        PhysicsRaycastHit hit;
        assert(engine.GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == bHandles[0]);

        assert(physics.RegisterStaticColliderChunk(c, chunkC) == EPhysicsResult::Success);
        Step(physics);
        const auto added = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics);
        assert(added.SubtreeRebuildCount == 1 && added.SubtreeReuseCount == 2);
        assert(engine.GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == bHandles[0]);

        assert(physics.UnregisterStaticColliderChunk(a) == EPhysicsResult::Success);
        Step(physics);
        const auto removed = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics);
        assert(removed.SubtreeRebuildCount == 0 && removed.SubtreeReuseCount == 2);
        assert(engine.GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == bHandles[0]);
        std::cout << "部分木: C登録後の再構築=" << added.SubtreeRebuildCount << "・再利用=" << added.SubtreeReuseCount
            << "、A解除後の再構築=" << removed.SubtreeRebuildCount << "・再利用=" << removed.SubtreeReuseCount << std::endl;

        assert(physics.UnregisterStaticColliderChunk(b) == EPhysicsResult::Success);
        assert(physics.UnregisterStaticColliderChunk(c) == EPhysicsResult::Success);
        Step(physics);
    }

    void TestRejectedRegistration(IPhysicsModule& physics, Engine::Engine& engine)
    {
        StaticColliderDesc valid;
        const StaticColliderChunkKey key{301};
        assert(physics.RegisterStaticColliderChunk(key, Container::Span<const StaticColliderDesc>(&valid, 1))
            == EPhysicsResult::Success);
        assert(physics.RegisterStaticColliderChunk(key, Container::Span<const StaticColliderDesc>(&valid, 1))
            == EPhysicsResult::Duplicate);
        assert(physics.UnregisterStaticColliderChunk({399}) == EPhysicsResult::NotRegistered);
        Step(physics);
        const uint32_t detachedBefore = PhysicsModuleTestAccess::GetDetachedColliderCount(physics);
        const size_t proxiesBefore = PhysicsModuleTestAccess::GetWorkingProxies(physics).size();
        const uint64_t treeItemsBefore = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount;

        Container::VariableArray<StaticColliderDesc> invalid(2);
        invalid[0].WorldTransform = Math::Transform(Math::Vector3(20.0f, 0.0f, 0.0f));
        invalid[1].WorldTransform.position.x = std::numeric_limits<float>::quiet_NaN();
        assert(physics.RegisterStaticColliderChunk({302}, invalid) == EPhysicsResult::InvalidArgument);
        assert(physics.UnregisterStaticColliderChunk({302}) == EPhysicsResult::NotRegistered);
        StaticColliderDesc trigger;
        trigger.bTrigger = true;
        assert(physics.RegisterStaticColliderChunk({303}, Container::Span<const StaticColliderDesc>(&trigger, 1))
            == EPhysicsResult::InvalidArgument);
        assert(physics.UnregisterStaticColliderChunk({303}) == EPhysicsResult::NotRegistered);
        Step(physics);
        assert(PhysicsModuleTestAccess::GetDetachedColliderCount(physics) == detachedBefore);
        assert(PhysicsModuleTestAccess::GetWorkingProxies(physics).size() == proxiesBefore);
        assert(PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount == treeItemsBefore);
        PhysicsRaycastHit hit;
        assert(engine.GetSceneQuery().Raycast(Math::Ray(Math::Vector3(20.0f, 3.0f, 0.0f),
            Math::Vector3(0.0f, -1.0f, 0.0f)), 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);
        assert(physics.UnregisterStaticColliderChunk(key) == EPhysicsResult::Success);
        Step(physics);
    }

    void TestRegistrationDuringFixedStep(IPhysicsModule& physics, Engine::Engine& engine)
    {
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

        const StaticColliderChunkKey key{401};
        StaticColliderDesc desc;
        desc.WorldTransform = Math::Transform(Math::Vector3(10.0f, 0.0f, 0.0f));
        uint32_t callbackCount = 0;
        uint32_t countInCallback = UINT32_MAX;
        EPhysicsResult registration = EPhysicsResult::InvalidState;
        PhysicsCallbackHandle callback;
        assert(first->AddOnOverlapBegin(Core::Delegate<void, const PhysicsContactEvent&>(
            [&physics, &desc, &callbackCount, &countInCallback, &registration, key](const PhysicsContactEvent&)
            {
                ++callbackCount;
                registration = physics.RegisterStaticColliderChunk(key, Container::Span<const StaticColliderDesc>(&desc, 1));
                countInCallback = PhysicsModuleTestAccess::GetDetachedColliderCount(physics);
            }), callback) == EPhysicsResult::Success);

        Step(physics);
        assert(callbackCount == 1 && registration == EPhysicsResult::Success && countInCallback == 0);
        assert(PhysicsModuleTestAccess::GetDetachedColliderCount(physics) == 0);
        const size_t workingBefore = PhysicsModuleTestAccess::GetWorkingProxies(physics).size();
        const size_t publishedBefore = PhysicsModuleTestAccess::GetPublishedProxyCount(physics);
        const uint64_t treeItemsBefore = PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount;
        const Math::Ray ray(Math::Vector3(10.0f, 3.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
        PhysicsRaycastHit hit;
        assert(engine.GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);

        physics.StepPreFixed(FixedDelta);
        assert(PhysicsModuleTestAccess::GetDetachedColliderCount(physics) == 1);
        physics.StepFixed(FixedDelta);
        assert(callbackCount == 1);
        assert(PhysicsModuleTestAccess::GetWorkingProxies(physics).size() == workingBefore + 1);
        assert(PhysicsModuleTestAccess::GetPublishedProxyCount(physics) == publishedBefore + 1);
        assert(PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).StaticTreeItemCount == treeItemsBefore + 1);
        assert(engine.GetSceneQuery().Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(physics.UnregisterStaticColliderChunk(key) == EPhysicsResult::Success);
        world.Finalize();
    }
}

int main()
{
    ConfigureFailureReporting();
    auto engine = Container::MakeUnique<Engine::Engine>();
    auto& registry = Module::GetModuleRegistry();
    auto* physics = RegisterPhysicsModule(registry);
    assert(physics != nullptr && registry.InstallAll(*engine));
    TestBulkRegistration(*physics, *engine);
    TestFloorAndRemoval(*physics, *engine);
    TestSubtreeReuse(*physics, *engine);
    TestRejectedRegistration(*physics, *engine);
    TestRegistrationDuringFixedStep(*physics, *engine);
    registry.ShutdownAll(*engine);
    return 0;
}
