#include "Animation/MotionAnalysis.h"
#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletalClipProcessing.h"
#include "Animation/SkeletonResource.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Animation
{
    namespace
    {
        void Number(Container::String& text, double value)
        {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
            for (auto p = buffer; p != result.ptr; ++p)
                text += *p;
        }
        void Quote(Container::String& text, Identity name)
        {
            text += '"';
            constexpr char hex[] = "0123456789abcdef";
            for (auto c : name.GetView())
            {
                const auto u = static_cast<unsigned char>(c);
                if (c == '"' || c == '\\')
                {
                    text += '\\';
                    text += c;
                }
                else if (u < 32)
                {
                    text += _T("\\u00");
                    text += hex[u >> 4];
                    text += hex[u & 15];
                }
                else
                    text += c;
            }
            text += '"';
        }
        struct CycleSource
        {
            const SkeletalPoseContext& Context;
            const AnimationClipResource& Clip;
            uint32_t Root;
            LocalPose Pose;
            static bool At(double time, Container::Span<Skeletal::SkeletalValue> rotations,
                           Skeletal::SkeletalPosition& position, void* opaque)
            {
                auto& self = *static_cast<CycleSource*>(opaque);
                if (!SkeletalPoseBuilder::SampleClipToLocalPose(self.Context, self.Clip, float(time), self.Pose) ||
                    rotations.size() != self.Pose.size())
                    return false;
                for (size_t i = 0; i < rotations.size(); ++i)
                {
                    const auto& q = self.Pose[i].Rotation;
                    rotations[i] = {q.x, q.y, q.z, q.w};
                }
                const auto& p = self.Pose[self.Root].Translation;
                position = {p.x, p.y, p.z};
                return true;
            }
        };
    } // namespace
    bool AnalyzeFootContacts(const SkeletalPoseContext& context, const AnimationClipResource& clip,
                             Container::Span<const FootContactSpec> feet, const FootContactOptions& options,
                             FootContactReport& out)
    {
        const double duration = clip.GetClip().DurationSeconds;
        if (feet.empty() || feet.size() > 32 || !std::isfinite(duration) || duration <= 0 ||
            !std::isfinite(options.SampleRate) || options.SampleRate <= 0 || options.SampleRate > 1000 ||
            !std::isfinite(options.HeightThreshold) || options.HeightThreshold <= 0 ||
            !std::isfinite(options.VerticalSpeedThreshold) || options.VerticalSpeedThreshold <= 0 ||
            !std::isfinite(options.MinimumWindowSeconds) || options.MinimumWindowSeconds < 0 ||
            options.MaximumSamples < 2 || options.MaximumSamples > 65536)
            return false;
        const double steps = std::ceil(duration * options.SampleRate);
        if (!std::isfinite(steps) || steps < 1 || steps + 1 > options.MaximumSamples)
            return false;
        const size_t count = size_t(steps) + 1;
        const double dt = duration / double(count - 1);
        PoseScratch scratch;
        Container::VariableArray<Math::Matrix4x4> models;
        Container::VariableArray<float> heights(count * feet.size());
        float ground = std::numeric_limits<float>::max();
        for (size_t i = 0; i < feet.size(); ++i)
        {
            if (!feet[i].Name.IsValid() || feet[i].Name.GetView().size() > 1024)
                return false;
            for (size_t j = 0; j < i; ++j)
                if (feet[i].Joint == feet[j].Joint || feet[i].Name == feet[j].Name)
                    return false;
        }
        for (size_t sample = 0; sample < count; ++sample)
        {
            if (!SkeletalPoseBuilder::SampleClipToLocalPose(context, clip, float(dt * sample), scratch.Local) ||
                !SkeletalPoseBuilder::BuildJointModelMatrices(context, scratch.Local, scratch, models))
                return false;
            for (size_t foot = 0; foot < feet.size(); ++foot)
            {
                if (feet[foot].Joint >= models.size())
                    return false;
                const float y = models[feet[foot].Joint].m31;
                if (!std::isfinite(y))
                    return false;
                heights[foot * count + sample] = y;
                ground = std::min(ground, y);
            }
        }
        FootContactReport report;
        report.GroundOffset = ground;
        report.Samples = uint32_t(count);
        report.Draft.GroundOffset = ground;
        double totalConfidence = 0;
        size_t acceptedSamples = 0;
        for (size_t foot = 0; foot < feet.size(); ++foot)
        {
            size_t begin = SIZE_MAX;
            double sum = 0;
            const auto close = [&](size_t end) {
                if (begin == SIZE_MAX)
                    return;
                const float start = float(dt * begin), finish = float(dt * end);
                const double length = dt * double(end - begin);
                const double tolerance =
                    4 * std::numeric_limits<float>::epsilon() * std::max(length, double(options.MinimumWindowSeconds));
                if (finish > start && length + tolerance >= options.MinimumWindowSeconds)
                {
                    const float confidence = float(sum / double(end - begin));
                    report.Windows.push_back({feet[foot].Name, start, finish, confidence});
                    report.Draft.Events.push_back({feet[foot].Name, start, finish});
                    totalConfidence += sum;
                    acceptedSamples += end - begin;
                }
                begin = SIZE_MAX;
                sum = 0;
            };
            for (size_t sample = 0; sample + 1 < count; ++sample)
            {
                const float y = heights[foot * count + sample];
                const double velocity = std::fabs(double(heights[foot * count + sample + 1]) - y) / dt;
                const double height = std::max(0.0, double(y) - ground);
                if (height <= options.HeightThreshold && velocity <= options.VerticalSpeedThreshold)
                {
                    if (begin == SIZE_MAX)
                        begin = sample;
                    sum +=
                        .5 * ((1 - height / options.HeightThreshold) + (1 - velocity / options.VerticalSpeedThreshold));
                }
                else
                    close(sample);
            }
            close(count - 1);
        }
        std::stable_sort(report.Windows.begin(), report.Windows.end(),
                         [](const auto& a, const auto& b) { return a.Start < b.Start; });
        std::stable_sort(report.Draft.Events.begin(), report.Draft.Events.end(),
                         [](const auto& a, const auto& b) { return a.Time < b.Time; });
        report.Confidence = acceptedSamples ? float(totalConfidence / acceptedSamples) : 0;
        ClipMetadataReport validation;
        if (!ValidateClipMetadata(report.Draft, float(duration), validation))
            return false;
        report.DraftJson = _T("{\"version\":1,\"groundOffset\":");
        Number(report.DraftJson, ground);
        report.DraftJson += _T(",\"events\":[");
        for (size_t i = 0; i < report.Windows.size(); ++i)
        {
            if (i)
                report.DraftJson += ',';
            const auto& w = report.Windows[i];
            report.DraftJson += _T("{\"name\":");
            Quote(report.DraftJson, w.Name);
            report.DraftJson += _T(",\"t\":");
            Number(report.DraftJson, w.Start);
            report.DraftJson += _T(",\"end\":");
            Number(report.DraftJson, w.End);
            report.DraftJson += '}';
        }
        report.DraftJson += _T("]}");
        out = std::move(report);
        return true;
    }
    bool DetectCycle(const SkeletonResource& skeleton, const SkeletalPoseContext& context,
                     const AnimationClipResource& clip, const CycleDetectionOptions& options, CycleDetectionReport& out)
    {
        const auto& runtime = skeleton.GetPoseRuntime();
        if (!runtime.bValid || !SkeletalPoseBuilder::IsPreparedForSkeleton(context, skeleton) ||
            !std::isfinite(options.SampleRate) || options.SampleRate <= 0 || options.SampleRate > 1000)
            return false;
        uint32_t root = UINT32_MAX;
        Container::VariableArray<uint32_t> joints;
        for (uint32_t i = 0; i < runtime.Parents.size(); ++i)
        {
            joints.push_back(i);
            if (runtime.Parents[i] < 0)
            {
                if (root != UINT32_MAX)
                    return false;
                root = i;
            }
        }
        if (root == UINT32_MAX)
            return false;
        CycleSource evaluator{context, clip, root};
        if (!SkeletalPoseBuilder::SampleClipToLocalPose(context, clip, 0, evaluator.Pose) ||
            evaluator.Pose.size() != joints.size())
            return false;
        SkeletalClipPoseSource source;
        source.Name = clip.GetClip().Name;
        source.DurationSeconds = clip.GetClip().DurationSeconds;
        source.SourceIntervalSeconds = 1 / options.SampleRate;
        source.JointIndices = joints;
        source.RootJoint = root;
        const auto& rest =
            runtime.AuthorRestLocal.size() == joints.size() ? runtime.AuthorRestLocal[root] : evaluator.Pose[root];
        source.RootRest.Translation = {rest.Translation.x, rest.Translation.y, rest.Translation.z};
        source.RootRest.Rotation = {rest.Rotation.x, rest.Rotation.y, rest.Rotation.z, rest.Rotation.w};
        source.RootRest.Scale = {rest.Scale.x, rest.Scale.y, rest.Scale.z};
        if (runtime.bSplit)
            for (size_t i = 0; i < 16; ++i)
                source.RootFrame[i] = runtime.RootFrame.values[i];
        source.Context = &evaluator;
        source.Sample = CycleSource::At;
        SkeletalClipProcessingSettings settings;
        settings.OutputFps = options.SampleRate;
        settings.MinimumPeriod = options.MinimumPeriod;
        settings.MaximumPeriod = options.MaximumPeriod;
        settings.LoopRmsThresholdRadians = options.RmsThresholdRadians;
        settings.MinimumMotionRadians = options.MinimumMotionRadians;
        settings.MaximumSamples = options.MaximumSamples;
        settings.MaximumPoseEvaluations = options.MaximumPoseEvaluations;
        settings.bExtractRootMotion = false;
        Skeletal::SkeletalAnimationClip ignored;
        SkeletalClipProcessingReport result;
        if (ProcessSkeletalClip(source, settings, ignored, result) != SkeletalClipProcessingStatus::Success)
            return false;
        CycleDetectionReport report;
        report.bDetected = result.bLoopDetected;
        report.Start = result.StartSeconds;
        report.End = result.EndSeconds;
        report.Period = result.PeriodSeconds;
        report.RmsRadians = result.RecurrenceRmsRadians;
        report.Confidence =
            report.bDetected ? std::clamp(1 - report.RmsRadians / options.RmsThresholdRadians, 0.0, 1.0) : 0;
        report.DraftJson = _T("{\"version\":1,\"loop\":");
        if (report.bDetected)
        {
            report.Draft.Loop = {true, float(report.Start), float(report.End)};
            report.DraftJson += _T("{\"start\":");
            Number(report.DraftJson, report.Draft.Loop.Start);
            report.DraftJson += _T(",\"end\":");
            Number(report.DraftJson, report.Draft.Loop.End);
            report.DraftJson += '}';
        }
        else
            report.DraftJson += _T("null");
        report.DraftJson += '}';
        out = std::move(report);
        return true;
    }
} // namespace NorvesLib::Core::Animation
