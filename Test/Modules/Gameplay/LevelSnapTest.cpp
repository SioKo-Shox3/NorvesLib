// 地面への吸着、空振り時の不変性、親子のキャッシュ更新と離れの検出を反証する。
#include "Gameplay/Level/LevelMarkerSnap.h"

#include "Engine/Engine.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Scene/SceneQuery.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    namespace C = NorvesLib::Core;
    namespace M = NorvesLib::Math;
    namespace L = NorvesLib::Modules::Gameplay::Level;
    namespace P = NorvesLib::Modules::Physics;
    namespace Snap = C::Scene::LevelSnap;

#define SNAP_CHECK(expr) \
    do \
    { \
        if (!(expr)) \
        { \
            std::fprintf(stderr, "吸着試験の検査失敗 %d 行目: %s\n", __LINE__, #expr); \
            std::abort(); \
        } \
    } while (false)

    bool Near(float actual, float expected)
    {
        return std::fabs(actual - expected) <= 1e-4f;
    }

    void ExpectVector(const M::Vector3& actual, const M::Vector3& expected, const char* context)
    {
        if (!Near(actual.x, expected.x) || !Near(actual.y, expected.y) || !Near(actual.z, expected.z))
        {
            std::fprintf(stderr, "位置または法線が不一致（%s）: 実際=(%g,%g,%g)、期待=(%g,%g,%g)\n",
                context, actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
            std::abort();
        }
    }

    struct PhysicsFixture
    {
        PhysicsFixture()
            : Registry(C::Module::GetModuleRegistry())
        {
            Physics = P::RegisterPhysicsModule(Registry);
            SNAP_CHECK(Physics != nullptr);
            SNAP_CHECK(Registry.InstallAll(Engine));
            World.Initialize();
            C::Entity* ground = World.SpawnEntity<C::Entity>();
            SNAP_CHECK(ground != nullptr);
            ground->SetWorldTransform(M::Transform(M::Vector3(0.0f, -0.5f, 0.0f)));
            auto* collider = World.CreateComponent<P::ColliderComponent>(ground);
            SNAP_CHECK(collider != nullptr);
            SNAP_CHECK(collider->SetBox(M::Vector3(20.0f, 0.5f, 20.0f)) == P::EPhysicsResult::Success);
            SNAP_CHECK(collider->GetColliderHandle().IsValid());
            // 最初の固定ステップで地面を公開し、追加のスナップショット更新は行わない。
            World.UpdateWorldTransforms();
            Physics->StepPreFixed(1.0f / 60.0f);
            Physics->StepFixed(1.0f / 60.0f);
        }

        ~PhysicsFixture()
        {
            World.Finalize();
            Registry.ShutdownAll(Engine);
        }

        L::LevelMarkerComponent* CreateMarker(const M::Vector3& localPosition,
            float groundOffset, bool bSnapToGround = true, C::Entity* parent = nullptr)
        {
            C::Entity* entity = World.SpawnEntity<C::Entity>(parent);
            SNAP_CHECK(entity != nullptr);
            entity->SetLocalPosition(localPosition.x, localPosition.y, localPosition.z);
            auto* marker = World.CreateComponent<L::LevelMarkerComponent>(entity);
            SNAP_CHECK(marker != nullptr);
            marker->getbSnapToGround() = bSnapToGround;
            marker->getGroundOffset() = groundOffset;
            return marker;
        }

        C::Entity* CreatePoint(C::Entity* parent, const M::Vector3& localPosition)
        {
            C::Entity* point = World.SpawnEntity<C::Entity>(parent);
            SNAP_CHECK(point != nullptr);
            point->SetLocalPosition(localPosition.x, localPosition.y, localPosition.z);
            SNAP_CHECK(World.CreateComponent<L::ScentTrailPointComponent>(point) != nullptr);
            return point;
        }

        C::Module::ModuleRegistry& Registry;
        P::IPhysicsModule* Physics = nullptr;
        C::Engine::Engine Engine;
        C::World World;
    };
} // namespace

int main()
{
    PhysicsFixture fixture;
    auto& world = fixture.World;
    const auto& query = fixture.Engine.GetSceneQuery();
    constexpr float maxDrop = 50.0f;

    // 1. 地面の高さと法線、マーカー自身のオフセットを確認する。
    auto* marker = fixture.CreateMarker(M::Vector3(1.0f, 10.0f, 2.0f), 0.25f);
    C::Entity* owner = marker->GetOwner();
    M::Vector3 position;
    M::Vector3 normal;
    SNAP_CHECK(Snap::SnapToGround(query, M::Vector3(1.0f, 10.0f, 2.0f), position, normal, maxDrop));
    ExpectVector(position, M::Vector3(1.0f, 0.0f, 2.0f), "地面の交点");
    ExpectVector(normal, M::Vector3(0.0f, 1.0f, 0.0f), "地面の法線");
    const auto first = L::ReSnapAllLevelMarkers(world, query, maxDrop);
    ExpectVector(owner->GetPosition(), M::Vector3(1.0f, 0.25f, 2.0f), "地面とオフセット");
    SNAP_CHECK(first.Snapped == 1 && first.Missed == 0);
    const M::Vector3 firstPosition = owner->GetPosition();

    // 2. 地面が無い問い合わせの出力と、吸着できないマーカーの位置を保つ。
    const M::Vector3 sentinelPosition(71.0f, 72.0f, 73.0f);
    const M::Vector3 sentinelNormal(-11.0f, -12.0f, -13.0f);
    position = sentinelPosition;
    normal = sentinelNormal;
    const M::Vector3 noGroundPosition(500.0f, 10.0f, 2.0f);
    SNAP_CHECK(!Snap::SnapToGround(query, noGroundPosition, position, normal, maxDrop));
    ExpectVector(position, sentinelPosition, "空振りの位置出力");
    ExpectVector(normal, sentinelNormal, "空振りの法線出力");
    auto* missedMarker = fixture.CreateMarker(noGroundPosition, 0.25f);
    const auto second = L::ReSnapAllLevelMarkers(world, query, maxDrop);
    SNAP_CHECK(second.Snapped == 1 && second.Missed == 1);
    ExpectVector(missedMarker->GetOwner()->GetPosition(), noGroundPosition, "地面の無いマーカー");

    // 3. 冪等性、吸着無効の除外と、上空の親を動かした後の子の吸着を確認する。
    ExpectVector(owner->GetPosition(), firstPosition, "二回目の吸着の冪等性");
    missedMarker->getbSnapToGround() = false;
    auto* disabled = fixture.CreateMarker(M::Vector3(-3.0f, 7.0f, 0.0f), 0.75f, false);
    SNAP_CHECK(world.CreateComponent<L::ScentTrailComponent>(disabled->GetOwner()) != nullptr);
    C::Entity* disabledPoint = fixture.CreatePoint(disabled->GetOwner(), M::Vector3(0.0f, 4.0f, 0.0f));
    C::Entity* orphan = fixture.CreatePoint(nullptr, M::Vector3(-5.0f, 6.0f, 0.0f));
    C::Entity* ancestor = world.SpawnEntity<C::Entity>();
    SNAP_CHECK(ancestor != nullptr);
    ancestor->SetLocalPosition(2.0f, 3.0f, 1.0f);
    auto* trailMarker = fixture.CreateMarker(M::Vector3(2.0f, 8.0f, -1.0f), 0.4f, true, ancestor);
    C::Entity* trail = trailMarker->GetOwner();
    SNAP_CHECK(world.CreateComponent<L::ScentTrailComponent>(trail) != nullptr);
    C::Entity* points[] = {
        fixture.CreatePoint(trail, M::Vector3(-1.0f, 2.0f, 0.0f)),
        fixture.CreatePoint(trail, M::Vector3(0.0f, 5.0f, 1.0f)),
        fixture.CreatePoint(trail, M::Vector3(1.0f, 9.0f, -1.0f))};
    world.UpdateWorldTransforms();
    // キャッシュを確定した後に祖先を動かし、入口での最初の更新も必要にする。
    ancestor->SetLocalPosition(3.0f, 4.0f, 2.0f);
    const M::Vector3 expectedPoints[] = {
        M::Vector3(4.0f, 0.4f, 1.0f), M::Vector3(5.0f, 0.4f, 2.0f), M::Vector3(6.0f, 0.4f, 0.0f)};
    const auto withTrail = L::ReSnapAllLevelMarkers(world, query, maxDrop);
    ExpectVector(trail->GetPosition(), M::Vector3(5.0f, 0.4f, 1.0f), "上空から動いた痕跡の親");
    for (size_t index = 0; index < 3; ++index)
    {
        ExpectVector(points[index]->GetPosition(), expectedPoints[index], "親の移動後に吸着した痕跡の点");
    }
    SNAP_CHECK(withTrail.Snapped == 5 && withTrail.Missed == 0);
    ExpectVector(disabled->GetOwner()->GetPosition(), M::Vector3(-3.0f, 7.0f, 0.0f), "吸着無効のマーカー");
    ExpectVector(disabledPoint->GetPosition(), M::Vector3(-3.0f, 11.0f, 0.0f), "吸着無効の親の痕跡");
    ExpectVector(orphan->GetPosition(), M::Vector3(-5.0f, 6.0f, 0.0f), "親の無い痕跡の点");
    const M::Vector3 trailPosition = trail->GetPosition();
    const auto repeated = L::ReSnapAllLevelMarkers(world, query, maxDrop);
    SNAP_CHECK(repeated.Snapped == 5 && repeated.Missed == 0);
    ExpectVector(owner->GetPosition(), firstPosition, "マーカーの繰り返し吸着");
    ExpectVector(trail->GetPosition(), trailPosition, "親の繰り返し吸着");
    for (size_t index = 0; index < 3; ++index)
    {
        ExpectVector(points[index]->GetPosition(), expectedPoints[index], "痕跡の繰り返し吸着");
    }
    C::Container::VariableArray<Snap::LevelSnapTarget> markerTargets;
    C::Container::VariableArray<Snap::LevelSnapTarget> pointTargets;
    markerTargets.push_back({nullptr, -1.0f});
    pointTargets.push_back({nullptr, -1.0f});
    L::CollectLevelSnapTargets(world, markerTargets, pointTargets);
    SNAP_CHECK(markerTargets.size() == 2 && pointTargets.size() == 3);
    SNAP_CHECK(markerTargets[0].Target == owner && markerTargets[1].Target == trail);
    for (size_t index = 0; index < 3; ++index)
    {
        SNAP_CHECK(pointTargets[index].Target == points[index]);
        SNAP_CHECK(Near(pointTargets[index].GroundOffset, 0.4f));
    }

    // 4. 許容内のずれは通し、許容外と地面の無い対象だけを報告する。
    C::Container::VariableArray<Snap::LevelSnapDeviation> deviations;
    deviations.push_back({});
    SNAP_CHECK(L::FindLevelMarkerSnapDeviations(world, query, maxDrop, 0.05f, deviations));
    SNAP_CHECK(deviations.empty());
    owner->SetWorldTransform(M::Transform(M::Vector3(1.0f, 0.28f, 2.0f)));
    SNAP_CHECK(L::FindLevelMarkerSnapDeviations(world, query, maxDrop, 0.05f, deviations));
    SNAP_CHECK(deviations.empty());
    owner->SetWorldTransform(M::Transform(M::Vector3(1.0f, 0.75f, 2.0f)));
    SNAP_CHECK(!L::FindLevelMarkerSnapDeviations(world, query, maxDrop, 0.05f, deviations));
    SNAP_CHECK(deviations.size() == 1 && deviations[0].Target == owner && !deviations[0].bNoGround);
    SNAP_CHECK(Near(deviations[0].Distance, 0.5f));
    SNAP_CHECK(Near(deviations[0].ExpectedY, 0.25f) && Near(deviations[0].ActualY, 0.75f));
    owner->SetWorldTransform(M::Transform(firstPosition));
    missedMarker->getbSnapToGround() = true;
    SNAP_CHECK(!L::FindLevelMarkerSnapDeviations(world, query, maxDrop, 0.05f, deviations));
    SNAP_CHECK(deviations.size() == 1 && deviations[0].Target == missedMarker->GetOwner());
    SNAP_CHECK(deviations[0].bNoGround);
    ExpectVector(missedMarker->GetOwner()->GetPosition(), noGroundPosition, "離れ検査で位置を保持");

    std::puts("LevelSnapTest: 合格");
    return 0;
}
