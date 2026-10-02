#include "Scene/SceneQuery.h"
#include "Object/World.h"
#include "Thread/Thread.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
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

    class FakePhysicsSceneQueryProvider : public IPhysicsSceneQueryProvider
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

    class UnifiedQueryProvider final : public FakePhysicsSceneQueryProvider
    {
    public:
        mutable PhysicsQueryDesc LastQuery;
        mutable uint32_t Calls = 0;
        bool bThrow = false;
        EPhysicsSceneQueryResult ExecuteQuery(const PhysicsQueryDesc& query,
            Container::VariableArray<PhysicsQueryHit>& outHits) const override
        {
            ++Calls;
            LastQuery = query;
            PhysicsQueryHit hit;
            hit.Collider = TestCollider;
            hit.UserData = UINT64_MAX;
            outHits.push_back(hit);
            if (bThrow)
            {
                throw std::runtime_error("provider検証例外");
            }
            return Result;
        }
    };

    void TestUnifiedQueryFacade()
    {
        SceneQuery scene;
        FakePhysicsSceneQueryProvider legacy;
        UnifiedQueryProvider provider;
        PhysicsQueryDesc query;
        query.Kind = EPhysicsQueryKind::SweepCapsule;
        query.Ray = Math::Ray(Math::Vector3(7,8,9),Math::Vector3(3,2,1));
        query.Sphere = Math::Sphere(Math::Vector3(9,8,7),2);
        query.Box = Math::OBB(Math::Vector3(6,5,4),Math::Vector3(1,2,3),
            Math::Vector3::UnitZ,Math::Vector3::UnitY,-Math::Vector3::UnitX);
        query.Capsule = Math::Capsule(Math::Vector3(1,2,3),Math::Vector3(4,5,6),.5f);
        query.Direction = Math::Vector3(0,0,2);
        query.MaxDistance = 17;
        query.MaxHits = 3;
        query.MaxSweepIterations = 9;
        query.bReportStartOverlap = false;
        query.Filter.LayerMask = 0x80000002u;
        query.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Only;
        query.Filter.IgnoreColliders[2] = TestCollider;
        query.Filter.IgnoreBodies[3] = TestBody;
        Container::VariableArray<PhysicsQueryHit> hits;
        hits.emplace_back();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Unavailable && hits.empty());
        assert(scene.BindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        hits.emplace_back();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Unavailable && hits.empty());
        assert(legacy.RaycastCallCount == 0 && legacy.CapsuleCallCount == 0);
        assert(scene.UnbindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        assert(scene.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        hits.emplace_back();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Success);
        assert(provider.Calls == 1 && hits.size() == 1 && hits[0].UserData == UINT64_MAX);
        assert(provider.LastQuery.Kind == query.Kind);
        assert(provider.LastQuery.Ray.Origin == query.Ray.Origin && provider.LastQuery.Ray.Direction == query.Ray.Direction);
        assert(provider.LastQuery.Sphere.Center == query.Sphere.Center && provider.LastQuery.Sphere.Radius == query.Sphere.Radius);
        assert(provider.LastQuery.Box.Center == query.Box.Center && provider.LastQuery.Box.HalfExtents == query.Box.HalfExtents);
        for (int axis = 0; axis < 3; ++axis)
        {
            assert(provider.LastQuery.Box.Axes[axis] == query.Box.Axes[axis]);
        }
        assert(provider.LastQuery.Capsule.PointA == query.Capsule.PointA);
        assert(provider.LastQuery.Capsule.PointB == query.Capsule.PointB);
        assert(provider.LastQuery.Capsule.Radius == query.Capsule.Radius);
        assert(provider.LastQuery.Direction == query.Direction);
        assert(provider.LastQuery.MaxDistance == 17 && provider.LastQuery.MaxHits == 3);
        assert(provider.LastQuery.MaxSweepIterations == 9 && !provider.LastQuery.bReportStartOverlap);
        assert(provider.LastQuery.Filter.LayerMask == query.Filter.LayerMask);
        assert(provider.LastQuery.Filter.TriggerPolicy == EPhysicsQueryTriggerPolicy::Only);
        assert(provider.LastQuery.Filter.IgnoreColliders[2] == TestCollider);
        assert(provider.LastQuery.Filter.IgnoreBodies[3] == TestBody);
        for (auto result : {EPhysicsSceneQueryResult::NoHit,EPhysicsSceneQueryResult::Unavailable,
            EPhysicsSceneQueryResult::NotReady,EPhysicsSceneQueryResult::InvalidArgument,
            EPhysicsSceneQueryResult::WrongThread,EPhysicsSceneQueryResult::AlreadyBound,
            EPhysicsSceneQueryResult::ProviderMismatch,EPhysicsSceneQueryResult::IterationLimit})
        {
            provider.Result = result;
            hits.emplace_back();
            assert(scene.ExecuteQuery(query,hits) == result && hits.empty());
        }
        provider.Result = EPhysicsSceneQueryResult::Success;
        provider.bThrow = true;
        bool caught = false;
        try
        {
            (void)scene.ExecuteQuery(query,hits);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        assert(caught && hits.empty());
        provider.bThrow = false;
        const auto calls = provider.Calls;
        EPhysicsSceneQueryResult threadResult{};
        hits.emplace_back();
        Thread::Thread worker([&]() { threadResult = scene.ExecuteQuery(query,hits); });
        worker.Join();
        assert(threadResult == EPhysicsSceneQueryResult::WrongThread);
        assert(hits.empty() && provider.Calls == calls);
        assert(scene.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        hits.emplace_back();
        assert(scene.ExecuteQuery(query,hits) == EPhysicsSceneQueryResult::Unavailable && hits.empty());
        assert(provider.Calls == calls);
    }

    class BatchQueryProvider final : public FakePhysicsSceneQueryProvider
    {
    public:
        mutable uint32_t Calls = 0;
        mutable size_t LastCount = 0;
        mutable PhysicsQueryDesc First, Last;
        bool bThrow = false;
        EPhysicsSceneQueryResult ExecuteBatch(Container::Span<const PhysicsQueryDesc> queries,
            Container::VariableArray<PhysicsQueryHit>& outHits,
            Container::VariableArray<PhysicsQueryBatchResult>& outResults) const override
        {
            ++Calls;
            LastCount = queries.size();
            if (queries.size() != 0)
            {
                First = queries[0];
                Last = queries[queries.size()-1];
                PhysicsQueryHit hit;
                hit.UserData = 42;
                outHits.push_back(hit);
                for (size_t index = 0; index < queries.size(); ++index)
                {
                    outResults.push_back({index == 0 ? EPhysicsSceneQueryResult::Success : EPhysicsSceneQueryResult::NoHit,
                        index == 0 ? size_t{0} : size_t{1}, index == 0 ? size_t{1} : size_t{0}});
                }
            }
            if (bThrow)
            {
                throw std::runtime_error("batch検証例外");
            }
            return Result;
        }
    };

    void TestBatchQueryFacade()
    {
        SceneQuery scene;
        FakePhysicsSceneQueryProvider legacy;
        BatchQueryProvider provider;
        PhysicsQueryDesc queries[2];
        queries[0].Kind = EPhysicsQueryKind::RaycastAll;
        queries[0].Filter.LayerMask = 0x80000000u;
        queries[1].Kind = EPhysicsQueryKind::SweepCapsule;
        queries[1].MaxHits = 3;
        Container::VariableArray<PhysicsQueryHit> hits;
        Container::VariableArray<PhysicsQueryBatchResult> results;
        auto fill = [&]() { hits.emplace_back(); results.emplace_back(); };
        fill();
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::Unavailable);
        assert(hits.empty() && results.empty());
        assert(scene.BindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        fill();
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::Unavailable);
        assert(hits.empty() && results.empty());
        assert(scene.UnbindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        assert(scene.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        fill();
        assert(scene.ExecuteBatch(queries,hits,results) == EPhysicsSceneQueryResult::Success);
        assert(provider.Calls == 1 && provider.LastCount == 2);
        assert(provider.First.Kind == queries[0].Kind && provider.First.Filter.LayerMask == queries[0].Filter.LayerMask);
        assert(provider.Last.Kind == queries[1].Kind && provider.Last.MaxHits == 3);
        assert(provider.RaycastCallCount == 0 && provider.PublishedSnapshotSequenceCallCount == 0);
        assert(hits.size() == 1 && hits[0].UserData == 42 && results.size() == 2);
        assert(results[0].Result == EPhysicsSceneQueryResult::Success && results[0].FirstHit == 0 && results[0].HitCount == 1);
        assert(results[1].Result == EPhysicsSceneQueryResult::NoHit && results[1].FirstHit == 1 && results[1].HitCount == 0);
        for (auto failure : {EPhysicsSceneQueryResult::NoHit,EPhysicsSceneQueryResult::Unavailable,
            EPhysicsSceneQueryResult::NotReady,EPhysicsSceneQueryResult::InvalidArgument,
            EPhysicsSceneQueryResult::WrongThread,EPhysicsSceneQueryResult::AlreadyBound,
            EPhysicsSceneQueryResult::ProviderMismatch,EPhysicsSceneQueryResult::IterationLimit})
        {
            provider.Result = failure;
            fill();
            assert(scene.ExecuteBatch(queries,hits,results) == failure);
            assert(hits.empty() && results.empty());
        }
        provider.Result = EPhysicsSceneQueryResult::Success;
        provider.bThrow = true;
        bool caught = false;
        try
        {
            (void)scene.ExecuteBatch(queries,hits,results);
        }
        catch (const std::runtime_error&)
        {
            caught = true;
        }
        assert(caught && hits.empty() && results.empty());
        provider.bThrow = false;
        const auto calls = provider.Calls;
        fill();
        assert(scene.ExecuteBatch(Container::Span<const PhysicsQueryDesc>(nullptr,1),hits,results)
            == EPhysicsSceneQueryResult::InvalidArgument);
        assert(hits.empty() && results.empty() && provider.Calls == calls);
        fill();
        EPhysicsSceneQueryResult result{};
        Thread::Thread worker([&]() { result = scene.ExecuteBatch(queries,hits,results); });
        worker.Join();
        assert(result == EPhysicsSceneQueryResult::WrongThread);
        assert(hits.empty() && results.empty() && provider.Calls == calls);
        assert(scene.ExecuteBatch({},hits,results) == EPhysicsSceneQueryResult::Success);
        assert(hits.empty() && results.empty() && provider.LastCount == 0 && provider.Calls == calls+1);
        assert(scene.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
    }

    class RefreshQueryProvider final : public FakePhysicsSceneQueryProvider
    {
    public:
        uint32_t Calls = 0;
        EPhysicsSceneQueryResult RefreshDynamicSnapshot() override
        {
            ++Calls;
            return Result;
        }
    };
    void TestExplicitSnapshotRefreshFacade()
    {
        SceneQuery scene;
        FakePhysicsSceneQueryProvider legacy;
        RefreshQueryProvider provider;
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Unavailable);
        assert(scene.BindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        assert(scene.RefreshDynamicSnapshot() == EPhysicsSceneQueryResult::Unavailable);
        assert(scene.UnbindPhysicsProvider(legacy) == EPhysicsSceneQueryResult::Success);
        assert(scene.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
        for (auto result : {EPhysicsSceneQueryResult::Success,EPhysicsSceneQueryResult::NotReady,
            EPhysicsSceneQueryResult::Unavailable,EPhysicsSceneQueryResult::InvalidArgument})
        {
            provider.Result = result;
            const auto before = provider.Calls;
            assert(scene.RefreshDynamicSnapshot() == result && provider.Calls == before+1);
        }
        const auto before = provider.Calls;
        EPhysicsSceneQueryResult result{};
        Thread::Thread worker([&]() { result = scene.RefreshDynamicSnapshot(); });
        worker.Join();
        assert(result == EPhysicsSceneQueryResult::WrongThread && provider.Calls == before);
        assert(provider.PublishedSnapshotSequenceCallCount == 0);
        assert(scene.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
    }

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
    TestUnifiedQueryFacade();
    TestBatchQueryFacade();
    TestExplicitSnapshotRefreshFacade();

    std::cout << "SceneQueryPhysicsFacadeTest start\n";

    TestHandlesHaveStableValueSemantics();
    TestUnboundQueriesDefaultOutputs();
    TestBindingDelegatesPhysicsQueriesAndPreservesBinding();
    TestNonSuccessProviderResultsClearEveryOutputFamily();
    TestWrongThreadDoesNotCallProvider();

    std::cout << "SceneQueryPhysicsFacadeTest passed\n";
    return 0;
}
