#pragma once
// 正準roleを経由する所有Profile。実joint名は明示し、補正Cは明示または作者restから求める。
#include "SkeletalBvhClipImport.h"
#include "SkeletalClipProcessing.h"

namespace NorvesLib::Core::Animation
{
    enum class SkeletalRole : uint8_t
    {
        Root,
        Pelvis,
        Spine,
        Neck,
        Head,
        Jaw,
        Tail,
        FrontLClavicle,
        FrontLUpper,
        FrontLLower,
        FrontLPaw,
        FrontRClavicle,
        FrontRUpper,
        FrontRLower,
        FrontRPaw,
        HindLThigh,
        HindLShin,
        HindLHock,
        HindLPaw,
        HindRThigh,
        HindRShin,
        HindRHock,
        HindRPaw,
        Count
    };
    inline constexpr size_t SkeletalRoleCount = static_cast<size_t>(SkeletalRole::Count);
    [[nodiscard]] const char* SkeletalRoleName(SkeletalRole role) noexcept;
    [[nodiscard]] bool IsSkeletalChainRole(SkeletalRole role) noexcept;
    struct SkeletalRoleTarget
    {
        Container::VariableArray<uint8_t> Name;
        Bvh::Matrix3d Correction;
    };
    enum class SkeletalRestCorrectionMode : uint8_t
    {
        Explicit,
        Match,
        AlignBones
    };
    struct SkeletalRoleProfile
    {
        Container::FixedArray<Container::VariableArray<Container::VariableArray<uint8_t>>, SkeletalRoleCount> Source;
        Container::FixedArray<Container::VariableArray<SkeletalRoleTarget>, SkeletalRoleCount> Target;
        SkeletalBvhClipSettings Settings;
        SkeletalRestCorrectionMode RestMode = SkeletalRestCorrectionMode::Explicit;
        Bvh::Vector3d RestUpHint{0, 1, 0};
        double MaximumRestErrorRadians = 0.08726646259971647;
        double RootScale = 1;
        bool bAutoRootHeight = false, bHasProcessing = false;
        SkeletalClipProcessingSettings Processing;
        uint32_t LoopExcludedRoles = 0;
        uint32_t RequiredMask = 0;
        uint32_t ExpandedMappings = 0;
        uint64_t NameBytes = 0;
    };
    struct SkeletalRoleProfileLimits
    {
        size_t MaxInputBytes = 1024u * 1024u;
        uint32_t MaxDepth = 16;
        size_t MaxSyntaxTokens = 32768;
        uint32_t MaxSourceElements = 1024;
        uint32_t MaxMappings = 1024;
        size_t MaxNameBytes = 4096;
        size_t MaxTotalNameBytes = 1024u * 1024u;
    };
    enum class SkeletalRoleProfileStatus : uint8_t
    {
        Success,
        InvalidInput,
        LimitExceeded,
        InvalidJson,
        InvalidType,
        UnknownField,
        DuplicateField,
        UnsupportedVersion,
        UnknownVocabulary,
        UnknownRole,
        MissingRole,
        InvalidChain,
        InvalidName,
        InvalidSettings,
        InvalidCorrection
    };
    struct SkeletalRoleProfileResult
    {
        SkeletalRoleProfileStatus Status = SkeletalRoleProfileStatus::InvalidInput;
        Container::AnsiString Field;
        uint32_t RoleIndex = UINT32_MAX;
        size_t ElementIndex = SIZE_MAX;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalRoleProfileStatus::Success;
        }
    };
    // JSON DOM確保前にbyte/depth/tokenを制限する。depthのhard上限は64。
    // 名前は厳密UTF8/NUL無し。実source存在と階層rootはcook接続で検証する。
    // 成功時だけoutを置換する。旧outは失敗/確保例外でも保持する。
    [[nodiscard]] SkeletalRoleProfileResult ParseSkeletalRoleProfile(Container::Span<const uint8_t> bytes,
                                                                     const SkeletalRoleProfileLimits& limits,
                                                                     SkeletalRoleProfile& out);
} // namespace NorvesLib::Core::Animation
