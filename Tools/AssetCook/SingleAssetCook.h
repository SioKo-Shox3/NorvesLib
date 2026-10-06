#pragma once
#include "Container/String.h"
#include "SkeletalRoleFileRequest.h"
#include "Resource/SkeletalImportOptions.h"
#include <filesystem>

namespace NorvesLib::Tools::AssetCook
{
    // argvに依存しない単体cook要求。CLIの明示指定有無はCLI解析側で検査する。
    struct SingleAssetCookRequest
    {
        std::filesystem::path InputPath, PackagePath, ManifestPath;
        Core::Container::AnsiString LogicalPath, Kind, EntryName, EntryTypeText, Format, Variant;
        std::filesystem::path ImportSettingsOverridePath;
        bool bNoSidecar = false;
        bool bRequireSidecar = false;
        bool bSkipIfUnchanged = false;
        Core::Skeletal::SkeletalGltfDecodeOptions SkeletalDecode;
        SkeletalRoleFileRequest RoleProfile;
    };
    // 従来の単体cook・manifest・出力検証を再利用する。batch集約や新しい増分判定は追加しない。
    // errorは呼出結果で置換する。診断streamの出力規則は従来通り。
    [[nodiscard]] bool CookSingleAsset(const SingleAssetCookRequest& request, Core::Container::AnsiString& error);
}
