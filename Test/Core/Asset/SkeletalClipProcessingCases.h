#pragma once
#include "Animation/SkeletalClipProcessing.h"
#include "Animation/SkeletalSamplingMath.h"
namespace NorvesLib::Tests::ClipProcessingCases
{
    namespace C = Core::Container;
    namespace A = Core::Animation;
    namespace S = Core::Skeletal;
    constexpr double Pi = 3.14159265358979323846;
    struct Source
    {
        bool bPeriodic = true, bSwing = false;
        static bool Sample(double time, C::Span<S::SkeletalValue> rotations, S::SkeletalPosition& position,
                           void* context)
        {
            const auto& source = *static_cast<Source*>(context);
            const auto at = [&](double t)
            {
                const double angle = source.bPeriodic ? 0.6 * std::sin(2 * Pi * t / (11.6 / 16.0)) : 0.5 * t;
                return NorvesLib::Math::Quaternion(0, 0, float(std::sin(angle * 0.5)), float(std::cos(angle * 0.5)));
            };
            // 16fpsのsource local quaternionを補間する。Euler値の補間はしない。
            const double frame = std::floor(time * 16), alpha = time * 16 - frame;
            const auto child = A::Detail::Slerp(at(frame / 16), at((frame + 1) / 16), float(alpha));
            const double heading = 0.8 + 0.4 * time;
            rotations[0] = {0, float(std::sin(heading * 0.5)), 0, float(std::cos(heading * 0.5))};
            if (source.bSwing)
            {
                const double angle = Pi / 3 * std::abs(2 * time - 1);
                rotations[0] = time <= 0.5
                                   ? S::SkeletalValue{0, 0, float(std::sin(angle / 2)), float(std::cos(angle / 2))}
                                   : S::SkeletalValue{float(std::sin(angle / 2)), 0, 0, float(std::cos(angle / 2))};
            }
            rotations[1] = {child.x, child.y, child.z, child.w};
            position = {float(time * 2), float(1 + 0.1 * std::sin(2 * Pi * time / (11.6 / 16.0))), float(time)};
            return true;
        }
    };
    inline void Run()
    {
        Source fixture;
        const uint32_t joints[] = {0, 1};
        const double weights[] = {0, 1};
        A::SkeletalClipPoseSource source;
        source.Name = C::String("Run");
        source.DurationSeconds = 2.6;
        source.SourceIntervalSeconds = 1.0 / 16;
        source.JointIndices = joints;
        source.RotationWeights = weights;
        source.RootJoint = 0;
        source.RootRest.Translation.Y = 1;
        source.Context = &fixture;
        source.Sample = Source::Sample;
        A::SkeletalClipProcessingSettings settings;
        S::SkeletalAnimationClip clip;
        A::SkeletalClipProcessingReport report;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(report.bLoopDetected && std::abs(report.PeriodSeconds * 16 - 11.6) < 0.15);
        CHECK(clip.Channels.size() == 3 && clip.RootMotionJoint == 0 && !clip.RootMotion.empty());
        CHECK(clip.DurationSeconds == clip.Channels[0].Samples.back().TimeSeconds &&
              std::abs(double(clip.DurationSeconds) - report.PeriodSeconds) < 1e-6);
        for (const auto& channel : clip.Channels)
        {
            const auto& first = channel.Samples.front().Value;
            const auto& last = channel.Samples.back().Value;
            CHECK(first.X == last.X && first.Y == last.Y && first.Z == last.Z && first.W == last.W);
        }
        CHECK(report.SeamAfterRadians <= 0.5 * Pi / 180);
        CHECK(std::abs(clip.RootMotion.back().YawRadians - report.PeriodSeconds * 0.4) < 1e-5);
        CHECK(std::abs(std::hypot(clip.RootMotion.back().TranslationX, clip.RootMotion.back().TranslationZ) -
                       std::sqrt(5.0) * report.PeriodSeconds) < 1e-5);
        CHECK(std::abs(report.AverageSpeedMetersPerSecond - std::sqrt(5.0)) < 1e-4);
        CHECK(std::abs(clip.Channels[0].Samples[1].Value.Y) < 1e-5);
        for (double cycles : {2.0, 4.0})
        {
            source.DurationSeconds = cycles * 11.6 / 16;
            CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
            CHECK(report.bLoopDetected && std::abs(report.PeriodSeconds * 16 - 11.6) < 0.15);
        }
        source.DurationSeconds = 2.6;
        settings.MaximumPeriod = 11.6 / 16;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(report.bLoopDetected && std::abs(report.PeriodSeconds * 16 - 11.6) < 0.15);
        settings.MaximumPeriod = 3;
        settings.MinimumPeriod = 11.6 / 16;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(report.bLoopDetected && std::abs(report.PeriodSeconds * 16 - 11.6) < 0.15);
        settings.MinimumPeriod = 0.15;
        settings.Loop = A::SkeletalLoopSelection::None;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(!report.bLoopDetected && clip.RootMotion.size() == 79);
        CHECK(std::abs(clip.Channels.back().Samples[5].Value.Y - 1) > 0.02);
        CHECK(std::abs(clip.RootMotion.back().YawRadians - 1.04) < 1e-5);
        settings.Loop = A::SkeletalLoopSelection::Auto;
        fixture.bPeriodic = false;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(!report.bLoopDetected && clip.DurationSeconds == float(source.DurationSeconds));
        settings.Loop = A::SkeletalLoopSelection::Range;
        settings.RangeStart = 0.125;
        settings.RangeEnd = 1.125;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(report.bRangeSelected && clip.DurationSeconds == 1 && clip.RootMotion.size() == 31);
        CHECK(std::abs(clip.RootMotion.back().YawRadians - 0.4) < 1e-5);
        const double excluded[] = {0, 0};
        source.RotationWeights = excluded;
        for (auto mode :
             {A::SkeletalLoopSelection::None, A::SkeletalLoopSelection::Range, A::SkeletalLoopSelection::Auto})
        {
            settings.Loop = mode;
            CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
            CHECK(!report.bLoopDetected && report.SeamVelocityDifferenceRadiansPerSecond == 0);
        }
        source.RotationWeights = {};
        settings.Loop = A::SkeletalLoopSelection::Range;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        const auto held = clip.RootMotion.back().YawRadians;
        settings.RangeEnd = 99;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) ==
              A::SkeletalClipProcessingStatus::InvalidSettings);
        CHECK(clip.DurationSeconds == 1 && clip.RootMotion.back().YawRadians == held);
        settings.RangeEnd = 1.125;
        settings.MaximumPoseEvaluations = 2;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::LimitExceeded);
        CHECK(clip.DurationSeconds == 1 && clip.RootMotion.back().YawRadians == held);
        settings.MaximumPoseEvaluations = 262144;
        settings.Loop = A::SkeletalLoopSelection::None;
        source.RootFrame = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 3, 0, 5, 1};
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(std::abs(clip.RootMotion.back().YawRadians - 1.04) < 1e-5);
        CHECK(std::abs(clip.Channels[0].Samples[1].Value.Y) < 1e-5);
        CHECK(std::abs(clip.Channels.back().Samples[5].Value.Y - 1) > 0.02);
        source.RootFrame = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1};
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        source.RootFrame = S::IdentityRigRootFrame();
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        const auto rootMotion = clip.RootMotion;
        const auto y = clip.Channels.back().Samples[5].Value.Y;
        source.RootFrame[12] = 100000000;
        source.RootFrame[13] = 100000000;
        source.RootFrame[14] = -100000000;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        CHECK(std::abs(clip.RootMotion.back().TranslationX - rootMotion.back().TranslationX) < 1e-9 &&
              std::abs(clip.RootMotion.back().TranslationZ - rootMotion.back().TranslationZ) < 1e-9 &&
              clip.Channels.back().Samples[5].Value.Y == y);
        source.RootFrame = S::IdentityRigRootFrame();
        fixture.bSwing = true;
        source.DurationSeconds = 1;
        settings.Loop = A::SkeletalLoopSelection::Range;
        settings.RangeStart = 0;
        settings.RangeEnd = 1;
        CHECK(A::ProcessSkeletalClip(source, settings, clip, report) == A::SkeletalClipProcessingStatus::Success);
        for (const auto& key : clip.Channels[0].Samples)
        {
            CHECK(std::abs(key.Value.Y) < 1e-5);
        }
        CHECK(clip.RootMotion.back().YawRadians == 0);
    }
} // namespace NorvesLib::Tests::ClipProcessingCases
