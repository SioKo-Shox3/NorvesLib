#include "Camera/SceneCameraProbe.h"
#include "Scene/SceneQuery.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Camera;
using namespace NorvesLib::Core::Scene;
namespace
{
    class Provider final : public IPhysicsSceneQueryProvider
    {
      public:
        EPhysicsSceneQueryResult Result = EPhysicsSceneQueryResult::Success;
        bool bEmpty = false, bThrow = false;
        PhysicsQueryHit Hit;
        mutable PhysicsQueryDesc Last;
        EPhysicsSceneQueryResult ExecuteQuery(const PhysicsQueryDesc& q,
                                              Container::VariableArray<PhysicsQueryHit>& out) const override
        {
            Last = q;
            out.clear();
            if (bThrow)
                throw std::runtime_error("provider");
            if (!bEmpty)
                out.push_back(Hit);
            return Result;
        }
        EPhysicsSceneQueryResult Raycast(const Math::Ray&, float, PhysicsRaycastHit&) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        EPhysicsSceneQueryResult OverlapSphere(const Math::Sphere&,
                                               Container::VariableArray<PhysicsOverlapHit>&) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        EPhysicsSceneQueryResult OverlapBox(const Math::OBB&,
                                            Container::VariableArray<PhysicsOverlapHit>&) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        EPhysicsSceneQueryResult OverlapCapsule(const Math::Capsule&,
                                                Container::VariableArray<PhysicsOverlapHit>&) const override
        {
            return EPhysicsSceneQueryResult::NoHit;
        }
        EPhysicsSceneQueryResult IsAlive(ColliderHandle, bool&) const override
        {
            return EPhysicsSceneQueryResult::Success;
        }
        EPhysicsSceneQueryResult IsAlive(BodyHandle, bool&) const override
        {
            return EPhysicsSceneQueryResult::Success;
        }
        EPhysicsSceneQueryResult GetPublishedSnapshotSequence(uint64_t&) const override
        {
            return EPhysicsSceneQueryResult::Success;
        }
    };
} // namespace
int main()
{
    SceneQuery query;
    Provider provider;
    assert(query.BindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
    PhysicsQueryFilter filter;
    filter.IgnoreBodies[0] = {2, 3};
    filter.IgnoreColliders[0] = {4, 5};
    filter.LayerMask = 7;
    SceneCameraProbe probe(query, filter);
    CameraProbeRequest request;
    request.Origin = {1, 2, 3};
    request.Direction = {0, 0, 1};
    request.Distance = 5;
    request.Radius = .2f;
    provider.Hit.Distance = 2;
    CameraProbeHit hit;
    assert(probe.SweepSphere(request, hit) == CameraProbeResult::Hit && hit.Distance == 2);
    assert(provider.Last.Kind == EPhysicsQueryKind::SweepSphere && provider.Last.MaxHits == 1 &&
           provider.Last.bReportStartOverlap);
    assert(provider.Last.Sphere.Center == request.Origin && provider.Last.Sphere.Radius == request.Radius);
    assert(provider.Last.Direction == request.Direction && provider.Last.MaxDistance == 5);
    assert(provider.Last.Filter.TriggerPolicy == EPhysicsQueryTriggerPolicy::Exclude &&
           provider.Last.Filter.LayerMask == 7);
    assert(provider.Last.Filter.IgnoreBodies[0] == filter.IgnoreBodies[0] &&
           provider.Last.Filter.IgnoreColliders[0] == filter.IgnoreColliders[0]);
    hit.Distance = 123;
    provider.Result = EPhysicsSceneQueryResult::NoHit;
    assert(probe.SweepSphere(request, hit) == CameraProbeResult::Clear && hit.Distance == 123);
    for (auto status : {EPhysicsSceneQueryResult::Unavailable, EPhysicsSceneQueryResult::NotReady,
                        EPhysicsSceneQueryResult::WrongThread, EPhysicsSceneQueryResult::IterationLimit})
    {
        provider.Result = status;
        assert(probe.SweepSphere(request, hit) == CameraProbeResult::Failed && hit.Distance == 123);
        assert(probe.GetLastQueryResult() == status);
    }
    provider.Result = EPhysicsSceneQueryResult::Success;
    provider.bEmpty = true;
    assert(probe.SweepSphere(request, hit) == CameraProbeResult::Failed && hit.Distance == 123);
    provider.bEmpty = false;
    provider.Hit.Distance = std::numeric_limits<float>::quiet_NaN();
    assert(probe.SweepSphere(request, hit) == CameraProbeResult::Failed && hit.Distance == 123);
    provider.Hit.Distance = 0;
    provider.Hit.bStartPenetrating = true;
    assert(probe.SweepSphere(request, hit) == CameraProbeResult::Hit && hit.bStartPenetrating);
    provider.bThrow = true;
    hit.Distance = 123;
    bool threw = false;
    try
    {
        probe.SweepSphere(request, hit);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw && hit.Distance == 123);
    assert(query.UnbindPhysicsProvider(provider) == EPhysicsSceneQueryResult::Success);
    std::cout << "SceneCameraProbeTest PASS\n";
    return 0;
}
