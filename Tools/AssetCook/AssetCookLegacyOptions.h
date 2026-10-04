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

    [[nodiscard]] bool ValidateCookOptions(const CookOptions& options, std::string& error);
    [[nodiscard]] SingleAssetCookRequest MakeSingleCookRequest(const CookOptions& options);
}
