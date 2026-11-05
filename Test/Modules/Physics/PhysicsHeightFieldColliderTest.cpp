// 地形の全問い合わせが高さ場の直接照会と一致し、変換の拒否と地面の選択が守られることを反証する。
// 台地へ落とした球・カプセルの静止・Hit と、斜面を往復するキャラクターの接地・移動量を反証する。
// 地形が箱やカプセルとして誤処理されても、撮影やログでは分からず接地が数 cm ずれるだけの場合がある。

#ifdef NDEBUG
#undef NDEBUG
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Physics/IPhysicsModule.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/CharacterMover.h"
#include "PhysicsModuleTestAccess.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Rendering/Terrain/TerrainHeightField.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include "Scene/SceneQuery.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <crtdbg.h>
#include <iomanip>
#include <iostream>
#include <Windows.h>

using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;
using namespace NorvesLib::Core::Scene;
using namespace NorvesLib::Modules::Physics;
namespace Terrain = NorvesLib::Core::Rendering::Terrain;

namespace
{
    using Result = EPhysicsSceneQueryResult;
    using Kind = EPhysicsQueryKind;
    constexpr uint32_t GridSize = 65;
    constexpr uint32_t CaseCount = 144;
    constexpr float Tolerance = 1.0e-4f;
    const Math::Vector3 Origin(5.0f, 2.0f, -3.0f);
    uint32_t FailureCount = 0;

    void Check(bool condition, const char* name, uint32_t index, const char* detail)
    {
        if (!condition)
        {
            ++FailureCount;
            if (FailureCount <= 40)
            {
                std::cerr << "不一致: " << name << " case=" << index << " " << detail << std::endl;
            }
        }
    }

    struct Random
    {
        uint32_t State = 0x48f713b9u;

        float Range(float low, float high)
        {
            State = State * 1664525u + 1013904223u;
            return low + (high - low) * static_cast<float>(State >> 8) / 16777216.0f;
        }
    };

    Container::TSharedPtr<const Terrain::TerrainHeightField> MakeField()
    {
        Terrain::TerrainGridLayout layout;
        layout.Columns = GridSize;
        layout.Rows = GridSize;
        layout.FirstX = -32.0;
        layout.FirstZ = -32.0;
        layout.Spacing = 1.0;
        Container::VariableArray<float> heights(GridSize * GridSize);
        Container::VariableArray<uint8_t> weights(GridSize * GridSize * 3);
        for (uint32_t row = 0; row < GridSize; ++row)
        {
            for (uint32_t column = 0; column < GridSize; ++column)
            {
                const float x = static_cast<float>(column) - 32.0f;
                const float z = static_cast<float>(row) - 32.0f;
                const size_t vertex = row * GridSize + column;
                heights[vertex] = 0.26f * x + 0.08f * z + 1.2f * std::sin(0.18f * x) + 0.9f * std::cos(0.16f * z);
                const uint32_t dominant = column < 22 ? 0 : column < 43 ? 1 : 2;
                for (uint32_t layer = 0; layer < 3; ++layer)
                {
                    weights[vertex * 3 + layer] = layer == dominant ? 220 : 20;
                }
            }
        }
        Container::AnsiString names[3] = {"surface0", "surface1", "surface2"};
        Terrain::TerrainSplatSamples splat;
        splat.LayerCount = 3;
        splat.Weights = Container::Span<const uint8_t>(weights);
        splat.LayerNames = Container::Span<const Container::AnsiString>(names, 3);
        Container::AnsiString error;
        auto field = Terrain::TerrainHeightField::CreateFromSamples(
            layout, Container::Span<const float>(heights), &splat, 8, error);
        if (!field)
        {
            std::cerr << "高さ場の作成に失敗: " << error.c_str() << std::endl;
        }
        assert(field);
        return field;
    }

    Engine::Engine& GetEngine()
    {
        static Engine::Engine* engine = new Engine::Engine();
        return *engine;
    }

    struct PhysicsFixture
    {
        PhysicsFixture() : Registry(GetModuleRegistry()), Engine(GetEngine())
        {
            Physics = RegisterPhysicsModule(Registry);
            assert(Physics != nullptr);
            const bool bInstalled = Registry.InstallAll(Engine);
            assert(bInstalled);
            World.Initialize();
        }

        ~PhysicsFixture()
        {
            World.Finalize();
            Registry.ShutdownAll(Engine);
        }

        ColliderComponent* CreateCollider(const Math::Transform& transform, Entity*& outEntity)
        {
            outEntity = World.SpawnEntity<Entity>();
            assert(outEntity != nullptr);
            outEntity->SetWorldTransform(transform);
            auto* collider = World.CreateComponent<ColliderComponent>(outEntity);
            assert(collider != nullptr);
            return collider;
        }

        void Publish()
        {
            Physics->StepPreFixed(1.0f / 60.0f);
            Physics->StepFixed(1.0f / 60.0f);
            assert(Engine.GetSceneQuery().RefreshDynamicSnapshot() == Result::Success);
        }

        ModuleRegistry& Registry;
        Engine::Engine& Engine;
        IPhysicsModule* Physics = nullptr;
        World World;
    };

    struct Statistics
    {
        uint32_t Hit = 0;
        uint32_t Miss = 0;
        uint32_t IterationLimit = 0;
        uint32_t Compared = 0;
        uint32_t SuppressedStart = 0;
        uint32_t SurfaceMask = 0;
        float MaxDistanceError = 0.0f;
        float MaxPointError = 0.0f;
        float MaxDepthError = 0.0f;
        float MinNormalDot = 1.0f;
    };

    void CompareHit(const Terrain::TerrainHeightField& field, const PhysicsQueryHit& actual,
        const PhysicsQueryHit& expected, Kind kind, const char* name, uint32_t index, Statistics& stats)
    {
        ++stats.Compared;
        const float distanceError = std::fabs(actual.Distance - expected.Distance);
        const float pointError = (actual.Point - expected.Point).Length();
        const float depthError = std::fabs(actual.Depth - expected.Depth);
        stats.MaxDistanceError = std::fmax(stats.MaxDistanceError, distanceError);
        stats.MaxPointError = std::fmax(stats.MaxPointError, pointError);
        stats.MaxDepthError = std::fmax(stats.MaxDepthError, depthError);
        Check(distanceError <= Tolerance, name, index, "距離");
        Check(pointError <= Tolerance, name, index, "点");
        if (kind == Kind::OverlapSphere || kind == Kind::OverlapBox || kind == Kind::OverlapCapsule)
        {
            Check(depthError <= Tolerance, name, index, "深さ");
        }
        if (kind == Kind::SweepSphere || kind == Kind::SweepCapsule)
        {
            Check(actual.bStartPenetrating == expected.bStartPenetrating, name, index, "始点の重なり");
        }
        const bool bExpectedZero = expected.Normal.Length() < 1.0e-6f;
        const bool bActualZero = actual.Normal.Length() < 1.0e-6f;
        if (bExpectedZero || bActualZero)
        {
            Check(bExpectedZero && bActualZero, name, index, "零法線");
        }
        else
        {
            const float dot = Math::Vector3::Dot(actual.Normal, expected.Normal);
            stats.MinNormalDot = std::fmin(stats.MinNormalDot, dot);
            Check(dot >= 0.9999f, name, index, "法線");
        }
        const Math::Vector3 localPoint = actual.Point - Origin;
        const uint8_t surface = field.SampleSurface(localPoint.x, localPoint.z);
        Check(actual.SurfaceId == surface && actual.SurfaceId == expected.SurfaceId, name, index, "表面番号");
        if (surface < 3)
        {
            stats.SurfaceMask |= 1u << surface;
        }
    }

    Result DirectQuery(const Terrain::TerrainHeightField& field, const PhysicsQueryDesc& query,
        PhysicsQueryHit& expected, bool& bSuppressedStart)
    {
        expected = {};
        bSuppressedStart = false;
        if (query.Kind == Kind::RaycastClosest || query.Kind == Kind::RaycastAll)
        {
            Terrain::TerrainRayHit hit;
            const Math::Ray local(query.Ray.Origin - Origin, query.Ray.Direction);
            if (!field.Raycast(local, query.MaxDistance, hit))
            {
                return Result::NoHit;
            }
            expected.Distance = hit.Distance;
            expected.Point = hit.Point + Origin;
            expected.Normal = hit.Normal;
        }
        else if (query.Kind == Kind::SweepSphere || query.Kind == Kind::SweepCapsule)
        {
            Math::GeometrySweepHit hit;
            bool bHit = false;
            if (query.Kind == Kind::SweepSphere)
            {
                Math::Sphere local = query.Sphere;
                local.Center -= Origin;
                bHit = field.SweepSphere(local, query.Direction, query.MaxDistance, hit);
            }
            else
            {
                Math::Capsule local = query.Capsule;
                local.PointA -= Origin;
                local.PointB -= Origin;
                bHit = field.SweepCapsule(local, query.Direction, query.MaxDistance, hit);
            }
            if (!bHit)
            {
                return hit.Result == Math::EGeometrySweepResult::IterationLimit ? Result::IterationLimit : Result::NoHit;
            }
            if (!query.bReportStartOverlap && hit.bStartPenetrating)
            {
                bSuppressedStart = true;
                return Result::NoHit;
            }
            expected.Distance = hit.Distance;
            expected.Point = hit.Point + Origin;
            expected.Normal = hit.Normal;
            expected.Depth = hit.Depth;
            expected.bStartPenetrating = hit.bStartPenetrating;
        }
        else
        {
            Container::VariableArray<Math::GeometryContact> contacts;
            if (query.Kind == Kind::OverlapSphere)
            {
                Math::Sphere local = query.Sphere;
                local.Center -= Origin;
                field.OverlapSphere(local, contacts);
            }
            else if (query.Kind == Kind::OverlapBox)
            {
                Math::OBB local = query.Box;
                local.Center -= Origin;
                field.OverlapBox(local, contacts);
            }
            else
            {
                Math::Capsule local = query.Capsule;
                local.PointA -= Origin;
                local.PointB -= Origin;
                field.OverlapCapsule(local, contacts);
            }
            if (contacts.empty())
            {
                return Result::NoHit;
            }
            const Math::GeometryContact* deepest = &contacts[0];
            for (const auto& contact : contacts)
            {
                if (contact.Depth > deepest->Depth)
                {
                    deepest = &contact;
                }
            }
            expected.Point = deepest->Point + Origin;
            expected.Normal = deepest->Normal;
            expected.Depth = deepest->Depth;
        }
        const Math::Vector3 localPoint = expected.Point - Origin;
        expected.SurfaceId = field.SampleSurface(localPoint.x, localPoint.z);
        return Result::Success;
    }

    PhysicsQueryDesc MakeQuery(const Terrain::TerrainHeightField& field, Kind kind, uint32_t index, Random& random)
    {
        const float x = random.Range(-29.0f, 29.0f);
        const float z = random.Range(-29.0f, 29.0f);
        const float height = field.SampleHeight((x + Origin.x) - Origin.x, (z + Origin.z) - Origin.z);
        const float radius = random.Range(0.45f, 0.9f);
        float offset = 0.0f;
        if (kind == Kind::RaycastClosest || kind == Kind::RaycastAll)
        {
            offset = index % 4 == 2 ? -1.0f : 4.0f;
        }
        else if (kind == Kind::SweepSphere || kind == Kind::SweepCapsule)
        {
            offset = index % 4 < 2 ? 4.0f : index % 4 == 2 ? -0.5f : 0.15f;
        }
        else
        {
            offset = index % 3 == 0 ? 3.0f : index % 3 == 1 ? -1.0f : 0.15f;
        }
        const Math::Vector3 center(x + Origin.x, height + Origin.y + offset, z + Origin.z);
        PhysicsQueryDesc query;
        query.Kind = kind;
        query.Ray = Math::Ray(center, index % 4 == 1 ? Math::Vector3::UnitY :
            Math::Vector3(index % 2 == 0 ? 0.12f : -0.08f, -1.0f, 0.09f));
        query.Sphere = Math::Sphere(center, radius);
        const float angle = index % 2 == 0 ? 0.0f : random.Range(-0.65f, 0.65f);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        query.Box = Math::OBB(center, Math::Vector3(radius, 0.65f, 0.55f),
            Math::Vector3(c, s, 0.0f), Math::Vector3(-s, c, 0.0f), Math::Vector3::UnitZ);
        const Math::Vector3 axis = index % 2 == 0 ? Math::Vector3(0.0f, 0.7f, 0.0f)
            : Math::Vector3(0.45f, 0.6f, 0.3f);
        query.Capsule = Math::Capsule(center - axis, center + axis, radius);
        query.Direction = index % 4 == 1 ? Math::Vector3::UnitY : Math::Vector3(0.08f, -1.0f, 0.05f);
        query.MaxDistance = (kind == Kind::RaycastClosest || kind == Kind::RaycastAll) && index % 4 == 3 ? 1.0f : 8.0f;
        query.bReportStartOverlap = index % 8 != 7;
        return query;
    }

    void TestQueries(PhysicsFixture& fixture, const Terrain::TerrainHeightField& field, ColliderComponent& collider)
    {
        const Kind kinds[] = {Kind::RaycastClosest, Kind::RaycastAll, Kind::OverlapSphere, Kind::OverlapBox,
            Kind::OverlapCapsule, Kind::SweepSphere, Kind::SweepCapsule};
        const char* names[] = {"RaycastClosest", "RaycastAll", "OverlapSphere", "OverlapBox",
            "OverlapCapsule", "SweepSphere", "SweepCapsule"};
        auto& scene = fixture.Engine.GetSceneQuery();
        Random random;
        for (uint32_t family = 0; family < 7; ++family)
        {
            Statistics stats;
            for (uint32_t index = 0; index < CaseCount; ++index)
            {
                const PhysicsQueryDesc query = MakeQuery(field, kinds[family], index, random);
                PhysicsQueryHit expected;
                bool bSuppressedStart = false;
                const Result expectedResult = DirectQuery(field, query, expected, bSuppressedStart);
                Container::VariableArray<PhysicsQueryHit> actual;
                const Result actualResult = scene.ExecuteQuery(query, actual);
                if (expectedResult == Result::Success)
                {
                    ++stats.Hit;
                }
                else if (expectedResult == Result::NoHit)
                {
                    ++stats.Miss;
                }
                else if (expectedResult == Result::IterationLimit)
                {
                    ++stats.IterationLimit;
                }
                stats.SuppressedStart += bSuppressedStart ? 1u : 0u;
                Check(actualResult == expectedResult, names[family], index, "当たりの状態");
                Check(actual.size() == (actualResult == Result::Success ? 1u : 0u), names[family], index, "当たりの件数");
                if (actualResult == Result::Success && expectedResult == Result::Success && actual.size() == 1)
                {
                    Check(actual[0].Collider == collider.GetColliderHandle(), names[family], index, "コライダー");
                    CompareHit(field, actual[0], expected, kinds[family], names[family], index, stats);
                }
            }
            Check(stats.Hit >= 3 && stats.Miss >= 3, names[family], CaseCount, "当たり・外れの件数不足");
            Check(stats.SurfaceMask == 7, names[family], CaseCount, "3 層に当たっていない");
            if (kinds[family] == Kind::SweepSphere || kinds[family] == Kind::SweepCapsule)
            {
                Check(stats.SuppressedStart >= 1, names[family], CaseCount, "始点の重なりの抑制が未検証");
            }
            std::cout << names[family] << " samples=" << CaseCount << " hits=" << stats.Hit << " misses=" << stats.Miss
                << " iteration_limit=" << stats.IterationLimit << " compared=" << stats.Compared
                << " suppressed_start=" << stats.SuppressedStart << " max_distance_error=" << stats.MaxDistanceError
                << " max_point_error=" << stats.MaxPointError << " max_depth_error=" << stats.MaxDepthError
                << " min_normal_dot=" << stats.MinNormalDot << std::endl;
        }

        const float x = 0.37f;
        const float z = 0.63f;
        const float height = field.SampleHeight(x, z);
        PhysicsQueryDesc modern;
        modern.Kind = Kind::RaycastClosest;
        modern.Ray = Math::Ray(Origin + Math::Vector3(x, height + 4.0f, z), Math::Vector3(0.0f, -1.0f, 0.0f));
        modern.MaxDistance = 8.0f;
        Container::VariableArray<PhysicsQueryHit> modernHits;
        PhysicsRaycastHit legacyRay;
        const Result modernRayResult = scene.ExecuteQuery(modern, modernHits);
        const Result legacyRayResult = scene.Raycast(modern.Ray, modern.MaxDistance, legacyRay);
        Check(modernRayResult == Result::Success && legacyRayResult == Result::Success && modernHits.size() == 1,
            "旧 Raycast", 0, "当たり");
        if (modernRayResult == Result::Success && legacyRayResult == Result::Success && modernHits.size() == 1)
        {
            const float pointError = (legacyRay.Point - modernHits[0].Point).Length();
            Check(pointError <= Tolerance, "旧 Raycast", 0, "点");
            Check(legacyRay.SurfaceId == modernHits[0].SurfaceId, "旧 Raycast", 0, "表面番号");
            std::cout << "LegacyRaycast compared=1 max_point_error=" << pointError << std::endl;
        }
        modern.Kind = Kind::OverlapSphere;
        modern.Sphere = Math::Sphere(Origin + Math::Vector3(x, height + 0.1f, z), 0.7f);
        Container::VariableArray<PhysicsOverlapHit> legacyOverlaps;
        const Result modernOverlapResult = scene.ExecuteQuery(modern, modernHits);
        const Result legacyOverlapResult = scene.OverlapSphere(modern.Sphere, legacyOverlaps);
        Check(modernOverlapResult == Result::Success && legacyOverlapResult == Result::Success
            && modernHits.size() == 1 && legacyOverlaps.size() == 1, "旧 OverlapSphere", 0, "当たり");
        if (modernHits.size() == 1 && legacyOverlaps.size() == 1)
        {
            const float pointError = (legacyOverlaps[0].Contact.Point - modernHits[0].Point).Length();
            Check(pointError <= Tolerance, "旧 OverlapSphere", 0, "点");
            Check(legacyOverlaps[0].SurfaceId == modernHits[0].SurfaceId, "旧 OverlapSphere", 0, "表面番号");
            std::cout << "LegacyOverlapSphere compared=1 max_point_error=" << pointError << std::endl;
        }
    }

    void TestRockSurface(PhysicsFixture& fixture)
    {
        Entity* rock = nullptr;
        auto* collider = fixture.CreateCollider(Math::Transform(Math::Vector3(90.0f, 2.0f, 0.0f)), rock);
        assert(collider->SetBox(Math::Vector3(1.0f)) == EPhysicsResult::Success);
        assert(collider->SetSurfaceId(5) == EPhysicsResult::Success);
        fixture.Publish();
        PhysicsQueryDesc query;
        query.Ray = Math::Ray(Math::Vector3(90.0f, 8.0f, 0.0f), Math::Vector3(0.0f, -1.0f, 0.0f));
        query.MaxDistance = 10.0f;
        Container::VariableArray<PhysicsQueryHit> hits;
        const Result result = fixture.Engine.GetSceneQuery().ExecuteQuery(query, hits);
        Check(result == Result::Success && hits.size() == 1, "岩の表面番号", 0, "当たり");
        if (hits.size() == 1)
        {
            Check(hits[0].Collider == collider->GetColliderHandle() && hits[0].SurfaceId == 5,
                "岩の表面番号", 0, "コライダーと表面番号");
        }
    }

    void TestTransformRejection(PhysicsFixture& fixture,
        const Container::TSharedPtr<const Terrain::TerrainHeightField>& field, Entity& terrain, ColliderComponent& collider)
    {
        const Math::Quaternion rotation(Math::Vector3::UnitY, 0.5235987756f);
        Entity* invalid = nullptr;
        auto* rotated = fixture.CreateCollider(Math::Transform(Origin, rotation), invalid);
        Check(rotated->SetHeightField(field) == EPhysicsResult::InvalidArgument, "回転の拒否", 0, "設定結果");
        auto* scaled = fixture.CreateCollider(Math::Transform(Origin, Math::Quaternion::Identity,
            Math::Vector3(2.0f, 1.0f, 1.0f)), invalid);
        Check(scaled->SetHeightField(field) == EPhysicsResult::InvalidArgument, "拡大の拒否", 0, "設定結果");
        auto& scene = fixture.Engine.GetSceneQuery();
        const float h = field->SampleHeight(0.37f, 0.63f);
        PhysicsQueryDesc query;
        query.Ray = Math::Ray(Origin + Math::Vector3(0.37f, h + 5.0f, 0.63f), Math::Vector3(0.0f, -1.0f, 0.0f));
        query.MaxDistance = 10.0f;
        Container::VariableArray<PhysicsQueryHit> hits;
        PhysicsShapeProxy proxy;
        Check(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics, collider.GetColliderHandle(), proxy),
            "変換変更", 0, "変更前のプロキシ");
        Check(scene.ExecuteQuery(query, hits) == Result::Success && hits.size() == 1,
            "変換変更", 0, "変更前の当たり");
        terrain.SetWorldTransform(Math::Transform(Origin, rotation));
        assert(scene.RefreshDynamicSnapshot() == Result::Success);
        Check(!PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics, collider.GetColliderHandle(), proxy),
            "変換変更", 1, "回転後のプロキシ");
        Check(scene.ExecuteQuery(query, hits) == Result::NoHit && hits.empty(), "変換変更", 1, "回転後の当たり");
        terrain.SetWorldTransform(Math::Transform(Origin));
        assert(scene.RefreshDynamicSnapshot() == Result::Success);
        Check(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics, collider.GetColliderHandle(), proxy),
            "変換変更", 2, "復帰後のプロキシ");
        Check(scene.ExecuteQuery(query, hits) == Result::Success && hits.size() == 1,
            "変換変更", 2, "復帰後の当たり");
    }

    void TestGround(PhysicsFixture& fixture, const Container::TSharedPtr<const Terrain::TerrainHeightField>& field)
    {
        auto& system = fixture.Engine.GetTerrainSystem();
        const Terrain::TerrainHandle handle = system.RegisterTerrain(field);
        assert(handle.IsValid());
        const float x = 0.37f;
        const float z = 0.63f;
        const float ground = field->SampleHeight(x, z);
        const float rockTop = ground + 1.0f;
        Entity* rock = nullptr;
        auto* collider = fixture.CreateCollider(Math::Transform(Math::Vector3(x, rockTop - 0.5f, z)), rock);
        assert(collider->SetBox(Math::Vector3(0.7f, 0.5f, 0.7f)) == EPhysicsResult::Success);
        assert(collider->SetSurfaceId(5) == EPhysicsResult::Success);
        fixture.Publish();
        auto& scene = fixture.Engine.GetSceneQuery();
        GroundQueryHit hit;
        float maxHeightError = 0.0f;
        uint32_t hits = 0;
        uint32_t misses = 0;
        Result result = scene.QueryGround(Math::Vector3(x, rockTop + 3.0f, z), 10.0f, hit);
        const auto countResult = [&]()
        {
            hits += result == Result::Success ? 1u : 0u;
            misses += result == Result::NoHit ? 1u : 0u;
        };
        countResult();
        maxHeightError = std::fmax(maxHeightError, std::fabs(hit.Point.y - rockTop));
        Check(result == Result::Success && hit.bHit && !hit.bTerrain && hit.Collider == collider->GetColliderHandle()
            && std::fabs(hit.Point.y - rockTop) <= Tolerance && hit.SurfaceId == 5, "QueryGround", 0, "岩の上面");
        const float groundX = 12.3f;
        const float groundZ = -7.4f;
        const float terrainHeight = field->SampleHeight(groundX, groundZ);
        result = scene.QueryGround(Math::Vector3(groundX, terrainHeight + 3.0f, groundZ), 10.0f, hit);
        countResult();
        maxHeightError = std::fmax(maxHeightError, std::fabs(hit.Point.y - terrainHeight));
        Check(result == Result::Success && hit.bHit && hit.bTerrain
            && std::fabs(hit.Point.y - terrainHeight) <= Tolerance
            && hit.SurfaceId == field->SampleSurface(groundX, groundZ), "QueryGround", 1, "地形");
        result = scene.QueryGround(Math::Vector3(groundX, terrainHeight + 3.0f, groundZ), 0.5f, hit);
        countResult();
        Check(result == Result::NoHit && !hit.bHit, "QueryGround", 2, "届かない距離");
        rock->SetWorldTransform(Math::Transform(Math::Vector3(x, ground - 0.5f, z)));
        assert(scene.RefreshDynamicSnapshot() == Result::Success);
        result = scene.QueryGround(Math::Vector3(x, ground + 3.0f, z), 10.0f, hit);
        countResult();
        maxHeightError = std::fmax(maxHeightError, std::fabs(hit.Point.y - ground));
        Check(result == Result::Success && hit.bHit && !hit.bTerrain && hit.Collider == collider->GetColliderHandle()
            && std::fabs(hit.Point.y - ground) <= Tolerance, "QueryGround", 3, "同じ高さでは物理を優先");
        std::cout << "QueryGround samples=4 hits=" << hits << " misses=" << misses
            << " max_height_error=" << maxHeightError << std::endl;
        system.UnregisterTerrain(handle);
        Check(!system.GetTerrain(handle), "QueryGround", 4, "地形の登録解除");
    }

    Container::TSharedPtr<const Terrain::TerrainHeightField> MakeMotionField(bool bTerraces)
    {
        Terrain::TerrainGridLayout layout;
        layout.Columns = GridSize;
        layout.Rows = GridSize;
        layout.FirstX = -32.0;
        layout.FirstZ = -32.0;
        layout.Spacing = 1.0;
        Container::VariableArray<float> heights(GridSize * GridSize);
        const float levels[] = {0.0f, 1.0f, 2.0f, 0.5f, 1.5f};
        for (uint32_t row = 0; row < GridSize; ++row)
        {
            for (uint32_t column = 0; column < GridSize; ++column)
            {
                const float x = static_cast<float>(column) - 32.0f;
                float height = levels[0];
                if (bTerraces)
                {
                    // 幅 8 m の台地を幅 4 m の斜面でつなぐ。落下位置は台地の中央。
                    for (uint32_t band = 0; band < 4; ++band)
                    {
                        const float blend = std::fmin(1.0f, std::fmax(0.0f, (x - (-20.0f + 12.0f * band)) / 4.0f));
                        height += (levels[band + 1] - levels[band]) * blend;
                    }
                }
                else
                {
                    // 導関数は 0.272～0.488（約 15～26 度）。既定の歩ける上限 50 度より緩い。
                    height = 0.38f * x + 0.18f * std::sin(0.6f * x);
                }
                heights[row * GridSize + column] = height;
            }
        }
        Container::AnsiString error;
        auto field = Terrain::TerrainHeightField::CreateFromSamples(
            layout, Container::Span<const float>(heights), nullptr, 8, error);
        if (!field)
        {
            std::cerr << "動的試験の高さ場の作成に失敗: " << error.c_str() << std::endl;
        }
        assert(field);
        return field;
    }

    void TestFallingBodies(PhysicsFixture& fixture)
    {
        constexpr uint32_t BodyCount = 20;
        constexpr uint32_t Steps = 240;
        constexpr float DeltaTime = 1.0f / 60.0f;
        const Math::Vector3 origin(120.0f, 3.0f, 0.0f);
        const auto field = MakeMotionField(true);
        Entity* terrain = nullptr;
        auto* ground = fixture.CreateCollider(Math::Transform(origin), terrain);
        assert(ground->SetHeightField(field) == EPhysicsResult::Success);
        struct FallingBody
        {
            Entity* Owner = nullptr;
            ColliderComponent* Collider = nullptr;
            RigidBodyComponent* Body = nullptr;
            PhysicsCallbackHandle Callback;
            float FootDistance = 0.0f;
            uint32_t Hits = 0;
            float MaxSink = 0.0f;
        };
        FallingBody bodies[BodyCount];
        for (uint32_t index = 0; index < BodyCount; ++index)
        {
            auto& falling = bodies[index];
            const float radius = 0.2f + 0.1f * (index % 3);
            const float halfHeight = index < 10 ? 0.0f : 0.25f + 0.15f * (index % 3);
            falling.FootDistance = radius + halfHeight;
            const float x = -24.0f + 12.0f * (index / 4);
            const float z = -9.0f + 6.0f * (index % 4);
            const float dropHeight = 1.0f + 0.5f * (index % 5);
            falling.Collider = fixture.CreateCollider(Math::Transform(origin + Math::Vector3(
                x, field->SampleHeight(x, z) + falling.FootDistance + dropHeight, z)), falling.Owner);
            assert((index < 10 ? falling.Collider->SetSphere(radius)
                : falling.Collider->SetCapsule(radius, halfHeight)) == EPhysicsResult::Success);
            falling.Body = fixture.World.CreateComponent<RigidBodyComponent>(falling.Owner);
            assert(falling.Body);
            assert(falling.Body->SetBodyType(EPhysicsBodyType::Dynamic) == EPhysicsResult::Success);
            assert(falling.Collider->AddOnHit(Core::Delegate<void, const PhysicsContactEvent&>(
                [&falling, ground, index](const PhysicsContactEvent& event)
                {
                    Check(event.Self == falling.Collider->GetColliderHandle(), "落下 Hit", index, "受信者");
                    Check(event.Other == ground->GetColliderHandle(), "落下 Hit", index, "相手が地形");
                    Check(std::isfinite(event.NormalImpulse) && event.NormalImpulse > 0.0f,
                        "落下 Hit", index, "正の法線撃力");
                    if (event.Other == ground->GetColliderHandle())
                    {
                        ++falling.Hits;
                    }
                }), falling.Callback) == EPhysicsResult::Success);
        }
        for (uint32_t step = 0; step < Steps; ++step)
        {
            fixture.Physics->StepPreFixed(DeltaTime);
            fixture.Physics->StepFixed(DeltaTime);
            for (uint32_t index = 0; index < BodyCount; ++index)
            {
                auto& falling = bodies[index];
                const auto position = falling.Owner->GetWorldTransform().position;
                const float height = origin.y + field->SampleHeight(position.x - origin.x, position.z - origin.z);
                const float gap = position.y - falling.FootDistance - height;
                falling.MaxSink = std::fmax(falling.MaxSink, -gap);
                Check(std::isfinite(gap) && gap >= -0.05f, "落下の沈み", step * BodyCount + index, "下端が地表より 0.05 m 以内");
            }
        }
        for (uint32_t family = 0; family < 2; ++family)
        {
            float maxError = 0.0f;
            float maxSpeed = 0.0f;
            float maxSink = 0.0f;
            uint32_t hitBodies = 0;
            uint32_t hits = 0;
            for (uint32_t index = family * 10; index < (family + 1) * 10; ++index)
            {
                const auto& falling = bodies[index];
                const auto position = falling.Owner->GetWorldTransform().position;
                const float height = origin.y + field->SampleHeight(position.x - origin.x, position.z - origin.z);
                const float error = std::fabs(position.y - falling.FootDistance - height);
                const float speed = falling.Body->GetLinearVelocity().Length();
                maxError = std::fmax(maxError, error);
                maxSpeed = std::fmax(maxSpeed, speed);
                maxSink = std::fmax(maxSink, falling.MaxSink);
                hits += falling.Hits;
                hitBodies += falling.Hits > 0 ? 1u : 0u;
                Check(std::isfinite(error) && error <= 0.02f, "落下の静止", index, "地表との差");
                Check(std::isfinite(speed) && speed <= 0.05f, "落下の静止", index, "速さ");
                Check(falling.Hits > 0, "落下 Hit", index, "地形との Hit がない");
            }
            std::cout << (family == 0 ? "FallingSphere" : "FallingCapsule") << " samples=10 steps=" << Steps
                << " max_height_error=" << maxError << " max_speed=" << maxSpeed << " hits=" << hits
                << " hit_bodies=" << hitBodies << " max_sink=" << maxSink << std::endl;
        }
        // コールバックが参照する測定値はこの関数の寿命なので、復帰前に解除する。
        for (auto& falling : bodies)
        {
            assert(falling.Collider->RemoveOnHit(falling.Callback) == EPhysicsResult::Success);
        }
    }

    void TestCharacterTerrainMove(PhysicsFixture& fixture)
    {
        constexpr uint32_t StepsPerLeg = 180;
        constexpr float DeltaTime = 1.0f / 60.0f;
        constexpr float Speed = 3.0f;
        constexpr float Radius = 0.3f;
        constexpr float HalfHeight = 0.5f;
        const Math::Vector3 origin(240.0f, 4.0f, 0.0f);
        const auto field = MakeMotionField(false);
        Entity* terrain = nullptr;
        auto* collider = fixture.CreateCollider(Math::Transform(origin), terrain);
        assert(collider->SetHeightField(field) == EPhysicsResult::Success);
        fixture.Publish();
        PhysicsShapeProxy proxy;
        assert(PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics, collider->GetColliderHandle(), proxy));
        CharacterMoveSettings settings;
        // 斜面では球面の接点と真下の地表がずれる。その幾何差は 0.05 m に収まり、
        // スイープで手前に止める skin の鉛直成分の上限だけ別途加える。
        const float gapLimit = 0.05f + settings.SkinWidth / std::cos(settings.MaximumSlopeDegrees * 0.01745329252f);
        const float startX = -5.0f;
        const float startHeight = field->SampleHeight(startX, 0.0f);
        const Math::Vector3 center = origin + Math::Vector3(startX, startHeight + HalfHeight + Radius + settings.SkinWidth, 0.0f);
        CharacterMoveRequest request;
        request.Shape = Math::Capsule(center - Math::Vector3::UnitY * HalfHeight,
            center + Math::Vector3::UnitY * HalfHeight, Radius);
        request.bWasGrounded = true;
        CharacterMoveScratch scratch;
        for (uint32_t leg = 0; leg < 2; ++leg)
        {
            const float direction = leg == 0 ? 1.0f : -1.0f;
            const float legStart = request.Shape.PointA.x;
            float maxSink = 0.0f;
            float maxFloat = 0.0f;
            uint32_t groundedSteps = 0;
            for (uint32_t step = 0; step < StepsPerLeg; ++step)
            {
                request.Displacement = Math::Vector3(direction * Speed * DeltaTime, -0.01f, 0.0f);
                CharacterMoveResult result;
                const Result status = CharacterMover::Move(Container::Span<const PhysicsShapeProxy>(&proxy, 1),
                    settings, request, scratch, result);
                Check(status == Result::Success, "キャラクター移動", leg * StepsPerLeg + step, "問い合わせの成功");
                if (status != Result::Success)
                {
                    break;
                }
                Check(!result.bStuck, "キャラクター移動", leg * StepsPerLeg + step, "押し戻しで停止");
                Check(result.bGrounded && result.GroundCollider == collider->GetColliderHandle(),
                    "キャラクター移動", leg * StepsPerLeg + step, "地形との接地の継続");
                const auto movedCenter = (result.Shape.PointA + result.Shape.PointB) * 0.5f;
                const float height = origin.y + field->SampleHeight(movedCenter.x - origin.x, movedCenter.z - origin.z);
                const float gap = movedCenter.y - HalfHeight - Radius - height;
                Check(std::isfinite(gap), "キャラクター移動", leg * StepsPerLeg + step, "有限の地表差");
                if (result.bGrounded)
                {
                    ++groundedSteps;
                    maxSink = std::fmax(maxSink, -gap);
                    maxFloat = std::fmax(maxFloat, gap);
                    Check(gap >= -gapLimit && gap <= gapLimit, "キャラクター接地", leg * StepsPerLeg + step, "沈み・浮きの上限");
                }
                request.Shape = result.Shape;
                request.bWasGrounded = result.bGrounded;
            }
            const float distance = direction * (request.Shape.PointA.x - legStart);
            const float requested = Speed * DeltaTime * StepsPerLeg;
            Check(groundedSteps == StepsPerLeg, "キャラクター移動", leg, "全ステップの接地");
            Check(std::isfinite(distance) && distance >= 0.8f * requested, "キャラクター移動", leg, "要求の 8 割以上進む");
            std::cout << (leg == 0 ? "CharacterUphill" : "CharacterDownhill") << " steps=" << StepsPerLeg
                << " grounded_steps=" << groundedSteps << " max_sink=" << maxSink << " max_float=" << maxFloat
                << " gap_limit=" << gapLimit << " distance=" << distance << " requested_distance=" << requested << std::endl;
        }
    }

}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::cout << std::setprecision(9);
    const auto field = MakeField();
    PhysicsFixture fixture;
    Entity* terrain = nullptr;
    auto* collider = fixture.CreateCollider(Math::Transform(Origin), terrain);
    assert(collider->SetHeightField(field) == EPhysicsResult::Success);
    assert(collider->SetSurfaceId(5) == EPhysicsResult::Success);
    fixture.Publish();
    TestQueries(fixture, *field, *collider);
    TestRockSurface(fixture);
    TestTransformRejection(fixture, field, *terrain, *collider);
    const ColliderHandle terrainCollider = collider->GetColliderHandle();
    fixture.World.Finalize();
    fixture.World.Initialize();
    assert(fixture.Engine.GetSceneQuery().RefreshDynamicSnapshot() == Result::Success);
    PhysicsShapeProxy proxy;
    Check(!PhysicsModuleTestAccess::CopyPublishedProxy(*fixture.Physics, terrainCollider, proxy),
        "QueryGround", 0, "物理の地形を除去");
    TestGround(fixture, field);
    TestFallingBodies(fixture);
    TestCharacterTerrainMove(fixture);
    std::cout << "PhysicsHeightFieldColliderTest 不一致=" << FailureCount << std::endl;
    assert(FailureCount == 0);
    return FailureCount == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
