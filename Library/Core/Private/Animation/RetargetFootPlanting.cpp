#include "Animation/RetargetFootPlanting.h"
#include "Animation/MotionAnalysis.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include "Animation/TwoBoneIK.h"
#include "Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace C = Container;
    namespace S = Skeletal;
    namespace D = Detail;
    using V = Bvh::Vector3d;
    using Q = D::RetargetQuaterniond;

    namespace
    {
        constexpr double Pi = 3.14159265358979323846;

        bool Fail(C::AnsiString& error, const char* code)
        {
            error = code;
            return false;
        }

        bool Finite(V v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
        }

        bool Finite(Q q)
        {
            return std::isfinite(q.X) && std::isfinite(q.Y) && std::isfinite(q.Z) && std::isfinite(q.W);
        }

        bool Finite(const S::SkeletalValue& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z) && std::isfinite(v.W);
        }

        V Add(V a, V b) { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }
        V Sub(V a, V b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }
        V Mul(V a, double s) { return {a.X * s, a.Y * s, a.Z * s}; }
        double Dot(V a, V b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        V Cross(V a, V b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }
        double Length(V a) { return std::hypot(a.X, a.Y, a.Z); }
        double Horizontal(V a) { return std::hypot(a.X, a.Z); }

        bool Unit(V& v)
        {
            const double length = Length(v);
            if (!std::isfinite(length) || length < 1e-12)
                return false;
            v = Mul(v, 1 / length);
            return Finite(v);
        }

        bool Normalize(Q& q)
        {
            const double length = std::hypot(std::hypot(q.X, q.Y), std::hypot(q.Z, q.W));
            if (!std::isfinite(length) || length < 1e-12)
                return false;
            q = {q.X / length, q.Y / length, q.Z / length, q.W / length};
            return Finite(q);
        }

        Q Quaternion(const S::SkeletalValue& v) { return {v.X, v.Y, v.Z, v.W}; }
        Q Inverse(Q q) { return {-q.X, -q.Y, -q.Z, q.W}; }
        Q Multiply(Q a, Q b)
        {
            return {a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
                    a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
                    a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
                    a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z};
        }
        V Rotate(Q q, V v)
        {
            const V xyz{q.X, q.Y, q.Z};
            const V twice = Mul(Cross(xyz, v), 2);
            return Add(v, Add(Mul(twice, q.W), Cross(xyz, twice)));
        }

        Q Slerp(Q a, Q b, double t)
        {
            double dot = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
            if (dot < 0)
            {
                b = {-b.X, -b.Y, -b.Z, -b.W};
                dot = -dot;
            }
            double left = 1 - t, right = t;
            if (dot <= 0.9995)
            {
                const double angle = std::acos(std::clamp(dot, -1.0, 1.0));
                const double sine = std::sin(angle);
                left = std::sin((1 - t) * angle) / sine;
                right = std::sin(t * angle) / sine;
            }
            Q result{left * a.X + right * b.X, left * a.Y + right * b.Y,
                     left * a.Z + right * b.Z, left * a.W + right * b.W};
            if (!Normalize(result))
                return {0, 0, 0, 0};
            return result;
        }

        V Perpendicular(V axis)
        {
            V basis = std::abs(axis.X) <= std::abs(axis.Y) && std::abs(axis.X) <= std::abs(axis.Z)
                          ? V{1, 0, 0}
                          : (std::abs(axis.Y) <= std::abs(axis.Z) ? V{0, 1, 0} : V{0, 0, 1});
            V result = Cross(axis, basis);
            if (!Unit(result))
                return {1, 0, 0};
            return result;
        }

        Q ShortestRotation(V from, V to)
        {
            if (!Unit(from) || !Unit(to))
                return {};
            const double dot = std::clamp(Dot(from, to), -1.0, 1.0);
            if (dot < -1 + 1e-12)
            {
                const V axis = Perpendicular(from);
                return {axis.X, axis.Y, axis.Z, 0};
            }
            const V cross = Cross(from, to);
            Q result{cross.X, cross.Y, cross.Z, 1 + dot};
            if (!Normalize(result))
                return {};
            return result;
        }

        struct Local
        {
            V Translation;
            Q Rotation;
            V Scale{1, 1, 1};
        };

        struct Pose
        {
            C::VariableArray<Local> Locals;
            C::VariableArray<V> Positions;
            C::VariableArray<Q> Rotations;
        };

        Local Rest(const S::SkeletalRestTransform& rest)
        {
            return {{rest.Translation.X, rest.Translation.Y, rest.Translation.Z}, Quaternion(rest.Rotation),
                    {rest.Scale.X, rest.Scale.Y, rest.Scale.Z}};
        }

        bool Sample(const S::SkeletalAnimationChannel& channel, float time, Local& local)
        {
            const auto& samples = channel.Samples;
            const auto next = std::lower_bound(samples.begin(), samples.end(), time,
                                               [](const auto& key, float t) { return key.TimeSeconds < t; });
            const auto* a = next == samples.begin() ? &samples.front() : &*(next - 1);
            const auto* b = next == samples.end() ? &samples.back() : &*next;
            if (time >= samples.back().TimeSeconds)
                a = b = &samples.back();
            double alpha = 0;
            if (a != b)
            {
                if (channel.Interpolation == S::SkeletalAnimationInterpolation::Step)
                    alpha = time == b->TimeSeconds ? 1 : 0;
                else
                {
                    const double duration = double(b->TimeSeconds) - a->TimeSeconds;
                    alpha = duration > double(Math::Constants::EPSILON) ? (double(time) - a->TimeSeconds) / duration : 0;
                }
            }
            const auto& x = a->Value;
            const auto& y = b->Value;
            if (channel.Path == S::SkeletalAnimationPath::Rotation)
            {
                Q qa = Quaternion(x), qb = Quaternion(y);
                if (!Normalize(qa) || !Normalize(qb))
                    return false;
                local.Rotation = Slerp(qa, qb, alpha);
                return Normalize(local.Rotation);
            }
            const V value{double(x.X) + (double(y.X) - x.X) * alpha,
                          double(x.Y) + (double(y.Y) - x.Y) * alpha,
                          double(x.Z) + (double(y.Z) - x.Z) * alpha};
            if (!Finite(value))
                return false;
            if (channel.Path == S::SkeletalAnimationPath::Translation)
                local.Translation = value;
            else
                local.Scale = value;
            return true;
        }

        bool Forward(const RetargetFootPlantingInput& input, C::Span<const uint32_t> order, Q rootParent,
                     V parentPosition, Pose& pose)
        {
            for (uint32_t i : order)
            {
                const int32_t parent = input.Parents[i];
                const Q parentRotation = parent < 0 ? rootParent : pose.Rotations[size_t(parent)];
                const V origin = parent < 0 ? parentPosition : pose.Positions[size_t(parent)];
                pose.Rotations[i] = Multiply(parentRotation, pose.Locals[i].Rotation);
                pose.Positions[i] = Add(origin, Rotate(parentRotation, Mul(pose.Locals[i].Translation, input.FrameScale)));
                if (!Normalize(pose.Rotations[i]) || !Finite(pose.Positions[i]))
                    return false;
            }
            return true;
        }

        bool SamplePose(const RetargetFootPlantingInput& input, const S::SkeletalAnimationClip& clip,
                        C::Span<const uint32_t> order, Q rootParent, V parentPosition, float time,
                        bool bRest, Pose& pose)
        {
            for (size_t i = 0; i < input.LocalRest.size(); ++i)
            {
                pose.Locals[i] = Rest(input.LocalRest[i]);
                if (!Normalize(pose.Locals[i].Rotation))
                    return false;
            }
            if (!bRest)
            {
                for (const auto& channel : clip.Channels)
                    if (!Sample(channel, time, pose.Locals[channel.JointIndex]))
                        return false;
            }
            return Forward(input, order, rootParent, parentPosition, pose);
        }

        struct IkResult
        {
            Q RootDelta, MidDelta;
            V PoleUsed;
        };

        bool HasAncestor(C::Span<const int32_t> parents, uint32_t ancestor, uint32_t child)
        {
            int32_t current = parents[child];
            while (current >= 0)
            {
                if (uint32_t(current) == ancestor)
                    return true;
                current = parents[size_t(current)];
            }
            return false;
        }

        bool Validate(const RetargetFootPlantingInput& input, const RetargetFootPlantingSettings& settings,
                      const S::SkeletalAnimationClip& clip, C::VariableArray<float>& times,
                      C::VariableArray<uint32_t>& order, C::VariableArray<Q>& parentRotations,
                      C::AnsiString& error)
        {
            if (!D::RetargetValidSpan(input.Parents) || !D::RetargetValidSpan(input.LocalRest) ||
                !D::RetargetValidSpan(input.RootParents) || !D::RetargetValidSpan(input.Legs) ||
                !D::RetargetValidSpan(input.SourceFeet))
                return Fail(error, "foot_planting_input");
            if (!std::isfinite(settings.HeightThresholdMeters) ||
                !std::isfinite(settings.SpeedThresholdMetersPerSecond) || !std::isfinite(settings.GroundHeightMeters) ||
                !std::isfinite(input.FrameScale) || !std::isfinite(input.SourceFrameIntervalSeconds))
                return Fail(error, "foot_planting_nonfinite");
            if (settings.HeightThresholdMeters <= 0 || settings.SpeedThresholdMetersPerSecond <= 0 ||
                settings.MinStanceFrames == 0 || input.FrameScale <= 0 || input.SourceFrameIntervalSeconds <= 0 ||
                settings.HeightThresholdMeters > std::numeric_limits<float>::max() ||
                settings.SpeedThresholdMetersPerSecond > std::numeric_limits<float>::max() ||
                float(settings.HeightThresholdMeters) <= 0 || float(settings.SpeedThresholdMetersPerSecond) <= 0)
                return Fail(error, "foot_planting_input");
            if (!CollectRetargetFootPlantingTimes(clip, times))
                return Fail(error, "foot_planting_nonfinite");
            const size_t frames = times.size(), joints = input.Parents.size(), legs = input.Legs.size();
            if (frames < 2 || frames > UINT32_MAX || joints == 0 || joints > INT32_MAX ||
                input.LocalRest.size() != joints || input.RootJoint >= joints || legs == 0 ||
                input.RootParents.size() != frames || legs > SIZE_MAX / frames ||
                input.SourceFeet.size() != frames * legs || input.Parents[input.RootJoint] != -1)
                return Fail(error, "foot_planting_input");
            const double interval = (double(times.back()) - times.front()) / double(frames - 1);
            if (!std::isfinite(interval))
                return Fail(error, "foot_planting_nonfinite");
            if (!(interval > 0))
                return Fail(error, "foot_planting_input");
            for (size_t f = 1; f < frames; ++f)
            {
                const double dt = double(times[f]) - times[f - 1];
                if (!(dt > 0) || std::abs(dt - interval) > interval * 1e-3)
                    return Fail(error, "foot_planting_input");
            }

            C::VariableArray<uint8_t> ready(joints, 0);
            for (size_t i = 0; i < joints; ++i)
            {
                const auto parent = input.Parents[i];
                if (parent < -1 || (parent >= 0 && size_t(parent) >= joints) ||
                    (parent < 0 && i != input.RootJoint))
                    return Fail(error, "foot_planting_input");
                const auto& r = input.LocalRest[i];
                const Local local = Rest(r);
                if (!Finite(local.Translation) || !Finite(local.Rotation) || !Finite(local.Scale))
                    return Fail(error, "foot_planting_nonfinite");
                Q rotation = local.Rotation;
                if (!Normalize(rotation))
                    return Fail(error, "foot_planting_input");
            }
            while (order.size() < joints)
            {
                const size_t before = order.size();
                for (size_t i = 0; i < joints; ++i)
                {
                    const auto parent = input.Parents[i];
                    if (!ready[i] && (parent < 0 || ready[size_t(parent)]))
                    {
                        order.push_back(uint32_t(i));
                        ready[i] = 1;
                    }
                }
                if (before == order.size())
                    return Fail(error, "foot_planting_input");
            }
            for (const auto& leg : input.Legs)
            {
                const bool front = leg.PawRole == SkeletalRole::FrontLPaw || leg.PawRole == SkeletalRole::FrontRPaw;
                const bool hind = leg.PawRole == SkeletalRole::HindLPaw || leg.PawRole == SkeletalRole::HindRPaw;
                if ((!front && !hind) || (front && leg.Hock != UINT32_MAX) ||
                    (hind && leg.Hock == UINT32_MAX) || leg.Upper >= joints || leg.Lower >= joints ||
                    leg.Paw >= joints || (hind && leg.Hock >= joints) ||
                    !HasAncestor(input.Parents, leg.Upper, leg.Lower) ||
                    !HasAncestor(input.Parents, leg.Lower, hind ? leg.Hock : leg.Paw) ||
                    (hind && !HasAncestor(input.Parents, leg.Hock, leg.Paw)))
                    return Fail(error, "foot_planting_input");
            }
            parentRotations.resize(frames);
            for (size_t f = 0; f < frames; ++f)
            {
                const auto& parent = input.RootParents[f];
                if (!Finite(parent.Translation))
                    return Fail(error, "foot_planting_nonfinite");
                for (double value : parent.Rotation.Values)
                    if (!std::isfinite(value))
                        return Fail(error, "foot_planting_nonfinite");
                Bvh::Matrix3d rotation;
                if (!D::RetargetProjectRotation(parent.Rotation, rotation) ||
                    !D::RetargetExtractQuaternion(rotation, parentRotations[f]))
                    return Fail(error, "foot_planting_input");
            }
            for (V foot : input.SourceFeet)
                if (!Finite(foot))
                    return Fail(error, "foot_planting_nonfinite");

            if (joints > SIZE_MAX / 3)
                return Fail(error, "foot_planting_input");
            C::VariableArray<uint8_t> used(joints * 3, 0);
            for (const auto& channel : clip.Channels)
            {
                const auto path = uint32_t(channel.Path);
                if (channel.JointIndex >= joints || path > 2 || uint32_t(channel.Interpolation) > 1 ||
                    channel.Samples.empty() || used[size_t(channel.JointIndex) * 3 + path]++)
                    return Fail(error, "foot_planting_input");
                for (size_t i = 0; i < channel.Samples.size(); ++i)
                {
                    const auto& key = channel.Samples[i];
                    if (!std::isfinite(key.TimeSeconds) || !Finite(key.Value))
                        return Fail(error, "foot_planting_nonfinite");
                    if (i && key.TimeSeconds <= channel.Samples[i - 1].TimeSeconds)
                        return Fail(error, "foot_planting_input");
                    if (path == 1)
                    {
                        Q rotation = Quaternion(key.Value);
                        if (!Normalize(rotation))
                            return Fail(error, "foot_planting_input");
                    }
                }
            }
            return true;
        }

        bool FindContacts(const RetargetFootPlantingInput& input, const RetargetFootPlantingSettings& settings,
                          size_t frames, RetargetFootPlantingReport& report, C::AnsiString& error)
        {
            const size_t legs = input.Legs.size();
            FootContactOptions options;
            options.HeightThreshold = float(settings.HeightThresholdMeters);
            options.VerticalSpeedThreshold = float(settings.SpeedThresholdMetersPerSecond);
            options.MinimumWindowSeconds = 0;
            options.Hysteresis = 1.5f;
            C::VariableArray<float> heights(frames);
            report.Legs.resize(legs);
            for (size_t l = 0; l < legs; ++l)
            {
                auto& out = report.Legs[l];
                out.PawRole = input.Legs[l].PawRole;
                double minimum = input.SourceFeet[l].Y;
                for (size_t f = 1; f < frames; ++f)
                    minimum = std::min(minimum, input.SourceFeet[f * legs + l].Y);
                for (size_t f = 0; f < frames; ++f)
                {
                    const double height = input.SourceFeet[f * legs + l].Y - minimum;
                    if (!std::isfinite(height) || height > std::numeric_limits<float>::max())
                        return Fail(error, "foot_planting_nonfinite");
                    heights[f] = float(height);
                }
                C::VariableArray<FootContactSampleWindow> windows;
                if (!DetectFootContactWindows(heights, input.SourceFrameIntervalSeconds, options, windows))
                    return Fail(error, "foot_planting_input");
                for (const auto& window : windows)
                {
                    size_t begin = SIZE_MAX;
                    const auto close = [&](size_t end)
                    {
                        if (begin != SIZE_MAX && end - begin >= settings.MinStanceFrames)
                            out.Contacts.push_back({uint32_t(begin), uint32_t(end)});
                        begin = SIZE_MAX;
                    };
                    for (size_t f = window.Begin; f < window.End; ++f)
                    {
                        const size_t previous = f == 0 ? 0 : f - 1;
                        const size_t next = std::min(f + 1, frames - 1);
                        const V delta = Sub(input.SourceFeet[next * legs + l], input.SourceFeet[previous * legs + l]);
                        const double elapsed = input.SourceFrameIntervalSeconds * double(next - previous);
                        const double speed = Horizontal(delta) / elapsed;
                        if (!std::isfinite(elapsed) || !std::isfinite(speed))
                            return Fail(error, "foot_planting_nonfinite");
                        const double limit = settings.SpeedThresholdMetersPerSecond * (begin == SIZE_MAX ? 1 : 1.5);
                        if (!std::isfinite(limit))
                            return Fail(error, "foot_planting_nonfinite");
                        if (speed < limit)
                        {
                            if (begin == SIZE_MAX)
                                begin = f;
                        }
                        else
                            close(f);
                    }
                    close(window.End);
                }
            }
            return true;
        }

        struct Goal
        {
            V Position;
            double Weight = 0;
        };

        void MakeGoals(const RetargetFootPlantingSettings& settings, size_t frames, size_t legs,
                       const C::VariableArray<V>& paws, const RetargetFootPlantingReport& report,
                       C::VariableArray<Goal>& goals)
        {
            for (size_t l = 0; l < legs; ++l)
            {
                const auto& ranges = report.Legs[l].Contacts;
                size_t next = 0;
                for (size_t f = 0; f < frames; ++f)
                {
                    while (next < ranges.size() && f >= ranges[next].End)
                        ++next;
                    size_t chosen = SIZE_MAX, distance = SIZE_MAX;
                    if (next < ranges.size() && f >= ranges[next].Begin)
                    {
                        chosen = next;
                        distance = 0;
                    }
                    else
                    {
                        if (next > 0)
                        {
                            chosen = next - 1;
                            distance = f - (size_t(ranges[chosen].End) - 1);
                        }
                        if (next < ranges.size() && size_t(ranges[next].Begin) - f < distance)
                        {
                            chosen = next;
                            distance = size_t(ranges[next].Begin) - f;
                        }
                    }
                    if (chosen == SIZE_MAX || distance > settings.BlendFrames)
                        continue;
                    const V anchor = paws[size_t(ranges[chosen].Begin) * legs + l];
                    const V fixed{anchor.X, settings.GroundHeightMeters, anchor.Z};
                    double weight = 1;
                    if (distance > 0)
                    {
                        const double x = 1 - double(distance) / (double(settings.BlendFrames) + 1);
                        weight = std::min(x * x * (3 - 2 * x), std::nextafter(1.0, 0.0));
                    }
                    auto& goal = goals[f * legs + l];
                    goal.Weight = weight;
                    goal.Position = Add(paws[f * legs + l], Mul(Sub(fixed, paws[f * legs + l]), weight));
                }
            }
        }

        bool FilterDrop(const C::VariableArray<double>& need, uint32_t blendFrames, C::VariableArray<double>& drop)
        {
            const size_t frames = need.size();
            const size_t radius = std::min<size_t>(std::max<uint32_t>(blendFrames, 1), frames - 1);
            C::VariableArray<size_t> queue(frames);
            C::VariableArray<double> maximum(frames);
            size_t head = 0, tail = 0, added = 0;
            for (size_t f = 0; f < frames; ++f)
            {
                const size_t right = std::min(frames - 1, f + radius);
                while (added <= right)
                {
                    while (tail > head && need[queue[tail - 1]] <= need[added])
                        --tail;
                    queue[tail++] = added++;
                }
                const size_t left = f > radius ? f - radius : 0;
                while (tail > head && queue[head] < left)
                    ++head;
                maximum[f] = need[queue[head]];
            }
            double sum = 0;
            size_t left = 0, right = 0;
            for (size_t f = 0; f < frames; ++f)
            {
                const size_t wantedRight = std::min(frames - 1, f + radius);
                while (right <= wantedRight)
                    sum += maximum[right++];
                const size_t wantedLeft = f > radius ? f - radius : 0;
                while (left < wantedLeft)
                    sum -= maximum[left++];
                if (!std::isfinite(sum))
                    return false;
                drop[f] = std::max(need[f], sum / double(right - left));
                if (!std::isfinite(drop[f]))
                    return false;
            }
            return true;
        }

        double Slide(const C::VariableArray<V>& paws, size_t legs, const RetargetFootPlantingReport& report)
        {
            double maximum = 0;
            for (size_t l = 0; l < legs; ++l)
                for (const auto& range : report.Legs[l].Contacts)
                {
                    const V anchor = paws[size_t(range.Begin) * legs + l];
                    for (size_t f = range.Begin; f < range.End; ++f)
                        maximum = std::max(maximum, Horizontal(Sub(paws[f * legs + l], anchor)));
                }
            return maximum;
        }

        bool StoreFloat(double value, float& out)
        {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                return false;
            out = float(value);
            return std::isfinite(out);
        }

        bool StoreVector(V value, Math::Vector3& out)
        {
            return StoreFloat(value.X, out.x) && StoreFloat(value.Y, out.y) && StoreFloat(value.Z, out.z);
        }

        bool WriteChannels(const RetargetFootPlantingInput& input, C::Span<const float> times,
                           C::Span<const uint32_t> changed, C::Span<const Q> rotations,
                           C::Span<const V> translations, S::SkeletalAnimationClip& clip)
        {
            const auto channelFor = [&](uint32_t joint, S::SkeletalAnimationPath path) -> S::SkeletalAnimationChannel&
            {
                for (auto& channel : clip.Channels)
                    if (channel.JointIndex == joint && channel.Path == path)
                        return channel;
                S::SkeletalAnimationChannel channel;
                channel.JointIndex = joint;
                channel.Path = path;
                channel.Interpolation = S::SkeletalAnimationInterpolation::Linear;
                clip.Channels.push_back(std::move(channel));
                return clip.Channels.back();
            };
            for (size_t j = 0; j < changed.size(); ++j)
            {
                auto& channel = channelFor(changed[j], S::SkeletalAnimationPath::Rotation);
                channel.Samples.clear();
                channel.Samples.reserve(times.size());
                S::SkeletalValue previous{};
                for (size_t f = 0; f < times.size(); ++f)
                {
                    Q q = rotations[f * changed.size() + j];
                    if (!Normalize(q))
                        return false;
                    S::SkeletalAnimationSample sample;
                    sample.TimeSeconds = times[f];
                    if (!StoreFloat(q.X, sample.Value.X) || !StoreFloat(q.Y, sample.Value.Y) ||
                        !StoreFloat(q.Z, sample.Value.Z) || !StoreFloat(q.W, sample.Value.W))
                        return false;
                    auto& value = sample.Value;
                    if (f && double(previous.X) * value.X + double(previous.Y) * value.Y +
                                 double(previous.Z) * value.Z + double(previous.W) * value.W < 0)
                        value = {-value.X, -value.Y, -value.Z, -value.W};
                    channel.Samples.push_back(sample);
                    previous = value;
                }
            }
            auto& channel = channelFor(input.RootJoint, S::SkeletalAnimationPath::Translation);
            channel.Samples.clear();
            channel.Samples.reserve(times.size());
            for (size_t f = 0; f < times.size(); ++f)
            {
                S::SkeletalAnimationSample sample;
                sample.TimeSeconds = times[f];
                if (!StoreFloat(translations[f].X, sample.Value.X) ||
                    !StoreFloat(translations[f].Y, sample.Value.Y) ||
                    !StoreFloat(translations[f].Z, sample.Value.Z))
                    return false;
                channel.Samples.push_back(sample);
            }
            return true;
        }
    } // namespace

    bool CollectRetargetFootPlantingTimes(const S::SkeletalAnimationClip& clip, C::VariableArray<float>& times)
    {
        C::VariableArray<float> result;
        for (const auto& channel : clip.Channels)
            for (const auto& key : channel.Samples)
            {
                if (!std::isfinite(key.TimeSeconds))
                    return false;
                result.push_back(key.TimeSeconds);
            }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        times = std::move(result);
        return true;
    }

    bool ApplyRetargetFootPlanting(const RetargetFootPlantingInput& input,
                                   const RetargetFootPlantingSettings& settings, S::SkeletalAnimationClip& clip,
                                   RetargetFootPlantingReport& report, C::AnsiString& error)
    {
        if (!settings.bEnabled)
        {
            report = {};
            return true;
        }
        C::VariableArray<float> times;
        C::VariableArray<uint32_t> order;
        C::VariableArray<Q> parentRotations;
        if (!Validate(input, settings, clip, times, order, parentRotations, error))
            return false;
        const size_t frames = times.size(), legs = input.Legs.size(), joints = input.Parents.size();
        RetargetFootPlantingReport candidateReport;
        if (!FindContacts(input, settings, frames, candidateReport, error))
            return false;

        Pose pose;
        pose.Locals.resize(joints);
        pose.Positions.resize(joints);
        pose.Rotations.resize(joints);
        struct LegRest
        {
            double Reach = 0;
            V PawUp;
        };
        C::VariableArray<LegRest> legRest(legs);
        if (!SamplePose(input, clip, order, parentRotations[0], input.RootParents[0].Translation, times[0], true, pose))
            return Fail(error, "foot_planting_nonfinite");
        for (size_t l = 0; l < legs; ++l)
        {
            const auto& leg = input.Legs[l];
            const uint32_t end = leg.Hock == UINT32_MAX ? leg.Paw : leg.Hock;
            legRest[l].Reach = 0.999 * (Length(Sub(pose.Positions[leg.Lower], pose.Positions[leg.Upper])) +
                                        Length(Sub(pose.Positions[end], pose.Positions[leg.Lower])));
            legRest[l].PawUp = Rotate(Inverse(pose.Rotations[leg.Paw]), {0, 1, 0});
            if (!std::isfinite(legRest[l].Reach) || !Unit(legRest[l].PawUp))
                return Fail(error, "foot_planting_nonfinite");
        }

        C::VariableArray<V> originalPaws(frames * legs);
        for (size_t f = 0; f < frames; ++f)
        {
            if (!SamplePose(input, clip, order, parentRotations[f], input.RootParents[f].Translation,
                            times[f], false, pose))
                return Fail(error, "foot_planting_nonfinite");
            for (size_t l = 0; l < legs; ++l)
                originalPaws[f * legs + l] = pose.Positions[input.Legs[l].Paw];
        }
        candidateReport.FootSlideBeforeMeters = Slide(originalPaws, legs, candidateReport);
        if (!std::isfinite(candidateReport.FootSlideBeforeMeters))
            return Fail(error, "foot_planting_nonfinite");
        C::VariableArray<Goal> goals(frames * legs);
        MakeGoals(settings, frames, legs, originalPaws, candidateReport, goals);
        for (const auto& goal : goals)
            if (!Finite(goal.Position) || !std::isfinite(goal.Weight))
                return Fail(error, "foot_planting_nonfinite");

        C::VariableArray<double> need(frames, 0), drop(frames, 0);
        for (size_t f = 0; f < frames; ++f)
        {
            if (!SamplePose(input, clip, order, parentRotations[f], input.RootParents[f].Translation,
                            times[f], false, pose))
                return Fail(error, "foot_planting_nonfinite");
            for (size_t l = 0; l < legs; ++l)
            {
                const auto& goal = goals[f * legs + l];
                if (goal.Weight <= 0)
                    continue;
                const auto& leg = input.Legs[l];
                V target = goal.Position;
                if (leg.Hock != UINT32_MAX)
                    target = Sub(target, Sub(pose.Positions[leg.Paw], pose.Positions[leg.Hock]));
                const V offset = Sub(pose.Positions[leg.Upper], target);
                const double horizontal = Horizontal(offset), reach = legRest[l].Reach;
                if (!Finite(target) || !Finite(offset) || !std::isfinite(horizontal))
                    return Fail(error, "foot_planting_nonfinite");
                double d = offset.Y;
                if (horizontal < reach)
                {
                    const double ratio = horizontal / reach;
                    d -= reach * std::sqrt(std::max(0.0, 1 - ratio * ratio));
                }
                if (!std::isfinite(d))
                    return Fail(error, "foot_planting_nonfinite");
                need[f] = std::max(need[f], d);
            }
        }
        if (!FilterDrop(need, settings.BlendFrames, drop))
            return Fail(error, "foot_planting_nonfinite");
        for (double value : drop)
            candidateReport.PelvisDropMeters = std::max(candidateReport.PelvisDropMeters, value);

        C::VariableArray<uint8_t> marked(joints, 0);
        for (const auto& leg : input.Legs)
        {
            marked[leg.Upper] = marked[leg.Lower] = marked[leg.Paw] = 1;
            if (leg.Hock != UINT32_MAX)
            {
                marked[leg.Hock] = 1;
            }
        }
        C::VariableArray<uint32_t> changed;
        for (size_t i = 0; i < joints; ++i)
            if (marked[i])
                changed.push_back(uint32_t(i));
        if (changed.size() > SIZE_MAX / frames)
            return Fail(error, "foot_planting_input");
        C::VariableArray<Q> outputRotations(frames * changed.size());
        C::VariableArray<V> outputTranslations(frames), previousPoles(legs);
        C::VariableArray<uint8_t> hasPreviousPole(legs, 0);
        for (size_t f = 0; f < frames; ++f)
        {
            if (!SamplePose(input, clip, order, parentRotations[f], input.RootParents[f].Translation,
                            times[f], false, pose))
                return Fail(error, "foot_planting_nonfinite");
            const V localDrop = Mul(Rotate(Inverse(parentRotations[f]), {0, -drop[f], 0}), 1 / input.FrameScale);
            pose.Locals[input.RootJoint].Translation = Add(pose.Locals[input.RootJoint].Translation, localDrop);
            if (!Finite(localDrop) || !Finite(pose.Locals[input.RootJoint].Translation) ||
                !Forward(input, order, parentRotations[f], input.RootParents[f].Translation, pose))
                return Fail(error, "foot_planting_nonfinite");
            for (size_t l = 0; l < legs; ++l)
            {
                const Goal& goal = goals[f * legs + l];
                if (goal.Weight <= 0)
                {
                    hasPreviousPole[l] = 0;
                    continue;
                }
                const auto& leg = input.Legs[l];
                const uint32_t endJoint = leg.Hock == UINT32_MAX ? leg.Paw : leg.Hock;
                const V root = pose.Positions[leg.Upper], mid = pose.Positions[leg.Lower], end = pose.Positions[endJoint];
                V axis = Sub(end, root), pole = Sub(mid, root);
                if (!Finite(axis) || !Finite(pole))
                    return Fail(error, "foot_planting_nonfinite");
                if (Unit(axis))
                    pole = Sub(pole, Mul(axis, Dot(pole, axis)));
                else
                    pole = {};
                if (!Finite(pole))
                    return Fail(error, "foot_planting_nonfinite");
                if (!Unit(pole))
                    pole = {};
                const V fallback = hasPreviousPole[l] ? previousPoles[l] : Rotate(parentRotations[f], {0, 0, 1});
                const V hockToPaw = Sub(pose.Positions[leg.Paw], end);
                // 中足のワールド方向を保ち、足先から引いた hock の目標を二関節で解く。
                const Q hockWorld = pose.Rotations[endJoint];
                const V target = leg.Hock == UINT32_MAX ? goal.Position : Sub(goal.Position, hockToPaw);
                TwoBoneIKInput ikInput;
                ikInput.Root = {0.0f, 0.0f, 0.0f};
                ikInput.MaxStretch = 1.0f;
                ikInput.SoftLimitWidth = 0.0f;
                if (!StoreVector(Sub(mid, root), ikInput.Mid) || !StoreVector(Sub(end, root), ikInput.End) ||
                    !StoreVector(pole, ikInput.Pole) || !StoreVector(fallback, ikInput.FallbackPole))
                    return Fail(error, "foot_planting_nonfinite");
                IkResult result;
                if (!StoreVector(Sub(target, root), ikInput.Target))
                {
                    return Fail(error, "foot_planting_nonfinite");
                }
                const auto solved = SolveTwoBoneIK(ikInput);
                result.RootDelta = {solved.RootDelta.x, solved.RootDelta.y, solved.RootDelta.z, solved.RootDelta.w};
                result.MidDelta = {solved.MidDelta.x, solved.MidDelta.y, solved.MidDelta.z, solved.MidDelta.w};
                result.PoleUsed = {solved.PoleUsed.x, solved.PoleUsed.y, solved.PoleUsed.z};
                if (!Normalize(result.RootDelta) || !Normalize(result.MidDelta) || !Finite(result.PoleUsed))
                {
                    return Fail(error, "foot_planting_nonfinite");
                }
                previousPoles[l] = result.PoleUsed;
                hasPreviousPole[l] = 1;
                const auto angle = [](Q q)
                {
                    return 2 * std::atan2(std::hypot(q.X, q.Y, q.Z), std::abs(q.W)) * (180 / Pi);
                };
                candidateReport.IkAdjustmentDegrees = std::max(candidateReport.IkAdjustmentDegrees,
                                                               std::max(angle(result.RootDelta), angle(result.MidDelta)));
                // 中間側の差分は、根を更新する前の中間側ワールド回転に掛ける。
                const Q newUpper = Multiply(result.RootDelta, pose.Rotations[leg.Upper]);
                const Q newLower = Multiply(result.MidDelta, pose.Rotations[leg.Lower]);
                const int32_t upperParent = input.Parents[leg.Upper];
                const Q upperParentRotation = upperParent < 0 ? parentRotations[f] : pose.Rotations[size_t(upperParent)];
                pose.Locals[leg.Upper].Rotation = Multiply(Inverse(upperParentRotation), newUpper);
                if (!Normalize(pose.Locals[leg.Upper].Rotation) ||
                    !Forward(input, order, parentRotations[f], input.RootParents[f].Translation, pose))
                    return Fail(error, "foot_planting_nonfinite");
                const int32_t lowerParent = input.Parents[leg.Lower];
                const Q lowerParentRotation = lowerParent < 0 ? parentRotations[f] : pose.Rotations[size_t(lowerParent)];
                pose.Locals[leg.Lower].Rotation = Multiply(Inverse(lowerParentRotation), newLower);
                if (!Normalize(pose.Locals[leg.Lower].Rotation) ||
                    !Forward(input, order, parentRotations[f], input.RootParents[f].Translation, pose))
                    return Fail(error, "foot_planting_nonfinite");

                if (leg.Hock != UINT32_MAX)
                {
                    const int32_t hockParent = input.Parents[leg.Hock];
                    pose.Locals[leg.Hock].Rotation = Multiply(Inverse(pose.Rotations[size_t(hockParent)]), hockWorld);
                    if (!Normalize(pose.Locals[leg.Hock].Rotation) ||
                        !Forward(input, order, parentRotations[f], input.RootParents[f].Translation, pose))
                    {
                        return Fail(error, "foot_planting_nonfinite");
                    }
                }

                const V worldUp = Rotate(pose.Rotations[leg.Paw], legRest[l].PawUp);
                const Q align = ShortestRotation(worldUp, {0, 1, 0});
                const Q pawWorld = Multiply(Slerp({}, align, goal.Weight), pose.Rotations[leg.Paw]);
                const int32_t pawParent = input.Parents[leg.Paw];
                const Q pawParentRotation = pawParent < 0 ? parentRotations[f] : pose.Rotations[size_t(pawParent)];
                pose.Locals[leg.Paw].Rotation = Multiply(Inverse(pawParentRotation), pawWorld);
                if (!Normalize(pose.Locals[leg.Paw].Rotation) ||
                    !Forward(input, order, parentRotations[f], input.RootParents[f].Translation, pose))
                    return Fail(error, "foot_planting_nonfinite");
            }
            outputTranslations[f] = pose.Locals[input.RootJoint].Translation;
            for (size_t j = 0; j < changed.size(); ++j)
                outputRotations[f * changed.size() + j] = pose.Locals[changed[j]].Rotation;
        }

        S::SkeletalAnimationClip candidateClip = clip;
        if (!WriteChannels(input, times, changed, outputRotations, outputTranslations, candidateClip))
            return Fail(error, "foot_planting_nonfinite");
        C::VariableArray<V> resultPaws(frames * legs);
        for (size_t f = 0; f < frames; ++f)
        {
            if (!SamplePose(input, candidateClip, order, parentRotations[f], input.RootParents[f].Translation,
                            times[f], false, pose))
                return Fail(error, "foot_planting_nonfinite");
            bool bIkFailed = false;
            for (size_t l = 0; l < legs; ++l)
            {
                const V paw = pose.Positions[input.Legs[l].Paw];
                const Goal& goal = goals[f * legs + l];
                resultPaws[f * legs + l] = paw;
                const double vertical = settings.GroundHeightMeters - paw.Y;
                const double distance = goal.Weight > 0 ? Length(Sub(paw, goal.Position)) : 0;
                if (!std::isfinite(vertical) || !std::isfinite(distance))
                    return Fail(error, "foot_planting_nonfinite");
                candidateReport.FootPenetrationMeters = std::max(candidateReport.FootPenetrationMeters, vertical);
                if (goal.Weight == 1)
                    candidateReport.FootFloatMeters = std::max(candidateReport.FootFloatMeters, -vertical);
                if (goal.Weight > 0 && distance > 0.001)
                    bIkFailed = true;
            }
            if (bIkFailed)
            {
                if (candidateReport.IkFailedFrames == UINT32_MAX)
                    return Fail(error, "foot_planting_input");
                ++candidateReport.IkFailedFrames;
            }
        }
        candidateReport.FootSlideAfterMeters = Slide(resultPaws, legs, candidateReport);
        if (!std::isfinite(candidateReport.FootSlideAfterMeters) ||
            !std::isfinite(candidateReport.FootPenetrationMeters) || !std::isfinite(candidateReport.FootFloatMeters) ||
            !std::isfinite(candidateReport.IkAdjustmentDegrees) || !std::isfinite(candidateReport.PelvisDropMeters))
            return Fail(error, "foot_planting_nonfinite");
        candidateReport.bMeasured = true;
        clip = std::move(candidateClip);
        report = std::move(candidateReport);
        return true;
    }
} // namespace NorvesLib::Core::Animation
