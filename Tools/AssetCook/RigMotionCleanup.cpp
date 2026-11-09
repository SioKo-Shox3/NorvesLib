#include "RigMotionCleanup.h"
#include "Animation/PoseTypes.h"
#include "Animation/MotionAnalysis.h"
#include "Animation/SkeletalClipSampling.h"
#include "SkeletalRoleFileInput.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace NorvesLib::Tools::AssetCook::Detail
{
    namespace A = Core::Animation;
    namespace S = Core::Skeletal;
    namespace C = Core::Container;
    namespace
    {
        struct Foot
        {
            uint32_t Joint;
            const char* Name;
            Core::Identity Leg;
            C::VariableArray<Math::Vector3> Positions;
        };
        struct Window
        {
            size_t FootIndex;
            float Start, End;
        };
        S::SkeletalValue Sample(const S::SkeletalAnimationChannel& channel, float time)
        {
            if (time <= channel.Samples.front().TimeSeconds)
                return channel.Samples.front().Value;
            if (time >= channel.Samples.back().TimeSeconds)
                return channel.Samples.back().Value;
            const auto next = std::lower_bound(channel.Samples.begin(), channel.Samples.end(), time,
                                               [](const auto& sample, float t) { return sample.TimeSeconds < t; });
            return A::Detail::SampleSkeletalChannelInterval(channel, *(next - 1), *next, time);
        }
    } // namespace
    bool ApplyRigMotionCleanup(const S::RigAuthoringCpu& target, const A::SkeletalRoleProfile& profile,
                               S::SkeletalAnimationClip& clip, Core::JsonWriter& json, C::AnsiString& error)
    {
        const auto& options = profile.Processing;
        const bool analyze = options.bAnalyzeContacts || options.bGenerateFootMarkers || options.bDeriveRootMotion ||
                             options.DesiredGroundSpeed > 0;
        json.WriteBool("contact_analysis", analyze);
        if (!analyze)
            return true;
        const auto* data = target.GetData();
        if (!data || data->LocalRest.size() != data->Geometry.Joints.size())
        {
            error = "cleanup_skeleton";
            return false;
        }
        C::VariableArray<Foot> feet;
        constexpr const char* quadrupedLegNames[] = {"LF", "RF", "LH", "RH"};
        constexpr const char* humanoidLegNames[] = {"L", "R"};
        const auto legs = A::GetSkeletalRoleVocabularyInfo(profile.Vocabulary).Legs;
        const auto* legNames = profile.Vocabulary == A::SkeletalRoleVocabulary::Humanoid
                                   ? humanoidLegNames : quadrupedLegNames;
        for (size_t legIndex = 0; legIndex < legs.size(); ++legIndex)
        {
            const auto& leg = legs[legIndex];
            const auto& targets = profile.Target[size_t(leg.Foot)];
            if (targets.empty())
                continue;
            const auto& bytes = targets.back().Name;
            C::String name;
            if (!DecodeSkeletalRoleUtf8Text({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, name))
            {
                error = "cleanup_foot_name";
                return false;
            }
            for (uint32_t i = 0; i < data->Geometry.Joints.size(); ++i)
                if (data->Geometry.Joints[i].Name == name)
                {
                    feet.push_back({i, A::SkeletalRoleName(profile.Vocabulary, leg.Foot), Core::Identity(legNames[legIndex]), {}});
                    break;
                }
        }
        json.WriteUInt64("foot_count", feet.size());
        if (feet.empty())
        {
            json.WriteUInt64("feet_without_contacts", 0);
            json.WriteBool("needs_review", true);
            return true;
        }
        const double grid = std::ceil(double(clip.DurationSeconds) * options.OutputFps);
        if (grid < 1 || grid + 1 > options.MaximumSamples)
        {
            error = "cleanup_sample_count";
            return false;
        }
        const size_t samples = size_t(grid) + 1, joints = data->LocalRest.size();
        const double dt = double(clip.DurationSeconds) / (samples - 1);
        for (auto& foot : feet)
            foot.Positions.resize(samples);
        C::VariableArray<uint32_t> order;
        C::VariableArray<uint8_t> ready(joints, 0);
        uint32_t root = UINT32_MAX;
        for (uint32_t i = 0; i < joints; ++i)
        {
            if (data->Geometry.Joints[i].ParentIndex < 0)
            {
                root = i;
            }
        }
        if (root == UINT32_MAX)
        {
            error = "cleanup_root";
            return false;
        }
        while (order.size() < joints)
        {
            const auto before = order.size();
            for (uint32_t i = 0; i < joints; ++i)
            {
                if (ready[i])
                    continue;
                const auto parent = data->Geometry.Joints[i].ParentIndex;
                if (parent < 0)
                    root = i;
                else if (size_t(parent) >= joints)
                {
                    error = "cleanup_parent";
                    return false;
                }
                else if (!ready[parent])
                    continue;
                ready[i] = 1;
                order.push_back(i);
            }
            if (order.size() == before)
            {
                error = "cleanup_cycle";
                return false;
            }
        }
        if (root == UINT32_MAX)
        {
            error = "cleanup_root";
            return false;
        }
        const auto& f = data->RootFrame;
        const Math::Matrix4x4 frame{f[0], f[1], f[2],  f[3],  f[4],  f[5],  f[6],  f[7],
                                    f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15]};
        A::LocalPose rest(joints), pose;
        for (size_t i = 0; i < joints; ++i)
        {
            const auto& t = data->LocalRest[i];
            rest[i].Translation = {float(t.Translation.X), float(t.Translation.Y), float(t.Translation.Z)};
            rest[i].Rotation = A::Detail::NormalizeQuaternion(
                {float(t.Rotation.X), float(t.Rotation.Y), float(t.Rotation.Z), float(t.Rotation.W)});
            rest[i].Scale = {float(t.Scale.X), float(t.Scale.Y), float(t.Scale.Z)};
        }
        C::VariableArray<Math::Matrix4x4> models(joints);
        for (size_t sample = 0; sample < samples; ++sample)
        {
            pose = rest;
            for (const auto& channel : clip.Channels)
            {
                if (channel.Samples.empty() || channel.JointIndex >= joints)
                    continue;
                const auto value = Sample(channel, float(sample * dt));
                auto& local = pose[channel.JointIndex];
                if (channel.Path == S::SkeletalAnimationPath::Rotation)
                    local.Rotation = A::Detail::NormalizeQuaternion(
                        {float(value.X), float(value.Y), float(value.Z), float(value.W)});
                else if (channel.Path == S::SkeletalAnimationPath::Translation)
                    local.Translation = {float(value.X), float(value.Y), float(value.Z)};
                else
                    local.Scale = {float(value.X), float(value.Y), float(value.Z)};
            }
            for (auto i : order)
            {
                const auto local = A::ToRowMatrix(pose[i]);
                const auto parent = data->Geometry.Joints[i].ParentIndex;
                models[i] = parent < 0 ? local : local * models[parent];
            }
            const auto rootModel = models[root] * frame;
            for (auto& foot : feet)
            {
                const auto model = models[foot.Joint] * frame;
                foot.Positions[sample] = {model.m30 - rootModel.m30, model.m31, model.m32 - rootModel.m32};
            }
        }
        C::VariableArray<Window> windows;
        C::VariableArray<Math::Vector3> groundVelocities;
        uint32_t feetWithoutContacts = 0;
        double ground = std::numeric_limits<double>::max();
        for (size_t footIndex = 0; footIndex < feet.size(); ++footIndex)
        {
            const auto& positions = feet[footIndex].Positions;
            C::VariableArray<float> heights;
            heights.reserve(positions.size());
            for (const auto& position : positions)
            {
                heights.push_back(position.y);
                ground = std::min(ground, double(position.y));
            }
            C::VariableArray<A::FootContactSampleWindow> footWindows;
            if (!A::DetectFootContactWindows(heights, dt, A::FootContactOptions{}, footWindows))
            {
                error = "cleanup_contacts";
                return false;
            }
            if (footWindows.empty())
            {
                ++feetWithoutContacts;
            }
            for (const auto& window : footWindows)
            {
                windows.push_back({footIndex, float(window.Begin * dt), float(window.End * dt)});
                for (size_t i = window.Begin; i < window.End; ++i)
                {
                    groundVelocities.push_back((positions[i] - positions[i + 1]) / float(dt));
                }
            }
        }
        double x = 0, z = 0;
        for (const auto& velocity : groundVelocities)
        {
            x += velocity.x;
            z += velocity.z;
        }
        if (!groundVelocities.empty())
        {
            x /= groundVelocities.size();
            z /= groundVelocities.size();
        }
        double speed = std::hypot(x, z), rms = 0;
        for (const auto& velocity : groundVelocities)
            rms += (velocity.x - x) * (velocity.x - x) + (velocity.z - z) * (velocity.z - z);
        rms = groundVelocities.empty() ? 0 : std::sqrt(rms / groundVelocities.size());
        uint32_t skippedMarkers = 0;
        if (options.bGenerateFootMarkers)
        {
            C::VariableArray<A::FootContactWindow> contactWindows;
            C::VariableArray<Core::Identity> legs;
            for (const auto& foot : feet)
            {
                legs.push_back(foot.Leg);
            }
            for (const auto& window : windows)
            {
                contactWindows.push_back({feet[window.FootIndex].Leg, window.Start, window.End});
            }
            // 自動選択で検出した周期は Metadata に転記されないため、processing のループ指定も使う。
            const bool bLoop = clip.Metadata.Loop.bEnabled || options.Loop != A::SkeletalLoopSelection::None;
            if (!A::AppendFootContactMetadata(contactWindows, legs, clip.DurationSeconds, bLoop, clip.Metadata,
                                              skippedMarkers))
            {
                error = "cleanup_foot_markers";
                return false;
            }
        }
        bool derived = false;
        double authoredDistance = 0;
        for (size_t i = 1; i < clip.RootMotion.size(); ++i)
            authoredDistance += std::hypot(clip.RootMotion[i].TranslationX - clip.RootMotion[i - 1].TranslationX,
                                           clip.RootMotion[i].TranslationZ - clip.RootMotion[i - 1].TranslationZ);
        if (options.bDeriveRootMotion && speed > 1e-4 && authoredDistance < 1e-4)
        {
            if (clip.RootMotion.empty())
                for (size_t i = 0; i < samples; ++i)
                    clip.RootMotion.push_back({float(i * dt), 0, 0, 0});
            double px = 0, pz = 0;
            for (size_t i = 0; i < clip.RootMotion.size(); ++i)
            {
                auto& sample = clip.RootMotion[i];
                if (i)
                {
                    const auto& previous = clip.RootMotion[i - 1];
                    const double heading = (sample.YawRadians + previous.YawRadians) * .5;
                    const double distance = speed * (sample.TimeSeconds - previous.TimeSeconds);
                    px += std::sin(heading) * distance;
                    pz += std::cos(heading) * distance;
                }
                sample.TranslationX = px;
                sample.TranslationZ = pz;
            }
            clip.RootMotionJoint = root;
            clip.Metadata.Root.Joint = root;
            clip.Metadata.Root.Mode = A::RootMotionMode::Extract;
            derived = true;
        }
        if (speed > 0 && clip.Metadata.Root.NominalSpeed < 0)
            clip.Metadata.Root.NominalSpeed = float(speed);
        double retime = 1;
        if (options.DesiredGroundSpeed > 0 && speed > 1e-4)
        {
            retime = speed / options.DesiredGroundSpeed;
            A::ClipMetadata metadata;
            A::ClipMetadataReport report;
            if (!A::RemapClipMetadataTime(clip.Metadata, clip.DurationSeconds, 0, clip.DurationSeconds, retime,
                                          metadata, report))
            {
                error = "cleanup_retime_metadata";
                return false;
            }
            for (auto& channel : clip.Channels)
                for (auto& sample : channel.Samples)
                    sample.TimeSeconds = float(sample.TimeSeconds * retime);
            for (auto& sample : clip.RootMotion)
                sample.TimeSeconds = float(sample.TimeSeconds * retime);
            clip.DurationSeconds = float(clip.DurationSeconds * retime);
            clip.Metadata = std::move(metadata);
            for (auto& window : windows)
            {
                window.Start = float(window.Start * retime);
                window.End = float(window.End * retime);
            }
            speed /= retime;
            rms /= retime;
        }
        json.WriteNumber("natural_ground_speed", speed);
        json.WriteNumber("slide_rms", rms);
        json.WriteNumber("ground_offset", ground);
        json.WriteNumber("ground_speed_retime", retime);
        json.WriteBool("derived_root_motion", derived);
        json.WriteUInt64("coincident_markers_omitted", skippedMarkers);
        json.WriteUInt64("feet_without_contacts", feetWithoutContacts);
        json.WriteBool("needs_review", windows.empty() || feetWithoutContacts > 0 || skippedMarkers > 0 ||
                                           rms > std::max(.1, speed * .25));
        json.BeginArray("contacts");
        for (const auto& window : windows)
        {
            json.BeginObject();
            json.WriteString("foot", feet[window.FootIndex].Name);
            json.WriteNumber("start", window.Start);
            json.WriteNumber("end", window.End);
            json.EndObject();
        }
        json.EndArray();
        A::ClipMetadataReport valid;
        if (!A::ValidateClipMetadata(clip.Metadata, clip.DurationSeconds, valid))
        {
            error = "cleanup_metadata";
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook::Detail
