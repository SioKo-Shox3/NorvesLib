// SpringBone と FollowThrough の追従、拘束、姿勢保存、設定の契約を検証する。
#include "Animation/Procedural/FollowThroughModifier.h"
#include "Animation/Procedural/SpringBoneModifier.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/HoldSlotComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    using NorvesLib::Core::Identity;
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
            std::fprintf(stderr, "手続き modifier 検査失敗: %s sample=%d actual=%.17g limit=%.17g\n",
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

        ModifierFixture(const char* const* jointNames = nullptr)
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
                joints[i].Name = jointNames ? jointNames[i] : names[i];
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

    constexpr char FollowSettingsJson[] =
        R"({"joints":["Chest","Neck"],"head":"Head","strength":1})";
    constexpr const char* FollowJointNames[] = {"Root", "Chest", "Neck", "Head"};
    constexpr double DegreesPerRadian = 180.0 / Math::Constants::PI;
    constexpr double AngleLimitDegrees = 0.01;
    constexpr int YawStepFrame = 50;
    constexpr int EarlyFrame = YawStepFrame + 4; // 段の入力から 5 回、計 0.05 秒進めた標本。
    constexpr int FinalFrame = YawStepFrame + 299; // 段の入力から計 3 秒進めた標本。

    struct FollowFixture : ModifierFixture
    {
        Animation::AnimGraphInstance Instance;
        Animation::PoseScratch Scratch;
        Container::VariableArray<Math::Matrix4x4> Model;
        Procedural::FollowThroughModifier* Follow = nullptr;

        explicit FollowFixture(const char* settings = FollowSettingsJson) : ModifierFixture(FollowJointNames)
        {
            CheckConfig(Instance.Initialize(*Graph, *Skeleton, *Mesh, Math::Matrix4x4::Identity),
                        "追従試験用の文脈を準備する");
            JsonDocument document;
            Procedural::PoseModifierConfigReport report;
            CheckConfig(JsonDocument::TryParse(Container::String(settings), document), "追従設定の解析");
            CheckConfig(Procedural::CreateFollowThroughModifier({*Skeleton, document.GetRoot()}, Modifier, report) &&
                            Modifier,
                        "追従 modifier の作成");
            Follow = static_cast<Procedural::FollowThroughModifier*>(Modifier.get());
        }

        Animation::LocalPose Input(int frame) const
        {
            auto pose = Skeleton->GetPoseRuntime().AuthorRestLocal;
            // 根の段入力で胸・首・頭すべてのモデル空間のヨーを同時に変える。
            pose[0].Rotation = Math::QuaternionUtils::FromAxisAngle(
                Math::Vector3::UnitY, frame < YawStepFrame ? 0.0f : Math::Constants::PI / 3.0f);
            return pose;
        }

        void Apply(Animation::LocalPose& pose, int frame, int sample, float weight = 1.0f)
        {
            Animation::PoseModifierContext context(Skeleton->GetPoseRuntime(), frame == 0 ? 0.0f : Step,
                                                   Instance.Parameters(), Instance, pose, Scratch, Model);
            context.SetOwner(Owner);
            context.SetSkeletonAsset(Skeleton.get());
            context.Weight = weight;
            const bool bApplied = Follow->Apply(pose, context);
            Check(bApplied, "追従の段入力を適用できる", sample, bApplied ? 1 : 0, 1);
        }
    };

    Math::Quaternion FollowModelRotation(const Animation::LocalPose& pose, int joint)
    {
        // 単位の RootFrame を持つ直列骨格なので、親からの合成を独立の測定基準にする。
        auto rotation = Math::Quaternion::Identity;
        for (int i = 0; i <= joint; ++i)
        {
            rotation = Math::QuaternionUtils::Normalize(rotation * pose[i].Rotation);
        }
        return rotation;
    }

    double FollowYawDegrees(const Animation::LocalPose& pose, int joint)
    {
        const auto forward = FollowModelRotation(pose, joint) * Math::Vector3::UnitZ;
        return std::atan2(double(forward.x), double(forward.z)) * DegreesPerRadian;
    }

    double FollowRotationErrorDegrees(const Math::Quaternion& actual, const Math::Quaternion& expected)
    {
        const auto residual = Math::QuaternionUtils::Conjugate(expected) * actual;
        const double sine = std::sqrt(double(residual.x) * residual.x + double(residual.y) * residual.y +
                                      double(residual.z) * residual.z);
        // acos の単精度の内積で小さな回転差を丸め落とさず、符号反転も同じ回転として測る。
        return 2.0 * std::atan2(sine, std::fabs(double(residual.w))) * DegreesPerRadian;
    }

    void TestFollowConvergence()
    {
        FollowFixture fixture;
        double chestLag = 0, neckLag = 0, chestFinalError = 0, neckFinalError = 0, maxHeadError = 0;
        int nonFiniteCount = 0;
        for (int frame = 0; frame <= FinalFrame; ++frame)
        {
            const auto animation = fixture.Input(frame);
            auto pose = animation;
            fixture.Apply(pose, frame, 1);
            for (int joint = 0; joint < JointCount; ++joint)
            {
                const auto& local = pose[joint];
                const auto global = FollowModelRotation(pose, joint);
                if (!IsFinite(local.Translation) || !IsFinite(local.Scale) ||
                    !std::isfinite(local.Rotation.x) || !std::isfinite(local.Rotation.y) ||
                    !std::isfinite(local.Rotation.z) || !std::isfinite(local.Rotation.w) ||
                    !std::isfinite(global.x) || !std::isfinite(global.y) ||
                    !std::isfinite(global.z) || !std::isfinite(global.w))
                {
                    ++nonFiniteCount;
                }
            }
            RecordMax(maxHeadError, FollowRotationErrorDegrees(FollowModelRotation(pose, 3),
                                                              FollowModelRotation(animation, 3)));
            if (frame == EarlyFrame)
            {
                chestLag = FollowYawDegrees(animation, 1) - FollowYawDegrees(pose, 1);
                neckLag = FollowYawDegrees(animation, 2) - FollowYawDegrees(pose, 2);
            }
            if (frame == FinalFrame)
            {
                chestFinalError = std::fabs(FollowYawDegrees(animation, 1) - FollowYawDegrees(pose, 1));
                neckFinalError = std::fabs(FollowYawDegrees(animation, 2) - FollowYawDegrees(pose, 2));
            }
        }
        std::printf("FollowThrough case=1 chest_lag_deg=%.9g neck_lag_deg=%.9g lag_min_deg=1 "
                    "chest_final_error_deg=%.9g neck_final_error_deg=%.9g max_head_error_deg=%.9g "
                    "angle_limit_deg=%.9g nonfinite=%d frames=%d\n",
                    chestLag, neckLag, chestFinalError, neckFinalError, maxHeadError, AngleLimitDegrees,
                    nonFiniteCount, FinalFrame + 1);
        Check(std::isfinite(chestLag) && chestLag >= 1, "追従の胸は段の直後に 1 度以上遅れる", 1, chestLag, 1);
        Check(std::isfinite(neckLag) && neckLag >= 1, "追従の首は段の直後に 1 度以上遅れる", 1, neckLag, 1);
        Check(std::isfinite(chestFinalError) && chestFinalError <= AngleLimitDegrees,
              "追従の胸は 3 秒後に 0.01 度以内へ収束する", 1, chestFinalError, AngleLimitDegrees);
        Check(std::isfinite(neckFinalError) && neckFinalError <= AngleLimitDegrees,
              "追従の首は 3 秒後に 0.01 度以内へ収束する", 1, neckFinalError, AngleLimitDegrees);
        Check(std::isfinite(maxHeadError) && maxHeadError <= AngleLimitDegrees,
              "追従中も頭のモデル回転は全フレームでアニメと一致する", 1, maxHeadError, AngleLimitDegrees);
        Check(nonFiniteCount == 0, "追従の全フレームの姿勢は有限である", 1, nonFiniteCount, 0);
    }

    void TestFollowZeroWeight()
    {
        FollowFixture fixture;
        int differentComponents = 0;
        for (int frame = 0; frame <= FinalFrame; ++frame)
        {
            const auto animation = fixture.Input(frame);
            auto pose = animation;
            fixture.Apply(pose, frame, 2, 0.0f);
            for (int joint = 0; joint < JointCount; ++joint)
            {
                const auto& actual = pose[joint];
                const auto& expected = animation[joint];
                // 型のパディングを除き、並進・回転・スケールの各成分のビットを比べる。
                const float actualValues[] = {actual.Translation.x, actual.Translation.y, actual.Translation.z,
                                              actual.Rotation.x, actual.Rotation.y, actual.Rotation.z, actual.Rotation.w,
                                              actual.Scale.x, actual.Scale.y, actual.Scale.z};
                const float expectedValues[] = {expected.Translation.x, expected.Translation.y, expected.Translation.z,
                                                expected.Rotation.x, expected.Rotation.y, expected.Rotation.z,
                                                expected.Rotation.w, expected.Scale.x, expected.Scale.y, expected.Scale.z};
                for (size_t component = 0; component < sizeof(actualValues) / sizeof(float); ++component)
                {
                    if (std::memcmp(&actualValues[component], &expectedValues[component], sizeof(float)) != 0)
                    {
                        ++differentComponents;
                    }
                }
            }
        }
        std::printf("FollowThrough case=2 different_components=%d limit=0 frames=%d joints=%d components_per_joint=10\n",
                    differentComponents, FinalFrame + 1, JointCount);
        Check(differentComponents == 0, "追従の重み 0 は段の前後を含む全関節のローカル姿勢をビット一致で保つ",
              2, differentComponents, 0);
    }

    void TestFollowHeldMass()
    {
        constexpr char settings[] =
            R"({"joints":["Chest","Neck"],"head":"Head","strength":1,"massScale":1.5,"heldSlot":"Carry","heldMassScale":4})";
        FollowFixture empty(settings), held(settings);
        auto* emptySlots = empty.TestWorld.CreateComponent<Component::HoldSlotComponent>(empty.Owner);
        auto* slots = held.TestWorld.CreateComponent<Component::HoldSlotComponent>(held.Owner);
        CheckConfig(emptySlots && slots, "追従試験用の保持スロットを作成する");
        const Container::VariableArray<Component::HoldSlotDefinition> definitions{{Identity("Carry"), 1, {}}};
        CheckConfig(emptySlots->SetSlots(definitions) && slots->SetSlots(definitions),
                    "追従試験用の保持スロットを設定する");
        auto* item = held.TestWorld.SpawnEntity<Entity>();
        CheckConfig(item != nullptr, "追従試験用の保持対象を作成する");
        CheckConfig(slots->TryAcquire(Identity("Carry"), item->GetObjectId(), {}) == Component::HoldAcquireResult::Granted,
                    "追従試験用の保持スロットを埋める");
        double emptyLag = 0, heldLag = 0, maxEmptyMassError = 0, maxHeldMassError = 0;
        for (int frame = 0; frame <= EarlyFrame; ++frame)
        {
            const auto animation = empty.Input(frame);
            auto emptyPose = animation;
            auto heldPose = animation;
            empty.Apply(emptyPose, frame, 3);
            held.Apply(heldPose, frame, 3);
            RecordMax(maxEmptyMassError, std::fabs(double(empty.Follow->GetLastMassScale()) - 1.5));
            RecordMax(maxHeldMassError, std::fabs(double(held.Follow->GetLastMassScale()) - 6.0));
            if (frame == EarlyFrame)
            {
                emptyLag = FollowYawDegrees(animation, 1) - FollowYawDegrees(emptyPose, 1);
                heldLag = FollowYawDegrees(animation, 1) - FollowYawDegrees(heldPose, 1);
            }
        }
        std::printf("FollowThrough case=3 empty_chest_lag_deg=%.9g held_chest_lag_deg=%.9g lag_increase_deg=%.9g "
                    "lag_increase_min_deg=0 empty_mass=%.9g empty_mass_expected=1.5 held_mass=%.9g held_mass_expected=6 "
                    "max_empty_mass_error=%.9g max_held_mass_error=%.9g mass_error_limit=0\n",
                    emptyLag, heldLag, heldLag - emptyLag, empty.Follow->GetLastMassScale(),
                    held.Follow->GetLastMassScale(), maxEmptyMassError, maxHeldMassError);
        Check(std::isfinite(emptyLag) && std::isfinite(heldLag) && heldLag > emptyLag,
              "追従の胸は保持スロットが埋まると段の直後の遅れが増える", 3, heldLag - emptyLag, 0);
        Check(std::isfinite(maxEmptyMassError) && maxEmptyMassError == 0,
              "追従の空スロットでは全フレームの質量倍率が massScale と一致する", 3, maxEmptyMassError, 0);
        Check(std::isfinite(maxHeldMassError) && maxHeldMassError == 0,
              "追従の保持中は全フレームの質量倍率が massScale の 4 倍と一致する", 3, maxHeldMassError, 0);
    }

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
    TestFollowConvergence();
    TestFollowZeroWeight();
    TestFollowHeldMass();
    std::fflush(stdout);
    if (g_Failures != 0)
    {
        std::abort();
    }
}
