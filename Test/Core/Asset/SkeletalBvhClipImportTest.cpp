// 複数frameの所有clip/reportと、最終keyの実Sampler評価を検査する。
#include "Animation/SkeletalBvhClipImport.h"
#include "Animation/SkeletalClipSampling.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Resource/SkinnedMeshResource.h"
#include "Math/MatrixUtils.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "BVH clip check failed %s:%d %s\n", __FILE__, __LINE__, #x);                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
#include "SkeletalClipProcessingCases.h"
namespace Core = NorvesLib::Core;
namespace A = Core::Animation;
namespace B = Core::Bvh;
namespace C = Core::Container;
namespace S = Core::Skeletal;
namespace M = NorvesLib::Math;
using Status = A::SkeletalBvhClipStatus;
namespace
{
    void Near(double actual, double expected, double epsilon = 1e-4)
    {
        CHECK(std::isfinite(actual));
        CHECK(std::abs(actual - expected) <= epsilon);
    }
    void Store(S::SkeletalJoint& joint, const M::Matrix4x4& matrix)
    {
        for (size_t i = 0; i < 16; ++i)
        {
            joint.InverseBindMatrix[i] = matrix.values[i];
        }
    }
    void Name(B::Joint& joint, const char* text)
    {
        const auto* begin = reinterpret_cast<const uint8_t*>(text);
        joint.Name.assign(begin, begin + std::strlen(text));
    }
    B::BvhDocument Source(uint32_t frames = 3, uint32_t count = 2, bool rootPositions = false,
                          bool childPositions = false)
    {
        B::BvhDocument d;
        d.FrameCount = frames;
        d.FrameTimeSeconds = .25;
        d.Joints.resize(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& joint = d.Joints[i];
            char name[32];
            std::snprintf(name, sizeof(name), "Source%u", i);
            Name(joint, name);
            joint.Parent = i == 0 ? UINT32_MAX : 0;
            joint.Offset = i == 0 ? B::Vector3d{} : B::Vector3d{0, 1, 0};
            joint.ChannelOffset = static_cast<uint32_t>(d.Channels.size());
            if ((i == 0 && rootPositions) || (i != 0 && childPositions))
            {
                d.Channels.push_back(B::Channel::Xposition);
                d.Channels.push_back(B::Channel::Yposition);
                d.Channels.push_back(B::Channel::Zposition);
            }
            d.Channels.push_back(B::Channel::Xrotation);
            d.Channels.push_back(B::Channel::Yrotation);
            d.Channels.push_back(B::Channel::Zrotation);
            joint.ChannelCount = static_cast<uint8_t>(d.Channels.size() - joint.ChannelOffset);
            if (i != 0)
            {
                joint.bHasEndSite = true;
                joint.EndSiteOffset = {0, 1, 0};
            }
        }
        d.Values.assign(static_cast<size_t>(frames) * d.Channels.size(), 0);
        return d;
    }
    C::VariableArray<S::SkeletalJoint> Target(uint32_t count = 2)
    {
        C::VariableArray<S::SkeletalJoint> joints(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "Target%u", i);
            joints[i].Name = name;
            joints[i].ParentIndex = i == 0 ? -1 : 0;
            auto matrix = M::Matrix4x4::Identity;
            if (i != 0)
            {
                matrix.m31 = -1;
            }
            Store(joints[i], matrix);
        }
        return joints;
    }
    A::SkeletalJointMappingSet Mapping(uint32_t sources, uint32_t targets, uint32_t count)
    {
        A::SkeletalJointMappingSet mapping;
        mapping.SourceJointCount = sources;
        mapping.TargetJointCount = targets;
        mapping.RootMappingIndex = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            mapping.Pairs.push_back({i % sources, i, i});
        }
        return mapping;
    }
    A::SkeletalBvhClipSettings Settings()
    {
        A::SkeletalBvhClipSettings s;
        s.Up = Core::AssetImport::SignedAxis::PositiveY;
        s.Forward = Core::AssetImport::SignedAxis::PositiveZ;
        s.Handedness = A::SkeletalSourceHandedness::Right;
        s.PositionScale = 1;
        s.Translation = B::TranslationConvention::OffsetPlusChannels;
        s.TimeMode = A::SkeletalBvhClipTimeMode::HeaderFrameTime;
        s.SourceReuse = A::SkeletalSourceReusePolicy::Reject;
        s.Rotation = A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations;
        return s;
    }
    struct Fixture
    {
        B::BvhDocument Document = Source();
        C::VariableArray<S::SkeletalJoint> Joints = Target();
        A::SkeletalJointMappingSet Mappings = Mapping(2, 2, 2);
        C::VariableArray<B::Matrix3d> Corrections = C::VariableArray<B::Matrix3d>(2);
        M::Matrix4x4 Mesh = M::Matrix4x4::Identity;
        C::String ClipName = "Run";
        A::SkeletalBvhClipSettings Options = Settings();
        A::SkeletalBvhClipLimits Limits;
        A::SkeletalBvhClipResult Import(A::SkeletalBvhClipOutput& out) const
        {
            return A::ImportSkeletalBvhRotationClip(Document, C::Span<const S::SkeletalJoint>(Joints), Mesh, Mappings,
                                                    C::Span<const B::Matrix3d>(Corrections), ClipName, Options, Limits,
                                                    out);
        }
        void One(uint32_t frames = 3)
        {
            Document = Source(frames, 1);
            Joints = Target(1);
            Mappings = Mapping(1, 1, 1);
            Corrections.resize(1);
        }
    };
    A::SkeletalPoseSnapshot Sample(const S::SkeletalAnimationClip& data,
                                   const C::VariableArray<S::SkeletalJoint>& joints, const M::Matrix4x4& meshGlobal,
                                   float time)
    {
        Core::SkeletonResource skeleton;
        Core::AnimationClipResource clip;
        Core::SkinnedMeshResource mesh;
        skeleton.Initialize();
        clip.Initialize();
        mesh.Initialize();
        auto jointCopy = joints;
        auto clipCopy = data;
        skeleton.SetJoints(std::move(jointCopy));
        clip.SetClip(std::move(clipCopy));
        CHECK(skeleton.Load());
        CHECK(clip.Load());
        A::SkeletalPoseSnapshot pose;
        CHECK(A::SkeletalAnimationSampler::Sample(skeleton, clip, mesh, time, meshGlobal, pose));
        return pose;
    }
    C::VariableArray<uint64_t> Snapshot(const A::SkeletalBvhClipOutput& out)
    {
        C::VariableArray<uint64_t> words;
        const auto u = [&](uint64_t value) { words.push_back(value); };
        const auto f = [&](float value) { u(std::bit_cast<uint32_t>(value)); };
        const auto d = [&](double value) { u(std::bit_cast<uint64_t>(value)); };
        const auto bytes = [&](const void* data, size_t size)
        {
            u(size);
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                u(p[i]);
            }
        };
        bytes(out.Clip.Name.data(), out.Clip.Name.size() * sizeof(C::String::value_type));
        f(out.Clip.DurationSeconds);
        u(out.Clip.Channels.size());
        for (const auto& ch : out.Clip.Channels)
        {
            u(ch.JointIndex);
            u(static_cast<uint32_t>(ch.Path));
            u(static_cast<uint32_t>(ch.Interpolation));
            u(ch.Samples.size());
            for (const auto& key : ch.Samples)
            {
                f(key.TimeSeconds);
                f(key.Value.X);
                f(key.Value.Y);
                f(key.Value.Z);
                f(key.Value.W);
            }
        }
        const auto& r = out.Report;
        const auto& settings = r.Settings;
        u(static_cast<uint8_t>(settings.Up));
        u(static_cast<uint8_t>(settings.Forward));
        u(static_cast<uint8_t>(settings.Handedness));
        d(settings.PositionScale);
        u(static_cast<uint8_t>(settings.Translation));
        u(static_cast<uint8_t>(settings.TimeMode));
        d(settings.SourceFps);
        u(static_cast<uint8_t>(settings.SourceReuse));
        u(static_cast<uint8_t>(settings.Rotation));
        u(r.SourceFrames);
        u(r.SourceJoints);
        u(r.TargetJoints);
        u(r.OutputKeys);
        u(r.JointFrames);
        u(r.WorkUnits);
        u(r.PlannedOwnedBytes);
        u(r.HemisphereFlips);
        d(r.HeaderFrameTimeSeconds);
        d(r.SelectedIntervalSeconds);
        d(r.ExactDurationSeconds);
        f(r.StoredDurationSeconds);
        d(r.MaximumTimeRoundingErrorSeconds);
        d(r.MaximumKeyRotationErrorRadians);
        u(r.MaximumRotationErrorFrame);
        u(r.bStoredKeysValidated);
        u(r.bContinuousCurveValidated);
        u(r.SourceNames.size());
        for (const auto& name : r.SourceNames)
        {
            bytes(name.data(), name.size());
        }
        u(r.TargetNames.size());
        for (const auto& name : r.TargetNames)
        {
            bytes(name.data(), name.size());
        }
        u(r.Mappings.size());
        for (const auto& pair : r.Mappings)
        {
            u(pair.SourceIndex);
            u(pair.TargetIndex);
            u(pair.InputEntryIndex);
        }
        u(r.Corrections.size());
        for (const auto& c : r.Corrections)
        {
            for (double value : c.Values)
            {
                d(value);
            }
        }
        u(r.UnmappedSource.size());
        for (uint32_t i : r.UnmappedSource)
        {
            u(i);
        }
        u(r.UnmappedTarget.size());
        for (uint32_t i : r.UnmappedTarget)
        {
            u(i);
        }
        const auto range = [&](const A::SkeletalBvhClipRange& value)
        {
            u(value.bHasValue);
            d(value.Minimum);
            d(value.Maximum);
        };
        u(r.IgnoredNonRootPositions.size());
        for (const auto& p : r.IgnoredNonRootPositions)
        {
            u(p.JointIndex);
            u(p.OriginalChannelIndex);
            u(static_cast<uint8_t>(p.Channel));
            range(p.Raw);
        }
        const auto& root = r.RootPosition;
        d(root.OriginalOffset.X);
        d(root.OriginalOffset.Y);
        d(root.OriginalOffset.Z);
        u(root.PositionMask);
        u(root.bPositionBeforeRotations);
        for (size_t i = 0; i < 3; ++i)
        {
            range(root.Raw[i]);
            range(root.Delta[i]);
            range(root.CanonicalDelta[i]);
        }
        u(root.UnavailableDeltaFrames);
        u(root.UnavailableCanonicalFrames);
        u(root.FirstUnavailableDeltaFrame);
        u(root.FirstUnavailableCanonicalFrame);
        u(root.ConversionFailureFrames);
        u(root.FirstConversionFailureFrame);
        u(static_cast<uint8_t>(root.FirstDeltaIssue));
        u(static_cast<uint8_t>(root.FirstConversionIssue));
        return words;
    }
    void Same(const C::VariableArray<uint64_t>& a, const C::VariableArray<uint64_t>& b)
    {
        CHECK(a.size() == b.size());
        CHECK(std::equal(a.begin(), a.end(), b.begin()));
    }
    void SameClip(const S::SkeletalAnimationClip& a, const S::SkeletalAnimationClip& b)
    {
        CHECK(a.DurationSeconds == b.DurationSeconds && a.Channels.size() == b.Channels.size());
        for (size_t i = 0; i < a.Channels.size(); ++i)
        {
            const auto& x = a.Channels[i];
            const auto& y = b.Channels[i];
            CHECK(x.JointIndex == y.JointIndex && x.Path == y.Path && x.Interpolation == y.Interpolation &&
                  x.Samples.size() == y.Samples.size());
            for (size_t j = 0; j < x.Samples.size(); ++j)
            {
                CHECK(x.Samples[j].TimeSeconds == y.Samples[j].TimeSeconds);
                const auto& p = x.Samples[j].Value;
                const auto& q = y.Samples[j].Value;
                CHECK(p.X == q.X && p.Y == q.Y && p.Z == q.Z && p.W == q.W);
            }
        }
    }
    void WorldNear(const M::Matrix4x4& actual, const B::Matrix3d& expected, double scale = 1)
    {
        for (size_t i = 0; i < 3; ++i)
        {
            for (size_t j = 0; j < 3; ++j)
            {
                Near(actual.values[i * 4 + j], scale * expected.Values[j * 3 + i]);
            }
        }
    }
    void TestDecoderThroughActualSampler(bool scaled, bool reflected)
    {
        const char text[] =
            "HIERARCHY\nROOT Root { OFFSET 0 0 0 CHANNELS 3 Zrotation Yrotation Xrotation JOINT Child { OFFSET 0 1 0 CHANNELS 3 Xrotation Yrotation Zrotation End Site { OFFSET 0 1 0 } } }\nMOTION\nFrames: 3\nFrame Time: 0.25\n0 0 0 0 0 0\n90 0 0 90 0 0\n180 0 0 180 0 0\n";
        Fixture f;
        CHECK(B::DecodeBvh(C::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text), sizeof(text) - 1),
                           B::BvhDecodeLimits{}, f.Document)
                  .Succeeded());
        if (reflected)
        {
            f.Options.Handedness = A::SkeletalSourceHandedness::Left;
        }
        double scale = 1;
        if (scaled)
        {
            scale = 2;
            f.Mesh = M::Matrix4x4(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 3, 4, 5, 1);
            Store(f.Joints[0], M::Matrix4x4(0, .5f, 0, 0, -.5f, 0, 0, 0, 0, 0, .5f, 0, 1.5f, 2, 2.5f, 1));
            Store(f.Joints[1], M::Matrix4x4(0, .5f, 0, 0, -.5f, 0, 0, 0, 0, 0, .5f, 0, 1.5f, 1.5f, 2.5f, 1));
        }
        A::SkeletalBvhClipOutput out;
        CHECK(f.Import(out).Succeeded());
        CHECK(out.Report.bStoredKeysValidated && !out.Report.bContinuousCurveValidated);
        CHECK(out.Report.OutputKeys == 6 && out.Report.SourceFrames == 3 && out.Clip.DurationSeconds == .5f &&
              out.Report.StoredDurationSeconds == .5f);
        CHECK(out.Report.MaximumKeyRotationErrorRadians <= .05 * 3.14159265358979323846 / 180);
        const double endpoint[3][3] = {{0, 2, 0}, {-1, 0, 1}, {0, 0, 0}};
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            CHECK(out.Clip.Channels[0].Samples[frame].TimeSeconds == .25f * frame);
            const auto pose = Sample(out.Clip, f.Joints, f.Mesh, .25f * frame);
            B::BvhPose reference;
            CHECK(B::EvaluateBvhFrame(f.Document, frame, B::TranslationConvention::OffsetPlusChannels, reference)
                      .Succeeded());
            for (size_t joint = 0; joint < 2; ++joint)
            {
                auto expected = reference.Joints[joint].World.Rotation;
                if (reflected)
                {
                    for (size_t r = 0; r < 3; ++r)
                    {
                        for (size_t c = 0; c < 3; ++c)
                        {
                            if ((r == 0) != (c == 0))
                            {
                                expected.Values[r * 3 + c] = -expected.Values[r * 3 + c];
                            }
                        }
                    }
                }
                WorldNear(pose.JointModelMatrices[joint] * f.Mesh, expected, scale);
            }
            const auto tip = pose.JointModelMatrices[1] * f.Mesh;
            const auto point =
                M::MatrixUtils::TransformPointRowVector(tip, M::Vector3(0, static_cast<float>(1 / scale), 0));
            Near(point.x, (reflected ? -1 : 1) * endpoint[frame][0], .001);
            Near(point.y, endpoint[frame][1], .001);
            Near(point.z, endpoint[frame][2], .001);
        }
        CHECK(out.Report.RootPosition.PositionMask == 0 && out.Report.RootPosition.Delta[0].bHasValue);
        Near(out.Report.RootPosition.Delta[0].Minimum, 0);
        CHECK(out.Report.IgnoredNonRootPositions.empty());
    }
    void TestHemisphereTimeAndOwnership()
    {
        A::SkeletalBvhClipOutput out;
        auto stableTarget = Target(1);
        {
            Fixture f;
            f.One();
            f.Document.Values[0] = -80;
            f.Document.Values[3] = -100;
            f.Document.Values[6] = -120;
            f.Options.TimeMode = A::SkeletalBvhClipTimeMode::OverrideFps;
            f.Options.SourceFps = 4;
            CHECK(f.Import(out).Succeeded());
            CHECK(out.Report.HemisphereFlips == 2);
            const auto& keys = out.Clip.Channels[0].Samples;
            for (size_t i = 1; i < keys.size(); ++i)
            {
                const auto& a = keys[i - 1].Value;
                const auto& b = keys[i].Value;
                CHECK(double(a.X) * b.X + double(a.Y) * b.Y + double(a.Z) * b.Z + double(a.W) * b.W >= 0);
            }
            f.Document.Values.clear();
            f.Document.Joints.clear();
            f.Joints.clear();
            f.Mappings.Pairs.clear();
            f.Corrections.clear();
            f.ClipName.clear();
        }
        CHECK(out.Clip.Name.size() == 3 && out.Report.SourceNames[0].size() == 7 &&
              out.Report.TargetNames[0].size() == 7);
        const auto pose = Sample(out.Clip, stableTarget, M::Matrix4x4::Identity, .125f);
        Near(pose.JointModelMatrices[0].m11, 0);
        Near(pose.JointModelMatrices[0].m12, -1);
        Near(pose.JointModelMatrices[0].m21, 1);
        Fixture longClip;
        longClip.One(67);
        longClip.Document.FrameTimeSeconds = 1.0 / 30;
        longClip.Options.TimeMode = A::SkeletalBvhClipTimeMode::OverrideFps;
        longClip.Options.SourceFps = 16;
        CHECK(longClip.Import(out).Succeeded());
        CHECK(out.Clip.DurationSeconds == 4.125f &&
              out.Clip.Channels[0].Samples.back().TimeSeconds == out.Clip.DurationSeconds);
        CHECK(out.Report.ExactDurationSeconds == 4.125 && out.Report.SelectedIntervalSeconds == .0625);
        Fixture single;
        single.One(1);
        CHECK(single.Import(out).Succeeded());
        CHECK(out.Clip.DurationSeconds == 0 && out.Clip.Channels[0].Samples.size() == 1);
    }
    void TestInteriorKeyRealizationRefusal()
    {
        Fixture f;
        f.One();
        f.Document.Values[2] = 0;
        f.Document.Values[5] = 90;
        f.Document.Values[8] = 180;
        A::SkeletalBvhClipOutput out;
        CHECK(f.Import(out).Succeeded());
        const auto before = Snapshot(out);
        auto bad = out.Clip;
        bad.Channels[0].Samples[1].TimeSeconds = 1e-7f;
        bad.Channels[0].Samples[2].TimeSeconds = 2e-7f;
        bad.DurationSeconds = 2e-7f;
        const auto actual = Sample(bad, f.Joints, f.Mesh, 1e-7f);
        WorldNear(actual.JointModelMatrices[0], B::Matrix3d{});
        f.Document.FrameTimeSeconds = 1e-7;
        const auto refused = f.Import(out);
        CHECK(refused.Status == Status::RetargetRejected && refused.FrameIndex == 1);
        CHECK(refused.Retarget.Status == A::SkeletalRetargetStatus::FloatRealizationMismatch &&
              refused.Retarget.AngularErrorRadians > 1.5);
        Same(before, Snapshot(out));
    }
    void TestRootDiagnostics()
    {
        Fixture f;
        f.One();
        f.Document = Source(3, 1, true);
        f.Document.Joints[0].Offset = {1, 2, 3};
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 6] = 2 + i;
            f.Document.Values[i * 6 + 1] = 6;
        }
        auto ibm = M::Matrix4x4::Identity;
        ibm.m31 = -7;
        Store(f.Joints[0], ibm);
        f.Options.PositionScale = 2;
        A::SkeletalBvhClipOutput additive, absolute;
        CHECK(f.Import(additive).Succeeded());
        f.Options.Translation = B::TranslationConvention::AbsoluteLocalChannels;
        CHECK(f.Import(absolute).Succeeded());
        SameClip(additive.Clip, absolute.Clip);
        Near(additive.Report.RootPosition.Delta[0].Minimum, 2);
        Near(absolute.Report.RootPosition.Delta[0].Minimum, 1);
        Near(additive.Report.RootPosition.CanonicalDelta[1].Maximum, 12);
        Near(absolute.Report.RootPosition.CanonicalDelta[1].Maximum, 8);
        const auto pose = Sample(absolute.Clip, f.Joints, f.Mesh, .25f);
        Near(pose.JointModelMatrices[0].m31, 7);
        f.Options.PositionScale = std::numeric_limits<double>::max();
        CHECK(f.Import(absolute).Succeeded());
        CHECK(absolute.Report.RootPosition.UnavailableDeltaFrames == 0 &&
              absolute.Report.RootPosition.ConversionFailureFrames == 3);
        CHECK(absolute.Report.RootPosition.FirstConversionIssue == A::SkeletalCoordinateStatus::UnrepresentableOutput);
        f.Options.PositionScale = 1;
        f.Document.Joints[0].Offset.X = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 6] = std::numeric_limits<double>::max();
        }
        CHECK(f.Import(absolute).Succeeded());
        CHECK(absolute.Report.RootPosition.UnavailableDeltaFrames == 3 &&
              absolute.Report.RootPosition.ConversionFailureFrames == 0);
        CHECK(absolute.Report.RootPosition.FirstDeltaIssue == A::SkeletalBvhRootDeltaIssue::UnrepresentableDelta);
        f.Options.Translation = B::TranslationConvention::OffsetPlusChannels;
        f.Document.Joints[0].Offset.X = std::numeric_limits<double>::max();
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 6] = 1;
        }
        CHECK(f.Import(additive).Succeeded());
        Near(additive.Report.RootPosition.Delta[0].Minimum, 1);
        // 元の宣言順がposition前置でない場合、回転は作るが位置の意味を推測しない。
        f.Document = Source(3, 1, true);
        f.Document.Channels = {B::Channel::Xrotation, B::Channel::Xposition, B::Channel::Yposition,
                               B::Channel::Zposition, B::Channel::Yrotation, B::Channel::Zrotation};
        CHECK(f.Import(additive).Succeeded());
        CHECK(!additive.Report.RootPosition.bPositionBeforeRotations);
        CHECK(additive.Report.RootPosition.FirstDeltaIssue == A::SkeletalBvhRootDeltaIssue::UnsupportedLayout);
    }
    void TestIgnoredPositionsAndNoRotationSource()
    {
        Fixture f;
        f.Document = Source(3, 2, false, true);
        f.Document.Channels[3] = B::Channel::Xrotation;
        f.Document.Channels[4] = B::Channel::Xposition;
        f.Document.Channels[5] = B::Channel::Yrotation;
        f.Document.Channels[6] = B::Channel::Yposition;
        f.Document.Channels[7] = B::Channel::Zrotation;
        f.Document.Channels[8] = B::Channel::Zposition;
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 9 + 2] = 90 * i;
            f.Document.Values[i * 9 + 3] = 90 * i;
        }
        A::SkeletalBvhClipOutput a, b;
        CHECK(f.Import(a).Succeeded());
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 9 + 4] =
                i == 1 ? -std::numeric_limits<double>::max() : std::numeric_limits<double>::max();
            f.Document.Values[i * 9 + 6] = std::numeric_limits<double>::max();
            f.Document.Values[i * 9 + 8] = -std::numeric_limits<double>::max();
        }
        CHECK(f.Import(b).Succeeded());
        SameClip(a.Clip, b.Clip);
        CHECK(b.Report.IgnoredNonRootPositions.size() == 3);
        Near(b.Report.IgnoredNonRootPositions[0].Raw.Minimum, -std::numeric_limits<double>::max());
        f.Document = Source();
        f.Document.Joints[0].Offset = {std::numeric_limits<double>::max(), 0, 0};
        f.Document.Joints[1].Offset = f.Document.Joints[0].Offset;
        B::BvhPose generic;
        CHECK(B::EvaluateBvhFrame(f.Document, 0, B::TranslationConvention::OffsetPlusChannels, generic).Status ==
              B::BvhEvaluateStatus::NonFiniteResult);
        CHECK(f.Import(b).Succeeded());
        f.One();
        f.Document.Channels = {B::Channel::Xposition};
        f.Document.Joints[0].ChannelCount = 1;
        f.Document.Values = {0, 1, 2};
        CHECK(f.Import(b).Succeeded());
        CHECK(b.Report.RootPosition.FirstDeltaIssue == A::SkeletalBvhRootDeltaIssue::IncompleteChannels);
        for (const auto& key : b.Clip.Channels[0].Samples)
        {
            Near(key.Value.X, 0);
            Near(key.Value.Y, 0);
            Near(key.Value.Z, 0);
            Near(std::abs(key.Value.W), 1);
        }
    }
    void TestCorrectionForestAndReuse()
    {
        Fixture f;
        f.One();
        f.Document.Values[0] = 0;
        f.Document.Values[3] = 90;
        f.Document.Values[6] = 180;
        Store(f.Joints[0], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 4, -3, -2, 1));
        f.Corrections[0].Values = {0, -1, 0, 1, 0, 0, 0, 0, 1};
        A::SkeletalBvhClipOutput out;
        CHECK(f.Import(out).Succeeded());
        B::Matrix3d expected[3];
        expected[0].Values = {0, 0, 1, 0, 1, 0, -1, 0, 0};
        expected[1].Values = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
        expected[2].Values = {0, 0, -1, 0, 1, 0, 1, 0, 0};
        for (uint32_t i = 0; i < 3; ++i)
        {
            const auto pose = Sample(out.Clip, f.Joints, f.Mesh, .25f * i);
            WorldNear(pose.JointModelMatrices[0], expected[i]);
            Near(pose.JointModelMatrices[0].m30, 2);
            Near(pose.JointModelMatrices[0].m31, 3);
            Near(pose.JointModelMatrices[0].m32, 4);
        }
        f = Fixture{};
        f.Joints = Target(4);
        f.Joints[2].ParentIndex = 1;
        f.Joints[3].ParentIndex = -1;
        Store(f.Joints[1], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -1, 0, 1));
        Store(f.Joints[2], M::Matrix4x4(0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, -2, 0, 1));
        Store(f.Joints[3], M::Matrix4x4(0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 7, 0, 1));
        f.Mappings = Mapping(2, 4, 2);
        f.Mappings.Pairs[1].TargetIndex = 2;
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 6 + 2] = 90 * i;
            f.Document.Values[i * 6 + 3] = 90 * i;
        }
        CHECK(f.Import(out).Succeeded());
        CHECK(out.Clip.Channels.size() == 2 && out.Clip.Channels[0].JointIndex == 0 &&
              out.Clip.Channels[1].JointIndex == 2);
        CHECK(out.Report.UnmappedTarget.size() == 2 && out.Report.UnmappedTarget[0] == 1 &&
              out.Report.UnmappedTarget[1] == 3);
        const auto pose = Sample(out.Clip, f.Joints, f.Mesh, .25f);
        B::Matrix3d middle, tip, other;
        middle.Values = {0, -1, 0, 0, 0, 1, -1, 0, 0};
        tip.Values = {-1, 0, 0, 0, 0, 1, 0, 1, 0};
        other.Values = {0, -1, 0, 1, 0, 0, 0, 0, 1};
        WorldNear(pose.JointModelMatrices[1], middle);
        WorldNear(pose.JointModelMatrices[2], tip);
        WorldNear(pose.JointModelMatrices[3], other);
        Near(pose.JointModelMatrices[2].m30, -2);
        Near(pose.JointModelMatrices[3].m30, 7);
        std::swap(f.Mappings.Pairs[0], f.Mappings.Pairs[1]);
        f.Mappings.RootMappingIndex = 1;
        CHECK(f.Import(out).Succeeded());
        const auto reordered = Sample(out.Clip, f.Joints, f.Mesh, .25f);
        for (size_t i = 0; i < 4; ++i)
        {
            for (size_t k = 0; k < 16; ++k)
            {
                Near(reordered.JointModelMatrices[i].values[k], pose.JointModelMatrices[i].values[k]);
            }
        }
        f = Fixture{};
        f.Document = Source(3, 1);
        f.Mappings = Mapping(1, 2, 2);
        f.Options.SourceReuse = A::SkeletalSourceReusePolicy::Allow;
        for (size_t i = 0; i < 3; ++i)
        {
            f.Document.Values[i * 3 + 2] = 90 * i;
        }
        CHECK(f.Import(out).Succeeded());
        const auto before = Snapshot(out);
        f.Options.SourceReuse = A::SkeletalSourceReusePolicy::Reject;
        CHECK(f.Import(out).Status == Status::InvalidMapping);
        Same(before, Snapshot(out));
    }
    void TestSuppliedNativeValues()
    {
        A::SkeletalRetargetSourceRotation source[1];
        A::SkeletalJointMappingPair pairs[1]{{0, 0, 0}};
        B::Matrix3d correction[1];
        const auto joints = Target(1);
        const A::SkeletalRetargetNativeInput input{C::Span<const A::SkeletalRetargetSourceRotation>(source),
                                                   C::Span<const A::SkeletalJointMappingPair>(pairs),
                                                   C::Span<const B::Matrix3d>(correction),
                                                   {0, 0},
                                                   A::SkeletalSourceReusePolicy::Reject,
                                                   A::SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations};
        A::SkeletalRetargetRotationValue values[1]{{0, 0, 0, 0, 1}};
        const auto check = [&]
        {
            return A::ValidateSkeletalRotationFrameValues(input, C::Span<const S::SkeletalJoint>(joints),
                                                          M::Matrix4x4::Identity,
                                                          C::Span<const A::SkeletalRetargetRotationValue>(values));
        };
        CHECK(check().Succeeded() && check().bFloatRealizationChecked);
        values[0].TargetIndex = 1;
        CHECK(check().Status == A::SkeletalRetargetStatus::InvalidMapping);
        values[0].TargetIndex = 0;
        values[0].W = 2;
        CHECK(check().Status == A::SkeletalRetargetStatus::InvalidRotation);
        values[0].W = std::numeric_limits<float>::quiet_NaN();
        CHECK(check().Status == A::SkeletalRetargetStatus::InvalidRotation);
        values[0] = {0, 1, 0, 0, 0};
        CHECK(check().Status == A::SkeletalRetargetStatus::FloatRealizationMismatch);
    }
    void TestSourcePlanContracts()
    {
        auto source = Source();
        B::BvhRotationSourcePlan plan;
        B::Matrix3d world[2];
        CHECK(B::PrepareBvhRotationSource(source, B::BvhRotationSourceLimits{}, plan).Succeeded());
        B::BvhRotationSourcePlan moved(std::move(plan));
        CHECK(!plan.IsValid() && plan.GetJointCount() == 0 && moved.IsValid());
        moved = std::move(moved);
        CHECK(moved.IsValid());
        CHECK(B::EvaluateBvhRotationFrame(moved, 0, C::Span<B::Matrix3d>(world)).Succeeded());
        auto bad = source;
        bad.Values.back() = std::numeric_limits<double>::quiet_NaN();
        CHECK(!B::PrepareBvhRotationSource(bad, B::BvhRotationSourceLimits{}, moved).Succeeded());
        CHECK(moved.IsValid());
        CHECK(B::EvaluateBvhRotationFrame(moved, 2, C::Span<B::Matrix3d>(world)).Succeeded());
        CHECK(B::EvaluateBvhRotationFrame(moved, 3, C::Span<B::Matrix3d>(world)).Status ==
              B::BvhRotationSourceStatus::FrameOutOfRange);
        CHECK(B::EvaluateBvhRotationFrame(moved, 0, C::Span<B::Matrix3d>(world, 1)).Status ==
              B::BvhRotationSourceStatus::InvalidOutput);
        CHECK(B::EvaluateBvhRotationFrame(moved, 0,
                                          C::Span<B::Matrix3d>(reinterpret_cast<B::Matrix3d*>(source.Values.data()), 2))
                  .Status == B::BvhRotationSourceStatus::InvalidOutput);
    }
    void TestSourcePlanInputBoundaries()
    {
        const auto source = Source(1, 2);
        B::BvhRotationSourcePlan plan;
        CHECK(B::PrepareBvhRotationSource(source, B::BvhRotationSourceLimits{}, plan).Succeeded());
        B::Matrix3d worlds[2];
        const auto reject = [&](const B::BvhDocument& bad, const B::BvhRotationSourceLimits& limits,
                                B::BvhRotationSourceStatus expected)
        {
            CHECK(B::PrepareBvhRotationSource(bad, limits, plan).Status == expected);
            CHECK(plan.IsValid() && plan.GetJointCount() == 2 && plan.GetFrameCount() == 1);
            CHECK(B::EvaluateBvhRotationFrame(plan, 0, C::Span<B::Matrix3d>(worlds)).Succeeded());
            for (size_t i = 0; i < 9; ++i)
            {
                Near(worlds[1].Values[i], i % 4 == 0 ? 1.0 : 0.0);
            }
        };
        auto bad = source;
        bad.Joints[0].Offset.X = std::numeric_limits<double>::quiet_NaN();
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        bad.Joints[1].EndSiteOffset.Z = std::numeric_limits<double>::infinity();
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        ++bad.Joints[1].ChannelOffset;
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        bad.Channels[0] = static_cast<B::Channel>(255);
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        bad.Channels[1] = bad.Channels[0];
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        bad.Values.pop_back();
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = source;
        bad.FrameTimeSeconds = std::numeric_limits<double>::quiet_NaN();
        reject(bad, {}, B::BvhRotationSourceStatus::InvalidDocument);
        bad = Source(1, 1);
        bad.Channels.resize(2);
        bad.Joints[0].ChannelCount = 2;
        bad.Values.resize(2);
        reject(bad, {}, B::BvhRotationSourceStatus::UnsupportedRotationChannels);
        B::BvhRotationSourceLimits limits;
        limits.MaxDepth = 1;
        reject(source, limits, B::BvhRotationSourceStatus::LimitExceeded);
        limits.MaxDepth = 2;
        CHECK(B::PrepareBvhRotationSource(source, limits, plan).Succeeded());
        limits = {};
        limits.MaxJoints = 1;
        reject(source, limits, B::BvhRotationSourceStatus::LimitExceeded);
        limits = {};
        limits.MaxValues = source.Values.size() - 1;
        reject(source, limits, B::BvhRotationSourceStatus::LimitExceeded);
        limits = {};
        limits.MaxNameBytes = 0;
        reject(source, limits, B::BvhRotationSourceStatus::LimitExceeded);
        limits = {};
        limits.MaxTotalNameBytes = source.Joints[0].Name.size() + source.Joints[1].Name.size() - 1;
        reject(source, limits, B::BvhRotationSourceStatus::LimitExceeded);
        ++limits.MaxTotalNameBytes;
        CHECK(B::PrepareBvhRotationSource(source, limits, plan).Succeeded());
        // 0-channel childは親のworld回転を保ち、宣言された位置を回転評価へ持ち込まない。
        auto stationary = source;
        stationary.Joints[1].ChannelCount = 0;
        stationary.Channels.resize(3);
        stationary.Values.assign(3, 0);
        stationary.Values[2] = 90;
        CHECK(B::PrepareBvhRotationSource(stationary, {}, plan).Succeeded());
        CHECK(B::EvaluateBvhRotationFrame(plan, 0, C::Span<B::Matrix3d>(worlds)).Succeeded());
        const double expected[9] = {0, -1, 0, 1, 0, 0, 0, 0, 1};
        for (size_t i = 0; i < 9; ++i)
        {
            Near(worlds[0].Values[i], expected[i]);
            Near(worlds[1].Values[i], expected[i]);
        }
    }
    void TestErrorsBudgetsAndAtomicOutput()
    {
        Fixture good;
        A::SkeletalBvhClipOutput out;
        CHECK(good.Import(out).Succeeded());
        const auto before = Snapshot(out);
        const auto refusal = [&](const Fixture& f, Status status)
        {
            const auto result = f.Import(out);
            CHECK(result.Status == status);
            Same(before, Snapshot(out));
            return result;
        };
        auto bad = good;
        bad.Document.Values.back() = std::numeric_limits<double>::infinity();
        CHECK(refusal(bad, Status::SourceRejected).FrameIndex == 2);
        bad = good;
        bad.Document.Joints[1].Name = bad.Document.Joints[0].Name;
        CHECK(refusal(bad, Status::SourceRejected).Source.Status == B::BvhRotationSourceStatus::DuplicateName);
        bad = good;
        bad.Document.Joints[0].Name = {0xc0, 0xaf};
        CHECK(refusal(bad, Status::SourceRejected).Source.Status == B::BvhRotationSourceStatus::InvalidName);
        bad = good;
        bad.Document.Joints[0].Parent = 0;
        refusal(bad, Status::SourceRejected);
        bad = good;
        bad.Document.Channels[0] = static_cast<B::Channel>(255);
        refusal(bad, Status::SourceRejected);
        bad = good;
        bad.Document.Channels[1] = B::Channel::Xrotation;
        refusal(bad, Status::SourceRejected);
        bad = good;
        bad.Joints[1].Name = bad.Joints[0].Name;
        refusal(bad, Status::InvalidTargetName);
        bad = good;
        bad.Mappings.Pairs[1].TargetIndex = 0;
        refusal(bad, Status::InvalidMapping);
        bad = good;
        bad.Mappings.SourceJointCount = 99;
        refusal(bad, Status::InvalidMapping);
        bad = good;
        bad.Options.Up = Core::AssetImport::SignedAxis::PositiveZ;
        refusal(bad, Status::CoordinateRejected);
        bad = good;
        bad.Options.TimeMode = A::SkeletalBvhClipTimeMode::Unspecified;
        refusal(bad, Status::InvalidSettings);
        bad = good;
        bad.Options.TimeMode = A::SkeletalBvhClipTimeMode::OverrideFps;
        bad.Options.SourceFps = 0;
        refusal(bad, Status::TimeRejected);
        bad = good;
        bad.Corrections[1].Values[0] = 2;
        refusal(bad, Status::RetargetRejected);
        bad = good;
        bad.Limits.MaxOutputKeys = out.Report.OutputKeys - 1;
        refusal(bad, Status::LimitExceeded);
        bad = good;
        bad.Limits.MaxJointFrames = out.Report.JointFrames - 1;
        refusal(bad, Status::LimitExceeded);
        bad = good;
        bad.Limits.MaxWorkUnits = out.Report.WorkUnits - 1;
        refusal(bad, Status::LimitExceeded);
        bad = good;
        bad.Limits.MaxOwnedBytes = out.Report.PlannedOwnedBytes - 1;
        refusal(bad, Status::LimitExceeded);
        bad = good;
        bad.Limits.MaxClipNameBytes = 0;
        refusal(bad, Status::LimitExceeded);
        bad = good;
        bad.Limits.MaxOwnedBytes = out.Report.PlannedOwnedBytes;
        CHECK(bad.Import(out).Succeeded());
        Same(before, Snapshot(out));
        bad = good;
        bad.One(2000);
        bad.Joints = Target(1024);
        bad.Mappings = Mapping(1, 1024, 1024);
        bad.Corrections.resize(1024);
        bad.Options.SourceReuse = A::SkeletalSourceReusePolicy::Allow;
        refusal(bad, Status::LimitExceeded);
        // 生成passの後半で位置がoverflowしても、以前のclip/reportを残す。
        bad = good;
        bad.Document = Source(3, 1);
        bad.Mappings = Mapping(1, 2, 1);
        bad.Corrections.resize(1);
        bad.Document.Values[5] = -90;
        auto ibm = M::Matrix4x4::Identity;
        const float huge = std::numeric_limits<float>::max() * .75f;
        ibm.m30 = -huge;
        Store(bad.Joints[0], ibm);
        ibm.m31 = -huge;
        Store(bad.Joints[1], ibm);
        const auto failed = refusal(bad, Status::RetargetRejected);
        CHECK(failed.FrameIndex == 1 && failed.Retarget.Status == A::SkeletalRetargetStatus::NonFiniteTransform);
    }
} // namespace
int main()
{
    NorvesLib::Tests::ClipProcessingCases::Run();
    TestDecoderThroughActualSampler(false, false);
    TestDecoderThroughActualSampler(true, false);
    TestDecoderThroughActualSampler(false, true);
    TestDecoderThroughActualSampler(true, true);
    TestHemisphereTimeAndOwnership();
    TestInteriorKeyRealizationRefusal();
    TestRootDiagnostics();
    TestIgnoredPositionsAndNoRotationSource();
    TestCorrectionForestAndReuse();
    TestSuppliedNativeValues();
    TestSourcePlanContracts();
    TestSourcePlanInputBoundaries();
    TestErrorsBudgetsAndAtomicOutput();
    std::puts(
        "SKELETAL_BVH_CLIP_IMPORT result=pass source_rotation_plan_explicit_time_hemisphere_actual_keys_root_diagnostics_limits_owned_atomic_no_cli");
    return 0;
}
