#pragma once
// role展開後の型付き要求。ファイル/CLIと増分cacheの入口ではない。
#include "MeshCooker.h"
#include "Animation/SkeletalBvhClipImport.h"

namespace NorvesLib::Tools::AssetCook
{
    enum class SkeletalBvhClipOperation : uint8_t
    {
        Unspecified,
        Add,
        Replace
    };
    struct SkeletalBvhCookRequest
    {
        Core::Container::Span<const uint8_t> BvhBytes;
        Core::Container::Span<const Core::Animation::SkeletalJointMappingNameView> Mappings;
        Core::Animation::SkeletalJointMappingNameView Root;
        Core::Container::Span<const Core::Bvh::Matrix3d> Corrections;
        Core::Container::String ClipName;
        SkeletalBvhClipOperation Operation = SkeletalBvhClipOperation::Unspecified;
        Core::Animation::SkeletalBvhClipSettings Settings;
        Core::Animation::SkeletalBvhClipLimits ClipLimits;
        Core::Animation::SkeletalJointMappingLimits MappingLimits;
        Core::Bvh::BvhDecodeLimits DecodeLimits;
        uint64_t MaxNvskelBytes = uint64_t{128} << 20;
    };
    struct SkeletalBvhCookResult
    {
        SkeletalCookResult Cook;
        Core::Animation::SkeletalBvhClipReport Report;
        uint32_t ClipIndex = UINT32_MAX;
    };
    // 全入力を呼出中不変に保つ。resultは独立所有し、成功時のみ一括置換する。
    // 旧glTF-only fingerprintを使わず毎回cookする。作者時restとの再束縛を保証しない。
    [[nodiscard]] bool CookGltfWithBvhToNvskelNativePath(
        const uint8_t* sourceBytes, size_t sourceSize, Core::Container::AnsiStringView format,
        const std::filesystem::path& sourcePath, const SkeletalBvhCookRequest& request,
        SkeletalBvhCookResult& outResult, Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    namespace Detail
    {
        // cook中のprivate candidateへだけ適用する。失敗時candidateは破棄する。
        [[nodiscard]] bool ApplyBvhCookRequest(const SkeletalBvhCookRequest& request,
                                               Core::Skeletal::SkeletalGltfData& target,
                                               Core::Animation::SkeletalBvhClipReport& report, uint32_t& clipIndex,
                                               Core::Container::AnsiString& error);
        [[nodiscard]] bool AppendBvhCookHash(uint64_t seed, const SkeletalBvhCookRequest& request, uint64_t& outHash);
        [[nodiscard]] bool EqualBvhCookClip(const Core::Skeletal::SkeletalAnimationClip& expected,
                                            const Core::Skeletal::SkeletalAnimationClip& actual);
    } // namespace Detail
} // namespace NorvesLib::Tools::AssetCook
