#pragma once
// 移動した既存CLIの型を閉じ込める互換境界。新しい呼出元はSingleAssetCookRequestを使う。
#include "SingleAssetCook.h"
#include "ImportCliOptions.h"
#include "SkeletalCliOptions.h"
#include <string>
#include <filesystem>

namespace NorvesLib::Tools::AssetCook::Detail
{
    struct CookOptions
    {
        std::filesystem::path InputPath;
        std::filesystem::path PackagePath;
        std::filesystem::path ManifestPath;
        NorvesLib::Core::AssetImport::ImportSettingsFileOptions ImportSettings;
        bool bSkipIfUnchanged = false;
        NorvesLib::Tools::AssetCook::SkeletalCliOptions SkeletalImport;
        std::string LogicalPath;
        std::string Kind;
        std::string EntryName;
        std::string EntryTypeText;
        std::string Format;
        std::string Variant;
    };

    [[nodiscard]] bool MakeCachePackagePath(const std::filesystem::path& package,const std::filesystem::path& parent,
        Core::Container::AnsiString& relative,Core::Container::AnsiString& error);
    [[nodiscard]] CookOptions MakeLegacyCookOptions(const SingleAssetCookRequest& request);
    // 共有cache用に既存単体境界の検査・正規化を再利用する。旧CLIのskip制約は変更しない。
    [[nodiscard]] bool NormalizeCacheCookRequest(const SingleAssetCookRequest& request, SingleAssetCookRequest& out,
        Core::Container::AnsiString& error);

    [[nodiscard]] bool ValidateCookOptions(const CookOptions& options, std::string& error);
    [[nodiscard]] SingleAssetCookRequest MakeSingleCookRequest(const CookOptions& options);
}
