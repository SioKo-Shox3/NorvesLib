#pragma once
// role展開後の具体的な名前pairを解決する。外部JSON schemaや骨格互換性は定義しない。
#include "SkeletalJointIndex.h"

namespace NorvesLib::Core::Animation
{
    struct SkeletalJointMappingNameView
    {
        Container::Span<const uint8_t> SourceName;
        Container::Span<const uint8_t> TargetName;
    };
    struct SkeletalJointMappingRoot
    {
        uint32_t SourceIndex = UINT32_MAX;
        uint32_t TargetIndex = UINT32_MAX;
    };
    enum class SkeletalSourceReusePolicy : uint8_t
    {
        Unspecified,
        Reject,
        Allow
    };
    struct SkeletalJointMappingLimits
    {
        uint32_t MaxCatalogJoints = 1024;
        uint32_t MaxMappings = 1024;
        size_t MaxTotalNameBytes = 1024u * 1024u;
    };
    enum class SkeletalJointMappingSide : uint8_t
    {
        None,
        Source,
        Target
    };
    enum class SkeletalJointMappingStatus : uint8_t
    {
        Success,
        InvalidInput,
        InvalidPolicy,
        EmptyIndex,
        LimitExceeded,
        InvalidRoot,
        InvalidName,
        UnknownName,
        DuplicateSource,
        DuplicateTarget,
        RootConflict,
        RootMissing
    };
    struct SkeletalJointMappingResult
    {
        SkeletalJointMappingStatus Status = SkeletalJointMappingStatus::InvalidInput;
        SkeletalJointMappingSide Side = SkeletalJointMappingSide::None;
        size_t EntryIndex = SIZE_MAX;
        size_t OtherEntryIndex = SIZE_MAX;
        uint32_t JointIndex = UINT32_MAX;
        SkeletalJointIndexResult NameResult{SkeletalJointIndexStatus::Success};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalJointMappingStatus::Success;
        }
    };
    struct SkeletalJointMappingPair
    {
        uint32_t SourceIndex = 0;
        uint32_t TargetIndex = 0;
        uint32_t InputEntryIndex = 0;
    };
    struct SkeletalJointMappingSet
    {
        uint32_t SourceJointCount = 0;
        uint32_t TargetJointCount = 0;
        uint32_t RootMappingIndex = UINT32_MAX;
        Container::VariableArray<SkeletalJointMappingPair> Pairs;
        Container::VariableArray<uint32_t> UnmappedSourceIndices;
        Container::VariableArray<uint32_t> UnmappedTargetIndices;

        SkeletalJointMappingSet() = default;
        SkeletalJointMappingSet(const SkeletalJointMappingSet&) = default;
        SkeletalJointMappingSet(SkeletalJointMappingSet&& other) noexcept;
        SkeletalJointMappingSet& operator=(const SkeletalJointMappingSet& other);
        SkeletalJointMappingSet& operator=(SkeletalJointMappingSet&& other) noexcept;
        void Swap(SkeletalJointMappingSet& other) noexcept;
    };
    // 索引/入力は呼出し中不変。outは成功時だけ置換し、確保例外でも既存値を保持する。
    // 出力は所有snapshot。catalogの名前/順序が変わった場合は再解決する。
    // requiredRootはcallerの指定を検査するだけで、階層rootかどうかは別の入力検証の責務。
    [[nodiscard]] SkeletalJointMappingResult ResolveSkeletalJointMappings(
        const SkeletalJointIndex& source, const SkeletalJointIndex& target,
        Container::Span<const SkeletalJointMappingNameView> mappings, SkeletalJointMappingRoot requiredRoot,
        SkeletalSourceReusePolicy sourceReuse, const SkeletalJointMappingLimits& limits, SkeletalJointMappingSet& out);
} // namespace NorvesLib::Core::Animation
