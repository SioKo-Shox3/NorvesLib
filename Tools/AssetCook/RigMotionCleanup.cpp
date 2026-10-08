#include "RigMotionCleanup.h"
#include "Animation/PoseTypes.h"
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
        constexpr A::SkeletalRole roles[] = {A::SkeletalRole::FrontLPaw, A::SkeletalRole::FrontRPaw,
                                             A::SkeletalRole::HindLPaw, A::SkeletalRole::HindRPaw};
        for (auto role : roles)
        {
            const auto& targets = profile.Target[size_t(role)];
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
                    feet.push_back({i, A::SkeletalRoleName(role), {}});
                    break;
                }
        }
        json.WriteUInt64("foot_count", feet.size());
        if (feet.empty())
        {
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
        double ground = std::numeric_limits<double>::max();
        for (size_t footIndex = 0; footIndex < feet.size(); ++footIndex)
        {
            const auto& positions = feet[footIndex].Positions;
            float minimum = std::numeric_limits<float>::max();
            for (const auto& p : positions)
                minimum = std::min(minimum, p.y);
            ground = std::min(ground, double(minimum));
            size_t begin = SIZE_MAX;
            const auto close = [&](size_t end) {
                if (begin != SIZE_MAX && (end - begin) * dt >= .05)
                {
                    windows.push_back({footIndex, float(begin * dt), float(end * dt)});
                    for (size_t i = begin; i < end; ++i)
                        groundVelocities.push_back((positions[i] - positions[i + 1]) / float(dt));
                }
                begin = SIZE_MAX;
            };
            for (size_t i = 0; i + 1 < samples; ++i)
            {
                const double hysteresis = begin == SIZE_MAX ? 1 : 1.5;
                const double vertical = std::abs(double(positions[i + 1].y) - positions[i].y) / dt;
                if (positions[i].y - minimum <= .03 * hysteresis && vertical <= .15 * hysteresis)
                {
                    if (begin == SIZE_MAX)
                        begin = i;
                }
                else
                    close(i);
            }
            close(samples - 1);
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
            const bool generateMarkers = clip.Metadata.Markers.empty();
            for (const auto& window : windows)
                clip.Metadata.Events.push_back({Core::Identity(feet[window.FootIndex].Name), window.Start, window.End});
            if (generateMarkers)
            {
                for (size_t i = 0; i < feet.size(); ++i)
                {
                    const Window* largest = nullptr;
                    for (const auto& window : windows)
                        if (window.FootIndex == i &&
                            (!largest || window.End - window.Start > largest->End - largest->Start))
                            largest = &window;
                    if (!largest)
                        continue;
                    const C::AnsiString name = feet[i].Name;
                    clip.Metadata.Markers.push_back({Core::Identity((name + "_down").c_str()), largest->Start});
                    clip.Metadata.Markers.push_back({Core::Identity((name + "_up").c_str()),
                                                     largest->End >= clip.DurationSeconds ? 0 : largest->End});
                }
                std::stable_sort(clip.Metadata.Markers.begin(), clip.Metadata.Markers.end(),
                                 [](const auto& a, const auto& b) { return a.Time < b.Time; });
                // 同時刻の複数markerは既存形式に入らない。接地windowはreportへ全件残す。
                for (size_t i = 1; i < clip.Metadata.Markers.size();)
                    if (clip.Metadata.Markers[i].Time == clip.Metadata.Markers[i - 1].Time)
                    {
                        clip.Metadata.Markers.erase(clip.Metadata.Markers.begin() + i);
                        ++skippedMarkers;
                    }
                    else
                        ++i;
            }
            std::stable_sort(clip.Metadata.Events.begin(), clip.Metadata.Events.end(),
                             [](const auto& a, const auto& b) { return a.Time < b.Time; });
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
        json.WriteBool("needs_review", windows.empty() || skippedMarkers > 0 || rms > std::max(.1, speed * .25));
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
