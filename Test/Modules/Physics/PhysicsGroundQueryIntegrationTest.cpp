// 実Worldの物理スナップショットで接地問い合わせの幾何・フィルター・状態を検証する。
#include "Animation/GroundQuery.h"
#include "Animation/FootIKModifier.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/ResourceRegistry.h"
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

    constexpr size_t FootJointCount = 9;
    constexpr size_t FootLegCount = 2;
    constexpr float FootDeltaSeconds = 1.0f / 60.0f;
    constexpr float FootSole = 0.02f;

    void CheckFiniteFootPose(const LocalPose& pose, const SkeletalPoseSnapshot& snapshot)
    {
        GROUND_CHECK(pose.size() == FootJointCount);
        GROUND_CHECK(snapshot.BonePalette.size() == FootJointCount);
        GROUND_CHECK(snapshot.JointModelMatrices.size() == FootJointCount);
        for (const auto& joint : pose)
        {
            const float values[] = {joint.Translation.x, joint.Translation.y, joint.Translation.z,
                joint.Rotation.x, joint.Rotation.y, joint.Rotation.z, joint.Rotation.w,
                joint.Scale.x, joint.Scale.y, joint.Scale.z};
            for (float value : values)
            {
                GROUND_CHECK(std::isfinite(value));
            }
        }
        for (size_t joint = 0; joint < FootJointCount; ++joint)
        {
            for (float value : snapshot.BonePalette[joint].values)
            {
                GROUND_CHECK(std::isfinite(value));
            }
            for (float value : snapshot.JointModelMatrices[joint].values)
            {
                GROUND_CHECK(std::isfinite(value));
            }
        }
    }

    // 実 World を借り、物理部品を持たない二足・各四関節を Animator で駆動する。
    struct FootIKFixture : IClipResolver
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        Container::TSharedPtr<AnimGraphResource> Graph;
        Container::TSharedPtr<FootIKModifier> Modifier;
        Core::World& World;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Component::AnimatorComponent* Animator = nullptr;

        explicit FootIKFixture(Core::World& world) : World(world)
        {
            GROUND_CHECK(Registry.Initialize());
            Skeleton = Registry.CreateTransient<SkeletonResource>("GroundFootSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("GroundFootMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("GroundFootClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("GroundFootAsset");
            Graph = Registry.CreateTransient<AnimGraphResource>("GroundFootGraph");
            GROUND_CHECK(Skeleton && Mesh && Clip && Asset && Graph);

            const char* names[] = {"Root", "LeftHip", "LeftKnee", "LeftAnkle", "LeftToe",
                "RightHip", "RightKnee", "RightAnkle", "RightToe"};
            Container::VariableArray<Skeletal::SkeletalJoint> joints(FootJointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(FootJointCount);
            Container::VariableArray<Math::Matrix4x4> globals(FootJointCount);
            for (size_t index = 0; index < FootJointCount; ++index)
            {
                joints[index].Name = names[index];
                joints[index].ParentIndex = index == 0 ? -1 : (index == 1 || index == 5 ? 0 : int32_t(index - 1));
                JointTransform local;
                if (index > 0)
                {
                    switch ((index - 1) % 4)
                    {
                    case 0: local.Translation = {index == 1 ? -0.2f : 0.2f, 1.2f, 0}; break;
                    case 1: local.Translation = {0, -0.55f, 0.3f}; break;
                    case 2: local.Translation = {0, -0.65f, -0.3f}; break;
                    case 3: local.Translation = {0, 0, 0.12f}; break;
                    }
                }
                rest[index].Translation = {local.Translation.x, local.Translation.y, local.Translation.z};
                globals[index] = ToRowMatrix(local);
                if (joints[index].ParentIndex >= 0)
                {
                    globals[index] = globals[index] * globals[joints[index].ParentIndex];
                }
                const auto inverse = Math::MatrixUtils::Inverse(globals[index]);
                for (size_t element = 0; element < 16; ++element)
                {
                    joints[index].InverseBindMatrix[element] = inverse.values[element];
                }
            }
            Skeleton->SetJoints(std::move(joints));
            GROUND_CHECK(Skeleton->SetAuthorRestPose(rest));
            GROUND_CHECK(Skeleton->Load());

            Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 4;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
            GROUND_CHECK(Mesh->Load());
            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "idle";
            clip.DurationSeconds = 1;
            Skeletal::SkeletalAnimationChannel channel;
            channel.JointIndex = 0;
            channel.Path = Skeletal::SkeletalAnimationPath::Translation;
            channel.Samples.push_back({0, {0, 0, 0, 0}});
            channel.Samples.push_back({1, {0, 0, 0, 0}});
            clip.Channels.push_back(std::move(channel));
            Clip->SetClip(std::move(clip));
            GROUND_CHECK(Clip->Load());
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            GROUND_CHECK(Asset->Load());
            AnimGraphReport report;
            GROUND_CHECK(Graph->Compile(Container::String(
                R"({"version":1,"nodes":[{"id":"idle","type":"clip","clip":"idle"}],"root":"idle"})"),
                *Skeleton, *this, report));
            GROUND_CHECK(Graph->Load());
            Owner = World.SpawnEntity<Entity>();
            GROUND_CHECK(Owner != nullptr);
            MeshComponent = World.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = World.CreateComponent<Component::AnimatorComponent>(Owner);
            GROUND_CHECK(MeshComponent && Animator);
            MeshComponent->SetSkeletalAsset(Asset);
            GROUND_CHECK(Animator->SetGraph(Graph));
            GROUND_CHECK(Owner->GetComponent<ColliderComponent>() == nullptr);
            GROUND_CHECK(Owner->GetComponent<RigidBodyComponent>() == nullptr);

            FootIKSettings settings;
            settings.Ground.RayUp = 1;
            settings.Ground.RayDown = 2;
            settings.Ground.LayerMask = 1;
            for (size_t legIndex = 0; legIndex < FootLegCount; ++legIndex)
            {
                FootIKLegSettings leg;
                leg.Name = Identity(legIndex == 0 ? "Left" : "Right");
                leg.JointCount = 4;
                leg.bAutoPole = false;
                leg.Pole = Math::Vector3::UnitZ;
                leg.Sole = FootSole;
                for (int32_t joint = 0; joint < 4; ++joint)
                {
                    leg.Joints[joint] = int32_t(1 + legIndex * 4) + joint;
                }
                settings.Legs.push_back(leg);
            }
            Modifier = Container::MakeShared<FootIKModifier>(std::move(settings));
            GROUND_CHECK(Animator->AddModifier(Modifier));
        }

        ~FootIKFixture()
        {
            GROUND_CHECK(World.RemoveEntity(Owner));
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("idle") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        void Tick()
        {
            GROUND_CHECK(Animator->UpdateAnimation(FootDeltaSeconds));
            GROUND_CHECK(Animator->EvaluateAnimation());
            CheckFiniteFootPose(Animator->GetFinalPose(), MeshComponent->GetPoseSnapshot());
        }
    };

    void CheckFootFailure(const FootIKFixture& fixture, EGroundQueryStatus expected, const float* previous)
    {
        for (size_t leg = 0; leg < FootLegCount; ++leg)
        {
            const float weight = fixture.Modifier->GetLegWeight(leg);
            if (fixture.Modifier->GetLegStatus(leg) != expected || !std::isfinite(weight) ||
                !((previous[leg] == 0 && weight == 0) || weight < previous[leg]))
            {
                std::fprintf(stderr, "FootIK 失敗状態の検証: leg=%zu expected=%u actual=%u previous=%.9f weight=%.9f\n",
                    leg, unsigned(expected), unsigned(fixture.Modifier->GetLegStatus(leg)), previous[leg], weight);
                std::fflush(stderr);
            }
            GROUND_CHECK(fixture.Modifier->GetLegStatus(leg) == expected);
            GROUND_CHECK(std::isfinite(weight) && weight >= 0 && weight <= 1);
            GROUND_CHECK((previous[leg] == 0 && weight == 0) || weight < previous[leg]);
        }
    }

    void TestFootIKSurfaces(FootIKFixture& fixture, const PhysicsGroundQuery& query)
    {
        const Math::Vector3 positions[] = {{0, 0, 0}, {5.4f, 0, 0}};
        for (size_t surface = 0; surface < 2; ++surface)
        {
            fixture.Owner->SetWorldTransform(Math::Transform(positions[surface]));
            fixture.Modifier->Reset();
            for (int frame = 0; frame < 180; ++frame)
            {
                fixture.Tick();
                for (size_t leg = 0; leg < FootLegCount; ++leg)
                {
                    GROUND_CHECK(fixture.Modifier->GetLegStatus(leg) == EGroundQueryStatus::Hit);
                }
            }
            for (size_t leg = 0; leg < FootLegCount; ++leg)
            {
                Math::Matrix4x4 toeWorld;
                GROUND_CHECK(fixture.MeshComponent->TryGetJointWorldMatrix(uint32_t(4 + leg * 4), toeWorld));
                const auto toe = toeWorld.GetTranslationRow();
                const auto hit = QueryChecked(query, toe + Math::Vector3(0, 2, 0), EGroundQueryStatus::Hit, 4);
                const double error = std::fabs(double(toe.y) - (double(hit.Point.y) + FootSole));
                std::printf("FootIK 接地: surface=%zu leg=%zu toe=(%.6f,%.6f,%.6f) ground=%.6f sole=%.6f error=%.6f weight=%.6f\n",
                    surface, leg, toe.x, toe.y, toe.z, hit.Point.y, FootSole, error, fixture.Modifier->GetLegWeight(leg));
                std::fflush(stdout);
                GROUND_CHECK(fixture.Modifier->GetLegWeight(leg) > 0.99f);
                GROUND_CHECK(error <= 0.01);
            }
        }
    }

    void TestFootIKFailureStates(FootIKFixture& fixture, EGroundQueryStatus status)
    {
        // ワーカーでは独立した姿勢と FK の作業領域だけを書き換え、World の更新と競合させない。
        AnimGraphInstance instance;
        GROUND_CHECK(instance.Initialize(*fixture.Graph, *fixture.Skeleton, *fixture.Mesh, Math::Matrix4x4::Identity));
        GROUND_CHECK(instance.Update(FootDeltaSeconds) && instance.Evaluate());
        PoseScratch scratch;
        Container::VariableArray<Math::Matrix4x4> models;
        for (int frame = 0; frame < 8; ++frame)
        {
            const float previous[] = {fixture.Modifier->GetLegWeight(0), fixture.Modifier->GetLegWeight(1)};
            if (status == EGroundQueryStatus::WrongThread)
            {
                LocalPose pose = instance.GetLocalPose();
                SkeletalPoseSnapshot snapshot;
                PoseModifierContext context(fixture.Skeleton->GetPoseRuntime(), FootDeltaSeconds,
                    instance.Parameters(), instance, pose, scratch, models);
                context.MeshToWorld = Math::MatrixUtils::CreateWorldRowVector(
                    fixture.Owner->GetWorldTransform().position, Math::Quaternion::Identity, Math::Vector3::One);
                context.SetOwner(fixture.Owner);
                context.SetSkeletonAsset(fixture.Skeleton.get());
                bool bApplied = false;
                bool bBuilt = false;
                Thread::Thread worker([&]()
                {
                    bApplied = fixture.Modifier->Apply(pose, context);
                    bBuilt = instance.BuildPose(pose, scratch, snapshot);
                });
                worker.Join();
                GROUND_CHECK(bApplied && bBuilt);
                CheckFiniteFootPose(pose, snapshot);
            }
            else
            {
                fixture.Tick();
            }
            CheckFootFailure(fixture, status, previous);
        }
        std::printf("FootIK 問い合わせ失敗: status=%u weight=(%.6f,%.6f) 姿勢・パレットは有限\n",
            unsigned(status), fixture.Modifier->GetLegWeight(0), fixture.Modifier->GetLegWeight(1));
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

    FootIKFixture footFixture(fixture.World);
    PhysicsGroundQuery footQuery(fixture.Engine.GetSceneQuery(),
        MakePhysicsGroundQuerySettings(footFixture.Modifier->GetSettings().Ground));
    footFixture.Modifier->SetGroundQuery(&footQuery);
    TestFootIKFailureStates(footFixture, EGroundQueryStatus::NotReady);

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

    TestFootIKSurfaces(footFixture, footQuery);
    TestFootIKFailureStates(footFixture, EGroundQueryStatus::WrongThread);
    // 有効な接地重みを戻してから足場の無い場所へ移し、履歴がある状態での減衰を確かめる。
    for (int frame = 0; frame < 180; ++frame)
    {
        footFixture.Tick();
    }
    GROUND_CHECK(footFixture.Modifier->GetLegWeight(0) > 0.99f);
    GROUND_CHECK(footFixture.Modifier->GetLegWeight(1) > 0.99f);
    footFixture.Owner->SetWorldTransform(Math::Transform({100, 0, 100}));
    TestFootIKFailureStates(footFixture, EGroundQueryStatus::NoHit);

    std::puts("PhysicsGroundQueryIntegrationTest: 接地の幾何・バッチ・フィルター・状態の検証に成功しました");
    return EXIT_SUCCESS;
}
