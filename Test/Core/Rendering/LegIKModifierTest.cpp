// 四足の合成骨格と地面の問い合わせで足 IK と体の釣り合いを検証する。
#include "Animation/FootIKModifier.h"
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
    namespace Component = NorvesLib::Core::Component;
    namespace Math = NorvesLib::Math;
    using NorvesLib::Core::AnimationClipResource;
    using NorvesLib::Core::AnimGraphResource;
    using NorvesLib::Core::Entity;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletalAssetResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    using NorvesLib::Core::World;

    constexpr int JointCount = 19;
    constexpr int LegCount = 4;
    constexpr int Pelvis = 0;
    constexpr int Chest = 2;
    constexpr int LegRoots[LegCount] = {3, 7, 11, 15};
    constexpr float Sole = 0.02f;
    constexpr float MaxStretch = 1.02f;
    constexpr float Dt = 1.0f / 60.0f;
    constexpr int SettleFrames = 240;
    constexpr float SlopeRadians = 20.0f * 3.14159265358979323846f / 180.0f;
    int g_Failures = 0;

    const char* const JointNames[JointCount] = {
        "Pelvis", "Spine", "Chest",
        "RearLeftRoot", "RearLeftMid", "RearLeftAnkle", "RearLeftToe",
        "RearRightRoot", "RearRightMid", "RearRightAnkle", "RearRightToe",
        "FrontLeftRoot", "FrontLeftMid", "FrontLeftAnkle", "FrontLeftToe",
        "FrontRightRoot", "FrontRightMid", "FrontRightAnkle", "FrontRightToe"
    };
    const int Parents[JointCount] = {
        -1, 0, 1, 0, 3, 4, 5, 0, 7, 8, 9, 2, 11, 12, 13, 2, 15, 16, 17
    };
    const char* const Config =
        R"({"version":1,"modifiers":[{"type":"FootIK","settings":{)"
        R"("legs":[{"name":"RearLeft","chain":["RearLeftRoot","RearLeftMid","RearLeftAnkle","RearLeftToe"],"pole":"auto","sole":0.02,"maxStretch":1.02},)"
        R"({"name":"RearRight","chain":["RearRightRoot","RearRightMid","RearRightAnkle","RearRightToe"],"pole":[1,0,0],"sole":0.02,"maxStretch":1.02},)"
        R"({"name":"FrontLeft","chain":["FrontLeftRoot","FrontLeftMid","FrontLeftAnkle","FrontLeftToe"],"pole":"auto","sole":0.02,"maxStretch":1.02},)"
        R"({"name":"FrontRight","chain":["FrontRightRoot","FrontRightMid","FrontRightAnkle","FrontRightToe"],"pole":[1,0,0],"sole":0.02,"maxStretch":1.02}],)"
        R"("body":{"pelvis":"Pelvis","chest":"Chest","maxDrop":0.25,"maxRaise":0.1,"smoothTime":0.12},)"
        R"("ground":{"rayUp":1.0,"rayDown":3.0,"maxSlopeDeg":50,"layerMask":4294967295}}}]})";

    void Check(bool bOk, const char* contract, int sample, double actual, double limit)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "LegIKModifierTest 検査失敗: %s sample=%d actual=%.17g limit=%.17g\n",
                         contract, sample, actual, limit);
            ++g_Failures;
        }
    }

    void Require(bool bOk, const char* contract)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "LegIKModifierTest 準備失敗: %s\n", contract);
            std::abort();
        }
    }

    void CheckWithin(double error, double limit, const char* contract, int sample)
    {
        Check(std::isfinite(error) && error <= limit, contract, sample, error, limit);
    }

    Math::Vector3 BindPosition(int joint)
    {
        if (joint == Pelvis)
        {
            return {-0.6f, 1.05f, 0.0f};
        }
        if (joint == 1)
        {
            return {-0.05f, 1.05f, 0.0f};
        }
        if (joint == Chest)
        {
            return {0.5f, 1.05f, 0.0f};
        }
        const int leg = (joint - 3) / 4;
        const int part = (joint - 3) % 4;
        const float x = leg < 2 ? -0.55f : 0.55f;
        const float z = leg % 2 == 0 ? -0.27f : 0.27f;
        switch (part)
        {
        case 0: return {x, 0.90f, z};
        case 1: return {x + 0.18f, 0.50f, z};
        case 2: return {x, 0.10f, z};
        default: return {x + 0.04f, Sole, z};
        }
    }

    class MockGround final : public Animation::IGroundQuery
    {
    public:
        enum class Terrain
        {
            Step,
            Slope,
            Unreachable
        };

        Terrain Shape = Terrain::Step;
        Animation::EGroundQueryStatus Status[LegCount] = {
            Animation::EGroundQueryStatus::Hit, Animation::EGroundQueryStatus::Hit,
            Animation::EGroundQueryStatus::Hit, Animation::EGroundQueryStatus::Hit
        };
        bool bSteepHit = false;

        static int LegAt(float x, float z)
        {
            return (x < 0.0f ? 0 : 2) + (z < 0.0f ? 0 : 1);
        }

        float Height(float x, float z) const
        {
            if (Shape == Terrain::Slope)
            {
                return std::tan(SlopeRadians) * x - 0.12f;
            }
            if (Shape == Terrain::Unreachable && LegAt(x, z) == 0)
            {
                return -1.0f;
            }
            return Shape == Terrain::Step && x < 0.0f ? -0.1f : 0.0f;
        }

        Animation::EGroundQueryStatus Query(const Math::Vector3& originAbove, float maxDown,
                                            Animation::GroundHit& out) const override
        {
            const int leg = LegAt(originAbove.x, originAbove.z);
            out = {};
            out.Status = Status[leg];
            if (out.Status != Animation::EGroundQueryStatus::Hit)
            {
                return out.Status;
            }
            const float height = Height(originAbove.x, originAbove.z);
            out.Distance = originAbove.y - height;
            if (out.Distance < 0.0f || out.Distance > maxDown)
            {
                out.Status = Animation::EGroundQueryStatus::NoHit;
                return out.Status;
            }
            out.Point = {originAbove.x, height, originAbove.z};
            out.Normal = Shape == Terrain::Slope
                ? Math::Vector3(-std::sin(SlopeRadians), std::cos(SlopeRadians), 0.0f)
                : Math::Vector3(0.0f, 1.0f, 0.0f);
            if (bSteepHit && leg == 3)
            {
                out.Normal = {1.0f, 0.0f, 0.0f};
            }
            return out.Status;
        }

        void QueryBatch(Container::Span<Animation::GroundProbe> probes) const override
        {
            for (auto& probe : probes)
            {
                Query(probe.OriginAbove, probe.MaxDown, probe.Result);
            }
        }
    };

    struct Fixture : Animation::IClipResolver
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        Container::TSharedPtr<AnimGraphResource> Graph;
        Container::TSharedPtr<Animation::IPoseModifier> ModifierOwner;
        Animation::FootIKModifier* Modifier = nullptr;
        MockGround Ground;
        World TestWorld;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Component::AnimatorComponent* Animator = nullptr;

        explicit Fixture(bool bRotated = false)
        {
            Require(Registry.Initialize(), "Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("LegIKSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("LegIKMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("LegIKClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("LegIKAsset");
            Require(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            Math::Matrix4x4 models[JointCount];
            for (int i = 0; i < JointCount; ++i)
            {
                Animation::JointTransform model;
                model.Translation = BindPosition(i);
                if (bRotated)
                {
                    const Math::Vector3 axis = Math::Vector3(
                        0.4f + 0.1f * float(i % 3), 0.7f - 0.05f * float(i % 4),
                        0.3f + 0.08f * float(i % 5)).Normalized();
                    const float angle = (i % 2 == 0 ? 1.0f : -1.0f) * (0.2f + 0.07f * float(i % 7));
                    model.Rotation = Math::QuaternionUtils::FromAxisAngle(axis, angle);
                }
                models[i] = Animation::ToRowMatrix(model);
                const auto inverse = Math::MatrixUtils::Inverse(models[i]);
                joints[i].Name = JointNames[i];
                joints[i].ParentIndex = Parents[i];
                for (size_t k = 0; k < 16; ++k)
                {
                    joints[i].InverseBindMatrix[k] = inverse.values[k];
                }
                const auto localMatrix = Parents[i] < 0
                    ? models[i]
                    : models[i] * Math::MatrixUtils::Inverse(models[Parents[i]]);
                const auto local = Animation::FromRowMatrix(localMatrix);
                rest[i].Translation = {local.Translation.x, local.Translation.y, local.Translation.z};
                rest[i].Rotation = {local.Rotation.x, local.Rotation.y, local.Rotation.z, local.Rotation.w};
                rest[i].Scale = {local.Scale.x, local.Scale.y, local.Scale.z};
            }
            Skeleton->SetJoints(std::move(joints));
            Require(Skeleton->SetAuthorRestPose(rest) && Skeleton->Load(), "試験用骨格の読み込み");

            Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 0;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
            Require(Mesh->Load(), "試験用メッシュの読み込み");

            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "a";
            clip.DurationSeconds = 1.0f;
            Skeletal::SkeletalAnimationChannel channel;
            channel.JointIndex = Pelvis;
            channel.Path = Skeletal::SkeletalAnimationPath::Translation;
            const auto root = BindPosition(Pelvis);
            channel.Samples.push_back({0.0f, {root.x, root.y, root.z, 0.0f}});
            channel.Samples.push_back({1.0f, {root.x, root.y, root.z, 0.0f}});
            clip.Channels.push_back(std::move(channel));
            Clip->SetClip(std::move(clip));
            Require(Clip->Load(), "試験用クリップの読み込み");
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            Require(Asset->Load(), "試験用アセットの読み込み");

            Graph = Registry.CreateTransient<AnimGraphResource>("LegIKGraph");
            Animation::AnimGraphReport report;
            const char* graphJson = R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})";
            Require(Graph && Graph->Compile(Container::String(graphJson), *Skeleton, *this, report) && Graph->Load(),
                    "試験用グラフの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            Require(Owner != nullptr, "試験用 Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = TestWorld.CreateComponent<Component::AnimatorComponent>(Owner);
            Require(MeshComponent && Animator, "試験用コンポーネントの作成");
            MeshComponent->SetSkeletalAsset(Asset);
            Require(Animator->SetGraph(Graph), "試験用グラフの接続");
        }

        ~Fixture()
        {
            TestWorld.Finalize();
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("a") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        void EnableModifier()
        {
            Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> modifiers;
            Procedural::PoseModifierConfigReport report;
            Require(Procedural::ParsePoseModifierConfig(Container::String(Config), *Skeleton, nullptr,
                        Procedural::GetBuiltinPoseModifierTypes(), modifiers, report) && modifiers.size() == 1,
                    "FootIK 設定の読み込み");
            ModifierOwner = modifiers[0];
            Modifier = Animation::FindFootIKModifier(ModifierOwner.get());
            Require(Modifier != nullptr, "FootIK modifier の取得");
            Modifier->SetGroundQuery(&Ground);
            Require(Animator->AddModifier(ModifierOwner), "FootIK modifier の接続");
        }

        void Tick()
        {
            const uint64_t previousSerial = MeshComponent->GetPoseSerial();
            TestWorld.Tick(Dt);
            Require(MeshComponent->IsPoseCurrent(), "更新済み姿勢の取得");
            Require(MeshComponent->GetPoseSerial() > previousSerial, "フレームごとの姿勢の更新");
        }

        Math::Vector3 Position(int joint, bool bWorld = false) const
        {
            Math::Matrix4x4 matrix;
            const bool bOk = bWorld
                ? MeshComponent->TryGetJointWorldMatrix(static_cast<uint32_t>(joint), matrix)
                : MeshComponent->TryGetJointModelMatrix(static_cast<uint32_t>(joint), matrix);
            Require(bOk, "関節行列の取得");
            return matrix.GetTranslationRow();
        }

        bool IsFinite() const
        {
            const auto& pose = Animator->GetFinalPose();
            const auto& palette = MeshComponent->GetPoseSnapshot().BonePalette;
            if (pose.size() != JointCount || palette.size() != JointCount)
            {
                return false;
            }
            for (const auto& joint : pose)
            {
                if (!std::isfinite(joint.Translation.x) || !std::isfinite(joint.Translation.y) ||
                    !std::isfinite(joint.Translation.z) || !std::isfinite(joint.Rotation.x) ||
                    !std::isfinite(joint.Rotation.y) || !std::isfinite(joint.Rotation.z) ||
                    !std::isfinite(joint.Rotation.w) || !std::isfinite(joint.Scale.x) ||
                    !std::isfinite(joint.Scale.y) || !std::isfinite(joint.Scale.z))
                {
                    return false;
                }
            }
            for (const auto& matrix : palette)
            {
                for (float value : matrix.values)
                {
                    if (!std::isfinite(value))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        uint64_t PaletteHash() const
        {
            uint64_t hash = UINT64_C(14695981039346656037);
            const auto& palette = MeshComponent->GetPoseSnapshot().BonePalette;
            Require(palette.size() == JointCount, "骨のパレットの取得");
            for (const auto& matrix : palette)
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(matrix.values);
                for (size_t i = 0; i < sizeof(matrix.values); ++i)
                {
                    hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
                }
            }
            return hash;
        }
    };

    void Settle(Fixture& fixture)
    {
        for (int frame = 0; frame < SettleFrames; ++frame)
        {
            fixture.Tick();
        }
    }

    void CheckFeet(const Fixture& fixture, const char* contract)
    {
        for (int leg = 0; leg < LegCount; ++leg)
        {
            const auto toe = fixture.Position(LegRoots[leg] + 3, true);
            const float expected = fixture.Ground.Height(toe.x, toe.z) + Sole;
            CheckWithin(std::fabs(double(toe.y) - expected), 0.01, contract, leg);
        }
    }

    struct StepResult
    {
        Math::Vector3 Before[JointCount];
        Math::Vector3 After[JointCount];
        uint64_t Palette = 0;
    };

    StepResult RunStep(bool bRotated, bool bCheckStep)
    {
        Fixture fixture(bRotated);
        fixture.Tick();
        StepResult result;
        for (int joint = 0; joint < JointCount; ++joint)
        {
            result.Before[joint] = fixture.Position(joint);
        }
        fixture.EnableModifier();
        Settle(fixture);
        for (int joint = 0; joint < JointCount; ++joint)
        {
            result.After[joint] = fixture.Position(joint);
        }
        result.Palette = fixture.PaletteHash();
        if (bCheckStep)
        {
            CheckFeet(fixture, "段差の足先と地面の高さ");
            const double pelvisDelta = double(result.After[Pelvis].y) - result.Before[Pelvis].y;
            CheckWithin(std::fabs(pelvisDelta - (-0.1)), 0.001, "段差の骨盤の下降量", 0);
            const double pitch = fixture.Modifier->GetBodyOutput().PitchRadians;
            Check(std::isfinite(pitch) && pitch > 0.0, "段差の体のピッチ", 0, pitch, 0.0);
            const double rise = double(result.After[Chest].y - result.After[Pelvis].y) -
                                double(result.Before[Chest].y - result.Before[Pelvis].y);
            Check(std::isfinite(rise) && rise > 0.0, "段差の胸が上がる向き", 0, rise, 0.0);
        }
        return result;
    }

    void TestSlope()
    {
        Fixture fixture;
        fixture.Ground.Shape = MockGround::Terrain::Slope;
        fixture.EnableModifier();
        Settle(fixture);
        CheckFeet(fixture, "20 度の斜面の足先と地面の高さ");
    }

    void TestMaxStretch()
    {
        Fixture fixture;
        fixture.Ground.Shape = MockGround::Terrain::Unreachable;
        fixture.Tick();
        const double bindLength = double((fixture.Position(4) - fixture.Position(3)).Length()) +
                                  double((fixture.Position(5) - fixture.Position(4)).Length());
        fixture.EnableModifier();
        Settle(fixture);
        const auto root = fixture.Position(3);
        const auto mid = fixture.Position(4);
        const auto ankle = fixture.Position(5);
        const double actual = double((mid - root).Length()) + double((ankle - mid).Length());
        const double limit = bindLength * MaxStretch + 1e-4;
        Check(std::isfinite(actual) && actual <= limit, "最大伸長後の脚の長さ", 0, actual, limit);
        const Math::Vector3 unreachableAnkle(-0.55f, -0.90f, -0.27f);
        const double targetDistance = (unreachableAnkle - root).Length();
        Check(targetDistance > bindLength * MaxStretch, "目標が脚の可到達範囲の外", 0,
              targetDistance, bindLength * MaxStretch);
        Check(fixture.IsFinite(), "最大伸長時の姿勢とパレットの有限性", 0, fixture.IsFinite() ? 1.0 : 0.0, 1.0);
    }

    void SetFailureStatuses(MockGround& ground, bool bSteepHit)
    {
        ground.Status[0] = Animation::EGroundQueryStatus::NotReady;
        ground.Status[1] = Animation::EGroundQueryStatus::WrongThread;
        ground.Status[2] = Animation::EGroundQueryStatus::NoHit;
        ground.Status[3] = bSteepHit ? Animation::EGroundQueryStatus::Hit : Animation::EGroundQueryStatus::TooSteep;
        ground.bSteepHit = bSteepHit;
    }

    void CheckFailureDecay(Fixture& fixture, bool bSteepHit)
    {
        float previous[LegCount];
        int firstIncrease[LegCount] = {-1, -1, -1, -1};
        double increase[LegCount] = {};
        int firstNonFinite = -1;
        for (int leg = 0; leg < LegCount; ++leg)
        {
            previous[leg] = fixture.Modifier->GetLegWeight(leg);
            Check(previous[leg] > 0.99f, "接地失敗前の重み", leg, previous[leg], 0.99);
        }
        SetFailureStatuses(fixture.Ground, bSteepHit);
        for (int frame = 0; frame < SettleFrames; ++frame)
        {
            fixture.Tick();
            if (!fixture.IsFinite() && firstNonFinite < 0)
            {
                firstNonFinite = frame;
            }
            for (int leg = 0; leg < LegCount; ++leg)
            {
                const float weight = fixture.Modifier->GetLegWeight(leg);
                if ((!std::isfinite(weight) || weight > previous[leg]) && firstIncrease[leg] < 0)
                {
                    firstIncrease[leg] = frame;
                    increase[leg] = double(weight) - previous[leg];
                }
                previous[leg] = weight;
            }
        }
        Check(firstNonFinite < 0, "接地失敗中の姿勢とパレットの有限性", firstNonFinite, firstNonFinite, -1);
        const Animation::EGroundQueryStatus expected[LegCount] = {
            Animation::EGroundQueryStatus::NotReady, Animation::EGroundQueryStatus::WrongThread,
            Animation::EGroundQueryStatus::NoHit, Animation::EGroundQueryStatus::TooSteep
        };
        for (int leg = 0; leg < LegCount; ++leg)
        {
            Check(firstIncrease[leg] < 0, "接地失敗後の重みの単調減少", leg, increase[leg], 0.0);
            Check(std::isfinite(previous[leg]) && previous[leg] < 0.05f,
                  "接地失敗後の重みの収束", leg, previous[leg], 0.05);
            const auto status = fixture.Modifier->GetLegStatus(leg);
            Check(status == expected[leg], "接地失敗の状態", leg, static_cast<int>(status),
                  static_cast<int>(expected[leg]));
        }
    }

    void TestFailureStates()
    {
        Fixture fixture;
        fixture.EnableModifier();
        Settle(fixture);
        CheckFailureDecay(fixture, false);
        for (auto& status : fixture.Ground.Status)
        {
            status = Animation::EGroundQueryStatus::Hit;
        }
        for (int frame = 0; frame < SettleFrames; ++frame)
        {
            fixture.Tick();
            Check(fixture.IsFinite(), "接地回復中の姿勢とパレットの有限性", frame,
                  fixture.IsFinite() ? 1.0 : 0.0, 1.0);
        }
        CheckFailureDecay(fixture, true);
    }

    void TestConfiguration()
    {
        Fixture fixture;
        const auto types = Procedural::GetBuiltinPoseModifierTypes();
        bool bFound = false;
        for (const auto& entry : types.Entries)
        {
            if (std::strcmp(entry.Name, "FootIK") == 0)
            {
                bFound = true;
                Check(entry.DefaultOrder == 500, "FootIK の既定 order", 0, entry.DefaultOrder, 500);
            }
        }
        Check(bFound, "組み込み型の FootIK", 0, bFound ? 1.0 : 0.0, 1.0);

        Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> modifiers;
        Procedural::PoseModifierConfigReport report;
        const bool bValid = Procedural::ParsePoseModifierConfig(
            Container::String(Config), *fixture.Skeleton, nullptr, types, modifiers, report);
        Check(bValid && modifiers.size() == 1, "四足の JSON 設定", 0, bValid ? 1.0 : 0.0, 1.0);
        const char* unknownJoint =
            R"({"version":1,"modifiers":[{"type":"FootIK","settings":{"legs":[{"name":"bad","chain":["Missing","RearLeftMid","RearLeftAnkle"]}]}}]})";
        report = {};
        const bool bJointAccepted = Procedural::ParsePoseModifierConfig(
            Container::String(unknownJoint), *fixture.Skeleton, nullptr, types, modifiers, report);
        Check(!bJointAccepted && report.Error == Procedural::PoseModifierConfigError::UnknownJoint,
              "未知の関節名の拒否", 0, static_cast<int>(report.Error),
              static_cast<int>(Procedural::PoseModifierConfigError::UnknownJoint));
        const char* unknownKey =
            R"({"version":1,"modifiers":[{"type":"FootIK","settings":{"legs":[{"name":"bad","chain":["RearLeftRoot","RearLeftMid","RearLeftAnkle"]}],"unknown":1}}]})";
        report = {};
        const bool bKeyAccepted = Procedural::ParsePoseModifierConfig(
            Container::String(unknownKey), *fixture.Skeleton, nullptr, types, modifiers, report);
        Check(!bKeyAccepted && report.Error == Procedural::PoseModifierConfigError::UnknownKey,
              "未知のキーの拒否", 0, static_cast<int>(report.Error),
              static_cast<int>(Procedural::PoseModifierConfigError::UnknownKey));
    }
} // namespace

void TestLegIKModifier()
{
    const StepResult step = RunStep(false, true);
    TestSlope();
    TestMaxStretch();
    const StepResult rotated = RunStep(true, false);
    for (int joint = 0; joint < JointCount; ++joint)
    {
        CheckWithin((step.Before[joint] - rotated.Before[joint]).Length(), 1e-4, "軸変更前の関節位置", joint);
        CheckWithin((step.After[joint] - rotated.After[joint]).Length(), 1e-4, "軸変更後の関節位置", joint);
    }
    TestFailureStates();
    const StepResult repeated = RunStep(false, false);
    if (step.Palette != repeated.Palette)
    {
        std::fprintf(stderr, "LegIKModifierTest パレットのハッシュ: first=%016llx second=%016llx\n",
                     static_cast<unsigned long long>(step.Palette), static_cast<unsigned long long>(repeated.Palette));
    }
    Check(step.Palette == repeated.Palette, "段差のパレットの決定性", 0,
          step.Palette == repeated.Palette ? 1.0 : 0.0, 1.0);
    TestConfiguration();
    if (g_Failures != 0)
    {
        std::fprintf(stderr, "LegIKModifierTest 失敗件数: %d\n", g_Failures);
        std::abort();
    }
    std::printf("LegIKModifierTest 合格\n");
}
