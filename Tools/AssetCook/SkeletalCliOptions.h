#pragma once

#include "ImportCliOptions.h"
#include "Resource/SkeletalImportPolicy.h"
#include <charconv>
#include <cmath>

namespace NorvesLib::Tools::AssetCook
{
    struct SkeletalCliOptions
    {
        Core::Skeletal::SkeletalGltfDecodeOptions Decode;
        bool bPolicySpecified = false;
        bool bWarnSpecified = false;
        bool bFailSpecified = false;
        [[nodiscard]] bool HasAny() const noexcept
        {
            return bPolicySpecified || bWarnSpecified || bFailSpecified;
        }
    };

    // 各引数は成功時だけ反映する。引数順序によらない相互検査は最後に行う。
    inline ImportArgumentResult ParseSkeletalArgument(int argc, const char* const* argv, int& index,
        SkeletalCliOptions& options, const char*& error)
    {
        if (!argv || index < 0 || index >= argc || !argv[index])
        {
            error = "骨格import引数の入力が不正です";
            return ImportArgumentResult::Rejected;
        }
        const char* argument = argv[index];
        const char* equals = std::strchr(argument, '=');
        const size_t length = equals ? static_cast<size_t>(equals - argument) : std::strlen(argument);
        const auto matches = [&](const char* name)
        {
            return length == std::strlen(name) && std::strncmp(argument, name, length) == 0;
        };
        const bool policy = matches("--skin-influences");
        const bool warn = matches("--skin-warn-dropped-weight");
        const bool fail = matches("--skin-fail-dropped-weight");
        if (!policy && !warn && !fail) return ImportArgumentResult::Unhandled;
        const auto reject = [&](const char* reason)
        {
            error = reason;
            return ImportArgumentResult::Rejected;
        };
        if ((policy && options.bPolicySpecified) || (warn && options.bWarnSpecified) || (fail && options.bFailSpecified))
            return reject("骨格import引数が重複しています");
        const char* value = equals ? equals + 1 : (index + 1 < argc ? argv[index + 1] : nullptr);
        if (!value || !*value || std::strncmp(value, "--", 2) == 0)
            return reject("骨格import引数の値が必要です");
        auto candidate = options;
        if (policy)
        {
            if (std::strcmp(value, "strict") == 0) candidate.Decode.InfluencePolicy = Core::Skeletal::SkeletalInfluencePolicy::Strict;
            else if (std::strcmp(value, "reduce") == 0) candidate.Decode.InfluencePolicy = Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour;
            else return reject("--skin-influencesにはstrictまたはreduceを指定してください");
            candidate.bPolicySpecified = true;
        }
        else
        {
            double number = 0;
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(number) || number < 0 || number > 1)
                return reject("脱落weight閾値は0以上1以下の有限数で指定してください");
            if (warn) { candidate.Decode.WarnDroppedWeight = number; candidate.bWarnSpecified = true; }
            else { candidate.Decode.FailDroppedWeight = number; candidate.bFailSpecified = true; }
        }
        options = candidate;
        if (!equals) ++index;
        return ImportArgumentResult::Accepted;
    }

    inline bool ValidateSkeletalArguments(const SkeletalCliOptions& options, bool skeletalFormat, const char*& error)
    {
        if (options.HasAny() && !skeletalFormat)
        {
            error = "骨格import引数はNVSKELのmodel cook専用です";
            return false;
        }
        if ((options.bWarnSpecified || options.bFailSpecified) &&
            options.Decode.InfluencePolicy != Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour)
        {
            error = "脱落weight閾値には--skin-influences reduceの明示指定が必要です";
            return false;
        }
        if (!Core::Skeletal::IsValidSkeletalGltfDecodeOptions(options.Decode))
        {
            error = "骨格import指定は0<=warn<=fail<=1を満たす必要があります";
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
