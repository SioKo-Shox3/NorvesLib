#include "SkeletalImportReport.h"
#include "Resource/SkeletalImportPolicy.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Tools::AssetCook
{
    SkeletalImportReportJson BuildSkeletalImportReport(const SkeletalImportReportInput& input) noexcept
    {
        using namespace Core::Skeletal;
        const auto& diagnostics = input.Diagnostics;
        const auto& report = diagnostics.Report;
        if (input.Options.MorphPolicy != SkeletalMorphPolicy::Reject)
        {
            return {};
        }
        const bool failed = input.Outcome == SkeletalImportOutcome::Failed;
        const bool ready = input.Outcome == SkeletalImportOutcome::PayloadReady;
        const bool cached = input.Outcome == SkeletalImportOutcome::CacheHit;
        if ((!failed && !ready && !cached) || !IsValidSkeletalGltfDecodeOptions(input.Options) ||
            (ready && (!diagnostics.bDecodeAttempted || diagnostics.DecodeStatus != 0)) ||
            (cached && diagnostics.bDecodeAttempted)) return {};
        const bool measured = diagnostics.bDecodeAttempted && input.Options.InfluencePolicy == SkeletalInfluencePolicy::ReduceToFour;
        if (measured)
        {
            const auto ratio = [](double value) { return std::isfinite(value) && value >= 0 && value <= 1; };
            if (report.ProcessedVertexCount > report.TotalVertexCount ||
                report.ReducedVertexCount > report.ProcessedVertexCount ||
                report.MergedJointVertexCount > report.ProcessedVertexCount ||
                report.RenormalizedVertexCount > report.ProcessedVertexCount ||
                report.WarningVertexCount > report.ProcessedVertexCount ||
                !ratio(report.MaximumDroppedWeight) || !ratio(report.MeanDroppedWeight) ||
                report.MeanDroppedWeight > report.MaximumDroppedWeight ||
                (report.ProcessedVertexCount == 0 && (report.MaximumDroppedWeight != 0 || report.MeanDroppedWeight != 0)) ||
                (report.bInfluenceScanComplete && report.ProcessedVertexCount != report.TotalVertexCount) ||
                (report.FailedVertexIndex != UINT64_MAX && (report.FailedVertexIndex >= report.TotalVertexCount ||
                    report.FailedVertexIndex != report.ProcessedVertexCount || report.bInfluenceScanComplete)) ||
                (report.bHasFailedVertexDroppedWeight && (report.FailedVertexIndex == UINT64_MAX || !ratio(report.FailedVertexDroppedWeight))) ||
                (ready && (report.TotalVertexCount == 0 || !report.bInfluenceScanComplete || report.FailedVertexIndex != UINT64_MAX))) return {};
        }
        const bool bake = input.Options.CubicSplinePolicy == SkeletalCubicSplinePolicy::Bake;
        if (bake)
        {
            const auto errorBound = [](double value, double tolerance, uint64_t count)
            {
                return std::isfinite(value) && value >= 0 && value <= tolerance && (count != 0 || value == 0);
            };
            const auto baked = report.BakedCubicChannelCount;
            // 加算overflowを避けて、種類別内訳と全数の一致を検査する。
            if (report.ProcessedAnimationChannelCount > report.TotalAnimationChannelCount ||
                baked > report.ProcessedAnimationChannelCount ||
                report.BakedCubicTranslationChannelCount > baked ||
                report.BakedCubicRotationChannelCount > baked - report.BakedCubicTranslationChannelCount ||
                report.BakedCubicScaleChannelCount != baked - report.BakedCubicTranslationChannelCount - report.BakedCubicRotationChannelCount ||
                baked > report.CubicInputKeyCount / 2 || report.CubicOutputKeyCount < report.CubicInputKeyCount ||
                report.CubicOutputKeyCount > input.Options.CubicMaximumSamplesPerAsset ||
                (baked == 0 && (report.CubicInputKeyCount != 0 || report.CubicOutputKeyCount != 0)) ||
                !errorBound(report.MaximumCubicTranslationErrorMeters, input.Options.CubicTranslationToleranceMeters, report.BakedCubicTranslationChannelCount) ||
                !errorBound(report.MaximumCubicRotationErrorRadians, input.Options.CubicRotationToleranceRadians, report.BakedCubicRotationChannelCount) ||
                !errorBound(report.MaximumCubicScaleError, input.Options.CubicScaleTolerance, report.BakedCubicScaleChannelCount))
            {
                return {};
            }
            if (report.bCubicScanStarted)
            {
                if (!diagnostics.bDecodeAttempted || report.TotalAnimationChannelCount == 0 ||
                    (report.bCubicScanComplete && report.ProcessedAnimationChannelCount != report.TotalAnimationChannelCount) ||
                    (report.FailedAnimationChannelIndex != UINT64_MAX && (report.bCubicScanComplete ||
                        report.FailedAnimationChannelIndex != report.ProcessedAnimationChannelCount ||
                        report.FailedAnimationChannelIndex >= report.TotalAnimationChannelCount)) ||
                    (report.bHasCubicBakeFailure && (report.FailedAnimationChannelIndex == UINT64_MAX || report.FailedCubicBakeStatus == 0)) ||
                    (!report.bHasCubicBakeFailure && report.FailedCubicBakeStatus != 0))
                {
                    return {};
                }
            }
            else if (report.TotalAnimationChannelCount != 0 || report.bCubicScanComplete ||
                report.FailedAnimationChannelIndex != UINT64_MAX || report.bHasCubicBakeFailure || report.FailedCubicBakeStatus != 0)
            {
                return {};
            }
            if (ready && (!report.bCubicScanStarted || !report.bCubicScanComplete || report.bHasCubicBakeFailure ||
                report.FailedAnimationChannelIndex != UINT64_MAX))
            {
                return {};
            }
        }
        SkeletalImportReportJson result;
        bool valid = true;
        const auto text = [&](const char* value)
        {
            const size_t length = std::strlen(value);
            if (length >= sizeof(result.Bytes) - result.Size) { valid = false; return; }
            std::memcpy(result.Bytes + result.Size, value, length);
            result.Size += length;
        };
        const auto number = [&](auto value)
        {
            char buffer[64] = {};
            const auto converted = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
            if (converted.ec != std::errc{}) { valid = false; return; }
            *converted.ptr = '\0';
            text(buffer);
        };
        text("{\"version\":1,\"kind\":\"skeletal\",\"outcome\":\"");
        text(failed ? "failed" : ready ? "payload_ready" : "cache_hit");
        text("\",\"source_hash\":");
        if (failed) text("null");
        else
        {
            char hash[17] = {};
            constexpr char hex[] = "0123456789abcdef";
            for (size_t index = 0; index < 16; ++index) hash[index] = hex[(input.SourceHash >> ((15 - index) * 4)) & 15];
            text("\""); text(hash); text("\"");
        }
        text(",\"decode_status\":");
        if (diagnostics.bDecodeAttempted) number(diagnostics.DecodeStatus); else text("null");
        text(",\"skin\":{\"policy\":\"");
        text(input.Options.InfluencePolicy == SkeletalInfluencePolicy::Strict ? "strict" : "reduce");
        text("\",\"warn_dropped_weight\":"); number(input.Options.WarnDroppedWeight == 0 ? 0.0 : input.Options.WarnDroppedWeight);
        text(",\"fail_dropped_weight\":"); number(input.Options.FailDroppedWeight == 0 ? 0.0 : input.Options.FailDroppedWeight);
        text(",\"influence_scan\":");
        if (!measured || report.TotalVertexCount == 0) text("null");
        else
        {
            text("{\"total_vertices\":"); number(report.TotalVertexCount);
            text(",\"processed_vertices\":"); number(report.ProcessedVertexCount);
            text(",\"reduced_vertices\":"); number(report.ReducedVertexCount);
            text(",\"merged_joint_vertices\":"); number(report.MergedJointVertexCount);
            text(",\"renormalized_vertices\":"); number(report.RenormalizedVertexCount);
            text(",\"warning_vertices\":"); number(report.WarningVertexCount);
            text(",\"max_dropped_weight\":");
            if (report.ProcessedVertexCount != 0) number(report.MaximumDroppedWeight == 0 ? 0.0 : report.MaximumDroppedWeight); else text("null");
            text(",\"mean_dropped_weight\":");
            if (report.ProcessedVertexCount != 0) number(report.MeanDroppedWeight == 0 ? 0.0 : report.MeanDroppedWeight); else text("null");
            text(",\"scan_complete\":"); text(report.bInfluenceScanComplete ? "true" : "false");
            text(",\"failed_vertex\":");
            if (report.FailedVertexIndex == UINT64_MAX) text("null"); else number(report.FailedVertexIndex);
            text(",\"failed_vertex_dropped_weight\":");
            if (report.bHasFailedVertexDroppedWeight) number(report.FailedVertexDroppedWeight == 0 ? 0.0 : report.FailedVertexDroppedWeight);
            else text("null");
            text("}");
        }
        if (bake)
        {
            text(",\"cubic_policy\":\"bake\",\"cubic_settings\":{\"translation_tolerance_m\":");
            number(input.Options.CubicTranslationToleranceMeters);
            text(",\"rotation_tolerance_rad\":"); number(input.Options.CubicRotationToleranceRadians);
            text(",\"scale_tolerance\":"); number(input.Options.CubicScaleTolerance);
            text(",\"max_depth\":"); number(input.Options.CubicMaximumDepth);
            text(",\"max_channel_samples\":"); number(input.Options.CubicMaximumSamplesPerChannel);
            text(",\"max_asset_samples\":"); number(input.Options.CubicMaximumSamplesPerAsset);
            text("},\"cubic_scan\":");
            if (!report.bCubicScanStarted)
            {
                text("null");
            }
            else
            {
                text("{\"total_channels\":"); number(report.TotalAnimationChannelCount);
                text(",\"processed_channels\":"); number(report.ProcessedAnimationChannelCount);
                text(",\"baked_channels\":"); number(report.BakedCubicChannelCount);
                text(",\"baked_translation_channels\":"); number(report.BakedCubicTranslationChannelCount);
                text(",\"baked_rotation_channels\":"); number(report.BakedCubicRotationChannelCount);
                text(",\"baked_scale_channels\":"); number(report.BakedCubicScaleChannelCount);
                text(",\"input_keys\":"); number(report.CubicInputKeyCount);
                text(",\"output_keys\":"); number(report.CubicOutputKeyCount);
                text(",\"max_translation_error_m\":");
                if (report.BakedCubicTranslationChannelCount != 0)
                {
                    number(report.MaximumCubicTranslationErrorMeters == 0 ? 0.0 : report.MaximumCubicTranslationErrorMeters);
                }
                else
                {
                    text("null");
                }
                text(",\"max_rotation_error_rad\":");
                if (report.BakedCubicRotationChannelCount != 0)
                {
                    number(report.MaximumCubicRotationErrorRadians == 0 ? 0.0 : report.MaximumCubicRotationErrorRadians);
                }
                else
                {
                    text("null");
                }
                text(",\"max_scale_error\":");
                if (report.BakedCubicScaleChannelCount != 0)
                {
                    number(report.MaximumCubicScaleError == 0 ? 0.0 : report.MaximumCubicScaleError);
                }
                else
                {
                    text("null");
                }
                text(",\"scan_complete\":"); text(report.bCubicScanComplete ? "true" : "false");
                text(",\"failed_channel\":");
                if (report.FailedAnimationChannelIndex != UINT64_MAX)
                {
                    number(report.FailedAnimationChannelIndex);
                }
                else
                {
                    text("null");
                }
                text(",\"failure_status\":");
                if (report.bHasCubicBakeFailure)
                {
                    number(report.FailedCubicBakeStatus);
                }
                else
                {
                    text("null");
                }
                text("}");
            }
        }
        text("}}");
        if (!valid) return {};
        result.bValid = true;
        return result;
    }
} // namespace NorvesLib::Tools::AssetCook
