// 速くした探索が、組・最近接・重なりの結果と順を素朴な総当たりから変えないことを反証する。
// 組の取りこぼしは撮影・ログで気づけず黙って起きるため、固定の種で全組・全形状を照合する。
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "Physics/PhysicsBroadphase.h"
#include "PhysicsModuleTestAccess.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"

#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Modules::Physics;

namespace
{
    using Proxies = Container::VariableArray<PhysicsShapeProxy>;
    using Pairs = Container::VariableArray<PhysicsCandidatePair>;
    using Mobility = EPhysicsProxyMobility;
    using Result = EPhysicsSceneQueryResult;
    constexpr uint32_t ProxyCount = 2000;

    struct RandomSource
    {
        uint32_t State;
        uint32_t Next()
        {
            State = State * 1664525u + 1013904223u;
            return State;
        }
        float Range(float low, float high)
        {
            return low + (high - low) * static_cast<float>(Next() >> 8) / 16777216.0f;
        }
        Math::Vector3 Position(float extent)
        {
            const float x = Range(-extent, extent);
            const float y = Range(-extent, extent);
            const float z = Range(-extent, extent);
            return Math::Vector3(x, y, z);
        }
        Math::OBB Box(const Math::Vector3& center)
        {
            const float angle = Range(-3.14f, 3.14f);
            const float c = std::cos(angle), s = std::sin(angle);
            const float x = Range(.5f, 1.5f), y = Range(.5f, 1.5f), z = Range(.5f, 1.5f);
            return Math::OBB(center, Math::Vector3(x, y, z),
                Math::Vector3(c, 0, s), Math::Vector3::UnitY, Math::Vector3(-s, 0, c));
        }
        Math::Capsule Capsule(const Math::Vector3& center)
        {
            const float angle = Range(-3.14f, 3.14f);
            const auto half = Math::Vector3(.6f * std::cos(angle), .6f * std::sin(angle), .8f) * Range(.3f, 1.5f);
            return Math::Capsule(center - half, center + half, Range(.5f, 1.5f));
        }
    };

    void Shuffle(Proxies& proxies, uint32_t seed)
    {
        RandomSource random{seed};
        for (size_t remaining = proxies.size(); remaining > 1; --remaining)
        {
            std::swap(proxies[remaining - 1], proxies[random.Next() % remaining]);
        }
    }

    Proxies MakeProxies()
    {
        RandomSource random{0x5823ac71u};
        Proxies proxies;
        proxies.reserve(ProxyCount);
        for (uint32_t index = 0; index < ProxyCount; ++index)
        {
            PhysicsShapeProxy proxy;
            proxy.Collider = {index, 1};
            proxy.UserData = 10000 + index;
            proxy.SurfaceId = static_cast<uint8_t>(index % 4);
            proxy.Shape = static_cast<EPhysicsProxyShape>(index % 3);
            const auto center = random.Position(20);
            proxy.Sphere = Math::Sphere(center, random.Range(.5f, 1.5f));
            proxy.Box = random.Box(center);
            proxy.Capsule = random.Capsule(center);
            proxy.Layer = uint32_t{1} << (index % 3);
            proxy.Mask = index % 4 == 0 ? proxy.Layer : AllPhysicsLayers;
            proxy.bTrigger = index % 5 == 0;
            proxies.push_back(proxy);
        }
        // 離れた場所に境界で接する箱と、同距離を保証する同形の球を置く。
        for (uint32_t index = ProxyCount - 4; index < ProxyCount - 2; ++index)
        {
            proxies[index].Shape = EPhysicsProxyShape::Box;
            proxies[index].Box = Math::OBB(Math::Vector3(-80 + 2.0f * (index - (ProxyCount - 4)), 0, 0),
                Math::Vector3(1), Math::Vector3::UnitX, Math::Vector3::UnitY, Math::Vector3::UnitZ);
            proxies[index].Layer = DefaultPhysicsLayer;
            proxies[index].Mask = AllPhysicsLayers;
        }
        for (uint32_t index = ProxyCount - 2; index < ProxyCount; ++index)
        {
            proxies[index].Shape = EPhysicsProxyShape::Sphere;
            proxies[index].Sphere = Math::Sphere(Math::Vector3(80, 0, 0), 1);
            proxies[index].Layer = DefaultPhysicsLayer;
            proxies[index].Mask = AllPhysicsLayers;
        }
        Shuffle(proxies, 0x13579bdfu);
        assert(!std::is_sorted(proxies.begin(), proxies.end(), [](const auto& a, const auto& b)
        {
            return a.Collider < b.Collider;
        }));
        return proxies;
    }

    bool BoundsOverlap(const Math::AABB& a, const Math::AABB& b)
    {
        return a.Min.x <= b.Max.x && b.Min.x <= a.Max.x
            && a.Min.y <= b.Max.y && b.Min.y <= a.Max.y
            && a.Min.z <= b.Max.z && b.Min.z <= a.Max.z;
    }

    Pairs ReferencePairs(const Proxies& proxies, size_t& outLayerRejected)
    {
        Container::VariableArray<Math::AABB> bounds;
        for (const auto& proxy : proxies)
        {
            bounds.push_back(PhysicsBroadphase::CalculateBounds(proxy));
        }
        Pairs pairs;
        outLayerRejected = 0;
        for (size_t first = 0; first < proxies.size(); ++first)
        {
            for (size_t second = first + 1; second < proxies.size(); ++second)
            {
                const auto& a = proxies[first];
                const auto& b = proxies[second];
                if (!BoundsOverlap(bounds[first], bounds[second]))
                {
                    continue;
                }
                if (!CanPhysicsLayersInteract(a.Layer, a.Mask, b.Layer, b.Mask))
                {
                    ++outLayerRejected;
                    continue;
                }
                const bool bBothSpecified = a.Mobility != Mobility::Unspecified && b.Mobility != Mobility::Unspecified;
                const bool bActive = a.Mobility == Mobility::Dynamic || b.Mobility == Mobility::Dynamic
                    || a.Mobility == Mobility::Trigger || b.Mobility == Mobility::Trigger;
                if (bBothSpecified && !bActive)
                {
                    continue;
                }
                const bool bFirstSmaller = a.Collider < b.Collider;
                pairs.push_back({bFirstSmaller ? a.Collider : b.Collider, bFirstSmaller ? b.Collider : a.Collider});
            }
        }
        std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b)
        {
            return a.First < b.First || (a.First == b.First && a.Second < b.Second);
        });
        return pairs;
    }

    void ComparePairs(const Pairs& actual, const Pairs& expected)
    {
        assert(actual.size() == expected.size());
        for (size_t index = 0; index < actual.size(); ++index)
        {
            assert(actual[index].First == expected[index].First && actual[index].Second == expected[index].Second);
        }
    }

    bool HasPair(const Pairs& pairs, ColliderHandle first, ColliderHandle second)
    {
        if (second < first)
        {
            std::swap(first, second);
        }
        for (const auto& pair : pairs)
        {
            if (pair.First == first && pair.Second == second)
            {
                return true;
            }
        }
        return false;
    }

    size_t TestPairs(const Proxies& proxies)
    {
        size_t layerRejected = 0;
        const auto expected = ReferencePairs(proxies, layerRejected);
        assert(expected.size() >= 100 && layerRejected > 0);
        PhysicsBroadphase broadphase;
        Proxies reordered = proxies;
        for (int order = 0; order < 3; ++order)
        {
            if (order == 1)
            {
                std::reverse(reordered.begin(), reordered.end());
            }
            else if (order == 2)
            {
                Shuffle(reordered, 0x913adf25u);
            }
            broadphase.SetProxies(reordered);
            ComparePairs(broadphase.GetCandidatePairs(), expected);
            assert(broadphase.GetStats().CandidatePairCount == expected.size());
            assert(broadphase.GetStats().TreeRebuildCount == 0);
        }
        std::cout << "候補の組=" << expected.size() << "、層で除外=" << layerRejected << "、入力順3通りで一致\n";
        return expected.size();
    }

    void CompareRay(const PhysicsBroadphase& broadphase, const Proxies& proxies, const Math::Ray& ray,
        ColliderHandle required = {})
    {
        PhysicsQueryDesc query;
        query.Kind = EPhysicsQueryKind::RaycastClosest;
        query.Ray = ray;
        query.MaxDistance = 150;
        bool bExpected = false;
        PhysicsQueryHit expected;
        // 個別判定には探索枝刈りを使わず、距離とハンドルだけで勝者を選ぶ。
        for (const auto& proxy : proxies)
        {
            PhysicsQueryHit hit;
            const auto result = PhysicsBroadphase::QueryProxy(proxy, query, hit);
            assert(result == Result::Success || result == Result::NoHit);
            if (result == Result::Success && (!bExpected || hit.Distance < expected.Distance
                || (hit.Distance == expected.Distance && expected.Collider < hit.Collider)))
            {
                bExpected = true;
                expected = hit;
            }
        }
        PhysicsRaycastHit actual;
        assert(broadphase.Raycast(ray, query.MaxDistance, actual) == bExpected);
        if (required.IsValid())
        {
            assert(bExpected && actual.Collider == required);
        }
        if (bExpected)
        {
            assert(actual.Collider == expected.Collider && actual.Distance == expected.Distance);
            assert(actual.Point == expected.Point && actual.Normal == expected.Normal);
            assert(actual.Body == expected.Body && actual.Entity == expected.Entity && actual.bHasEntity == expected.bHasEntity);
            assert(actual.UserData == expected.UserData && actual.SurfaceId == expected.SurfaceId);
        }
    }

    void CompareOverlap(const PhysicsBroadphase& broadphase, const Proxies& proxies, const PhysicsQueryDesc& query)
    {
        Container::VariableArray<PhysicsQueryHit> expected;
        for (const auto& proxy : proxies)
        {
            PhysicsQueryHit hit;
            const auto result = PhysicsBroadphase::QueryProxy(proxy, query, hit);
            assert(result == Result::Success || result == Result::NoHit);
            if (result == Result::Success)
            {
                expected.push_back(hit);
            }
        }
        std::sort(expected.begin(), expected.end(), [](const auto& a, const auto& b)
        {
            return a.Collider < b.Collider;
        });
        Container::VariableArray<PhysicsOverlapHit> actual;
        switch (query.Kind)
        {
        case EPhysicsQueryKind::OverlapSphere:
            broadphase.OverlapSphere(query.Sphere, actual);
            break;
        case EPhysicsQueryKind::OverlapBox:
            broadphase.OverlapBox(query.Box, actual);
            break;
        case EPhysicsQueryKind::OverlapCapsule:
            broadphase.OverlapCapsule(query.Capsule, actual);
            break;
        default:
            assert(false);
        }
        assert(actual.size() == expected.size());
        for (size_t index = 0; index < actual.size(); ++index)
        {
            const auto& a = actual[index];
            const auto& e = expected[index];
            assert(a.Collider == e.Collider && a.Body == e.Body && a.Entity == e.Entity && a.bHasEntity == e.bHasEntity);
            assert(a.UserData == e.UserData && a.SurfaceId == e.SurfaceId);
            assert(a.Contact.Point == e.Point && a.Contact.Normal == e.Normal * -1.0f && a.Contact.Depth == e.Depth);
        }
    }

    void TestQueries(const Proxies& proxies)
    {
        PhysicsBroadphase broadphase;
        broadphase.SetProxies(proxies);
        RandomSource random{0x7513cdf2u};
        for (uint32_t index = 0; index < 200; ++index)
        {
            const auto origin = random.Position(30);
            const auto direction = index % 2 == 0
                ? proxies[index].Sphere.Center - origin : random.Position(1);
            CompareRay(broadphase, proxies, Math::Ray(origin, direction));
        }
        CompareRay(broadphase, proxies, Math::Ray(Math::Vector3(75, 0, 0), Math::Vector3::UnitX), {ProxyCount - 1, 1});
        for (uint32_t index = 0; index < 100; ++index)
        {
            PhysicsQueryDesc query;
            const auto center = index % 2 == 0 ? proxies[index].Sphere.Center : random.Position(25);
            query.Kind = EPhysicsQueryKind::OverlapSphere;
            query.Sphere = Math::Sphere(center, random.Range(.5f, 3));
            CompareOverlap(broadphase, proxies, query);
            query.Kind = EPhysicsQueryKind::OverlapBox;
            query.Box = random.Box(center);
            CompareOverlap(broadphase, proxies, query);
            query.Kind = EPhysicsQueryKind::OverlapCapsule;
            query.Capsule = random.Capsule(center);
            CompareOverlap(broadphase, proxies, query);
        }
        std::cout << "光線200本と同距離1本、各形状の重なり100回が総当たりと一致\n";
    }

    void CheckModuleCase(IPhysicsModule& physics, EPhysicsBodyType bodyType, bool bTrigger, Mobility expectedMobility)
    {
        World world;
        world.Initialize();
        auto* firstEntity = world.SpawnEntity<Entity>();
        auto* secondEntity = world.SpawnEntity<Entity>();
        assert(firstEntity != nullptr && secondEntity != nullptr);
        auto* first = world.CreateComponent<ColliderComponent>(firstEntity);
        auto* second = world.CreateComponent<ColliderComponent>(secondEntity);
        auto* body = world.CreateComponent<RigidBodyComponent>(firstEntity);
        assert(first != nullptr && second != nullptr && body != nullptr);
        assert(first->SetBox(Math::Vector3(1)) == EPhysicsResult::Success);
        assert(second->SetBox(Math::Vector3(1)) == EPhysicsResult::Success);
        assert(body->SetBodyType(bodyType) == EPhysicsResult::Success);
        assert(body->SetGravityScale(0) == EPhysicsResult::Success);
        assert(first->SetTrigger(bTrigger) == EPhysicsResult::Success);
        physics.StepPreFixed(1.0f / 60.0f);
        physics.StepFixed(1.0f / 60.0f);
        const auto& proxies = PhysicsModuleTestAccess::GetWorkingProxies(physics);
        assert(proxies.size() == 2);
        for (const auto& proxy : proxies)
        {
            if (proxy.Collider == first->GetColliderHandle())
            {
                assert(proxy.Mobility == expectedMobility);
            }
            else
            {
                assert(proxy.Collider == second->GetColliderHandle() && proxy.Mobility == Mobility::Static);
                assert(!proxy.Body.IsValid());
            }
        }
        const auto& pairs = PhysicsModuleTestAccess::GetWorkingCandidatePairs(physics);
        const bool bExpected = expectedMobility == Mobility::Dynamic || expectedMobility == Mobility::Trigger;
        assert(HasPair(pairs, first->GetColliderHandle(), second->GetColliderHandle()) == bExpected);
        assert(pairs.size() == (bExpected ? 1u : 0u));
        assert(PhysicsModuleTestAccess::GetWorkingBroadphaseStats(physics).CandidatePairCount == pairs.size());
        world.Finalize();
    }

    void TestModulePolicy()
    {
        auto engine = Container::MakeUnique<Engine::Engine>();
        auto& registry = Module::GetModuleRegistry();
        auto* physics = RegisterPhysicsModule(registry);
        assert(physics != nullptr && registry.InstallAll(*engine));
        CheckModuleCase(*physics, EPhysicsBodyType::Kinematic, false, Mobility::Kinematic);
        CheckModuleCase(*physics, EPhysicsBodyType::Dynamic, false, Mobility::Dynamic);
        CheckModuleCase(*physics, EPhysicsBodyType::Kinematic, true, Mobility::Trigger);
        registry.ShutdownAll(*engine);
        std::cout << "モジュール経由のKinematic・Static・Dynamic・Triggerと組の方針が一致\n";
    }

    void TestUnspecifiedCompatibility()
    {
        Proxies proxies(2);
        for (uint32_t index = 0; index < 2; ++index)
        {
            proxies[index].Collider = {index, 1};
            proxies[index].Sphere = Math::Sphere(Math::Vector3(), 1);
            assert(proxies[index].Mobility == Mobility::Unspecified && !proxies[index].Body.IsValid());
        }
        PhysicsBroadphase broadphase;
        broadphase.SetProxies(proxies);
        assert(broadphase.GetCandidatePairs().size() == 1);
        assert(HasPair(broadphase.GetCandidatePairs(), {0, 1}, {1, 1}));
        // 片方だけ設定した場合も、手組みのプロキシとの互換性を保つ。
        proxies[0].Mobility = Mobility::Static;
        broadphase.SetProxies(proxies);
        assert(broadphase.GetCandidatePairs().size() == 1);
    }
} // 名前空間

int main(int argumentCount, char** arguments)
{
    (void)arguments;
    // 束の入口が --test= を取り除いた後は引数を受け付けない。
    if (argumentCount != 1)
    {
        std::cerr << "不正な引数です\n";
        return EXIT_FAILURE;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    const auto proxies = MakeProxies();
    const size_t unspecifiedPairs = TestPairs(proxies);
    PhysicsBroadphase boundary;
    boundary.SetProxies(proxies);
    assert(HasPair(boundary.GetCandidatePairs(), {ProxyCount - 4, 1}, {ProxyCount - 3, 1}));
    Proxies mixed = proxies;
    size_t dynamicCount = 0;
    for (auto& proxy : mixed)
    {
        const uint32_t index = proxy.Collider.Index;
        proxy.Mobility = index < 100 ? Mobility::Dynamic
            : index % 3 == 0 ? Mobility::Static : index % 3 == 1 ? Mobility::Kinematic : Mobility::Trigger;
        proxy.bTrigger = proxy.Mobility == Mobility::Trigger;
        dynamicCount += proxy.Mobility == Mobility::Dynamic ? 1 : 0;
    }
    assert(dynamicCount == 100);
    assert(TestPairs(mixed) < unspecifiedPairs);
    TestQueries(proxies);
    TestQueries(mixed);
    TestUnspecifiedCompatibility();
    TestModulePolicy();
    std::cout << "大規模な物理探索の照合に成功しました\n";
    return EXIT_SUCCESS;
}
