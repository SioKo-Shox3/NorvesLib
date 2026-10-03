#pragma once

#include "ImportCliOptions.h"
#include "Resource/SkeletalImportPolicy.h"
#include <charconv>
#include <cmath>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    struct SkeletalCliOptions
    {
        Core::Skeletal::SkeletalGltfDecodeOptions Decode;
        bool bPolicySpecified = false;
        bool bWarnSpecified = false;
        bool bFailSpecified = false;
        bool bCubicSpecified = false;
        bool bTranslationSpecified = false;
        bool bRotationSpecified = false;
        bool bScaleSpecified = false;
        bool bDepthSpecified = false;
        bool bChannelSamplesSpecified = false;
        bool bAssetSamplesSpecified = false;
        [[nodiscard]] bool HasCubicSettings() const noexcept
        {
            return bTranslationSpecified || bRotationSpecified || bScaleSpecified || bDepthSpecified ||
                bChannelSamplesSpecified || bAssetSamplesSpecified;
        }
        [[nodiscard]] bool HasAny() const noexcept
        {
            return bPolicySpecified || bWarnSpecified || bFailSpecified || bCubicSpecified || HasCubicSettings();
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
        auto candidate = options;
        bool* specified = nullptr;
        double* scalar = nullptr;
        uint32_t* integer = nullptr;
        bool influencePolicy = false;
        bool cubicPolicy = false;
        bool rotationDegrees = false;
        double lower = 0, upper = 1;
        bool exclusiveLower = false;
        uint32_t integerLower = 0, integerUpper = 0;
        if (matches("--skin-influences"))
        {
            specified = &candidate.bPolicySpecified;
            influencePolicy = true;
        }
        else if (matches("--skin-warn-dropped-weight"))
        {
            specified = &candidate.bWarnSpecified;
            scalar = &candidate.Decode.WarnDroppedWeight;
        }
        else if (matches("--skin-fail-dropped-weight"))
        {
            specified = &candidate.bFailSpecified;
            scalar = &candidate.Decode.FailDroppedWeight;
        }
        else if (matches("--cubicspline"))
        {
            specified = &candidate.bCubicSpecified;
            cubicPolicy = true;
        }
        else if (matches("--cubic-translation-tolerance"))
        {
            specified = &candidate.bTranslationSpecified;
            scalar = &candidate.Decode.CubicTranslationToleranceMeters;
            lower = Core::Skeletal::CubicVectorNumericErrorFloor;
            upper = std::numeric_limits<double>::max();
            exclusiveLower = true;
        }
        else if (matches("--cubic-rotation-tolerance-deg"))
        {
            specified = &candidate.bRotationSpecified;
            scalar = &candidate.Decode.CubicRotationToleranceRadians;
            rotationDegrees = true;
            lower = Core::Skeletal::CubicRotationNumericErrorBudget;
            upper = 3.14159265358979323846;
            exclusiveLower = true;
        }
        else if (matches("--cubic-scale-tolerance"))
        {
            specified = &candidate.bScaleSpecified;
            scalar = &candidate.Decode.CubicScaleTolerance;
            lower = Core::Skeletal::CubicVectorNumericErrorFloor;
            upper = std::numeric_limits<double>::max();
            exclusiveLower = true;
        }
        else if (matches("--cubic-max-depth"))
        {
            specified = &candidate.bDepthSpecified;
            integer = &candidate.Decode.CubicMaximumDepth;
            integerUpper = Core::Skeletal::MaximumCubicSubdivisionDepth;
        }
        else if (matches("--cubic-max-channel-samples"))
        {
            specified = &candidate.bChannelSamplesSpecified;
            integer = &candidate.Decode.CubicMaximumSamplesPerChannel;
            integerLower = 2;
            integerUpper = Core::Skeletal::MaximumCubicSamplesPerChannel;
        }
        else if (matches("--cubic-max-asset-samples"))
        {
            specified = &candidate.bAssetSamplesSpecified;
            integer = &candidate.Decode.CubicMaximumSamplesPerAsset;
            integerLower = 2;
            integerUpper = Core::Skeletal::MaximumCubicSamplesPerAsset;
        }
        else
        {
            return ImportArgumentResult::Unhandled;
        }
        const auto reject = [&](const char* reason)
        {
            error = reason;
            return ImportArgumentResult::Rejected;
        };
        if (*specified)
        {
            return reject("骨格import引数が重複しています");
        }
        const char* value = equals ? equals + 1 : (index + 1 < argc ? argv[index + 1] : nullptr);
        if (!value || !*value || std::strncmp(value, "--", 2) == 0)
        {
            return reject("骨格import引数の値が必要です");
        }
        if (influencePolicy)
        {
            if (std::strcmp(value, "strict") == 0)
            {
                candidate.Decode.InfluencePolicy = Core::Skeletal::SkeletalInfluencePolicy::Strict;
            }
            else if (std::strcmp(value, "reduce") == 0)
            {
                candidate.Decode.InfluencePolicy = Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour;
            }
            else
            {
                return reject("--skin-influencesにはstrictまたはreduceを指定してください");
            }
        }
        else if (cubicPolicy)
        {
            if (std::strcmp(value, "reject") == 0)
            {
                candidate.Decode.CubicSplinePolicy = Core::Skeletal::SkeletalCubicSplinePolicy::Reject;
            }
            else if (std::strcmp(value, "bake") == 0)
            {
                candidate.Decode.CubicSplinePolicy = Core::Skeletal::SkeletalCubicSplinePolicy::Bake;
            }
            else
            {
                return reject("--cubicsplineにはrejectまたはbakeを指定してください");
            }
        }
        else if (scalar)
        {
            double number = 0;
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(number))
            {
                return reject("骨格importの許容値は有限数で指定してください");
            }
            if (rotationDegrees)
            {
                // 先に除算して、度からラジアンへの変換途中のoverflowを避ける。
                number = (number / 180.0) * 3.14159265358979323846;
            }
            if (number < lower || (exclusiveLower && number == lower) || number > upper)
            {
                return reject("骨格importの許容値が範囲外です（焼込許容は数値誤差の下限を超える必要があります）");
            }
            *scalar = number;
        }
        else
        {
            uint32_t number = 0;
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, number);
            if (parsed.ec != std::errc{} || parsed.ptr != end || number < integerLower || number > integerUpper)
            {
                return reject("焼込予算は許容範囲内の符号なし整数で指定してください");
            }
            *integer = number;
        }
        *specified = true;
        options = candidate;
        if (!equals)
        {
            ++index;
        }
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
            (!options.bPolicySpecified || options.Decode.InfluencePolicy != Core::Skeletal::SkeletalInfluencePolicy::ReduceToFour))
        {
            error = "脱落weight閾値には--skin-influences reduceの明示指定が必要です";
            return false;
        }
        if (options.HasCubicSettings() && (!options.bCubicSpecified ||
            options.Decode.CubicSplinePolicy != Core::Skeletal::SkeletalCubicSplinePolicy::Bake))
        {
            error = "焼込許容・予算には--cubicspline bakeの明示指定が必要です";
            return false;
        }
        if (!Core::Skeletal::IsValidSkeletalGltfDecodeOptions(options.Decode))
        {
            error = "骨格importの閾値・焼込許容・予算の組合せが不正です";
            return false;
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
