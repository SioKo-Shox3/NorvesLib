#include "Animation/ClipOptimizer.h"

#include "Animation/PoseTypes.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletalClipSampling.h"
#include "Text/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Channel = Skeletal::SkeletalAnimationChannel;
        using Sample = Skeletal::SkeletalAnimationSample;
        using Value = Skeletal::SkeletalValue;
        using Path = Skeletal::SkeletalAnimationPath;

        struct SelectionEntry
        {
            uint32_t Joint = 0;
            uint32_t PathIndex = 0;
            size_t Index = 0;
        };

        struct ChannelResult
        {
            Channel Data;
            bool bRemoved = false;
            bool bConstant = false;
        };

        struct Interval
        {
            size_t First = 0;
            size_t Last = 0;
        };

        bool IsFinite(const Skeletal::SkeletalPosition& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        bool IsFinite(const Value& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z) &&
                   std::isfinite(value.W);
        }

        Math::Matrix4x4 Matrix(const Container::FixedArray<float, 16>& values)
        {
            return {values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
                    values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15]};
        }

        // 親の範囲と循環を確かめてから逆バインドを大域行列へ戻す。
        bool BuildBindGlobals(Container::Span<const Skeletal::SkeletalJoint> joints,
                              Container::VariableArray<Math::Matrix4x4>& globals)
        {
            if (joints.size() > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }
            for (const auto& joint : joints)
            {
                if (joint.ParentIndex < -1 ||
                    (joint.ParentIndex >= 0 && static_cast<size_t>(joint.ParentIndex) >= joints.size()))
                {
                    return false;
                }
            }
            Container::VariableArray<uint8_t> states(joints.size(), 0);
            Container::VariableArray<size_t> chain;
            for (size_t first = 0; first < joints.size(); ++first)
            {
                chain.clear();
                int64_t current = static_cast<int64_t>(first);
                while (current >= 0 && states[static_cast<size_t>(current)] == 0)
                {
                    const size_t index = static_cast<size_t>(current);
                    states[index] = 1;
                    chain.push_back(index);
                    current = joints[index].ParentIndex;
                }
                if (current >= 0 && states[static_cast<size_t>(current)] == 1)
                {
                    return false;
                }
                for (size_t index : chain)
                {
                    states[index] = 2;
                }
            }
            globals.resize(joints.size());
            for (size_t i = 0; i < joints.size(); ++i)
            {
                if (!Detail::TryInverseMatrix(Matrix(joints[i].InverseBindMatrix), globals[i]))
                {
                    return false;
                }
            }
            return true;
        }

        double Distance(double ax, double ay, double az, double bx, double by, double bz)
        {
            const double x = ax - bx, y = ay - by, z = az - bz;
            return std::sqrt(x * x + y * y + z * z);
        }

        bool BuildJointRadii(const ClipOptimizeRig& rig, Container::VariableArray<double>& radii)
        {
            Container::VariableArray<Math::Matrix4x4> globals;
            if (!BuildBindGlobals(rig.Joints, globals))
            {
                return false;
            }
            Container::VariableArray<Math::Vector3> positions;
            positions.reserve(globals.size());
            for (const auto& global : globals)
            {
                positions.push_back(global.GetTranslationRow());
            }
            radii.assign(rig.Joints.size(), 0.0);
            for (size_t descendant = 0; descendant < rig.Joints.size(); ++descendant)
            {
                const auto& target = positions[descendant];
                int64_t ancestor = rig.Joints[descendant].ParentIndex;
                while (ancestor >= 0)
                {
                    const size_t index = static_cast<size_t>(ancestor);
                    const auto& origin = positions[index];
                    radii[index] = std::max(radii[index], Distance(origin.x, origin.y, origin.z,
                                                                   target.x, target.y, target.z));
                    ancestor = rig.Joints[index].ParentIndex;
                }
            }
            for (const auto& vertex : rig.Vertices)
            {
                for (size_t influence = 0; influence < vertex.JointWeights.size(); ++influence)
                {
                    const float weight = vertex.JointWeights[influence];
                    if (!std::isfinite(weight))
                    {
                        return false;
                    }
                    if (weight <= 0)
                    {
                        continue;
                    }
                    if (!IsFinite(vertex.Position) || vertex.JointIndices[influence] >= rig.Joints.size())
                    {
                        return false;
                    }
                    int64_t ancestor = vertex.JointIndices[influence];
                    while (ancestor >= 0)
                    {
                        const size_t index = static_cast<size_t>(ancestor);
                        const auto& origin = positions[index];
                        radii[index] = std::max(radii[index], Distance(origin.x, origin.y, origin.z,
                                                                       vertex.Position.X, vertex.Position.Y,
                                                                       vertex.Position.Z));
                        ancestor = rig.Joints[index].ParentIndex;
                    }
                }
            }
            return true;
        }

        void MakeRotationContinuous(Channel& channel)
        {
            if (channel.Path != Path::Rotation)
            {
                return;
            }
            for (size_t i = 1; i < channel.Samples.size(); ++i)
            {
                const auto& previous = channel.Samples[i - 1].Value;
                auto& current = channel.Samples[i].Value;
                const double dot = double(previous.X) * current.X + double(previous.Y) * current.Y +
                                   double(previous.Z) * current.Z + double(previous.W) * current.W;
                if (dot < 0)
                {
                    current.X = -current.X;
                    current.Y = -current.Y;
                    current.Z = -current.Z;
                    current.W = -current.W;
                }
            }
        }

        double RotationDistance(const Value& left, const Value& right)
        {
            if ((left.X == right.X && left.Y == right.Y && left.Z == right.Z && left.W == right.W) ||
                (left.X == -right.X && left.Y == -right.Y && left.Z == -right.Z && left.W == -right.W))
            {
                return 0;
            }
            const auto a = Detail::NormalizeQuaternion({left.X, left.Y, left.Z, left.W});
            const auto b = Detail::NormalizeQuaternion({right.X, right.Y, right.Z, right.W});
            const double aLength = std::sqrt(double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z +
                                             double(a.w) * a.w);
            const double bLength = std::sqrt(double(b.x) * b.x + double(b.y) * b.y + double(b.z) * b.z +
                                             double(b.w) * b.w);
            if (aLength == 0 || bLength == 0)
            {
                // 実行時の正規化が零四元数を単位回転として扱う場合に合わせる。
                const double aw = aLength == 0 ? 1 : a.w / aLength;
                const double bw = bLength == 0 ? 1 : b.w / bLength;
                return 2 * std::acos(std::clamp(std::abs(aw * bw), 0.0, 1.0));
            }
            const double dot = (double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z + double(a.w) * b.w) /
                               (aLength * bLength);
            return 2 * std::acos(std::clamp(std::abs(dot), 0.0, 1.0));
        }

        double ValueDistance(Path path, const Value& left, const Value& right)
        {
            if (!IsFinite(left) || !IsFinite(right))
            {
                return std::numeric_limits<double>::infinity();
            }
            if (path == Path::Rotation)
            {
                return RotationDistance(left, right);
            }
            if (path == Path::Scale)
            {
                return std::max({std::abs(double(left.X) - right.X), std::abs(double(left.Y) - right.Y),
                                 std::abs(double(left.Z) - right.Z)});
            }
            return Distance(left.X, left.Y, left.Z, right.X, right.Y, right.Z);
        }

        Value DefaultValue(Path path, const Skeletal::SkeletalRestTransform& rest)
        {
            if (path == Path::Rotation)
            {
                return rest.Rotation;
            }
            if (path == Path::Scale)
            {
                return {rest.Scale.X, rest.Scale.Y, rest.Scale.Z, 0};
            }
            return {rest.Translation.X, rest.Translation.Y, rest.Translation.Z, 0};
        }

        bool AllWithin(Container::Span<const Sample> samples, Path path, const Value& reference, double tolerance)
        {
            for (const auto& sample : samples)
            {
                if (!(ValueDistance(path, sample.Value, reference) <= tolerance))
                {
                    return false;
                }
            }
            return true;
        }

        double Tolerance(Path path, double radius, const ClipOptimizeSettings& settings, double scale)
        {
            constexpr double DegreesToRadians = 0.017453292519943295769236907684886;
            const double position = std::min(settings.MaxJointErrorMeters, settings.MaxVertexErrorMeters) * 0.5 * scale;
            if (path == Path::Rotation)
            {
                const double rotation = settings.MaxRotationErrorDegrees * DegreesToRadians * scale;
                return radius > 0 ? std::min(rotation, position / radius) : rotation;
            }
            if (path == Path::Scale)
            {
                return radius > 0 ? position / radius : position;
            }
            return position;
        }

        Container::VariableArray<Sample> ReduceKeys(const Channel& channel, double tolerance)
        {
            const auto& samples = channel.Samples;
            if (samples.size() <= 2)
            {
                return samples;
            }
            Container::VariableArray<uint8_t> keep(samples.size(), 0);
            Container::VariableArray<Interval> pending;
            keep.front() = 1;
            keep.back() = 1;
            pending.push_back({0, samples.size() - 1});
            while (!pending.empty())
            {
                const Interval interval = pending.back();
                pending.pop_back();
                double maximum = tolerance;
                size_t split = SIZE_MAX;
                for (size_t i = interval.First + 1; i < interval.Last; ++i)
                {
                    const Value interpolated = Detail::SampleSkeletalChannelInterval(
                        channel, samples[interval.First], samples[interval.Last], samples[i].TimeSeconds);
                    const double error = ValueDistance(channel.Path, interpolated, samples[i].Value);
                    const bool bExceeded = !std::isfinite(error) || error > tolerance;
                    if (bExceeded && (split == SIZE_MAX || error > maximum))
                    {
                        maximum = error;
                        split = i;
                    }
                }
                if (split != SIZE_MAX)
                {
                    keep[split] = 1;
                    pending.push_back({split, interval.Last});
                    pending.push_back({interval.First, split});
                }
            }
            Container::VariableArray<Sample> reduced;
            reduced.reserve(samples.size());
            for (size_t i = 0; i < samples.size(); ++i)
            {
                if (keep[i])
                {
                    reduced.push_back(samples[i]);
                }
            }
            return reduced;
        }

        void SelectChannels(const Skeletal::SkeletalAnimationClip& clip, Container::VariableArray<uint8_t>& selected,
                            Container::VariableArray<uint8_t>& hasEarlier)
        {
            selected.assign(clip.Channels.size(), 0);
            hasEarlier.assign(clip.Channels.size(), 0);
            Container::VariableArray<SelectionEntry> entries;
            entries.reserve(clip.Channels.size());
            for (size_t i = 0; i < clip.Channels.size(); ++i)
            {
                const auto& channel = clip.Channels[i];
                if (!channel.Samples.empty())
                {
                    entries.push_back({channel.JointIndex, static_cast<uint32_t>(channel.Path), i});
                }
            }
            std::sort(entries.begin(), entries.end(), [](const SelectionEntry& a, const SelectionEntry& b)
            {
                if (a.Joint != b.Joint)
                {
                    return a.Joint < b.Joint;
                }
                if (a.PathIndex != b.PathIndex)
                {
                    return a.PathIndex < b.PathIndex;
                }
                return a.Index < b.Index;
            });
            size_t first = 0;
            while (first < entries.size())
            {
                size_t last = first + 1;
                while (last < entries.size() && entries[last].Joint == entries[first].Joint &&
                       entries[last].PathIndex == entries[first].PathIndex)
                {
                    ++last;
                }
                selected[entries[last - 1].Index] = 1;
                hasEarlier[entries[last - 1].Index] = last - first > 1;
                first = last;
            }
        }

        void RestoreEndpoints(ChannelResult& result, const Channel& source, ClipCompressionClipReport& report)
        {
            if (result.bRemoved)
            {
                result.bRemoved = false;
                --report.RemovedChannels;
            }
            if (result.bConstant)
            {
                result.bConstant = false;
                --report.ConstantChannels;
            }
            result.Data = source;
            if (source.Samples.size() > 1)
            {
                result.Data.Samples.clear();
                result.Data.Samples.push_back(source.Samples.front());
                result.Data.Samples.push_back(source.Samples.back());
            }
        }

        void OptimizeClip(const ClipOptimizeRig& rig, const Skeletal::SkeletalAnimationClip& input,
                          const Container::VariableArray<double>& radii, const ClipOptimizeSettings& settings,
                          double scale, Skeletal::SkeletalAnimationClip& output, ClipCompressionClipReport& report)
        {
            report.Name = input.Name;
            report.DurationSeconds = input.DurationSeconds;
            report.ChannelsBefore = static_cast<uint32_t>(input.Channels.size());
            report.Attempts = 1;
            report.ToleranceScale = scale;
            report.Joints.reserve(rig.Joints.size());
            for (size_t joint = 0; joint < rig.Joints.size(); ++joint)
            {
                report.Joints.push_back({static_cast<uint32_t>(joint), 0, 0});
            }
            Container::VariableArray<uint8_t> selected, hasEarlier;
            SelectChannels(input, selected, hasEarlier);
            Container::VariableArray<ChannelResult> results;
            Container::VariableArray<Channel> sources;
            results.reserve(input.Channels.size());
            sources.reserve(input.Channels.size());
            bool bHasEnd = false;
            float end = 0;
            size_t firstEnd = SIZE_MAX;
            for (size_t i = 0; i < input.Channels.size(); ++i)
            {
                sources.push_back(input.Channels[i]);
                Channel& source = sources.back();
                if (selected[i])
                {
                    MakeRotationContinuous(source);
                }
                report.KeysBefore += source.Samples.size();
                if (!source.Samples.empty() && (!bHasEnd || source.Samples.back().TimeSeconds > end))
                {
                    bHasEnd = true;
                    end = source.Samples.back().TimeSeconds;
                    firstEnd = i;
                }
                ChannelResult result;
                result.Data = source;
                if (selected[i])
                {
                    const double tolerance = Tolerance(source.Path, radii[source.JointIndex], settings, scale);
                    const Value rest = DefaultValue(source.Path, rig.DefaultLocal[source.JointIndex]);
                    // 前の重複を残したまま勝者を消すと、前の値が有効になる。
                    const bool bCanRemove = rig.Joints[source.JointIndex].ParentIndex >= 0 &&
                                            source.JointIndex != input.RootMotionJoint && !hasEarlier[i];
                    const bool bAtDefault = bCanRemove && AllWithin(source.Samples, source.Path, rest, tolerance);
                    if (bAtDefault)
                    {
                        result.bRemoved = true;
                        ++report.RemovedChannels;
                    }
                    else if (source.Samples.size() > 1 &&
                             AllWithin(source.Samples, source.Path, source.Samples.front().Value, tolerance))
                    {
                        // 削減後だけ既定値内になると、次の適用で削除されてしまう。
                        const bool bWouldRemoveNext = bCanRemove &&
                            ValueDistance(source.Path, source.Samples.front().Value, rest) <= tolerance;
                        if (!bWouldRemoveNext)
                        {
                            result.Data.Samples.resize(1);
                            result.bConstant = true;
                            ++report.ConstantChannels;
                        }
                    }
                    else if (source.Samples.size() > 2)
                    {
                        auto reduced = ReduceKeys(source, tolerance);
                        const bool bWouldRemoveNext = bCanRemove && AllWithin(reduced, source.Path, rest, tolerance);
                        const bool bWouldBecomeConstant =
                            AllWithin(reduced, source.Path, reduced.front().Value, tolerance);
                        // 再適用時に別の段で縮む場合は元のキーを保持する。
                        if (!bWouldRemoveNext && !bWouldBecomeConstant)
                        {
                            result.Data.Samples = std::move(reduced);
                        }
                    }
                }
                results.push_back(std::move(result));
            }
            bool bOutputHasEnd = false;
            float outputEnd = 0;
            for (const auto& result : results)
            {
                if (!result.bRemoved && !result.Data.Samples.empty() &&
                    (!bOutputHasEnd || result.Data.Samples.back().TimeSeconds > outputEnd))
                {
                    bOutputHasEnd = true;
                    outputEnd = result.Data.Samples.back().TimeSeconds;
                }
            }
            if (bHasEnd && (!bOutputHasEnd || outputEnd != end))
            {
                RestoreEndpoints(results[firstEnd], sources[firstEnd], report);
            }
            bool bHasChannel = false;
            for (const auto& result : results)
            {
                bHasChannel = bHasChannel || !result.bRemoved;
            }
            if (!bHasChannel && !results.empty())
            {
                RestoreEndpoints(results.front(), sources.front(), report);
            }
            output = input;
            output.Channels.clear();
            output.Channels.reserve(results.size());
            for (size_t i = 0; i < results.size(); ++i)
            {
                auto& result = results[i];
                if (!result.bRemoved)
                {
                    if (selected[i])
                    {
                        MakeRotationContinuous(result.Data);
                    }
                    report.KeysAfter += result.Data.Samples.size();
                    output.Channels.push_back(std::move(result.Data));
                }
            }
            report.ChannelsAfter = static_cast<uint32_t>(output.Channels.size());
            report.BytesBefore = report.KeysBefore * 20;
            report.BytesAfter = report.KeysAfter * 20;
        }
    } // namespace

    bool BuildLegacyClipOptimizeDefaultLocal(Container::Span<const Skeletal::SkeletalJoint> joints,
                                             Container::VariableArray<Skeletal::SkeletalRestTransform>& out)
    {
        if (joints.empty())
        {
            return false;
        }
        Container::VariableArray<Math::Matrix4x4> globals;
        if (!BuildBindGlobals(joints, globals))
        {
            return false;
        }
        for (auto& global : globals)
        {
            global = global * Math::Matrix4x4::Identity;
            if (!Detail::IsFiniteMatrix(global))
            {
                return false;
            }
        }
        Container::VariableArray<Skeletal::SkeletalRestTransform> result;
        result.reserve(joints.size());
        for (size_t i = 0; i < joints.size(); ++i)
        {
            const int32_t parent = joints[i].ParentIndex;
            Math::Matrix4x4 local;
            if (!Detail::TryBuildBindLocalRow(globals[i], parent >= 0 ? &globals[parent] : nullptr, local))
            {
                return false;
            }
            const auto transform = FromRowMatrix(local);
            result.push_back({{transform.Translation.x, transform.Translation.y, transform.Translation.z},
                              {transform.Rotation.x, transform.Rotation.y, transform.Rotation.z, transform.Rotation.w},
                              {transform.Scale.x, transform.Scale.y, transform.Scale.z}});
        }
        out = std::move(result);
        return true;
    }

    bool OptimizeSkeletalClips(const ClipOptimizeRig& rig, Container::Span<const Skeletal::SkeletalAnimationClip> clips,
                               Container::VariableArray<Skeletal::SkeletalAnimationClip>& out,
                               const ClipOptimizeSettings& settings, ClipCompressionReport& report)
    {
        const ClipOptimizeSettings requested = settings;
        report = {};
        report.Settings = requested;
        if (!std::isfinite(requested.MaxVertexErrorMeters) || requested.MaxVertexErrorMeters <= 0 ||
            !std::isfinite(requested.MaxJointErrorMeters) || requested.MaxJointErrorMeters <= 0 ||
            !std::isfinite(requested.MaxRotationErrorDegrees) || requested.MaxRotationErrorDegrees <= 0)
        {
            report.FailureReason = _T("誤差の上限は有限の正の値で指定してください。");
            return false;
        }
        if (rig.DefaultLocal.size() != rig.Joints.size())
        {
            report.FailureReason = _T("既定ローカルの数が関節数と一致しません。");
            return false;
        }
        for (const auto& rest : rig.DefaultLocal)
        {
            if (!IsFinite(rest.Translation) || !IsFinite(rest.Rotation) || !IsFinite(rest.Scale))
            {
                report.FailureReason = _T("既定ローカルに有限でない値があります。");
                return false;
            }
        }
        for (const auto& clip : clips)
        {
            if (!std::isfinite(clip.DurationSeconds) || clip.DurationSeconds < 0 || clip.Channels.empty() ||
                clip.Channels.size() > std::numeric_limits<uint32_t>::max())
            {
                report.FailureReason = _T("クリップの長さまたはチャンネル数が不正です。");
                return false;
            }
            uint64_t keys = 0;
            for (const auto& channel : clip.Channels)
            {
                if (channel.JointIndex >= rig.Joints.size() || static_cast<uint32_t>(channel.Path) > 2 ||
                    static_cast<uint32_t>(channel.Interpolation) > 1)
                {
                    report.FailureReason = _T("チャンネルの関節番号または種類が不正です。");
                    return false;
                }
                if (channel.Samples.size() > std::numeric_limits<uint64_t>::max() / 20 - keys)
                {
                    report.FailureReason = _T("クリップのキー数が大きすぎます。");
                    return false;
                }
                keys += channel.Samples.size();
                bool bFirst = true;
                float previous = 0;
                for (const auto& sample : channel.Samples)
                {
                    if (!std::isfinite(sample.TimeSeconds) || !IsFinite(sample.Value) ||
                        (!bFirst && !(sample.TimeSeconds > previous)))
                    {
                        report.FailureReason = _T("キーの値または時刻が不正です。");
                        return false;
                    }
                    bFirst = false;
                    previous = sample.TimeSeconds;
                }
            }
        }
        Container::VariableArray<double> radii;
        if (!BuildJointRadii(rig, radii))
        {
            report.FailureReason = _T("骨格または頂点のバインド情報が不正です。");
            return false;
        }
        Container::VariableArray<Skeletal::SkeletalAnimationClip> result;
        result.reserve(clips.size());
        report.Clips.reserve(clips.size());
        for (const auto& clip : clips)
        {
            result.push_back({});
            report.Clips.push_back({});
            OptimizeClip(rig, clip, radii, requested, 1.0, result.back(), report.Clips.back());
        }
        out = std::move(result);
        report.bSucceeded = true;
        return true;
    }

    bool WriteClipCompressionReportJson(const ClipCompressionReport& report, Container::String& outJson)
    {
        uint64_t keysBefore = 0, keysAfter = 0, bytesBefore = 0, bytesAfter = 0;
        uint64_t removed = 0, constant = 0;
        const auto add = [](uint64_t& total, uint64_t value)
        {
            if (value > std::numeric_limits<uint64_t>::max() - total)
            {
                return false;
            }
            total += value;
            return true;
        };
        for (const auto& clip : report.Clips)
        {
            if (!add(keysBefore, clip.KeysBefore) || !add(keysAfter, clip.KeysAfter) ||
                !add(bytesBefore, clip.BytesBefore) || !add(bytesAfter, clip.BytesAfter) ||
                !add(removed, clip.RemovedChannels) || !add(constant, clip.ConstantChannels))
            {
                return false;
            }
        }
        JsonWriter writer;
        // 不正な入力値も失敗報告として記録できるよう、JSONで表せない数値はnullにする。
        const auto writeNumber = [&writer](const char* key, double value)
        {
            if (std::isfinite(value))
            {
                writer.WriteNumber(key, value);
            }
            else
            {
                writer.WriteNull(key);
            }
        };
        writer.BeginObject();
        writer.WriteUInt64("version", 1);
        writer.WriteBool("succeeded", report.bSucceeded);
        writer.WriteString("failure_reason", report.FailureReason);
        writer.BeginObject("settings");
        writeNumber("max_vertex_error_m", report.Settings.MaxVertexErrorMeters);
        writeNumber("max_joint_error_m", report.Settings.MaxJointErrorMeters);
        writeNumber("max_rotation_error_deg", report.Settings.MaxRotationErrorDegrees);
        writer.EndObject();
        writer.BeginObject("totals");
        writer.WriteUInt64("keys_before", keysBefore);
        writer.WriteUInt64("keys_after", keysAfter);
        writer.WriteUInt64("bytes_before", bytesBefore);
        writer.WriteUInt64("bytes_after", bytesAfter);
        writer.WriteUInt64("removed_channels", removed);
        writer.WriteUInt64("constant_channels", constant);
        writer.EndObject();
        writer.BeginArray("clips");
        for (const auto& clip : report.Clips)
        {
            writer.BeginObject();
            writer.WriteString("name", clip.Name);
            writeNumber("duration_s", clip.DurationSeconds);
            writer.WriteUInt64("keys_before", clip.KeysBefore);
            writer.WriteUInt64("keys_after", clip.KeysAfter);
            writer.WriteUInt64("bytes_before", clip.BytesBefore);
            writer.WriteUInt64("bytes_after", clip.BytesAfter);
            writer.WriteUInt64("channels_before", clip.ChannelsBefore);
            writer.WriteUInt64("channels_after", clip.ChannelsAfter);
            writer.WriteUInt64("removed_channels", clip.RemovedChannels);
            writer.WriteUInt64("constant_channels", clip.ConstantChannels);
            writer.WriteUInt64("attempts", clip.Attempts);
            writeNumber("tolerance_scale", clip.ToleranceScale);
            writeNumber("max_vertex_error_m", clip.MaxVertexErrorMeters);
            writer.BeginArray("joints");
            for (const auto& joint : clip.Joints)
            {
                writer.BeginObject();
                writer.WriteUInt64("joint", joint.JointIndex);
                writeNumber("max_position_error_m", joint.MaxPositionErrorMeters);
                writeNumber("max_rotation_error_deg", joint.MaxRotationErrorDegrees);
                writer.EndObject();
            }
            writer.EndArray();
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        if (!writer.IsComplete())
        {
            return false;
        }
        outJson = writer.ToString();
        return true;
    }
} // namespace NorvesLib::Core::Animation
