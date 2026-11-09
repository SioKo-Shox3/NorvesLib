// 合成クリップの削減誤差、定数の扱い、再適用と実行時スキニングの契約を検査する。
#include "ClipBankV1Fixture.h"
#include "Animation/ClipOptimizer.h"
#include "Tools/AssetCook/MeshCooker.h"
#include "Tools/AssetCook/RigSplitCook.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/ClipChannelSampler.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletonResource.h"
#include "Resource/SkinnedMeshResource.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            std::fprintf(stderr, "ClipOptimizerTest 検査失敗 行 %d: %s\n", __LINE__, #value);                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    namespace A = NorvesLib::Core::Animation;
    namespace S = NorvesLib::Core::Skeletal;
    namespace C = NorvesLib::Core::Container;
    namespace Core = NorvesLib::Core;
    using Clip = S::SkeletalAnimationClip;
    using Channel = S::SkeletalAnimationChannel;
    using Value = S::SkeletalValue;
    using Path = S::SkeletalAnimationPath;
    constexpr double Pi = 3.14159265358979323846;
    constexpr size_t KeyCount = 61;

    bool SameBits(float a, float b)
    {
        return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
    }

    bool SameValue(const Value& a, const Value& b)
    {
        return SameBits(a.X, b.X) && SameBits(a.Y, b.Y) && SameBits(a.Z, b.Z) && SameBits(a.W, b.W);
    }

    void CheckSameClip(const Clip& a, const Clip& b)
    {
        CHECK(a.Name == b.Name && SameBits(a.DurationSeconds, b.DurationSeconds));
        CHECK(a.Channels.size() == b.Channels.size());
        for (size_t i = 0; i < a.Channels.size(); ++i)
        {
            const auto& left = a.Channels[i];
            const auto& right = b.Channels[i];
            CHECK(left.JointIndex == right.JointIndex && left.Path == right.Path &&
                  left.Interpolation == right.Interpolation && left.Samples.size() == right.Samples.size());
            for (size_t key = 0; key < left.Samples.size(); ++key)
            {
                CHECK(SameBits(left.Samples[key].TimeSeconds, right.Samples[key].TimeSeconds));
                CHECK(SameValue(left.Samples[key].Value, right.Samples[key].Value));
            }
        }
    }

    struct Fixture
    {
        C::VariableArray<S::SkeletalJoint> Joints{3};
        C::VariableArray<S::SkeletalRestTransform> Rest{3};
        C::VariableArray<S::SkeletalVertex> Vertices;

        Fixture()
        {
            const char* names[] = {"Root", "Child", "Grandchild"};
            for (size_t i = 0; i < 3; ++i)
            {
                Joints[i].Name = C::String(names[i]);
                Joints[i].ParentIndex = static_cast<int32_t>(i) - 1;
                // glTFの列規約で、各関節の大域restの逆行列を指定する。
                Joints[i].InverseBindMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                              0, -0.3f * static_cast<float>(i), 0, 1};
                Rest[i].Translation.Y = i == 0 ? 0.0f : 0.3f;
            }
            for (size_t i = 0; i < 12; ++i)
            {
                S::SkeletalVertex vertex;
                vertex.Position = {i % 2 == 0 ? -0.08f : 0.08f, 0.06f * static_cast<float>(i),
                                   i % 3 == 0 ? 0.04f : -0.04f};
                vertex.Normal = {0, 0, 1};
                vertex.JointIndices[0] = static_cast<uint32_t>(i / 4);
                vertex.JointWeights[0] = i % 2 == 0 ? 1.0f : 0.65f;
                if (i % 2 != 0)
                {
                    vertex.JointIndices[1] = static_cast<uint32_t>((i / 4 + 1) % 3);
                    vertex.JointWeights[1] = 0.35f;
                }
                Vertices.push_back(vertex);
            }
        }

        A::ClipOptimizeRig Rig() const
        {
            return {{Joints.data(), Joints.size()}, {Rest.data(), Rest.size()}, {Vertices.data(), Vertices.size()}};
        }
    };

    Channel MakeChannel(uint32_t joint, Path path)
    {
        Channel channel;
        channel.JointIndex = joint;
        channel.Path = path;
        channel.Samples.resize(KeyCount);
        for (size_t key = 0; key < KeyCount; ++key)
        {
            channel.Samples[key].TimeSeconds = static_cast<float>(key) / 30.0f;
        }
        return channel;
    }

    Value Rotation(double angle, bool bAroundX = false)
    {
        const float sine = static_cast<float>(std::sin(angle / 2));
        return {bAroundX ? sine : 0, 0, bAroundX ? 0 : sine, static_cast<float>(std::cos(angle / 2))};
    }

    Clip MakeChannelClip()
    {
        Clip clip;
        clip.Name = C::String("ChannelContracts");
        clip.DurationSeconds = 2;
        // 動くチャンネルが終端を担い、定数チャンネルの終端保護と混同しない。
        clip.Channels = {MakeChannel(0, Path::Translation), MakeChannel(1, Path::Translation),
                         MakeChannel(2, Path::Scale), MakeChannel(0, Path::Rotation),
                         MakeChannel(1, Path::Rotation), MakeChannel(2, Path::Translation)};
        clip.Channels[5].Interpolation = S::SkeletalAnimationInterpolation::Step;
        for (size_t key = 0; key < KeyCount; ++key)
        {
            const double time = clip.Channels[0].Samples[key].TimeSeconds;
            clip.Channels[0].Samples[key].Value = {static_cast<float>(0.08 * std::sin(Pi * time)), 0, 0, 0};
            clip.Channels[1].Samples[key].Value = {0, 0.3f, 0, 0};
            clip.Channels[2].Samples[key].Value = {1.2f, 1.2f, 1.2f, 0};
            clip.Channels[3].Samples[key].Value = {0, 0, 0, 1};
            clip.Channels[4].Samples[key].Value = Rotation(0.24 * std::sin(Pi * time));
            constexpr float jumps[] = {0, 0.04f, -0.03f, 0.02f};
            clip.Channels[5].Samples[key].Value = {jumps[std::min(key / 15, size_t{3})], 0.3f, 0, 0};
        }
        return clip;
    }

    Clip MakeSkinClip()
    {
        Clip clip;
        clip.Name = C::String("SkinContracts");
        clip.DurationSeconds = 2;
        clip.Channels = {MakeChannel(1, Path::Rotation), MakeChannel(2, Path::Rotation)};
        for (size_t key = 0; key < KeyCount; ++key)
        {
            const double time = clip.Channels[0].Samples[key].TimeSeconds;
            clip.Channels[0].Samples[key].Value = Rotation(0.6 * std::sin(Pi * time));
            clip.Channels[1].Samples[key].Value = Rotation(0.45 * std::sin(2 * Pi * time), true);
        }
        return clip;
    }

    const Channel* FindChannel(const Clip& clip, uint32_t joint, Path path)
    {
        for (const auto& channel : clip.Channels)
        {
            if (channel.JointIndex == joint && channel.Path == path)
            {
                return &channel;
            }
        }
        return nullptr;
    }

    // パディングを比較せず、チャンネルとキーの各floatのビットを照合する。
    Clip OptimizeAndCheckStability(const A::ClipOptimizeRig& rig, const Clip& input,
                                   const A::ClipOptimizeSettings& settings)
    {
        C::VariableArray<Clip> output, again, repeated;
        A::ClipCompressionReport report, againReport, repeatedReport;
        CHECK(A::OptimizeSkeletalClips(rig, {&input, 1}, output, settings, report));
        CHECK(report.bSucceeded && output.size() == 1);
        CHECK(A::OptimizeSkeletalClips(rig, {output.data(), output.size()}, again, settings, againReport));
        CHECK(again.size() == output.size());
        CheckSameClip(output[0], again[0]);
        CHECK(A::OptimizeSkeletalClips(rig, {&input, 1}, repeated, settings, repeatedReport));
        CHECK(repeated.size() == output.size());
        CheckSameClip(output[0], repeated[0]);
        C::String json, repeatedJson;
        CHECK(A::WriteClipCompressionReportJson(report, json));
        CHECK(A::WriteClipCompressionReportJson(repeatedReport, repeatedJson));
        CHECK(!json.empty() && json == repeatedJson);
        return output[0];
    }

    template <typename Callback>
    void AtKeysAndMidpoints(const Channel& channel, Callback&& callback)
    {
        for (size_t key = 0; key < channel.Samples.size(); ++key)
        {
            callback(channel.Samples[key].TimeSeconds);
            if (key + 1 < channel.Samples.size())
            {
                callback(static_cast<float>((double(channel.Samples[key].TimeSeconds) +
                                             channel.Samples[key + 1].TimeSeconds) / 2));
            }
        }
    }

    double Distance(double x, double y, double z)
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    double RotationErrorDegrees(const Value& a, const Value& b)
    {
        const double dot = double(a.X) * b.X + double(a.Y) * b.Y + double(a.Z) * b.Z + double(a.W) * b.W;
        const double normA = std::sqrt(double(a.X) * a.X + double(a.Y) * a.Y + double(a.Z) * a.Z + double(a.W) * a.W);
        const double normB = std::sqrt(double(b.X) * b.X + double(b.Y) * b.Y + double(b.Z) * b.Z + double(b.W) * b.W);
        CHECK(normA > 0 && normB > 0);
        // 単位四元数の内積に戻し、acosの定義域だけを丸めから保護する。
        return 2 * std::acos(std::clamp(std::abs(dot / (normA * normB)), 0.0, 1.0)) * 180 / Pi;
    }

    void CheckChannels(const Clip& input, const Clip& output, const A::ClipOptimizeSettings& settings)
    {
        CHECK(output.Channels.size() == input.Channels.size() - 1);
        CHECK(FindChannel(output, 1, Path::Translation) == nullptr);
        const auto* scale = FindChannel(output, 2, Path::Scale);
        const auto* root = FindChannel(output, 0, Path::Rotation);
        CHECK(scale && scale->Samples.size() == 1 && SameValue(scale->Samples[0].Value, input.Channels[2].Samples[0].Value));
        CHECK(root && root->Samples.size() == 1 && SameValue(root->Samples[0].Value, input.Channels[3].Samples[0].Value));
        for (size_t index : {size_t{0}, size_t{4}, size_t{5}})
        {
            const auto& before = input.Channels[index];
            const auto* after = FindChannel(output, before.JointIndex, before.Path);
            CHECK(after && after->Interpolation == before.Interpolation);
            CHECK(after->Samples.size() < before.Samples.size());
            AtKeysAndMidpoints(before, [&](float time)
            {
                const auto a = A::ClipChannelSampler::Sample(before, time);
                const auto b = A::ClipChannelSampler::Sample(*after, time);
                if (before.Path == Path::Rotation)
                {
                    CHECK(RotationErrorDegrees(a, b) <= settings.MaxRotationErrorDegrees);
                }
                else
                {
                    CHECK(Distance(double(a.X) - b.X, double(a.Y) - b.Y, double(a.Z) - b.Z) <= settings.MaxJointErrorMeters);
                }
            });
        }
        const auto* step = FindChannel(output, 2, Path::Translation);
        CHECK(step);
        for (size_t jump : {size_t{15}, size_t{30}, size_t{45}})
        {
            const auto& expected = input.Channels[5].Samples[jump];
            bool bFound = false;
            for (const auto& sample : step->Samples)
            {
                if (SameBits(sample.TimeSeconds, expected.TimeSeconds))
                {
                    CHECK(SameValue(sample.Value, expected.Value));
                    bFound = true;
                }
            }
            CHECK(bFound);
            CHECK(SameValue(A::ClipChannelSampler::Sample(*step, expected.TimeSeconds), expected.Value));
        }
    }

    void CheckRuntimeSkin(const Fixture& fixture, const Clip& input, const Clip& output,
                          const A::ClipOptimizeSettings& settings)
    {
        Core::SkeletonResource skeleton;
        Core::AnimationClipResource original, optimized;
        Core::SkinnedMeshResource mesh;
        skeleton.Initialize();
        original.Initialize();
        optimized.Initialize();
        mesh.Initialize();
        auto joints = fixture.Joints;
        auto vertices = fixture.Vertices;
        auto before = input;
        auto after = output;
        skeleton.SetJoints(std::move(joints));
        original.SetClip(std::move(before));
        optimized.SetClip(std::move(after));
        mesh.SetVertices(std::move(vertices));
        CHECK(skeleton.Load() && original.Load() && optimized.Load());
        // 作者restを設定せず、inverse bindから既定ローカルを復元する旧経路を通す。
        AtKeysAndMidpoints(input.Channels[0], [&](float time)
        {
            A::SkeletalPoseSnapshot a, b;
            CHECK(A::SkeletalAnimationSampler::Sample(skeleton, original, mesh, time, NorvesLib::Math::Matrix4x4::Identity, a));
            CHECK(A::SkeletalAnimationSampler::Sample(skeleton, optimized, mesh, time, NorvesLib::Math::Matrix4x4::Identity, b));
            for (const auto& vertex : fixture.Vertices)
            {
                const auto left = A::SkeletalAnimationSampler::SkinVertex(vertex, a.BonePalette).Position;
                const auto right = A::SkeletalAnimationSampler::SkinVertex(vertex, b.BonePalette).Position;
                CHECK(Distance(double(left.x) - right.x, double(left.y) - right.y, double(left.z) - right.z) <= settings.MaxVertexErrorMeters);
            }
        });
        mesh.Finalize();
        original.Finalize();
        optimized.Finalize();
        skeleton.Finalize();
    }
    void CheckDisabledCookBytes()
    {
        namespace F = NorvesLib::Tests::RigV1Fixture;
        namespace Cook = NorvesLib::Tools::AssetCook;
        F::Fixture fixture;
        const auto source = F::View(fixture.Json);
        const std::filesystem::path sourcePath = "RigV1Fixture/rig.gltf";
        Core::AssetImport::ImportSettingsFileOptions importOptions;
        importOptions.bDisabled = true;
        Cook::ClipOptimizeCookOptions disabled;
        disabled.bEnabled = false;
        disabled.Settings.MaxVertexErrorMeters = 0.02;
        disabled.Settings.MaxJointErrorMeters = 0.01;
        disabled.Settings.MaxRotationErrorDegrees = 1.0;
        C::AnsiString error;
        Cook::SkeletalCookResult legacyBase, legacyDisabled;
        constexpr C::AnsiStringView format = "nvskel.v0.skinned.pnujiw.u32";
        const bool baseOk = Cook::CookGltfToNvskelNativePath(source.data(), source.size(), format, sourcePath,
                                                             legacyBase, error, &importOptions, nullptr, nullptr, nullptr);
        if (!baseOk) std::fprintf(stderr, "旧形式の基準 cook に失敗: %s\n", error.c_str());
        CHECK(baseOk);
        const bool disabledOk = Cook::CookGltfToNvskelNativePath(source.data(), source.size(), format, sourcePath,
                                                                 legacyDisabled, error, &importOptions, nullptr, nullptr,
                                                                 &disabled);
        if (!disabledOk) std::fprintf(stderr, "旧形式の無効化 cook に失敗: %s\n", error.c_str());
        CHECK(disabledOk);
        CHECK(legacyBase.ClipCount > 0 && !legacyBase.NvskelBytes.empty());
        CHECK(legacyBase.NvskelBytes == legacyDisabled.NvskelBytes && legacyBase.SourceHash == legacyDisabled.SourceHash);

        Cook::RigSplitCookRequest request;
        request.SourcePath = sourcePath;
        request.SkeletonPath = "Models/Rig.nvskel";
        request.MeshPath = "Models/Mesh.nvskel";
        request.BankPath = "Animations/Wave.nvclip";
        request.ImportOptions = importOptions;
        Cook::RigSplitCookResult splitBase, splitDisabled, splitEnabled;
        S::RigV1Report report;
        const bool splitBaseOk = Cook::CookRigSplitV1NativePath(source, request, splitBase, report, error);
        if (!splitBaseOk) std::fprintf(stderr, "v1 の基準 cook に失敗: %s\n", error.c_str());
        CHECK(splitBaseOk);
        request.ClipOptimize = disabled;
        const bool splitDisabledOk = Cook::CookRigSplitV1NativePath(source, request, splitDisabled, report, error);
        if (!splitDisabledOk) std::fprintf(stderr, "v1 の無効化 cook に失敗: %s\n", error.c_str());
        CHECK(splitDisabledOk);
        CHECK(splitBase.SourceHash == splitDisabled.SourceHash);
        const Cook::RigSplitCookEntry* baseline[] = {&splitBase.Skeleton, &splitBase.Mesh, &splitBase.Bank};
        const Cook::RigSplitCookEntry* unchanged[] = {&splitDisabled.Skeleton, &splitDisabled.Mesh, &splitDisabled.Bank};
        for (size_t i = 0; i < 3; ++i)
        {
            CHECK(!baseline[i]->Payload.empty() && !baseline[i]->Package.empty());
            CHECK(baseline[i]->Payload == unchanged[i]->Payload && baseline[i]->Package == unchanged[i]->Package);
        }
        request.ClipOptimize = {};
        request.ClipOptimize.bEnabled = true;
        const bool splitEnabledOk = Cook::CookRigSplitV1NativePath(source, request, splitEnabled, report, error);
        if (!splitEnabledOk) std::fprintf(stderr, "v1 の有効化 cook に失敗: %s\n", error.c_str());
        CHECK(splitEnabledOk);
        CHECK(splitEnabled.Bank.Payload != splitBase.Bank.Payload || splitEnabled.SourceHash != splitBase.SourceHash);
    }
} // namespace

int main()
{
    Fixture fixture;
    const A::ClipOptimizeSettings settings;
    const auto channels = MakeChannelClip();
    const auto optimized = OptimizeAndCheckStability(fixture.Rig(), channels, settings);
    CheckChannels(channels, optimized, settings);

    C::VariableArray<S::SkeletalRestTransform> legacyRest;
    CHECK(A::BuildLegacyClipOptimizeDefaultLocal({fixture.Joints.data(), fixture.Joints.size()}, legacyRest));
    CHECK(legacyRest.size() == 3);
    for (size_t joint = 0; joint < 3; ++joint)
    {
        const auto& rest = legacyRest[joint];
        CHECK(rest.Translation.X == 0 && rest.Translation.Z == 0);
        CHECK(std::abs(double(rest.Translation.Y) - fixture.Rest[joint].Translation.Y) <= 1e-7);
        CHECK(rest.Rotation.X == 0 && rest.Rotation.Y == 0 && rest.Rotation.Z == 0 && rest.Rotation.W == 1);
        CHECK(rest.Scale.X == 1 && rest.Scale.Y == 1 && rest.Scale.Z == 1);
    }
    auto legacyRig = fixture.Rig();
    legacyRig.DefaultLocal = {legacyRest.data(), legacyRest.size()};
    const auto skin = MakeSkinClip();
    const auto optimizedSkin = OptimizeAndCheckStability(legacyRig, skin, settings);
    A::ClipDeviation deviation;
    CHECK(A::MeasureClipDeviation(legacyRig, skin, optimizedSkin, deviation));
    CHECK(deviation.MaxVertexErrorMeters <= settings.MaxVertexErrorMeters);
    CheckRuntimeSkin(fixture, skin, optimizedSkin, settings);

    auto invalidRig = fixture.Rig();
    invalidRig.DefaultLocal = {fixture.Rest.data(), fixture.Rest.size() - 1};
    C::VariableArray<Clip> rejected;
    A::ClipCompressionReport rejectedReport;
    CHECK(!A::OptimizeSkeletalClips(invalidRig, {&channels, 1}, rejected, settings, rejectedReport));
    CheckDisabledCookBytes();
    std::puts("ClipOptimizerTest: 定数・削減誤差・冪等性・決定性・実行時スキニング・入力検査に合格");
    return 0;
}
