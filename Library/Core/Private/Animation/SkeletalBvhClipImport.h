#pragma once
// 内部sample保持import。外部BoneMapは後続のrole展開から既存mappingへ接続する。
#include "SkeletalBvhClipTime.h"
#include "SkeletalCoordinateConversion.h"
#include "SkeletalRotationRetargetNative.h"
#include "Resource/BvhRotationSource.h"

namespace NorvesLib::Core::Animation
{
    struct SkeletalBvhClipSettings
    {
        AssetImport::SignedAxis Up = static_cast<AssetImport::SignedAxis>(255);
        AssetImport::SignedAxis Forward = static_cast<AssetImport::SignedAxis>(255);
        SkeletalSourceHandedness Handedness = SkeletalSourceHandedness::Unspecified;
        double PositionScale = 0;
        Bvh::TranslationConvention Translation = Bvh::TranslationConvention::Unspecified;
        SkeletalBvhClipTimeMode TimeMode = SkeletalBvhClipTimeMode::Unspecified;
        double SourceFps = 0;
        SkeletalSourceReusePolicy SourceReuse = SkeletalSourceReusePolicy::Unspecified;
        SkeletalRetargetRotationPolicy Rotation = SkeletalRetargetRotationPolicy::Unspecified;
    };
    struct SkeletalBvhClipLimits
    {
        Bvh::BvhRotationSourceLimits Source;
        SkeletalJointIndexLimits TargetNames;
        size_t MaxClipNameBytes = 4096;
        uint64_t MaxOutputKeys = uint64_t{1} << 20;
        uint64_t MaxJointFrames = uint64_t{1} << 22;
        uint64_t MaxWorkUnits = uint64_t{1} << 26;
        uint64_t MaxOwnedBytes = uint64_t{128} << 20;
    };
    struct SkeletalBvhClipRange
    {
        bool bHasValue = false;
        double Minimum = 0, Maximum = 0;
    };
    struct SkeletalBvhIgnoredPosition
    {
        uint32_t JointIndex = 0;
        uint32_t OriginalChannelIndex = 0;
        Bvh::Channel Channel = Bvh::Channel::Xposition;
        SkeletalBvhClipRange Raw;
    };
    enum class SkeletalBvhRootDeltaIssue : uint8_t
    {
        None,
        IncompleteChannels,
        UnsupportedLayout,
        UnrepresentableDelta
    };
    struct SkeletalBvhRootPositionReport
    {
        Bvh::Vector3d OriginalOffset;
        uint8_t PositionMask = 0;
        bool bPositionBeforeRotations = true;
        Container::FixedArray<SkeletalBvhClipRange, 3> Raw, Delta, CanonicalDelta;
        uint32_t UnavailableDeltaFrames = 0, UnavailableCanonicalFrames = 0;
        uint32_t FirstUnavailableDeltaFrame = UINT32_MAX, FirstUnavailableCanonicalFrame = UINT32_MAX;
        uint32_t ConversionFailureFrames = 0, FirstConversionFailureFrame = UINT32_MAX;
        SkeletalBvhRootDeltaIssue FirstDeltaIssue = SkeletalBvhRootDeltaIssue::None;
        SkeletalCoordinateStatus FirstConversionIssue = SkeletalCoordinateStatus::Success;
    };
    struct SkeletalBvhClipReport
    {
        SkeletalBvhClipSettings Settings;
        uint32_t SourceFrames = 0, SourceJoints = 0, TargetJoints = 0;
        uint64_t OutputKeys = 0, JointFrames = 0, WorkUnits = 0, PlannedOwnedBytes = 0, HemisphereFlips = 0;
        double HeaderFrameTimeSeconds = 0, SelectedIntervalSeconds = 0, ExactDurationSeconds = 0;
        float StoredDurationSeconds = 0;
        double MaximumTimeRoundingErrorSeconds = 0, MaximumKeyRotationErrorRadians = 0;
        uint32_t MaximumRotationErrorFrame = UINT32_MAX;
        bool bStoredKeysValidated = false;
        bool bContinuousCurveValidated = false;
        Container::VariableArray<Container::VariableArray<uint8_t>> SourceNames, TargetNames;
        Container::VariableArray<SkeletalJointMappingPair> Mappings;
        Container::VariableArray<Bvh::Matrix3d> Corrections;
        Container::VariableArray<uint32_t> UnmappedSource, UnmappedTarget;
        Container::VariableArray<SkeletalBvhIgnoredPosition> IgnoredNonRootPositions;
        SkeletalBvhRootPositionReport RootPosition;
    };
    struct SkeletalBvhClipOutput
    {
        Skeletal::SkeletalAnimationClip Clip;
        SkeletalBvhClipReport Report;
    };
    enum class SkeletalBvhClipStatus : uint8_t
    {
        Success,
        InvalidInput,
        InvalidSettings,
        LimitExceeded,
        InvalidClipName,
        InvalidTargetName,
        InvalidMapping,
        SourceRejected,
        CoordinateRejected,
        TimeRejected,
        RetargetRejected
    };
    struct SkeletalBvhClipResult
    {
        SkeletalBvhClipStatus Status = SkeletalBvhClipStatus::InvalidInput;
        uint32_t FrameIndex = UINT32_MAX;
        Bvh::BvhRotationSourceResult Source;
        SkeletalJointIndexResult TargetNames;
        SkeletalCoordinateStatus Coordinate = SkeletalCoordinateStatus::Success;
        SkeletalBvhClipTimeStatus Time = SkeletalBvhClipTimeStatus::Success;
        SkeletalRetargetResult Retarget;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalBvhClipStatus::Success;
        }
    };
    // 全入力は呼出し中不変。成功時だけclip/reportをまとめて置換し、失敗/確保例外ではoutを保持する。
    // root/source位置の計算不能はreportに残し、位置を出力しない回転clipの失敗にはしない。
    // targetの数学的上限は1024。legacy cookerの128制限は後続の書出し境界で適用する。
    [[nodiscard]] SkeletalBvhClipResult ImportSkeletalBvhRotationClip(
        const Bvh::BvhDocument& source, Container::Span<const Skeletal::SkeletalJoint> target,
        const Math::Matrix4x4& meshGlobal, const SkeletalJointMappingSet& mappings,
        Container::Span<const Bvh::Matrix3d> corrections, const Container::String& clipName,
        const SkeletalBvhClipSettings& settings, const SkeletalBvhClipLimits& limits, SkeletalBvhClipOutput& out);
} // namespace NorvesLib::Core::Animation
