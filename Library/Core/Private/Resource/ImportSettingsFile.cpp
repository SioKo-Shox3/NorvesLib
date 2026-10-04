#include "Resource/ImportSettingsFile.h"
#include "Text/JsonDocument.h"
#include <fstream>
#include <utility>

namespace NorvesLib::Core::AssetImport
{
    SettingsFileOutcome LoadImportSettingsFile(const std::filesystem::path& source,
        const ImportSettingsFileOptions& options, LoadedImportSettings& outSettings)
    {
        const auto fail = [](SettingsFileResult result)
        {
            return SettingsFileOutcome{result, SettingsResult::Success};
        };
        if (options.bDisabled && (options.bRequired || !options.OverridePath.empty()))
        {
            return fail(SettingsFileResult::InvalidOptions);
        }
        LoadedImportSettings candidate;
        if (options.bDisabled)
        {
            outSettings = std::move(candidate);
            return {};
        }
        if (source.empty() && options.OverridePath.empty())
        {
            return fail(SettingsFileResult::InvalidOptions);
        }
        candidate.Path = options.OverridePath;
        if (candidate.Path.empty())
        {
            candidate.Path = source;
            candidate.Path += ".import.json";
        }

        std::error_code error;
        const auto linkStatus = std::filesystem::symlink_status(candidate.Path, error);
#if defined(_WIN32)
        const bool bMissingError = error.category() == std::system_category() &&
            IsMissingWindowsSettingsFileError(static_cast<uint32_t>(error.value()));
#else
        const bool bMissingError = !error || error == std::errc::no_such_file_or_directory;
#endif
        if (linkStatus.type() == std::filesystem::file_type::not_found && bMissingError)
        {
            if (options.bRequired || !options.OverridePath.empty())
            {
                return fail(SettingsFileResult::Missing);
            }
            outSettings = std::move(candidate);
            return {};
        }
        if (error)
        {
            return fail(SettingsFileResult::StatusFailed);
        }
        const auto status = std::filesystem::status(candidate.Path, error);
        if (error || !std::filesystem::is_regular_file(status))
        {
            // 存在するlinkのtarget不在もauto不在とは区別する。
            return fail(SettingsFileResult::NotRegularFile);
        }
        const auto size = std::filesystem::file_size(candidate.Path, error);
        if (error || size > MaximumImportSettingsFileSize)
        {
            return fail(SettingsFileResult::InvalidSize);
        }
        std::ifstream file(candidate.Path, std::ios::binary);
        if (!file.is_open())
        {
            return fail(SettingsFileResult::OpenFailed);
        }
        Container::VariableArray<uint8_t> bytes(static_cast<size_t>(size));
        if (size != 0)
        {
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
            if (file.gcount() != static_cast<std::streamsize>(size) || file.bad())
            {
                return fail(SettingsFileResult::ReadFailed);
            }
        }
        if (file.peek() != std::char_traits<char>::eof() || file.bad())
        {
            return fail(SettingsFileResult::ReadFailed);
        }
        size_t start = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            start = 3;
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8({bytes.data() ? bytes.data() + start : nullptr, bytes.size() - start}, document))
        {
            return fail(SettingsFileResult::InvalidJson);
        }
        const auto validation = ParseSettings(document.GetRoot(), candidate.Settings);
        if (validation != SettingsResult::Success)
        {
            return {SettingsFileResult::InvalidSettings, validation};
        }
        candidate.bPresent = true;
        outSettings = std::move(candidate);
        return {};
    }
} // namespace NorvesLib::Core::AssetImport
