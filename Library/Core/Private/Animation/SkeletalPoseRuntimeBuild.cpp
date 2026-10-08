#include "Animation/SkeletalPoseRuntimeBuild.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Asset/CookedSkeletonV1.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation::Detail
{
    namespace
    {
        Math::Matrix4x4 Matrix(const Container::FixedArray<float, 16>& v)
        {
            return {v[0], v[1], v[2],  v[3],  v[4],  v[5],  v[6],  v[7],
                    v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]};
        }
        bool BuildOrder(SkeletonPoseRuntime& runtime)
        {
            const size_t count = runtime.Parents.size();
            if (count == 0 || count > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            for (int32_t parent : runtime.Parents)
            {
                if (parent < -1 || (parent >= 0 && static_cast<size_t>(parent) >= count))
                {
                    return false;
                }
            }
            Container::VariableArray<uint8_t> states(count, 0);
            Container::VariableArray<uint32_t> chain;
            chain.reserve(count);
            runtime.EvaluationOrder.reserve(count);
            for (uint32_t first = 0; first < count; ++first)
            {
                if (states[first] == 2)
                {
                    continue;
                }
                chain.clear();
                int64_t current = first;
                while (current >= 0 && states[static_cast<size_t>(current)] == 0)
                {
                    const auto index = static_cast<uint32_t>(current);
                    states[index] = 1;
                    chain.push_back(index);
                    current = runtime.Parents[index];
                }
                if (current >= 0 && states[static_cast<size_t>(current)] == 1)
                {
                    return false;
                }
                while (!chain.empty())
                {
                    const auto index = chain.back();
                    chain.pop_back();
                    states[index] = 2;
                    runtime.EvaluationOrder.push_back(index);
                }
            }
            return true;
        }
        bool BuildRest(Container::Span<const Skeletal::SkeletalRestTransform> rest, LocalPose& out)
        {
            out.resize(rest.size());
            for (size_t i = 0; i < rest.size(); ++i)
            {
                const auto& r = rest[i];
                if (!Skeletal::IsValidSkeletalRestTransform(r))
                {
                    return false;
                }
                out[i].Translation = {r.Translation.X, r.Translation.Y, r.Translation.Z};
                out[i].Rotation = NormalizeQuaternion({r.Rotation.X, r.Rotation.Y, r.Rotation.Z, r.Rotation.W});
                out[i].Scale = {r.Scale.X, r.Scale.Y, r.Scale.Z};
            }
            return true;
        }
    } // namespace

    void BuildLegacySkeletonPoseRuntime(Container::Span<const Skeletal::SkeletalJoint> joints,
                                        Container::Span<const Skeletal::SkeletalRestTransform> rest,
                                        SkeletonPoseRuntime& out)
    {
        out = {};
        out.Parents.reserve(joints.size());
        for (const auto& joint : joints)
        {
            out.Parents.push_back(joint.ParentIndex);
        }
        if (!BuildOrder(out) || (!rest.empty() && rest.size() != joints.size()))
        {
            return;
        }
        out.LegacyInverseBind.resize(joints.size());
        out.LegacyBindModel.resize(joints.size());
        for (size_t i = 0; i < joints.size(); ++i)
        {
            out.LegacyInverseBind[i] = Matrix(joints[i].InverseBindMatrix);
            if (!TryInverseMatrix(out.LegacyInverseBind[i], out.LegacyBindModel[i]))
            {
                return;
            }
        }
        if (!BuildRest(rest, out.AuthorRestLocal))
        {
            return;
        }
        out.bValid = true;
    }

    void BuildSplitSkeletonPoseRuntime(const Skeletal::SkeletonV1Data& skeleton, SkeletonPoseRuntime& out)
    {
        out = {};
        out.bSplit = true;
        out.Parents.reserve(skeleton.Topology.Joints.size());
        for (const auto& joint : skeleton.Topology.Joints)
        {
            out.Parents.push_back(joint.ParentIndex);
        }
        if (!BuildOrder(out) || skeleton.CurrentRest.Rest.size() != out.Parents.size() ||
            !BuildRest(skeleton.CurrentRest.Rest, out.AuthorRestLocal))
        {
            return;
        }
        out.RootFrame = Matrix(skeleton.RootTransform);
        out.bValid = IsFiniteMatrix(out.RootFrame);
    }

    void BuildClipPoseRuntime(const Skeletal::SkeletalAnimationClip& clip, ClipPoseRuntime& out)
    {
        out = {};
        out.bLegacyValid = false;
        bool legacy = std::isfinite(clip.DurationSeconds) && clip.DurationSeconds >= 0;
        const Skeletal::RigV1Limits limits;
        bool split = legacy && !clip.Name.empty() && clip.Channels.size() <= limits.MaxChannels;
        struct Entry
        {
            uint32_t Joint = 0, Path = 0;
            size_t Index = 0;
        };
        Container::VariableArray<Entry> entries;
        entries.reserve(clip.Channels.size());
        size_t totalSamples = 0;
        for (size_t index = 0; index < clip.Channels.size(); ++index)
        {
            const auto& channel = clip.Channels[index];
            const uint32_t path = static_cast<uint32_t>(channel.Path);
            const bool validKind = path <= 2 && static_cast<uint32_t>(channel.Interpolation) <= 1;
            legacy = legacy && validKind;
            split = split && validKind && !channel.Samples.empty();
            out.RequiredJointCount = std::max(out.RequiredJointCount, uint64_t(channel.JointIndex) + 1);
            if (channel.Samples.size() > limits.MaxSamples - std::min<size_t>(totalSamples, limits.MaxSamples))
            {
                split = false;
            }
            else
            {
                totalSamples += channel.Samples.size();
            }
            float previous = 0;
            bool first = true;
            for (const auto& sample : channel.Samples)
            {
                const auto& v = sample.Value;
                const bool finite = std::isfinite(sample.TimeSeconds) && std::isfinite(v.X) && std::isfinite(v.Y) &&
                                    std::isfinite(v.Z) && std::isfinite(v.W);
                const bool ordered = first || sample.TimeSeconds > previous;
                legacy = legacy && finite && ordered;
                split =
                    split && finite && ordered && sample.TimeSeconds >= 0 && sample.TimeSeconds <= clip.DurationSeconds;
                if (channel.Path == Skeletal::SkeletalAnimationPath::Rotation)
                {
                    split = split && Skeletal::IsRepresentableSkeletalRotation(v);
                }
                if (channel.Path == Skeletal::SkeletalAnimationPath::Scale)
                {
                    split = split && v.X > 0 && v.Y > 0 && v.Z > 0;
                }
                first = false;
                previous = sample.TimeSeconds;
            }
            if (validKind && !channel.Samples.empty())
            {
                entries.push_back({channel.JointIndex, path, index});
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b)
                  {
                      if (a.Joint != b.Joint)
                      {
                          return a.Joint < b.Joint;
                      }
                      if (a.Path != b.Path)
                      {
                          return a.Path < b.Path;
                      }
                      return a.Index < b.Index;
                  });
        for (const auto& entry : entries)
        {
            if (out.Joints.empty() || out.Joints.back().JointIndex != entry.Joint)
            {
                JointChannelSelection selection;
                selection.JointIndex = entry.Joint;
                out.Joints.push_back(selection);
            }
            auto& selected = out.Joints.back().Channels[entry.Path];
            if (selected != SIZE_MAX)
            {
                split = false;
            }
            // legacyは後ろの非空channelを優先し、空のchannelでは上書きしない。
            selected = entry.Index;
        }
        out.bLegacyValid = legacy;
        out.bSplitValid = split;
    }
} // namespace NorvesLib::Core::Animation::Detail
