#pragma once
// v1の有限profileと診断。legacy0.xの上限や値規則には使わない。
#include "Animation/SkeletalRestPose.h"
#include "Container/PointerTypes.h"
#include "Container/Span.h"
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
    struct RigTopologyJoint
    {
        Container::AnsiString Name;
        int32_t ParentIndex = -1;
    };
    struct RigTopology
    {
        uint64_t SkeletonId = 0;
        Container::VariableArray<RigTopologyJoint> Joints;
        Container::VariableArray<uint32_t> SourceToCanonical, CanonicalToSource;
        Container::VariableArray<uint8_t> CanonicalBytes;
    };
    [[nodiscard]] bool IsValidRigV1Limits(const RigV1Limits&) noexcept;
    [[nodiscard]] bool IsValidRigBindingPolicy(const RigBindingPolicy&) noexcept;
    [[nodiscard]] uint64_t RigBytesHash(Container::Span<const uint8_t>) noexcept;
    // 親番号は入力順に依らず名前順へ写す。失敗/確保例外でoutを変更しない。
    [[nodiscard]] RigV1Status BuildRigTopology(Container::Span<const SkeletalJoint>, const RigV1Limits&,
                                               RigTopology& out);
    [[nodiscard]] bool SameRigTopology(const RigTopology&, const RigTopology&) noexcept;
    [[nodiscard]] uint64_t RigRestHash(Container::Span<const SkeletalRestTransform>) noexcept;
} // namespace NorvesLib::Core::Skeletal
