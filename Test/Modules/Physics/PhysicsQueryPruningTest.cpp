#include "Physics/PhysicsBroadphase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
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
    uint32_t State = 0x5823ac71;
    size_t SuccessfulComparisons[7]{};
    uint32_t Next() { State = State*1664525u+1013904223u; return State; }
    float Random(float low, float high) { return low+(high-low)*static_cast<float>(Next()>>8)/16777216.f; }
    Math::Vector3 Position(float extent) { return Math::Vector3(Random(-extent,extent),Random(-extent,extent),Random(-extent,extent)); }
    Math::OBB Box(const Math::Vector3& center)
    {
        const float angle = Random(-3.14f,3.14f), c = std::cos(angle), s = std::sin(angle);
        return Math::OBB(center,Math::Vector3(Random(.1f,2),Random(.1f,2),Random(.1f,2)),
            Math::Vector3(c,0,s),Math::Vector3::UnitY,Math::Vector3(-s,0,c));
    }
    Math::Capsule Capsule(const Math::Vector3& center)
    {
        const float angle = Random(-3.14f,3.14f), height = Random(0,2);
        const auto half = Math::Vector3(.6f*std::cos(angle),.6f*std::sin(angle),.8f)*height;
        return Math::Capsule(center-half,center+half,Random(.1f,2));
    }
    Result Reference(Core::Container::Span<const PhysicsShapeProxy> proxies, const PhysicsQueryDesc& query,
        PhysicsQueryHit* output, size_t& count)
    {
        count = 0;
        for (size_t index = 0; index < 64; ++index)
        {
            output[index] = {};
        }
        for (const auto& proxy : proxies)
        {
            PhysicsQueryHit hit;
            const auto result = PhysicsBroadphase::QueryProxy(proxy,query,hit);
            if (result == Result::NoHit)
            {
                continue;
            }
            if (result != Result::Success)
            {
                count = 0;
                for (size_t index = 0; index < 64; ++index)
                {
                    output[index] = {};
                }
                return result;
            }
            output[count++] = hit;
        }
        const bool overlap = query.Kind == EPhysicsQueryKind::OverlapSphere || query.Kind == EPhysicsQueryKind::OverlapBox || query.Kind == EPhysicsQueryKind::OverlapCapsule;
        std::sort(output,output+count,[&](const PhysicsQueryHit& a,const PhysicsQueryHit& b)
        {
            if (!overlap && a.Distance != b.Distance)
            {
                return a.Distance < b.Distance;
            }
            if (query.Kind == EPhysicsQueryKind::RaycastClosest)
            {
                return b.Collider < a.Collider;
            }
            return a.Collider < b.Collider;
        });
        count = std::min(count,query.Kind == EPhysicsQueryKind::RaycastClosest ? size_t{1} : static_cast<size_t>(query.MaxHits));
        return count == 0 ? Result::NoHit : Result::Success;
    }
    void Compare(Core::Container::Span<const PhysicsShapeProxy> proxies, const PhysicsQueryDesc& query)
    {
        PhysicsQueryHit expected[64], actual[64];
        size_t expectedCount = 0, actualCount = 99;
        const auto reference = Reference(proxies,query,expected,expectedCount);
        const auto result = PhysicsBroadphase::ExecuteQueryOverProxies(proxies,query,actual,actualCount);
        if (result != reference)
        {
            std::cerr << "result mismatch kind=" << static_cast<int>(query.Kind) << " reference=" << static_cast<int>(reference) << " actual=" << static_cast<int>(result) << " seed=" << State << "\n";
        }
        assert(result == reference && actualCount == expectedCount);
        if (result == Result::Success)
        {
            ++SuccessfulComparisons[static_cast<size_t>(query.Kind)];
        }
        for (size_t index = 0; index < actualCount; ++index)
        {
            const auto& a = actual[index];
            const auto& e = expected[index];
            assert(a.Collider == e.Collider && a.Body == e.Body && a.Entity == e.Entity && a.bHasEntity == e.bHasEntity);
            assert(a.UserData == e.UserData && a.Distance == e.Distance && a.Point == e.Point && a.Normal == e.Normal);
            assert(a.Depth == e.Depth && a.bStartPenetrating == e.bStartPenetrating);
        }
    }
}
int main()
{
    State = 0x5823ac71;
    for (auto& count : SuccessfulComparisons)
    {
        count = 0;
    }
    for (int scene = 0; scene < 1000; ++scene)
    {
        PhysicsShapeProxy proxies[16];
        for (uint32_t index = 0; index < 16; ++index)
        {
            auto& p = proxies[index];
            p.Collider = {16-index,1+Next()%3};
            p.Body = {index,1};
            p.UserData = 100+index;
            p.Layer = uint32_t{1} << (Next()%3);
            p.bTrigger = Next()%4 == 0;
            p.Shape = static_cast<EPhysicsProxyShape>(Next()%3);
            const auto center = Position(20);
            p.Sphere = Math::Sphere(center,Random(.1f,2));
            p.Box = Box(center);
            p.Capsule = Capsule(center);
        }
        for (int kind = 0; kind < 7; ++kind)
        {
            PhysicsQueryDesc query;
            query.Kind = static_cast<EPhysicsQueryKind>(kind);
            const auto origin = Position(25);
            query.Ray = Math::Ray(origin,Position(1));
            query.Sphere = Math::Sphere(origin,Random(.1f,2));
            query.Box = Box(origin);
            query.Capsule = Capsule(origin);
            query.Direction = Position(1);
            query.MaxDistance = 60;
            query.MaxHits = 1+Next()%20;
            query.MaxSweepIterations = 128;
            query.Filter.LayerMask = Next()%8;
            query.Filter.TriggerPolicy = static_cast<EPhysicsQueryTriggerPolicy>(Next()%3);
            if (Next()%4 == 0)
            {
                query.Filter.IgnoreColliders[0] = proxies[Next()%16].Collider;
            }
            if (scene%3 == 0)
            {
                query.Filter = {};
                const auto target = proxies[0].Sphere.Center;
                const bool overlap = kind >= 2 && kind <= 4;
                const auto center = overlap ? target : target-Math::Vector3(8,0,0);
                query.Sphere.Center = center;
                query.Box.Center = center;
                query.Capsule = Capsule(center);
                query.Ray = Math::Ray(center,Math::Vector3::UnitX);
                query.Direction = Math::Vector3::UnitX;
            }
            Compare(proxies,query);
        }
    }
    for (size_t kind = 0; kind < 7; ++kind)
    {
        assert(SuccessfulComparisons[kind] >= 300);
        std::cout << "kind=" << kind << " known-hit comparisons=" << SuccessfulComparisons[kind] << "\n";
    }
    PhysicsShapeProxy proxy;
    proxy.Collider = {1,1};
    proxy.Sphere = Math::Sphere(Math::Vector3(5,0,0),1);
    PhysicsQueryDesc query;
    query.Kind = EPhysicsQueryKind::SweepSphere;
    query.Sphere = Math::Sphere(Math::Vector3(),1);
    query.Direction = Math::Vector3::UnitX;
    query.MaxDistance = 10;
    query.MaxSweepIterations = 1;
    PhysicsQueryHit hits[64], hit;
    size_t count = 0;
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::IterationLimit && count == 0);
    // y方向の間隔が半径和より広く、全移動区間で非交差を証明できる。
    proxy.Sphere.Center.y = 2.01f;
    assert(PhysicsBroadphase::QueryProxy(proxy,query,hit) == Result::IterationLimit);
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::NoHit && count == 0);
    query.MaxSweepIterations = 24;
    proxy.Sphere.Center = query.Sphere.Center = Math::Vector3(std::numeric_limits<float>::max()/16,0,0);
    query.MaxDistance = std::numeric_limits<float>::max();
    assert(PhysicsBroadphase::QueryProxy(proxy,query,hit) == Result::Success && hit.bStartPenetrating);
    Compare({&proxy,1},query);
    proxy.Shape = EPhysicsProxyShape::Box;
    proxy.Box = Math::OBB(Math::Vector3(),Math::Vector3(10,1,1),Math::Vector3(.99996f,0,0),Math::Vector3::UnitY,Math::Vector3::UnitZ);
    query = {};
    query.Ray = Math::Ray(Math::Vector3(10.00035f,0,0),Math::Vector3::UnitX);
    Compare({&proxy,1},query);
    proxy.Shape = EPhysicsProxyShape::Sphere;
    proxy.Sphere = Math::Sphere(Math::Vector3(1000,0,0),-1);
    query.Ray = Math::Ray(Math::Vector3(),Math::Vector3::UnitX);
    query.MaxDistance = 1;
    query.Filter.LayerMask = 0;
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::InvalidArgument && count == 0);
    query.Filter = {};
    proxy.Sphere = Math::Sphere(Math::Vector3(1e8f,0,0),1);
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::InvalidArgument && count == 0);
    assert(PhysicsBroadphase::ExecuteQueryOverProxies(Core::Container::Span<const PhysicsShapeProxy>(nullptr,1),query,hits,count) == Result::InvalidArgument && count == 0);
    assert(PhysicsBroadphase::ExecuteQueryOverProxies({},query,Core::Container::Span<PhysicsQueryHit>(nullptr,1),count) == Result::InvalidArgument && count == 0);
    // float Gramでは通るが実Sweepのdouble検証で落ちる境界OBBも、遠方除外で隠さない。
    proxy.Shape = EPhysicsProxyShape::Box;
    proxy.Box = Math::OBB(Math::Vector3(0,100,0),Math::Vector3(1),
        Math::Vector3(-.81839263439178467f,.57351326942443848f,.037630140781402588f),
        Math::Vector3(.38710057735443115f,.50162374973297119f,.77364510297775269f),
        Math::Vector3(.42479833960533142f,.64767968654632568f,-.63250088691711426f));
    PhysicsQueryDesc overlap;
    overlap.Kind = EPhysicsQueryKind::OverlapBox;
    overlap.Box = proxy.Box;
    assert(PhysicsBroadphase::IsValidQuery(overlap));
    for (auto kind : {EPhysicsQueryKind::SweepSphere,EPhysicsQueryKind::SweepCapsule})
    {
        query = {};
        query.Kind = kind;
        query.Sphere = Math::Sphere(Math::Vector3(),1);
        query.Capsule = Math::Capsule(Math::Vector3(0,-1,0),Math::Vector3(0,1,0),1);
        query.Direction = Math::Vector3::UnitX;
        query.MaxDistance = 10;
        assert(PhysicsBroadphase::QueryProxy(proxy,query,hit) == Result::InvalidArgument);
        assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::InvalidArgument && count == 0);
        query.Filter.LayerMask = 0;
        assert(PhysicsBroadphase::QueryProxy(proxy,query,hit) == Result::NoHit);
        assert(PhysicsBroadphase::ExecuteQueryOverProxies({&proxy,1},query,hits,count) == Result::NoHit && count == 0);
    }
    std::cout << "PhysicsQueryPruningTest PASS: 7000 fixed-seed comparisons\n";
    return 0;
}
