#pragma once

#include "Resource/ImportSettings.h"
#include "Resource/MaterialImportDocument.h"
#include <filesystem>

namespace NorvesLib::Core::AssetImport
{
    struct ImportSettingsFileOptions
    {
        std::filesystem::path OverridePath;
        bool bDisabled = false;
        bool bRequired = false;
        EmissiveScale AssetSetEmission;
    };
    struct LoadedImportSettings
    {
        ImportSettings Settings;
        std::filesystem::path Path;
        bool bPresent = false;
        // 検証した同じreadの生byte。増分判定用であり、runtime SourceHashは従来通り設定値を使う。
        Container::VariableArray<uint8_t> RawSourceBytes;
    };
    struct LoadedImportSettingsDocument
    {
        ImportSettingsDocument Settings;
        std::filesystem::path Path;
        bool bPresent = false;
        // 検証した同じreadの生byte。増分判定用であり、runtime SourceHashは従来通り設定値を使う。
        Container::VariableArray<uint8_t> RawSourceBytes;
    };
    enum class SettingsFileResult : uint8_t
    {
        Success, InvalidOptions, Missing, StatusFailed, NotRegularFile, InvalidSize,
        OpenFailed, ReadFailed, InvalidJson, InvalidSettings
    };
    struct SettingsFileOutcome
    {
        SettingsFileResult Result = SettingsFileResult::Success;
        SettingsResult Validation = SettingsResult::Success;
    };
    // Win32のFILE_NOT_FOUND(2)/PATH_NOT_FOUND(3)だけを不在とする。
    // errcへの写像ではINVALID_NAME/BAD_NETPATH等もno_such_fileへ畳まれるため、生値を判定する。
    [[nodiscard]] constexpr bool IsMissingWindowsSettingsFileError(uint32_t value) noexcept
    {
        return value == 2 || value == 3;
    }
    inline constexpr size_t MaximumImportSettingsFileSize = 1024 * 1024;
    // source全名に.import.jsonを付ける。明示override/requiredの不在は失敗、autoの不在だけ既定。
    // no-sidecarとrequired/overrideは排他。失敗時outは非変更、確保例外は呼出側へ伝播する。
    // 通常fileだけ読み最大1MiB。source/設定の競合書換えに対するatomic snapshotは提供しない。
    [[nodiscard]] SettingsFileOutcome LoadImportSettingsFile(const std::filesystem::path& source,
        const ImportSettingsFileOptions& options, LoadedImportSettings& outSettings);
    // 新document用。旧LoadImportSettingsFileは幾何-onlyの拒否互換を保つ。
    [[nodiscard]] SettingsFileOutcome LoadImportSettingsDocument(const std::filesystem::path& source,
        const ImportSettingsFileOptions& options, LoadedImportSettingsDocument& outSettings);
} // namespace NorvesLib::Core::AssetImport
