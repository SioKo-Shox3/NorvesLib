#include "Physics/PhysicsBroadphase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib;
using namespace NorvesLib::Modules::Physics;
using namespace NorvesLib::Core::Scene;
using Result = EPhysicsSceneQueryResult;
namespace
{
    uint32_t Seed = 0x97452;
    uint32_t Next() { Seed = Seed*1664525u+1013904223u; return Seed; }
    float Random(float low,float high) { return low+(high-low)*static_cast<float>(Next()>>8)/16777216.f; }
    Math::Vector3 Position() { return Math::Vector3(Random(-10,10),Random(-10,10),Random(-10,10)); }
    void SameRay(const PhysicsRaycastHit& a,const PhysicsQueryHit& b)
    {
        assert(a.Collider == b.Collider && a.Body == b.Body && a.Entity == b.Entity && a.bHasEntity == b.bHasEntity);
        assert(a.UserData == b.UserData && a.Distance == b.Distance && a.Point == b.Point && a.Normal == b.Normal);
    }
    void SameOverlap(const PhysicsOverlapHit& a,const PhysicsQueryHit& b)
    {
        assert(a.Collider == b.Collider && a.Body == b.Body && a.Entity == b.Entity && a.bHasEntity == b.bHasEntity);
        assert(a.UserData == b.UserData && a.Contact.Point == b.Point && a.Contact.Depth == b.Depth);
        assert(a.Contact.Normal == b.Normal*-1.0f);
    }
}
int main()
{
    Seed = 0x97452;
    size_t successful[4]{};
    for (int scene = 0; scene < 1000; ++scene)
    {
        PhysicsShapeProxy proxies[16];
        for (uint32_t index = 0; index < 16; ++index)
        {
            auto& p = proxies[index];
            p.Collider = {16-index,1+Next()%3};
            p.Body = {index,1};
            p.Entity = {index,2};
            p.bHasEntity = index%2 == 0;
            p.UserData = 100+index;
            p.bTrigger = Next()%3 == 0;
            p.Shape = static_cast<EPhysicsProxyShape>(Next()%3);
            const auto position = Position();
            p.Sphere = Math::Sphere(position,Random(.1f,2));
            const float angle = Random(-3,3), c = std::cos(angle), s = std::sin(angle);
            p.Box = Math::OBB(position,Math::Vector3(Random(.1f,2),Random(.1f,2),Random(.1f,2)),
                Math::Vector3(c,0,s),Math::Vector3::UnitY,Math::Vector3(-s,0,c));
            p.Capsule = Math::Capsule(position-Math::Vector3(.3f,1,.5f),position+Math::Vector3(.3f,1,.5f),Random(.1f,2));
        }
        for (int family = 0; family < 4; ++family)
        {
            const auto center = scene%2 == 0 ? proxies[0].Sphere.Center : Position();
            PhysicsQueryDesc query;
            query.Ray = Math::Ray(center-Math::Vector3(5,0,0),Math::Vector3(3,0,0));
            query.MaxDistance = 30;
            query.Sphere = Math::Sphere(center,1);
            query.Box = Math::OBB(center,Math::Vector3(1),Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
            query.Capsule = Math::Capsule(center-Math::Vector3(0,1,0),center+Math::Vector3(0,1,0),1);
            query.Kind = family == 0 ? EPhysicsQueryKind::RaycastClosest : static_cast<EPhysicsQueryKind>(family+1);
            PhysicsQueryHit modern[64], scratch[64];
            PhysicsOverlapHit legacy[64];
            size_t count = 0, legacyCount = 99;
            const auto result = PhysicsBroadphase::ExecuteQueryOverProxies(proxies,query,modern,count);
            if (family == 0)
            {
                PhysicsRaycastHit ray;
                ray.UserData = UINT64_MAX;
                const auto legacyResult = PhysicsBroadphase::RaycastOverProxies(proxies,query.Ray,query.MaxDistance,ray);
                assert(legacyResult == result);
                if (result == Result::Success)
                {
                    assert(count == 1);
                    SameRay(ray,modern[0]);
                    ++successful[family];
                }
                else
                {
                    assert(!ray.Collider.IsValid() && ray.UserData == 0 && ray.Distance == 0);
                }
            }
            else
            {
                const auto legacyResult = PhysicsBroadphase::OverlapOverProxies(proxies,query,scratch,legacy,legacyCount);
                assert(legacyResult == result && legacyCount == count);
                for (size_t index = 0; index < count; ++index)
                {
                    SameOverlap(legacy[index],modern[index]);
                }
                if (result == Result::Success)
                {
                    ++successful[family];
                }
            }
        }
    }
    for (size_t family = 0; family < 4; ++family)
    {
        assert(successful[family] >= 500);
        std::cout << "family=" << family << " matched-hit cases=" << successful[family] << "\n";
    }
    PhysicsShapeProxy ties[2];
    ties[0].Collider = {9,2}; ties[1].Collider = {1,3};
    ties[0].Sphere = ties[1].Sphere = Math::Sphere(Math::Vector3(5,0,0),1);
    PhysicsRaycastHit ray;
    assert(PhysicsBroadphase::RaycastOverProxies(ties,Math::Ray(Math::Vector3(),Math::Vector3::UnitX),10,ray) == Result::Success);
    assert(ray.Collider == ties[0].Collider && ray.Distance == 4);
    PhysicsQueryDesc query;
    query.Kind = EPhysicsQueryKind::OverlapSphere;
    query.Sphere = Math::Sphere(Math::Vector3(4.5f,0,0),1);
    PhysicsQueryHit scratch[2];
    PhysicsOverlapHit overlap[2];
    size_t count = 99;
    assert(PhysicsBroadphase::OverlapOverProxies(ties,query,scratch,overlap,count) == Result::Success);
    assert(count == 2 && overlap[0].Collider == ties[1].Collider && overlap[1].Collider == ties[0].Collider);
    assert(overlap[0].Contact.Normal.x > .99f && std::fabs(overlap[0].Contact.Depth-1.5f) < 1e-5f);
    assert(PhysicsBroadphase::OverlapOverProxies(ties,query,{scratch,1},overlap,count) == Result::InvalidArgument && count == 0);
    assert(!overlap[0].Collider.IsValid() && !overlap[1].Collider.IsValid());
    assert(PhysicsBroadphase::OverlapOverProxies(ties,query,scratch,{overlap,1},count) == Result::InvalidArgument && count == 0);
    assert(PhysicsBroadphase::OverlapOverProxies(ties,query,scratch,Core::Container::Span<PhysicsOverlapHit>(nullptr,1),count) == Result::InvalidArgument && count == 0);
    query.Kind = EPhysicsQueryKind::RaycastClosest;
    assert(PhysicsBroadphase::OverlapOverProxies(ties,query,scratch,overlap,count) == Result::InvalidArgument && count == 0);
    assert(PhysicsBroadphase::RaycastOverProxies(ties,Math::Ray(Math::Vector3(),Math::Vector3()),10,ray) == Result::InvalidArgument);
    assert(!ray.Collider.IsValid() && ray.UserData == 0);
    // 旧4入口は無filter検索。所属Layerが0でも従来どおり返す。
    PhysicsShapeProxy unclassified;
    unclassified.Collider = {77,1};
    unclassified.Layer = 0;
    unclassified.Sphere = Math::Sphere(Math::Vector3(),1);
    const Math::Ray unfilteredRay(Math::Vector3(-5,0,0),Math::Vector3::UnitX);
    assert(PhysicsBroadphase::RaycastOverProxies({&unclassified,1},unfilteredRay,10,ray) == Result::Success);
    assert(ray.Collider == unclassified.Collider && ray.Distance == 4);
    PhysicsQueryHit modern[2];
    query = {};
    query.Ray = unfilteredRay;
    query.MaxDistance = 10;
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({&unclassified,1},query,modern,count) == Result::NoHit);
    for (auto kind : {EPhysicsQueryKind::OverlapSphere,EPhysicsQueryKind::OverlapBox,EPhysicsQueryKind::OverlapCapsule})
    {
        query.Kind = kind;
        query.Sphere = Math::Sphere(Math::Vector3(),.5f);
        query.Box = Math::OBB(Math::Vector3(),Math::Vector3(.5f),Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
        query.Capsule = Math::Capsule(Math::Vector3(0,-.5f,0),Math::Vector3(0,.5f,0),.5f);
        assert(PhysicsBroadphase::OverlapOverProxies({&unclassified,1},query,scratch,overlap,count) == Result::Success);
        assert(count == 1 && overlap[0].Collider == unclassified.Collider);
        assert(PhysicsBroadphase::ExecuteQueryOverProxies({&unclassified,1},query,modern,count) == Result::NoHit);
    }
    std::cout << "PhysicsLegacyQueryTest PASS: 4000 fixed-seed comparisons\n";
    return 0;
}
