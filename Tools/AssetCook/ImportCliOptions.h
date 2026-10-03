#pragma once

#include "Resource/ImportSettingsFile.h"
#include <cstring>

namespace NorvesLib::Tools::AssetCook
{
    enum class ImportArgumentResult { Unhandled, Accepted, Rejected };

    // 成功した引数だけ設定へ反映する。ファイル読込はcookの共有loaderへ委ねる。
    inline ImportArgumentResult ParseImportArgument(int argc, const char* const* argv, int& index,
        Core::AssetImport::ImportSettingsFileOptions& options, const char*& error)
    {
        const char* argument = argv[index];
        const char* equals = std::strchr(argument,'=');
        const size_t length = equals ? static_cast<size_t>(equals-argument) : std::strlen(argument);
        const auto matches = [&](const char* name)
        {
            return length == std::strlen(name) && std::strncmp(argument,name,length)==0;
        };
        const bool bOverride = matches("--import-settings");
        const bool bDisabled = matches("--no-sidecar");
        const bool bRequired = matches("--require-sidecar");
        if (!bOverride && !bDisabled && !bRequired)
        {
            return ImportArgumentResult::Unhandled;
        }
        const auto reject = [&](const char* reason)
        {
            error = reason;
            return ImportArgumentResult::Rejected;
        };
        if ((!bOverride && equals) || (bOverride && !options.OverridePath.empty()) ||
            (bDisabled && options.bDisabled) || (bRequired && options.bRequired))
        {
            return reject("duplicate import option or value supplied to sidecar flag");
        }
        if ((bDisabled && (options.bRequired || !options.OverridePath.empty())) ||
            (!bDisabled && options.bDisabled))
        {
            return reject("--no-sidecar conflicts with --import-settings and --require-sidecar");
        }
        if (bOverride)
        {
            const char* value = equals ? equals+1 : (index+1<argc ? argv[index+1] : nullptr);
            if (!value || !*value || std::strncmp(value,"--",2)==0)
            {
                return reject("--import-settings requires a nonempty file path");
            }
            options.OverridePath = value;
            if (!equals)
            {
                ++index;
            }
        }
        else if (bDisabled)
        {
            options.bDisabled = true;
        }
        else
        {
            options.bRequired = true;
        }
        return ImportArgumentResult::Accepted;
    }

    inline ImportArgumentResult ParseSkipArgument(const char* argument, bool& bSkip, const char*& error)
    {
        constexpr char option[]="--skip-if-unchanged";
        if (std::strcmp(argument,option)==0)
        {
            if (bSkip)
            {
                error="duplicate --skip-if-unchanged";
                return ImportArgumentResult::Rejected;
            }
            bSkip=true;
            return ImportArgumentResult::Accepted;
        }
        if (std::strncmp(argument,option,sizeof(option)-1)==0 && argument[sizeof(option)-1]=='=')
        {
            error="--skip-if-unchanged does not accept a value";
            return ImportArgumentResult::Rejected;
        }
        return ImportArgumentResult::Unhandled;
    }

    inline bool HasImportArguments(const Core::AssetImport::ImportSettingsFileOptions& options)
    {
        return options.bDisabled || options.bRequired || !options.OverridePath.empty();
    }
} // namespace NorvesLib::Tools::AssetCook
