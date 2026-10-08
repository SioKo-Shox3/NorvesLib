#include "Animation/SkeletalClipRetarget.h"
#include "Animation/RigRootFrame.h"
#include "Animation/SkeletalClipSampling.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Resource/ImportTransform.h"
#include <algorithm>
#include <cmath>
namespace NorvesLib::Core::Animation
{
    namespace C = Container;
    namespace S = Skeletal;
    namespace D = Detail;
    using Q = Math::Quaternion;
    using V = Bvh::Vector3d;
    using M = Bvh::Matrix3d;
    namespace
    {
        bool Fail(C::AnsiString& error, const char* reason)
        {
            error = reason;
            return false;
        }
        C::Span<const uint8_t> Bytes(const C::AnsiString& name)
        {
            return {reinterpret_cast<const uint8_t*>(name.data()), name.size()};
        }
        Q Rotation(const S::SkeletalValue& v)
        {
            return D::NormalizeQuaternion(Q(v.X, v.Y, v.Z, v.W));
        }
        Q Inverse(Q q)
        {
            return {-q.x, -q.y, -q.z, q.w};
        }
        M MatrixComponents(double x, double y, double z, double w)
        {
            const double norm = std::hypot(std::hypot(x, y), std::hypot(z, w));
            x /= norm;
            y /= norm;
            z /= norm;
            w /= norm;
            M m;
            m.Values = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
            return m;
        }
        M Matrix(Q q)
        {
            return MatrixComponents(q.x, q.y, q.z, q.w);
        }
        bool Quaternion(const M& m, Q& q)
        {
            D::RetargetQuaterniond value;
            if (!D::RetargetExtractQuaternion(m, value))
            {
                return false;
            }
            q = D::NormalizeQuaternion(Q(float(value.X), float(value.Y), float(value.Z), float(value.W)));
            return S::IsRepresentableSkeletalRotation({q.x, q.y, q.z, q.w});
        }
        V Add(V a, V b)
        {
            return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
        }
        V Sub(V a, V b)
        {
            return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
        }
        V Mul(V a, double s)
        {
            return {a.X * s, a.Y * s, a.Z * s};
        }
        double Dot(V a, V b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }
        V Cross(V a, V b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }
        bool Unit(V& v)
        {
            const double n = std::hypot(v.X, v.Y, v.Z);
            if (!std::isfinite(n) || n < 1e-10)
            {
                return false;
            }
            v = Mul(v, 1 / n);
            return true;
        }
        V Transform(const M& m, V v)
        {
            return {m.Values[0] * v.X + m.Values[1] * v.Y + m.Values[2] * v.Z,
                    m.Values[3] * v.X + m.Values[4] * v.Y + m.Values[5] * v.Z,
                    m.Values[6] * v.X + m.Values[7] * v.Y + m.Values[8] * v.Z};
        }
        bool Frame(const S::RigRootFrame& f, M& rotation, V& position, double& scale)
        {
            if (!S::IsValidRigRootFrame(f, S::RigImportProfile::StaticRootFrame256))
            {
                return false;
            }
            scale = std::sqrt(double(f[0]) * f[0] + double(f[1]) * f[1] + double(f[2]) * f[2]);
            for (size_t r = 0; r < 3; ++r)
            {
                for (size_t c = 0; c < 3; ++c)
                {
                    rotation.Values[r * 3 + c] = f[c * 4 + r] / scale;
                }
            }
            D::RetargetQuaterniond q;
            if (!D::RetargetExtractQuaternion(rotation, q))
            {
                return false;
            }
            rotation = MatrixComponents(q.X, q.Y, q.Z, q.W);
            position = {f[12], f[13], f[14]};
            return true;
        }
        bool EncodeName(const C::String& native, C::AnsiString& name)
        {
            const auto measured = Asset::MeasureSkeletalNameEncoding(
                2, C::Span<const C::String::value_type>{native.data(), native.size()});
            if (!measured.Succeeded() || !measured.ByteCount || measured.ByteCount > 4096)
            {
                return false;
            }
            C::VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, C::Span<const C::String::value_type>{native.data(), native.size()},
                                               C::Span<uint8_t>{bytes})
                     .Succeeded())
            {
                return false;
            }
            name = C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            return true;
        }
        bool RestWorld(C::Span<const SkeletalRetargetJoint> joints, const S::RigRootFrame& frame,
                       C::VariableArray<uint32_t>& order, C::VariableArray<M>& rotations,
                       C::VariableArray<V>& positions)
        {
            M f;
            V p;
            double scale = 1;
            if (joints.empty() || joints.size() > 1024 || !Frame(frame, f, p, scale))
            {
                return false;
            }
            p = {}; // 骨方向とroot差分には定数frame原点を加えない。
            C::VariableArray<double> scales(joints.size());
            C::VariableArray<uint8_t> ready(joints.size(), 0);
            rotations.resize(joints.size());
            positions.resize(joints.size());
            uint32_t roots = 0;
            for (const auto& joint : joints)
            {
                const auto& s = joint.Rest.Scale;
                if (!S::IsValidSkeletalRestTransform(joint.Rest) || joint.ParentIndex < -1 ||
                    joint.ParentIndex >= int32_t(joints.size()) || std::abs(double(s.X) - s.Y) > 1e-5 * s.X ||
                    std::abs(double(s.X) - s.Z) > 1e-5 * s.X)
                {
                    return false;
                }
                roots += joint.ParentIndex < 0;
            }
            if (roots != 1)
            {
                return false;
            }
            while (order.size() < joints.size())
            {
                const auto before = order.size();
                for (size_t i = 0; i < joints.size(); ++i)
                {
                    const auto& j = joints[i];
                    if (ready[i] || (j.ParentIndex >= 0 && !ready[size_t(j.ParentIndex)]))
                    {
                        continue;
                    }
                    const auto parentRotation = j.ParentIndex < 0 ? f : rotations[size_t(j.ParentIndex)];
                    const auto parentPosition = j.ParentIndex < 0 ? p : positions[size_t(j.ParentIndex)];
                    const double parentScale = j.ParentIndex < 0 ? scale : scales[size_t(j.ParentIndex)];
                    rotations[i] = D::RetargetMultiply(parentRotation, Matrix(Rotation(j.Rest.Rotation)));
                    positions[i] = Add(parentPosition,
                                       Transform(parentRotation,
                                                 Mul({j.Rest.Translation.X, j.Rest.Translation.Y, j.Rest.Translation.Z},
                                                     parentScale)));
                    scales[i] = parentScale * j.Rest.Scale.X;
                    if (!std::isfinite(scales[i]) || !std::isfinite(positions[i].X) || !std::isfinite(positions[i].Y) ||
                        !std::isfinite(positions[i].Z))
                    {
                        return false;
                    }
                    ready[i] = 1;
                    order.push_back(uint32_t(i));
                }
                if (order.size() == before)
                {
                    return false;
                }
            }
            return true;
        }
        S::SkeletalValue Sample(const S::SkeletalAnimationChannel& c, double time)
        {
            const float sampledTime = float(time);
            if (sampledTime <= c.Samples.front().TimeSeconds)
            {
                return c.Samples.front().Value;
            }
            if (sampledTime >= c.Samples.back().TimeSeconds)
            {
                return c.Samples.back().Value;
            }
            const auto next = std::lower_bound(c.Samples.begin(), c.Samples.end(), sampledTime,
                                               [](const auto& s, float t) { return s.TimeSeconds < t; });
            return D::SampleSkeletalChannelInterval(c, *(next - 1), *next, sampledTime);
        }
        bool AlignDirections(V from, V to, V hint, M& correction)
        {
            const double dot = std::clamp(Dot(from, to), -1.0, 1.0);
            V axis = Cross(from, to);
            if (dot < -1 + 1e-10)
            {
                axis = Sub(hint, Mul(from, Dot(hint, from)));
                if (!Unit(axis))
                {
                    const V fallback = std::abs(from.X) < 0.7 ? V{1, 0, 0} : V{0, 0, 1};
                    axis = Cross(from, fallback);
                    if (!Unit(axis))
                    {
                        return false;
                    }
                }
                correction = MatrixComponents(axis.X, axis.Y, axis.Z, 0);
            }
            else
            {
                correction = MatrixComponents(axis.X, axis.Y, axis.Z, 1 + dot);
            }
            V sourceUp = Sub(hint, Mul(from, Dot(hint, from))), targetUp = Sub(hint, Mul(to, Dot(hint, to)));
            // 片側のhintが骨方向と平行ならtwistを推測せず、最小swingだけを採る。
            if (Unit(sourceUp) && Unit(targetUp))
            {
                V mapped = Transform(correction, sourceUp);
                if (!Unit(mapped))
                {
                    return false;
                }
                const double angle =
                    std::atan2(Dot(to, Cross(mapped, targetUp)), std::clamp(Dot(mapped, targetUp), -1.0, 1.0));
                const double sine = std::sin(angle * 0.5);
                correction = D::RetargetMultiply(
                    MatrixComponents(to.X * sine, to.Y * sine, to.Z * sine, std::cos(angle * 0.5)), correction);
            }
            return true;
        }
        bool Direction(uint32_t joint, C::Span<const SkeletalRetargetJoint> joints, C::Span<const V> positions,
                       C::Span<const uint32_t> mapped, V& out)
        {
            V sum{};
            uint32_t count = 0;
            for (uint32_t child : mapped)
            {
                if (child == joint)
                {
                    continue;
                }
                int32_t parent = joints[child].ParentIndex;
                while (parent >= 0 && uint32_t(parent) != joint)
                {
                    if (std::find(mapped.begin(), mapped.end(), uint32_t(parent)) != mapped.end())
                    {
                        parent = -1;
                        break;
                    }
                    parent = joints[size_t(parent)].ParentIndex;
                }
                if (parent >= 0)
                {
                    V d = Sub(positions[child], positions[joint]);
                    if (Unit(d))
                    {
                        sum = Add(sum, d);
                        ++count;
                    }
                }
            }
            if (!count && joints[joint].ParentIndex >= 0)
            {
                sum = Sub(positions[joint], positions[size_t(joints[joint].ParentIndex)]);
            }
            if (!Unit(sum))
            {
                return false;
            }
            out = sum;
            return true;
        }
        struct Context
        {
            const SkeletalRetargetClipSource& Source;
            SkeletalCoordinateConversion Conversion;
            SkeletalJointMappingSet Mapping;
            C::VariableArray<SkeletalRetargetTargetRotation> Target;
            C::VariableArray<SkeletalRetargetSourceRotation> Current;
            C::VariableArray<M> SourceRest, World, Corrections;
            C::VariableArray<uint32_t> Order;
            C::VariableArray<const S::SkeletalAnimationChannel*> RotationChannels;
            const S::SkeletalAnimationChannel* Translation = nullptr;
            C::VariableArray<SkeletalRetargetRotationWork> Work;
            C::VariableArray<SkeletalRetargetRotationValue> Values;
            M SourceFrame, TargetFrame;
            V SourceFramePosition, SourceRootRest, TargetFramePosition, TargetRootRest;
            double SourceFrameScale = 1, TargetFrameScale = 1, RootScale = 1;
            uint32_t SourceRoot = 0, TargetRoot = 0;
            static bool At(double time, C::Span<S::SkeletalValue> out, S::SkeletalPosition& translation, void* opaque)
            {
                auto& c = *static_cast<Context*>(opaque);
                for (uint32_t i : c.Order)
                {
                    const auto& j = c.Source.Joints[i];
                    const auto q = c.RotationChannels[i] ? Rotation(Sample(*c.RotationChannels[i], time))
                                                         : Rotation(j.Rest.Rotation);
                    c.World[i] = D::RetargetMultiply(j.ParentIndex < 0 ? c.SourceFrame : c.World[size_t(j.ParentIndex)],
                                                     Matrix(q));
                    M converted;
                    if (ConvertSkeletalMatrixBasis(c.Conversion, c.World[i], converted) !=
                        SkeletalCoordinateStatus::Success)
                    {
                        return false;
                    }
                    c.Current[i] = {j.ParentIndex,
                                    D::RetargetMultiply(converted, D::RetargetTranspose(c.SourceRest[i]))};
                }
                SkeletalRetargetRotationRequest request;
                request.Source = c.Current;
                request.Target = c.Target;
                request.Mappings = c.Mapping.Pairs;
                request.Corrections = c.Corrections;
                request.Root = {c.SourceRoot, c.TargetRoot};
                request.SourceReuse = SkeletalSourceReusePolicy::Reject;
                request.Policy = SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations;
                const auto result = EvaluateSkeletalRotationFrame(request, c.Work, c.Values);
                if (!result.Succeeded())
                {
                    return false;
                }
                Q inverseFrame;
                if (!Quaternion(D::RetargetTranspose(c.TargetFrame), inverseFrame))
                {
                    return false;
                }
                for (size_t i = 0; i < c.Values.size(); ++i)
                {
                    const auto& v = c.Values[i];
                    Q q(v.X, v.Y, v.Z, v.W);
                    if (v.TargetIndex == c.TargetRoot)
                    {
                        q = D::NormalizeQuaternion(inverseFrame * q);
                    }
                    out[i] = {q.x, q.y, q.z, q.w};
                }
                const auto& rest = c.Source.Joints[c.SourceRoot].Rest.Translation;
                const auto p =
                    c.Translation ? Sample(*c.Translation, time) : S::SkeletalValue{rest.X, rest.Y, rest.Z, 0};
                const auto world = Transform(c.SourceFrame, Mul({p.X, p.Y, p.Z}, c.SourceFrameScale));
                V delta;
                if (ConvertSkeletalTranslation(c.Conversion, Sub(world, c.SourceRootRest), delta) !=
                    SkeletalCoordinateStatus::Success)
                {
                    return false;
                }
                const auto target = Add(c.TargetRootRest, Mul(delta, c.RootScale));
                const auto local = Mul(Transform(D::RetargetTranspose(c.TargetFrame), target), 1 / c.TargetFrameScale);
                translation = {float(local.X), float(local.Y), float(local.Z)};
                return true;
            }
        };
    } // namespace
    bool MakeBvhRetargetClipSource(const Bvh::BvhDocument& source, const SkeletalBvhClipSettings& settings,
                                   const C::String& name, SkeletalRetargetClipSource& out, C::AnsiString& error)
    {
        Bvh::BvhRotationSourcePlan plan;
        if (!Bvh::PrepareBvhRotationSource(source, {}, plan).Succeeded() || source.FrameCount < 2 ||
            uint64_t(source.FrameCount) * (source.Joints.size() + 1) > 1048576 || name.empty())
        {
            return Fail(error, "bvh_source_limits");
        }
        if ((settings.TimeMode != SkeletalBvhClipTimeMode::HeaderFrameTime &&
             settings.TimeMode != SkeletalBvhClipTimeMode::OverrideFps) ||
            (settings.TimeMode == SkeletalBvhClipTimeMode::HeaderFrameTime && settings.SourceFps != 0))
        {
            return Fail(error, "source_time_mode");
        }
        const double interval = settings.TimeMode == SkeletalBvhClipTimeMode::OverrideFps ? 1 / settings.SourceFps
                                                                                          : source.FrameTimeSeconds;
        if (!std::isfinite(interval) || interval <= 0)
        {
            return Fail(error, "source_time");
        }
        SkeletalRetargetClipSource result;
        result.IntervalSeconds = interval;
        result.Clip.Name = name;
        result.Clip.DurationSeconds = float((source.FrameCount - 1) * interval);
        result.Joints.resize(source.Joints.size());
        result.Clip.Channels.resize(source.Joints.size() + 1);
        for (size_t i = 0; i < source.Joints.size(); ++i)
        {
            const auto& joint = source.Joints[i];
            auto& dst = result.Joints[i];
            dst.Name =
                C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(joint.Name.data()), joint.Name.size()));
            dst.ParentIndex = joint.Parent == UINT32_MAX ? -1 : int32_t(joint.Parent);
            dst.Rest.Translation = {float(joint.Offset.X), float(joint.Offset.Y), float(joint.Offset.Z)};
            auto& channel = result.Clip.Channels[i];
            channel.JointIndex = uint32_t(i);
            channel.Path = S::SkeletalAnimationPath::Rotation;
            channel.Samples.reserve(source.FrameCount);
        }
        auto& rootChannel = result.Clip.Channels.back();
        rootChannel.Path = S::SkeletalAnimationPath::Translation;
        rootChannel.JointIndex = 0;
        rootChannel.Samples.reserve(source.FrameCount);
        for (uint32_t frame = 0; frame < source.FrameCount; ++frame)
        {
            Bvh::BvhPose pose;
            if (!Bvh::EvaluateBvhFrame(source, frame, settings.Translation, pose).Succeeded())
            {
                return Fail(error, "bvh_frame");
            }
            const float time = float(frame * interval);
            for (size_t i = 0; i < source.Joints.size(); ++i)
            {
                Q q;
                if (!Quaternion(pose.Joints[i].Local.Rotation, q))
                {
                    return Fail(error, "bvh_rotation");
                }
                result.Clip.Channels[i].Samples.push_back({time, {q.x, q.y, q.z, q.w}});
            }
            const auto& p = pose.Joints[0].Local.Translation;
            rootChannel.Samples.push_back({time, {float(p.X), float(p.Y), float(p.Z), 0}});
        }
        for (size_t i = 1; i < source.Joints.size(); ++i)
        {
            const auto& joint = source.Joints[i];
            bool hasPosition = false;
            for (size_t n = 0; n < joint.ChannelCount; ++n)
            {
                hasPosition |= uint32_t(source.Channels[joint.ChannelOffset + n]) <= uint32_t(Bvh::Channel::Zposition);
            }
            result.DroppedTranslationChannels += hasPosition;
        }
        out = std::move(result);
        return true;
    }
    bool MakeGltfRetargetClipSource(const S::RigAuthoringCpu& author, size_t clipIndex, SkeletalRetargetClipSource& out,
                                    C::AnsiString& error)
    {
        const auto* data = author.GetData();
        if (!data || clipIndex >= data->Geometry.Clips.size())
        {
            return Fail(error, "gltf_clip_selection");
        }
        SkeletalRetargetClipSource result;
        result.RootFrame = data->RootFrame;
        result.Clip = data->Geometry.Clips[clipIndex];
        result.Joints.resize(data->Geometry.Joints.size());
        result.IntervalSeconds = 1.0 / 30;
        for (size_t i = 0; i < result.Joints.size(); ++i)
        {
            auto& j = result.Joints[i];
            if (!EncodeName(data->Geometry.Joints[i].Name, j.Name))
            {
                return Fail(error, "joint_name");
            }
            j.ParentIndex = data->Geometry.Joints[i].ParentIndex;
            j.Rest = data->LocalRest[i];
        }
        for (const auto& channel : result.Clip.Channels)
        {
            for (size_t i = 1; i < channel.Samples.size(); ++i)
            {
                result.IntervalSeconds = std::min(result.IntervalSeconds, double(channel.Samples[i].TimeSeconds) -
                                                                              channel.Samples[i - 1].TimeSeconds);
            }
        }
        out = std::move(result);
        return true;
    }
    bool RetargetSkeletalClip(const SkeletalRetargetClipSource& source, const S::RigAuthoringCpu& targetAuthor,
                              const SkeletalRoleProfile& profile, const SkeletalClipRetargetSettings& settings,
                              S::SkeletalAnimationClip& out, SkeletalClipRetargetReport& outReport,
                              C::AnsiString& error)
    {
        const auto* author = targetAuthor.GetData();
        if (!author || uint32_t(settings.RestMode) > 2 || !std::isfinite(settings.RootScale) ||
            settings.RootScale <= 0 || !std::isfinite(settings.MaximumRestErrorRadians) ||
            settings.MaximumRestErrorRadians < 0 || !std::isfinite(source.Clip.DurationSeconds) ||
            source.Clip.DurationSeconds <= 0 ||
            (settings.LoopExcludedRoles & ~((uint32_t{1} << SkeletalRoleCount) - 1)) ||
            source.Clip.Channels.size() > 32768 || !source.Clip.RootMotion.empty() ||
            source.DroppedTranslationChannels > source.Joints.size() || !std::isfinite(settings.UpHint.X) ||
            !std::isfinite(settings.UpHint.Y) || !std::isfinite(settings.UpHint.Z) ||
            std::hypot(settings.UpHint.X, settings.UpHint.Y, settings.UpHint.Z) < 1e-10)
        {
            return Fail(error, "retarget_input");
        }
        Context c{source};
        SkeletalClipRetargetReport report;
        report.IgnoredTranslationChannels = source.DroppedTranslationChannels;
        C::VariableArray<SkeletalRetargetJoint> target(author->Geometry.Joints.size());
        C::VariableArray<uint32_t> targetOrder;
        C::VariableArray<M> targetRotations;
        C::VariableArray<V> sourcePositions, targetPositions;
        for (size_t i = 0; i < target.size(); ++i)
        {
            if (!EncodeName(author->Geometry.Joints[i].Name, target[i].Name))
            {
                return Fail(error, "target_name");
            }
            target[i].ParentIndex = author->Geometry.Joints[i].ParentIndex;
            target[i].Rest = author->LocalRest[i];
        }
        if (!RestWorld(source.Joints, source.RootFrame, c.Order, c.SourceRest, sourcePositions) ||
            !RestWorld(target, author->RootFrame, targetOrder, targetRotations, targetPositions) ||
            BuildSkeletalCoordinateConversion(profile.Settings.Up, profile.Settings.Forward,
                                              profile.Settings.Handedness, profile.Settings.PositionScale,
                                              c.Conversion) != SkeletalCoordinateStatus::Success)
        {
            return Fail(error, "rest_or_axes");
        }
        for (size_t i = 0; i < c.SourceRest.size(); ++i)
        {
            M r;
            V p;
            if (ConvertSkeletalMatrixBasis(c.Conversion, c.SourceRest[i], r) != SkeletalCoordinateStatus::Success ||
                ConvertSkeletalTranslation(c.Conversion, sourcePositions[i], p) != SkeletalCoordinateStatus::Success)
            {
                return Fail(error, "source_axes");
            }
            c.SourceRest[i] = r;
            sourcePositions[i] = p;
        }
        C::VariableArray<SkeletalJointNameView> sourceNames, targetNames;
        for (const auto& j : source.Joints)
        {
            sourceNames.push_back({Bytes(j.Name)});
        }
        for (const auto& j : target)
        {
            targetNames.push_back({Bytes(j.Name)});
        }
        SkeletalJointIndex si, ti;
        if (!BuildSkeletalJointIndex(sourceNames, {}, si).Succeeded() ||
            !BuildSkeletalJointIndex(targetNames, {}, ti).Succeeded())
        {
            return Fail(error, "joint_index");
        }
        C::VariableArray<SkeletalJointMappingNameView> names;
        C::VariableArray<M> explicitCorrections;
        C::VariableArray<uint32_t> entryRoles;
        for (size_t role = 0; role < SkeletalRoleCount; ++role)
        {
            for (const auto& name : profile.Source[role])
            {
                uint32_t index;
                if (!FindSkeletalJointIndex(si, name, index).Succeeded())
                {
                    return Fail(error, "source_role_missing");
                }
            }
            if (profile.Target[role].size() > profile.Source[role].size())
            {
                return Fail(error, "role_chain_length");
            }
            for (size_t ordinal = 0; ordinal < profile.Target[role].size(); ++ordinal)
            {
                names.push_back({profile.Source[role][ordinal], profile.Target[role][ordinal].Name});
                explicitCorrections.push_back(profile.Target[role][ordinal].Correction);
                entryRoles.push_back(uint32_t(role));
            }
        }
        if (names.empty() || profile.Target[0].size() != 1 || profile.Source[0].size() != 1 ||
            !FindSkeletalJointIndex(si, names[0].SourceName, c.SourceRoot).Succeeded() ||
            !FindSkeletalJointIndex(ti, names[0].TargetName, c.TargetRoot).Succeeded() ||
            source.Joints[c.SourceRoot].ParentIndex != -1 || target[c.TargetRoot].ParentIndex != -1 ||
            !ResolveSkeletalJointMappings(si, ti, names, {c.SourceRoot, c.TargetRoot},
                                          SkeletalSourceReusePolicy::Reject, {}, c.Mapping)
                 .Succeeded())
        {
            return Fail(error, "role_mapping");
        }
        C::VariableArray<uint32_t> sourceMapped, targetMapped;
        for (const auto& pair : c.Mapping.Pairs)
        {
            sourceMapped.push_back(pair.SourceIndex);
            targetMapped.push_back(pair.TargetIndex);
        }
        c.Corrections.resize(c.Mapping.Pairs.size());
        for (size_t i = 0; i < c.Mapping.Pairs.size(); ++i)
        {
            const auto& pair = c.Mapping.Pairs[i];
            SkeletalRestCorrectionReport r;
            r.SourceJoint = pair.SourceIndex;
            r.TargetJoint = pair.TargetIndex;
            V a, b;
            r.bDirectionMissing = !Direction(pair.SourceIndex, source.Joints, sourcePositions, sourceMapped, a) ||
                                  !Direction(pair.TargetIndex, target, targetPositions, targetMapped, b);
            M correction = explicitCorrections[pair.InputEntryIndex];
            if (settings.RestMode != SkeletalRestCorrectionMode::Explicit)
            {
                if (r.bDirectionMissing)
                {
                    return Fail(error, "rest_bone_direction_missing");
                }
                correction = {};
                if (settings.RestMode == SkeletalRestCorrectionMode::AlignBones)
                {
                    if (!AlignDirections(a, b, settings.UpHint, correction))
                    {
                        return Fail(error, "rest_up_hint");
                    }
                }
            }
            if (!r.bDirectionMissing)
            {
                r.BeforeRadians = std::acos(std::clamp(Dot(a, b), -1.0, 1.0));
                r.AfterRadians = std::acos(std::clamp(Dot(Transform(correction, a), b), -1.0, 1.0));
                if (settings.RestMode == SkeletalRestCorrectionMode::Match &&
                    r.AfterRadians > settings.MaximumRestErrorRadians)
                {
                    return Fail(error, "rest_direction_mismatch");
                }
            }
            r.Correction = correction;
            report.Corrections.push_back(r);
            c.Corrections[i] = correction;
        }
        c.Target.resize(target.size());
        for (size_t i = 0; i < target.size(); ++i)
        {
            c.Target[i] = {target[i].ParentIndex,
                           target[i].ParentIndex < 0 ? targetRotations[i] : Matrix(Rotation(target[i].Rest.Rotation)),
                           targetRotations[i]};
        }
        if (!Frame(source.RootFrame, c.SourceFrame, c.SourceFramePosition, c.SourceFrameScale) ||
            !Frame(author->RootFrame, c.TargetFrame, c.TargetFramePosition, c.TargetFrameScale))
        {
            return Fail(error, "root_frame");
        }
        const auto& sr = source.Joints[c.SourceRoot].Rest.Translation;
        c.SourceRootRest = Transform(c.SourceFrame, Mul({sr.X, sr.Y, sr.Z}, c.SourceFrameScale));
        c.TargetRootRest = targetPositions[c.TargetRoot];
        c.RootScale = settings.RootScale;
        if (settings.bAutoRootHeight)
        {
            uint32_t sourcePelvis = c.SourceRoot, targetPelvis = c.TargetRoot;
            const auto pelvis = size_t(SkeletalRole::Pelvis);
            if (profile.Source[pelvis].size() == 1 &&
                !FindSkeletalJointIndex(si, profile.Source[pelvis][0], sourcePelvis).Succeeded())
            {
                return Fail(error, "source_pelvis");
            }
            if (profile.Target[pelvis].size() == 1 &&
                !FindSkeletalJointIndex(ti, profile.Target[pelvis][0].Name, targetPelvis).Succeeded())
            {
                return Fail(error, "target_pelvis");
            }
            double sourceGround = 0, targetGround = 0;
            bool sourceFoot = false, targetFoot = false;
            for (auto role :
                 {SkeletalRole::FrontLPaw, SkeletalRole::FrontRPaw, SkeletalRole::HindLPaw, SkeletalRole::HindRPaw})
            {
                for (const auto& name : profile.Source[size_t(role)])
                {
                    uint32_t index;
                    if (!FindSkeletalJointIndex(si, name, index).Succeeded())
                    {
                        return Fail(error, "source_foot");
                    }
                    sourceGround =
                        sourceFoot ? std::min(sourceGround, sourcePositions[index].Y) : sourcePositions[index].Y;
                    sourceFoot = true;
                }
                for (const auto& joint : profile.Target[size_t(role)])
                {
                    uint32_t index;
                    if (!FindSkeletalJointIndex(ti, joint.Name, index).Succeeded())
                    {
                        return Fail(error, "target_foot");
                    }
                    targetGround =
                        targetFoot ? std::min(targetGround, targetPositions[index].Y) : targetPositions[index].Y;
                    targetFoot = true;
                }
            }
            const double sourceHeight = sourcePositions[sourcePelvis].Y - sourceGround,
                         targetHeight = targetPositions[targetPelvis].Y - targetGround;
            if (!sourceFoot || !targetFoot || sourceHeight < 1e-8 || targetHeight < 1e-8)
            {
                return Fail(error, "root_height_requires_pelvis_and_paw");
            }
            c.RootScale *= targetHeight / sourceHeight;
        }
        c.RotationChannels.resize(source.Joints.size(), nullptr);
        C::VariableArray<uint8_t> used(source.Joints.size() * 3, 0);
        uint64_t samples = 0;
        for (const auto& channel : source.Clip.Channels)
        {
            const auto path = uint32_t(channel.Path);
            if (channel.JointIndex >= source.Joints.size() || path > 2 || uint32_t(channel.Interpolation) > 1 ||
                channel.Samples.empty() || used[channel.JointIndex * 3 + path]++ ||
                channel.Samples.size() > 1048576 - samples)
            {
                return Fail(error, "source_channel");
            }
            samples += channel.Samples.size();
            float previous = -1;
            for (const auto& key : channel.Samples)
            {
                const auto& v = key.Value;
                if (!std::isfinite(key.TimeSeconds) || key.TimeSeconds < 0 || key.TimeSeconds <= previous ||
                    key.TimeSeconds > source.Clip.DurationSeconds || !std::isfinite(v.X) || !std::isfinite(v.Y) ||
                    !std::isfinite(v.Z) || !std::isfinite(v.W) || (path == 1 && !S::IsRepresentableSkeletalRotation(v)))
                {
                    return Fail(error, "source_key");
                }
                previous = key.TimeSeconds;
                const auto& rest = source.Joints[channel.JointIndex].Rest.Scale;
                if (path == 2 && (v.X != rest.X || v.Y != rest.Y || v.Z != rest.Z))
                {
                    return Fail(error, "animated_scale_unsupported");
                }
            }
            if (path == 1)
            {
                c.RotationChannels[channel.JointIndex] = &channel;
            }
            if (path == 0)
            {
                if (channel.JointIndex == c.SourceRoot)
                {
                    c.Translation = &channel;
                }
                else
                {
                    ++report.IgnoredTranslationChannels;
                }
            }
        }
        c.World.resize(source.Joints.size());
        c.Current.resize(source.Joints.size());
        c.Work.resize(target.size());
        c.Values.resize(c.Mapping.Pairs.size());
        C::VariableArray<double> weights;
        for (size_t i = 0; i < targetMapped.size(); ++i)
        {
            const auto joint = targetMapped[i];
            double length = 0;
            for (size_t child = 0; child < target.size(); ++child)
            {
                int32_t parent = target[child].ParentIndex;
                while (parent >= 0 && uint32_t(parent) != joint)
                {
                    parent = target[size_t(parent)].ParentIndex;
                }
                if (parent >= 0)
                {
                    const auto d = Sub(targetPositions[child], targetPositions[joint]);
                    length = std::max(length, std::hypot(d.X, d.Y, d.Z));
                }
            }
            if (length < 1e-8 && target[joint].ParentIndex >= 0)
            {
                const auto d = Sub(targetPositions[joint], targetPositions[size_t(target[joint].ParentIndex)]);
                length = std::hypot(d.X, d.Y, d.Z);
            }
            const auto role = entryRoles[c.Mapping.Pairs[i].InputEntryIndex];
            weights.push_back((settings.LoopExcludedRoles & (uint32_t{1} << role)) ? 0 : std::max(length, 1e-6));
        }
        SkeletalClipPoseSource input;
        input.RotationWeights = weights;
        input.Name = source.Clip.Name;
        input.DurationSeconds = source.Clip.DurationSeconds;
        input.SourceIntervalSeconds = source.IntervalSeconds;
        input.JointIndices = targetMapped;
        input.RootJoint = c.TargetRoot;
        input.RootRest = target[c.TargetRoot].Rest;
        input.RootFrame = author->RootFrame;
        input.Context = &c;
        input.Sample = Context::At;
        S::SkeletalAnimationClip clip;
        if (ProcessSkeletalClip(input, settings.Processing, clip, report.Processing) !=
            SkeletalClipProcessingStatus::Success)
        {
            return Fail(error, "clip_processing");
        }
        ClipMetadataReport metadataReport;
        if (!RemapClipMetadataTime(source.Clip.Metadata, source.Clip.DurationSeconds, report.Processing.StartSeconds,
                                   report.Processing.EndSeconds, 1, clip.Metadata, metadataReport))
            return Fail(error, "clip_metadata_time");
        if (clip.Metadata.Root.Joint != UINT32_MAX)
        {
            bool mapped = false;
            for (const auto& pair : c.Mapping.Pairs)
                if (pair.SourceIndex == clip.Metadata.Root.Joint)
                {
                    clip.Metadata.Root.Joint = pair.TargetIndex;
                    mapped = true;
                    break;
                }
            if (!mapped)
                return Fail(error, "clip_metadata_root");
        }
        const double metadataScale = profile.Settings.PositionScale * c.RootScale;
        if (!AssetImport::TryScaleImportValue(clip.Metadata.GroundOffset, metadataScale, clip.Metadata.GroundOffset) ||
            (clip.Metadata.Root.NominalSpeed >= 0 &&
             !AssetImport::TryScaleImportValue(clip.Metadata.Root.NominalSpeed, metadataScale,
                                               clip.Metadata.Root.NominalSpeed)))
            return Fail(error, "clip_metadata_scale");
        if (!ValidateClipMetadata(clip.Metadata, clip.DurationSeconds, metadataReport))
            return Fail(error, "clip_metadata_scale");
        report.RootScale = c.RootScale;
        report.UnmappedSource = uint32_t(c.Mapping.UnmappedSourceIndices.size());
        report.UnmappedTarget = uint32_t(c.Mapping.UnmappedTargetIndices.size());
        out = std::move(clip);
        outReport = std::move(report);
        return true;
    }
} // namespace NorvesLib::Core::Animation
