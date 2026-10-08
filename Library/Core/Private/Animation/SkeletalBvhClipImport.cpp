#include "Animation/SkeletalBvhClipImport.h"
#include "Animation/SkeletalClipSampling.h"
#include "Animation/SkeletalBindRowMath.h"
#include "Animation/SkeletalRotationRetargetMath.h"
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        using Status = SkeletalBvhClipStatus;
        using Char = Container::String::value_type;
        SkeletalBvhClipResult Failure(Status status, uint32_t frame = UINT32_MAX)
        {
            SkeletalBvhClipResult result;
            result.Status = status;
            result.FrameIndex = frame;
            return result;
        }
        bool AddBytes(uint64_t count, uint64_t stride, uint64_t& total)
        {
            uint64_t bytes = 0;
            return Detail::BvhClipCheckedMultiply(count, stride, bytes) &&
                   Detail::BvhClipCheckedAdd(total, bytes, total);
        }
        void Include(SkeletalBvhClipRange& range, double value)
        {
            if (!range.bHasValue)
            {
                range = {true, value, value};
            }
            else
            {
                range.Minimum = std::min(range.Minimum, value);
                range.Maximum = std::max(range.Maximum, value);
            }
        }
        void Include(Container::FixedArray<SkeletalBvhClipRange, 3>& ranges, const Bvh::Vector3d& value)
        {
            Include(ranges[0], value.X);
            Include(ranges[1], value.Y);
            Include(ranges[2], value.Z);
        }
        void ObserveRoot(const Bvh::BvhDocument& source, uint32_t frame,
                         const Container::FixedArray<uint32_t, 3>& indices, const SkeletalBvhClipSettings& settings,
                         const SkeletalCoordinateConversion& conversion, SkeletalBvhRootPositionReport& report)
        {
            const size_t offset = static_cast<size_t>(frame) * source.Channels.size();
            double raw[3]{};
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (indices[axis] != UINT32_MAX)
                {
                    raw[axis] = source.Values[offset + indices[axis]];
                    Include(report.Raw[axis], raw[axis]);
                }
            }
            Bvh::Vector3d delta;
            auto issue = SkeletalBvhRootDeltaIssue::None;
            if (report.PositionMask != 0 && report.PositionMask != 7)
            {
                issue = SkeletalBvhRootDeltaIssue::IncompleteChannels;
            }
            else if (report.PositionMask != 0 && !report.bPositionBeforeRotations)
            {
                issue = SkeletalBvhRootDeltaIssue::UnsupportedLayout;
            }
            else if (report.PositionMask == 7)
            {
                if (settings.Translation == Bvh::TranslationConvention::OffsetPlusChannels)
                {
                    delta = {raw[0], raw[1], raw[2]};
                }
                else
                {
                    delta = {raw[0] - report.OriginalOffset.X, raw[1] - report.OriginalOffset.Y,
                             raw[2] - report.OriginalOffset.Z};
                }
                if (!std::isfinite(delta.X) || !std::isfinite(delta.Y) || !std::isfinite(delta.Z))
                {
                    issue = SkeletalBvhRootDeltaIssue::UnrepresentableDelta;
                }
            }
            if (issue != SkeletalBvhRootDeltaIssue::None)
            {
                if (report.UnavailableDeltaFrames == 0)
                {
                    report.FirstUnavailableDeltaFrame = frame;
                    report.FirstDeltaIssue = issue;
                }
                ++report.UnavailableDeltaFrames;
                if (report.UnavailableCanonicalFrames == 0)
                {
                    report.FirstUnavailableCanonicalFrame = frame;
                }
                ++report.UnavailableCanonicalFrames;
                return;
            }
            Include(report.Delta, delta);
            Bvh::Vector3d canonical;
            const auto converted = ConvertSkeletalTranslation(conversion, delta, canonical);
            if (converted == SkeletalCoordinateStatus::Success)
            {
                Include(report.CanonicalDelta, canonical);
            }
            else
            {
                if (report.ConversionFailureFrames == 0)
                {
                    report.FirstConversionFailureFrame = frame;
                    report.FirstConversionIssue = converted;
                }
                ++report.ConversionFailureFrames;
                if (report.UnavailableCanonicalFrames == 0)
                {
                    report.FirstUnavailableCanonicalFrame = frame;
                }
                ++report.UnavailableCanonicalFrames;
            }
        }
        SkeletalBvhClipResult SourceFrame(const Bvh::BvhRotationSourcePlan& source, uint32_t frame,
                                          const SkeletalCoordinateConversion& conversion,
                                          Container::Span<Bvh::Matrix3d> worlds,
                                          Container::Span<SkeletalRetargetSourceRotation> native)
        {
            const auto evaluated = Bvh::EvaluateBvhRotationFrame(source, frame, worlds);
            if (!evaluated.Succeeded())
            {
                auto result = Failure(Status::SourceRejected, frame);
                result.Source = evaluated;
                return result;
            }
            for (size_t i = 0; i < worlds.size(); ++i)
            {
                const auto converted = ConvertSkeletalMatrixBasis(conversion, worlds[i], native[i].WorldRotation);
                if (converted != SkeletalCoordinateStatus::Success)
                {
                    auto result = Failure(Status::CoordinateRejected, frame);
                    result.Coordinate = converted;
                    return result;
                }
            }
            return {Status::Success};
        }
    } // namespace
    SkeletalBvhClipResult ImportSkeletalBvhRotationClip(
        const Bvh::BvhDocument& source, Container::Span<const Skeletal::SkeletalJoint> target,
        const Math::Matrix4x4& meshGlobal, const SkeletalJointMappingSet& mappings,
        Container::Span<const Bvh::Matrix3d> corrections, const Container::String& clipName,
        const SkeletalBvhClipSettings& settings, const SkeletalBvhClipLimits& limits, SkeletalBvhClipOutput& out)
    {
        const size_t sourceCount = source.Joints.size(), targetCount = target.size(),
                     mappingCount = mappings.Pairs.size();
        if (sourceCount == 0 || targetCount == 0 || mappingCount == 0 || source.FrameCount == 0 ||
            corrections.size() != mappingCount || !Detail::RetargetValidSpan(target) ||
            !Detail::RetargetValidSpan(corrections))
        {
            return Failure(Status::InvalidInput);
        }
        if (sourceCount > SkeletalRetargetMaxJoints || targetCount > SkeletalRetargetMaxJoints ||
            mappingCount > SkeletalRetargetMaxJoints || sourceCount > limits.Source.MaxJoints ||
            targetCount > limits.TargetNames.MaxJoints || source.FrameCount > limits.Source.MaxFrames ||
            source.Values.size() > limits.Source.MaxValues)
        {
            return Failure(Status::LimitExceeded);
        }
        if (mappings.SourceJointCount != sourceCount || mappings.TargetJointCount != targetCount ||
            mappings.RootMappingIndex >= mappingCount)
        {
            return Failure(Status::InvalidMapping);
        }
        if ((settings.Translation != Bvh::TranslationConvention::OffsetPlusChannels &&
             settings.Translation != Bvh::TranslationConvention::AbsoluteLocalChannels) ||
            (settings.SourceReuse != SkeletalSourceReusePolicy::Allow &&
             settings.SourceReuse != SkeletalSourceReusePolicy::Reject) ||
            settings.Rotation != SkeletalRetargetRotationPolicy::PreserveHeadingHoldTranslations ||
            (settings.TimeMode != SkeletalBvhClipTimeMode::HeaderFrameTime &&
             settings.TimeMode != SkeletalBvhClipTimeMode::OverrideFps) ||
            (settings.TimeMode == SkeletalBvhClipTimeMode::HeaderFrameTime && settings.SourceFps != 0))
        {
            return Failure(Status::InvalidSettings);
        }
        SkeletalCoordinateConversion conversion;
        const auto converted = BuildSkeletalCoordinateConversion(settings.Up, settings.Forward, settings.Handedness,
                                                                 settings.PositionScale, conversion);
        if (converted != SkeletalCoordinateStatus::Success)
        {
            auto result = Failure(Status::CoordinateRejected);
            result.Coordinate = converted;
            return result;
        }
        const auto firstTime =
            ComputeSkeletalBvhClipTime(settings.TimeMode, source.FrameTimeSeconds, settings.SourceFps, 0, false, 0);
        if (!firstTime.Succeeded())
        {
            auto result = Failure(Status::TimeRejected, 0);
            result.Time = firstTime.Status;
            return result;
        }
        uint64_t keys = 0, jointFrames = 0, jointSum = 0;
        if (!Detail::BvhClipCheckedMultiply(source.FrameCount, mappingCount, keys) || keys > limits.MaxOutputKeys ||
            !Detail::BvhClipCheckedAdd(sourceCount, targetCount, jointSum) ||
            !Detail::BvhClipCheckedMultiply(source.FrameCount, jointSum, jointFrames) ||
            jointFrames > limits.MaxJointFrames)
        {
            return Failure(Status::LimitExceeded);
        }
        const auto clipMeasured = Asset::MeasureSkeletalNameEncoding<Char>(2, {clipName.data(), clipName.size()});
        if (clipName.empty() || !clipMeasured.Succeeded())
        {
            return Failure(Status::InvalidClipName);
        }
        if (clipMeasured.ByteCount > limits.MaxClipNameBytes)
        {
            return Failure(Status::LimitExceeded);
        }
        uint64_t sourceNames = 0, targetNames = 0;
        for (const auto& joint : source.Joints)
        {
            if (joint.Name.size() > limits.Source.MaxNameBytes ||
                !Detail::BvhClipCheckedAdd(sourceNames, joint.Name.size(), sourceNames) ||
                sourceNames > limits.Source.MaxTotalNameBytes)
            {
                return Failure(Status::LimitExceeded);
            }
        }
        for (size_t i = 0; i < targetCount; ++i)
        {
            const auto& name = target[i].Name;
            const auto measured = Asset::MeasureSkeletalNameEncoding<Char>(2, {name.data(), name.size()});
            if (name.empty() || !measured.Succeeded())
            {
                auto result = Failure(Status::InvalidTargetName);
                result.TargetNames = {name.empty() ? SkeletalJointIndexStatus::EmptyName
                                                   : SkeletalJointIndexStatus::InvalidName,
                                      i, SIZE_MAX, measured.Status};
                return result;
            }
            if (measured.ByteCount > limits.TargetNames.MaxNameBytes ||
                !Detail::BvhClipCheckedAdd(targetNames, measured.ByteCount, targetNames) ||
                targetNames > limits.TargetNames.MaxTotalBytes)
            {
                return Failure(Status::LimitExceeded);
            }
        }
        uint64_t names = 0, workUnits = 0, planned = sizeof(SkeletalBvhClipOutput), clipUnits = 0;
        if (!Detail::BvhClipCheckedAdd(sourceNames, targetNames, names) ||
            !Detail::BvhClipCheckedAdd(names, clipMeasured.ByteCount, names) ||
            !Detail::ComputeSkeletalBvhClipWork(sourceCount, targetCount, mappingCount, source.FrameCount,
                                                source.Values.size(), names, workUnits) ||
            workUnits > limits.MaxWorkUnits || !Detail::BvhClipCheckedAdd(clipName.size(), 1, clipUnits) ||
            !AddBytes(clipUnits, 2 * sizeof(Char), planned) ||
            !AddBytes(keys, sizeof(Skeletal::SkeletalAnimationSample), planned) ||
            !AddBytes(mappingCount, sizeof(Skeletal::SkeletalAnimationChannel), planned) ||
            !AddBytes(jointSum, sizeof(Container::VariableArray<uint8_t>), planned) || !AddBytes(names, 4, planned) ||
            !AddBytes(mappingCount, sizeof(SkeletalJointMappingPair) + sizeof(Bvh::Matrix3d), planned) ||
            !AddBytes(jointSum, sizeof(uint32_t) + sizeof(uint8_t), planned) ||
            !AddBytes(sourceCount, 3 * sizeof(SkeletalBvhIgnoredPosition), planned) ||
            !AddBytes(sourceCount,
                      Bvh::BvhRotationSourcePlan::GetJointStorageSize() + sizeof(SkeletalRetargetSourceRotation) +
                          sizeof(Bvh::Matrix3d),
                      planned) ||
            !AddBytes(mappingCount, 3 * sizeof(SkeletalRetargetRotationValue), planned) ||
            !AddBytes(targetCount,
                      4 * sizeof(Math::Matrix4x4) + sizeof(Detail::JointTransform) +
                          sizeof(SkeletalRetargetTargetRotation) + sizeof(SkeletalRetargetRotationWork) +
                          sizeof(uint8_t),
                      planned) ||
            !AddBytes(jointSum, sizeof(SkeletalJointNameView) + 3 * sizeof(uint32_t), planned) ||
            !AddBytes(targetCount, sizeof(Container::VariableArray<uint8_t>), planned) ||
            planned > limits.MaxOwnedBytes)
        {
            return Failure(Status::LimitExceeded);
        }
        if (source.FrameCount > Container::VariableArray<Skeletal::SkeletalAnimationSample>{}.max_size() ||
            mappingCount > Container::VariableArray<Skeletal::SkeletalAnimationChannel>{}.max_size())
        {
            return Failure(Status::LimitExceeded);
        }
        Bvh::BvhRotationSourcePlan sourcePlan;
        const auto prepared = Bvh::PrepareBvhRotationSource(source, limits.Source, sourcePlan);
        if (!prepared.Succeeded())
        {
            auto result = Failure(Status::SourceRejected, prepared.FrameIndex);
            result.Source = prepared;
            return result;
        }
        {
            SkeletalJointIndex targetIndex;
            const auto indexed = BuildSkeletalJointIndexFromJoints(target, limits.TargetNames, targetIndex);
            if (!indexed.Succeeded())
            {
                auto result = Failure(Status::InvalidTargetName);
                result.TargetNames = indexed;
                return result;
            }
        }
        Container::VariableArray<uint8_t> sourceUsed(sourceCount, 0), targetUsed(targetCount, 0);
        const auto rootPair = mappings.Pairs[mappings.RootMappingIndex];
        if (rootPair.SourceIndex != 0 || rootPair.TargetIndex >= targetCount ||
            target[rootPair.TargetIndex].ParentIndex != -1)
        {
            return Failure(Status::InvalidMapping);
        }
        for (size_t i = 0; i < mappingCount; ++i)
        {
            const auto& pair = mappings.Pairs[i];
            if (pair.SourceIndex >= sourceCount || pair.TargetIndex >= targetCount || targetUsed[pair.TargetIndex] ||
                (sourceUsed[pair.SourceIndex] && settings.SourceReuse == SkeletalSourceReusePolicy::Reject))
            {
                return Failure(Status::InvalidMapping);
            }
            sourceUsed[pair.SourceIndex] = 1;
            targetUsed[pair.TargetIndex] = 1;
        }
        SkeletalBvhClipOutput candidate;
        candidate.Clip.Name = clipName;
        auto& report = candidate.Report;
        report.Settings = settings;
        report.SourceFrames = source.FrameCount;
        report.SourceJoints = static_cast<uint32_t>(sourceCount);
        report.TargetJoints = static_cast<uint32_t>(targetCount);
        report.OutputKeys = keys;
        report.JointFrames = jointFrames;
        report.WorkUnits = workUnits;
        report.PlannedOwnedBytes = planned;
        report.HeaderFrameTimeSeconds = source.FrameTimeSeconds;
        report.SelectedIntervalSeconds = firstTime.IntervalSeconds;
        report.SourceNames.resize(sourceCount);
        report.TargetNames.resize(targetCount);
        report.UnmappedSource.reserve(sourceCount);
        report.UnmappedTarget.reserve(targetCount);
        report.IgnoredNonRootPositions.reserve(3 * (sourceCount - 1));
        for (size_t i = 0; i < sourceCount; ++i)
        {
            report.SourceNames[i] = source.Joints[i].Name;
            if (!sourceUsed[i])
            {
                report.UnmappedSource.push_back(static_cast<uint32_t>(i));
            }
        }
        for (size_t i = 0; i < targetCount; ++i)
        {
            const auto& name = target[i].Name;
            const auto measured = Asset::MeasureSkeletalNameEncoding<Char>(2, {name.data(), name.size()});
            if (!measured.Succeeded())
            {
                return Failure(Status::InvalidTargetName);
            }
            report.TargetNames[i].resize(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName<Char>(2, {name.data(), name.size()}, report.TargetNames[i]).Succeeded())
            {
                return Failure(Status::InvalidTargetName);
            }
            if (!targetUsed[i])
            {
                report.UnmappedTarget.push_back(static_cast<uint32_t>(i));
            }
        }
        report.Mappings = mappings.Pairs;
        report.Corrections.assign(corrections.begin(), corrections.end());
        Container::FixedArray<uint32_t, 3> rootPositions{UINT32_MAX, UINT32_MAX, UINT32_MAX};
        report.RootPosition.OriginalOffset = source.Joints[0].Offset;
        bool bRootRotationStarted = false;
        for (size_t i = 0; i < sourceCount; ++i)
        {
            const auto& joint = source.Joints[i];
            for (size_t channel = joint.ChannelOffset;
                 channel < static_cast<size_t>(joint.ChannelOffset) + joint.ChannelCount; ++channel)
            {
                const auto kind = source.Channels[channel];
                const auto axis = static_cast<unsigned>(kind);
                if (axis > static_cast<unsigned>(Bvh::Channel::Zposition))
                {
                    if (i == 0)
                    {
                        bRootRotationStarted = true;
                    }
                    continue;
                }
                if (i == 0 && bRootRotationStarted)
                {
                    report.RootPosition.bPositionBeforeRotations = false;
                }
                if (i == 0)
                {
                    rootPositions[axis] = static_cast<uint32_t>(channel);
                    report.RootPosition.PositionMask |= static_cast<uint8_t>(1u << axis);
                }
                else
                {
                    report.IgnoredNonRootPositions.push_back(
                        {static_cast<uint32_t>(i), static_cast<uint32_t>(channel), kind, {}});
                }
            }
        }
        candidate.Clip.Channels.resize(mappingCount);
        for (size_t i = 0; i < mappingCount; ++i)
        {
            auto& channel = candidate.Clip.Channels[i];
            channel.JointIndex = mappings.Pairs[i].TargetIndex;
            channel.Path = Skeletal::SkeletalAnimationPath::Rotation;
            channel.Interpolation = Skeletal::SkeletalAnimationInterpolation::Linear;
            channel.Samples.reserve(source.FrameCount);
        }
        Container::VariableArray<Bvh::Matrix3d> sourceWorlds(sourceCount);
        Container::VariableArray<SkeletalRetargetSourceRotation> nativeSource(sourceCount);
        Container::VariableArray<SkeletalRetargetRotationValue> values(mappingCount), sampled(mappingCount);
        for (size_t i = 0; i < sourceCount; ++i)
        {
            nativeSource[i].ParentIndex =
                sourcePlan.GetParent(i) == UINT32_MAX ? -1 : static_cast<int32_t>(sourcePlan.GetParent(i));
        }
        const Container::Span<Bvh::Matrix3d> worldView(sourceWorlds);
        const Container::Span<SkeletalRetargetSourceRotation> nativeView(nativeSource);
        const SkeletalRetargetNativeInput nativeInput{
            Container::Span<const SkeletalRetargetSourceRotation>(nativeSource),
            Container::Span<const SkeletalJointMappingPair>(mappings.Pairs),
            corrections,
            {rootPair.SourceIndex, rootPair.TargetIndex},
            settings.SourceReuse,
            settings.Rotation};
        float previousTime = 0;
        for (uint32_t frame = 0; frame < source.FrameCount; ++frame)
        {
            const auto time = ComputeSkeletalBvhClipTime(settings.TimeMode, source.FrameTimeSeconds, settings.SourceFps,
                                                         frame, frame != 0, previousTime);
            if (!time.Succeeded())
            {
                auto result = Failure(Status::TimeRejected, frame);
                result.Time = time.Status;
                return result;
            }
            const auto evaluated = SourceFrame(sourcePlan, frame, conversion, worldView, nativeView);
            if (!evaluated.Succeeded())
            {
                return evaluated;
            }
            const auto retargeted = RetargetSkeletalRotationFrame(
                nativeInput, target, meshGlobal, Container::Span<SkeletalRetargetRotationValue>(values));
            if (!retargeted.Succeeded())
            {
                auto result = Failure(Status::RetargetRejected, frame);
                result.Retarget = retargeted;
                return result;
            }
            for (size_t i = 0; i < mappingCount; ++i)
            {
                auto value = values[i];
                auto& samples = candidate.Clip.Channels[i].Samples;
                if (!samples.empty())
                {
                    const auto& previous = samples.back().Value;
                    const double dot =
                        static_cast<double>(previous.X) * value.X + static_cast<double>(previous.Y) * value.Y +
                        static_cast<double>(previous.Z) * value.Z + static_cast<double>(previous.W) * value.W;
                    if (dot < 0)
                    {
                        value.X = -value.X;
                        value.Y = -value.Y;
                        value.Z = -value.Z;
                        value.W = -value.W;
                        ++report.HemisphereFlips;
                    }
                }
                samples.push_back({time.StoredSeconds, {value.X, value.Y, value.Z, value.W}});
            }
            ObserveRoot(source, frame, rootPositions, settings, conversion, report.RootPosition);
            const size_t frameOffset = static_cast<size_t>(frame) * source.Channels.size();
            for (auto& position : report.IgnoredNonRootPositions)
            {
                Include(position.Raw, source.Values[frameOffset + position.OriginalChannelIndex]);
            }
            report.MaximumTimeRoundingErrorSeconds =
                std::max(report.MaximumTimeRoundingErrorSeconds, time.RoundingErrorSeconds);
            report.ExactDurationSeconds = time.ExactSeconds;
            previousTime = time.StoredSeconds;
        }
        candidate.Clip.DurationSeconds = previousTime;
        report.StoredDurationSeconds = previousTime;
        // 完成済みclipの内部keyも実Samplerの区間評価を通す。raw生成値の確認では代用しない。
        for (uint32_t frame = 0; frame < source.FrameCount; ++frame)
        {
            const auto evaluated = SourceFrame(sourcePlan, frame, conversion, worldView, nativeView);
            if (!evaluated.Succeeded())
            {
                return evaluated;
            }
            for (size_t i = 0; i < mappingCount; ++i)
            {
                Skeletal::SkeletalValue value;
                if (!Detail::SampleSkeletalChannelAtKnownKey(candidate.Clip.Channels[i], frame, value))
                {
                    return Failure(Status::InvalidInput, frame);
                }
                sampled[i] = {candidate.Clip.Channels[i].JointIndex, value.X, value.Y, value.Z, value.W};
            }
            const auto validated = ValidateSkeletalRotationFrameValues(
                nativeInput, target, meshGlobal, Container::Span<const SkeletalRetargetRotationValue>(sampled));
            if (!validated.Succeeded())
            {
                auto result = Failure(Status::RetargetRejected, frame);
                result.Retarget = validated;
                return result;
            }
            if (frame == 0 || validated.AngularErrorRadians > report.MaximumKeyRotationErrorRadians)
            {
                report.MaximumKeyRotationErrorRadians = validated.AngularErrorRadians;
                report.MaximumRotationErrorFrame = frame;
            }
        }
        report.bStoredKeysValidated = true;
        static_assert(std::is_nothrow_move_assignable_v<SkeletalBvhClipOutput>);
        out = std::move(candidate);
        return {Status::Success};
    }
} // namespace NorvesLib::Core::Animation
