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
    bool Near(float actual, double expected)
    {
        return std::fabs(static_cast<double>(actual)-expected) <= std::fmax(2e-6,std::fabs(expected)*1e-7);
    }
    Result Query(const PhysicsShapeProxy& proxy, const Math::Vector3& origin, const Math::Vector3& direction,
        float distance, PhysicsQueryHit& hit)
    {
        PhysicsQueryDesc query;
        query.Ray = Math::Ray(origin,direction);
        query.MaxDistance = distance;
        return PhysicsBroadphase::QueryProxy(proxy,query,hit);
    }
    void Miss(const PhysicsShapeProxy& proxy, const Math::Vector3& origin, const Math::Vector3& direction, float distance)
    {
        PhysicsQueryHit hit;
        hit.UserData = UINT64_MAX;
        assert(Query(proxy,origin,direction,distance,hit) == Result::NoHit);
        assert(hit.UserData == 0 && hit.Distance == 0 && !hit.Collider.IsValid());
    }
}
int main()
{
    PhysicsShapeProxy sphere;
    sphere.Collider = {1,1};
    sphere.Sphere = Math::Sphere(Math::Vector3(),1);
    PhysicsShapeProxy capsule;
    capsule.Collider = {2,1};
    capsule.Shape = EPhysicsProxyShape::Capsule;
    capsule.Capsule = Math::Capsule(Math::Vector3(0,-2,0),Math::Vector3(0,2,0),1);
    PhysicsQueryHit hit;
    for (float distance : {1.f,10.f,100.f,1000.f,10000.f,100000.f})
    {
        for (float radius : {.001f,.1f,1.f,10.f})
        {
            if (distance <= radius)
            {
                continue;
            }
            sphere.Sphere.Radius = radius;
            capsule.Capsule.Radius = radius;
            for (float offsetFactor : {0.f,.5f,.9f,1.f})
            {
                const float offset = radius*offsetFactor;
                const double section = static_cast<double>(radius)*radius-static_cast<double>(offset)*offset;
                const double expected = distance-std::sqrt(std::fmax(0.0,section));
                assert(Query(sphere,Math::Vector3(-distance,0,offset),Math::Vector3::UnitX,2*distance,hit) == Result::Success);
                assert(Near(hit.Distance,expected));
                const double normalLength = std::sqrt(static_cast<double>(hit.Normal.x)*hit.Normal.x+
                    static_cast<double>(hit.Normal.y)*hit.Normal.y+static_cast<double>(hit.Normal.z)*hit.Normal.z);
                assert(std::fabs(normalLength-1) < 1e-5);
                assert(Query(capsule,Math::Vector3(-distance,0,offset),Math::Vector3::UnitX,2*distance,hit) == Result::Success);
                assert(Near(hit.Distance,expected));
                const double capsuleNormalLength = std::sqrt(static_cast<double>(hit.Normal.x)*hit.Normal.x+
                    static_cast<double>(hit.Normal.y)*hit.Normal.y+static_cast<double>(hit.Normal.z)*hit.Normal.z);
                assert(std::fabs(capsuleNormalLength-1) < 1e-5);
            }
            Miss(sphere,Math::Vector3(-distance,0,2*radius),Math::Vector3::UnitX,2*distance);
            Miss(capsule,Math::Vector3(-distance,0,2*radius),Math::Vector3::UnitX,2*distance);
            Miss(sphere,Math::Vector3(-distance,0,0),Math::Vector3(-1,0,0),2*distance);
            Miss(capsule,Math::Vector3(-distance,0,0),Math::Vector3(-1,0,0),2*distance);
        }
    }
    sphere.Sphere.Radius = capsule.Capsule.Radius = 1;
    for (float scale : {std::numeric_limits<float>::denorm_min(),1.f,1e30f})
    {
        assert(Query(sphere,Math::Vector3(-10000,0,.5f),Math::Vector3(scale,0,0),20000,hit) == Result::Success);
        assert(Near(hit.Distance,10000-std::sqrt(.75)));
        assert(Query(capsule,Math::Vector3(-10000,0,.5f),Math::Vector3(scale,0,0),20000,hit) == Result::Success);
        assert(Near(hit.Distance,10000-std::sqrt(.75)));
    }
    // 軸に平行なrayは先端球が最初、胴の側面は円筒が最初。
    assert(Query(capsule,Math::Vector3(0,-10,0),Math::Vector3::UnitY,20,hit) == Result::Success && Near(hit.Distance,7));
    assert(Query(capsule,Math::Vector3(-10,2.5f,0),Math::Vector3::UnitX,20,hit) == Result::Success);
    assert(Near(hit.Distance,10-std::sqrt(.75)));
    assert(Query(capsule,Math::Vector3(-10,2,0),Math::Vector3::UnitX,20,hit) == Result::Success && Near(hit.Distance,9));
    for (const auto& origin : {Math::Vector3(),Math::Vector3(1,0,0),Math::Vector3(0,3,0)})
    {
        assert(Query(capsule,origin,Math::Vector3::UnitX,0,hit) == Result::Success && hit.Distance == 0);
    }
    assert(Query(sphere,Math::Vector3(1,0,0),Math::Vector3::UnitX,0,hit) == Result::Success && hit.Distance == 0);
    Miss(sphere,Math::Vector3(-10,0,0),Math::Vector3::UnitX,8.99f);
    Miss(capsule,Math::Vector3(-10,0,0),Math::Vector3::UnitX,8.99f);
    capsule.Capsule.PointB = capsule.Capsule.PointA = Math::Vector3();
    assert(Query(capsule,Math::Vector3(-10000,0,.5f),Math::Vector3::UnitX,20000,hit) == Result::Success);
    assert(Near(hit.Distance,10000-std::sqrt(.75)));
    Miss(capsule,Math::Vector3(-10000,0,2),Math::Vector3::UnitX,20000);
    // 極小だが非zeroの線分も端球へ潰さず、側面の法線を返す。
    capsule.Capsule = Math::Capsule(Math::Vector3(),Math::Vector3(0,.0005f,0),.0001f);
    assert(Query(capsule,Math::Vector3(-.001f,.00025f,0),Math::Vector3::UnitX,1,hit) == Result::Success);
    assert(Near(hit.Distance,.0009) && hit.Normal.x < -.999f && std::fabs(hit.Normal.y) < 1e-5f);
    // 斜めの線分と平行なrayで、投影差の偽円筒根を選ばない。
    capsule.Capsule = Math::Capsule(Math::Vector3(-1,-1,-1),Math::Vector3(1,1,1),1);
    assert(Query(capsule,Math::Vector3(-10,-10,-10),Math::Vector3(1,1,1),100,hit) == Result::Success);
    assert(Near(hit.Distance,9*std::sqrt(3.0)-1));
    capsule.Capsule = Math::Capsule(Math::Vector3(0,-100000,0),Math::Vector3(0,100000,0),1);
    const Math::Vector3 parallelOrigin(1.001f,-99999,0), almostParallel(-1e-8f,1,0);
    const double parallelDistance = (static_cast<double>(parallelOrigin.x)-1)/-almostParallel.x;
    assert(Query(capsule,parallelOrigin,almostParallel,200000,hit) == Result::Success);
    assert(Near(hit.Distance,parallelDistance));
    Miss(capsule,parallelOrigin,almostParallel,static_cast<float>(parallelDistance*.99));
    const auto originalA = capsule.Capsule.PointA;
    capsule.Capsule.PointA = capsule.Capsule.PointB;
    capsule.Capsule.PointB = originalA;
    assert(Query(capsule,parallelOrigin,almostParallel,200000,hit) == Result::Success);
    assert(Near(hit.Distance,parallelDistance));
    sphere.Sphere.Radius = 0;
    assert(Query(sphere,Math::Vector3(-1,0,0),Math::Vector3::UnitX,2,hit) == Result::Success && hit.Distance == 1);
    Miss(sphere,Math::Vector3(-1,1e-6f,0),Math::Vector3::UnitX,2);
    capsule.Capsule = Math::Capsule(Math::Vector3(0,-1,0),Math::Vector3(0,1,0),0);
    assert(Query(capsule,Math::Vector3(-1,0,0),Math::Vector3::UnitX,2,hit) == Result::Success && hit.Distance == 1);
    // 最終hit.Pointがworld floatへ丸まって中心と一致しても、法線は局所doubleで保持する。
    sphere.Sphere = Math::Sphere(Math::Vector3(1000000,0,0),.001f);
    assert(Query(sphere,Math::Vector3(999990,0,0),Math::Vector3::UnitX,20,hit) == Result::Success);
    assert(Near(hit.Distance,9.999) && hit.Normal.x < -.999f);
    capsule.Capsule = Math::Capsule(Math::Vector3(1000000,-2,0),Math::Vector3(1000000,2,0),.001f);
    assert(Query(capsule,Math::Vector3(999990,0,0),Math::Vector3::UnitX,20,hit) == Result::Success);
    assert(Near(hit.Distance,9.999) && hit.Normal.x < -.999f && std::fabs(hit.Normal.y) < 1e-5f);
    PhysicsShapeProxy box;
    box.Shape = EPhysicsProxyShape::Box;
    box.Box = Math::OBB(Math::Vector3(100000,0,0),Math::Vector3(1,.001f,1),
        Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
    Miss(box,Math::Vector3(),Math::Vector3(1,1e-7f,0),200000);
    assert(Query(box,Math::Vector3(0,-.01f,0),Math::Vector3(1,1e-7f,0),200000,hit) == Result::Success);
    assert(Near(hit.Distance,99999));
    box.Box = Math::OBB(Math::Vector3(1000000,0,0),Math::Vector3(.001f,1,1),
        Math::Vector3::UnitX,Math::Vector3::UnitY,Math::Vector3::UnitZ);
    assert(Query(box,Math::Vector3(999990,0,0),Math::Vector3::UnitX,20,hit) == Result::Success);
    assert(Near(hit.Distance,9.999) && hit.Normal.x < -.999f);
    std::cout << "PhysicsRayPrecisionTest PASS\n";
    return 0;
}
