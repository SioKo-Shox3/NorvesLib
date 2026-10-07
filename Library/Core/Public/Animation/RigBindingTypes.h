#pragma once
// v1の公開要求と完了診断で共有する有限profile。wire実装は公開しない。
#include "Animation/SkeletalRestPose.h"
namespace NorvesLib::Core::Skeletal
{
    // profile1は既定のまま。新しい親frameは明示選択時だけ受ける。
    enum class RigImportProfile : uint32_t
    {
        DirectTrs128 = 1,
        StaticRootFrame128 = 2,
        StaticRootFrame256 = 3
    };
    constexpr bool IsStaticRootFrameProfile(RigImportProfile profile) noexcept
    {
        return profile == RigImportProfile::StaticRootFrame128 || profile == RigImportProfile::StaticRootFrame256;
    }
    constexpr uint32_t RigProfileMaximumJoints(RigImportProfile profile) noexcept
    {
        return profile == RigImportProfile::StaticRootFrame256
                   ? 256
                   : (profile == RigImportProfile::DirectTrs128 || profile == RigImportProfile::StaticRootFrame128 ? 128
                                                                                                                   : 0);
    }

    using RigRootFrame = Container::FixedArray<float, 16>;
    inline RigRootFrame IdentityRigRootFrame()
    {
        return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    }

    enum class RigV1Status : uint8_t
    {
        Success,
        InvalidInput,
        LimitExceeded,
        InvalidName,
        InvalidTopology,
        InvalidRest,
        UnsupportedProfile,
        DecodeRejected,
        BadWire,
        UnsupportedVersion,
        UnsupportedSection,
        HashMismatch,
        InvalidClip,
        TopologyMismatch,
        RestMismatch,
        WrongOwner,
        RegistryNotReady,
        ResourceFailure,
        Exception,
        FrameMismatch
    };
    struct RigV1Limits
    {
        uint32_t MaxJoints = 128;
        uint32_t MaxClips = 256;
        uint32_t MaxSnapshots = 256;
        uint32_t MaxChannels = 32768;
        uint32_t MaxSamples = 1048576;
        uint32_t MaxNameBytes = 4096;
        uint32_t MaxStringBytes = 1048576;
        uint64_t MaxWireBytes = 64ull * 1024 * 1024;
        uint64_t MaxSourceBytes = 64ull * 1024 * 1024;
        uint64_t MaxBufferBytes = 64ull * 1024 * 1024;
        uint32_t MaxNodes = 1024, MaxAccessors = 65536, MaxBuffers = 256;
        uint32_t MaxVertices = 1048576, MaxIndices = 3145728;
    };
    struct RigRestTolerance
    {
        double TranslationMeters = 1e-5;
        double RotationRadians = 1e-4;
        double LogScale = 1e-5;
    };
    struct RigBindingPolicy
    {
        RigRestTolerance Tolerance;
        bool bAllowRestMismatch = false;
    };
    struct RigJointDifference
    {
        uint32_t SnapshotIndex = 0, CanonicalIndex = 0, TargetIndex = 0;
        Container::AnsiString Name;
        double TranslationMeters = 0, RotationRadians = 0, MaximumLogScale = 0;
        Container::FixedArray<double, 3> ScaleRatios{1, 1, 1};
        bool bExceedsTolerance = false;
    };
    struct RigSnapshotComparison
    {
        Container::AnsiString AuthorLabel;
        uint64_t AuthorRestHash = 0, TargetRestHash = 0;
        double AuthorImportScale = 1, TargetImportScale = 1;
        double MaximumTranslationMeters = 0, MaximumRotationRadians = 0, MaximumLogScale = 0;
        uint32_t ExceededJoints = 0;
    };
    struct RigFrameComparison
    {
        uint32_t SnapshotIndex = 0;
        Container::AnsiString AuthorLabel;
        uint64_t AuthorFrameHash = 0, TargetFrameHash = 0;
        double MaximumAbsoluteMatrixDifference = 0;
        bool bEqual = false;
    };
    struct RigV1Report
    {
        RigV1Status Status = RigV1Status::InvalidInput;
        SkeletalGltfDecodeStatus DecodeStatus = SkeletalGltfDecodeStatus::InvalidDocument;
        Container::AnsiString TargetLabel;
        uint64_t SkeletonId = 0, BankPayloadHash = 0;
        uint32_t PolicyRevision = 1;
        RigBindingPolicy Policy;
        bool bComparisonComplete = false, bOverrideUsed = false;
        Container::VariableArray<RigSnapshotComparison> Snapshots;
        Container::VariableArray<RigJointDifference> Differences;
        // profile2専用の独立検査。rest overrideでframe差を免除しない。
        bool bFrameComparisonComplete = false;
        Container::VariableArray<RigFrameComparison> FrameComparisons;
    };
    struct RigSplitReport
    {
        RigV1Status Status = RigV1Status::InvalidInput;
        uint32_t FailedBank = UINT32_MAX;
        Container::VariableArray<RigV1Report> Banks;
    };
} // namespace NorvesLib::Core::Skeletal
