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
    bool DetectFootContactWindows(Container::Span<const float> heights, double sampleInterval,
                                  const FootContactOptions& options,
                                  Container::VariableArray<FootContactSampleWindow>& out)
    {
        if (heights.size() < 2 || heights.size() - 1 > UINT32_MAX ||
            !std::isfinite(sampleInterval) || sampleInterval <= 0 ||
            !std::isfinite(options.HeightThreshold) || options.HeightThreshold <= 0 ||
            !std::isfinite(options.VerticalSpeedThreshold) || options.VerticalSpeedThreshold <= 0 ||
            !std::isfinite(options.MinimumWindowSeconds) || options.MinimumWindowSeconds < 0 ||
            !std::isfinite(options.Hysteresis) || options.Hysteresis < 1)
        {
            return false;
        }
        float minimum = std::numeric_limits<float>::max();
        for (const float height : heights)
        {
            if (!std::isfinite(height))
            {
                return false;
            }
            minimum = std::min(minimum, height);
        }
        Container::VariableArray<FootContactSampleWindow> windows;
        size_t begin = SIZE_MAX;
        double sum = 0;
        const auto close = [&](size_t end) {
            if (begin == SIZE_MAX)
            {
                return;
            }
            const double length = sampleInterval * double(end - begin);
            const double tolerance =
                4 * std::numeric_limits<float>::epsilon() * std::max(length, double(options.MinimumWindowSeconds));
            if (length + tolerance >= options.MinimumWindowSeconds &&
                float(sampleInterval * end) > float(sampleInterval * begin))
            {
                windows.push_back({uint32_t(begin), uint32_t(end), float(sum / double(end - begin))});
            }
            begin = SIZE_MAX;
            sum = 0;
        };
        for (size_t i = 0; i + 1 < heights.size(); ++i)
        {
            const double height = double(heights[i]) - minimum;
            const double velocity = std::fabs(double(heights[i + 1]) - heights[i]) / sampleInterval;
            const double hysteresis = begin == SIZE_MAX ? 1 : options.Hysteresis;
            if (height <= options.HeightThreshold * hysteresis && velocity <= options.VerticalSpeedThreshold * hysteresis)
            {
                if (begin == SIZE_MAX)
                {
                    begin = i;
                }
                sum += .5 * (std::clamp(1 - height / options.HeightThreshold, 0.0, 1.0) +
                             std::clamp(1 - velocity / options.VerticalSpeedThreshold, 0.0, 1.0));
            }
            else
            {
                close(i);
            }
        }
        close(heights.size() - 1);
        out = std::move(windows);
        return true;
    }
    Identity FootContactEventName(Identity leg, bool bDown)
    {
        Container::AnsiString name = "Foot.";
        for (const auto c : leg.GetView())
        {
            name += c;
        }
        name += bDown ? ".Down" : ".Up";
        return Identity(name.c_str());
    }
    bool AppendFootContactMetadata(Container::Span<const FootContactWindow> windows,
                                   Container::Span<const Identity> legs, float duration, bool bLoop,
                                   ClipMetadata& metadata, uint32_t& coincidentMarkersOmitted)
    {
        if (!std::isfinite(duration) || duration <= 0)
        {
            return false;
        }
        for (const auto& window : windows)
        {
            if (!std::isfinite(window.Start) || !std::isfinite(window.End) ||
                window.Start < 0 || window.End < window.Start || window.End > duration ||
                std::find(legs.begin(), legs.end(), window.Name) == legs.end())
            {
                return false;
            }
        }
        coincidentMarkersOmitted = 0;
        if (bLoop)
        {
            if (!metadata.Markers.empty())
            {
                return true;
            }
            for (const auto leg : legs)
            {
                const FootContactWindow* largest = nullptr;
                const FootContactWindow* first = nullptr;
                const FootContactWindow* last = nullptr;
                for (const auto& window : windows)
                {
                    if (window.Name != leg)
                    {
                        continue;
                    }
                    if (!largest || window.End - window.Start > largest->End - largest->Start)
                    {
                        largest = &window;
                    }
                    if (window.Start == 0)
                    {
                        first = &window;
                    }
                    if (window.End == duration)
                    {
                        last = &window;
                    }
                }
                if (!largest)
                {
                    continue;
                }
                float down = largest->Start, up = largest->End;
                if (first && last && first != last &&
                    duration - last->Start + first->End >= largest->End - largest->Start)
                {
                    down = last->Start;
                    up = first->End;
                }
                metadata.Markers.push_back({FootContactEventName(leg, true), down});
                metadata.Markers.push_back({FootContactEventName(leg, false), up >= duration ? 0 : up});
            }
            std::stable_sort(metadata.Markers.begin(), metadata.Markers.end(),
                             [](const auto& a, const auto& b) { return a.Time < b.Time; });
            for (size_t i = 1; i < metadata.Markers.size();)
            {
                if (metadata.Markers[i].Time == metadata.Markers[i - 1].Time)
                {
                    metadata.Markers.erase(metadata.Markers.begin() + i);
                    ++coincidentMarkersOmitted;
                }
                else
                {
                    ++i;
                }
            }
        }
        else
        {
            for (const auto& window : windows)
            {
                metadata.Events.push_back({FootContactEventName(window.Name, true), window.Start});
                if (window.End < duration)
                {
                    metadata.Events.push_back({FootContactEventName(window.Name, false), window.End});
                }
            }
            std::stable_sort(metadata.Events.begin(), metadata.Events.end(),
                             [](const auto& a, const auto& b) { return a.Time < b.Time; });
        }
        return true;
    }
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
            !std::isfinite(options.Hysteresis) || options.Hysteresis < 1 ||
            options.MaximumSamples < 2 || options.MaximumSamples > 65536)
        {
            return false;
        }
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
        Container::VariableArray<Identity> legs;
        for (size_t foot = 0; foot < feet.size(); ++foot)
        {
            legs.push_back(feet[foot].Name);
            Container::VariableArray<FootContactSampleWindow> windows;
            if (!DetectFootContactWindows({heights.data() + foot * count, count}, dt, options, windows))
            {
                return false;
            }
            for (const auto& window : windows)
            {
                report.Windows.push_back({feet[foot].Name, float(dt * window.Begin), float(dt * window.End),
                                          window.Confidence});
                const size_t samples = window.End - window.Begin;
                totalConfidence += double(window.Confidence) * samples;
                acceptedSamples += samples;
            }
        }
        std::stable_sort(report.Windows.begin(), report.Windows.end(),
                         [](const auto& a, const auto& b) { return a.Start < b.Start; });
        report.Confidence = acceptedSamples ? float(totalConfidence / acceptedSamples) : 0;
        uint32_t omitted = 0;
        if (!AppendFootContactMetadata(report.Windows, legs, float(duration), clip.GetClipMetadata().Loop.bEnabled,
                                       report.Draft, omitted))
        {
            return false;
        }
        ClipMetadataReport validation;
        if (!ValidateClipMetadata(report.Draft, float(duration), validation))
        {
            return false;
        }
        report.DraftJson = _T("{\"version\":1,\"groundOffset\":");
        Number(report.DraftJson, ground);
        report.DraftJson += _T(",\"events\":[");
        for (size_t i = 0; i < report.Draft.Events.size(); ++i)
        {
            if (i)
            {
                report.DraftJson += ',';
            }
            const auto& event = report.Draft.Events[i];
            report.DraftJson += _T("{\"name\":");
            Quote(report.DraftJson, event.Name);
            report.DraftJson += _T(",\"t\":");
            Number(report.DraftJson, event.Time);
            report.DraftJson += '}';
        }
        report.DraftJson += _T("],\"markers\":[");
        for (size_t i = 0; i < report.Draft.Markers.size(); ++i)
        {
            if (i)
            {
                report.DraftJson += ',';
            }
            const auto& marker = report.Draft.Markers[i];
            report.DraftJson += _T("{\"name\":");
            Quote(report.DraftJson, marker.Name);
            report.DraftJson += _T(",\"t\":");
            Number(report.DraftJson, marker.Time);
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
