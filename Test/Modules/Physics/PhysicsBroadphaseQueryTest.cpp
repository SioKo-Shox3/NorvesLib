// PhysicsBroadphaseQueryTest — Physics private SAP と値型 snapshot query の契約を検証する。

#include "Physics/PhysicsBroadphase.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Scene/SceneQuery.h"

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

namespace NorvesLib::Modules::Physics
{
    class PhysicsModuleTestAccess
    {
    public:
        static bool ShutdownAndInitialize(IPhysicsModule& module)
        {
            auto* concrete = dynamic_cast<PhysicsModule*>(&module);
            assert(concrete != nullptr);
            concrete->Shutdown();
            return concrete->Initialize();
        }
    };
} // namespace NorvesLib::Modules::Physics

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
            && hit.Distance == 0.0f;
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
    std::cout << "PhysicsBroadphaseQueryTest passed\n";
    return 0;
}
