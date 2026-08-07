#include "Scene/SceneQuery.h"
#include "Object/World.h"
#include "Thread/Thread.h"
#include <cassert>
#include <iostream>
#include <type_traits>
#include <Windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Scene;

static_assert(std::is_same_v<std::underlying_type_t<EPhysicsSceneQueryResult>, uint8_t>);

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

    constexpr ColliderHandle TestCollider{7, 3};
    constexpr BodyHandle TestBody{11, 5};
    constexpr EntityHandle TestEntity{13, 17};

    bool IsZeroVector(const Math::Vector3& vector)
    {
        return vector.x == 0.0f && vector.y == 0.0f && vector.z == 0.0f;
    }

    bool IsDefaultHit(const PhysicsRaycastHit& hit)
    {
        return !hit.Collider.IsValid()
            && !hit.Body.IsValid()
            && !hit.Entity.IsValid()
            && !hit.bHasEntity
            && IsZeroVector(hit.Point)
            && IsZeroVector(hit.Normal)
            && hit.Distance == 0.0f;
    }

    bool IsDefaultHit(const PhysicsOverlapHit& hit)
    {
        return !hit.Collider.IsValid()
            && !hit.Body.IsValid()
            && !hit.Entity.IsValid()
            && !hit.bHasEntity
            && IsZeroVector(hit.Contact.Normal)
            && hit.Contact.Depth == 0.0f
            && IsZeroVector(hit.Contact.Point);
    }

    PhysicsRaycastHit CreateRaycastHit()
    {
        PhysicsRaycastHit hit;
        hit.Collider = TestCollider;
        hit.Body = TestBody;
        hit.Entity = TestEntity;
        hit.bHasEntity = true;
        hit.Point = Math::Vector3(3.0f, 5.0f, 7.0f);
        hit.Normal = Math::Vector3(0.0f, 1.0f, 0.0f);
        hit.Distance = 19.0f;
        return hit;
    }

    PhysicsOverlapHit CreateOverlapHit()
    {
        PhysicsOverlapHit hit;
        hit.Collider = TestCollider;
        hit.Body = TestBody;
        hit.Entity = TestEntity;
        hit.bHasEntity = true;
        hit.Contact.Normal = Math::Vector3(1.0f, 0.0f, 0.0f);
        hit.Contact.Depth = 23.0f;
        hit.Contact.Point = Math::Vector3(29.0f, 31.0f, 37.0f);
        return hit;
    }

    class FakePhysicsSceneQueryProvider final : public IPhysicsSceneQueryProvider
    {
    public:
        EPhysicsSceneQueryResult Result = EPhysicsSceneQueryResult::Success;
        PhysicsRaycastHit RaycastOutput = CreateRaycastHit();
        Container::VariableArray<PhysicsOverlapHit> OverlapOutput;
        bool ColliderAliveOutput = true;
        bool BodyAliveOutput = true;
        uint64_t PublishedSnapshotSequenceOutput = 173;

        mutable uint32_t RaycastCallCount = 0;
        mutable uint32_t SphereCallCount = 0;
        mutable uint32_t BoxCallCount = 0;
        mutable uint32_t CapsuleCallCount = 0;
        mutable uint32_t ColliderAliveCallCount = 0;
        mutable uint32_t BodyAliveCallCount = 0;
        mutable uint32_t PublishedSnapshotSequenceCallCount = 0;

        mutable Math::Ray LastRay;
        mutable float LastMaxDistance = 0.0f;
        mutable Math::Sphere LastSphere;
        mutable Math::OBB LastBox;
        mutable Math::Capsule LastCapsule;
        mutable ColliderHandle LastCollider;
        mutable BodyHandle LastBody;

        EPhysicsSceneQueryResult Raycast(
            const Math::Ray& ray,
            float maxDistance,
            PhysicsRaycastHit& outHit) const override
        {
            ++RaycastCallCount;
            LastRay = ray;
            LastMaxDistance = maxDistance;
            outHit = RaycastOutput;
            return Result;
        }

        EPhysicsSceneQueryResult OverlapSphere(
            const Math::Sphere& sphere,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const override
        {
            ++SphereCallCount;
            LastSphere = sphere;
            outHits = OverlapOutput;
            return Result;
        }

        EPhysicsSceneQueryResult OverlapBox(
            const Math::OBB& box,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const override
        {
            ++BoxCallCount;
            LastBox = box;
            outHits = OverlapOutput;
            return Result;
        }

        EPhysicsSceneQueryResult OverlapCapsule(
            const Math::Capsule& capsule,
            Container::VariableArray<PhysicsOverlapHit>& outHits) const override
        {
            ++CapsuleCallCount;
            LastCapsule = capsule;
            outHits = OverlapOutput;
            return Result;
        }

        EPhysicsSceneQueryResult IsAlive(ColliderHandle collider, bool& outAlive) const override
        {
            ++ColliderAliveCallCount;
            LastCollider = collider;
            outAlive = ColliderAliveOutput;
            return Result;
        }

        EPhysicsSceneQueryResult IsAlive(BodyHandle body, bool& outAlive) const override
        {
            ++BodyAliveCallCount;
            LastBody = body;
            outAlive = BodyAliveOutput;
            return Result;
        }

        EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t& outSequence) const override
        {
            ++PublishedSnapshotSequenceCallCount;
            outSequence = PublishedSnapshotSequenceOutput;
            return Result;
        }
    };

    void SetSentinel(PhysicsRaycastHit& hit)
    {
        hit = CreateRaycastHit();
    }

    void SetSentinel(Container::VariableArray<PhysicsOverlapHit>& hits)
    {
        hits.clear();
        hits.push_back(CreateOverlapHit());
    }

    void AssertSingleOverlapHit(const Container::VariableArray<PhysicsOverlapHit>& hits)
    {
        assert(hits.size() == 1);
        const PhysicsOverlapHit& hit = hits[0];
        assert(hit.Collider == TestCollider);
        assert(hit.Body == TestBody);
        assert(hit.Entity == TestEntity);
        assert(hit.bHasEntity);
        assert(hit.Contact.Normal.x == 1.0f);
        assert(hit.Contact.Depth == 23.0f);
        assert(hit.Contact.Point.z == 37.0f);
    }

    void TestHandlesHaveStableValueSemantics()
    {
        const ColliderHandle invalidCollider;
        const BodyHandle invalidBody;
        assert(!invalidCollider.IsValid());
        assert(!invalidBody.IsValid());
        assert(ColliderHandle::InvalidIndex == UINT32_MAX);
        assert(BodyHandle::InvalidIndex == UINT32_MAX);
        assert(TestCollider.IsValid());
        assert(TestBody.IsValid());
        assert((TestCollider == ColliderHandle{7, 3}));
        assert((TestBody == BodyHandle{11, 5}));
        assert((ColliderHandle{1, 1} < ColliderHandle{2, 1}));
        assert((BodyHandle{2, 1} < BodyHandle{2, 2}));
    }

    void TestUnboundQueriesDefaultOutputs()
    {
        SceneQuery sceneQuery;
        FakePhysicsSceneQueryProvider foreignProvider;
        const Math::Ray ray(Math::Vector3(1.0f, 2.0f, 3.0f), Math::Vector3(0.0f, 1.0f, 0.0f));
        const Math::Sphere sphere(Math::Vector3(5.0f, 7.0f, 11.0f), 13.0f);
        const Math::OBB box;
        const Math::Capsule capsule(Math::Vector3(17.0f, 19.0f, 23.0f), Math::Vector3(29.0f, 31.0f, 37.0f), 41.0f);
        PhysicsRaycastHit raycastHit;
        Container::VariableArray<PhysicsOverlapHit> overlapHits;
        bool bAlive = true;
        uint64_t publishedSnapshotSequence = 181;

        assert(sceneQuery.GetPublishedSnapshotSequence(publishedSnapshotSequence)
            == EPhysicsSceneQueryResult::Unavailable);
        assert(publishedSnapshotSequence == 0);

        SetSentinel(raycastHit);
        assert(sceneQuery.Raycast(ray, 43.0f, raycastHit) == EPhysicsSceneQueryResult::Unavailable);
        assert(IsDefaultHit(raycastHit));

        SetSentinel(overlapHits);
        assert(sceneQuery.OverlapSphere(sphere, overlapHits) == EPhysicsSceneQueryResult::Unavailable);
        assert(overlapHits.empty());
        SetSentinel(overlapHits);
        assert(sceneQuery.OverlapBox(box, overlapHits) == EPhysicsSceneQueryResult::Unavailable);
        assert(overlapHits.empty());
        SetSentinel(overlapHits);
        assert(sceneQuery.OverlapCapsule(capsule, overlapHits) == EPhysicsSceneQueryResult::Unavailable);
        assert(overlapHits.empty());

        assert(sceneQuery.IsAlive(TestCollider, bAlive) == EPhysicsSceneQueryResult::Unavailable);
        assert(!bAlive);
        bAlive = true;
        assert(sceneQuery.IsAlive(TestBody, bAlive) == EPhysicsSceneQueryResult::Unavailable);
        assert(!bAlive);
        assert(sceneQuery.UnbindPhysicsProvider(foreignProvider) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestBindingDelegatesPhysicsQueriesAndPreservesBinding()
    {
        SceneQuery sceneQuery;
        FakePhysicsSceneQueryProvider provider;
        FakePhysicsSceneQueryProvider foreignProvider;
        provider.OverlapOutput.push_back(CreateOverlapHit());

        assert(sceneQuery.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        assert(sceneQuery.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::AlreadyBound);
        assert(sceneQuery.BindPhysicsProvider(foreignProvider) == EPhysicsSceneQueryResult::AlreadyBound);
        assert(sceneQuery.UnbindPhysicsProvider(foreignProvider) == EPhysicsSceneQueryResult::ProviderMismatch);

        const Math::Ray ray(Math::Vector3(2.0f, 3.0f, 5.0f), Math::Vector3(7.0f, 11.0f, 13.0f));
        const Math::Sphere sphere(Math::Vector3(17.0f, 19.0f, 23.0f), 29.0f);
        const Math::OBB box(
            Math::Vector3(31.0f, 37.0f, 41.0f),
            Math::Vector3(43.0f, 47.0f, 53.0f),
            Math::Vector3(1.0f, 0.0f, 0.0f),
            Math::Vector3(0.0f, 1.0f, 0.0f),
            Math::Vector3(0.0f, 0.0f, 1.0f));
        const Math::Capsule capsule(Math::Vector3(59.0f, 61.0f, 67.0f), Math::Vector3(71.0f, 73.0f, 79.0f), 83.0f);
        PhysicsRaycastHit raycastHit;
        Container::VariableArray<PhysicsOverlapHit> overlapHits;
        bool bAlive = false;
        uint64_t publishedSnapshotSequence = 0;

        assert(sceneQuery.GetPublishedSnapshotSequence(publishedSnapshotSequence)
            == EPhysicsSceneQueryResult::Success);
        assert(provider.PublishedSnapshotSequenceCallCount == 1);
        assert(publishedSnapshotSequence == 173);

        assert(sceneQuery.Raycast(ray, 89.0f, raycastHit) == EPhysicsSceneQueryResult::Success);
        assert(provider.RaycastCallCount == 1);
        assert(provider.LastRay.Origin.x == 2.0f);
        assert(provider.LastRay.Direction.z == 13.0f);
        assert(provider.LastMaxDistance == 89.0f);
        assert(raycastHit.Collider == TestCollider);
        assert(raycastHit.Body == TestBody);
        assert(raycastHit.Entity == TestEntity);
        assert(raycastHit.bHasEntity);
        assert(raycastHit.Point.y == 5.0f);
        assert(raycastHit.Normal.y == 1.0f);
        assert(raycastHit.Distance == 19.0f);

        assert(sceneQuery.OverlapSphere(sphere, overlapHits) == EPhysicsSceneQueryResult::Success);
        assert(provider.SphereCallCount == 1);
        assert(provider.LastSphere.Center.z == 23.0f);
        assert(provider.LastSphere.Radius == 29.0f);
        AssertSingleOverlapHit(overlapHits);

        assert(sceneQuery.OverlapBox(box, overlapHits) == EPhysicsSceneQueryResult::Success);
        assert(provider.BoxCallCount == 1);
        assert(provider.LastBox.Center.x == 31.0f);
        assert(provider.LastBox.HalfExtents.y == 47.0f);
        assert(provider.LastBox.Axes[2].z == 1.0f);
        AssertSingleOverlapHit(overlapHits);

        assert(sceneQuery.OverlapCapsule(capsule, overlapHits) == EPhysicsSceneQueryResult::Success);
        assert(provider.CapsuleCallCount == 1);
        assert(provider.LastCapsule.PointA.x == 59.0f);
        assert(provider.LastCapsule.PointB.z == 79.0f);
        assert(provider.LastCapsule.Radius == 83.0f);
        AssertSingleOverlapHit(overlapHits);

        assert(sceneQuery.IsAlive(TestCollider, bAlive) == EPhysicsSceneQueryResult::Success);
        assert(provider.ColliderAliveCallCount == 1);
        assert(provider.LastCollider == TestCollider);
        assert(bAlive);
        bAlive = false;
        assert(sceneQuery.IsAlive(TestBody, bAlive) == EPhysicsSceneQueryResult::Success);
        assert(provider.BodyAliveCallCount == 1);
        assert(provider.LastBody == TestBody);
        assert(bAlive);

        sceneQuery.Clear();
        assert(sceneQuery.Raycast(ray, 97.0f, raycastHit) == EPhysicsSceneQueryResult::Success);
        Container::VariableArray<Entity*> entities;
        sceneQuery.Rebuild(Container::Span<Entity* const>(entities.data(), entities.size()));
        assert(sceneQuery.Raycast(ray, 101.0f, raycastHit) == EPhysicsSceneQueryResult::Success);
        World world;
        world.Initialize();
        sceneQuery.Rebuild(world);
        assert(sceneQuery.Raycast(ray, 103.0f, raycastHit) == EPhysicsSceneQueryResult::Success);
        world.Finalize();
        assert(provider.RaycastCallCount == 4);

        assert(sceneQuery.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        assert(sceneQuery.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Unavailable);
    }

    void TestNonSuccessProviderResultsClearEveryOutputFamily()
    {
        const EPhysicsSceneQueryResult nonSuccessResults[] = {
            EPhysicsSceneQueryResult::NoHit,
            EPhysicsSceneQueryResult::Unavailable,
            EPhysicsSceneQueryResult::NotReady,
            EPhysicsSceneQueryResult::InvalidArgument,
            EPhysicsSceneQueryResult::WrongThread,
            EPhysicsSceneQueryResult::AlreadyBound,
            EPhysicsSceneQueryResult::ProviderMismatch};
        SceneQuery sceneQuery;
        FakePhysicsSceneQueryProvider provider;
        provider.OverlapOutput.push_back(CreateOverlapHit());
        assert(sceneQuery.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);

        const Math::Ray ray(Math::Vector3(107.0f, 109.0f, 113.0f), Math::Vector3(1.0f, 0.0f, 0.0f));
        const Math::Sphere sphere(Math::Vector3(127.0f, 131.0f, 137.0f), 139.0f);
        const Math::OBB box;
        const Math::Capsule capsule(Math::Vector3(), Math::Vector3(149.0f, 151.0f, 157.0f), 163.0f);
        PhysicsRaycastHit raycastHit;
        Container::VariableArray<PhysicsOverlapHit> overlapHits;
        bool bAlive = true;
        uint64_t publishedSnapshotSequence = 191;

        for (EPhysicsSceneQueryResult result : nonSuccessResults)
        {
            provider.Result = result;

            publishedSnapshotSequence = 191;
            assert(sceneQuery.GetPublishedSnapshotSequence(publishedSnapshotSequence) == result);
            assert(publishedSnapshotSequence == 0);

            SetSentinel(raycastHit);
            assert(sceneQuery.Raycast(ray, 167.0f, raycastHit) == result);
            assert(IsDefaultHit(raycastHit));

            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapSphere(sphere, overlapHits) == result);
            assert(overlapHits.empty());
            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapBox(box, overlapHits) == result);
            assert(overlapHits.empty());
            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapCapsule(capsule, overlapHits) == result);
            assert(overlapHits.empty());

            bAlive = true;
            assert(sceneQuery.IsAlive(TestCollider, bAlive) == result);
            assert(!bAlive);
            bAlive = true;
            assert(sceneQuery.IsAlive(TestBody, bAlive) == result);
            assert(!bAlive);
        }

        assert(provider.RaycastCallCount == 7);
        assert(provider.SphereCallCount == 7);
        assert(provider.BoxCallCount == 7);
        assert(provider.CapsuleCallCount == 7);
        assert(provider.ColliderAliveCallCount == 7);
        assert(provider.BodyAliveCallCount == 7);
        assert(provider.PublishedSnapshotSequenceCallCount == 7);
    }

    void TestWrongThreadDoesNotCallProvider()
    {
        SceneQuery sceneQuery;
        FakePhysicsSceneQueryProvider provider;

        Thread::Thread worker([&sceneQuery, &provider]()
        {
            const Math::Ray ray(Math::Vector3(), Math::Vector3(1.0f, 0.0f, 0.0f));
            const Math::Sphere sphere(Math::Vector3(), 1.0f);
            const Math::OBB box;
            const Math::Capsule capsule(Math::Vector3(), Math::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
            PhysicsRaycastHit raycastHit;
            Container::VariableArray<PhysicsOverlapHit> overlapHits;
            bool bAlive = true;
            uint64_t publishedSnapshotSequence = 193;

            SetSentinel(raycastHit);
            assert(sceneQuery.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::WrongThread);
            assert(sceneQuery.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::WrongThread);
            assert(sceneQuery.Raycast(ray, 1.0f, raycastHit) == EPhysicsSceneQueryResult::WrongThread);
            assert(IsDefaultHit(raycastHit));
            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapSphere(sphere, overlapHits) == EPhysicsSceneQueryResult::WrongThread);
            assert(overlapHits.empty());
            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapBox(box, overlapHits) == EPhysicsSceneQueryResult::WrongThread);
            assert(overlapHits.empty());
            SetSentinel(overlapHits);
            assert(sceneQuery.OverlapCapsule(capsule, overlapHits) == EPhysicsSceneQueryResult::WrongThread);
            assert(overlapHits.empty());
            assert(sceneQuery.IsAlive(TestCollider, bAlive) == EPhysicsSceneQueryResult::WrongThread);
            assert(!bAlive);
            bAlive = true;
            assert(sceneQuery.IsAlive(TestBody, bAlive) == EPhysicsSceneQueryResult::WrongThread);
            assert(!bAlive);
            assert(sceneQuery.GetPublishedSnapshotSequence(publishedSnapshotSequence)
                == EPhysicsSceneQueryResult::WrongThread);
            assert(publishedSnapshotSequence == 0);
        });
        worker.Join();

        assert(provider.RaycastCallCount == 0);
        assert(provider.SphereCallCount == 0);
        assert(provider.BoxCallCount == 0);
        assert(provider.CapsuleCallCount == 0);
        assert(provider.ColliderAliveCallCount == 0);
        assert(provider.BodyAliveCallCount == 0);
        assert(provider.PublishedSnapshotSequenceCallCount == 0);
    }
}

int main()
{
    ConfigureFailureReporting();

    std::cout << "SceneQueryPhysicsFacadeTest start\n";

    TestHandlesHaveStableValueSemantics();
    TestUnboundQueriesDefaultOutputs();
    TestBindingDelegatesPhysicsQueriesAndPreservesBinding();
    TestNonSuccessProviderResultsClearEveryOutputFamily();
    TestWrongThreadDoesNotCallProvider();

    std::cout << "SceneQueryPhysicsFacadeTest passed\n";
    return 0;
}
