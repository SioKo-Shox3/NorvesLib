#pragma once

#include "Animation/ClipOptimizer.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    struct ClipOptimizeCookOptions
    {
        bool bEnabled = false;
        bool bQuantize = false;
        Core::Animation::ClipOptimizeSettings Settings;
        std::filesystem::path ReportPath;
        bool bAnyLimitGiven = false;
        bool bVertexLimitGiven = false;
        bool bJointLimitGiven = false;
        bool bRotationLimitGiven = false;
        bool bReportGiven = false;
    };

    // 既知の引数だけを処理し、成功した値だけ設定へ反映する。
    [[nodiscard]] inline bool TryParseClipOptimizeArgument(std::string_view key, const char* value, ClipOptimizeCookOptions& options,
                                      bool& consumed, std::string& error)
    {
        consumed = true;
        auto candidate = options;
        if (key == "--clip-optimize")
        {
            if (value || candidate.bEnabled)
            {
                error = reinterpret_cast<const char*>(u8"--clip-optimize は値を取らず、重複して指定できません");
                return false;
            }
            candidate.bEnabled = true;
            options = std::move(candidate);
            return true;
        }

        if (key == "--clip-quantize")
        {
            if (value || candidate.bQuantize)
            {
                error = reinterpret_cast<const char*>(u8"--clip-quantize は値を取らず、重複して指定できません");
                return false;
            }
            candidate.bQuantize = true;
            options = std::move(candidate);
            return true;
        }

        double* limit = nullptr;
        bool* specified = nullptr;
        if (key == "--clip-max-vertex-error-m")
        {
            limit = &candidate.Settings.MaxVertexErrorMeters;
            specified = &candidate.bVertexLimitGiven;
        }
        else if (key == "--clip-max-joint-error-m")
        {
            limit = &candidate.Settings.MaxJointErrorMeters;
            specified = &candidate.bJointLimitGiven;
        }
        else if (key == "--clip-max-rotation-error-deg")
        {
            limit = &candidate.Settings.MaxRotationErrorDegrees;
            specified = &candidate.bRotationLimitGiven;
        }
        else if (key == "--clip-report")
        {
            specified = &candidate.bReportGiven;
        }
        else
        {
            consumed = false;
            return true;
        }
        if (*specified)
        {
            error = reinterpret_cast<const char*>(u8"クリップ最適化の引数が重複しています");
            return false;
        }
        if (!value || !*value || std::strncmp(value, "--", 2) == 0)
        {
            error = reinterpret_cast<const char*>(u8"クリップ最適化の引数には値が必要です");
            return false;
        }
        if (limit)
        {
            double number = 0;
            const char* end = value + std::strlen(value);
            const auto parsed = std::from_chars(value, end, number, std::chars_format::general);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !std::isfinite(number) || number <= 0)
            {
                error = reinterpret_cast<const char*>(u8"クリップ最適化の上限は有限の正の数で指定してください");
                return false;
            }
            *limit = number;
            candidate.bAnyLimitGiven = true;
        }
        else
        {
            try
            {
                candidate.ReportPath = std::filesystem::path(value);
            }
            catch (const std::exception&)
            {
                error = reinterpret_cast<const char*>(u8"クリップ最適化レポートのパスが不正です");
                return false;
            }
        }
        *specified = true;
        options = std::move(candidate);
        return true;
    }

    [[nodiscard]] inline bool ValidateClipOptimizeOptions(const ClipOptimizeCookOptions& options, std::string& error)
    {
        if (!options.bEnabled && (options.bAnyLimitGiven || options.bReportGiven || !options.ReportPath.empty()))
        {
            error = reinterpret_cast<const char*>(u8"--clip-max-... と --clip-report は --clip-optimize と一緒に指定してください");
            return false;
        }
        const auto& settings = options.Settings;
        if (options.bEnabled && (!std::isfinite(settings.MaxVertexErrorMeters) || settings.MaxVertexErrorMeters <= 0 ||
            !std::isfinite(settings.MaxJointErrorMeters) || settings.MaxJointErrorMeters <= 0 ||
            !std::isfinite(settings.MaxRotationErrorDegrees) || settings.MaxRotationErrorDegrees <= 0))
        {
            error = reinterpret_cast<const char*>(u8"クリップ最適化の上限は有限の正の数で指定してください");
            return false;
        }
        return true;
    }

    // 失敗時も指定された報告を書き、成功時だけクリップを置き換える。
    [[nodiscard]] bool RunClipOptimizeStage(const ClipOptimizeCookOptions& options,
        const Core::Animation::ClipOptimizeRig& rig,
        Core::Container::VariableArray<Core::Skeletal::SkeletalAnimationClip>& clips, std::string& error);
}
