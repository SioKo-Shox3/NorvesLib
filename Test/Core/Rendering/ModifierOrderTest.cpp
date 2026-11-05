// 四足歩行の姿勢後処理の順序と接地への影響を検証する。
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
    using NorvesLib::Core::Identity;
    using NorvesLib::Core::ResourceRegistry;
    using NorvesLib::Core::SkeletalAssetResource;
    using NorvesLib::Core::SkeletonResource;
    using NorvesLib::Core::SkinnedMeshResource;
    using NorvesLib::Core::World;

    constexpr int JointCount = 23;
    constexpr int LegCount = 4;
    constexpr int Frames = 120;
    constexpr int LegRoots[LegCount] = {11, 14, 17, 20};
    constexpr float HipHeight = 0.8f;
    constexpr float BoneLength = 0.5f;
    constexpr float Stride = 0.16f;
    constexpr float Slope = 12.0f * 3.14159265358979323846f / 180.0f;

    const char* const JointNames[JointCount] = {
        "Pelvis", "Spine0", "Spine1", "Chest", "Neck", "Head", "EarLeft", "EarRight",
        "TailRoot", "TailMid", "TailTip",
        "RearLeftRoot", "RearLeftMid", "RearLeftFoot", "RearRightRoot", "RearRightMid", "RearRightFoot",
        "FrontLeftRoot", "FrontLeftMid", "FrontLeftFoot", "FrontRightRoot", "FrontRightMid", "FrontRightFoot"
    };
    const int Parents[JointCount] = {
        -1, 0, 1, 2, 3, 4, 5, 5, 0, 8, 9, 0, 11, 12, 0, 14, 15, 3, 17, 18, 3, 20, 21
    };

    void Require(bool bOk, const char* message)
    {
        if (!bOk)
        {
            std::fprintf(stderr, "ModifierOrderTest 準備失敗: %s\n", message);
            std::abort();
        }
    }

    Math::Vector3 BindPosition(int joint)
    {
        const Math::Vector3 body[] = {
            {0.0f, 0.84f, -0.45f}, {0.0f, 0.87f, -0.15f}, {0.0f, 0.89f, 0.15f},
            {0.0f, 0.90f, 0.45f}, {0.0f, 1.02f, 0.62f}, {0.0f, 1.14f, 0.80f},
            {-0.12f, 1.30f, 0.82f}, {0.12f, 1.30f, 0.82f},
            {0.0f, 0.92f, -0.60f}, {0.0f, 0.90f, -0.80f}, {0.0f, 0.87f, -1.00f}
        };
        if (joint < LegRoots[0])
        {
            return body[joint];
        }
        const int leg = (joint - LegRoots[0]) / 3;
        const int part = (joint - LegRoots[0]) % 3;
        const float x = leg % 2 == 0 ? -0.28f : 0.28f;
        const float z = leg < 2 ? -0.45f : 0.45f;
        if (part == 0)
        {
            return {x, HipHeight, z};
        }
        if (part == 1)
        {
            const float half = HipHeight * 0.5f;
            return {x, half, z + std::sqrt(BoneLength * BoneLength - half * half)};
        }
        return {x, 0.0f, z};
    }

    class MockGround final : public Animation::IGroundQuery
    {
    public:
        float Height(float x) const
        {
            return std::tan(Slope) * x;
        }

        Animation::EGroundQueryStatus Query(const Math::Vector3& originAbove, float maxDown,
                                            Animation::GroundHit& out) const override
        {
            out = {};
            const float height = Height(originAbove.x);
            out.Distance = originAbove.y - height;
            out.Status = out.Distance >= 0.0f && out.Distance <= maxDown
                ? Animation::EGroundQueryStatus::Hit : Animation::EGroundQueryStatus::NoHit;
            out.Point = {originAbove.x, height, originAbove.z};
            out.Normal = {-std::sin(Slope), std::cos(Slope), 0.0f};
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

    // 生成した組み込み FootIK を借用し、Animator への設定後に地面を渡す。
    struct FootFactoryProbe
    {
        static Procedural::PoseModifierFactory& Factory()
        {
            static Procedural::PoseModifierFactory factory = nullptr;
            return factory;
        }

        static Animation::IPoseModifier*& Last()
        {
            static Animation::IPoseModifier* last = nullptr;
            return last;
        }

        static bool Create(const Procedural::PoseModifierCreateArgs& args,
                           Container::TSharedPtr<Animation::IPoseModifier>& out,
                           Procedural::PoseModifierConfigReport& report)
        {
            if (!Factory()(args, out, report))
            {
                return false;
            }
            Last() = out.get();
            return true;
        }
    };

    struct FrameSample
    {
        uint64_t Hash = 0;
        double GroundError[LegCount] = {};
        bool bPlanted[LegCount] = {};
        bool bExpectedContact[LegCount] = {};
    };

    struct Fixture final : Animation::IClipResolver
    {
        ResourceRegistry Registry;
        Container::TSharedPtr<SkeletonResource> Skeleton;
        Container::TSharedPtr<SkinnedMeshResource> Mesh;
        Container::TSharedPtr<AnimationClipResource> Clip;
        Container::TSharedPtr<SkeletalAssetResource> Asset;
        Container::TSharedPtr<AnimGraphResource> Graph;
        MockGround Ground;
        World TestWorld;
        Entity* Owner = nullptr;
        Component::SkinnedMeshComponent* MeshComponent = nullptr;
        Component::AnimatorComponent* Animator = nullptr;
        Animation::FootIKModifier* Foot = nullptr;
        double Time = 0.0;

        explicit Fixture(bool bFootFirst)
        {
            Require(Registry.Initialize(), "資源管理の初期化");
            Skeleton = Registry.CreateTransient<SkeletonResource>("ModifierOrderSkeleton");
            Mesh = Registry.CreateTransient<SkinnedMeshResource>("ModifierOrderMesh");
            Clip = Registry.CreateTransient<AnimationClipResource>("ModifierOrderClip");
            Asset = Registry.CreateTransient<SkeletalAssetResource>("ModifierOrderAsset");
            Require(Skeleton && Mesh && Clip && Asset, "試験用資源の作成");

            Container::VariableArray<Skeletal::SkeletalJoint> joints(JointCount);
            Container::VariableArray<Skeletal::SkeletalRestTransform> rest(JointCount);
            Math::Matrix4x4 models[JointCount];
            for (int i = 0; i < JointCount; ++i)
            {
                Animation::JointTransform model;
                model.Translation = BindPosition(i);
                models[i] = Animation::ToRowMatrix(model);
                const auto inverse = Math::MatrixUtils::Inverse(models[i]);
                joints[i].Name = JointNames[i];
                joints[i].ParentIndex = Parents[i];
                for (size_t k = 0; k < 16; ++k)
                {
                    joints[i].InverseBindMatrix[k] = inverse.values[k];
                }
                const auto local = Animation::FromRowMatrix(Parents[i] < 0 ? models[i]
                    : models[i] * Math::MatrixUtils::Inverse(models[Parents[i]]));
                rest[i].Translation = {local.Translation.x, local.Translation.y, local.Translation.z};
                rest[i].Rotation = {local.Rotation.x, local.Rotation.y, local.Rotation.z, local.Rotation.w};
                rest[i].Scale = {local.Scale.x, local.Scale.y, local.Scale.z};
            }
            Skeleton->SetJoints(std::move(joints));
            Require(Skeleton->SetAuthorRestPose(rest) && Skeleton->Load(), "四足骨格の読み込み");

            Container::VariableArray<Skeletal::SkeletalVertex> vertices(1);
            vertices[0].Normal = {0, 1, 0};
            vertices[0].JointIndices[0] = 0;
            vertices[0].JointWeights[0] = 1;
            Mesh->SetVertices(std::move(vertices));
            Mesh->SetIndices(Container::VariableArray<uint32_t>{0, 0, 0});
            Require(Mesh->Load(), "試験用メッシュの読み込み");

            Skeletal::SkeletalAnimationClip clip;
            clip.Name = "walk";
            clip.DurationSeconds = 1.0f;
            for (int leg = 0; leg < LegCount; ++leg)
            {
                Skeletal::SkeletalAnimationChannel channels[3];
                for (int part = 0; part < 3; ++part)
                {
                    channels[part].JointIndex = LegRoots[leg] + part;
                    channels[part].Path = Skeletal::SkeletalAnimationPath::Rotation;
                }
                const auto upper = BindPosition(LegRoots[leg] + 1) - BindPosition(LegRoots[leg]);
                const auto lower = BindPosition(LegRoots[leg] + 2) - BindPosition(LegRoots[leg] + 1);
                const double bindUpper = std::atan2(double(upper.y), double(upper.z));
                const double bindLower = std::atan2(double(lower.y), double(lower.z));
                for (int sample = 0; sample <= 120; ++sample)
                {
                    const double time = double(sample) / 120.0;
                    const bool bOpposite = leg == 1 || leg == 2;
                    const double phase = std::fmod(time + (bOpposite ? 0.5 : 0.0), 1.0);
                    const double swing = (phase - 0.5) * 2.0;
                    const double z = phase < 0.5 ? Stride * (0.5 - 2.0 * phase)
                        : Stride * (swing - 0.5);
                    const double y = phase < 0.5 ? 0.0 : 0.15 * (1.0 - std::abs(2.0 * swing - 1.0));
                    const double dy = y - HipHeight;
                    const double distance = std::hypot(z, dy);
                    const double height = std::sqrt(double(BoneLength) * BoneLength - distance * distance * 0.25);
                    const double kneeZ = z * 0.5 - dy / distance * height;
                    const double kneeY = HipHeight + dy * 0.5 + z / distance * height;
                    const double rootAngle = std::atan2(kneeY - HipHeight, kneeZ) - bindUpper;
                    const double midAngle = std::atan2(y - kneeY, z - kneeZ) - bindLower - rootAngle;
                    const double angles[] = {-rootAngle, -midAngle, rootAngle + midAngle};
                    for (int part = 0; part < 3; ++part)
                    {
                        const auto q = Math::QuaternionUtils::FromAxisAngle(Math::Vector3::UnitX, float(angles[part]));
                        channels[part].Samples.push_back({float(time), {q.x, q.y, q.z, q.w}});
                    }
                }
                for (auto& channel : channels)
                {
                    clip.Channels.push_back(std::move(channel));
                }
            }
            Clip->SetClip(std::move(clip));
            Require(Clip->Load(), "歩行クリップの読み込み");
            Animation::ClipMetadataReport clipReport;
            const char* metadata =
                R"({"events":[{"name":"Foot.RearLeft.Down","t":0,"end":0.5},{"name":"Foot.FrontRight.Down","t":0,"end":0.5},{"name":"Foot.RearRight.Down","t":0.5,"end":1},{"name":"Foot.FrontLeft.Down","t":0.5,"end":1}]})";
            Require(Clip->ApplyMetadataJson(Container::String(metadata), clipReport), "歩行の接地窓");
            Asset->SetClipResources(Mesh, Skeleton, {Clip});
            Require(Asset->Load(), "試験用アセットの読み込み");

            Graph = Registry.CreateTransient<AnimGraphResource>("ModifierOrderGraph");
            Animation::AnimGraphReport graphReport;
            const char* graph =
                R"({"version":1,"params":[{"name":"BankAngle","type":"float"},{"name":"PitchAngle","type":"float"}],"nodes":[{"id":"walk","type":"clip","clip":"walk","loop":true}],"root":"walk"})";
            Require(Graph && Graph->Compile(Container::String(graph), *Skeleton, *this, graphReport) && Graph->Load(),
                    "歩行グラフの読み込み");
            TestWorld.Initialize();
            Owner = TestWorld.SpawnEntity<Entity>();
            Require(Owner != nullptr, "Entity の作成");
            MeshComponent = TestWorld.CreateComponent<Component::SkinnedMeshComponent>(Owner);
            Animator = TestWorld.CreateComponent<Component::AnimatorComponent>(Owner);
            Require(MeshComponent && Animator, "コンポーネントの作成");
            MeshComponent->SetSkeletalAsset(Asset);
            Require(Animator->SetGraph(Graph), "歩行グラフの接続");
            Require(Animator->SetFloat(Animator->FindParam(Identity("BankAngle")), 0.261799388f) &&
                    Animator->SetFloat(Animator->FindParam(Identity("PitchAngle")), 0.02f), "旋回角度の設定");
            Animator->SetLookTarget(10, Component::AnimatorLookTarget::AtWorldPosition({1.0f, 1.2f, 4.0f}));

            const char* prefix = R"({"version":1,"modifiers":[
                {"type":"Lean","settings":{"chain":[{"joint":"Pelvis","weight":0.55},{"joint":"Spine0","weight":0.25},{"joint":"Chest","weight":0.2}]}},
                {"type":"Spine","settings":{"joints":[{"joint":"Spine0","amplitude":0.02},{"joint":"Spine1","amplitude":0.01,"phaseOffset":0.25}],"fallbackHz":1}},
                {"type":"Look","settings":{"chain":[{"joint":"Neck","weight":0.4},{"joint":"Head","weight":0.6}]}},
                {"type":"FootIK")";
            const char* suffix = R"(,"settings":{"legs":[
                    {"name":"RearLeft","chain":["RearLeftRoot","RearLeftMid","RearLeftFoot"],"sole":0},
                    {"name":"RearRight","chain":["RearRightRoot","RearRightMid","RearRightFoot"],"sole":0},
                    {"name":"FrontLeft","chain":["FrontLeftRoot","FrontLeftMid","FrontLeftFoot"],"sole":0},
                    {"name":"FrontRight","chain":["FrontRightRoot","FrontRightMid","FrontRightFoot"],"sole":0}],
                    "plant":{"releaseBlendSeconds":0},"land":{"blendSeconds":0,"snapDistance":0.5}}},
                {"type":"SpringBone","settings":{"chains":[{"name":"Tail","joints":["TailRoot","TailMid","TailTip"],"gravity":[0,-1,0]}]}},
                {"type":"FollowThrough","settings":{"joints":["Chest","Neck"],"head":"Head","strength":0.1}}
            ]})";
            Container::String config(prefix);
            if (bFootFirst)
            {
                config += Container::String(",\"order\":50");
            }
            config += Container::String(suffix);
            Container::VariableArray<Procedural::PoseModifierTypeEntry> entries;
            FootFactoryProbe::Factory() = nullptr;
            FootFactoryProbe::Last() = nullptr;
            for (auto entry : Procedural::GetBuiltinPoseModifierTypes().Entries)
            {
                if (std::strcmp(entry.Name, "FootIK") == 0)
                {
                    FootFactoryProbe::Factory() = entry.Create;
                    entry.Create = &FootFactoryProbe::Create;
                }
                entries.push_back(entry);
            }
            Require(FootFactoryProbe::Factory() != nullptr, "組み込み FootIK の登録");
            Procedural::PoseModifierConfigReport modifierReport;
            const bool bApplied = Animator->ApplyPoseModifiersJson(config, {entries}, &modifierReport);
            if (!bApplied)
            {
                std::fprintf(stderr, "ModifierOrderTest 設定エラー: 種別=%d 経路=", int(modifierReport.Error));
                for (const auto character : modifierReport.Path)
                {
                    std::fputc(static_cast<unsigned char>(character), stderr);
                }
                std::fputc('\n', stderr);
            }
            Require(bApplied, "姿勢後処理の設定");
            Foot = Animation::FindFootIKModifier(FootFactoryProbe::Last());
            FootFactoryProbe::Last() = nullptr;
            Require(Foot != nullptr, "FootIK の取得");
            Foot->SetGroundQuery(&Ground);
        }

        ~Fixture()
        {
            TestWorld.Finalize();
        }

        Container::TSharedPtr<AnimationClipResource> ResolveClip(Container::StringView name) const override
        {
            return name == Container::StringView("walk") ? Clip : Container::TSharedPtr<AnimationClipResource>{};
        }

        FrameSample Tick(float dt)
        {
            Time += double(dt);
            Owner->SetPosition(0.0f, 0.0f, float(double(Stride) * Time));
            const uint64_t previousSerial = MeshComponent->GetPoseSerial();
            TestWorld.Tick(dt);
            Require(MeshComponent->IsPoseCurrent() && MeshComponent->GetPoseSerial() > previousSerial,
                    "最終姿勢の更新");
            FrameSample sample;
            sample.Hash = UINT64_C(14695981039346656037);
            const auto& palette = MeshComponent->GetPoseSnapshot().BonePalette;
            Require(palette.size() == JointCount, "骨パレットの取得");
            for (const auto& matrix : palette)
            {
                const auto* bytes = reinterpret_cast<const unsigned char*>(matrix.values);
                for (size_t i = 0; i < sizeof(matrix.values); ++i)
                {
                    sample.Hash = (sample.Hash ^ bytes[i]) * UINT64_C(1099511628211);
                }
            }
            for (int leg = 0; leg < LegCount; ++leg)
            {
                Math::Matrix4x4 matrix;
                Require(MeshComponent->TryGetJointWorldMatrix(uint32_t(LegRoots[leg] + 2), matrix),
                        "足先のワールド行列の取得");
                const auto position = matrix.GetTranslationRow();
                sample.GroundError[leg] = std::abs(double(position.y) - Ground.Height(position.x)) * std::cos(Slope);
                sample.bPlanted[leg] = Foot->GetLegPlantPhase(size_t(leg)) == Animation::EFootPlantPhase::Planted;
                const bool bOpposite = leg == 1 || leg == 2;
                const double phase = std::fmod(Time + (bOpposite ? 0.5 : 0.0), 1.0);
                sample.bExpectedContact[leg] = phase >= 0.10 && phase <= 0.40;
            }
            return sample;
        }
    };

    Container::VariableArray<FrameSample> Trace(bool bFootFirst)
    {
        Fixture fixture(bFootFirst);
        Container::VariableArray<FrameSample> samples;
        for (int frame = 0; frame < Frames; ++frame)
        {
            const float dt = frame % 3 == 0 ? 1.0f / 30.0f : 1.0f / 60.0f;
            samples.push_back(fixture.Tick(dt));
        }
        return samples;
    }
} // namespace

void TestModifierOrder()
{
    const auto baseline = Trace(false);
    const auto repeat = Trace(false);
    const auto footFirst = Trace(true);
    int mismatchCount = 0;
    int differentCount = 0;
    int contactCount = 0;
    int legCounts[LegCount] = {};
    double baselineError = 0.0;
    double footFirstError = 0.0;
    for (int frame = 0; frame < Frames; ++frame)
    {
        mismatchCount += baseline[frame].Hash != repeat[frame].Hash ? 1 : 0;
        differentCount += baseline[frame].Hash != footFirst[frame].Hash ? 1 : 0;
        for (int leg = 0; leg < LegCount; ++leg)
        {
            if (baseline[frame].bExpectedContact[leg] && baseline[frame].bPlanted[leg] &&
                footFirst[frame].bPlanted[leg])
            {
                baselineError += baseline[frame].GroundError[leg];
                footFirstError += footFirst[frame].GroundError[leg];
                ++contactCount;
                ++legCounts[leg];
            }
        }
    }
    Require(contactCount > 0, "両方の順序に共通する接地標本");
    baselineError /= contactCount;
    footFirstError /= contactCount;
    std::printf("ModifierOrderTest: ハッシュ不一致=%d/%d 順序差=%d/%d 平均接地誤差[m] 既定=%.9g FootIK先行=%.9g 接地標本=%d 脚別=%d,%d,%d,%d\n",
                mismatchCount, Frames, differentCount, Frames, baselineError, footFirstError, contactCount,
                legCounts[0], legCounts[1], legCounts[2], legCounts[3]);
    std::fflush(stdout);
    if (mismatchCount != 0)
    {
        std::fprintf(stderr, "ModifierOrderTest 検査失敗: 同じ設定と時間刻みの骨パレットが一致しない\n");
        std::abort();
    }
    if (differentCount == 0 || !std::isfinite(baselineError) || !std::isfinite(footFirstError) ||
        footFirstError <= baselineError)
    {
        std::fprintf(stderr, "ModifierOrderTest 検査失敗: FootIK の先行による姿勢差と接地誤差の増加\n");
        std::abort();
    }
}
