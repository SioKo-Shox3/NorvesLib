// 実Worldの物理スナップショットで接地問い合わせの幾何・フィルター・状態を検証する。
#include "Animation/GroundQuery.h"
#include "Engine/Engine.h"
#include "Module/ModuleRegistry.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/IPhysicsModule.h"
#include "Physics/PhysicsModule.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/SceneQuery.h"
#include "Thread/Thread.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Animation;
    using namespace NorvesLib::Core::Module;
    using namespace NorvesLib::Core::Scene;
    using namespace NorvesLib::Modules::Physics;

#define GROUND_CHECK(expr)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "接地統合試験の検証失敗 行%d: %s\n", __LINE__, #expr);                                  \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

    constexpr double Pi = 3.14159265358979323846;
    constexpr float MaxDown = 10.0f;

    Engine::Engine& GetEngine()
    {
        static Engine::Engine* engine = new Engine::Engine();
        return *engine;
    }

    struct PhysicsFixture
    {
        PhysicsFixture()
            : Registry(GetModuleRegistry())
            , Engine(GetEngine())
        {
            Physics = RegisterPhysicsModule(Registry);
            GROUND_CHECK(Physics != nullptr);
            GROUND_CHECK(Registry.InstallAll(Engine));
            World.Initialize();
        }

        ~PhysicsFixture()
        {
            World.Finalize();
            Registry.ShutdownAll(Engine);
        }

        Entity* CreateBox(const Math::Vector3& center, const Math::Vector3& halfExtents,
            double angleDeg = 0.0, PhysicsCollisionMask layer = 1)
        {
            Entity* entity = World.SpawnEntity<Entity>();
            GROUND_CHECK(entity != nullptr);
            entity->SetWorldTransform(Math::Transform(center,
                Math::Quaternion(Math::Vector3::UnitZ, static_cast<float>(angleDeg * Pi / 180.0))));
            ColliderComponent* collider = World.CreateComponent<ColliderComponent>(entity);
            GROUND_CHECK(collider != nullptr);
            GROUND_CHECK(collider->SetBox(halfExtents) == EPhysicsResult::Success);
            GROUND_CHECK(collider->SetCollisionLayer(layer) == EPhysicsResult::Success);
            GROUND_CHECK(collider->GetColliderHandle().IsValid());
            return entity;
        }

        ModuleRegistry& Registry;
        Engine::Engine& Engine;
        IPhysicsModule* Physics = nullptr;
        Core::World World;
    };

    struct ExpectedProbe
    {
        Math::Vector3 Origin;
        double Height;
        double SlopeDeg;
        EGroundQueryStatus Status = EGroundQueryStatus::Hit;
    };

    double TopHeight(double centerX, double centerY, double halfHeight, double angleDeg, double x)
    {
        const double theta = angleDeg * Pi / 180.0;
        // Z軸の正回転では上面法線が(-sinθ, cosθ, 0)となり、右へ行くほど上がる。
        return centerY + halfHeight / std::cos(theta) + (x - centerX) * std::tan(theta);
    }

    GroundHit QueryChecked(const PhysicsGroundQuery& query, const Math::Vector3& origin,
        EGroundQueryStatus expectedStatus, float maxDown = MaxDown)
    {
        GroundHit hit;
        const EGroundQueryStatus status = query.Query(origin, maxDown, hit);
        GROUND_CHECK(status == hit.Status);
        GROUND_CHECK(status == expectedStatus);
        return hit;
    }

    void CheckSurface(const GroundHit& hit, const ExpectedProbe& expected)
    {
        GROUND_CHECK(hit.Status == expected.Status);
        GROUND_CHECK(std::fabs(static_cast<double>(hit.Point.x) - expected.Origin.x) <= 1e-3);
        GROUND_CHECK(std::fabs(static_cast<double>(hit.Point.z) - expected.Origin.z) <= 1e-3);
        GROUND_CHECK(std::fabs(static_cast<double>(hit.Point.y) - expected.Height) <= 1e-3);
        GROUND_CHECK(std::fabs(static_cast<double>(hit.Distance) - (expected.Origin.y - hit.Point.y)) <= 1e-3);
        const double x = hit.Normal.x, y = hit.Normal.y, z = hit.Normal.z;
        const double length = std::sqrt(x * x + y * y + z * z);
        GROUND_CHECK(std::fabs(length - 1.0) <= 1e-4);
        const double theta = expected.SlopeDeg * Pi / 180.0;
        const double dot = (-x * std::sin(theta) + y * std::cos(theta)) / length;
        const double normalAngleDeg = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / Pi;
        GROUND_CHECK(normalAngleDeg <= 0.5);
        const double slopeDeg = std::acos(std::clamp(y / length, -1.0, 1.0)) * 180.0 / Pi;
        GROUND_CHECK(std::fabs(slopeDeg - expected.SlopeDeg) <= 0.5);
    }

    void CheckSameVector(const Math::Vector3& actual, const Math::Vector3& expected)
    {
        GROUND_CHECK(std::fabs(actual.x - expected.x) <= 1e-5f);
        GROUND_CHECK(std::fabs(actual.y - expected.y) <= 1e-5f);
        GROUND_CHECK(std::fabs(actual.z - expected.z) <= 1e-5f);
    }
} // namespace

int main()
{
    PhysicsFixture fixture;
    fixture.CreateBox({0, 0, 0}, {2, .1f, 1}, 20.0);
    fixture.CreateBox({5.2f, .1f, 0}, {.2f, .1f, .6f});
    fixture.CreateBox({5.6f, .2f, 0}, {.2f, .2f, .6f});
    fixture.CreateBox({6, .3f, 0}, {.2f, .3f, .6f});
    fixture.CreateBox({10, -.25f, 0}, {1, .25f, 1});
    fixture.CreateBox({10, .35f, 0}, {.6f, .05f, .6f}, 0.0, 2);
    fixture.CreateBox({14, -.25f, 0}, {1, .25f, 1});
    Entity* trigger = fixture.CreateBox({14, .6f, 0}, {.6f, .1f, .6f});
    GROUND_CHECK(trigger->GetComponent<ColliderComponent>()->SetTrigger(true) == EPhysicsResult::Success);
    fixture.CreateBox({18, -.25f, 0}, {1, .25f, 1});
    Entity* self = fixture.CreateBox({18, .75f, 0}, {.5f, .25f, .5f});
    RigidBodyComponent* body = fixture.World.CreateComponent<RigidBodyComponent>(self);
    GROUND_CHECK(body != nullptr);
    GROUND_CHECK(body->SetBodyType(EPhysicsBodyType::Kinematic) == EPhysicsResult::Success);
    GROUND_CHECK(body->GetBodyHandle().IsValid());
    fixture.CreateBox({24, 0, 0}, {2, .1f, 1}, 60.0);

    PhysicsGroundQuerySettings settings;
    settings.LayerMask = 1;
    settings.MaxSlopeDeg = 50.0f;
    PhysicsGroundQuery query(fixture.Engine.GetSceneQuery(), settings);

    const ExpectedProbe expected[] = {
        {{-.75f, 5, .2f}, TopHeight(0, 0, .1, 20, -.75), 20},
        {{0, 5, .2f}, TopHeight(0, 0, .1, 20, 0), 20},
        {{.75f, 5, .2f}, TopHeight(0, 0, .1, 20, .75), 20},
        {{5.2f, 5, 0}, .1 + .1, 0},
        {{100, 5, 100}, 0, 0, EGroundQueryStatus::NoHit},
        {{5.6f, 5, 0}, .2 + .2, 0},
        {{24, 5, 0}, TopHeight(24, 0, .1, 60, 24), 60, EGroundQueryStatus::TooSteep},
        {{6, 5, 0}, .3 + .3, 0}
    };
    Container::VariableArray<GroundProbe> probes;
    for (const ExpectedProbe& item : expected)
    {
        probes.push_back(GroundProbe{item.Origin, MaxDown, {}});
    }

    QueryChecked(query, expected[0].Origin, EGroundQueryStatus::NotReady);
    query.QueryBatch(Container::Span<GroundProbe>(probes));
    for (const GroundProbe& probe : probes)
    {
        GROUND_CHECK(probe.Result.Status == EGroundQueryStatus::NotReady);
    }

    fixture.Physics->StepPreFixed(1.0f / 60.0f);
    fixture.Physics->StepFixed(1.0f / 60.0f);

    Container::VariableArray<GroundHit> individual;
    for (const ExpectedProbe& item : expected)
    {
        const GroundHit hit = QueryChecked(query, item.Origin, item.Status);
        if (item.Status != EGroundQueryStatus::NoHit)
        {
            CheckSurface(hit, item);
        }
        individual.push_back(hit);
    }
    query.QueryBatch(Container::Span<GroundProbe>(probes));
    for (size_t index = 0; index < probes.size(); ++index)
    {
        const GroundHit& batch = probes[index].Result;
        const GroundHit& single = individual[index];
        GROUND_CHECK(batch.Status == expected[index].Status && batch.Status == single.Status);
        CheckSameVector(batch.Point, single.Point);
        CheckSameVector(batch.Normal, single.Normal);
        GROUND_CHECK(std::fabs(batch.Distance - single.Distance) <= 1e-5f);
    }

    CheckSurface(QueryChecked(query, {10, 5, 0}, EGroundQueryStatus::Hit), {{10, 5, 0}, 0, 0});
    PhysicsGroundQuerySettings grassSettings = settings;
    grassSettings.LayerMask = 1 | 2;
    PhysicsGroundQuery grassQuery(fixture.Engine.GetSceneQuery(), grassSettings);
    CheckSurface(QueryChecked(grassQuery, {10, 5, 0}, EGroundQueryStatus::Hit), {{10, 5, 0}, .35 + .05, 0});
    CheckSurface(QueryChecked(query, {14, 5, 0}, EGroundQueryStatus::Hit), {{14, 5, 0}, 0, 0});
    CheckSurface(QueryChecked(query, {18, 5, 0}, EGroundQueryStatus::Hit), {{18, 5, 0}, .75 + .25, 0});
    PhysicsGroundQuerySettings selfSettings = settings;
    selfSettings.IgnoreBodies[0] = body->GetBodyHandle();
    PhysicsGroundQuery selfQuery(fixture.Engine.GetSceneQuery(), selfSettings);
    CheckSurface(QueryChecked(selfQuery, {18, 5, 0}, EGroundQueryStatus::Hit), {{18, 5, 0}, 0, 0});

    GroundHit workerHit;
    EGroundQueryStatus workerStatus = EGroundQueryStatus::Hit;
    Thread::Thread worker([&]() { workerStatus = query.Query(expected[0].Origin, MaxDown, workerHit); });
    worker.Join();
    GROUND_CHECK(workerStatus == EGroundQueryStatus::WrongThread);
    GROUND_CHECK(workerHit.Status == EGroundQueryStatus::WrongThread);
    CheckSurface(QueryChecked(query, expected[0].Origin, EGroundQueryStatus::Hit), expected[0]);

    QueryChecked(query, expected[0].Origin, EGroundQueryStatus::Unavailable, 0.0f);
    QueryChecked(query, expected[0].Origin, EGroundQueryStatus::Unavailable, -1.0f);

    std::puts("PhysicsGroundQueryIntegrationTest: 接地の幾何・バッチ・フィルター・状態の検証に成功しました");
    return EXIT_SUCCESS;
}
