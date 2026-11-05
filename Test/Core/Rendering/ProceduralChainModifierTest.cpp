// 手続きチェーンの姿勢変更を、角度とモデル空間の位置で検証する。
#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/SkeletalAssetResource.h"
#include "Animation/SkeletonResource.h"
#include "Component/AnimatorComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/ResourceRegistry.h"
#include "Object/World.h"
#include <algorithm>
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
    namespace Skeletal = NorvesLib::Core::Skeletal;
    namespace Math = NorvesLib::Math;
    using NorvesLib::Core::Identity;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletonResource;

    constexpr size_t JointCount = 5;
    constexpr float Step = 0.01f;
    constexpr int SettlingFrames = 300;
    constexpr char SignalConfig[] =
        R"({"version":1,"modifiers":[{"type":"Lean","settings":{"source":"signals","chain":[{"joint":"Spine0","weight":0.2},{"joint":"Spine1","weight":0.3},{"joint":"Spine2","weight":0.5}]}}]})";
    constexpr char ParamConfig[] =
        R"({"version":1,"modifiers":[{"type":"Lean","settings":{"chain":[{"joint":"Spine0","weight":0.2},{"joint":"Spine1","weight":0.3},{"joint":"Spine2","weight":0.5}]}}]})";

    void Require(bool bOk, const char* message)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "手続きチェーン検査失敗: %s\n", message);
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

    double RotationError(const Math::Quaternion& actual, const Math::Quaternion& expected)
    {
        const auto difference = Math::QuaternionUtils::Conjugate(expected) * actual;
        const double sine = std::sqrt(double(difference.x) * difference.x + double(difference.y) * difference.y +
                                      double(difference.z) * difference.z);
        return 2.0 * std::atan2(sine, std::fabs(double(difference.w)));
    }

    Math::Quaternion ModelRotation(const Animation::LocalPose& pose, size_t joint)
    {
        auto result = Math::Quaternion::Identity;
        for (size_t i = 0; i <= joint; ++i)
        {
            result = Math::QuaternionUtils::Normalize(result * pose[i].Rotation);
        }
        return result;
    }

    // 回転前の姿勢との差を測り、メッシュモデル空間の回転へ戻す。
    Math::Quaternion MeshRotationDelta(const Animation::LocalPose& pose, const Animation::LocalPose& rest,
                                       const Math::Quaternion& meshRotation)
    {
        const auto delta = ModelRotation(pose, 3) * Math::QuaternionUtils::Conjugate(ModelRotation(rest, 3));
        return Math::QuaternionUtils::Conjugate(meshRotation) * delta * meshRotation;
    }

    double BankAngle(const Math::Quaternion& delta)
    {
        return 2.0 * std::atan2(double(delta.z), double(delta.w));
    }

    double PitchAngle(const Math::Quaternion& delta)
    {
        return -2.0 * std::atan2(double(delta.x), double(delta.w));
    }

    struct ChainFixture
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Animation::AnimGraphInstance Instance;
        Animation::AnimParamSet Parameters;
        Animation::PoseScratch Scratch;
        Container::VariableArray<Math::Matrix4x4> Model;
        Container::TSharedPtr<Animation::IPoseModifier> Modifier;
        Math::Quaternion MeshRotation = Math::Quaternion::Identity;
        Math::Quaternion WorldRotation = Math::Quaternion::Identity;
        Math::Matrix4x4 MeshNode = Math::Matrix4x4::Identity;
        Math::Matrix4x4 MeshToWorld = Math::Matrix4x4::Identity;

        explicit ChainFixture(const char* config, bool bRotatedJoints = false)
        {
            Require(Registry.Initialize(), "試験用 Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("ChainSkeleton");
            Require(bool(Skeleton), "試験用骨格の作成");
            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            const char* const names[] = {"Root", "Spine0", "Spine1", "Spine2", "Tip"};
            const Math::Vector3 offset(0.08f, 0.16f, 0.23f);
            const Math::Quaternion localRotations[] = {
                Math::Quaternion(Math::Vector3::UnitY, 0.4f),
                Math::Quaternion(Math::Vector3::UnitX, -0.35f),
                Math::Quaternion(Math::Vector3::UnitZ, 0.5f),
                Math::Quaternion(Math::Vector3::UnitY, -0.3f),
                Math::Quaternion(Math::Vector3::UnitX, 0.25f)};
            auto parentRotation = Math::Quaternion::Identity;
            auto parentMatrix = Math::Matrix4x4::Identity;
            for (size_t i = 0; i < JointCount; ++i)
            {
                const auto localRotation = bRotatedJoints ? localRotations[i] : Math::Quaternion::Identity;
                const auto translation = i == 0 ? Math::Vector3(0.1f, 0.2f, 0.3f)
                                                : Math::QuaternionUtils::Conjugate(parentRotation) * offset;
                rest[i].Translation = {translation.x, translation.y, translation.z};
                rest[i].Rotation = {localRotation.x, localRotation.y, localRotation.z, localRotation.w};
                Animation::JointTransform local;
                local.Translation = translation;
                local.Rotation = localRotation;
                const auto global = Animation::ToRowMatrix(local) * parentMatrix;
                const auto inverseBind = Math::MatrixUtils::Inverse(global);
                joints[i].Name = names[i];
                joints[i].ParentIndex = int32_t(i) - 1;
                Math::MatrixUtils::CopyToShaderData(inverseBind, joints[i].InverseBindMatrix.data());
                parentRotation = Math::QuaternionUtils::Normalize(parentRotation * localRotation);
                parentMatrix = global;
            }
            Skeleton->SetJoints(std::move(joints));
            Require(Skeleton->SetAuthorRestPose(rest) && Skeleton->Load() && Skeleton->GetPoseRuntime().bValid,
                    "試験用骨格の読み込み");

            Animation::AnimParamDefinition definitions[2];
            definitions[0].Name = Identity("BankAngle");
            definitions[1].Name = Identity("PitchAngle");
            Require(Parameters.Initialize(definitions), "試験用パラメータの初期化");
            Procedural::PoseModifierConfigReport report;
            Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> modifiers;
            Require(Procedural::ParsePoseModifierConfig(Container::String(config), *Skeleton, &Parameters,
                                                       Procedural::GetBuiltinPoseModifierTypes(), modifiers, report) &&
                        modifiers.size() == 1,
                    "組み込み Lean の設定読み込み");
            Modifier = modifiers[0];
            Require(Modifier->Order() == Procedural::PoseModifierDefaultOrder::Lean, "Lean の既定順序");
        }

        void SetMeshRotation(const Math::Quaternion& rotation)
        {
            MeshRotation = rotation;
            Animation::JointTransform transform;
            transform.Rotation = rotation;
            transform.Scale = {2.0f, 3.0f, 4.0f};
            MeshNode = Animation::ToRowMatrix(transform);
        }

        void SetWorldRotation(const Math::Quaternion& rotation)
        {
            WorldRotation = rotation;
            MeshToWorld = Math::MatrixUtils::CreateWorldRowVector(
                Math::Vector3::Zero, Math::QuaternionUtils::Conjugate(rotation), Math::Vector3(2.0f, 3.0f, 4.0f));
        }

        void SetAngles(float bank, float pitch)
        {
            Require(Parameters.SetFloat(Parameters.Find(Identity("BankAngle")), bank) &&
                        Parameters.SetFloat(Parameters.Find(Identity("PitchAngle")), pitch),
                    "試験用角度の設定");
        }

        Animation::LocalPose Apply(const Animation::AnimDriveSignals* signals)
        {
            auto pose = Skeleton->GetPoseRuntime().AuthorRestLocal;
            Animation::PoseModifierContext context(Skeleton->GetPoseRuntime(), Step, Parameters, Instance, pose,
                                                   Scratch, Model);
            context.MeshNodeTransform = MeshNode;
            context.MeshToWorld = MeshToWorld;
            context.SetDriveSignals(signals);
            Require(Modifier->Apply(pose, context), "Lean の適用");
            return pose;
        }

        Animation::LocalPose Settle(const Animation::AnimDriveSignals* signals, int frames = SettlingFrames)
        {
            Animation::LocalPose pose;
            for (int i = 0; i < frames; ++i)
            {
                pose = Apply(signals);
            }
            return pose;
        }

        Math::Quaternion Delta(const Animation::LocalPose& pose) const
        {
            return MeshRotationDelta(pose, Skeleton->GetPoseRuntime().AuthorRestLocal, MeshRotation);
        }

        void ModelPositions(const Animation::LocalPose& pose, Math::Vector3 (&positions)[JointCount]) const
        {
            auto global = Math::Matrix4x4::Identity;
            const auto inverseMesh = Math::MatrixUtils::Inverse(MeshNode);
            for (size_t i = 0; i < JointCount; ++i)
            {
                global = Animation::ToRowMatrix(pose[i]) * global;
                positions[i] = (global * inverseMesh).GetTranslationRow();
            }
        }
    };

    void TestBodyLean()
    {
        ChainFixture signals(SignalConfig);
        Animation::AnimDriveSignals drive;
        drive.bGrounded = true;
        drive.Speed = 4.0f;
        drive.TurnRate = 0.5f;
        auto pose = signals.Settle(&drive);
        const double bank = BankAngle(signals.Delta(pose));
        const double expectedBank = std::atan(4.0 * 0.5 / 9.81);
        const double bankError = std::fabs(bank - expectedBank);
        drive.TurnRate = 5.0f;
        pose = signals.Settle(&drive);
        const double clampedBank = BankAngle(signals.Delta(pose));
        const double clampedError = std::fabs(clampedBank - 25.0 * double(Math::Constants::PI) / 180.0);
        std::printf("BodyLean case=1 bank_rad=%.12g bank_error_rad=%.12g clamped_bank_rad=%.12g "
                    "clamped_error_rad=%.12g limit_rad=0.001\n", bank, bankError, clampedBank, clampedError);
        Require(std::isfinite(bankError) && bankError <= 1e-3 &&
                    std::isfinite(clampedError) && clampedError <= 1e-3,
                "駆動信号のバンクと上限");

        ChainFixture params(ParamConfig);
        params.SetMeshRotation(Math::Quaternion(Math::Vector3::UnitY, 0.63f));
        constexpr float inputBank = 0.22f;
        constexpr float inputPitch = 0.08f;
        params.SetAngles(inputBank, inputPitch);
        pose = params.Apply(nullptr);
        const auto delta = params.Delta(pose);
        const double paramBankError = std::fabs(BankAngle(delta) - double(inputBank));
        const double paramPitchError = std::fabs(PitchAngle(delta) - double(inputPitch));
        const auto expectedRotation = Math::Quaternion(Math::Vector3::UnitZ, inputBank) *
                                      Math::Quaternion(Math::Vector3::UnitX, -inputPitch);
        const double compositionError = RotationError(delta, expectedRotation);
        std::printf("BodyLean case=2 bank_error_rad=%.12g pitch_error_rad=%.12g composition_error_rad=%.12g "
                    "limit_rad=0.00001\n", paramBankError, paramPitchError, compositionError);
        Require(std::isfinite(paramBankError) && paramBankError <= 1e-5 &&
                    std::isfinite(paramPitchError) && paramPitchError <= 1e-5 &&
                    std::isfinite(compositionError) && compositionError <= 1e-5,
                "パラメータの角度、軸、回転合成順");

        ChainFixture acceleration(SignalConfig);
        acceleration.SetWorldRotation(Math::Quaternion(Math::Vector3::UnitY, -0.71f));
        drive = {};
        drive.bGrounded = true;
        drive.Acceleration = acceleration.WorldRotation * Math::Vector3(2.0f, 0.0f, 3.0f);
        pose = acceleration.Settle(&drive);
        const double positivePitch = PitchAngle(acceleration.Delta(pose));
        drive.Acceleration = drive.Acceleration * -1.0f;
        pose = acceleration.Settle(&drive);
        const double negativePitch = PitchAngle(acceleration.Delta(pose));
        const double positivePitchError = std::fabs(positivePitch - 0.06);
        const double negativePitchError = std::fabs(negativePitch + 0.06);
        std::printf("BodyLean case=3 accel_pitch_rad=%.12g decel_pitch_rad=%.12g accel_error_rad=%.12g "
                    "decel_error_rad=%.12g limit_rad=0.001\n", positivePitch, negativePitch,
                    positivePitchError, negativePitchError);
        Require(positivePitch > 0.0 && negativePitch < 0.0 && positivePitchError <= 1e-3 &&
                    negativePitchError <= 1e-3,
                "前方加速と減速でピッチが反転");

        ChainFixture airborne(ParamConfig);
        constexpr float airBank = 0.24f;
        airborne.SetAngles(airBank, 0.0f);
        drive = {};
        drive.bGrounded = true;
        (void)airborne.Apply(&drive);
        drive.bGrounded = false;
        pose = airborne.Settle(&drive, 100);
        const double airWeight = std::fabs(BankAngle(airborne.Delta(pose)) / double(airBank));
        std::printf("BodyLean case=4 airborne_weight=%.12g limit=0.001\n", airWeight);
        Require(std::isfinite(airWeight) && airWeight < 1e-3, "空中で傾きが消える");

        ChainFixture aligned(ParamConfig);
        ChainFixture rotated(ParamConfig, true);
        const auto meshRotation = Math::Quaternion(Math::Vector3::UnitY, 0.43f);
        aligned.SetMeshRotation(meshRotation);
        rotated.SetMeshRotation(meshRotation);
        aligned.SetAngles(0.24f, -0.12f);
        rotated.SetAngles(0.24f, -0.12f);
        Math::Vector3 bindA[JointCount], bindB[JointCount], outputA[JointCount], outputB[JointCount];
        aligned.ModelPositions(aligned.Skeleton->GetPoseRuntime().AuthorRestLocal, bindA);
        rotated.ModelPositions(rotated.Skeleton->GetPoseRuntime().AuthorRestLocal, bindB);
        aligned.ModelPositions(aligned.Apply(nullptr), outputA);
        rotated.ModelPositions(rotated.Apply(nullptr), outputB);
        double bindError = 0.0;
        double positionError = 0.0;
        double displacement = 0.0;
        for (size_t i = 0; i < JointCount; ++i)
        {
            bindError = std::max(bindError, Distance(bindA[i], bindB[i]));
            positionError = std::max(positionError, Distance(outputA[i], outputB[i]));
            displacement = std::max(displacement, Distance(bindA[i], outputA[i]));
        }
        std::printf("BodyLean case=5 bind_error_m=%.12g position_error_m=%.12g displacement_m=%.12g "
                    "limit_m=0.00001\n", bindError, positionError, displacement);
        Require(std::isfinite(bindError) && bindError <= 1e-5 &&
                    std::isfinite(positionError) && positionError <= 1e-5 && displacement > 0.01,
                "関節軸の違いによらずモデル空間の位置が一致");
    }

    void TestSpineWave()
    {
        ChainFixture fixture(ParamConfig, true);
        fixture.SetMeshRotation(Math::Quaternion(Math::Vector3::UnitZ, 0.47f) *
                                Math::Quaternion(Math::Vector3::UnitY, 0.34f));
        auto mesh = fixture.Registry.CreateTransient<NorvesLib::Core::SkinnedMeshResource>("SpineWaveMesh");
        auto clip = fixture.Registry.CreateTransient<NorvesLib::Core::AnimationClipResource>("SpineWaveClip");
        auto asset = fixture.Registry.CreateTransient<NorvesLib::Core::SkeletalAssetResource>("SpineWaveAsset");
        auto graph = fixture.Registry.CreateTransient<NorvesLib::Core::AnimGraphResource>("SpineWaveGraph");
        Require(mesh && clip && asset && graph, "Spine の試験用資源を作成");

        Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
        vertices[0].Normal = {0, 1, 0};
        vertices[0].JointIndices[0] = 1;
        vertices[0].JointWeights[0] = 1;
        mesh->SetVertices(std::move(vertices));
        mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
        Require(mesh->Load(), "Spine の試験用メッシュを読み込む");
        Skeletal::SkeletalAnimationClip clipData;
        clipData.Name = "a";
        clipData.DurationSeconds = 1;
        Skeletal::SkeletalAnimationChannel channel;
        channel.JointIndex = 0;
        channel.Path = Skeletal::SkeletalAnimationPath::Translation;
        channel.Samples.push_back({0, {0, 0, 0, 0}});
        channel.Samples.push_back({1, {0, 0, 0, 0}});
        clipData.Channels.push_back(std::move(channel));
        clip->SetClip(std::move(clipData));
        Require(clip->Load(), "Spine の試験用クリップを読み込む");
        asset->SetClipResources(mesh, fixture.Skeleton, {clip});
        Require(asset->Load(), "Spine の試験用アセットを読み込む");
        struct Resolver : Animation::IClipResolver
        {
            Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> Clip;
            Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> ResolveClip(
                Container::StringView name) const override
            {
                return name == Container::StringView("a") ? Clip
                                                          : Container::TSharedPtr<NorvesLib::Core::AnimationClipResource>{};
            }
        } resolver;
        resolver.Clip = clip;
        constexpr char graphJson[] =
            R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a","syncGroup":"gait"}],"root":"a"})";
        Animation::AnimGraphReport graphReport;
        Require(graph->Compile(Container::String(graphJson), *fixture.Skeleton, resolver, graphReport) && graph->Load(),
                "Spine の同期グループを持つグラフを読み込む");
        NorvesLib::Core::World world;
        world.Initialize();
        auto* owner = world.SpawnEntity<NorvesLib::Core::Entity>();
        Require(owner != nullptr, "Spine の所有者を作成");
        auto* meshComponent = world.CreateComponent<NorvesLib::Core::Component::SkinnedMeshComponent>(owner);
        auto* animator = world.CreateComponent<NorvesLib::Core::Component::AnimatorComponent>(owner);
        Require(meshComponent && animator, "Spine のコンポーネントを作成");
        meshComponent->SetSkeletalAsset(asset);
        Require(animator->SetGraph(graph) && animator->FindSyncGroup(Identity("gait")) == 0,
                "Spine の同期グループを接続");

        const auto create = [&](const char* config) {
            Procedural::PoseModifierConfigReport report;
            Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> modifiers;
            Require(Procedural::ParsePoseModifierConfig(Container::String(config), *fixture.Skeleton,
                                                       &fixture.Parameters, Procedural::GetBuiltinPoseModifierTypes(),
                                                       modifiers, report) && modifiers.size() == 1 &&
                        modifiers[0]->Order() == Procedural::PoseModifierDefaultOrder::Spine,
                    "組み込み Spine の設定読み込み");
            return modifiers[0];
        };
        float phases[] = {0.125f};
        const auto apply = [&](Animation::IPoseModifier& modifier, Animation::LocalPose& pose,
                               const Animation::AnimDriveSignals* signals) {
            Animation::PoseModifierContext context(fixture.Skeleton->GetPoseRuntime(), Step, fixture.Parameters,
                                                   fixture.Instance, pose, fixture.Scratch, fixture.Model);
            context.MeshNodeTransform = fixture.MeshNode;
            context.SyncPhases = Container::Span<const float>(phases, 1);
            context.SetOwner(owner);
            context.SetDriveSignals(signals);
            Require(modifier.Apply(pose, context), "Spine の適用");
        };
        const auto& rest = fixture.Skeleton->GetPoseRuntime().AuthorRestLocal;
        const auto checkRotation = [&](const Animation::LocalPose& pose, size_t joint, const Math::Vector3& modelAxis,
                                       double expected, double& angleError, double& rotationError) {
            const auto parent = ModelRotation(pose, joint - 1);
            const auto axis = Math::QuaternionUtils::Conjugate(parent) * (fixture.MeshRotation * modelAxis);
            const auto delta = pose[joint].Rotation * Math::QuaternionUtils::Conjugate(rest[joint].Rotation);
            const double projection = double(delta.x) * axis.x + double(delta.y) * axis.y + double(delta.z) * axis.z;
            const double actual = 2.0 * std::atan2(projection, double(delta.w));
            angleError = std::fabs(actual - expected);
            rotationError = RotationError(delta, Math::Quaternion(axis, float(expected)));
        };

        constexpr char yawConfig[] =
            R"({"version":1,"modifiers":[{"type":"Spine","settings":{"joints":[{"joint":"Spine0","amplitude":0.2},{"joint":"Spine1","amplitude":0.3,"phaseOffset":0.2},{"joint":"Spine2","amplitude":0.4,"phaseOffset":0.45}],"upAxis":[0,2,0],"frequency":1.5,"syncGroup":"gait","strength":0.8}}]})";
        constexpr char pitchConfig[] =
            R"({"version":1,"modifiers":[{"type":"Spine","settings":{"joints":[{"joint":"Spine0","amplitude":0.2},{"joint":"Spine1","amplitude":0.3,"phaseOffset":0.2},{"joint":"Spine2","amplitude":0.4,"phaseOffset":0.45}],"axis":"pitch","rightAxis":[2,0,0],"frequency":1.5,"syncGroup":"gait","strength":0.8}}]})";
        const char* configs[] = {yawConfig, pitchConfig};
        const Math::Vector3 axes[] = {Math::Vector3::UnitY, Math::Vector3::UnitX};
        const float amplitudes[] = {0.2f, 0.3f, 0.4f};
        const float offsets[] = {0.0f, 0.2f, 0.45f};
        constexpr double twoPi = 6.283185307179586476925286766559;
        for (size_t direction = 0; direction < 2; ++direction)
        {
            auto modifier = create(configs[direction]);
            auto pose = rest;
            apply(*modifier, pose, nullptr);
            for (size_t i = 0; i < 3; ++i)
            {
                const double expected = double(amplitudes[i]) *
                                        std::sin(twoPi * (1.5 * double(phases[0]) + double(offsets[i]))) *
                                        double(0.8f);
                double angleError, rotationError;
                checkRotation(pose, i + 1, axes[direction], expected, angleError, rotationError);
                std::printf("SpineWave case=1 axis=%s joint=%zu angle_error_rad=%.12g rotation_error_rad=%.12g "
                            "limit_rad=0.00001\n", direction == 0 ? "yaw" : "pitch", i, angleError, rotationError);
                Require(std::isfinite(angleError) && angleError <= 1e-5 &&
                            std::isfinite(rotationError) && rotationError <= 1e-5,
                        "同期位相による関節の角度、符号と軸");
            }
        }

        constexpr char curveConfig[] =
            R"({"version":1,"modifiers":[{"type":"Spine","settings":{"joints":[{"joint":"Spine1","amplitude":[{"speed":0,"value":0.1},{"speed":4,"value":0.5}]}],"syncGroup":"gait"}}]})";
        auto curve = create(curveConfig);
        auto curvePose = rest;
        Animation::AnimDriveSignals signals;
        signals.Speed = 2.0f;
        phases[0] = 0.25f;
        apply(*curve, curvePose, &signals);
        double curveAngleError, curveRotationError;
        checkRotation(curvePose, 2, Math::Vector3::UnitY, 0.3, curveAngleError, curveRotationError);
        std::printf("SpineWave case=2 angle_error_rad=%.12g rotation_error_rad=%.12g limit_rad=0.00001\n",
                    curveAngleError, curveRotationError);
        Require(std::isfinite(curveAngleError) && curveAngleError <= 1e-5 &&
                    std::isfinite(curveRotationError) && curveRotationError <= 1e-5,
                "速度の中間で振幅を補間");

        constexpr char zeroConfig[] =
            R"({"version":1,"modifiers":[{"type":"Spine","settings":{"joints":[{"joint":"Spine0","amplitude":0.4},{"joint":"Spine1","amplitude":0.2}],"syncGroup":"gait","strength":0}}]})";
        auto zero = create(zeroConfig);
        auto zeroPose = rest;
        const auto before = zeroPose;
        apply(*zero, zeroPose, nullptr);
        const int byteDifference = std::memcmp(zeroPose.data(), before.data(), zeroPose.size() * sizeof(zeroPose[0]));
        std::printf("SpineWave case=3 memcmp=%d expected=0\n", byteDifference);
        Require(byteDifference == 0, "重みゼロで姿勢がビット一致");
        world.Finalize();
    }
} // 無名名前空間

void TestProceduralChainModifier()
{
    TestBodyLean();
    TestSpineWave();
}
