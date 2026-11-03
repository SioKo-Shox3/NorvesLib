// 動画由来の外れ値除去・周期平均・ループ・平滑を正解の分かる合成歩容で確かめる。
#include "Animation/SkeletalClipProcessing.h"
#include "Animation/SkeletalSamplingMath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Motion processing check failed %s:%d %s\n", __FILE__, __LINE__, #x);                 \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

namespace
{
    namespace A = NorvesLib::Core::Animation;
    namespace C = NorvesLib::Core::Container;
    namespace S = NorvesLib::Core::Skeletal;
    using Q = NorvesLib::Math::Quaternion;
    constexpr double Pi = 3.14159265358979323846;
    constexpr double Fps = 16;
    constexpr double PeriodFrames = 12.5;
    constexpr double Period = PeriodFrames / Fps;
    constexpr size_t SpikeFrames[] = {8, 23, 39};

    Q Rotation(double angle)
    {
        return Q(0, 0, float(std::sin(angle / 2)), float(std::cos(angle / 2)));
    }
    Q Quaternion(const S::SkeletalValue& value)
    {
        return A::Detail::NormalizeQuaternion(Q(value.X, value.Y, value.Z, value.W));
    }
    double Distance(const S::SkeletalValue& a, const Q& b)
    {
        const auto q = Quaternion(a);
        const double dot = double(q.x) * b.x + double(q.y) * b.y + double(q.z) * b.z + double(q.w) * b.w;
        const double aa = double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w;
        const double bb = double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z + double(b.w) * b.w;
        return 2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0.0, 1.0));
    }
    struct MotionSource
    {
        C::VariableArray<Q> Frames;
        uint32_t Joints[2] = {0, 1};
        double Weights[2] = {0, 1};

        explicit MotionSource(bool bSpikes)
        {
            uint32_t state = 17;
            // 4周期、50区間。雑音は周期に同期せず、同じ入力を常に再現する。
            for (size_t frame = 0; frame <= 50; ++frame)
            {
                state = state * 1664525u + 1013904223u;
                const double noise = (double(state >> 8) / 16777215.0 * 2 - 1) * 0.006;
                double angle = 0.6 * std::sin(2 * Pi * frame / PeriodFrames) + noise;
                for (size_t spike : SpikeFrames)
                {
                    if (bSpikes && frame == spike)
                    {
                        angle += 50 * Pi / 180;
                    }
                }
                Frames.push_back(Rotation(angle));
            }
        }
        static bool Sample(double time, C::Span<S::SkeletalValue> rotations, S::SkeletalPosition& position,
                           void* context)
        {
            const auto& source = *static_cast<MotionSource*>(context);
            const double index = std::clamp(time * Fps, 0.0, double(source.Frames.size() - 1));
            const size_t left = size_t(index), right = std::min(left + 1, source.Frames.size() - 1);
            const auto q = A::Detail::Slerp(source.Frames[left], source.Frames[right], float(index - left));
            rotations[0] = {0, 0, 0, 1};
            rotations[1] = {q.x, q.y, q.z, q.w};
            position = {0, 1, 0};
            return true;
        }
        A::SkeletalClipPoseSource Source()
        {
            A::SkeletalClipPoseSource source;
            source.Name = C::String("SyntheticGait");
            source.DurationSeconds = 4 * Period;
            source.SourceIntervalSeconds = 1 / Fps;
            source.JointIndices = Joints;
            source.RotationWeights = Weights;
            source.RootJoint = 0;
            source.RootRest.Translation.Y = 1;
            source.Context = this;
            source.Sample = Sample;
            return source;
        }
    };
    A::SkeletalClipProcessingSettings Settings()
    {
        A::SkeletalClipProcessingSettings settings;
        settings.OutputFps = Fps;
        return settings;
    }
    struct Result
    {
        S::SkeletalAnimationClip Clip;
        A::SkeletalClipProcessingReport Report;
    };
    Result Process(MotionSource& source, const A::SkeletalClipProcessingSettings& settings)
    {
        Result result;
        CHECK(A::ProcessSkeletalClip(source.Source(), settings, result.Clip, result.Report) ==
              A::SkeletalClipProcessingStatus::Success);
        return result;
    }
    // 30 Hzの複数軸回転を補間し、入力表現の符号だけをコマごとに反転する。
    struct SampledMotionSource
    {
        C::VariableArray<Q> Frames;
        uint32_t Joints[2] = {0, 1};
        static constexpr double InputFps = 30;
        static constexpr double Duration = 2;

        SampledMotionSource()
        {
            for (size_t frame = 0; frame <= 60; ++frame)
            {
                const double phase = 2 * Pi * frame / 60;
                const double yaw = (-89 + 9 * std::cos(phase)) * Pi / 180;
                const double pitch = 0.2 * std::sin(phase), roll = 0.15 * std::cos(phase);
                const Q y(0, float(std::sin(yaw / 2)), 0, float(std::cos(yaw / 2)));
                const Q x(float(std::sin(pitch / 2)), 0, 0, float(std::cos(pitch / 2)));
                Frames.push_back(A::Detail::NormalizeQuaternion(y * x * Rotation(roll)));
            }
        }
        static bool Sample(double time, C::Span<S::SkeletalValue> rotations, S::SkeletalPosition& position,
                           void* context)
        {
            const auto& source = *static_cast<SampledMotionSource*>(context);
            const double index = std::clamp(time * InputFps, 0.0, double(source.Frames.size() - 1));
            const size_t left = size_t(index), right = std::min(left + 1, source.Frames.size() - 1);
            auto child = A::Detail::Slerp(source.Frames[left], source.Frames[right], float(index - left));
            if (size_t(std::floor(index + 1e-6)) % 2)
            {
                child = Q(-child.x, -child.y, -child.z, -child.w);
            }
            const double yaw = 0.8 + 0.9 * time, pitch = 0.25 * std::sin(Pi * time);
            const Q root = Q(0, float(std::sin(yaw / 2)), 0, float(std::cos(yaw / 2))) *
                           Q(float(std::sin(pitch / 2)), 0, 0, float(std::cos(pitch / 2)));
            rotations[0] = {root.x, root.y, root.z, root.w};
            rotations[1] = {child.x, child.y, child.z, child.w};
            position = {float(2 * time), 1, float(time)};
            return true;
        }
        A::SkeletalClipPoseSource Source()
        {
            A::SkeletalClipPoseSource source;
            source.Name = C::String("SampledMotion");
            source.DurationSeconds = Duration;
            source.SourceIntervalSeconds = 1 / InputFps;
            source.JointIndices = Joints;
            source.RootJoint = 0;
            source.RootRest.Translation.Y = 1;
            source.Context = this;
            source.Sample = Sample;
            return source;
        }
    };
    Result Process(const A::SkeletalClipPoseSource& source, const A::SkeletalClipProcessingSettings& settings)
    {
        Result result;
        CHECK(A::ProcessSkeletalClip(source, settings, result.Clip, result.Report) ==
              A::SkeletalClipProcessingStatus::Success);
        return result;
    }
    void TestQuaternionContinuity()
    {
        SampledMotionSource source;
        auto settings = Settings();
        settings.OutputFps = SampledMotionSource::InputFps;
        settings.Loop = A::SkeletalLoopSelection::None;
        settings.bExtractRootMotion = false;
        const auto result = Process(source.Source(), settings);
        const auto& keys = result.Clip.Channels[1].Samples;
        CHECK(keys.size() == source.Frames.size());
        double maximumAngle = 0, maximumError = 0;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            C::VariableArray<S::SkeletalValue> input(2);
            S::SkeletalPosition position;
            CHECK(SampledMotionSource::Sample(keys[i].TimeSeconds, input, position, &source));
            const double error = Distance(keys[i].Value, Quaternion(input[1]));
            maximumError = std::max(maximumError, error);
            CHECK(error <= 1e-4);
            if (i)
            {
                const auto a = Quaternion(keys[i - 1].Value), b = Quaternion(keys[i].Value);
                const double dot = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w;
                const double aa = double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z + double(a.w) * a.w;
                const double bb = double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z + double(b.w) * b.w;
                const double angle = 2 * std::acos(std::clamp(dot / std::sqrt(aa * bb), -1.0, 1.0));
                maximumAngle = std::max(maximumAngle, angle);
                CHECK(dot >= 0);
                CHECK(angle <= Pi);
            }
        }
        std::fprintf(stderr, "MOTION_CONTINUITY max_signed_angle=%.9f max_error=%.9f\n", maximumAngle, maximumError);
    }
    struct ResampledMotionSource
    {
        const S::SkeletalAnimationClip& Clip;
        static bool Sample(double time, C::Span<S::SkeletalValue> rotations, S::SkeletalPosition& position,
                           void* context)
        {
            const auto& clip = static_cast<ResampledMotionSource*>(context)->Clip;
            for (size_t joint = 0; joint < rotations.size(); ++joint)
            {
                const auto& keys = clip.Channels[joint].Samples;
                size_t right = 1;
                while (right + 1 < keys.size() && keys[right].TimeSeconds < time)
                {
                    ++right;
                }
                const auto& a = keys[right - 1];
                const auto& b = keys[right];
                const float alpha = float(std::clamp((time - a.TimeSeconds) / (b.TimeSeconds - a.TimeSeconds), 0.0, 1.0));
                const auto q = A::Detail::Slerp(Quaternion(a.Value), Quaternion(b.Value), alpha);
                rotations[joint] = {q.x, q.y, q.z, q.w};
            }
            position = {0, 1, 0};
            return true;
        }
    };
    void TestResamplingRoundTrip()
    {
        SampledMotionSource source;
        auto settings = Settings();
        settings.Loop = A::SkeletalLoopSelection::None;
        settings.bExtractRootMotion = false;
        const auto downsampled = Process(source.Source(), settings);
        CHECK(downsampled.Clip.Channels[1].Samples.size() == 33);
        ResampledMotionSource resampled{downsampled.Clip};
        auto input = source.Source();
        input.DurationSeconds = downsampled.Clip.DurationSeconds;
        input.SourceIntervalSeconds = 1 / settings.OutputFps;
        input.Context = &resampled;
        input.Sample = ResampledMotionSource::Sample;
        settings.OutputFps = SampledMotionSource::InputFps;
        const auto restored = Process(input, settings);
        CHECK(restored.Clip.Channels[1].Samples.size() == source.Frames.size());
        double maximumError = 0;
        // 端の除外はせず、元の30 Hzの全61コマと両関節を比較する。
        for (size_t frame = 0; frame < source.Frames.size(); ++frame)
        {
            C::VariableArray<S::SkeletalValue> expected(2);
            S::SkeletalPosition position;
            CHECK(SampledMotionSource::Sample(frame / SampledMotionSource::InputFps, expected, position, &source));
            for (size_t joint = 0; joint < expected.size(); ++joint)
            {
                const auto& key = restored.Clip.Channels[joint].Samples[frame];
                CHECK(key.TimeSeconds == float(frame / SampledMotionSource::InputFps));
                const double error = Distance(key.Value, Quaternion(expected[joint]));
                maximumError = std::max(maximumError, error);
                CHECK(error <= 0.5 * Pi / 180);
            }
        }
        std::fprintf(stderr, "MOTION_RESAMPLE max_error_degrees=%.9f\n", maximumError * 180 / Pi);
    }
    void TestTimeScale()
    {
        SampledMotionSource source;
        auto settings = Settings();
        settings.Loop = A::SkeletalLoopSelection::None;
        settings.bExtractRootMotion = true;
        const auto original = Process(source.Source(), settings);
        settings.TimeScale = 2;
        const auto stretched = Process(source.Source(), settings);
        CHECK(original.Clip.DurationSeconds == SampledMotionSource::Duration);
        CHECK(stretched.Clip.DurationSeconds == 2 * original.Clip.DurationSeconds);
        CHECK(std::abs(original.Report.PlanarDistanceMeters - std::sqrt(5.0) * SampledMotionSource::Duration) <= 1e-6);
        CHECK(std::abs(stretched.Report.PlanarDistanceMeters - original.Report.PlanarDistanceMeters) <= 1e-6);
        CHECK(std::abs(stretched.Report.AverageSpeedMetersPerSecond - original.Report.AverageSpeedMetersPerSecond / 2) <=
              1e-6);
        std::fprintf(stderr, "MOTION_TIME duration=%.9f stretched=%.9f distance=%.9f stretched_distance=%.9f "
                             "speed=%.9f stretched_speed=%.9f\n",
                     original.Clip.DurationSeconds, stretched.Clip.DurationSeconds, original.Report.PlanarDistanceMeters,
                     stretched.Report.PlanarDistanceMeters, original.Report.AverageSpeedMetersPerSecond,
                     stretched.Report.AverageSpeedMetersPerSecond);
    }
    void TestExtractedHeading()
    {
        SampledMotionSource source;
        auto settings = Settings();
        settings.Loop = A::SkeletalLoopSelection::None;
        settings.bExtractRootMotion = true;
        const auto result = Process(source.Source(), settings);
        CHECK(result.Clip.Channels[0].JointIndex == source.Joints[0]);
        CHECK(result.Clip.Channels[0].Path == S::SkeletalAnimationPath::Rotation);
        CHECK(!result.Clip.RootMotion.empty());
        CHECK(std::abs(result.Clip.RootMotion.back().YawRadians - 0.9 * SampledMotionSource::Duration) <= 1e-6);
        double maximumHeading = 0;
        for (const auto& key : result.Clip.Channels[0].Samples)
        {
            const auto q = Quaternion(key.Value);
            // +Yのtwistでheadingを測り、残したpitchから独立に判定する。
            CHECK(std::hypot(double(q.y), double(q.w)) > 1e-12);
            const double heading = std::remainder(2 * std::atan2(double(q.y), double(q.w)), 2 * Pi);
            maximumHeading = std::max(maximumHeading, std::abs(heading));
            CHECK(std::abs(heading) <= 1e-4);
        }
        std::fprintf(stderr, "MOTION_HEADING max_absolute_yaw=%.9f\n", maximumHeading);
    }
    void CheckPeriod(const Result& result)
    {
        CHECK(result.Report.bLoopDetected);
        CHECK(std::abs(result.Report.PeriodSeconds * Fps - PeriodFrames) <= 0.05);
    }
    void TestSpikes()
    {
        MotionSource source(true);
        auto settings = Settings();
        settings.SpikeThresholdRadians = 10 * Pi / 180;
        const auto periodic = Process(source, settings);
        std::fprintf(stderr, "MOTION_SPIKES replaced=%u period_frames=%.9f\n", periodic.Report.ReplacedSpikes,
                     periodic.Report.PeriodSeconds * Fps);
        CHECK(periodic.Report.ReplacedSpikes == 3);
        CheckPeriod(periodic);

        settings.Loop = A::SkeletalLoopSelection::None;
        const auto cleaned = Process(source, settings);
        settings.SpikeThresholdRadians = 0;
        const auto original = Process(source, settings);
        CHECK(cleaned.Report.ReplacedSpikes == 3);
        CHECK(original.Report.ReplacedSpikes == 0);
        const auto& keys = cleaned.Clip.Channels[1].Samples;
        CHECK(keys.size() == source.Frames.size());
        for (size_t frame = 0; frame < keys.size(); ++frame)
        {
            CHECK(keys[frame].TimeSeconds == float(frame / Fps));
            CHECK(Distance(original.Clip.Channels[1].Samples[frame].Value, source.Frames[frame]) < 1e-6);
            bool bSpike = false;
            for (size_t spike : SpikeFrames)
            {
                bSpike |= frame == spike;
            }
            if (bSpike)
            {
                const auto expected = A::Detail::Slerp(source.Frames[frame - 1], source.Frames[frame + 1], 0.5f);
                CHECK(Distance(keys[frame].Value, expected) <= Pi / 180);
                CHECK(Distance(original.Clip.Channels[1].Samples[frame].Value, expected) > 40 * Pi / 180);
            }
            else
            {
                CHECK(Distance(keys[frame].Value, source.Frames[frame]) < 1e-6);
            }
        }
    }
    void CheckLoop(const Result& result)
    {
        CheckPeriod(result);
        for (const auto& channel : result.Clip.Channels)
        {
            const auto& keys = channel.Samples;
            const auto& first = keys.front().Value;
            const auto& last = keys.back().Value;
            CHECK(first.X == last.X && first.Y == last.Y && first.Z == last.Z && first.W == last.W);
            if (channel.Path != S::SkeletalAnimationPath::Rotation)
            {
                continue;
            }
            C::VariableArray<double> speeds;
            for (size_t i = 1; i < keys.size(); ++i)
            {
                speeds.push_back(Distance(keys[i - 1].Value, Quaternion(keys[i].Value)) /
                                 (keys[i].TimeSeconds - keys[i - 1].TimeSeconds));
            }
            const double incoming = speeds.back(), outgoing = speeds.front();
            std::sort(speeds.begin(), speeds.end());
            const size_t middle = speeds.size() / 2;
            const double median = speeds.size() % 2 ? speeds[middle] : (speeds[middle - 1] + speeds[middle]) / 2;
            CHECK(incoming <= 1.5 * median);
            CHECK(outgoing <= 1.5 * median);
        }
    }
    void TestLoopAndAverageCycles()
    {
        MotionSource source(false);
        auto settings = Settings();
        const auto single = Process(source, settings);
        settings.bAverageCycles = true;
        const auto averaged = Process(source, settings);
        // 検出した開始から入力末尾までに完全に収まる周期だけを数える。
        const uint32_t expectedCycles = uint32_t(std::floor(
            (source.Source().DurationSeconds - averaged.Report.StartSeconds) / averaged.Report.PeriodSeconds));
        std::fprintf(stderr, "MOTION_LOOP cycles=%u expected=%u start=%.9f seam_single=%.9f seam_average=%.9f\n",
                     averaged.Report.AveragedCycles, expectedCycles, averaged.Report.StartSeconds,
                     single.Report.SeamBeforeRadians, averaged.Report.SeamBeforeRadians);
        CHECK(expectedCycles >= 2);
        CHECK(averaged.Report.AveragedCycles == expectedCycles);
        CHECK(averaged.Report.SeamBeforeRadians < single.Report.SeamBeforeRadians);
        CheckLoop(single);
        CheckLoop(averaged);
    }
    double Roughness(const Result& result)
    {
        const auto& keys = result.Clip.Channels[1].Samples;
        double sum = 0;
        for (size_t i = 1; i + 1 < keys.size(); ++i)
        {
            const auto angle = [](const S::SkeletalValue& q) { return 2 * std::atan2(double(q.Z), double(q.W)); };
            const double second = angle(keys[i - 1].Value) - 2 * angle(keys[i].Value) + angle(keys[i + 1].Value);
            sum += second * second;
        }
        return sum;
    }
    void TestSmoothing()
    {
        MotionSource source(false);
        auto settings = Settings();
        const auto original = Process(source, settings);
        settings.SmoothingRadius = 2;
        const auto smoothed = Process(source, settings);
        std::fprintf(stderr, "MOTION_SMOOTH roughness_before=%.9f roughness_after=%.9f channels=%u\n",
                     Roughness(original), Roughness(smoothed), smoothed.Report.SmoothedChannels);
        CHECK(smoothed.Report.SmoothedChannels > 0);
        CHECK(original.Report.SmoothedChannels == 0);
        CHECK(Roughness(smoothed) < Roughness(original));
        CheckPeriod(original);
        CheckPeriod(smoothed);
        CHECK(std::abs(smoothed.Report.PeriodSeconds - original.Report.PeriodSeconds) * Fps <= 0.05);
    }
} // namespace

int main()
{
    TestSpikes();
    TestLoopAndAverageCycles();
    TestSmoothing();
    TestQuaternionContinuity();
    TestResamplingRoundTrip();
    TestTimeScale();
    TestExtractedHeading();
    std::puts("MOTION_PROCESSING result=pass spikes_loop_average_cycles_smoothing_continuity_resample_time_heading");
    return 0;
}
