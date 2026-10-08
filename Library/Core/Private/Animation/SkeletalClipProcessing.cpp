#include "Animation/SkeletalClipProcessing.h"
#include "Animation/SkeletalSamplingMath.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include "Animation/RigRootFrame.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Animation
{
    namespace S = Skeletal;
    namespace C = Container;
    using Status = SkeletalClipProcessingStatus;
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        using Q = Math::Quaternion;
        Q Quaternion(const S::SkeletalValue& v)
        {
            return Detail::NormalizeQuaternion(Q(v.X, v.Y, v.Z, v.W));
        }
        S::SkeletalValue Value(const Q& q)
        {
            return {q.x, q.y, q.z, q.w};
        }
        Q Inverse(const Q& q)
        {
            return Q(-q.x, -q.y, -q.z, q.w);
        }
        Q Yaw(double angle)
        {
            return Q(0, float(std::sin(angle * 0.5)), 0, float(std::cos(angle * 0.5)));
        }
        double Distance(const Q& a, const Q& b)
        {
            const double dot = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
            const double aa = double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z + double(a.w) * a.w;
            const double bb = double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z + double(b.w) * b.w;
            return 2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0.0, 1.0));
        }
        bool Heading(const Q& q, double& yaw)
        {
            // world +Yのtwist。前方投影が垂直になるpitch 90度でも定義できる。
            const double norm = std::hypot(double(q.y), double(q.w));
            if (norm < 1e-12)
            {
                return false;
            }
            yaw = 2 * std::atan2(double(q.y) / norm, double(q.w) / norm);
            return true;
        }
        Bvh::Vector3d Transform(const Bvh::Matrix3d& m, Bvh::Vector3d v)
        {
            return {m.Values[0] * v.X + m.Values[1] * v.Y + m.Values[2] * v.Z,
                    m.Values[3] * v.X + m.Values[4] * v.Y + m.Values[5] * v.Z,
                    m.Values[6] * v.X + m.Values[7] * v.Y + m.Values[8] * v.Z};
        }
        struct Pose
        {
            C::VariableArray<S::SkeletalValue> Rotations;
            S::SkeletalPosition Translation;
            double WorldX = 0, WorldZ = 0, Heading = 0;
            explicit Pose(size_t size) : Rotations(size)
            {
            }
        };
        struct CleanupSamples
        {
            C::VariableArray<Pose> Frames;
            double Interval = 0;
            static bool Sample(double time, C::Span<S::SkeletalValue> rotations, S::SkeletalPosition& position,
                               void* context)
            {
                const auto& cache = *static_cast<CleanupSamples*>(context);
                const double index = std::clamp(time / cache.Interval, 0.0, double(cache.Frames.size() - 1));
                const size_t left = size_t(index), right = std::min(left + 1, cache.Frames.size() - 1);
                const float alpha = float(index - left);
                for (size_t j = 0; j < rotations.size(); ++j)
                    rotations[j] = Value(Detail::Slerp(Quaternion(cache.Frames[left].Rotations[j]),
                                                       Quaternion(cache.Frames[right].Rotations[j]), alpha));
                const auto& a = cache.Frames[left].Translation;
                const auto& z = cache.Frames[right].Translation;
                position = {a.X + (z.X - a.X) * alpha, a.Y + (z.Y - a.Y) * alpha, a.Z + (z.Z - a.Z) * alpha};
                return true;
            }
        };
        double Median(C::VariableArray<double> values)
        {
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        }
        void SmoothChannels(S::SkeletalAnimationClip& clip, uint32_t radius, bool loop, uint32_t& count)
        {
            if (!radius)
                return;
            for (auto& channel : clip.Channels)
            {
                const auto original = channel.Samples;
                const size_t samples = original.size() - (loop ? 1 : 0);
                if (samples < 2)
                    continue;
                for (size_t i = 0; i < samples; ++i)
                {
                    double x = 0, y = 0, z = 0, w = 0, total = 0;
                    const auto anchor = Quaternion(original[i].Value);
                    for (int k = -int(radius); k <= int(radius); ++k)
                    {
                        int64_t index = int64_t(i) + k;
                        index = loop ? (index % int64_t(samples) + samples) % samples
                                     : std::clamp<int64_t>(index, 0, samples - 1);
                        const double weight = radius + 1 - std::abs(k);
                        auto value = original[size_t(index)].Value;
                        if (channel.Path == S::SkeletalAnimationPath::Rotation)
                        {
                            const auto q = Quaternion(value);
                            const double sign =
                                anchor.x * q.x + anchor.y * q.y + anchor.z * q.z + anchor.w * q.w < 0 ? -1 : 1;
                            value = Value(q);
                            value.X *= sign;
                            value.Y *= sign;
                            value.Z *= sign;
                            value.W *= sign;
                        }
                        x += value.X * weight;
                        y += value.Y * weight;
                        z += value.Z * weight;
                        w += value.W * weight;
                        total += weight;
                    }
                    channel.Samples[i].Value =
                        channel.Path == S::SkeletalAnimationPath::Rotation
                            ? Value(Detail::NormalizeQuaternion(
                                  Q(float(x / total), float(y / total), float(z / total), float(w / total))))
                            : S::SkeletalValue{float(x / total), float(y / total), float(z / total), float(w / total)};
                }
                if (loop)
                    channel.Samples.back().Value = channel.Samples.front().Value;
                ++count;
            }
        }
        struct Evaluator
        {
            const SkeletalClipPoseSource& Source;
            const SkeletalClipProcessingSettings& Settings;
            uint32_t Count = 0;
            size_t RootSlot = 0;
            Q Frame;
            double Scale = 1, BindYaw = 0;
            Bvh::Vector3d BindWorld;
            Bvh::Matrix3d FrameMatrix;
            Status Failure = Status::Success;
            bool Initialize()
            {
                const auto& f = Source.RootFrame;
                Scale = std::sqrt(double(f[0]) * f[0] + double(f[1]) * f[1] + double(f[2]) * f[2]);
                Bvh::Matrix3d column;
                for (size_t r = 0; r < 3; ++r)
                {
                    for (size_t c = 0; c < 3; ++c)
                    {
                        column.Values[r * 3 + c] = f[c * 4 + r] / Scale;
                    }
                }
                Detail::RetargetQuaterniond q;
                if (!Detail::RetargetExtractQuaternion(column, q))
                {
                    return false;
                }
                Frame = Detail::NormalizeQuaternion(Q(float(q.X), float(q.Y), float(q.Z), float(q.W)));
                FrameMatrix = column;
                const auto& t = Source.RootRest.Translation;
                // 定数frame原点は差分と逆変換で消える。巨大な原点へ足して小さい動きを丸め落とさない。
                BindWorld = Transform(FrameMatrix, {t.X * Scale, t.Y * Scale, t.Z * Scale});
                for (size_t i = 0; i < Source.JointIndices.size(); ++i)
                {
                    if (Source.JointIndices[i] == Source.RootJoint)
                    {
                        RootSlot = i;
                    }
                }
                return !Settings.bExtractRootMotion || Heading(Frame * Quaternion(Source.RootRest.Rotation), BindYaw);
            }
            bool At(double time, Pose& pose)
            {
                if (Count >= Settings.MaximumPoseEvaluations)
                {
                    Failure = Status::LimitExceeded;
                    return false;
                }
                ++Count;
                if (!Source.Sample(time, pose.Rotations, pose.Translation, Source.Context))
                {
                    Failure = Status::SourceRejected;
                    return false;
                }
                for (auto& value : pose.Rotations)
                {
                    if (!S::IsRepresentableSkeletalRotation(value))
                    {
                        Failure = Status::InvalidPose;
                        return false;
                    }
                    value = Value(Quaternion(value));
                }
                const auto& t = pose.Translation;
                if (!std::isfinite(t.X) || !std::isfinite(t.Y) || !std::isfinite(t.Z))
                {
                    Failure = Status::InvalidPose;
                    return false;
                }
                const auto& f = Source.RootFrame;
                auto world = Transform(FrameMatrix, {t.X * Scale, t.Y * Scale, t.Z * Scale});
                if (!std::isfinite(world.X) || !std::isfinite(world.Y) || !std::isfinite(world.Z))
                {
                    Failure = Status::InvalidPose;
                    return false;
                }
                pose.WorldX = world.X;
                pose.WorldZ = world.Z;
                const auto rootWorld = Frame * Quaternion(pose.Rotations[RootSlot]);
                if (Settings.bExtractRootMotion && !Heading(rootWorld, pose.Heading))
                {
                    Failure = Status::DegenerateHeading;
                    return false;
                }
                if (Settings.bExtractRootMotion)
                {
                    pose.Rotations[RootSlot] =
                        Value(Detail::NormalizeQuaternion(Inverse(Frame) * Yaw(BindYaw - pose.Heading) * rootWorld));
                    world.X = BindWorld.X;
                    world.Z = BindWorld.Z;
                    const auto local = Transform(Detail::RetargetTranspose(FrameMatrix), world);
                    pose.Translation = {float(local.X / Scale), float(local.Y / Scale), float(local.Z / Scale)};
                    if (!std::isfinite(pose.Translation.X) || !std::isfinite(pose.Translation.Y) ||
                        !std::isfinite(pose.Translation.Z))
                    {
                        Failure = Status::InvalidPose;
                        return false;
                    }
                }
                return true;
            }
            double Error(const Pose& a, const Pose& b)
            {
                double total = 0, weight = 0;
                for (size_t i = 0; i < a.Rotations.size(); ++i)
                {
                    const double w = Source.RotationWeights.empty() ? 1 : Source.RotationWeights[i];
                    const double d = Distance(Quaternion(a.Rotations[i]), Quaternion(b.Rotations[i]));
                    total += w * d * d;
                    weight += w;
                }
                return weight > 0 ? std::sqrt(total / weight) : 0;
            }
            double VelocityError(double first, double last)
            {
                const double h =
                    std::min({Source.SourceIntervalSeconds, 1 / Settings.OutputFps, Source.DurationSeconds * 0.25});
                const double t0 = std::max(0.0, first - h), t1 = std::min(Source.DurationSeconds, first + h);
                const double t2 = std::max(0.0, last - h), t3 = std::min(Source.DurationSeconds, last + h);
                Pose p0(Source.JointIndices.size()), p1(Source.JointIndices.size()), p2(Source.JointIndices.size()),
                    p3(Source.JointIndices.size());
                if (!At(t0, p0) || !At(t1, p1) || !At(t2, p2) || !At(t3, p3))
                {
                    return std::numeric_limits<double>::infinity();
                }
                const auto velocity = [](const S::SkeletalValue& a, const S::SkeletalValue& b, double dt)
                {
                    auto q = Detail::NormalizeQuaternion(Inverse(Quaternion(a)) * Quaternion(b));
                    if (q.w < 0)
                    {
                        q = Q(-q.x, -q.y, -q.z, -q.w);
                    }
                    const double length = std::hypot(double(q.x), double(q.y), double(q.z));
                    if (length < 1e-12)
                    {
                        return Bvh::Vector3d{};
                    }
                    const double factor = 2 * std::atan2(length, double(q.w)) / (length * dt);
                    return Bvh::Vector3d{q.x * factor, q.y * factor, q.z * factor};
                };
                double sum = 0, total = 0;
                for (size_t i = 0; i < Source.JointIndices.size(); ++i)
                {
                    const double w = Source.RotationWeights.empty() ? 1 : Source.RotationWeights[i];
                    if (w == 0)
                    {
                        continue;
                    }
                    const auto a = velocity(p0.Rotations[i], p1.Rotations[i], t1 - t0);
                    const auto b = velocity(p2.Rotations[i], p3.Rotations[i], t3 - t2);
                    sum += w * ((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
                    total += w;
                }
                return total > 0 ? std::sqrt(sum / total) : 0;
            }
            double Recurrence(double lag, Pose& a, Pose& b)
            {
                double cost = 0;
                constexpr uint32_t samples = 24;
                for (uint32_t i = 0; i < samples; ++i)
                {
                    const double t = (Source.DurationSeconds - lag) * i / (samples - 1);
                    if (!At(t, a) || !At(t + lag, b))
                    {
                        return std::numeric_limits<double>::infinity();
                    }
                    const double e = Error(a, b);
                    cost += e * e;
                }
                return std::sqrt(cost / samples);
            }
        };
    } // namespace
    Status ProcessSkeletalClip(const SkeletalClipPoseSource& source, const SkeletalClipProcessingSettings& settings,
                               S::SkeletalAnimationClip& out, SkeletalClipProcessingReport& outReport)
    {
        if (!source.Sample || source.Name.empty() || source.JointIndices.empty() || source.JointIndices.size() > 1024 ||
            (!source.RotationWeights.empty() && source.RotationWeights.size() != source.JointIndices.size()) ||
            !std::isfinite(source.DurationSeconds) || source.DurationSeconds <= 0 ||
            source.DurationSeconds > std::numeric_limits<float>::max() ||
            !std::isfinite(source.SourceIntervalSeconds) || source.SourceIntervalSeconds <= 0 ||
            !S::IsValidSkeletalRestTransform(source.RootRest) ||
            !S::IsValidRigRootFrame(source.RootFrame, S::RigImportProfile::StaticRootFrame256))
        {
            return Status::InvalidInput;
        }
        bool root = false;
        double weight = 0;
        for (size_t i = 0; i < source.JointIndices.size(); ++i)
        {
            root |= source.JointIndices[i] == source.RootJoint;
            for (size_t j = 0; j < i; ++j)
            {
                if (source.JointIndices[i] == source.JointIndices[j])
                {
                    return Status::InvalidInput;
                }
            }
            const double w = source.RotationWeights.empty() ? 1 : source.RotationWeights[i];
            if (!std::isfinite(w) || w < 0 || w > 1e6)
            {
                return Status::InvalidInput;
            }
            weight += w;
        }
        if (!root)
        {
            return Status::InvalidInput;
        }
        if (!std::isfinite(settings.OutputFps) || settings.OutputFps <= 0 || settings.OutputFps > 1000 ||
            uint32_t(settings.Loop) > uint32_t(SkeletalLoopSelection::Range) ||
            !std::isfinite(settings.MinimumPeriod) || !std::isfinite(settings.MaximumPeriod) ||
            settings.MinimumPeriod <= 0 || settings.MaximumPeriod <= settings.MinimumPeriod ||
            !std::isfinite(settings.LoopRmsThresholdRadians) || settings.LoopRmsThresholdRadians <= 0 ||
            !std::isfinite(settings.MinimumMotionRadians) || settings.MinimumMotionRadians <= 0 ||
            !std::isfinite(settings.SpikeThresholdRadians) || settings.SpikeThresholdRadians < 0 ||
            settings.SpikeThresholdRadians > Pi || settings.SpikeWindowRadius > 16 || settings.SmoothingRadius > 16 ||
            !std::isfinite(settings.TimeScale) || settings.TimeScale <= 0 || settings.TimeScale > 1000 ||
            settings.MaximumSamples < 2 || settings.MaximumSamples > 65536 || settings.MaximumPoseEvaluations < 2 ||
            settings.MaximumPoseEvaluations > 1048576 || settings.MaximumOutputKeys > (uint64_t{1} << 22))
        {
            return Status::InvalidSettings;
        }
        if (settings.SpikeThresholdRadians > 0)
        {
            const double frameCount = std::ceil(source.DurationSeconds / source.SourceIntervalSeconds) + 1;
            if (frameCount > settings.MaximumSamples || frameCount + 2 > settings.MaximumPoseEvaluations ||
                frameCount * source.JointIndices.size() > settings.MaximumOutputKeys)
                return Status::LimitExceeded;
            CleanupSamples cache;
            const size_t count = size_t(frameCount);
            cache.Interval = source.DurationSeconds / (count - 1);
            cache.Frames.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                cache.Frames.emplace_back(source.JointIndices.size());
                auto& pose = cache.Frames.back();
                if (!source.Sample(i + 1 == count ? source.DurationSeconds : i * cache.Interval, pose.Rotations,
                                   pose.Translation, source.Context))
                    return Status::SourceRejected;
            }
            uint32_t replaced = 0;
            for (size_t joint = 0; joint < source.JointIndices.size(); ++joint)
            {
                C::VariableArray<Q> quaternions;
                quaternions.reserve(count);
                for (const auto& pose : cache.Frames)
                    quaternions.push_back(Quaternion(pose.Rotations[joint]));
                C::VariableArray<double> errors(count, 0);
                for (size_t i = 1; i + 1 < count; ++i)
                    errors[i] = Distance(quaternions[i], Detail::Slerp(quaternions[i - 1], quaternions[i + 1], .5f));
                for (size_t i = 1; i + 1 < count; ++i)
                {
                    C::VariableArray<double> neighborhood;
                    const size_t first = i > settings.SpikeWindowRadius ? i - settings.SpikeWindowRadius : 0;
                    const size_t last = std::min(count - 1, i + settings.SpikeWindowRadius);
                    for (size_t k = first; k <= last; ++k)
                        neighborhood.push_back(errors[k]);
                    const double median = Median(neighborhood);
                    for (auto& value : neighborhood)
                        value = std::abs(value - median);
                    const double threshold =
                        std::max(settings.SpikeThresholdRadians, median + 3 * 1.4826 * Median(neighborhood));
                    if (errors[i] > threshold)
                    {
                        cache.Frames[i].Rotations[joint] =
                            Value(Detail::Slerp(quaternions[i - 1], quaternions[i + 1], .5f));
                        ++replaced;
                    }
                }
            }
            auto cleaned = source;
            cleaned.Context = &cache;
            cleaned.Sample = &CleanupSamples::Sample;
            cleaned.SourceIntervalSeconds = cache.Interval;
            auto options = settings;
            options.SpikeThresholdRadians = 0;
            options.MaximumPoseEvaluations -= uint32_t(count);
            S::SkeletalAnimationClip clip;
            SkeletalClipProcessingReport report;
            const auto status = ProcessSkeletalClip(cleaned, options, clip, report);
            if (status != Status::Success)
                return status;
            report.ReplacedSpikes = replaced;
            report.PoseEvaluations += uint32_t(count);
            out = std::move(clip);
            outReport = report;
            return Status::Success;
        }
        Evaluator eval{source, settings};
        if (!eval.Initialize())
        {
            return Status::DegenerateHeading;
        }
        Pose a(source.JointIndices.size()), b(source.JointIndices.size()), start(source.JointIndices.size()),
            finish(source.JointIndices.size());
        SkeletalClipProcessingReport report;
        report.EndSeconds = source.DurationSeconds;
        if (settings.Loop == SkeletalLoopSelection::Range)
        {
            if (!std::isfinite(settings.RangeStart) || !std::isfinite(settings.RangeEnd) || settings.RangeStart < 0 ||
                settings.RangeEnd <= settings.RangeStart || settings.RangeEnd > source.DurationSeconds)
            {
                return Status::InvalidSettings;
            }
            report.StartSeconds = settings.RangeStart;
            report.EndSeconds = settings.RangeEnd;
            report.bRangeSelected = true;
        }
        if (settings.Loop == SkeletalLoopSelection::Auto && weight > 0)
        {
            const double low = settings.MinimumPeriod,
                         high = std::min(settings.MaximumPeriod, source.DurationSeconds * 0.5);
            if (high > low)
            {
                const uint32_t count =
                    uint32_t(std::clamp(std::ceil((high - low) / source.SourceIntervalSeconds) + 1, 3.0, 512.0));
                const double step = (high - low) / (count - 1);
                C::VariableArray<double> scores(count);
                double best = std::numeric_limits<double>::infinity();
                for (uint32_t i = 0; i < count; ++i)
                {
                    scores[i] = eval.Recurrence(low + step * i, a, b);
                    best = std::min(best, scores[i]);
                }
                if (eval.Failure != Status::Success)
                {
                    return eval.Failure;
                }
                C::VariableArray<double> periods, refinedScores;
                best = std::numeric_limits<double>::infinity();
                // 倍周期が偶然格子上に乗っても基本周期を飛ばさないよう、各極小を先に精密化する。
                for (size_t i = 0; i < scores.size(); ++i)
                {
                    if ((i && scores[i] >= scores[i - 1]) || (i + 1 < scores.size() && scores[i] > scores[i + 1]))
                    {
                        continue;
                    }
                    double left = low + step * (i ? i - 1 : 0), right = low + step * std::min(i + 1, scores.size() - 1);
                    for (uint32_t iteration = 0; iteration < 20; ++iteration)
                    {
                        const double m1 = left + (right - left) / 3, m2 = right - (right - left) / 3;
                        if (eval.Recurrence(m1, a, b) <= eval.Recurrence(m2, a, b))
                        {
                            right = m2;
                        }
                        else
                        {
                            left = m1;
                        }
                        if (eval.Failure != Status::Success)
                        {
                            return eval.Failure;
                        }
                    }
                    const double period = (left + right) * 0.5;
                    const double score = eval.Recurrence(period, a, b);
                    if (eval.Failure != Status::Success)
                    {
                        return eval.Failure;
                    }
                    periods.push_back(period);
                    refinedScores.push_back(score);
                    best = std::min(best, score);
                }
                size_t selected = SIZE_MAX;
                for (size_t i = 0; i < periods.size(); ++i)
                {
                    if (refinedScores[i] <= best * 1.25 + 1e-4)
                    {
                        selected = i;
                        break;
                    }
                }
                if (selected != SIZE_MAX)
                {
                    const double period = periods[selected];
                    report.RecurrenceRmsRadians = refinedScores[selected];
                    double motion = 0;
                    if (!eval.At(0, start))
                    {
                        return eval.Failure;
                    }
                    for (uint32_t i = 1; i <= 16; ++i)
                    {
                        if (!eval.At(period * i / 16, a))
                        {
                            return eval.Failure;
                        }
                        motion = std::max(motion, eval.Error(start, a));
                    }
                    if (eval.Failure != Status::Success)
                    {
                        return eval.Failure;
                    }
                    if (report.RecurrenceRmsRadians <= settings.LoopRmsThresholdRadians &&
                        motion >= settings.MinimumMotionRadians && report.RecurrenceRmsRadians <= motion * 0.2)
                    {
                        report.bLoopDetected = true;
                        double seam = std::numeric_limits<double>::infinity();
                        for (uint32_t i = 0; i < 32; ++i)
                        {
                            const double t = (source.DurationSeconds - period) * i / 31;
                            if (!eval.At(t, a) || !eval.At(t + period, b))
                            {
                                return eval.Failure;
                            }
                            const double speed = eval.VelocityError(t, t + period);
                            if (eval.Failure != Status::Success)
                            {
                                return eval.Failure;
                            }
                            const double e = eval.Error(a, b) +
                                             std::min(source.SourceIntervalSeconds, 1 / settings.OutputFps) * speed;
                            if (e < seam)
                            {
                                seam = e;
                                report.StartSeconds = t;
                            }
                        }
                        report.EndSeconds = report.StartSeconds + period;
                    }
                }
            }
        }
        const double duration = report.EndSeconds - report.StartSeconds;
        const double outputDuration = duration * settings.TimeScale;
        const double grid = std::ceil(outputDuration * settings.OutputFps);
        if (!std::isfinite(grid) || grid < 1 || grid + 1 > settings.MaximumSamples ||
            (grid + 1) * (source.JointIndices.size() + 1 + (settings.bExtractRootMotion ? 1 : 0)) >
                settings.MaximumOutputKeys ||
            outputDuration > std::numeric_limits<float>::max())
        {
            return Status::LimitExceeded;
        }
        const size_t samples = size_t(grid) + 1;
        if (!eval.At(report.StartSeconds, start) || !eval.At(report.EndSeconds, finish))
        {
            return eval.Failure;
        }
        const bool loop = report.bLoopDetected || report.bRangeSelected;
        report.PeriodSeconds = loop ? outputDuration : 0;
        report.SeamBeforeRadians = eval.Error(start, finish);
        report.SeamVelocityDifferenceRadiansPerSecond = eval.VelocityError(report.StartSeconds, report.EndSeconds);
        if (eval.Failure != Status::Success)
        {
            return eval.Failure;
        }
        S::SkeletalAnimationClip clip;
        clip.Name = source.Name;
        clip.DurationSeconds = float(outputDuration);
        clip.Channels.resize(source.JointIndices.size() + 1);
        for (size_t i = 0; i < source.JointIndices.size(); ++i)
        {
            auto& c = clip.Channels[i];
            c.JointIndex = source.JointIndices[i];
            c.Path = S::SkeletalAnimationPath::Rotation;
            c.Samples.reserve(samples);
        }
        auto& translation = clip.Channels.back();
        translation.JointIndex = source.RootJoint;
        translation.Path = S::SkeletalAnimationPath::Translation;
        translation.Samples.reserve(samples);
        if (settings.bExtractRootMotion)
        {
            clip.RootMotionJoint = source.RootJoint;
            clip.RootMotion.reserve(samples);
        }
        const uint32_t cycles =
            settings.bAverageCycles && loop
                ? uint32_t(std::clamp(std::floor((source.DurationSeconds - report.StartSeconds) / duration), 1.0, 16.0))
                : 1;
        report.AveragedCycles = cycles;
        Pose cyclePose(source.JointIndices.size());
        double lastYaw = start.Heading, unwrapped = 0, previousX = 0, previousZ = 0;
        const double headingAngle = eval.BindYaw - start.Heading;
        const double headingCos = std::cos(headingAngle), headingSin = std::sin(headingAngle);
        for (size_t sample = 0; sample < samples; ++sample)
        {
            const double outputTime = sample + 1 == samples ? outputDuration : sample / settings.OutputFps;
            const double t = outputTime / settings.TimeScale;
            const float stored = float(outputTime);
            if (!std::isfinite(stored) || (sample && stored <= translation.Samples.back().TimeSeconds))
            {
                return Status::LimitExceeded;
            }
            if (!eval.At(report.StartSeconds + t, a))
            {
                return eval.Failure;
            }
            for (uint32_t cycle = 1; cycle < cycles; ++cycle)
            {
                if (!eval.At(std::min(source.DurationSeconds, report.StartSeconds + cycle * duration + t), cyclePose))
                    return eval.Failure;
                for (size_t joint = 0; joint < source.JointIndices.size(); ++joint)
                    if (joint != eval.RootSlot)
                        a.Rotations[joint] = Value(Detail::Slerp(
                            Quaternion(a.Rotations[joint]), Quaternion(cyclePose.Rotations[joint]), 1.f / (cycle + 1)));
            }
            const float alpha = float(t / duration);
            for (size_t i = 0; i < source.JointIndices.size(); ++i)
            {
                auto q = Quaternion(a.Rotations[i]);
                if (loop)
                {
                    const auto residual = Inverse(Quaternion(finish.Rotations[i])) * Quaternion(start.Rotations[i]);
                    q = Detail::NormalizeQuaternion(q * Detail::Slerp(Q::Identity, residual, alpha));
                    if (sample + 1 == samples)
                    {
                        q = Quaternion(start.Rotations[i]);
                    }
                }
                if (settings.bExtractRootMotion && i == eval.RootSlot)
                {
                    const auto world = eval.Frame * q;
                    double yaw = 0;
                    if (!Heading(world, yaw))
                    {
                        return Status::DegenerateHeading;
                    }
                    q = Detail::NormalizeQuaternion(Inverse(eval.Frame) * Yaw(eval.BindYaw - yaw) * world);
                }
                const auto value = loop && sample + 1 == samples ? clip.Channels[i].Samples.front().Value : Value(q);
                clip.Channels[i].Samples.push_back({stored, value});
            }
            auto p = a.Translation;
            if (loop)
            {
                p.X += (start.Translation.X - finish.Translation.X) * alpha;
                p.Y += (start.Translation.Y - finish.Translation.Y) * alpha;
                p.Z += (start.Translation.Z - finish.Translation.Z) * alpha;
                if (sample + 1 == samples)
                {
                    p = start.Translation;
                }
            }
            if (!std::isfinite(p.X) || !std::isfinite(p.Y) || !std::isfinite(p.Z))
            {
                return Status::InvalidPose;
            }
            translation.Samples.push_back({stored, {p.X, p.Y, p.Z, 0}});
            if (settings.bExtractRootMotion)
            {
                unwrapped += std::remainder(a.Heading - lastYaw, 2 * Pi);
                lastYaw = a.Heading;
                const double x = a.WorldX - start.WorldX, z = a.WorldZ - start.WorldZ;
                const double dx = headingCos * x + headingSin * z, dz = -headingSin * x + headingCos * z;
                if (!std::isfinite(dx) || !std::isfinite(dz) || !std::isfinite(unwrapped))
                {
                    return Status::InvalidPose;
                }
                clip.RootMotion.push_back({stored, dx, dz, unwrapped});
                if (sample)
                {
                    report.PlanarDistanceMeters += std::hypot(dx - previousX, dz - previousZ);
                }
                previousX = dx;
                previousZ = dz;
            }
        }
        SmoothChannels(clip, settings.SmoothingRadius, loop, report.SmoothedChannels);
        report.SeamAfterRadians = loop ? 0 : report.SeamBeforeRadians;
        report.AverageSpeedMetersPerSecond = report.PlanarDistanceMeters / outputDuration;
        report.PoseEvaluations = eval.Count;
        report.OutputSamples = uint32_t(samples);
        out = std::move(clip);
        outReport = report;
        return Status::Success;
    }
} // namespace NorvesLib::Core::Animation
