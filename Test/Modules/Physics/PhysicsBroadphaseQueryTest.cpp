// PhysicsBroadphaseQueryTest — Physics private SAP と値型 snapshot query の契約を検証する。

#include "Physics/PhysicsBroadphase.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "PhysicsModuleTestAccess.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Modules::Physics;

namespace
{
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Collider)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Body)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsRaycastHit::Entity)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Collider)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Body)>);
    static_assert(!std::is_pointer_v<decltype(PhysicsOverlapHit::Entity)>);

    Engine::Engine& GetEngine()
    {
        static Engine::Engine* engine = new Engine::Engine();
        return *engine;
    }

    PhysicsShapeProxy MakeSphereProxy(uint32_t index, float centerX, float radius)
    {
        PhysicsShapeProxy proxy;
        proxy.Collider = ColliderHandle{index, 1};
        proxy.Shape = EPhysicsProxyShape::Sphere;
        proxy.Sphere = Math::Sphere(Math::Vector3(centerX, 0.0f, 0.0f), radius);
        return proxy;
    }

    void AssertCanonicalPair(const PhysicsCandidatePair& pair, uint32_t first, uint32_t second)
    {
        assert((pair.First == ColliderHandle{first, 1}));
        assert((pair.Second == ColliderHandle{second, 1}));
    }

    bool NearlyEqual(float left, float right)
    {
        return std::fabs(left - right) < 0.001f;
    }

    bool IsDefaultRaycastHit(const PhysicsRaycastHit& hit)
    {
        return !hit.Collider.IsValid()
            && !hit.Body.IsValid()
            && !hit.Entity.IsValid()
            && !hit.bHasEntity
            && hit.Point == Math::Vector3()
            && hit.Normal == Math::Vector3()
            && hit.Distance == 0.0f
            && hit.UserData == 0;
    }

    void TestSapTouchingPermutationAndUnregister()
    {
        std::cout << "[Test] SAP touching endpoint, permutation, canonical pair and unregister\n";
        Core::Container::VariableArray<PhysicsShapeProxy> proxies;
        proxies.push_back(MakeSphereProxy(7, 0.0f, 1.0f));
        proxies.push_back(MakeSphereProxy(2, 2.0f, 1.0f));
        proxies.push_back(MakeSphereProxy(9, 8.0f, 1.0f));

        PhysicsBroadphase broadphase;
        broadphase.SetProxies(proxies);
        assert(broadphase.GetCandidatePairs().size() == 1);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[0], 2, 7);

        Core::Container::VariableArray<PhysicsShapeProxy> permuted;
        permuted.push_back(proxies[2]);
        permuted.push_back(proxies[0]);
        permuted.push_back(proxies[1]);
        broadphase.SetProxies(permuted);
        assert(broadphase.GetCandidatePairs().size() == 1);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[0], 2, 7);

        Core::Container::VariableArray<PhysicsShapeProxy> deduplicated;
        deduplicated.push_back(MakeSphereProxy(1, 0.0f, 2.0f));
        deduplicated.push_back(MakeSphereProxy(2, 0.0f, 2.0f));
        deduplicated.push_back(MakeSphereProxy(3, 0.0f, 2.0f));
        broadphase.SetProxies(deduplicated);
        assert(broadphase.GetCandidatePairs().size() == 3);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[0], 1, 2);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[1], 1, 3);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[2], 2, 3);

        Core::Container::VariableArray<PhysicsShapeProxy> empty;
        broadphase.SetProxies(empty);
        assert(broadphase.GetCandidatePairs().empty());
    }

    void TestLayerMaskCandidatePairs()
    {
        auto first = MakeSphereProxy(1,0,1), second = MakeSphereProxy(2,2,1);
        first.Layer = 1;
        first.Mask = 0x80000000u;
        second.Layer = 0x80000000u;
        second.Mask = 1;
        PhysicsBroadphase broadphase;
        auto set = [&]()
        {
            Core::Container::VariableArray<PhysicsShapeProxy> proxies;
            proxies.push_back(second);
            proxies.push_back(first);
            broadphase.SetProxies(std::move(proxies));
        };
        set();
        assert(broadphase.GetCandidatePairs().size() == 1);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[0],1,2);
        first.Mask = 0;
        set();
        assert(broadphase.GetCandidatePairs().empty());
        first.Mask = 0x80000000u;
        second.Mask = 0;
        set();
        assert(broadphase.GetCandidatePairs().empty());
        first.Layer = 3;
        second.Mask = 2;
        set();
        assert(broadphase.GetCandidatePairs().size() == 1);
        second.Mask = 4;
        set();
        assert(broadphase.GetCandidatePairs().empty());
        second.Mask = AllPhysicsLayers;
        first.Layer = 0;
        set();
        assert(broadphase.GetCandidatePairs().empty());
        first.Layer = DefaultPhysicsLayer;
        first.Mask = AllPhysicsLayers;
        second.Layer = DefaultPhysicsLayer;
        set();
        assert(broadphase.GetCandidatePairs().size() == 1);
        AssertCanonicalPair(broadphase.GetCandidatePairs()[0],1,2);
    }

    void TestValueOverlapAllShapes()
    {
        std::cout << "[Test] value overlap returns all shapes in handle order\n";
        Core::Container::VariableArray<PhysicsShapeProxy> proxies;
        proxies.push_back(MakeSphereProxy(3, 0.0f, 1.0f));

        PhysicsShapeProxy box;
        box.Collider = ColliderHandle{1, 1};
        box.Shape = EPhysicsProxyShape::Box;
        box.Box = Math::OBB(Math::Vector3(), Math::Vector3(1.0f, 1.0f, 1.0f),
            Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ);
        proxies.push_back(box);

        PhysicsShapeProxy capsule;
        capsule.Collider = ColliderHandle{2, 1};
        capsule.Shape = EPhysicsProxyShape::Capsule;
        capsule.Capsule = Math::Capsule(Math::Vector3(0.0f, -1.0f, 0.0f), Math::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
        proxies.push_back(capsule);

        PhysicsBroadphase broadphase;
        broadphase.SetProxies(proxies);
        Core::Container::VariableArray<PhysicsOverlapHit> hits;
        broadphase.OverlapSphere(Math::Sphere(Math::Vector3(), 0.5f), hits);
        assert(hits.size() == 3);
        assert((hits[0].Collider == ColliderHandle{1, 1}));
        assert((hits[1].Collider == ColliderHandle{2, 1}));
        assert((hits[2].Collider == ColliderHandle{3, 1}));

        hits.clear();
        broadphase.OverlapBox(Math::OBB(Math::Vector3(), Math::Vector3(0.5f, 0.5f, 0.5f),
            Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ), hits);
        assert(hits.size() == 3);
        assert(hits[0].Contact.Depth >= 0.0f);
        assert((hits[0].Collider == ColliderHandle{1, 1}));
        assert((hits[1].Collider == ColliderHandle{2, 1}));
        assert((hits[2].Collider == ColliderHandle{3, 1}));

        hits.clear();
        broadphase.OverlapCapsule(Math::Capsule(Math::Vector3(0.0f, -0.5f, 0.0f), Math::Vector3(0.0f, 0.5f, 0.0f), 0.5f), hits);
        assert(hits.size() == 3);
        assert(hits[0].Contact.Depth >= 0.0f);
        assert((hits[0].Collider == ColliderHandle{1, 1}));
        assert((hits[1].Collider == ColliderHandle{2, 1}));
        assert((hits[2].Collider == ColliderHandle{3, 1}));
    }

    struct PhysicsFixture
    {
        PhysicsFixture()
            : Registry(GetModuleRegistry())
            , Engine(GetEngine())
        {
            Physics = RegisterPhysicsModule(Registry);
            assert(Physics != nullptr);
            assert(Registry.InstallAll(Engine));
            World.Initialize();
        }

        ~PhysicsFixture()
        {
            World.Finalize();
            Registry.ShutdownAll(Engine);
        }

        Entity* CreateSphere(const Math::Transform& transform, float radius)
        {
            Entity* entity = World.SpawnEntity<Entity>();
            assert(entity != nullptr);
            entity->SetWorldTransform(transform);
            ColliderComponent* collider = World.CreateComponent<ColliderComponent>(entity);
            assert(collider != nullptr);
            assert(collider->SetSphere(radius) == EPhysicsResult::Success);
            return entity;
        }

        ModuleRegistry& Registry;
        Engine::Engine& Engine;
        IPhysicsModule* Physics = nullptr;
        World World;
    };

    void TestLayerMaskContactsAndEvents()
    {
        PhysicsFixture fixture;
        auto* firstEntity = fixture.CreateSphere(Math::Transform(Math::Vector3()),1);
        auto* secondEntity = fixture.CreateSphere(Math::Transform(Math::Vector3(1.5f,0,0)),1);
        auto* first = firstEntity->GetComponent<ColliderComponent>();
        auto* second = secondEntity->GetComponent<ColliderComponent>();
        auto* body = fixture.World.CreateComponent<RigidBodyComponent>(secondEntity);
        assert(body != nullptr);
        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0) == EPhysicsResult::Success);
        assert(first->SetCollisionLayer(1) == EPhysicsResult::Success);
        assert(first->SetCollisionMask(2) == EPhysicsResult::Success);
        assert(second->SetCollisionLayer(2) == EPhysicsResult::Success);
        assert(second->SetCollisionMask(1) == EPhysicsResult::Success);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        int begin = 0, end = 0, hit = 0;
        PhysicsCallbackHandle beginHandle, endHandle, hitHandle;
        assert(first->AddOnOverlapBegin(Core::Delegate<void,const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent&) { ++begin; }),beginHandle) == EPhysicsResult::Success);
        assert(first->AddOnOverlapEnd(Core::Delegate<void,const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent&) { ++end; }),endHandle) == EPhysicsResult::Success);
        assert(second->AddOnHit(Core::Delegate<void,const PhysicsContactEvent&>(
            [&](const PhysicsContactEvent&) { ++hit; }),hitHandle) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(begin == 1 && end == 0 && hit == 0);
        assert(secondEntity->GetLocalTransform().position.x == 1.5f);
        assert(second->SetCollisionMask(0) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(begin == 1 && end == 1 && hit == 0);
        assert(secondEntity->GetLocalTransform().position.x == 1.5f);
        assert(second->SetCollisionMask(1) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(begin == 2 && end == 1 && hit == 0);
        assert(first->SetTrigger(false) == EPhysicsResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(-1,0,0)) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(end == 2 && hit == 1);
        assert(secondEntity->GetLocalTransform().position.x > 1.5f);
        secondEntity->SetLocalPosition(1.5f,0,0);
        assert(body->SetLinearVelocity(Math::Vector3(-1,0,0)) == EPhysicsResult::Success);
        assert(first->SetCollisionMask(0) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(begin == 2 && end == 2 && hit == 1);
        assert(NearlyEqual(secondEntity->GetLocalTransform().position.x,1.5f-1.f/60));
        assert(body->GetLinearVelocity() == Math::Vector3(-1,0,0));
        // 空間検索のLayerMaskは相互作用Maskではない。maskで非衝突でもquery対象にはなる。
        PhysicsQueryDesc query;
        query.Kind = EPhysicsQueryKind::OverlapSphere;
        query.Sphere = Math::Sphere(Math::Vector3(),10);
        Core::Container::VariableArray<PhysicsQueryHit> hits;
        assert(fixture.Engine.GetSceneQuery().ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 2);
        assert(first->RemoveOnOverlapBegin(beginHandle) == EPhysicsResult::Success);
        assert(first->RemoveOnOverlapEnd(endHandle) == EPhysicsResult::Success);
        assert(second->RemoveOnHit(hitHandle) == EPhysicsResult::Success);
    }

    void TestExplicitSnapshotRefresh()
    {
        PhysicsFixture fixture;
        SceneQuery& scene = fixture.Engine.GetSceneQuery();
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::NotReady);
        auto* entity = fixture.CreateSphere(Math::Transform(Math::Vector3()),1);
        auto* collider = entity->GetComponent<ColliderComponent>();
        auto* body = fixture.World.CreateComponent<RigidBodyComponent>(entity);
        assert(body != nullptr);
        assert(body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        uint64_t sequence = 0, after = 0;
        assert(scene.GetPublishedSnapshotSequence(sequence) == EPhysicsSceneQueryResult::Success);
        assert(body->SetLinearVelocity(Math::Vector3(3,0,0)) == EPhysicsResult::Success);
        assert(body->AddImpulse(Math::Vector3(2,0,0)) == EPhysicsResult::Success);
        fixture.Physics->PreFixedTick(1.f/60);
        const auto handle = body->GetBodyHandle();
        const auto pending = PhysicsModuleTestAccess::GetPendingImpulse(*fixture.Physics,handle);
        const auto preStep = PhysicsModuleTestAccess::GetPreStepPosition(*fixture.Physics,handle);
        const auto preValid = PhysicsModuleTestAccess::HasPreStepSnapshot(*fixture.Physics,handle);
        const auto events = PhysicsModuleTestAccess::GetDispatchedEventCount(*fixture.Physics);
        const auto pairs = PhysicsModuleTestAccess::GetPreviousPairCount(*fixture.Physics);
        entity->SetLocalPosition(5,0,0);
        assert(collider->SetUserData(88) == EPhysicsResult::Success);
        const Math::Ray ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX);
        PhysicsRaycastHit hit;
        assert(scene.Raycast(ray,20,hit) == EPhysicsSceneQueryResult::Success && NearlyEqual(hit.Distance,4));
        EPhysicsSceneQueryResult threadResult{};
        Thread::Thread worker([&]() { threadResult = scene.RefreshDynamicSnapshot(); });
        worker.Join();
        assert(threadResult == EPhysicsSceneQueryResult::WrongThread);
        assert(scene.Raycast(ray,20,hit) == EPhysicsSceneQueryResult::Success && NearlyEqual(hit.Distance,4));
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Success);
        assert(scene.Raycast(ray,20,hit) == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance,9) && hit.UserData == 88 && hit.Body == handle);
        assert(scene.GetPublishedSnapshotSequence(after) == EPhysicsSceneQueryResult::Success && sequence == after);
        assert(entity->GetLocalTransform().position == Math::Vector3(5,0,0));
        assert(body->GetLinearVelocity() == Math::Vector3(3,0,0));
        assert(PhysicsModuleTestAccess::GetPendingImpulse(*fixture.Physics,handle) == pending);
        assert(PhysicsModuleTestAccess::GetPreStepPosition(*fixture.Physics,handle) == preStep);
        assert(PhysicsModuleTestAccess::HasPreStepSnapshot(*fixture.Physics,handle) == preValid);
        assert(PhysicsModuleTestAccess::GetDispatchedEventCount(*fixture.Physics) == events);
        assert(PhysicsModuleTestAccess::GetPreviousPairCount(*fixture.Physics) == pairs);
        auto* freshEntity = fixture.CreateSphere(Math::Transform(Math::Vector3(10,0,0)),1);
        auto* freshCollider = freshEntity->GetComponent<ColliderComponent>();
        auto* freshBody = fixture.World.CreateComponent<RigidBodyComponent>(freshEntity);
        assert(freshBody != nullptr);
        assert(freshBody->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success);
        const bool wasBodyActive = PhysicsModuleTestAccess::IsBodyActive(*fixture.Physics,freshBody->GetBodyHandle());
        const bool wasColliderActive = PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics,freshCollider->GetColliderHandle());
        entity->SetActive(false);
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Success);
        assert(scene.Raycast(ray,20,hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == freshCollider->GetColliderHandle() && hit.Body == freshBody->GetBodyHandle());
        assert(PhysicsModuleTestAccess::IsBodyActive(*fixture.Physics,freshBody->GetBodyHandle()) == wasBodyActive);
        assert(PhysicsModuleTestAccess::IsColliderActive(*fixture.Physics,freshCollider->GetColliderHandle()) == wasColliderActive);
        entity->SetActive(true);
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Success);
        assert(scene.Raycast(ray,20,hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == collider->GetColliderHandle() && hit.Body == handle && NearlyEqual(hit.Distance,9));
        assert(PhysicsModuleTestAccess::GetPendingImpulse(*fixture.Physics,handle) == pending);
        fixture.Physics->FixedTick(1.f/60);
        assert(body->GetLinearVelocity() == Math::Vector3(5,0,0));
        assert(NearlyEqual(entity->GetLocalTransform().position.x,5+5.f/60));
        assert(scene.GetPublishedSnapshotSequence(after) == EPhysicsSceneQueryResult::Success && after == sequence+1);
    }

    void TestRefreshRejectedDuringPhysicsNotification()
    {
        PhysicsFixture fixture;
        SceneQuery& scene = fixture.Engine.GetSceneQuery();
        auto* first = fixture.CreateSphere(Math::Transform(Math::Vector3()),1)->GetComponent<ColliderComponent>();
        fixture.CreateSphere(Math::Transform(Math::Vector3(1.5f,0,0)),1);
        assert(first->SetTrigger(true) == EPhysicsResult::Success);
        assert(first->SetUserData(111) == EPhysicsResult::Success);
        int calls = 0;
        EPhysicsSceneQueryResult refreshResult{};
        PhysicsCallbackHandle subscription;
        assert(first->AddOnOverlapBegin(Core::Delegate<void,const PhysicsContactEvent&>([&](const PhysicsContactEvent&)
        {
            ++calls;
            assert(first->SetUserData(222) == EPhysicsResult::Success);
            refreshResult = scene.RefreshDynamicSnapshot();
            PhysicsRaycastHit hit;
            assert(scene.Raycast(Math::Ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX),10,hit)
                == EPhysicsSceneQueryResult::Success);
            assert(hit.UserData == 111);
        }),subscription) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(calls == 1 && refreshResult == EPhysicsSceneQueryResult::NotReady);
        assert(first->RemoveOnOverlapBegin(subscription) == EPhysicsResult::Success);
        uint64_t before = 0, after = 0;
        assert(scene.GetPublishedSnapshotSequence(before) == EPhysicsSceneQueryResult::Success);
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Success);
        PhysicsRaycastHit hit;
        assert(scene.Raycast(Math::Ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX),10,hit)
            == EPhysicsSceneQueryResult::Success && hit.UserData == 222);
        assert(scene.GetPublishedSnapshotSequence(after) == EPhysicsSceneQueryResult::Success && before == after);
        assert(calls == 1);
    }

    void TestPublishedQueryBatch()
    {
        PhysicsFixture fixture;
        SceneQuery& scene = fixture.Engine.GetSceneQuery();
        PhysicsQueryDesc queries[6];
        queries[0].Kind = EPhysicsQueryKind::RaycastAll;
        queries[0].Ray = Math::Ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX);
        queries[0].MaxDistance = 20;
        queries[0].MaxHits = 2;
        queries[1] = queries[0];
        queries[1].Filter.LayerMask = 0;
        queries[2] = queries[0];
        queries[2].MaxHits = 0;
        queries[3] = queries[0];
        queries[3].Kind = EPhysicsQueryKind::SweepSphere;
        queries[3].Sphere = Math::Sphere(Math::Vector3(-5,0,0),.5f);
        queries[3].Direction = Math::Vector3::UnitX;
        queries[3].MaxHits = 1;
        queries[4] = queries[0];
        queries[4].Filter.LayerMask = 4;
        queries[5].Kind = EPhysicsQueryKind::OverlapSphere;
        queries[5].Sphere = Math::Sphere(Math::Vector3(8,0,0),.2f);
        Core::Container::VariableArray<PhysicsQueryHit> hits;
        Core::Container::VariableArray<PhysicsQueryBatchResult> results;
        hits.emplace_back(); results.emplace_back();
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::NotReady);
        assert(hits.empty() && results.empty());
        auto* first = fixture.CreateSphere(Math::Transform(Math::Vector3()),1)->GetComponent<ColliderComponent>();
        auto* second = fixture.CreateSphere(Math::Transform(Math::Vector3(4,0,0)),1)->GetComponent<ColliderComponent>();
        auto* third = fixture.CreateSphere(Math::Transform(Math::Vector3(8,0,0)),1)->GetComponent<ColliderComponent>();
        assert(first->SetCollisionLayer(2) == EPhysicsResult::Success);
        assert(second->SetCollisionLayer(4) == EPhysicsResult::Success);
        assert(third->SetCollisionLayer(2) == EPhysicsResult::Success);
        assert(first->SetUserData(111) == EPhysicsResult::Success);
        assert(second->SetUserData(222) == EPhysicsResult::Success);
        assert(third->SetUserData(333) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        uint64_t before = 0, after = 0;
        assert(scene.GetPublishedSnapshotSequence(before) == EPhysicsSceneQueryResult::Success);
        assert(first->SetUserData(444) == EPhysicsResult::Success);
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::Success);
        assert(results.size() == 6 && hits.size() == 5);
        const size_t offsets[]{0,2,2,2,3,4};
        const size_t counts[]{2,0,0,1,1,1};
        Core::Container::VariableArray<PhysicsQueryHit> single;
        for (size_t index = 0; index < 6; ++index)
        {
            assert(results[index].FirstHit == offsets[index] && results[index].HitCount == counts[index]);
            const auto result = scene.ExecuteQuery(queries[index],single);
            assert(result == results[index].Result && single.size() == results[index].HitCount);
            for (size_t hit = 0; hit < single.size(); ++hit)
            {
                const auto& batched = hits[results[index].FirstHit+hit];
                assert(single[hit].Collider == batched.Collider && single[hit].UserData == batched.UserData);
                assert(single[hit].Distance == batched.Distance && single[hit].Normal == batched.Normal);
                assert(single[hit].Body == batched.Body && single[hit].Entity == batched.Entity);
                assert(single[hit].bHasEntity == batched.bHasEntity && single[hit].Point == batched.Point);
                assert(single[hit].Depth == batched.Depth && single[hit].bStartPenetrating == batched.bStartPenetrating);
            }
        }
        assert(results[1].Result == EPhysicsSceneQueryResult::NoHit);
        assert(results[2].Result == EPhysicsSceneQueryResult::InvalidArgument);
        assert(hits[0].UserData == 111 && hits[1].UserData == 222 && hits[2].UserData == 111);
        assert(hits[3].UserData == 222 && hits[4].UserData == 333);
        assert(scene.GetPublishedSnapshotSequence(after) == EPhysicsSceneQueryResult::Success && before == after);
        assert(scene.ExecuteBatch({},hits,results) == EPhysicsSceneQueryResult::Success);
        assert(hits.empty() && results.empty());
        assert(scene.ExecuteBatch(Core::Container::Span<const PhysicsQueryDesc>(nullptr,1),hits,results)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(hits.empty() && results.empty());
        fixture.Physics->FixedTick(1.f/60);
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::Success);
        assert(hits[0].UserData == 444 && hits[2].UserData == 444);
    }

    void TestUnifiedPublishedQuery()
    {
        PhysicsFixture fixture;
        SceneQuery& scene = fixture.Engine.GetSceneQuery();
        PhysicsQueryDesc query;
        query.Kind = EPhysicsQueryKind::RaycastAll;
        query.Ray = Math::Ray(Math::Vector3(-5,0,0),Math::Vector3(2,0,0));
        query.MaxDistance = 20;
        Core::Container::VariableArray<PhysicsQueryHit> hits;
        hits.emplace_back();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::NotReady && hits.empty());
        auto* first = fixture.CreateSphere(Math::Transform(Math::Vector3()),1)->GetComponent<ColliderComponent>();
        auto* second = fixture.CreateSphere(Math::Transform(Math::Vector3(4,0,0)),1)->GetComponent<ColliderComponent>();
        auto* trigger = fixture.CreateSphere(Math::Transform(Math::Vector3(8,0,0)),1)->GetComponent<ColliderComponent>();
        assert(first->SetCollisionLayer(2) == EPhysicsResult::Success);
        assert(second->SetCollisionLayer(4) == EPhysicsResult::Success);
        assert(trigger->SetCollisionLayer(2) == EPhysicsResult::Success);
        assert(first->SetUserData(111) == EPhysicsResult::Success);
        assert(second->SetUserData(222) == EPhysicsResult::Success);
        assert(trigger->SetUserData(333) == EPhysicsResult::Success);
        assert(trigger->SetTrigger(true) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 3 && hits[0].UserData == 111 && hits[1].UserData == 222 && hits[2].UserData == 333);
        assert(NearlyEqual(hits[0].Distance,4) && NearlyEqual(hits[2].Distance,12));
        query.Filter.LayerMask = 2;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success && hits.size() == 2);
        query.Filter.IgnoreColliders[0] = first->GetColliderHandle();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 1 && hits[0].UserData == 333);
        query.Filter.IgnoreColliders[0] = {};
        query.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Exclude;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 1 && hits[0].UserData == 111);
        query.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Only;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 1 && hits[0].UserData == 333);
        query.Filter = {};
        query.Kind = EPhysicsQueryKind::RaycastClosest;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 1 && hits[0].UserData == 111);
        query.Kind = EPhysicsQueryKind::OverlapSphere;
        query.Sphere = Math::Sphere(Math::Vector3(),20);
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success && hits.size() == 3);
        query.Kind = EPhysicsQueryKind::OverlapBox;
        query.Box = Math::OBB(Math::Vector3(),Math::Vector3(20,20,20),Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success && hits.size() == 3);
        query.Kind = EPhysicsQueryKind::OverlapCapsule;
        query.Capsule = Math::Capsule(Math::Vector3(),Math::Vector3(8,0,0),2);
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success && hits.size() == 3);
        query.Kind = EPhysicsQueryKind::SweepSphere;
        query.Sphere = Math::Sphere(Math::Vector3(-5,0,0),.5f);
        query.Direction = Math::Vector3(2,0,0);
        query.MaxHits = 2;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(hits.size() == 2 && hits[0].UserData == 111 && hits[1].UserData == 222);
        assert(NearlyEqual(hits[0].Distance,3.5f));
        query.Kind = EPhysicsQueryKind::SweepCapsule;
        query.Capsule = Math::Capsule(Math::Vector3(-5,-.5f,0),Math::Vector3(-5,.5f,0),.5f);
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success && hits.size() == 2);
        query.MaxHits = 0;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::InvalidArgument && hits.empty());
        query.MaxHits = 2;
        query.Filter.LayerMask = 0;
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::NoHit && hits.empty());
    }

    void TestColliderMetadataSnapshot()
    {
        PhysicsFixture fixture;
        Entity* entity = fixture.CreateSphere(Math::Transform(Math::Vector3()),1);
        auto* collider = entity->GetComponent<ColliderComponent>();
        assert(collider->GetCollisionLayer() == DefaultPhysicsLayer);
        assert(collider->GetCollisionMask() == AllPhysicsLayers);
        assert(collider->GetUserData() == 0);
        fixture.Physics->FixedTick(1.f/60);
        const auto handle = collider->GetColliderHandle();
        PhysicsShapeProxy snapshot;
        assert(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics,handle,snapshot));
        assert(snapshot.Layer == DefaultPhysicsLayer && snapshot.Mask == AllPhysicsLayers);
        assert(snapshot.UserData == 0 && !snapshot.bTrigger);
        assert(collider->SetCollisionLayer(0x80000002u) == EPhysicsResult::Success);
        assert(collider->SetCollisionMask(0) == EPhysicsResult::Success);
        assert(collider->SetUserData(UINT64_MAX) == EPhysicsResult::Success);
        assert(collider->SetTrigger(true) == EPhysicsResult::Success);
        // setterは公開済みsnapshotをその場で変更しない。
        assert(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics,handle,snapshot));
        assert(snapshot.Layer == DefaultPhysicsLayer && snapshot.Mask == AllPhysicsLayers);
        assert(snapshot.UserData == 0 && !snapshot.bTrigger);
        EPhysicsResult layerResult{}, maskResult{}, dataResult{};
        Thread::Thread worker([&]()
        {
            layerResult = collider->SetCollisionLayer(1);
            maskResult = collider->SetCollisionMask(1);
            dataResult = collider->SetUserData(1);
        });
        worker.Join();
        assert(layerResult == EPhysicsResult::WrongThread);
        assert(maskResult == EPhysicsResult::WrongThread);
        assert(dataResult == EPhysicsResult::WrongThread);
        assert(collider->GetCollisionLayer() == 0x80000002u);
        assert(collider->GetCollisionMask() == 0 && collider->GetUserData() == UINT64_MAX);
        SceneQuery& query = fixture.Engine.GetSceneQuery();
        for (int shape = 0; shape < 3; ++shape)
        {
            if (shape == 1)
            {
                assert(collider->SetBox(Math::Vector3(1,1,1)) == EPhysicsResult::Success);
            }
            if (shape == 2)
            {
                assert(collider->SetCapsule(1,1) == EPhysicsResult::Success);
            }
            fixture.Physics->FixedTick(1.f/60);
            assert(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics,handle,snapshot));
            assert(snapshot.Layer == 0x80000002u && snapshot.Mask == 0);
            assert(snapshot.UserData == UINT64_MAX && snapshot.bTrigger);
            PhysicsRaycastHit hit;
            assert(query.Raycast(Math::Ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX),10,hit)
                == EPhysicsSceneQueryResult::Success);
            assert(hit.Collider == handle && hit.UserData == UINT64_MAX);
            Core::Container::VariableArray<PhysicsOverlapHit> hits;
            assert(query.OverlapSphere(Math::Sphere(Math::Vector3(),.5f),hits) == EPhysicsSceneQueryResult::Success);
            assert(hits.size() == 1 && hits[0].UserData == UINT64_MAX);
            assert(query.OverlapBox(Math::OBB(Math::Vector3(),Math::Vector3(.5f,.5f,.5f),
                Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ),hits) == EPhysicsSceneQueryResult::Success);
            assert(hits.size() == 1 && hits[0].UserData == UINT64_MAX);
            assert(query.OverlapCapsule(Math::Capsule(Math::Vector3(0,-.5f,0),Math::Vector3(0,.5f,0),.5f),hits)
                == EPhysicsSceneQueryResult::Success);
            assert(hits.size() == 1 && hits[0].UserData == UINT64_MAX);
            hit.UserData = UINT64_MAX;
            assert(query.Raycast(Math::Ray(Math::Vector3(100,0,0),Math::Vector3::UnitX),10,hit)
                == EPhysicsSceneQueryResult::NoHit);
            assert(IsDefaultRaycastHit(hit));
        }
        assert(collider->SetCollisionLayer(0) == EPhysicsResult::Success);
        assert(collider->SetCollisionMask(AllPhysicsLayers) == EPhysicsResult::Success);
        assert(collider->SetUserData(0) == EPhysicsResult::Success);
        assert(collider->SetTrigger(false) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.f/60);
        assert(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics,handle,snapshot));
        assert(snapshot.Layer == 0 && snapshot.Mask == AllPhysicsLayers);
        assert(snapshot.UserData == 0 && !snapshot.bTrigger);
        assert(PhysicsModuleTestAccess::UnregisterColliderForTest(*fixture.Physics,*collider) == EPhysicsResult::Success);
        assert(collider->SetCollisionLayer(2) == EPhysicsResult::NotRegistered);
        assert(collider->SetCollisionMask(2) == EPhysicsResult::NotRegistered);
        assert(collider->SetUserData(2) == EPhysicsResult::NotRegistered);
        assert(collider->GetCollisionLayer() == 0 && collider->GetCollisionMask() == AllPhysicsLayers);
        assert(collider->GetUserData() == 0);
    }

    void TestSnapshotScaleAndRayContract()
    {
        std::cout << "[Test] post-step snapshot, scale rules and ray validation\n";
        PhysicsFixture fixture;
        SceneQuery& query = fixture.Engine.GetSceneQuery();
        PhysicsRaycastHit hit;
        const Math::Ray ray(Math::Vector3(-5.0f, 0.0f, 0.0f), Math::Vector3(2.0f, 0.0f, 0.0f));
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NotReady);

        fixture.Physics->FixedTick(1.0f / 60.0f);
        Core::Container::VariableArray<PhysicsOverlapHit> emptyHits;
        emptyHits.push_back(PhysicsOverlapHit{ColliderHandle{1, 1}});
        assert(query.OverlapSphere(Math::Sphere(Math::Vector3(100.0f, 0.0f, 0.0f), 1.0f), emptyHits)
            == EPhysicsSceneQueryResult::NoHit);
        assert(emptyHits.empty());

        Entity* unconfigured = fixture.World.SpawnEntity<Entity>();
        assert(unconfigured != nullptr);
        assert(fixture.World.CreateComponent<ColliderComponent>(unconfigured) != nullptr);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);

        Entity* entity = fixture.CreateSphere(Math::Transform(
            Math::Vector3(), Math::Quaternion::Identity, Math::Vector3(2.0f, 3.0f, 4.0f)), 1.0f);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Distance == 1.0f);
        const ColliderHandle preservedHandle = hit.Collider;

        entity->SetWorldTransform(Math::Transform(Math::Vector3(100.0f, 0.0f, 0.0f)));
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider.IsValid());
        assert(hit.Entity.IsValid());
        assert(hit.bHasEntity);
        assert(hit.Point != Math::Vector3());
        assert(hit.Normal != Math::Vector3());
        assert(hit.Distance > 0.0f);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);
        assert(IsDefaultRaycastHit(hit));

        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion::Identity,
            Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f)));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);

        entity->SetWorldTransform(Math::Transform(Math::Vector3()));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == preservedHandle);

        ColliderComponent* collider = entity->GetComponent<ColliderComponent>();
        assert(collider != nullptr);
        assert(collider->SetSphere(1.0f) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(-5.0f, 0.0f, 0.0f), Math::Vector3(0.0001f, 0.0f, 0.0f)), 10.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance, 4.0f));
        assert(query.Raycast(Math::Ray(Math::Vector3(-5.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 4.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance, 4.0f));
        assert(query.Raycast(Math::Ray(Math::Vector3(-5.0f, 0.0f, 0.0f),
            Math::Vector3(std::numeric_limits<float>::max(), 0.0f, 0.0f)), 10.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance, 4.0f));

        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion(Math::Vector3::UnitZ, 1.57079632679f), Math::Vector3(-2.0f, -3.0f, -4.0f)));
        assert(collider->SetBox(Math::Vector3(1.0f, 0.5f, 1.0f)) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(-5.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 10.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance, 3.5f));

        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion(Math::Vector3::UnitZ, 1.57079632679f), Math::Vector3(2.0f, 3.0f, 4.0f)));
        assert(collider->SetCapsule(1.0f, 1.0f) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(-10.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 10.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(NearlyEqual(hit.Distance, 3.0f));

        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion::Identity, Math::Vector3(0.0f, 1.0f, 1.0f)));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(0.0f, -10.0f, 0.0f), Math::Vector3::UnitY), 10.0f, hit)
            == EPhysicsSceneQueryResult::NoHit);
        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);
        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion(
                std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f), Math::Vector3::One));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NoHit);
        entity->SetWorldTransform(Math::Transform(
            Math::Vector3(), Math::Quaternion::Identity,
            Math::Vector3(std::numeric_limits<float>::infinity(), 1.0f, 1.0f)));
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(0.0f, -10.0f, 0.0f), Math::Vector3::UnitY), 10.0f, hit)
            == EPhysicsSceneQueryResult::NoHit);
        entity->SetWorldTransform(Math::Transform(Math::Vector3()));
        collider->Disable();
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(0.0f, -10.0f, 0.0f), Math::Vector3::UnitY), 10.0f, hit)
            == EPhysicsSceneQueryResult::NoHit);
        collider->Enable();
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(0.0f, -10.0f, 0.0f), Math::Vector3::UnitY), 10.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(hit.Collider == preservedHandle);

        entity->SetActive(false);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(Math::Ray(Math::Vector3(0.0f, -10.0f, 0.0f), Math::Vector3::UnitY), 10.0f, hit)
            == EPhysicsSceneQueryResult::NoHit);
        entity->SetActive(true);
        fixture.Physics->FixedTick(1.0f / 60.0f);

        hit = PhysicsRaycastHit{ColliderHandle{1, 1}};
        assert(query.Raycast(Math::Ray(Math::Vector3(), Math::Vector3()), 1.0f, hit)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(!hit.Collider.IsValid());
        assert(query.Raycast(ray, -1.0f, hit) == EPhysicsSceneQueryResult::InvalidArgument);
        assert(query.Raycast(ray, std::numeric_limits<float>::infinity(), hit) == EPhysicsSceneQueryResult::InvalidArgument);
        assert(query.Raycast(ray, std::numeric_limits<float>::quiet_NaN(), hit) == EPhysicsSceneQueryResult::InvalidArgument);
        assert(query.Raycast(Math::Ray(Math::Vector3(), Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)), 1.0f, hit)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(query.Raycast(Math::Ray(Math::Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f), Math::Vector3::UnitX), 1.0f, hit)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(query.Raycast(Math::Ray(Math::Vector3(-1.0f, 0.0f, 0.0f), Math::Vector3::UnitX), 0.0f, hit)
            == EPhysicsSceneQueryResult::Success);
        assert(hit.Distance == 0.0f);

        assert(query.Raycast(Math::Ray(Math::Vector3(-5.0f, 9.0f, 0.0f), Math::Vector3::UnitX), 0.0f, hit)
            == EPhysicsSceneQueryResult::NoHit);

        emptyHits.push_back(PhysicsOverlapHit{ColliderHandle{1, 1}});
        assert(query.OverlapSphere(Math::Sphere(Math::Vector3(), std::numeric_limits<float>::quiet_NaN()), emptyHits)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(emptyHits.empty());

        bool bAlive = true;
        entity->RemoveComponent(collider);
        assert(query.IsAlive(preservedHandle, bAlive) == EPhysicsSceneQueryResult::Success);
        assert(!bAlive);

        ColliderComponent* reusedCollider = fixture.World.CreateComponent<ColliderComponent>(entity);
        assert(reusedCollider != nullptr);
        assert(reusedCollider->SetSphere(1.0f) == EPhysicsResult::Success);
        const ColliderHandle reusedHandle = reusedCollider->GetColliderHandle();
        assert(reusedHandle.Index == preservedHandle.Index);
        assert(reusedHandle.Generation != preservedHandle.Generation);
        assert(query.IsAlive(preservedHandle, bAlive) == EPhysicsSceneQueryResult::Success);
        assert(!bAlive);

        Entity* boxEntity = fixture.CreateSphere(Math::Transform(Math::Vector3()), 1.0f);
        ColliderComponent* boxCollider = boxEntity->GetComponent<ColliderComponent>();
        assert(boxCollider->SetBox(Math::Vector3(1.0f, 1.0f, 1.0f)) == EPhysicsResult::Success);
        Entity* capsuleEntity = fixture.CreateSphere(Math::Transform(Math::Vector3()), 1.0f);
        ColliderComponent* capsuleCollider = capsuleEntity->GetComponent<ColliderComponent>();
        assert(capsuleCollider->SetCapsule(1.0f, 1.0f) == EPhysicsResult::Success);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        Core::Container::VariableArray<PhysicsOverlapHit> overlapHits;
        assert(query.OverlapBox(Math::OBB(Math::Vector3(), Math::Vector3(0.5f, 0.5f, 0.5f),
            Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ), overlapHits)
            == EPhysicsSceneQueryResult::Success);
        assert(overlapHits.size() == 3);
        assert(overlapHits[0].Collider < overlapHits[1].Collider);
        assert(overlapHits[1].Collider < overlapHits[2].Collider);
        assert(overlapHits[0].Contact.Depth >= 0.0f);
        overlapHits.clear();
        assert(query.OverlapCapsule(Math::Capsule(Math::Vector3(0.0f, -0.5f, 0.0f), Math::Vector3(0.0f, 0.5f, 0.0f), 0.5f), overlapHits)
            == EPhysicsSceneQueryResult::Success);
        assert(overlapHits.size() == 3);
        assert(overlapHits[0].Collider < overlapHits[1].Collider);
        assert(overlapHits[1].Collider < overlapHits[2].Collider);
        assert(overlapHits[0].Contact.Depth >= 0.0f);

        entity->MarkForDestroy();
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.IsAlive(reusedHandle, bAlive) == EPhysicsSceneQueryResult::Success);
        assert(bAlive);
        overlapHits.clear();
        assert(query.OverlapBox(Math::OBB(Math::Vector3(), Math::Vector3(0.5f, 0.5f, 0.5f),
            Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ), overlapHits)
            == EPhysicsSceneQueryResult::Success);
        assert(overlapHits.size() == 2);
        assert(PhysicsModuleTestAccess::ShutdownAndInitialize(*fixture.Physics));
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::NotReady);
        fixture.Physics->FixedTick(1.0f / 60.0f);
        assert(query.Raycast(ray, 10.0f, hit) == EPhysicsSceneQueryResult::Success);
    }
} // namespace

int main()
{
    std::cout << "PhysicsBroadphaseQueryTest start\n";
    TestSapTouchingPermutationAndUnregister();
    TestValueOverlapAllShapes();
    TestSnapshotScaleAndRayContract();
    TestColliderMetadataSnapshot();
    TestUnifiedPublishedQuery();
    TestPublishedQueryBatch();
    TestLayerMaskCandidatePairs();
    TestLayerMaskContactsAndEvents();
    TestExplicitSnapshotRefresh();
    TestRefreshRejectedDuringPhysicsNotification();
    std::cout << "PhysicsBroadphaseQueryTest passed\n";
    return 0;
}
