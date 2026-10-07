#pragma once
// v1の公開要求と完了診断で共有する有限profile。wire実装は公開しない。
#include "Animation/SkeletalRestPose.h"
namespace NorvesLib::Core::Skeletal
{
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
        Exception
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
    };
    struct RigSplitReport
    {
        RigV1Status Status = RigV1Status::InvalidInput;
        uint32_t FailedBank = UINT32_MAX;
        Container::VariableArray<RigV1Report> Banks;
    };
} // namespace NorvesLib::Core::Skeletal
