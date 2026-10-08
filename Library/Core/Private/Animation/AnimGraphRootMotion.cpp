#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletalRootMotion.h"
#include "Animation/SkeletonResource.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Core::Animation
{
    namespace
    {
        constexpr double Tau = 6.2831853071795864769;
        bool ModelMotion(const Math::Matrix4x4& m, bool yaw, RootMotionDelta& out)
        {
            out = {m.m30, m.m32, 0};
            if (yaw)
            {
                const double x = m.m20, z = m.m22;
                if (x * x + z * z < 1e-16)
                    return false;
                out.Yaw = std::atan2(x, z);
            }
            return out.IsFinite();
        }
        RootMotionDelta MixMotion(const RootMotionDelta& a, const RootMotionDelta& b, double weight)
        {
            return AddRootMotion(ScaleRootMotion(a, 1 - weight), ScaleRootMotion(b, weight));
        }
    } // namespace
    bool AnimGraphInstance::RawRootAt(uint32_t clip, double time, RootMotionDelta& out)
    {
        if (!SkeletalPoseBuilder::SampleClipToLocalPose(m_Contexts[clip], *m_Graph->Clips[clip], float(time),
                                                        m_MotionScratch.Local))
            return false;
        const auto joint = m_RootClips[clip].Joint;
        if (joint >= m_MotionScratch.Local.size())
            return false;
        Math::Matrix4x4 model;
        return SkeletalPoseBuilder::BuildRootModelMatrix(m_Contexts[clip], m_MotionScratch.Local[joint], model) &&
               ModelMotion(model, m_Graph->Clips[clip]->GetClipMetadata().Root.bYaw, out);
    }
    bool AnimGraphInstance::RootAt(uint32_t index, double time, RootMotionDelta& out)
    {
        const auto& clip = m_Graph->Clips[index]->GetClip();
        const auto& runtime = m_RootClips[index];
        const auto& settings = m_Graph->Clips[index]->GetClipMetadata().Root;
        auto lockAxes = [&]() {
            if (!settings.bX)
                out.X = 0;
            if (!settings.bZ)
                out.Z = 0;
            if (!settings.bYaw)
                out.Yaw = 0;
        };
        time = std::clamp(time, 0.0, double(clip.DurationSeconds));
        if (runtime.bCurve)
        {
            const auto& track = clip.RootMotion;
            auto next = std::upper_bound(track.begin(), track.end(), time,
                                         [](double t, const auto& key) { return t < key.TimeSeconds; });
            const auto* a = next == track.begin() ? &track.front() : &*(next - 1);
            const auto* b = next == track.end() ? a : &*next;
            const double w = a == b ? 0 : (time - a->TimeSeconds) / (double(b->TimeSeconds) - a->TimeSeconds);
            out = {a->TranslationX + (b->TranslationX - a->TranslationX) * w,
                   a->TranslationZ + (b->TranslationZ - a->TranslationZ) * w,
                   a->YawRadians + (b->YawRadians - a->YawRadians) * w};
            lockAxes();
            return out.IsFinite();
        }
        if (!RawRootAt(index, time, out))
            return false;
        const auto& keys = runtime.YawKeys;
        if (!keys.empty())
        {
            auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                         [](double t, const auto& key) { return t < key.Time; });
            const auto& key = next == keys.begin() ? keys.front() : *(next - 1);
            out.Yaw = key.Unwrapped + std::remainder(out.Yaw - key.Raw, Tau);
        }
        const double yaw = settings.bYaw ? out.Yaw - runtime.ReferenceModel.Yaw : 0;
        const auto rotated = ComposeRootMotion({0, 0, yaw}, {runtime.ReferenceModel.X, runtime.ReferenceModel.Z, 0});
        out = {out.X - rotated.X, out.Z - rotated.Z, yaw};
        lockAxes();
        return out.IsFinite();
    }
    bool AnimGraphInstance::RefreshRootMetadata(bool force)
    {
        uint32_t commonRoot = UINT32_MAX;
        for (uint32_t i = 0; i < m_Graph->Clips.size(); ++i)
        {
            const auto& resource = *m_Graph->Clips[i];
            const auto& clip = resource.GetClip();
            const auto& metadata = resource.GetClipMetadata();
            auto& state = m_RootClips[i];
            if (force || state.MetadataRevision != resource.GetMetadataRevision())
            {
                ClipMetadataReport report;
                if (!ValidateClipMetadata(metadata, clip.DurationSeconds, report))
                    return false;
                state.Joint = metadata.Root.Joint;
                state.bCurve =
                    !clip.RootMotion.empty() && Skeletal::IsValidSkeletalRootMotion(clip, m_Graph->Parents.size());
                if (state.Joint == UINT32_MAX && state.bCurve)
                    state.Joint = clip.RootMotionJoint;
                if (state.Joint == UINT32_MAX)
                    for (uint32_t j = 0; j < m_Graph->Parents.size(); ++j)
                        if (m_Graph->Parents[j] < 0)
                        {
                            state.Joint = j;
                            break;
                        }
                if (state.Joint >= m_Graph->Parents.size() || m_Graph->Parents[state.Joint] >= 0)
                    return false;
                state.YawKeys.clear();
                state.NominalSpeed = 0;
                if (metadata.Root.Mode != RootMotionMode::None)
                {
                    if (!clip.RootMotion.empty() && (!state.bCurve || state.Joint != clip.RootMotionJoint))
                        return false;
                    if (!state.bCurve)
                    {
                        if (!RawRootAt(i, 0, state.ReferenceModel))
                            return false;
                        state.Reference = m_MotionScratch.Local[state.Joint];
                        Container::VariableArray<float> times{0, clip.DurationSeconds};
                        for (const auto& selected : resource.GetPoseRuntime().Joints)
                            if (selected.JointIndex == state.Joint && selected.Channels[1] != SIZE_MAX)
                                for (const auto& key : clip.Channels[selected.Channels[1]].Samples)
                                    if (key.TimeSeconds > 0 && key.TimeSeconds < clip.DurationSeconds)
                                        times.push_back(key.TimeSeconds);
                        std::sort(times.begin(), times.end());
                        times.erase(std::unique(times.begin(), times.end()), times.end());
                        const size_t endpoints = times.size();
                        for (size_t k = 1; k < endpoints; ++k)
                        {
                            const float middle = float((double(times[k - 1]) + times[k]) * .5);
                            if (middle > times[k - 1] && middle < times[k])
                                times.push_back(middle);
                        }
                        std::sort(times.begin(), times.end());
                        state.YawKeys.reserve(times.size());
                        for (float time : times)
                        {
                            RootMotionDelta value;
                            if (!RawRootAt(i, time, value))
                                return false;
                            const double unwrapped =
                                state.YawKeys.empty() ? value.Yaw
                                                      : state.YawKeys.back().Unwrapped +
                                                            std::remainder(value.Yaw - state.YawKeys.back().Raw, Tau);
                            state.YawKeys.push_back({time, value.Yaw, unwrapped});
                        }
                    }
                }
                // 曲線があるclipはNoneでも同期用の公称速度を読める。
                if (state.bCurve || metadata.Root.Mode != RootMotionMode::None)
                {
                    const double start = metadata.Loop.bEnabled ? metadata.Loop.Start : 0,
                                 end = metadata.Loop.bEnabled ? metadata.Loop.End : clip.DurationSeconds;
                    Container::VariableArray<float> times{float(start), float(end)};
                    if (state.bCurve)
                    {
                        for (const auto& key : clip.RootMotion)
                            if (key.TimeSeconds > start && key.TimeSeconds < end)
                                times.push_back(key.TimeSeconds);
                    }
                    else
                    {
                        for (const auto& selected : resource.GetPoseRuntime().Joints)
                            if (selected.JointIndex == state.Joint && selected.Channels[0] != SIZE_MAX)
                                for (const auto& key : clip.Channels[selected.Channels[0]].Samples)
                                    if (key.TimeSeconds > start && key.TimeSeconds < end)
                                        times.push_back(key.TimeSeconds);
                    }
                    std::sort(times.begin(), times.end());
                    times.erase(std::unique(times.begin(), times.end()), times.end());
                    RootMotionDelta previous;
                    double distance = 0;
                    for (size_t k = 0; k < times.size(); ++k)
                    {
                        RootMotionDelta value;
                        if (!RootAt(i, times[k], value))
                            return false;
                        if (k)
                            distance += std::hypot(value.X - previous.X, value.Z - previous.Z);
                        previous = value;
                    }
                    if (end > start)
                        state.NominalSpeed = float(distance / (end - start));
                    if (!std::isfinite(state.NominalSpeed))
                        return false;
                }
                state.MetadataRevision = resource.GetMetadataRevision();
            }
            if (metadata.Root.Mode != RootMotionMode::None)
            {
                if (commonRoot != UINT32_MAX && commonRoot != state.Joint)
                    return false;
                commonRoot = state.Joint;
            }
        }
        for (uint32_t index : m_Graph->EvaluationOrder)
        {
            const auto& n = m_Graph->Nodes[index];
            m_Durations[index] = 0;
            if (n.Kind == AnimNodeKind::Clip)
            {
                const auto& clip = *m_Graph->Clips[n.Clip];
                const auto& loop = clip.GetClipMetadata().Loop;
                const double duration =
                    n.bLoop && loop.bEnabled ? loop.End - loop.Start : clip.GetClip().DurationSeconds;
                m_Durations[index] = n.PlaybackRate == 0 ? std::numeric_limits<double>::infinity()
                                                         : duration / std::fabs(double(n.PlaybackRate));
            }
            else
                for (uint32_t child : n.Children)
                    m_Durations[index] = std::max(m_Durations[index], m_Durations[child]);
        }
        return true;
    }
    float AnimGraphInstance::GetNominalSpeed(uint32_t clip) const
    {
        if (!m_Graph || clip >= m_RootClips.size())
            return 0;
        const float value = m_Graph->Clips[clip]->GetClipMetadata().Root.NominalSpeed;
        return value >= 0 ? value
               : m_RootClips[clip].MetadataRevision == m_Graph->Clips[clip]->GetMetadataRevision()
                   ? m_RootClips[clip].NominalSpeed
                   : 0;
    }
    bool AnimGraphInstance::RootAtUnwrapped(uint32_t index, double time, bool loop, RootMotionDelta& out)
    {
        const auto& resource = *m_Graph->Clips[index];
        const auto& clip = resource.GetClip();
        const auto& metadata = resource.GetClipMetadata();
        const double start = metadata.Loop.bEnabled ? metadata.Loop.Start : 0,
                     end = metadata.Loop.bEnabled ? metadata.Loop.End : clip.DurationSeconds;
        if (!loop || end <= start)
            return RootAt(index, time, out);
        const double cycles = std::floor((time - start) / (end - start));
        if (!std::isfinite(cycles) || std::fabs(cycles) > 4503599627370496.0)
            return false;
        const double phase = time - cycles * (end - start);
        RootMotionDelta a, b, c, power;
        if (!RootAt(index, start, a) || !RootAt(index, end, b) || !RootAt(index, phase, c) ||
            !RepeatRootMotion(RootMotionBetween(a, b), int64_t(cycles), power))
            return false;
        out = ComposeRootMotion(power, RootMotionBetween(a, c));
        return out.IsFinite();
    }
    bool AnimGraphInstance::RemoveRootMotion(uint32_t index, LocalPose& pose) const
    {
        const auto& resource = *m_Graph->Clips[index];
        const auto& settings = resource.GetClipMetadata().Root;
        const auto& runtime = m_RootClips[index];
        // GR84の曲線は既に姿勢から分離済み。二重に引かない。
        if (settings.Mode == RootMotionMode::None || runtime.bCurve || (!settings.bX && !settings.bZ && !settings.bYaw))
            return true;
        if (runtime.Joint >= pose.size())
            return false;
        Math::Matrix4x4 model;
        if (!SkeletalPoseBuilder::BuildRootModelMatrix(m_Contexts[index], pose[runtime.Joint], model))
            return false;
        RootMotionDelta current;
        if (!ModelMotion(model, settings.bYaw, current))
            return false;
        const double yaw = settings.bYaw ? std::remainder(current.Yaw - runtime.ReferenceModel.Yaw, Tau) : 0;
        const auto rotated = ComposeRootMotion({0, 0, yaw}, {runtime.ReferenceModel.X, runtime.ReferenceModel.Z, 0});
        RootMotionDelta extracted{settings.bX ? current.X - rotated.X : 0, settings.bZ ? current.Z - rotated.Z : 0,
                                  yaw};
        const auto inverse = InverseRootMotion(extracted);
        JointTransform correction;
        correction.Translation = {float(inverse.X), 0, float(inverse.Z)};
        correction.Rotation = {0, float(std::sin(inverse.Yaw * .5)), 0, float(std::cos(inverse.Yaw * .5))};
        model = model * ToRowMatrix(correction);
        JointTransform local;
        if (!SkeletalPoseBuilder::RootModelToLocal(m_Contexts[index], model, local))
            return false;
        pose[runtime.Joint] = local;
        return true;
    }
    bool AnimGraphInstance::AdvanceRootMotion()
    {
        for (auto& value : m_NodeMotion)
            value = {};
        for (const auto& t : m_Traversals)
        {
            // 同時刻の剛体差分を計算し直すと逆回転の丸めで微小移動が残るため、identityを維持する。
            if (t.Previous == t.Current)
                continue;
            const auto& settings = m_Graph->Clips[t.Clip]->GetClipMetadata().Root;
            if (settings.Mode == RootMotionMode::None || (!settings.bX && !settings.bZ && !settings.bYaw))
                continue;
            RootMotionDelta a, b;
            if (!RootAtUnwrapped(t.Clip, t.Previous, t.bLoop, a) || !RootAtUnwrapped(t.Clip, t.Current, t.bLoop, b))
                return false;
            auto delta = RootMotionBetween(a, b);
            if (!delta.IsFinite())
                return false;
            m_NodeMotion[t.Node].Available = delta;
            if (settings.Mode == RootMotionMode::Extract)
                m_NodeMotion[t.Node].Extracted = delta;
        }
        uint32_t root = 0;
        for (size_t i = 0; i < m_RootClips.size(); ++i)
            if (m_Graph->Clips[i]->GetClipMetadata().Root.Mode != RootMotionMode::None)
            {
                root = m_RootClips[i].Joint;
                break;
            }
        for (uint32_t index : m_Graph->EvaluationOrder)
        {
            const auto& n = m_Graph->Nodes[index];
            const auto& rt = m_Runtime[index];
            auto& result = m_NodeMotion[index];
            if (n.Kind == AnimNodeKind::Clip)
                continue;
            if (n.Kind == AnimNodeKind::Layered)
            {
                result = m_NodeMotion[n.Children[0]];
                for (size_t i = 0; i < n.Layers.size(); ++i)
                {
                    const auto& layer = n.Layers[i];
                    const auto& child = m_NodeMotion[layer.Node];
                    double weight = rt.Edges[i + 1];
                    if (layer.Mask != InvalidAnimNode)
                        weight *= m_Graph->Masks[layer.Mask].Weights[root];
                    if (layer.bAdditive)
                    {
                        result.Extracted = AddRootMotion(result.Extracted, ScaleRootMotion(child.Extracted, weight));
                        result.Available = AddRootMotion(result.Available, ScaleRootMotion(child.Available, weight));
                    }
                    else
                    {
                        result.Extracted = MixMotion(result.Extracted, child.Extracted, weight);
                        result.Available = MixMotion(result.Available, child.Available, weight);
                    }
                }
                continue;
            }
            for (size_t edge = 0; edge < n.Children.size(); ++edge)
            {
                const double weight = rt.Edges[edge];
                if (weight <= 0)
                    continue;
                const auto& child = m_NodeMotion[n.Children[edge]];
                auto extracted = child.Extracted;
                if (n.Kind == AnimNodeKind::StateMachine)
                {
                    if (n.States[edge].RootPolicy == AnimRootPolicy::Velocity)
                        extracted = {};
                    else if (n.States[edge].RootPolicy == AnimRootPolicy::Animation)
                        extracted = child.Available;
                }
                result.Extracted = AddRootMotion(result.Extracted, ScaleRootMotion(extracted, weight));
                result.Available = AddRootMotion(result.Available, ScaleRootMotion(child.Available, weight));
            }
        }
        const auto candidate = ComposeRootMotion(m_PendingRootMotion, m_NodeMotion[m_Graph->Root].Extracted);
        if (!candidate.IsFinite())
            return false;
        m_PendingRootMotion = candidate;
        return true;
    }
} // namespace NorvesLib::Core::Animation
