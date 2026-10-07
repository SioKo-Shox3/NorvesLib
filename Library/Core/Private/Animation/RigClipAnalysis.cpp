#include "Animation/RigClipAnalysis.h"
#include "Animation/SkeletalJointGlobalRowMath.h"
#include "Animation/SkeletalClipSampling.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Skeletal
{
    namespace D = Animation::Detail;
    namespace C = Container;
    namespace
    {
        SkeletalValue Sample(const SkeletalAnimationChannel& channel, float time)
        {
            if (time <= channel.Samples.front().TimeSeconds)
            {
                return channel.Samples.front().Value;
            }
            if (time >= channel.Samples.back().TimeSeconds)
            {
                return channel.Samples.back().Value;
            }
            const auto next = std::lower_bound(channel.Samples.begin(), channel.Samples.end(), time,
                                               [](const auto& sample, float t) { return sample.TimeSeconds < t; });
            return D::SampleSkeletalChannelInterval(channel, *(next - 1), *next, time);
        }
        double RotationDistance(const SkeletalValue& a, const SkeletalValue& b)
        {
            const double aa = double(a.X) * a.X + double(a.Y) * a.Y + double(a.Z) * a.Z + double(a.W) * a.W;
            const double bb = double(b.X) * b.X + double(b.Y) * b.Y + double(b.Z) * b.Z + double(b.W) * b.W;
            const double dot = double(a.X) * b.X + double(a.Y) * b.Y + double(a.Z) * b.Z + double(a.W) * b.W;
            return 2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0.0, 1.0));
        }
        struct Evaluator
        {
            const RigAuthoringData& Author;
            const SkeletalAnimationClip& Clip;
            C::VariableArray<SkeletalRestTransform> Pose;
            C::VariableArray<Math::Matrix4x4> Local, Global;
            C::VariableArray<uint8_t> State;
            Evaluator(const RigAuthoringData& a, const SkeletalAnimationClip& c)
                : Author(a), Clip(c), Pose(a.LocalRest), Local(a.LocalRest.size()), Global(a.LocalRest.size()),
                  State(a.LocalRest.size(), 0)
            {
            }
            bool At(float time, bool bAnimate)
            {
                Pose = Author.LocalRest;
                if (bAnimate)
                {
                    for (const auto& c : Clip.Channels)
                    {
                        const auto v = Sample(c, time);
                        auto& p = Pose[c.JointIndex];
                        if (c.Path == SkeletalAnimationPath::Translation)
                        {
                            p.Translation = {v.X, v.Y, v.Z};
                        }
                        else if (c.Path == SkeletalAnimationPath::Rotation)
                        {
                            p.Rotation = v;
                        }
                        else
                        {
                            p.Scale = {v.X, v.Y, v.Z};
                        }
                    }
                }
                std::fill(State.begin(), State.end(), 0);
                for (size_t i = 0; i < Pose.size(); ++i)
                {
                    const auto& p = Pose[i];
                    if (!IsValidSkeletalRestTransform(p))
                    {
                        return false;
                    }
                    D::JointTransform t;
                    t.Translation = {p.Translation.X, p.Translation.Y, p.Translation.Z};
                    t.Rotation = D::SkeletalRotationFromColumn(p.Rotation.X, p.Rotation.Y, p.Rotation.Z, p.Rotation.W);
                    t.Scale = {p.Scale.X, p.Scale.Y, p.Scale.Z};
                    Local[i] = D::ComposeSkeletalLocalRowTransform(t);
                }
                const auto parent = [&](uint32_t i) noexcept -> int32_t
                { return Author.Geometry.Joints[i].ParentIndex; };
                for (uint32_t i = 0; i < Pose.size(); ++i)
                {
                    if (!D::BuildJointGlobalRow(i, parent, Local, Global, State))
                    {
                        return false;
                    }
                }
                const auto& f = Author.RootFrame;
                const Math::Matrix4x4 frame(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11],
                                            f[12], f[13], f[14], f[15]);
                for (auto& global : Global)
                {
                    global = global * frame;
                    if (!D::IsFiniteMatrix(global))
                    {
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace
    bool AnalyzeRigClip(const RigAuthoringCpu& author, const SkeletalAnimationClip& source,
                        const RigClipAnalysisOptions& options, SkeletalAnimationClip& out, RigClipAnalysis& analysis)
    {
        try
        {
            const auto* a = author.GetData();
            if (!a || a->LocalRest.empty() || a->Geometry.Joints.size() != a->LocalRest.size() ||
                uint32_t(options.Loop) > uint32_t(RigClipLoopMode::Once) || !std::isfinite(options.LoopThreshold) ||
                options.LoopThreshold < 0 || !std::isfinite(options.ReferenceLengthMeters) ||
                options.ReferenceLengthMeters < 0 || !std::isfinite(options.SampleRate) || options.SampleRate <= 0 ||
                options.MaximumSamples < 2 || options.MaximumSamples > 65536 || !std::isfinite(options.TimeScale) ||
                options.TimeScale <= 0 || !std::isfinite(options.AuthoredFps) || options.AuthoredFps < 0 ||
                !std::isfinite(options.SourceFps) || options.SourceFps < 0 ||
                ((options.AuthoredFps == 0) != (options.SourceFps == 0)) || !std::isfinite(source.DurationSeconds) ||
                source.DurationSeconds < 0 || source.Channels.empty() || source.Channels.size() > 32768)
            {
                return false;
            }
            const double factor =
                options.TimeScale * (options.SourceFps > 0 ? options.AuthoredFps / options.SourceFps : 1);
            const double duration = double(source.DurationSeconds) * factor;
            if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(duration) ||
                duration > std::numeric_limits<float>::max())
            {
                return false;
            }
            uint64_t sourceSamples = 0;
            for (const auto& c : source.Channels)
            {
                sourceSamples += c.Samples.size();
                if (sourceSamples > 1048576)
                {
                    return false;
                }
            }
            auto clip = source;
            clip.DurationSeconds = float(duration);
            if (duration > 0 && clip.DurationSeconds == 0)
            {
                return false;
            }
            C::VariableArray<uint8_t> used(a->LocalRest.size() * 3, 0);
            uint64_t samples = 0;
            for (auto& c : clip.Channels)
            {
                if (c.JointIndex >= a->LocalRest.size() || uint32_t(c.Path) > 2 || uint32_t(c.Interpolation) > 1 ||
                    c.Samples.empty() || used[c.JointIndex * 3 + uint32_t(c.Path)]++)
                {
                    return false;
                }
                samples += c.Samples.size();
                if (samples > 1048576)
                {
                    return false;
                }
                float previous = -1;
                for (auto& s : c.Samples)
                {
                    const auto& v = s.Value;
                    if (!std::isfinite(s.TimeSeconds) || s.TimeSeconds < 0 || s.TimeSeconds > source.DurationSeconds ||
                        !std::isfinite(v.X) || !std::isfinite(v.Y) || !std::isfinite(v.Z) || !std::isfinite(v.W) ||
                        (c.Path == SkeletalAnimationPath::Rotation && !IsRepresentableSkeletalRotation(v)) ||
                        (c.Path == SkeletalAnimationPath::Scale && (v.X <= 0 || v.Y <= 0 || v.Z <= 0)))
                    {
                        return false;
                    }
                    const double time = double(s.TimeSeconds) * factor;
                    s.TimeSeconds = float(time);
                    if (!std::isfinite(s.TimeSeconds) || s.TimeSeconds <= previous || (time > 0 && s.TimeSeconds == 0))
                    {
                        return false;
                    }
                    previous = s.TimeSeconds;
                }
            }
            RigClipAnalysis result;
            result.DurationSeconds = clip.DurationSeconds;
            result.SourceFps = options.SourceFps > 0 ? options.SourceFps / options.TimeScale : 0;
            result.RootJoint = options.RootJoint;
            if (result.RootJoint == UINT32_MAX)
            {
                for (uint32_t i = 0; i < a->Geometry.Joints.size(); ++i)
                {
                    if (a->Geometry.Joints[i].ParentIndex < 0)
                    {
                        result.RootJoint = i;
                        break;
                    }
                }
                for (uint32_t i = 0; i < a->Geometry.Joints.size(); ++i)
                {
                    if (a->Geometry.Joints[i].ParentIndex == int32_t(result.RootJoint))
                    {
                        result.RootJoint = i;
                        break;
                    }
                }
            }
            if (result.RootJoint >= a->LocalRest.size())
            {
                return false;
            }
            Evaluator eval(*a, clip);
            if (!eval.At(0, false))
            {
                return false;
            }
            double reference = options.ReferenceLengthMeters;
            if (reference == 0)
            {
                double min[3], max[3];
                for (size_t k = 0; k < 3; ++k)
                {
                    min[k] = max[k] = eval.Global[0].values[12 + k];
                }
                for (const auto& m : eval.Global)
                {
                    for (size_t k = 0; k < 3; ++k)
                    {
                        min[k] = std::min(min[k], double(m.values[12 + k]));
                        max[k] = std::max(max[k], double(m.values[12 + k]));
                    }
                }
                reference = std::hypot(max[0] - min[0], max[1] - min[1], max[2] - min[2]);
                if (reference <= 1e-8)
                {
                    reference = 1;
                }
            }
            if (!eval.At(0, true))
            {
                return false;
            }
            const auto start = eval.Pose;
            const auto startGlobal = eval.Global;
            const auto startMatrix = eval.Global[result.RootJoint];
            if (!eval.At(clip.DurationSeconds, true))
            {
                return false;
            }
            const auto endMatrix = eval.Global[result.RootJoint];
            for (size_t i = 0; i < start.size(); ++i)
            {
                const auto& x = start[i];
                const auto& y = eval.Pose[i];
                const double error = RotationDistance(x.Rotation, y.Rotation) +
                                     std::hypot(double(startGlobal[i].values[12]) - eval.Global[i].values[12],
                                                double(startGlobal[i].values[13]) - eval.Global[i].values[13],
                                                double(startGlobal[i].values[14]) - eval.Global[i].values[14]) /
                                         reference +
                                     std::max({std::abs(std::log(double(x.Scale.X) / y.Scale.X)),
                                               std::abs(std::log(double(x.Scale.Y) / y.Scale.Y)),
                                               std::abs(std::log(double(x.Scale.Z) / y.Scale.Z))});
                result.LoopError = std::max(result.LoopError, error);
            }
            result.bLoopCandidate = clip.DurationSeconds > 0 && result.LoopError <= options.LoopThreshold;
            result.bLoop = options.Loop == RigClipLoopMode::Loop ||
                           (options.Loop == RigClipLoopMode::Auto && result.bLoopCandidate);
            result.TranslationX = double(endMatrix.values[12]) - startMatrix.values[12];
            result.TranslationZ = double(endMatrix.values[14]) - startMatrix.values[14];
            const double steps = std::max(1.0, std::ceil(double(clip.DurationSeconds) * options.SampleRate));
            if (!std::isfinite(steps) || steps + 1 > options.MaximumSamples)
            {
                return false;
            }
            if (std::hypot(double(startMatrix.values[8]), double(startMatrix.values[10])) <= 1e-8)
            {
                return false;
            }
            auto previousMatrix = startMatrix;
            double previousYaw = std::atan2(double(startMatrix.values[8]), double(startMatrix.values[10]));
            constexpr double Pi = 3.14159265358979323846;
            for (uint32_t i = 1; i <= uint32_t(steps); ++i)
            {
                if (!eval.At(float(double(clip.DurationSeconds) * i / steps), true))
                {
                    return false;
                }
                const auto& m = eval.Global[result.RootJoint];
                if (std::hypot(double(m.values[8]), double(m.values[10])) <= 1e-8)
                {
                    return false;
                }
                result.PlanarDistanceMeters += std::hypot(double(m.values[12]) - previousMatrix.values[12],
                                                          double(m.values[14]) - previousMatrix.values[14]);
                const double yaw = std::atan2(double(m.values[8]), double(m.values[10]));
                result.TotalYawRadians += std::remainder(yaw - previousYaw, 2 * Pi);
                previousYaw = yaw;
                previousMatrix = m;
            }
            result.AverageSpeedMetersPerSecond =
                clip.DurationSeconds > 0 ? result.PlanarDistanceMeters / clip.DurationSeconds : 0;
            if (!std::isfinite(result.LoopError) || !std::isfinite(result.SourceFps) ||
                !std::isfinite(result.PlanarDistanceMeters) || !std::isfinite(result.AverageSpeedMetersPerSecond) ||
                !std::isfinite(result.TotalYawRadians))
            {
                return false;
            }
            out = std::move(clip);
            analysis = result;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace NorvesLib::Core::Skeletal
