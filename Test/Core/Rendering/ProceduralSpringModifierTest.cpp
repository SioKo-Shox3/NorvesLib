// SpringBone の追従、拘束、再初期化、設定の契約を検証する。
#include "Animation/Procedural/SpringBoneModifier.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace
{
    namespace Animation = NorvesLib::Core::Animation;
    namespace Procedural = NorvesLib::Core::Animation::Procedural;
    namespace Container = NorvesLib::Core::Container;
    namespace Component = NorvesLib::Core::Component;
    namespace Skeletal = NorvesLib::Core::Skeletal;
    namespace Math = NorvesLib::Math;
    using NorvesLib::Core::AnimationClipResource;
    using NorvesLib::Core::AnimGraphResource;
    using NorvesLib::Core::Entity;
    using NorvesLib::Core::JsonDocument;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletalAssetResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    using NorvesLib::Core::World;

    constexpr int JointCount = 4;
    constexpr double PositionLimit = 1e-4;
    constexpr double BoneLength = 0.1;
    constexpr float Step = 0.01f;
    constexpr char SettingsJson[] =
        R"({"chains":[{"name":"tail","joints":["Root","Tail0","Tail1","Tail2"]}]})";
    constexpr char ConfigJson[] =
        R"({"version":1,"modifiers":[{"type":"SpringBone","settings":{"chains":[{"name":"tail","joints":["Root","Tail0","Tail1","Tail2"]}]}}]})";
    int g_Failures = 0;

    void Check(bool bOk, const char* contract, int sample, double actual, double limit)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "SpringBone 検査失敗: %s sample=%d actual=%.17g limit=%.17g\n",
                         contract, sample, actual, limit);
            ++g_Failures;
        }
    }

    void CheckConfig(bool bOk, const char* contract)
    {
        Check(bOk, contract, 0, bOk ? 1.0 : 0.0, 1.0);
        if (!bOk)
        {
            std::abort();
        }
    }

    double Distance(const Math::Vector3& a, const Math::Vector3& b)
    {
        const double x = double(a.x) - b.x;
        const double y = double(a.y) - b.y;
        const double z = double(a.z) - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    bool IsFinite(const Math::Vector3& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    void RecordMax(double& maximum, double value)
    {
        if (!std::isfinite(value) || value > maximum)
        {
            maximum = value;
        }
    }

    struct ModifierFixture : Animation::IClipResolver
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        Container::TSharedPtr<AnimGraphResource> Graph;
        World TestWorld;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Component::AnimatorComponent* Animator = nullptr;
        Container::TSharedPtr<Animation::IPoseModifier> Modifier;
        Procedural::SpringBoneModifier* Spring = nullptr;

        ModifierFixture()
        {
            CheckConfig(Registry.Initialize(), "試験用 Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("SpringSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("SpringMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("SpringClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("SpringAsset");
            CheckConfig(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            const char* const names[] = {"Root", "Tail0", "Tail1", "Tail2"};
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            for (int i = 0; i < JointCount; ++i)
            {
                joints[i].Name = names[i];
                joints[i].ParentIndex = i - 1;
                const auto inverseBind = Math::MatrixUtils::CreateTranslation(0.0f, 0.0f, 0.1f * float(i));
                Math::MatrixUtils::CopyToShaderData(inverseBind, joints[i].InverseBindMatrix.data());
                if (i > 0)
                {
                    rest[i].Translation = {0.0f, 0.0f, -0.1f};
                }
            }
            Skeleton->SetJoints(std::move(joints));
            CheckConfig(Skeleton->SetAuthorRestPose(rest) && Skeleton->Load(), "試験用骨格の読み込み");

            Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 1;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
            CheckConfig(Mesh->Load(), "試験用メッシュの読み込み");

            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "a";
            clip.DurationSeconds = 1;
            Skeletal::SkeletalAnimationChannel channel;
            channel.JointIndex = 0;
            channel.Path = Skeletal::SkeletalAnimationPath::Translation;
            channel.Samples.push_back({0, {0, 0, 0, 0}});
            channel.Samples.push_back({1, {0, 0, 0, 0}});
            clip.Channels.push_back(std::move(channel));
            Clip->SetClip(std::move(clip));
            CheckConfig(Clip->Load(), "試験用クリップの読み込み");
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            CheckConfig(Asset->Load(), "試験用アセットの読み込み");

            Graph = Registry.CreateTransient<AnimGraphResource>("SpringGraph");
            Animation::AnimGraphReport report;
            const char* graphJson = R"({"version":1,"params":[],"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})";
            CheckConfig(Graph && Graph->Compile(Container::String(graphJson), *Skeleton, *this, report) && Graph->Load(),
                        "試験用グラフの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            CheckConfig(Owner != nullptr, "試験用 Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = TestWorld.CreateComponent<Component::AnimatorComponent>(Owner);
            CheckConfig(MeshComponent && Animator, "試験用コンポーネントの作成");
            MeshComponent->SetSkeletalAsset(Asset);
            CheckConfig(Animator->SetGraph(Graph), "試験用グラフの接続");
        }

        ~ModifierFixture()
        {
            TestWorld.Finalize();
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("a") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        void AddSpring()
        {
            JsonDocument document;
            Procedural::PoseModifierConfigReport report;
            CheckConfig(JsonDocument::TryParse(Container::String(SettingsJson), document), "ばね設定の解析");
            CheckConfig(Procedural::CreateSpringBoneModifier({*Skeleton, document.GetRoot()}, Modifier, report) && Modifier,
                        "ばね modifier の作成");
            Spring = static_cast<Procedural::SpringBoneModifier*>(Modifier.get());
            CheckConfig(Animator->AddModifier(Modifier), "ばね modifier の追加");
        }

        void Tick(float x, float dt = Step)
        {
            Owner->SetWorldTransform(Math::Transform(Math::Vector3(x, 0, 0)));
            TestWorld.Tick(dt);
        }

        void ReadJoints(Math::Vector3 (&positions)[JointCount], int sample)
        {
            for (int i = 0; i < JointCount; ++i)
            {
                Math::Matrix4x4 matrix = Math::Matrix4x4::Identity;
                const bool bRead = MeshComponent->TryGetJointWorldMatrix(uint32_t(i), matrix);
                Check(bRead, "関節のワールド位置を取得できる", sample, bRead ? 1.0 : 0.0, 1.0);
                positions[i] = matrix.GetTranslationRow();
            }
        }

        Math::Vector3 Particle(int joint, int sample)
        {
            Math::Vector3 position;
            const bool bRead = Spring->TryGetParticleWorldPosition(0, size_t(joint), position);
            Check(bRead, "粒子のワールド位置を取得できる", sample, bRead ? 1.0 : 0.0, 1.0);
            return position;
        }
    };

    struct Measurements
    {
        double MaxJointBoneError = 0;
        double MaxParticleBoneError = 0;
        int NonFiniteCount = 0;

        void Observe(ModifierFixture& fixture, int sample, Math::Vector3 (&positions)[JointCount], bool bParticles)
        {
            fixture.ReadJoints(positions, sample);
            for (int i = 0; i < JointCount; ++i)
            {
                if (!IsFinite(positions[i]))
                {
                    ++NonFiniteCount;
                }
                if (i > 0)
                {
                    RecordMax(MaxJointBoneError, std::fabs(Distance(positions[i], positions[i - 1]) - BoneLength));
                }
            }
            if (bParticles)
            {
                Math::Vector3 previous = fixture.Particle(0, sample);
                for (int i = 1; i < JointCount; ++i)
                {
                    const Math::Vector3 current = fixture.Particle(i, sample);
                    RecordMax(MaxParticleBoneError, std::fabs(Distance(current, previous) - BoneLength));
                    previous = current;
                }
            }
        }
    };

    double TailResidual(const Math::Vector3& tail, float ownerX)
    {
        return Distance(tail, Math::Vector3(ownerX, 0, -0.3f));
    }

    void TestRestConvergence()
    {
        ModifierFixture fixture;
        Procedural::PoseModifierConfigReport report;
        CheckConfig(fixture.Animator->ApplyPoseModifiersJson(Container::String(ConfigJson), &report),
                    "組み込み型の表によるばね設定");
        Math::Vector3 positions[JointCount];
        fixture.Tick(0);
        fixture.ReadJoints(positions, 0);
        fixture.Tick(0.3f);
        fixture.ReadJoints(positions, 1);
        const double initial = TailResidual(positions[3], 0.3f);
        for (int i = 1; i < 300; ++i)
        {
            fixture.Tick(0.3f);
        }
        fixture.ReadJoints(positions, 300);
        const double residual = TailResidual(positions[3], 0.3f);
        std::printf("SpringBone case=1 initial=%.9g residual=%.9g limit=%.9g\n", initial, residual, PositionLimit);
        Check(initial >= 0.001, "組み込み設定のばねが移動に追従して揺れる", 1, initial, 0.001);
        Check(std::isfinite(residual) && residual <= PositionLimit, "静止後に先端がアニメの位置へ収束する", 1,
              residual, PositionLimit);
    }

    void TestAccelerationAndLengths()
    {
        ModifierFixture fixture;
        fixture.AddSpring();
        Measurements measured;
        Math::Vector3 positions[JointCount];
        fixture.Tick(0);
        measured.Observe(fixture, 0, positions, true);
        double minLag = 0;
        float x = 0;
        for (int i = 1; i <= 50; ++i)
        {
            const float time = float(i) * Step;
            x = 2.0f * time * time;
            fixture.Tick(x);
            measured.Observe(fixture, i, positions, true);
            const double lag = double(positions[3].x) - x;
            if (lag < minLag)
            {
                minLag = lag;
            }
        }
        for (int i = 1; i <= 300; ++i)
        {
            fixture.Tick(x);
            measured.Observe(fixture, 50 + i, positions, true);
        }
        const double residual = TailResidual(positions[3], x);
        std::printf("SpringBone case=2 min_x_difference=%.9g lag_limit=-0.001 residual=%.9g limit=%.9g\n",
                    minLag, residual, PositionLimit);
        Check(minLag <= -0.001, "加速中に先端がアニメの位置から遅れる", 2, minLag, -0.001);
        Check(std::isfinite(residual) && residual <= PositionLimit, "加速後の静止で先端が収束する", 2,
              residual, PositionLimit);
        std::printf("SpringBone case=3 joint_bone_error=%.9g particle_bone_error=%.9g limit=%.9g\n",
                    measured.MaxJointBoneError, measured.MaxParticleBoneError, PositionLimit);
        Check(measured.NonFiniteCount == 0, "加速から収束までの関節位置が有限である", 3,
              measured.NonFiniteCount, 0);
        Check(std::isfinite(measured.MaxJointBoneError) && measured.MaxJointBoneError <= PositionLimit,
              "加速から収束まで全ての関節間の長さが保たれる", 3, measured.MaxJointBoneError, PositionLimit);
        Check(std::isfinite(measured.MaxParticleBoneError) && measured.MaxParticleBoneError <= PositionLimit,
              "加速から収束まで全ての粒子間の長さが保たれる", 3, measured.MaxParticleBoneError, PositionLimit);
    }

    void TestReset()
    {
        ModifierFixture fixture;
        fixture.AddSpring();
        Measurements measured;
        Math::Vector3 positions[JointCount];
        fixture.Tick(0);
        measured.Observe(fixture, 0, positions, false);
        for (int i = 1; i <= 50; ++i)
        {
            const float time = float(i) * Step;
            fixture.Tick(2.0f * time * time);
            measured.Observe(fixture, i, positions, false);
        }
        const double beforeTeleport = TailResidual(positions[3], 0.5f);
        fixture.Tick(10.5f);
        measured.Observe(fixture, 51, positions, false);
        const double teleportResidual = TailResidual(positions[3], 10.5f);
        for (int i = 1; i <= 50; ++i)
        {
            const float time = float(i) * Step;
            fixture.Tick(10.5f + 2.0f * time * time);
            measured.Observe(fixture, 51 + i, positions, false);
        }
        const double beforeReset = TailResidual(positions[3], 11.0f);
        fixture.Spring->RequestReset();
        fixture.Tick(11.0f);
        measured.Observe(fixture, 102, positions, false);
        const double resetResidual = TailResidual(positions[3], 11.0f);
        std::printf("SpringBone case=4 before_teleport=%.9g teleport_residual=%.9g before_reset=%.9g reset_residual=%.9g nonfinite=%d limit=%.9g\n",
                    beforeTeleport, teleportResidual, beforeReset, resetResidual, measured.NonFiniteCount, PositionLimit);
        Check(beforeTeleport >= 0.001, "テレポート前に揺れている", 4, beforeTeleport, 0.001);
        Check(std::isfinite(teleportResidual) && teleportResidual <= PositionLimit,
              "テレポートしたフレームでアニメの姿勢へ戻る", 4, teleportResidual, PositionLimit);
        Check(beforeReset >= 0.001, "再初期化の要求前に揺れている", 4, beforeReset, 0.001);
        Check(std::isfinite(resetResidual) && resetResidual <= PositionLimit,
              "再初期化の要求後にアニメの姿勢へ戻る", 4, resetResidual, PositionLimit);
        Check(measured.NonFiniteCount == 0, "テレポートと再初期化の全フレームで関節位置が有限である", 4,
              measured.NonFiniteCount, 0);
    }

    void TestLargeDelta()
    {
        ModifierFixture fixture;
        fixture.AddSpring();
        Measurements measured;
        Math::Vector3 positions[JointCount];
        fixture.Tick(0);
        measured.Observe(fixture, 0, positions, false);
        float x = 0;
        for (int i = 1; i <= 49; ++i)
        {
            const float time = float(i) * Step;
            x = 2.0f * time * time;
            fixture.Tick(x);
            measured.Observe(fixture, i, positions, false);
        }
        const double before = TailResidual(positions[3], x);
        const double expectedDropped = 0.5 - 4.0 / 120.0;
        double maxDroppedError = 0;
        double lastDropped = 0;
        for (int i = 0; i < 10; ++i)
        {
            fixture.Tick(x, 0.5f);
            measured.Observe(fixture, 50 + i, positions, true);
            lastDropped = fixture.Spring->GetLastDroppedSeconds();
            RecordMax(maxDroppedError, std::fabs(lastDropped - expectedDropped));
        }
        std::printf("SpringBone case=5 before=%.9g nonfinite=%d joint_bone_error=%.9g particle_bone_error=%.9g dropped=%.9g dropped_error=%.9g limit=%.9g\n",
                    before, measured.NonFiniteCount, measured.MaxJointBoneError, measured.MaxParticleBoneError,
                    lastDropped, maxDroppedError, PositionLimit);
        Check(before >= 0.001, "大きな時間刻みの前に揺れている", 5, before, 0.001);
        Check(measured.NonFiniteCount == 0, "大きな時間刻みでも関節位置が有限である", 5,
              measured.NonFiniteCount, 0);
        Check(std::isfinite(measured.MaxJointBoneError) && measured.MaxJointBoneError <= PositionLimit,
              "大きな時間刻みでも関節間の長さが保たれる", 5, measured.MaxJointBoneError, PositionLimit);
        Check(std::isfinite(measured.MaxParticleBoneError) && measured.MaxParticleBoneError <= PositionLimit,
              "大きな時間刻みでも粒子間の長さが保たれる", 5, measured.MaxParticleBoneError, PositionLimit);
        Check(std::isfinite(maxDroppedError) && maxDroppedError <= PositionLimit,
              "大きな時間刻みで上限を超えた秒数が捨てられる", 5, maxDroppedError, PositionLimit);
    }

    struct CountingModifier : Animation::IPoseModifier
    {
        int Calls = 0;
        int32_t Order() const override { return 800; }
        bool Apply(Animation::LocalPose&, Animation::PoseModifierContext&) override
        {
            ++Calls;
            return true;
        }
    };

    void TestRejectedConfiguration()
    {
        ModifierFixture fixture;
        fixture.AddSpring();
        const auto counter = Container::MakeShared<CountingModifier>();
        CheckConfig(fixture.Animator->AddModifier(counter), "確認用 modifier の追加");
        fixture.Tick(0);
        fixture.Tick(0.3f);
        const int previousCalls = counter->Calls;
        const char* invalid =
            R"({"version":1,"modifiers":[{"type":"SpringBone","settings":{"chains":[{"name":"tail","joints":["Root","Tail0","Tail1","Tail2"],"unknown":1}]}}]})";
        Procedural::PoseModifierConfigReport report;
        const bool bAccepted = fixture.Animator->ApplyPoseModifiersJson(Container::String(invalid), &report);
        fixture.Tick(0.3f, 0.5f);
        const int callsAfter = counter->Calls;
        const double dropped = fixture.Spring->GetLastDroppedSeconds();
        const bool bCorrectPath = report.Path == Container::String("modifiers[0].settings.chains[0].unknown");
        const bool bCorrectError = report.Error == Procedural::PoseModifierConfigError::UnknownKey;
        std::printf("SpringBone case=6 accepted=%d unknown_key=%d path_match=%d prior_calls=%d calls_after=%d prior_spring_dropped=%.9g path=",
                    int(bAccepted), int(bCorrectError), int(bCorrectPath), previousCalls, callsAfter, dropped);
        for (const auto character : report.Path)
        {
            std::putchar(static_cast<unsigned char>(character));
        }
        std::putchar('\n');
        Check(!bAccepted, "未知のチェーン設定を拒否する", 6, bAccepted ? 1.0 : 0.0, 0);
        Check(bCorrectError, "未知のキーを理由として報告する", 6, bCorrectError ? 1.0 : 0.0, 1);
        Check(bCorrectPath, "未知のキーの完全なパスを報告する", 6, bCorrectPath ? 1.0 : 0.0, 1);
        Check(callsAfter == previousCalls + 1, "拒否後も元の後続 modifier が呼ばれる", 6,
              callsAfter - previousCalls, 1);
        Check(std::isfinite(dropped) && dropped > 0.46, "拒否後も元のばね modifier が呼ばれる", 6, dropped, 0.46);
    }
} // namespace

void TestProceduralSpringModifier()
{
    TestRestConvergence();
    TestAccelerationAndLengths();
    TestReset();
    TestLargeDelta();
    TestRejectedConfiguration();
    std::fflush(stdout);
    if (g_Failures != 0)
    {
        std::abort();
    }
}
