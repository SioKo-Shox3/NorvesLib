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
        /// プロファイルの版。既定は 1。v2 は proportions.root_scale が必須。
        uint32_t Version = 1;
        /// v2 のロール別上向き方向。未指定はゼロ。
        Container::FixedArray<Bvh::Vector3d, SkeletalRoleCount> RoleUpHints{};
        /// v2 の上向き方向を指定したロールのビット集合。既定は 0。
        uint32_t RoleUpHintMask = 0;
        /// v2 で脚長からルートの倍率を求めるか。既定は false。
        bool bAutoLegRootScale = false;
        /// v2 の脚長比の上書き値。既定の 0 は上書きなし。
        double LegLengthRatioOverride = 0;
        /// v2 の関節誤差の上限（度）。負は未指定で、既定は -1。
        double ReportMaxJointErrorDegrees = -1;
        /// v2 の足滑りの上限（メートル）。負は未指定で、既定は -1。
        double ReportMaxFootSlideMeters = -1;
        /// v2 の足のめり込みの上限（メートル）。負は未指定で、既定は -1。
        double ReportMaxFootPenetrationMeters = -1;
        /// v2 の再接地を有効にするか。既定は false。
        bool bFootPlanting = false;
        /// v2 の足先の最低位置からの接地高さのしきい値（メートル）。
        double FootHeightThresholdMeters = 0.03;
        /// v2 の接地とみなす速さのしきい値（メートル毎秒）。
        double FootSpeedThresholdMetersPerSecond = 0.15;
        /// v2 の接地区間の最短フレーム数。
        uint32_t FootMinStanceFrames = 3;
        /// v2 の接地区間の前後で補間するフレーム数。
        uint32_t FootBlendFrames = 3;
        /// v2 の再接地対象の paw ロールのビット集合。既定は 0。
        uint32_t FootPlantingLegRoleMask = 0;
        /// v2 の平らな地面のワールド高さ（Y、メートル）。既定は 0。
        double GroundHeightMeters = 0;
        /// v2 の IK 失敗フレーム数の上限。負は未指定で、既定は -1。
        int64_t ReportMaxIkFailedFrames = -1;
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
