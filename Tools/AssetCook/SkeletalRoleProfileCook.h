#pragma once
// 自己完結Profile bytesを既存の型付きBVH cookへ接続する。file cacheは使わない。
#include "SkeletalBvhCook.h"
#include "Animation/SkeletalRoleProfile.h"

namespace NorvesLib::Tools::AssetCook
{
    inline constexpr uint32_t SkeletalRoleProfileParserRevision = 1;
    inline constexpr uint32_t SkeletalRoleProfileVocabularyRevision = 1;
    struct SkeletalRoleProfileCookRequest
    {
        Core::Container::Span<const uint8_t> BvhBytes;
        Core::Container::Span<const uint8_t> ProfileBytes;
        Core::Container::String ClipName;
        SkeletalBvhClipOperation Operation = SkeletalBvhClipOperation::Unspecified;
        Core::Animation::SkeletalRoleProfileLimits ProfileLimits;
        Core::Animation::SkeletalBvhClipLimits ClipLimits;
        Core::Animation::SkeletalJointMappingLimits MappingLimits;
        Core::Bvh::BvhDecodeLimits DecodeLimits;
        uint64_t MaxNvskelBytes = uint64_t{128} << 20;
    };
    struct SkeletalExpandedRole
    {
        Core::Animation::SkeletalRole Role = Core::Animation::SkeletalRole::Root;
        uint32_t Ordinal = 0;
    };
    struct SkeletalRoleProfileCookResult
    {
        SkeletalBvhCookResult Cooked;
        Core::Animation::SkeletalRoleProfile Profile;
        Core::Container::VariableArray<SkeletalExpandedRole> ExpandedRoles;
        Core::Container::VariableArray<Core::Animation::SkeletalRole> SourceOnlyRoles;
        uint64_t RawProfileBytes = 0;
        uint64_t RawProfileHash = 0;
        uint32_t ParserRevision = SkeletalRoleProfileParserRevision;
        uint32_t VocabularyRevision = SkeletalRoleProfileVocabularyRevision;
    };
    // 全結果を所有し、成功時だけ置換する。inputは呼出中不変。失敗/確保例外は旧outを保持。
    // sourceだけのroleも実BVH名へ解決する。活動pairの再利用はReject、target重複も拒否。
    [[nodiscard]] bool CookGltfWithRoleProfileToNvskelNativePath(
        const uint8_t* sourceBytes, size_t sourceSize, Core::Container::AnsiStringView format,
        const std::filesystem::path& sourcePath, const SkeletalRoleProfileCookRequest& request,
        SkeletalRoleProfileCookResult& outResult, Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr);
    // SourceHashの同一算術を共有する。geometry/clipのcookは行わず、cook可能性は保証しない。
    [[nodiscard]] bool FingerprintGltfWithRoleProfileNativePath(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, const std::filesystem::path& sourcePath,
        Core::Container::AnsiStringView logicalPath, const SkeletalRoleProfileCookRequest& request,
        ModelCookFingerprint& outResult, Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr);
} // namespace NorvesLib::Tools::AssetCook
