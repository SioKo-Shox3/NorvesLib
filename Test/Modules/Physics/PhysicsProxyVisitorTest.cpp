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
    PhysicsShapeProxy Sphere(uint32_t index, float x, float y = 0)
    {
        PhysicsShapeProxy proxy;
        proxy.Collider = {index,1};
        proxy.Sphere = Math::Sphere(Math::Vector3(x,y,0),1);
        return proxy;
    }
    struct Visits
    {
        uint32_t Indices[64]{};
        size_t Count = 0, Checked = 0;
        uint32_t Reject = UINT32_MAX, Fail = UINT32_MAX;
        bool bFailPrecheck = false;
    };
    Result Visit(const PhysicsShapeProxy& proxy, void* context)
    {
        auto& visits = *static_cast<Visits*>(context);
        assert(visits.Count < 64);
        visits.Indices[visits.Count++] = proxy.Collider.Index;
        return proxy.Collider.Index == visits.Fail ? Result::IterationLimit :
            proxy.Collider.Index == visits.Reject ? Result::NoHit : Result::Success;
    }
    Result Precheck(const PhysicsShapeProxy& proxy, void* context)
    {
        auto& visits = *static_cast<Visits*>(context);
        ++visits.Checked;
        if (visits.bFailPrecheck && proxy.Collider.Index == visits.Fail)
        {
            return Result::InvalidArgument;
        }
        return proxy.Collider.Index == visits.Reject ? Result::NoHit : Result::Success;
    }
}
int main()
{
    PhysicsShapeProxy proxies[]{Sphere(8,100),Sphere(3,0),Sphere(5,2),Sphere(9,-100)};
    const Math::AABB bounds(Math::Vector3(-1,-1,-1),Math::Vector3(1,1,1));
    Visits visits;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::Success);
    assert(visits.Count == 2 && visits.Indices[0] == 3 && visits.Indices[1] == 5);
    visits = {};
    visits.Reject = 3;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::Success);
    assert(visits.Count == 2);
    visits = {};
    visits.Reject = 3;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits,Precheck) == Result::Success);
    assert(visits.Checked == 4 && visits.Count == 1 && visits.Indices[0] == 5);
    visits = {};
    visits.Fail = 8;
    visits.bFailPrecheck = true;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits,Precheck) == Result::InvalidArgument);
    assert(visits.Checked == 1 && visits.Count == 0);
    visits = {};
    visits.Fail = 3;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::IterationLimit);
    assert(visits.Count == 1);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(-5,0,0),Math::Vector3(2,0,0)),8,Visit,&visits) == Result::Success);
    assert(visits.Count == 2 && visits.Indices[0] == 3 && visits.Indices[1] == 5);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(-5,0,0),Math::Vector3::UnitX),3,Visit,&visits) == Result::Success);
    assert(visits.Count == 0);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(),Math::Vector3::UnitX),0,Visit,&visits) == Result::Success);
    assert(visits.Count == 1 && visits.Indices[0] == 3);
    for (float magnitude : {std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::max()})
    {
        visits = {};
        assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(-5,0,0),Math::Vector3(magnitude,0,0)),8,Visit,&visits) == Result::Success);
        assert(visits.Count == 2);
    }
    // 平行判定にepsilonを入れると長いrayの微小な軸変化を失う。
    PhysicsShapeProxy longRayProxy[]{Sphere(1,1e9f,10)};
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay(longRayProxy,Math::Ray(Math::Vector3(),Math::Vector3(1,1e-8f,0)),2e9f,Visit,&visits) == Result::Success);
    assert(visits.Count == 1);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay(longRayProxy,Math::Ray(Math::Vector3(),Math::Vector3::UnitX),2e9f,Visit,&visits) == Result::Success);
    assert(visits.Count == 0);
    // 形状やboundsが不正でも空間除外で黙殺せず、検証callbackへ渡す。
    proxies[0].Sphere.Radius = -1;
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::Success);
    assert(visits.Count == 3 && visits.Indices[0] == 8);
    proxies[0] = Sphere(8,100);
    proxies[0].Shape = static_cast<EPhysicsProxyShape>(99);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::Success);
    assert(visits.Count == 3 && visits.Indices[0] == 8);
    proxies[0] = Sphere(8,std::numeric_limits<float>::max());
    proxies[0].Sphere.Radius = std::numeric_limits<float>::max();
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,Visit,&visits) == Result::Success);
    assert(visits.Count == 3 && visits.Indices[0] == 8);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb({},bounds,Visit,&visits) == Result::Success && visits.Count == 0);
    assert(PhysicsBroadphase::VisitProxiesInAabb(Core::Container::Span<const PhysicsShapeProxy>(nullptr,1),bounds,Visit,&visits) == Result::InvalidArgument);
    assert(PhysicsBroadphase::VisitProxiesInAabb({},bounds,nullptr,&visits) == Result::InvalidArgument);
    auto badBounds = bounds;
    badBounds.Min.x = 5;
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,badBounds,Visit,&visits) == Result::InvalidArgument);
    badBounds.Min.x = std::numeric_limits<float>::quiet_NaN();
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,badBounds,Visit,&visits) == Result::InvalidArgument);
    assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(),Math::Vector3()),1,Visit,&visits) == Result::InvalidArgument);
    assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(),Math::Vector3::UnitX),-1,Visit,&visits) == Result::InvalidArgument);
    assert(visits.Count == 0);
    for (float bad : {std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    {
        assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(bad,0,0),Math::Vector3::UnitX),1,Visit,&visits) == Result::InvalidArgument);
        assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(),Math::Vector3(bad,0,0)),1,Visit,&visits) == Result::InvalidArgument);
        assert(PhysicsBroadphase::VisitProxiesAlongRay(proxies,Math::Ray(Math::Vector3(),Math::Vector3::UnitX),bad,Visit,&visits) == Result::InvalidArgument);
    }
    assert(PhysicsBroadphase::VisitProxiesInAabb(proxies,bounds,
        [](const PhysicsShapeProxy&,void*) { return Result::Success; },nullptr) == Result::Success);
    // OBB/capsuleはproxy内の既存Boundsに依存せず、実形状から候補域を求める。
    PhysicsShapeProxy shapes[2];
    shapes[0].Collider = {1,1};
    shapes[0].Shape = EPhysicsProxyShape::Box;
    shapes[0].Box = Math::OBB(Math::Vector3(1.5f,0,0),Math::Vector3(1,1,1),Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
    shapes[1].Collider = {2,1};
    shapes[1].Shape = EPhysicsProxyShape::Capsule;
    shapes[1].Capsule = Math::Capsule(Math::Vector3(0,3,0),Math::Vector3(0,2,0),1);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb(shapes,bounds,Visit,&visits) == Result::Success && visits.Count == 2);
    // 許容範囲の非厳密unit軸はdot判定の領域が前向きboundsより広がる。
    PhysicsShapeProxy nearUnit;
    nearUnit.Shape = EPhysicsProxyShape::Box;
    nearUnit.Box = Math::OBB(Math::Vector3(),Math::Vector3(10,1,1),
        Math::Vector3(.99996f,0,0),Math::Vector3::UnitY,Math::Vector3::UnitZ);
    nearUnit.Collider = {7,1};
    const Math::Vector3 boundary(10.00035f,0,0);
    PhysicsQueryDesc reference;
    reference.Ray = Math::Ray(boundary,Math::Vector3::UnitX);
    reference.MaxDistance = 0;
    PhysicsQueryHit hit;
    assert(PhysicsBroadphase::QueryProxy(nearUnit,reference,hit) == Result::Success);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesAlongRay({&nearUnit,1},reference.Ray,0,Visit,&visits) == Result::Success);
    assert(visits.Count == 1);
    visits = {};
    assert(PhysicsBroadphase::VisitProxiesInAabb({&nearUnit,1},Math::AABB(boundary,boundary),Visit,&visits) == Result::Success);
    assert(visits.Count == 1);
    std::cout << "PhysicsProxyVisitorTest PASS\n";
    return 0;
}
