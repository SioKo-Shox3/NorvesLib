#include "Physics/PhysicsBroadphase.h"
#include "Math/GeometryIntersection.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace NorvesLib::Modules::Physics;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Math;

namespace
{
    bool Near(float a, float b)
    {
        return std::fabs(a - b) <= 1e-3f;
    }

    void CheckDefault(const PhysicsQueryHit& hit)
    {
        assert(!hit.Collider.IsValid() && !hit.Body.IsValid() && !hit.Entity.IsValid());
        assert(!hit.bHasEntity && hit.UserData == 0 && hit.Distance == 0.0f && hit.Depth == 0.0f && !hit.bStartPenetrating);
        assert(hit.Point == Vector3(0.0f) && hit.Normal == Vector3(0.0f));
    }

    PhysicsShapeProxy MakeProxy(EPhysicsProxyShape shape)
    {
        PhysicsShapeProxy proxy;
        proxy.Shape = shape;
        proxy.Collider = {4, 2};
        proxy.Body = {7, 1};
        proxy.Layer = 4;
        proxy.UserData = 0x123456789abcdef0ull;
        proxy.Sphere = Sphere(Vector3(5.0f, 0.0f, 0.0f), 0.5f);
        proxy.Box.Center = Vector3(5.0f, 0.0f, 0.0f);
        proxy.Box.HalfExtents = Vector3(0.5f);
        proxy.Capsule = Capsule(Vector3(5.0f, -0.5f, 0.0f), Vector3(5.0f, 0.5f, 0.0f), 0.5f);
        return proxy;
    }
}

int main()
{
    PhysicsQueryDesc query;
    query.Ray = Ray(Vector3(0.0f), Vector3(7.0f, 0.0f, 0.0f));
    query.MaxDistance = 10.0f;
    PhysicsQueryHit hit;
    for (const auto shape : {EPhysicsProxyShape::Sphere, EPhysicsProxyShape::Box, EPhysicsProxyShape::Capsule})
    {
        const auto proxy = MakeProxy(shape);
        query.Kind = EPhysicsQueryKind::RaycastClosest;
        assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
        assert(Near(hit.Distance, 4.5f) && Near(hit.Normal.x, -1.0f));
        assert(hit.Collider == proxy.Collider && hit.Body == proxy.Body && hit.UserData == proxy.UserData && !hit.bHasEntity);
        query.Kind = EPhysicsQueryKind::RaycastAll;
        assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success && Near(hit.Distance, 4.5f));
        query.Kind = EPhysicsQueryKind::SweepSphere;
        query.Sphere = Sphere(Vector3(0.0f), 0.5f);
        query.Direction = Vector3(13.0f, 0.0f, 0.0f);
        assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
        assert(Near(hit.Distance, 4.0f) && Near(hit.Normal.x, -1.0f) && !hit.bStartPenetrating);
        query.Kind = EPhysicsQueryKind::SweepCapsule;
        query.Capsule = Capsule(Vector3(0.0f, -1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.5f);
        assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success && Near(hit.Distance, 4.0f));
        query.MaxSweepIterations = 1;
        assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::IterationLimit);
        CheckDefault(hit);
        query.MaxSweepIterations = 24;
    }
    std::cout << "ray_sweep_three_shapes_and_userdata passed\n";

    auto proxy = MakeProxy(EPhysicsProxyShape::Box);
    query.Kind = EPhysicsQueryKind::OverlapSphere;
    query.Sphere = Sphere(Vector3(4.25f, 0.0f, 0.0f), 0.5f);
    GeometryContact contact;
    assert(ComputeContact(query.Sphere, proxy.Box, contact));
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    assert(Near(hit.Normal.x, -contact.Normal.x) && Near(hit.Depth, contact.Depth));
    query.Kind = EPhysicsQueryKind::OverlapBox;
    query.Box = proxy.Box;
    query.Box.Center.x = 4.25f;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success && Near(hit.Normal.x, -1.0f));
    query.Kind = EPhysicsQueryKind::OverlapCapsule;
    query.Capsule = Capsule(Vector3(4.25f, -1.0f, 0.0f), Vector3(4.25f, 1.0f, 0.0f), 0.5f);
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success && Near(hit.Normal.x, -1.0f));
    std::cout << "overlap_outward_normal passed\n";

    query.Kind = EPhysicsQueryKind::RaycastClosest;
    query.Filter.LayerMask = 1;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::NoHit);
    CheckDefault(hit);
    query.Filter.LayerMask = 4;
    query.Filter.IgnoreColliders[0] = proxy.Collider;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::NoHit);
    query.Filter.IgnoreColliders[0].Generation += 1;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    proxy.bTrigger = true;
    query.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Exclude;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::NoHit);
    CheckDefault(hit);
    query.Filter.TriggerPolicy = EPhysicsQueryTriggerPolicy::Only;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    query.Filter.IgnoreBodies[0] = proxy.Body;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::NoHit);
    CheckDefault(hit);
    query.Filter = {};
    std::cout << "filters_generation_triggers passed\n";

    query.Ray.Origin = proxy.Box.Center;
    query.MaxDistance = 0.0f;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    assert(hit.Distance == 0.0f && hit.Normal == Vector3(0.0f));
    query.Kind = EPhysicsQueryKind::SweepSphere;
    query.Sphere = Sphere(proxy.Box.Center, 0.5f);
    query.Direction = Vector3(0.0f);
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    assert(hit.bStartPenetrating && hit.Distance == 0.0f && hit.Depth > 0.0f);
    query.bReportStartOverlap = false;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::NoHit);
    CheckDefault(hit);
    query.MaxHits = 0;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    CheckDefault(hit);
    query.MaxHits = 1;
    query.Kind = static_cast<EPhysicsQueryKind>(255);
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    query.Kind = EPhysicsQueryKind::RaycastClosest;
    query.Ray.Direction = Vector3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    query.Ray.Direction = Vector3(1.0f, 0.0f, 0.0f);
    proxy.Shape = static_cast<EPhysicsProxyShape>(255);
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    CheckDefault(hit);
    proxy = MakeProxy(EPhysicsProxyShape::Sphere);
    proxy.Sphere = Sphere(Vector3(1e30f, 0.0f, 0.0f), 1e25f);
    query = {};
    query.Ray = Ray(Vector3(0.0f), Vector3(1.0f, 0.0f, 0.0f));
    query.MaxDistance = 100.0f;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    CheckDefault(hit);
    query.Kind = EPhysicsQueryKind::OverlapSphere;
    query.Sphere = Sphere(Vector3(0.0f), 1e30f);
    proxy.Sphere.Radius = 1e30f;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::InvalidArgument);
    CheckDefault(hit);
    // 共通の大きい平行移動自体は相対尺度の制限にしない。
    proxy = MakeProxy(EPhysicsProxyShape::Sphere);
    proxy.Sphere.Center = Vector3(1e7f + 5.0f, 0.0f, 0.0f);
    proxy.Sphere.Radius = 1.0f;
    query = {};
    query.Ray = Ray(Vector3(1e7f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f));
    query.MaxDistance = 100.0f;
    assert(PhysicsBroadphase::QueryProxy(proxy, query, hit) == EPhysicsSceneQueryResult::Success);
    assert(Near(hit.Distance, 4.0f));
    std::cout << "initial_zero_error_clearing passed\nPhysicsProxyQueryTest passed\n";
    return 0;
}
