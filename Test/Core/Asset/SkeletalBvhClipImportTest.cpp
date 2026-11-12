// 複数frameの所有clip/reportと、最終keyの実Sampler評価を検査する。
#include "Animation/SkeletalBvhClipImport.h"
#include "Animation/SkeletalClipSampling.h"
#include "Animation/SkeletalAnimationSampler.h"
#include "Animation/SkeletonResource.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletalClipRetarget.h"
#include "Resource/SkinnedMeshResource.h"
#include "Resource/SkeletalGltfDecode.h"
#include "Math/MatrixUtils.h"
#include "ClipBankV1Fixture.h"
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
namespace F = NorvesLib::Tests::RigV1Fixture;
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
    struct AlignBonesFixture
    {
        static constexpr size_t JointCount = 9;
        static constexpr uint32_t FrameCount = 9;
        static constexpr double Pi = 3.14159265358979323846;
        using V = B::Vector3d;
        struct Joint
        {
            const char* Name;
            const char* Role;
            int Parent;
            V Offset;
        };
        const Joint Joints[JointCount] = {
            {"Hips", "hips", -1, {0, 1.2, 0}},
            {"Spine", "spine1", 0, {.08, .8, .15}},
            {"Chest", "spine2", 1, {.12, .65, .05}},
            {"Neck", "neck", 2, {.08, .5, .12}},
            {"Head", "head", 3, {.05, .4, .08}},
            {"LArm", "L_arm", 2, {.65, .12, .2}},
            {"LHand", "L_hand", 5, {.6, -.08, .1}},
            {"LLeg", "L_upleg", 0, {.5, -.45, .2}},
            {"LFoot", "L_foot", 7, {.1, -.75, .18}},
        };
        struct Q
        {
            double X = 0, Y = 0, Z = 0, W = 1;
            Q Inverse() const
            {
                return {-X, -Y, -Z, W};
            }
            Q operator*(const Q& b) const
            {
                return {W * b.X + X * b.W + Y * b.Z - Z * b.Y,
                        W * b.Y - X * b.Z + Y * b.W + Z * b.X,
                        W * b.Z + X * b.Y - Y * b.X + Z * b.W,
                        W * b.W - X * b.X - Y * b.Y - Z * b.Z};
            }
            V Rotate(V v) const
            {
                const auto r = *this * Q{v.X, v.Y, v.Z, 0} * Inverse();
                return {r.X, r.Y, r.Z};
            }
        };
        F::Fixture Files;
        C::AnsiString BvhText, ProfileText;
        double Angles[FrameCount][JointCount][3]{};

        static V Add(V a, V b)
        {
            return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
        }
        static V Sub(V a, V b)
        {
            return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
        }
        static V Mul(V a, double b)
        {
            return {a.X * b, a.Y * b, a.Z * b};
        }
        static double Dot(V a, V b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }
        static V Cross(V a, V b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }
        static double Length(V a)
        {
            return std::hypot(a.X, a.Y, a.Z);
        }
        static V Unit(V a)
        {
            const double length = Length(a);
            CHECK(std::isfinite(length) && length > 1e-10);
            return Mul(a, 1 / length);
        }
        static Q AxisAngle(V axis, double degrees)
        {
            axis = Unit(axis);
            const double half = degrees * Pi / 360;
            const double s = std::sin(half);
            return {axis.X * s, axis.Y * s, axis.Z * s, std::cos(half)};
        }
        static double Angle(V a, V b)
        {
            a = Unit(a);
            b = Unit(b);
            return std::atan2(Length(Cross(a, b)), Dot(a, b)) * 180 / Pi;
        }
        static B::Matrix3d Multiply(const B::Matrix3d& a, const B::Matrix3d& b)
        {
            B::Matrix3d result;
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t col = 0; col < 3; ++col)
                {
                    result.Values[row * 3 + col] = 0;
                    for (size_t k = 0; k < 3; ++k)
                    {
                        result.Values[row * 3 + col] += a.Values[row * 3 + k] * b.Values[k * 3 + col];
                    }
                }
            }
            return result;
        }
        static V Transform(const B::Matrix3d& m, V v)
        {
            return {m.Values[0] * v.X + m.Values[1] * v.Y + m.Values[2] * v.Z,
                    m.Values[3] * v.X + m.Values[4] * v.Y + m.Values[5] * v.Z,
                    m.Values[6] * v.X + m.Values[7] * v.Y + m.Values[8] * v.Z};
        }
        static B::Matrix3d Euler(const double* angles)
        {
            const double z = angles[0] * Pi / 180, x = angles[1] * Pi / 180, y = angles[2] * Pi / 180;
            B::Matrix3d rz, rx, ry;
            rz.Values = {std::cos(z), -std::sin(z), 0, std::sin(z), std::cos(z), 0, 0, 0, 1};
            rx.Values = {1, 0, 0, 0, std::cos(x), -std::sin(x), 0, std::sin(x), std::cos(x)};
            ry.Values = {std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y)};
            return Multiply(Multiply(rz, rx), ry);
        }
        void AppendJoint(size_t i)
        {
            char text[256];
            const auto& j = Joints[i];
            std::snprintf(text, sizeof(text), "%s %s { OFFSET %.17g %.17g %.17g CHANNELS %s\n",
                          i == 0 ? "ROOT" : "JOINT", j.Name, j.Offset.X, j.Offset.Y, j.Offset.Z,
                          i == 0 ? "6 Xposition Yposition Zposition Zrotation Xrotation Yrotation"
                                 : "3 Zrotation Xrotation Yrotation");
            BvhText += text;
            bool hasChild = false;
            for (size_t child = i + 1; child < JointCount; ++child)
            {
                if (Joints[child].Parent == int(i))
                {
                    hasChild = true;
                    AppendJoint(child);
                }
            }
            BvhText += hasChild ? "}\n" : "End Site { OFFSET .1 .2 .1 } }\n";
        }
        AlignBonesFixture()
        {
            auto buffer = Files.Binary;
            buffer.resize(buffer.size() + (JointCount + 1) * 16 * sizeof(float), 0);
            for (size_t joint = 0; joint < JointCount + 1; ++joint)
            {
                for (size_t i = 0; i < 16; ++i)
                {
                    F::Float(buffer, Files.Binary.size() + (joint * 16 + i) * sizeof(float), i % 5 == 0 ? 1.f : 0.f);
                }
            }
            Files.SetBuffer(buffer);
            BvhText = "HIERARCHY\n";
            AppendJoint(0);
            BvhText += "MOTION\nFrames: 9\nFrame Time: 0.125\n";
            char text[256];
            for (uint32_t frame = 0; frame < FrameCount; ++frame)
            {
                const auto root = RootTranslation(frame);
                std::snprintf(text, sizeof(text), "%.17g %.17g %.17g ", root.X, root.Y, root.Z);
                BvhText += text;
                for (size_t joint = 0; joint < JointCount; ++joint)
                {
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        const double phase = .51 * (frame + 1) + .37 * (joint + 1) + .73 * (axis + 1);
                        Angles[frame][joint][axis] = 38 * std::sin(phase) + 2.5 * std::cos(phase * .43);
                    }
                    std::snprintf(text, sizeof(text), "%.17g %.17g %.17g ", Angles[frame][joint][0],
                                  Angles[frame][joint][1], Angles[frame][joint][2]);
                    BvhText += text;
                }
                BvhText += "\n";
            }
            ProfileText = R"({"version":1,"vocabulary":"humanoid_v1",)"
                          R"("axes":{"up":"+Y","forward":"+Z","handedness":"right"},)"
                          R"("units":{"position_scale":1},"position_convention":"additive",)"
                          R"("time":{"mode":"header_frame_time"},"source_roles":{)";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":["%s"])", i ? "," : "", Joints[i].Role, Joints[i].Name);
                ProfileText += text;
            }
            ProfileText += "},\"target_roles\":{";
            for (size_t i = 0; i < JointCount; ++i)
            {
                std::snprintf(text, sizeof(text), R"(%s"%s":[{"joint":"%s"}])", i ? "," : "", Joints[i].Role,
                              Joints[i].Name);
                ProfileText += text;
            }
            ProfileText += R"(},"rest_pose":{"mode":"align_bones"}})";
        }
        static V RootTranslation(uint32_t frame)
        {
            return {.11 * (frame + 1), .025 * (frame + 1), -.07 * (frame + 1)};
        }
        C::AnsiString Target(double swingDegrees, double twistDegrees, bool rootFrame, bool extraLeaf,
                             const double* jointSwings = nullptr) const
        {
            const size_t count = JointCount + (extraLeaf ? 1 : 0), mesh = count, armature = count + 1;
            C::AnsiString json = rootFrame ? R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[)"
                                           : R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,)";
            char text[512];
            std::snprintf(text, sizeof(text), rootFrame ? "%zu]}],\"nodes\":[" : "%zu]}],\"nodes\":[",
                          rootFrame ? armature : mesh);
            json += text;
            V offsets[JointCount]{};
            for (size_t i = 1; i < JointCount; ++i)
            {
                const V reference = i % 2 ? V{1, 0, 0} : V{0, 0, 1};
                const auto axis = Cross(Joints[i].Offset, reference);
                const double swing = jointSwings ? jointSwings[i] : (i % 2 ? 1 : -1) * swingDegrees;
                offsets[i] = Mul(AxisAngle(axis, swing).Rotate(Joints[i].Offset), 1.1);
            }
            Q worlds[JointCount];
            double scales[JointCount]{};
            for (size_t i = 0; i < JointCount; ++i)
            {
                const int parentIndex = Joints[i].Parent;
                const Q parent = parentIndex < 0 ? Q{} : worlds[parentIndex];
                const double parentScale = parentIndex < 0 ? 1 : scales[parentIndex];
                V axis = i == 0 ? V{0, 1, 0} : offsets[i];
                for (size_t child = i + 1; child < JointCount; ++child)
                {
                    if (Joints[child].Parent == int(i))
                    {
                        axis = offsets[child];
                        break;
                    }
                }
                worlds[i] = AxisAngle(axis, (i % 2 ? -1 : 1) * twistDegrees) * parent;
                const auto local = parent.Inverse() * worlds[i];
                const auto translation = parentIndex < 0 ? Joints[i].Offset
                                                         : Mul(parent.Inverse().Rotate(offsets[i]), 1 / parentScale);
                const double scale = rootFrame && i == 1 ? 1.25 : rootFrame && i == 5 ? .8 : 1;
                scales[i] = parentScale * scale;
                std::snprintf(text, sizeof(text),
                              R"({"name":"%s","translation":[%.17g,%.17g,%.17g],)"
                              R"("rotation":[%.17g,%.17g,%.17g,%.17g],"scale":[%.17g,%.17g,%.17g])",
                              Joints[i].Name, translation.X, translation.Y, translation.Z, local.X, local.Y, local.Z,
                              local.W, scale, scale, scale);
                json += text;
                bool first = true;
                for (size_t child = i + 1; child < JointCount; ++child)
                {
                    if (Joints[child].Parent == int(i))
                    {
                        std::snprintf(text, sizeof(text), "%s%zu", first ? ",\"children\":[" : ",", child);
                        json += text;
                        first = false;
                    }
                }
                if (extraLeaf && i == 2)
                {
                    std::snprintf(text, sizeof(text), "%s%zu", first ? ",\"children\":[" : ",", JointCount);
                    json += text;
                    first = false;
                }
                json += first ? "}," : "]},";
            }
            if (extraLeaf)
            {
                json += R"({"name":"UnmappedLeaf","translation":[0.2,-0.15,0.4]},)";
            }
            json += rootFrame ? R"({"name":"Mesh","mesh":0,"skin":0,"translation":[0.4,-0.2,0.3],)"
                                R"("rotation":[0,0.25881904510252074,0,0.9659258262890683],"scale":[1.1,1.1,1.1]},)"
                              : R"({"name":"Mesh","mesh":0,"skin":0})";
            if (rootFrame)
            {
                const auto q = AxisAngle({1, 2, 3}, 50);
                std::snprintf(text, sizeof(text),
                              R"({"name":"Armature","children":[0,%zu],"translation":[1.3,-0.7,0.9],)"
                              R"("rotation":[%.17g,%.17g,%.17g,%.17g],"scale":[1.3,1.3,1.3]})",
                              mesh, q.X, q.Y, q.Z, q.W);
                json += text;
            }
            json += "],";
            const auto buffers = Files.Json.find("\"buffers\":");
            const auto skins = Files.Json.find("\"skins\":");
            CHECK(buffers != C::AnsiString::npos && skins != C::AnsiString::npos && buffers < skins);
            auto geometry = Files.Json.substr(buffers, skins - buffers);
            std::snprintf(text, sizeof(text), R"("uri":"fixture.bin","byteLength":%zu)",
                          Files.Binary.size() + (JointCount + 1) * 16 * sizeof(float));
            geometry = F::Replace(geometry, R"("uri":"fixture.bin","byteLength":416)", text);
            std::snprintf(text, sizeof(text), R"({"buffer":0,"byteOffset":416,"byteLength":%zu})",
                          count * 16 * sizeof(float));
            geometry = F::Replace(geometry, R"({"buffer":0,"byteOffset":224,"byteLength":128})", text);
            std::snprintf(text, sizeof(text), R"({"bufferView":9,"componentType":5126,"count":%zu,"type":"MAT4"})",
                          count);
            geometry = F::Replace(geometry, R"({"bufferView":9,"componentType":5126,"count":2,"type":"MAT4"})", text);
            json += geometry;
            json += R"("skins":[{"inverseBindMatrices":9,"skeleton":0,"joints":[)";
            for (size_t i = 0; i < count; ++i)
            {
                std::snprintf(text, sizeof(text), "%s%zu", i ? "," : "", i);
                json += text;
            }
            json += "]}]}";
            return json;
        }
        void SourcePositions(uint32_t frame, V (&positions)[JointCount]) const
        {
            B::Matrix3d rotations[JointCount];
            for (size_t i = 0; i < JointCount; ++i)
            {
                const auto local = Euler(Angles[frame][i]);
                const int parent = Joints[i].Parent;
                if (parent < 0)
                {
                    positions[i] = Add(Joints[i].Offset, RootTranslation(frame));
                    rotations[i] = local;
                }
                else
                {
                    positions[i] = Add(positions[parent], Transform(rotations[parent], Joints[i].Offset));
                    rotations[i] = Multiply(rotations[parent], local);
                }
            }
        }
        bool Direction(size_t joint, const V (&positions)[JointCount], V& direction) const
        {
            V sum{};
            size_t count = 0;
            for (size_t child = 0; child < JointCount; ++child)
            {
                if (Joints[child].Parent == int(joint))
                {
                    sum = Add(sum, Unit(Sub(positions[child], positions[joint])));
                    ++count;
                }
            }
            if (!count)
            {
                return false;
            }
            direction = Unit(sum);
            return true;
        }
    };

    M::Matrix4x4 RowMatrix(const C::FixedArray<float, 16>& values)
    {
        M::Matrix4x4 result;
        for (size_t i = 0; i < 16; ++i)
        {
            result.values[i] = values[i];
        }
        return result;
    }

    void SameRetargetReport(const A::SkeletalClipRetargetReport& a, const A::SkeletalClipRetargetReport& b)
    {
        const auto same = [](double x, double y)
        {
            CHECK(std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y));
        };
        CHECK(a.Corrections.size() == b.Corrections.size());
        for (size_t i = 0; i < a.Corrections.size(); ++i)
        {
            const auto& x = a.Corrections[i];
            const auto& y = b.Corrections[i];
            CHECK(x.SourceJoint == y.SourceJoint && x.TargetJoint == y.TargetJoint &&
                  x.bDirectionMissing == y.bDirectionMissing);
            same(x.BeforeRadians, y.BeforeRadians);
            same(x.AfterRadians, y.AfterRadians);
            for (size_t j = 0; j < 9; ++j)
            {
                same(x.Correction.Values[j], y.Correction.Values[j]);
            }
        }
        CHECK(a.UnmappedSource == b.UnmappedSource && a.UnmappedTarget == b.UnmappedTarget &&
              a.IgnoredTranslationChannels == b.IgnoredTranslationChannels &&
              a.bKeyErrorMeasured == b.bKeyErrorMeasured &&
              a.bRotationErrorMeasured == b.bRotationErrorMeasured && a.UnmappedSourceNames == b.UnmappedSourceNames &&
              a.UnmappedTargetNames == b.UnmappedTargetNames);
        same(a.RootScale, b.RootScale);
        same(a.SourceLegLengthMeters, b.SourceLegLengthMeters);
        same(a.TargetLegLengthMeters, b.TargetLegLengthMeters);
        same(a.RotationErrorMaxDegrees, b.RotationErrorMaxDegrees);
        same(a.RotationErrorMeanDegrees, b.RotationErrorMeanDegrees);
        same(a.SourceRootDistanceMeters, b.SourceRootDistanceMeters);
        same(a.TargetRootDistanceMeters, b.TargetRootDistanceMeters);
        const auto& p = a.Processing;
        const auto& q = b.Processing;
        CHECK(p.bLoopDetected == q.bLoopDetected && p.bRangeSelected == q.bRangeSelected &&
              p.PoseEvaluations == q.PoseEvaluations && p.OutputSamples == q.OutputSamples &&
              p.ReplacedSpikes == q.ReplacedSpikes && p.SmoothedChannels == q.SmoothedChannels &&
              p.AveragedCycles == q.AveragedCycles);
        same(p.StartSeconds, q.StartSeconds);
        same(p.EndSeconds, q.EndSeconds);
        same(p.PeriodSeconds, q.PeriodSeconds);
        same(p.RecurrenceRmsRadians, q.RecurrenceRmsRadians);
        same(p.SeamBeforeRadians, q.SeamBeforeRadians);
        same(p.SeamAfterRadians, q.SeamAfterRadians);
        same(p.SeamVelocityDifferenceRadiansPerSecond, q.SeamVelocityDifferenceRadiansPerSecond);
        same(p.PlanarDistanceMeters, q.PlanarDistanceMeters);
        same(p.AverageSpeedMetersPerSecond, q.AverageSpeedMetersPerSecond);
        const auto& x = a.FootPlanting;
        const auto& y = b.FootPlanting;
        CHECK(x.bMeasured == y.bMeasured && x.IkFailedFrames == y.IkFailedFrames && x.Legs.empty() && y.Legs.empty());
        same(x.FootSlideBeforeMeters, y.FootSlideBeforeMeters);
        same(x.FootSlideAfterMeters, y.FootSlideAfterMeters);
        same(x.FootPenetrationMeters, y.FootPenetrationMeters);
        same(x.FootFloatMeters, y.FootFloatMeters);
        same(x.IkAdjustmentDegrees, y.IkAdjustmentDegrees);
        same(x.PelvisDropMeters, y.PelvisDropMeters);
    }

    void SameRetargetClip(const S::SkeletalAnimationClip& a, const S::SkeletalAnimationClip& b)
    {
        SameClip(a, b);
        const auto same = [](float x, float y)
        {
            CHECK(std::bit_cast<uint32_t>(x) == std::bit_cast<uint32_t>(y));
        };
        CHECK(a.Name == b.Name && a.RootMotionJoint == b.RootMotionJoint && a.RootMotion.empty() &&
              b.RootMotion.empty());
        CHECK(A::SameClipMetadata(a.Metadata, b.Metadata));
        CHECK(a.Metadata.Events.empty() && b.Metadata.Events.empty() && a.Metadata.Markers.empty() &&
              b.Metadata.Markers.empty());
        same(a.Metadata.Loop.Start, b.Metadata.Loop.Start);
        same(a.Metadata.Loop.End, b.Metadata.Loop.End);
        same(a.Metadata.Root.NominalSpeed, b.Metadata.Root.NominalSpeed);
        same(a.Metadata.GroundOffset, b.Metadata.GroundOffset);
        same(a.DurationSeconds, b.DurationSeconds);
        for (size_t i = 0; i < a.Channels.size(); ++i)
        {
            same(a.Channels[i].UniformStartSeconds, b.Channels[i].UniformStartSeconds);
            same(a.Channels[i].UniformStepSeconds, b.Channels[i].UniformStepSeconds);
            for (size_t j = 0; j < a.Channels[i].Samples.size(); ++j)
            {
                const auto& x = a.Channels[i].Samples[j];
                const auto& y = b.Channels[i].Samples[j];
                same(x.TimeSeconds, y.TimeSeconds);
                same(x.Value.X, y.Value.X);
                same(x.Value.Y, y.Value.Y);
                same(x.Value.Z, y.Value.Z);
                same(x.Value.W, y.Value.W);
            }
        }
    }

    double BoneDirectionError(const AlignBonesFixture& f, size_t joint,
                              const AlignBonesFixture::V (&reference)[AlignBonesFixture::JointCount],
                              const AlignBonesFixture::V (&actual)[AlignBonesFixture::JointCount])
    {
        AlignBonesFixture::V expectedDirection, actualDirection;
        CHECK(f.Direction(joint, reference, expectedDirection));
        CHECK(f.Direction(joint, actual, actualDirection));
        return AlignBonesFixture::Angle(expectedDirection, actualDirection);
    }

    void TestAlignBonesDirections(bool rootFrame)
    {
        using Fixture = AlignBonesFixture;
        using V = Fixture::V;
        Fixture f;
        B::BvhDocument document;
        CHECK(B::DecodeBvh(F::View(f.BvhText), {}, document).Succeeded());
        CHECK(document.FrameCount == Fixture::FrameCount && document.Joints.size() == Fixture::JointCount);
        A::SkeletalRoleProfile profile;
        CHECK(A::ParseSkeletalRoleProfile(F::View(f.ProfileText), {}, profile).Succeeded());
        A::SkeletalRetargetClipSource source;
        C::AnsiString error;
        CHECK(A::MakeBvhRetargetClipSource(document, profile.Settings, C::String("AlignBones"), source, error));
        S::RigAuthoringCpu target;
        S::RigV1Report importReport;
        S::RigV1Limits limits;
        S::RigClipSourceSelection selection;
        selection.bAllowEmptyAnimations = true;
        const auto targetJson = f.Target(20, 20, rootFrame, true);
        if (!S::DecodeRigAuthoringWithProfileNativePath(F::View(targetJson), "RigV1Fixture/target.gltf",
                S::RigImportProfile::StaticRootFrame256, target, importReport, limits, nullptr, nullptr, nullptr,
                &selection))
        {
            std::fprintf(stderr, "align_bones の対象の読み込みに失敗: status=%u decode=%u\n",
                         unsigned(importReport.Status), unsigned(importReport.DecodeStatus));
            CHECK(false);
        }
        // clip 専用の選択は mesh frame を保持しないので、同じ入力を mesh 込みでも読み取る。
        S::RigAuthoringCpu meshTarget;
        CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(targetJson), "RigV1Fixture/target.gltf",
              S::RigImportProfile::StaticRootFrame256, meshTarget, importReport, limits, nullptr, nullptr, nullptr,
              nullptr));
        const auto* selected = target.GetData();
        const auto* data = meshTarget.GetData();
        CHECK(selected && data && selected->RootFrame == data->RootFrame &&
              selected->LocalRest.size() == data->LocalRest.size() &&
              data->Geometry.Joints.size() == Fixture::JointCount + 1);
        for (size_t i = 0; i < Fixture::JointCount; ++i)
        {
            CHECK(data->Geometry.Joints[i].Name == C::String(f.Joints[i].Name));
            CHECK(data->Geometry.Joints[i].ParentIndex == f.Joints[i].Parent);
            CHECK(selected->Geometry.Joints[i].Name == data->Geometry.Joints[i].Name &&
                  selected->Geometry.Joints[i].ParentIndex == data->Geometry.Joints[i].ParentIndex);
        }
        for (size_t i = 0; i < data->LocalRest.size(); ++i)
        {
            const auto& x = selected->LocalRest[i];
            const auto& y = data->LocalRest[i];
            CHECK(x.Translation.X == y.Translation.X && x.Translation.Y == y.Translation.Y &&
                  x.Translation.Z == y.Translation.Z && x.Rotation.X == y.Rotation.X && x.Rotation.Y == y.Rotation.Y &&
                  x.Rotation.Z == y.Rotation.Z && x.Rotation.W == y.Rotation.W && x.Scale.X == y.Scale.X &&
                  x.Scale.Y == y.Scale.Y && x.Scale.Z == y.Scale.Z);
        }
        const auto mesh = RowMatrix(data->Geometry.MeshNodeGlobalTransform);
        const auto frame = RowMatrix(data->RootFrame);
        if (rootFrame)
        {
            bool differentFrame = false, differentMesh = false;
            for (size_t i = 0; i < 16; ++i)
            {
                const float identity = i % 5 == 0 ? 1.f : 0.f;
                differentFrame |= std::abs(data->RootFrame[i] - identity) > 1e-5f;
                differentMesh |= std::abs(data->Geometry.MeshNodeGlobalTransform[i] - identity) > 1e-5f;
            }
            CHECK(differentFrame && differentMesh);
        }
        A::SkeletalClipRetargetSettings settings;
        settings.RestMode = A::SkeletalRestCorrectionMode::AlignBones;
        settings.UpHint = {0, 1, 0};
        settings.Processing.Loop = A::SkeletalLoopSelection::None;
        settings.Processing.bExtractRootMotion = false;
        settings.Processing.OutputFps = 8;
        S::SkeletalAnimationClip clip, repeated, twisted;
        A::SkeletalClipRetargetReport report, repeatedReport, twistedReport;
        if (!A::RetargetSkeletalClip(source, target, profile, settings, clip, report, error))
        {
            std::fprintf(stderr, "align_bones の変換に失敗: %s\n", error.c_str());
            CHECK(false);
        }
        CHECK(report.UnmappedTarget == 1);
        CHECK(A::RetargetSkeletalClip(source, target, profile, settings, repeated, repeatedReport, error));
        SameRetargetClip(clip, repeated);
        SameRetargetReport(report, repeatedReport);
        if (!rootFrame)
        {
            settings.UpHint = {0, 0, 1};
            CHECK(A::RetargetSkeletalClip(source, target, profile, settings, twisted, twistedReport, error));
        }
        Core::SkeletonResource skeleton;
        Core::AnimationClipResource resource, twistedResource;
        Core::SkinnedMeshResource meshResource;
        skeleton.Initialize();
        resource.Initialize();
        twistedResource.Initialize();
        meshResource.Initialize();
        skeleton.SetJoints(C::VariableArray<S::SkeletalJoint>(data->Geometry.Joints));
        CHECK(skeleton.SetAuthorRestPose(data->LocalRest) && skeleton.Load());
        resource.SetClip(S::SkeletalAnimationClip(clip));
        CHECK(resource.Load());
        if (!rootFrame)
        {
            twistedResource.SetClip(S::SkeletalAnimationClip(twisted));
            CHECK(twistedResource.Load());
        }
        double maximum = 0, maxHintDirection = 0, maxTwist = 0, maxAxisError = 0;
        size_t compared = 0, twistCount = 0;
        for (uint32_t sample = 0; sample < Fixture::FrameCount; ++sample)
        {
            A::SkeletalPoseSnapshot pose, other;
            CHECK(A::SkeletalAnimationSampler::Sample(skeleton, resource, meshResource, sample * .125f, mesh, pose));
            if (!rootFrame)
            {
                CHECK(A::SkeletalAnimationSampler::Sample(skeleton, twistedResource, meshResource, sample * .125f,
                                                          mesh, other));
            }
            V reference[Fixture::JointCount], actual[Fixture::JointCount], alternate[Fixture::JointCount];
            M::Matrix4x4 world[Fixture::JointCount], alternateWorld[Fixture::JointCount];
            f.SourcePositions(sample, reference);
            for (size_t i = 0; i < Fixture::JointCount; ++i)
            {
                // 旧形式のサンプラー出力にメッシュ変換を戻し、分割形式と同じ root frame を適用する。
                world[i] = pose.JointModelMatrices[i] * mesh * frame;
                actual[i] = {world[i].m30, world[i].m31, world[i].m32};
                if (!rootFrame)
                {
                    alternateWorld[i] = other.JointModelMatrices[i] * mesh * frame;
                    alternate[i] = {alternateWorld[i].m30, alternateWorld[i].m31, alternateWorld[i].m32};
                }
            }
            for (size_t i = 0; i < Fixture::JointCount; ++i)
            {
                V expectedDirection, actualDirection, alternateDirection;
                if (!f.Direction(i, reference, expectedDirection))
                {
                    continue;
                }
                CHECK(f.Direction(i, actual, actualDirection));
                maximum = std::max(maximum, BoneDirectionError(f, i, reference, actual));
                ++compared;
                if (!rootFrame)
                {
                    CHECK(f.Direction(i, alternate, alternateDirection));
                    maxHintDirection = std::max(maxHintDirection, Fixture::Angle(actualDirection, alternateDirection));
                    double relative[3][3]{};
                    for (size_t r = 0; r < 3; ++r)
                    {
                        for (size_t c = 0; c < 3; ++c)
                        {
                            for (size_t k = 0; k < 3; ++k)
                            {
                                const V rowA{world[i].values[k * 4], world[i].values[k * 4 + 1],
                                             world[i].values[k * 4 + 2]};
                                const V rowB{alternateWorld[i].values[k * 4], alternateWorld[i].values[k * 4 + 1],
                                             alternateWorld[i].values[k * 4 + 2]};
                                relative[r][c] += alternateWorld[i].values[k * 4 + r] / Fixture::Length(rowB) *
                                                  world[i].values[k * 4 + c] / Fixture::Length(rowA);
                            }
                        }
                    }
                    const V axis{relative[2][1] - relative[1][2], relative[0][2] - relative[2][0],
                                 relative[1][0] - relative[0][1]};
                    const double angle =
                        std::atan2(Fixture::Length(axis), relative[0][0] + relative[1][1] + relative[2][2] - 1) *
                        180 / Fixture::Pi;
                    maxTwist = std::max(maxTwist, angle);
                    if (angle >= 1)
                    {
                        ++twistCount;
                        const auto unitAxis = Fixture::Unit(axis);
                        const double axisError =
                            std::atan2(Fixture::Length(Fixture::Cross(unitAxis, actualDirection)),
                                       std::abs(Fixture::Dot(unitAxis, actualDirection))) * 180 / Fixture::Pi;
                        maxAxisError = std::max(maxAxisError, axisError);
                    }
                }
            }
        }
        std::fprintf(stderr, "ALIGN_BONES target=%s max_direction_degrees=%.9f max_hint_difference_degrees=%.9f "
                             "max_twist_degrees=%.9f max_axis_error_degrees=%.9f comparisons=%zu\n",
                     rootFrame ? "B" : "A", maximum, maxHintDirection, maxTwist, maxAxisError, compared);
        CHECK(compared == Fixture::FrameCount * 6);
        CHECK(maximum <= .1);
        if (!rootFrame)
        {
            CHECK(maxHintDirection <= 1e-4);
            CHECK(twistCount > 0 && maxTwist >= 1 && maxAxisError <= .01);
        }
    }
    struct RestCorrectionCase
    {
        using Fixture = AlignBonesFixture;
        using V = Fixture::V;
        Fixture Input;
        A::SkeletalRoleProfile Profile;
        A::SkeletalRetargetClipSource Source;
        S::RigAuthoringCpu Target;
        A::SkeletalClipRetargetSettings Settings;
        C::AnsiString Error;
        V SourceRest[Fixture::JointCount]{};
        V TargetRest[Fixture::JointCount]{};

        RestCorrectionCase()
        {
            B::BvhDocument document;
            CHECK(B::DecodeBvh(F::View(Input.BvhText), {}, document).Succeeded());
            CHECK(A::ParseSkeletalRoleProfile(F::View(Input.ProfileText), {}, Profile).Succeeded());
            CHECK(A::MakeBvhRetargetClipSource(document, Profile.Settings, C::String("RestCorrection"), Source, Error));
            // BVH の OFFSET を足して rest を得る。アニメーションの先頭姿勢は使わない。
            for (size_t i = 0; i < Fixture::JointCount; ++i)
            {
                const auto& joint = document.Joints[i];
                SourceRest[i] = joint.Parent == UINT32_MAX ? joint.Offset
                    : Fixture::Add(SourceRest[joint.Parent], joint.Offset);
            }
            Settings.Processing.Loop = A::SkeletalLoopSelection::None;
            Settings.Processing.bExtractRootMotion = false;
            Settings.Processing.OutputFps = 8;
        }

        void Load(double swing, const double* jointSwings = nullptr)
        {
            // 捻りを付けず rest の位置を double の FK で組み立て、細かな補正行列も比較できるようにする。
            auto json = Input.Target(swing, 0, false, true, jointSwings);
            // 未写像の回転と scale も既定値への置換を検出できる bind にする。
            json = F::Replace(json, R"({"name":"UnmappedLeaf","translation":[0.2,-0.15,0.4]})",
                              R"({"name":"UnmappedLeaf","translation":[0.2,-0.15,0.4],)"
                              R"("rotation":[0,0,0.25881904510252074,0.9659258262890683],"scale":[1.2,1.2,1.2]})");
            S::RigV1Report report;
            CHECK(S::DecodeRigAuthoringWithProfileNativePath(F::View(json), "RigV1Fixture/target.gltf",
                  S::RigImportProfile::StaticRootFrame256, Target, report));
            const auto* data = Target.GetData();
            CHECK(data && data->LocalRest.size() == Fixture::JointCount + 1);
            for (size_t i = 0; i < Fixture::JointCount; ++i)
            {
                const auto& rest = data->LocalRest[i];
                CHECK(rest.Rotation.X == 0 && rest.Rotation.Y == 0 && rest.Rotation.Z == 0 && rest.Rotation.W == 1);
                CHECK(rest.Scale.X == 1 && rest.Scale.Y == 1 && rest.Scale.Z == 1);
                const V offset{rest.Translation.X, rest.Translation.Y, rest.Translation.Z};
                const int parent = data->Geometry.Joints[i].ParentIndex;
                TargetRest[i] = parent < 0 ? offset : Fixture::Add(TargetRest[parent], offset);
            }
        }

        V RestDirection(size_t joint, const V (&positions)[Fixture::JointCount]) const
        {
            V direction;
            if (Input.Direction(joint, positions, direction))
            {
                return direction;
            }
            CHECK(Input.Joints[joint].Parent >= 0);
            return Fixture::Unit(Fixture::Sub(positions[joint], positions[Input.Joints[joint].Parent]));
        }

        bool Run(S::SkeletalAnimationClip& clip, A::SkeletalClipRetargetReport& report)
        {
            const bool success = A::RetargetSkeletalClip(Source, Target, Profile, Settings, clip, report, Error);
            if (!success)
            {
                std::fprintf(stderr, "REST_CORRECTION error=%s\n", Error.c_str());
            }
            return success;
        }

        void Reject(const char* expected, S::SkeletalAnimationClip& clip, A::SkeletalClipRetargetReport& report)
        {
            const auto beforeClip = clip;
            const auto beforeReport = report;
            CHECK(!Run(clip, report));
            CHECK(Error == expected);
            SameRetargetClip(beforeClip, clip);
            SameRetargetReport(beforeReport, report);
        }

        void SampleDirections(const S::SkeletalAnimationClip& clip, bool checkUnmapped)
        {
            const auto* data = Target.GetData();
            Core::SkeletonResource skeleton;
            Core::AnimationClipResource resource;
            Core::SkinnedMeshResource meshResource;
            skeleton.Initialize();
            resource.Initialize();
            meshResource.Initialize();
            skeleton.SetJoints(C::VariableArray<S::SkeletalJoint>(data->Geometry.Joints));
            CHECK(skeleton.SetAuthorRestPose(data->LocalRest) && skeleton.Load());
            resource.SetClip(S::SkeletalAnimationClip(clip));
            CHECK(resource.Load());
            const auto mesh = RowMatrix(data->Geometry.MeshNodeGlobalTransform);
            const auto frame = RowMatrix(data->RootFrame);
            const size_t leaf = Fixture::JointCount;
            if (checkUnmapped)
            {
                CHECK(data->Geometry.Joints[leaf].Name == C::String("UnmappedLeaf"));
                for (const auto& channel : clip.Channels)
                {
                    CHECK(channel.JointIndex != leaf);
                }
            }
            double maximum = 0;
            size_t compared = 0;
            for (uint32_t sample = 0; sample < Fixture::FrameCount; ++sample)
            {
                A::SkeletalPoseSnapshot pose;
                CHECK(A::SkeletalAnimationSampler::Sample(skeleton, resource, meshResource, sample * .125f, mesh, pose));
                V reference[Fixture::JointCount], actual[Fixture::JointCount];
                Input.SourcePositions(sample, reference);
                for (size_t i = 0; i < Fixture::JointCount; ++i)
                {
                    const auto world = pose.JointModelMatrices[i] * mesh * frame;
                    actual[i] = {world.m30, world.m31, world.m32};
                }
                for (size_t i = 0; i < Fixture::JointCount; ++i)
                {
                    V direction;
                    if (Input.Direction(i, reference, direction))
                    {
                        maximum = std::max(maximum, BoneDirectionError(Input, i, reference, actual));
                        ++compared;
                    }
                }
                if (checkUnmapped)
                {
                    // 行ベクトルの子行列から動く親を除き、bind のローカル TRS と照合する。
                    const int parent = data->Geometry.Joints[leaf].ParentIndex;
                    CHECK(parent == 2);
                    const auto local = pose.JointModelMatrices[leaf] * M::MatrixUtils::Inverse(pose.JointModelMatrices[parent]);
                    const auto& rest = data->LocalRest[leaf];
                    Near(local.m30, rest.Translation.X, 1e-6);
                    Near(local.m31, rest.Translation.Y, 1e-6);
                    Near(local.m32, rest.Translation.Z, 1e-6);
                    const Fixture::Q rotation{rest.Rotation.X, rest.Rotation.Y, rest.Rotation.Z, rest.Rotation.W};
                    const double scales[]{rest.Scale.X, rest.Scale.Y, rest.Scale.Z};
                    const V basis[]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                    for (size_t row = 0; row < 3; ++row)
                    {
                        const V axis{local.values[row * 4], local.values[row * 4 + 1], local.values[row * 4 + 2]};
                        Near(Fixture::Length(axis), scales[row], 1e-6);
                        const V actualAxis = Fixture::Unit(axis), expectedAxis = rotation.Rotate(basis[row]);
                        Near(actualAxis.X, expectedAxis.X, 1e-6);
                        Near(actualAxis.Y, expectedAxis.Y, 1e-6);
                        Near(actualAxis.Z, expectedAxis.Z, 1e-6);
                    }
                }
            }
            std::fprintf(stderr, "REST_CORRECTION max_direction_degrees=%.9f comparisons=%zu unmapped=%u\n",
                         maximum, compared, unsigned(checkUnmapped));
            CHECK(compared == Fixture::FrameCount * 6);
            CHECK(maximum <= .1);
        }
    };

    void TestMatchRestDirections()
    {
        using F = AlignBonesFixture;
        RestCorrectionCase c;
        const double swings[]{0, 1.1, -1.3, 1.5, -1.7, 1.9, -2.1, 2.3, -2.5};
        c.Load(0, swings);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::Match;
        S::SkeletalAnimationClip clip;
        A::SkeletalClipRetargetReport report;
        CHECK(c.Run(clip, report));
        CHECK(report.Corrections.size() == F::JointCount);
        double maximum = 0;
        for (const auto& correction : report.Corrections)
        {
            CHECK(correction.SourceJoint < F::JointCount && correction.TargetJoint < F::JointCount);
            const double expected = F::Angle(c.RestDirection(correction.SourceJoint, c.SourceRest),
                                             c.RestDirection(correction.TargetJoint, c.TargetRest)) * F::Pi / 180;
            std::fprintf(stderr, "MATCH_REST joint=%u expected_radians=%.12f before_radians=%.12f\n",
                         correction.TargetJoint, expected, correction.BeforeRadians);
            Near(correction.BeforeRadians, expected, 1e-6);
            CHECK(!correction.bDirectionMissing && correction.AfterRadians == correction.BeforeRadians);
            for (size_t i = 0; i < 9; ++i)
            {
                CHECK(correction.Correction.Values[i] == (i % 4 == 0 ? 1 : 0));
            }
            maximum = std::max(maximum, expected);
        }
        CHECK(maximum > 2 * F::Pi / 180 && maximum < 5 * F::Pi / 180);
        // 失敗時に空出力との取り違えを検出できる、非既定値の clip と報告を残す。
        clip.Name = "AtomicSentinel";
        clip.DurationSeconds = 17;
        clip.Channels[0].Samples[0].Value.X = .375f;
        report.RootScale = 7;
        report.Processing.PoseEvaluations = 123;
        report.Corrections[0].BeforeRadians = .625;
        c.Settings.MaximumRestErrorRadians = maximum - .01 * F::Pi / 180;
        c.Reject("rest_direction_mismatch", clip, report);
        c.Load(20);
        c.Settings.MaximumRestErrorRadians = A::SkeletalClipRetargetSettings{}.MaximumRestErrorRadians;
        c.Reject("rest_direction_mismatch", clip, report);
    }

    void TestRestCorrectionEdges()
    {
        using F = AlignBonesFixture;
        RestCorrectionCase c;
        c.Load(20);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::AlignBones;
        S::SkeletalAnimationClip clip;
        A::SkeletalClipRetargetReport report;
        CHECK(c.Run(clip, report));
        size_t leaves = 0;
        for (const auto& correction : report.Corrections)
        {
            const size_t joint = correction.TargetJoint;
            if (joint == 4 || joint == 6 || joint == 8)
            {
                const auto parent = c.Input.Joints[joint].Parent;
                const double expected = F::Angle(F::Sub(c.SourceRest[joint], c.SourceRest[parent]),
                                                 F::Sub(c.TargetRest[joint], c.TargetRest[parent])) * F::Pi / 180;
                Near(correction.BeforeRadians, expected, 1e-6);
                CHECK(correction.AfterRadians < 1e-6 && !correction.bDirectionMissing);
                ++leaves;
            }
        }
        CHECK(leaves == 3);
        CHECK(report.UnmappedTarget >= 1);
        CHECK(std::find(report.UnmappedTargetNames.begin(), report.UnmappedTargetNames.end(),
                        C::AnsiString("UnmappedLeaf")) != report.UnmappedTargetNames.end());
        c.SampleDirections(clip, true);

        const double antiparallel[]{0, 0, 0, 0, 180, 0, 0, 0, 0};
        c.Load(0, antiparallel);
        CHECK(c.Run(clip, report));
        size_t opposite = 0;
        for (const auto& correction : report.Corrections)
        {
            if (correction.TargetJoint == 3)
            {
                Near(correction.BeforeRadians, F::Pi, 1e-6);
                CHECK(correction.AfterRadians < 1e-6);
                ++opposite;
            }
        }
        CHECK(opposite == 1);
        c.SampleDirections(clip, false);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::Match;
        c.Reject("rest_direction_mismatch", clip, report);

        c.Load(20);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::AlignBones;
        // source の保存精度で厳密に平行にし、up の投影が退化する条件を与える。
        const auto& offset = c.Source.Joints[4].Rest.Translation;
        c.Settings.UpHint = {offset.X, offset.Y, offset.Z};
        CHECK(c.Run(clip, report));
        const auto from = F::Unit(c.Settings.UpHint), to = c.RestDirection(3, c.TargetRest);
        const double degrees = std::acos(std::clamp(F::Dot(from, to), -1.0, 1.0)) * 180 / F::Pi;
        const auto swing = F::AxisAngle(F::Cross(from, to), degrees);
        size_t parallel = 0;
        for (const auto& correction : report.Corrections)
        {
            if (correction.TargetJoint == 3)
            {
                const F::V basis[]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                for (size_t col = 0; col < 3; ++col)
                {
                    const auto expected = swing.Rotate(basis[col]);
                    Near(correction.Correction.Values[col], expected.X, 1e-9);
                    Near(correction.Correction.Values[3 + col], expected.Y, 1e-9);
                    Near(correction.Correction.Values[6 + col], expected.Z, 1e-9);
                }
                ++parallel;
            }
        }
        CHECK(parallel == 1);
        c.SampleDirections(clip, false);
        c.Settings.UpHint = {0, 0, 0};
        c.Reject("retarget_input", clip, report);

        c.Settings.UpHint = {0, 1, 0};
        for (size_t role = 1; role < A::SkeletalRoleCount; ++role)
        {
            c.Profile.Source[role].clear();
            c.Profile.Target[role].clear();
        }
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::Match;
        c.Reject("rest_bone_direction_missing", clip, report);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::AlignBones;
        c.Reject("rest_bone_direction_missing", clip, report);
        c.Settings.RestMode = A::SkeletalRestCorrectionMode::Explicit;
        CHECK(c.Run(clip, report));
        CHECK(report.Corrections.size() == 1 && report.Corrections[0].SourceJoint == 0 &&
              report.Corrections[0].TargetJoint == 0 && report.Corrections[0].bDirectionMissing);
        for (size_t i = 0; i < 9; ++i)
        {
            CHECK(report.Corrections[0].Correction.Values[i] == (i % 4 == 0 ? 1 : 0));
        }
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
    TestAlignBonesDirections(false);
    TestAlignBonesDirections(true);
    TestMatchRestDirections();
    TestRestCorrectionEdges();
    std::puts(
        "SKELETAL_BVH_CLIP_IMPORT result=pass source_rotation_plan_explicit_time_hemisphere_actual_keys_root_diagnostics_limits_owned_atomic_no_cli");
    return 0;
}
