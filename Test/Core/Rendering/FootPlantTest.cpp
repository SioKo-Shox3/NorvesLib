// 実際のアニメ更新経路で足の固定・離地・着地・テレポート・決定性を検証する。
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    constexpr int JointCount = 3;
    constexpr int Toe = 2;
    constexpr float Sole = 0.02f;
    constexpr float RootHeight = 0.8f;
    constexpr float BoneLength = 0.5f;
    constexpr float Stride = 0.28f;
    constexpr float Speed = 0.8f;
    constexpr float Lift = 0.24f;
    constexpr float Dt = 1.0f / 60.0f;
    constexpr int Frames = 120;
    int g_Failures = 0;

    void Check(bool bOk, const char* contract, int sample, double actual, double limit)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "FootPlantTest 検査失敗: %s 標本=%d 実測=%.17g 限界=%.17g\n",
                         contract, sample, actual, limit);
            ++g_Failures;
        }
    }

    void Require(bool bOk, const char* contract)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "FootPlantTest 準備失敗: %s\n", contract);
            std::abort();
        }
    }

    void CheckWithin(double error, double limit, const char* contract, int sample)
    {
        Check(std::isfinite(error) && error <= limit, contract, sample, error, limit);
    }

    double Horizontal(const Math::Vector3& a, const Math::Vector3& b)
    {
        return std::hypot(double(a.x) - b.x, double(a.z) - b.z);
    }

    Math::Vector3 BindPosition(int joint)
    {
        if (joint == 0)
        {
            return {0.0f, RootHeight, 0.0f};
        }
        if (joint == 1)
        {
            const float halfHeight = (RootHeight - Sole) * 0.5f;
            return {std::sqrt(BoneLength * BoneLength - halfHeight * halfHeight),
                    (RootHeight + Sole) * 0.5f, 0.0f};
        }
        return {0.0f, Sole, 0.0f};
    }

    Math::Vector3 AnimatedToe(double phase)
    {
        if (phase <= 0.5)
        {
            return {float(Stride * (0.5 - 2.0 * phase)), Sole, 0.0f};
        }
        const double swing = (phase - 0.5) * 2.0;
        return {float(Stride * (swing - 0.5)),
                Sole + float(Lift * (1.0 - std::abs(2.0 * swing - 1.0))), 0.0f};
    }

    class MockGround final : public Animation::IGroundQuery
    {
    public:
        Animation::EGroundQueryStatus Query(const Math::Vector3& originAbove, float maxDown,
                                            Animation::GroundHit& out) const override
        {
            out = {};
            out.Distance = originAbove.y;
            out.Status = out.Distance >= 0.0f && out.Distance <= maxDown
                ? Animation::EGroundQueryStatus::Hit : Animation::EGroundQueryStatus::NoHit;
            out.Point = {originAbove.x, 0.0f, originAbove.z};
            out.Normal = {0.0f, 1.0f, 0.0f};
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
        double Time = 0.0;
        float Offset = 0.0f;

        explicit Fixture(bool bPlant = true, bool bEvents = true)
        {
            Require(Registry.Initialize(), "Registry の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("FootPlantSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("FootPlantMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("FootPlantClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("FootPlantAsset");
            Require(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            const char* const names[JointCount] = {"Root", "Mid", "Toe"};
            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            Math::Matrix4x4 models[JointCount];
            for (int i = 0; i < JointCount; ++i)
            {
                Animation::JointTransform model;
                model.Translation = BindPosition(i);
                models[i] = Animation::ToRowMatrix(model);
                const auto inverse = Math::MatrixUtils::Inverse(models[i]);
                joints[i].Name = names[i];
                joints[i].ParentIndex = i - 1;
                for (size_t k = 0; k < 16; ++k)
                {
                    joints[i].InverseBindMatrix[k] = inverse.values[k];
                }
                const auto local = Animation::FromRowMatrix(i == 0 ? models[i]
                    : models[i] * Math::MatrixUtils::Inverse(models[i - 1]));
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
            Skeletal::SkeletalAnimationChannel channels[JointCount];
            for (int i = 0; i < JointCount; ++i)
            {
                channels[i].JointIndex = i;
                channels[i].Path = Skeletal::SkeletalAnimationPath::Rotation;
            }
            const auto upper = BindPosition(1) - BindPosition(0);
            const auto lower = BindPosition(2) - BindPosition(1);
            const double bindUpper = std::atan2(double(upper.y), double(upper.x));
            const double bindLower = std::atan2(double(lower.y), double(lower.x));
            // 二本の長さを保つ平面三角形から曲線を作る。製品の IK は期待値に使わない。
            for (int sample = 0; sample <= 120; ++sample)
            {
                const double phase = double(sample) / 120.0;
                const auto toe = AnimatedToe(phase);
                const double dx = toe.x;
                const double dy = double(toe.y) - RootHeight;
                const double distance = std::hypot(dx, dy);
                const double height = std::sqrt(double(BoneLength) * BoneLength - distance * distance * 0.25);
                const double kneeX = dx * 0.5 - dy / distance * height;
                const double kneeY = RootHeight + dy * 0.5 + dx / distance * height;
                const double rootAngle = std::atan2(kneeY - RootHeight, kneeX) - bindUpper;
                const double midAngle = std::atan2(double(toe.y) - kneeY, double(toe.x) - kneeX)
                    - bindLower - rootAngle;
                const double angles[JointCount] = {rootAngle, midAngle, -rootAngle - midAngle};
                for (int i = 0; i < JointCount; ++i)
                {
                    const auto q = Math::QuaternionUtils::FromAxisAngle(Math::Vector3::UnitZ, float(angles[i]));
                    channels[i].Samples.push_back({float(phase), {q.x, q.y, q.z, q.w}});
                }
            }
            for (auto& channel : channels)
            {
                clip.Channels.push_back(std::move(channel));
            }
            Clip->SetClip(std::move(clip));
            Require(Clip->Load(), "試験用クリップの読み込み");
            if (bEvents)
            {
                Animation::ClipMetadataReport report;
                Require(Clip->ApplyMetadataJson(Container::String(
                    R"({"events":[{"name":"Foot.Left.Down","t":0.0,"end":0.5}]})"), report),
                    "接地の窓イベントの読み込み");
            }
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            Require(Asset->Load(), "試験用アセットの読み込み");
            Graph = Registry.CreateTransient<AnimGraphResource>("FootPlantGraph");
            Animation::AnimGraphReport graphReport;
            Require(Graph && Graph->Compile(Container::String(
                R"({"version":1,"nodes":[{"id":"a","type":"clip","clip":"a"}],"root":"a"})"),
                *Skeleton, *this, graphReport) && Graph->Load(), "試験用グラフの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            Require(Owner != nullptr, "試験用 Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = TestWorld.CreateComponent<Component::AnimatorComponent>(Owner);
            Require(MeshComponent && Animator, "試験用コンポーネントの作成");
            MeshComponent->SetSkeletalAsset(Asset);
            Require(Animator->SetGraph(Graph), "試験用グラフの接続");

            const char* config = bPlant
                ? R"({"version":1,"modifiers":[{"type":"FootIK","settings":{"legs":[{"name":"Left","chain":["Root","Mid","Toe"],"sole":0.02,"maxStretch":1.02}],"plant":{}}}]})"
                : R"({"version":1,"modifiers":[{"type":"FootIK","settings":{"legs":[{"name":"Left","chain":["Root","Mid","Toe"],"sole":0.02,"maxStretch":1.02}]}}]})";
            Container::VariableArray<Container::TSharedPtr<Animation::IPoseModifier>> modifiers;
            Procedural::PoseModifierConfigReport modifierReport;
            Require(Procedural::ParsePoseModifierConfig(Container::String(config), *Skeleton, nullptr,
                        Procedural::GetBuiltinPoseModifierTypes(), modifiers, modifierReport) && modifiers.size() == 1,
                    "FootIK 設定の読み込み");
            ModifierOwner = modifiers[0];
            Modifier = Animation::FindFootIKModifier(ModifierOwner.get());
            Require(Modifier != nullptr, "FootIK modifier の取得");
            Modifier->SetGroundQuery(&Ground);
            Require(Animator->AddModifier(ModifierOwner), "FootIK modifier の接続");
        }

        ~Fixture()
        {
            TestWorld.Finalize();
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("a") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        void Tick(float dt = Dt)
        {
            Time += double(dt);
            Owner->SetPosition(float(double(Speed) * Time) + Offset, 0.0f, 0.0f);
            const uint64_t previousSerial = MeshComponent->GetPoseSerial();
            TestWorld.Tick(dt);
            Require(MeshComponent->IsPoseCurrent() && MeshComponent->GetPoseSerial() > previousSerial,
                    "フレームごとの最終姿勢の更新");
        }

        Math::Vector3 Position(int joint = Toe) const
        {
            Math::Matrix4x4 matrix;
            Require(MeshComponent->TryGetJointWorldMatrix(static_cast<uint32_t>(joint), matrix),
                    "最終姿勢の関節ワールド行列の取得");
            return matrix.GetTranslationRow();
        }

        bool IsPlanted() const
        {
            return Modifier->GetLegPlantPhase(0) == Animation::EFootPlantPhase::Planted;
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

    void Settle(Fixture& fixture, int rate = 60)
    {
        for (int frame = 0; frame < 2 * rate; ++frame)
        {
            fixture.Tick(1.0f / float(rate));
        }
        Require(fixture.Modifier->GetLegWeight(0) > 0.999f, "問い合わせ重みの収束");
    }

    void RunContactScene()
    {
        Fixture planted;
        Fixture disabled(false);
        Fixture threshold(true, false);
        Settle(planted);
        Settle(disabled);
        Settle(threshold);
        Math::Vector3 previous[3] = {planted.Position(), disabled.Position(), threshold.Position()};
        double maximum[3] = {};
        double maximumLift = 0.0;
        int samples = 0;
        int swingSamples = 0;
        for (int frame = 1; frame <= Frames; ++frame)
        {
            planted.Tick();
            disabled.Tick();
            threshold.Tick();
            const Math::Vector3 current[3] = {planted.Position(), disabled.Position(), threshold.Position()};
            const int phaseFrame = frame % 60;
            // 入ったフレームと離地のフレームを除く、窓の内部を全て調べる。
            if (phaseFrame >= 1 && phaseFrame <= 29)
            {
                Check(planted.IsPlanted(), "A: 窓の間はイベント駆動の固定を保つ", frame,
                      planted.IsPlanted() ? 1.0 : 0.0, 1.0);
                Check(threshold.IsPlanted(), "A: イベントなしでもしきい値駆動で固定する", frame,
                      threshold.IsPlanted() ? 1.0 : 0.0, 1.0);
                if (phaseFrame >= 2)
                {
                    ++samples;
                    for (int i = 0; i < 3; ++i)
                    {
                        const double movement = Horizontal(current[i], previous[i]);
                        maximum[i] = std::max(maximum[i], movement);
                        if (i == 1)
                        {
                            Check(std::isfinite(movement) && movement > 0.003,
                                  "A: 同じ標本の固定なしは明らかに滑る", frame, movement, 0.003);
                        }
                        else
                        {
                            CheckWithin(movement, 0.002, i == 0 ? "A: イベント駆動の足滑り"
                                        : "A: しきい値駆動の足滑り", frame);
                        }
                    }
                }
            }
            if (phaseFrame == 31)
            {
                Check(!planted.IsPlanted(), "C: 窓の End が離地を伝える", frame,
                      planted.IsPlanted() ? 1.0 : 0.0, 0.0);
            }
            if (phaseFrame >= 31 && phaseFrame <= 59)
            {
                Check(!planted.IsPlanted(), "C: 遊脚中は Swing を保つ", frame,
                      planted.IsPlanted() ? 1.0 : 0.0, 0.0);
                const double lift = double(current[0].y) - Sole;
                Check(std::isfinite(lift), "C: 遊脚の高さは有限", frame, lift, Lift);
                maximumLift = std::max(maximumLift, lift);
                ++swingSamples;
            }
            for (int i = 0; i < 3; ++i)
            {
                previous[i] = current[i];
            }
        }
        Check(samples == 56, "A: 二周期の接地標本数", 0, samples, 56);
        Check(swingSamples == 58 && maximumLift >= double(Lift) * 0.5,
              "C: 最終姿勢が持ち上がりの半分以上へ達する", 0, maximumLift, double(Lift) * 0.5);
        std::printf("FootPlantTest A: 最大水平移動[m/フレーム] イベント=%.9g 固定なし=%.9g しきい値=%.9g 標本=%d\n",
                    maximum[0], maximum[1], maximum[2], samples);
        std::printf("FootPlantTest C: 最大持ち上がり[m]=%.9g\n", maximumLift);
    }

    void CheckStretchRelease()
    {
        Fixture fixture;
        Settle(fixture);
        for (int frame = 0; frame < 10; ++frame)
        {
            fixture.Tick();
        }
        Check(fixture.IsPlanted(), "B: 急移動の前は Planted", 0, fixture.IsPlanted() ? 1.0 : 0.0, 1.0);
        constexpr float jump = 0.9f;
        const auto anchor = fixture.Modifier->GetLegToeTargetWorld(0);
        auto nextRoot = fixture.Position(0);
        nextRoot.x += jump + Speed * Dt;
        const double distance = (anchor - nextRoot).Length();
        const double maximumReach = 2.0 * BoneLength * fixture.Modifier->GetSettings().Legs[0].MaxStretch;
        Check(distance > maximumReach, "B: 固定を保つと maxStretch を超える移動", 0, distance, maximumReach);
        Check(jump + Speed * Dt < fixture.Modifier->GetSettings().Land.TeleportDistance,
              "B: 自動テレポートのしきい値未満", 0, jump + Speed * Dt,
              fixture.Modifier->GetSettings().Land.TeleportDistance);
        fixture.Offset += jump;
        fixture.Tick();
        const bool bReleasedImmediately = !fixture.IsPlanted();
        fixture.Tick();
        Check(bReleasedImmediately || !fixture.IsPlanted(), "B: 急移動の当該フレームか次で Swing へ解除", 0,
              bReleasedImmediately || !fixture.IsPlanted() ? 1.0 : 0.0, 1.0);
        std::printf("FootPlantTest B: 追加移動[m]=%.9g 必要な脚長[m]=%.9g 上限[m]=%.9g 即時解除=%d\n",
                    double(jump), distance, maximumReach, int(bReleasedImmediately));
    }

    void CheckLanding()
    {
        Fixture fixture;
        Settle(fixture);
        Animation::AnimDriveSignals signals;
        signals.bGrounded = false;
        Require(fixture.Animator->SetDriveSignals(signals), "空中の駆動信号の設定");
        for (int frame = 0; frame < 5; ++frame)
        {
            fixture.Tick();
            Check(fixture.Modifier->GetLegLandWeight(0) == 0.0f,
                  "D: 非接地中は着地重みがゼロ", frame, fixture.Modifier->GetLegLandWeight(0), 0.0);
        }
        signals.bGrounded = true;
        Require(fixture.Animator->SetDriveSignals(signals), "接地の駆動信号の設定");
        double maximumError = 0.0;
        for (int k = 1; k <= 12; ++k)
        {
            fixture.Tick();
            const auto animToe = AnimatedToe(fixture.Time - std::floor(fixture.Time));
            CheckWithin(std::abs(double(animToe.y) - Sole), fixture.Modifier->GetSettings().Land.SnapDistance,
                        "D: 着地の標本は接地の窓で地面に近い", k);
            const double expected = std::min(1.0, double(k) * Dt /
                                            fixture.Modifier->GetSettings().Land.BlendSeconds);
            const double actual = fixture.Modifier->GetLegLandWeight(0);
            const double error = std::abs(actual - expected);
            maximumError = std::max(maximumError, error);
            CheckWithin(error, 1e-3, "D: 開始フレームから直線で増えて一に止まる着地重み", k);
        }
        std::printf("FootPlantTest D: blendSeconds=%.9g 最大重み誤差=%.9g 最終重み=%.9g\n",
                    double(fixture.Modifier->GetSettings().Land.BlendSeconds), maximumError,
                    double(fixture.Modifier->GetLegLandWeight(0)));
    }

    void CheckTeleport()
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            Fixture fixture;
            Settle(fixture);
            for (int frame = 0; frame < 10; ++frame)
            {
                fixture.Tick();
            }
            Check(fixture.IsPlanted(), "E: 移動前の足は Planted", mode, fixture.IsPlanted() ? 1.0 : 0.0, 1.0);
            const auto oldTarget = fixture.Modifier->GetLegToeTargetWorld(0);
            const float jump = mode == 0 ? 5.0f : 0.4f;
            fixture.Offset += jump;
            if (mode == 1)
            {
                fixture.Modifier->NotifyTeleport();
            }
            fixture.Tick();
            auto expected = AnimatedToe(fixture.Time - std::floor(fixture.Time));
            expected.x += fixture.Owner->GetPosition().x;
            const auto target = fixture.Modifier->GetLegToeTargetWorld(0);
            const double error = Horizontal(target, expected);
            CheckWithin(error, 0.01, mode == 0 ? "E: 距離によるテレポートは新しいアニメ足先へ戻す"
                        : "E: NotifyTeleport はしきい値未満でも新しいアニメ足先へ戻す", mode);
            Check(Horizontal(target, oldTarget) > double(jump) - 0.02,
                  "E: 古い固定位置へ引き戻さない", mode, Horizontal(target, oldTarget), double(jump) - 0.02);
            std::printf("FootPlantTest E: %s 追加移動[m]=%.9g 新しい足先との水平差[m]=%.9g\n",
                        mode == 0 ? "距離検出" : "明示通知", double(jump), error);
        }
    }

    struct Trace
    {
        float Height[Frames] = {};
        uint64_t Palette[Frames] = {};
    };

    Trace RunTrace(int rate)
    {
        Fixture fixture;
        Settle(fixture, rate);
        Trace trace;
        const int substeps = rate / 60;
        for (int sample = 0; sample < Frames; ++sample)
        {
            for (int step = 0; step < substeps; ++step)
            {
                fixture.Tick(1.0f / float(rate));
            }
            trace.Height[sample] = fixture.Position().y;
            trace.Palette[sample] = fixture.PaletteHash();
        }
        return trace;
    }

    void CheckDeterminism()
    {
        const Trace first = RunTrace(60);
        const Trace finer = RunTrace(120);
        const Trace repeated = RunTrace(60);
        double maximumDifference = 0.0;
        int mismatches = 0;
        for (int sample = 0; sample < Frames; ++sample)
        {
            const double difference = std::abs(double(first.Height[sample]) - finer.Height[sample]);
            maximumDifference = std::max(maximumDifference, difference);
            CheckWithin(difference, 0.002, "F: 共通時刻の最終姿勢の足先高さは刻みに依存しない", sample);
            if (first.Palette[sample] != repeated.Palette[sample])
            {
                ++mismatches;
                std::fprintf(stderr, "FootPlantTest F: パレットのハッシュ 標本=%d 初回=%016llx 再実行=%016llx\n",
                             sample, static_cast<unsigned long long>(first.Palette[sample]),
                             static_cast<unsigned long long>(repeated.Palette[sample]));
            }
            Check(first.Palette[sample] == repeated.Palette[sample], "F: 同じ刻み列のパレットの決定性", sample,
                  first.Palette[sample] == repeated.Palette[sample] ? 1.0 : 0.0, 1.0);
        }
        std::printf("FootPlantTest F: 60/120 Hz 最大高さ差[m]=%.9g パレット不一致=%d 共通標本=%d\n",
                    maximumDifference, mismatches, Frames);
    }
} // 無名名前空間

void TestFootPlant()
{
    RunContactScene();
    CheckStretchRelease();
    CheckLanding();
    CheckTeleport();
    CheckDeterminism();
    std::fflush(stdout);
    if (g_Failures != 0)
    {
        std::fprintf(stderr, "FootPlantTest 失敗件数: %d\n", g_Failures);
        std::abort();
    }
    std::printf("FootPlantTest 合格\n");
}
