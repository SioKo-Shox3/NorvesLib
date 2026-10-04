#include "Tools/AssetCook/SkeletalImportReport.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace NorvesLib::Tools::AssetCook;
using namespace NorvesLib::Core::Skeletal;
namespace
{
    void Print(const SkeletalImportReportInput& input)
    {
        const auto result = BuildSkeletalImportReport(input);
        assert(result.bValid && result.Size < sizeof(result.Bytes) && result.Bytes[result.Size] == 0);
        assert(std::strlen(result.Bytes) == result.Size);
        const auto repeat = BuildSkeletalImportReport(input);
        assert(repeat.Size == result.Size && std::memcmp(repeat.Bytes, result.Bytes, result.Size) == 0);
        std::cout << "JSON="; std::cout.write(result.Bytes, static_cast<std::streamsize>(result.Size)); std::cout << "\n";
    }
}
int main()
{
    SkeletalImportReportInput input;
    Print(input);
    const auto early = BuildSkeletalImportReport(input);
    assert(std::strcmp(early.Bytes, "{\"version\":1,\"kind\":\"skeletal\",\"outcome\":\"failed\",\"source_hash\":null,\"decode_status\":null,\"skin\":{\"policy\":\"strict\",\"warn_dropped_weight\":0.01,\"fail_dropped_weight\":0.25,\"influence_scan\":null}}") == 0);
    input.Outcome = SkeletalImportOutcome::PayloadReady;
    input.SourceHash = 0x0123456789abcdef;
    input.Diagnostics.bDecodeAttempted = true;
    Print(input);
    input.Options.InfluencePolicy = SkeletalInfluencePolicy::ReduceToFour;
    auto& report = input.Diagnostics.Report;
    report.TotalVertexCount = 3; report.ProcessedVertexCount = 3;
    report.ReducedVertexCount = 1; report.RenormalizedVertexCount = 1; report.WarningVertexCount = 1;
    report.MaximumDroppedWeight = 0.05; report.MeanDroppedWeight = 0.05 / 3;
    report.bInfluenceScanComplete = true;
    Print(input);
    input.Outcome = SkeletalImportOutcome::Failed;
    input.Diagnostics.DecodeStatus = 17;
    report.ProcessedVertexCount = 1; report.ReducedVertexCount = 0; report.RenormalizedVertexCount = 0; report.WarningVertexCount = 0;
    report.MaximumDroppedWeight = 0; report.MeanDroppedWeight = 0;
    report.FailedVertexIndex = 1; report.FailedVertexDroppedWeight = 0.05; report.bHasFailedVertexDroppedWeight = true;
    report.bInfluenceScanComplete = false;
    Print(input);
    auto zeroPrefix = input;
    zeroPrefix.Diagnostics.Report.ProcessedVertexCount = 0;
    zeroPrefix.Diagnostics.Report.FailedVertexIndex = 0;
    const auto zeroJson = BuildSkeletalImportReport(zeroPrefix);
    assert(zeroJson.bValid && std::strstr(zeroJson.Bytes, "\"max_dropped_weight\":null,\"mean_dropped_weight\":null"));
    zeroPrefix.Diagnostics.Report = {};
    const auto beforeScan = BuildSkeletalImportReport(zeroPrefix);
    assert(beforeScan.bValid && std::strstr(beforeScan.Bytes, "\"influence_scan\":null"));
    const auto failed = input;
    input.Outcome = SkeletalImportOutcome::CacheHit;
    input.Diagnostics = {};
    input.SourceHash = UINT64_MAX;
    Print(input);
    assert(std::strstr(BuildSkeletalImportReport(input).Bytes, "\"influence_scan\":null"));
    input.Options.WarnDroppedWeight = -0.0; input.Options.FailDroppedWeight = -0.0;
    const auto minusZero = BuildSkeletalImportReport(input);
    assert(minusZero.bValid && !std::strstr(minusZero.Bytes, "-0"));
    input.Options.FailDroppedWeight = std::numeric_limits<double>::quiet_NaN();
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Diagnostics.Report.MeanDroppedWeight = 0.01;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Diagnostics.Report.ProcessedVertexCount = 4;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Diagnostics.Report.FailedVertexIndex = 3;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Diagnostics.Report.bInfluenceScanComplete = true;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Diagnostics.Report.FailedVertexDroppedWeight = std::numeric_limits<double>::infinity();
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Outcome = SkeletalImportOutcome::PayloadReady;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Outcome = SkeletalImportOutcome::CacheHit;
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; input.Outcome = static_cast<SkeletalImportOutcome>(99);
    assert(!BuildSkeletalImportReport(input).bValid);
    input = failed; report = {}; // inputの参照は同じstorageを指し続ける。
    input.Diagnostics.Report.TotalVertexCount = UINT64_MAX;
    input.Diagnostics.Report.ProcessedVertexCount = UINT64_MAX - 1;
    input.Diagnostics.Report.FailedVertexIndex = UINT64_MAX - 1;
    assert(BuildSkeletalImportReport(input).bValid);
    input = {};
    input.Options.CubicSplinePolicy = SkeletalCubicSplinePolicy::Bake;
    Print(input);
    assert(std::strstr(BuildSkeletalImportReport(input).Bytes, "\"cubic_scan\":null"));
    input.Diagnostics.bDecodeAttempted = true;
    input.Diagnostics.Report.bCubicScanStarted = true;
    input.Diagnostics.Report.TotalAnimationChannelCount = 3;
    input.Diagnostics.Report.FailedAnimationChannelIndex = 0;
    input.Diagnostics.Report.bHasCubicBakeFailure = true;
    input.Diagnostics.Report.FailedCubicBakeStatus = 9;
    input.Diagnostics.DecodeStatus = 18;
    Print(input);
    const auto noKeys = BuildSkeletalImportReport(input);
    assert(std::strstr(noKeys.Bytes, "\"max_translation_error_m\":null"));
    assert(std::strstr(noKeys.Bytes, "\"max_rotation_error_rad\":null"));
    assert(std::strstr(noKeys.Bytes, "\"max_scale_error\":null"));
    input.Diagnostics.Report.ProcessedAnimationChannelCount = 1;
    input.Diagnostics.Report.BakedCubicChannelCount = 1;
    input.Diagnostics.Report.BakedCubicTranslationChannelCount = 1;
    input.Diagnostics.Report.CubicInputKeyCount = 2;
    input.Diagnostics.Report.CubicOutputKeyCount = 101;
    input.Diagnostics.Report.MaximumCubicTranslationErrorMeters = .0009;
    input.Diagnostics.Report.FailedAnimationChannelIndex = 1;
    Print(input);
    const auto prefix = input;
    input.Outcome = SkeletalImportOutcome::PayloadReady;
    input.Diagnostics.DecodeStatus = 0;
    input.Diagnostics.Report.ProcessedAnimationChannelCount = 3;
    input.Diagnostics.Report.BakedCubicChannelCount = 3;
    input.Diagnostics.Report.BakedCubicRotationChannelCount = 1;
    input.Diagnostics.Report.BakedCubicScaleChannelCount = 1;
    input.Diagnostics.Report.CubicInputKeyCount = 6;
    input.Diagnostics.Report.CubicOutputKeyCount = 231;
    input.Diagnostics.Report.MaximumCubicRotationErrorRadians = .001;
    input.Diagnostics.Report.MaximumCubicScaleError = .0008;
    input.Diagnostics.Report.bCubicScanComplete = true;
    input.Diagnostics.Report.bHasCubicBakeFailure = false;
    input.Diagnostics.Report.FailedCubicBakeStatus = 0;
    input.Diagnostics.Report.FailedAnimationChannelIndex = UINT64_MAX;
    Print(input);
    const auto complete = input;
    input.Outcome = SkeletalImportOutcome::CacheHit;
    input.Diagnostics = {};
    Print(input);
    assert(std::strstr(BuildSkeletalImportReport(input).Bytes, "\"cubic_scan\":null"));
    input = complete;
    input.Diagnostics.Report = {};
    input.Diagnostics.Report.bCubicScanStarted = true;
    input.Diagnostics.Report.bCubicScanComplete = true;
    input.Diagnostics.Report.TotalAnimationChannelCount = 3;
    input.Diagnostics.Report.ProcessedAnimationChannelCount = 3;
    Print(input); // 通常LINEARだけなら焼込の誤差は未測定。
    for (int invalid = 0; invalid < 15; ++invalid)
    {
        input = prefix;
        switch (invalid)
        {
        case 0: input.Diagnostics.Report.ProcessedAnimationChannelCount = 4; break;
        case 1: input.Diagnostics.Report.BakedCubicChannelCount = 2; break;
        case 2: input.Diagnostics.Report.BakedCubicTranslationChannelCount = UINT64_MAX; break;
        case 3: input.Diagnostics.Report.CubicInputKeyCount = 1; break;
        case 4: input.Diagnostics.Report.CubicOutputKeyCount = 1; break;
        case 5: input.Diagnostics.Report.CubicOutputKeyCount = UINT64_MAX; break;
        case 6: input.Diagnostics.Report.MaximumCubicTranslationErrorMeters = .002; break;
        case 7: input.Diagnostics.Report.MaximumCubicRotationErrorRadians = .0001; break;
        case 8: input.Diagnostics.Report.MaximumCubicTranslationErrorMeters = std::numeric_limits<double>::quiet_NaN(); break;
        case 9: input.Diagnostics.Report.bCubicScanComplete = true; break;
        case 10: input.Diagnostics.Report.FailedAnimationChannelIndex = 2; break;
        case 11: input.Diagnostics.Report.FailedCubicBakeStatus = 0; break;
        case 12: input.Diagnostics.Report.bCubicScanStarted = false; break;
        case 13: input.Diagnostics.bDecodeAttempted = false; break;
        case 14: input.Outcome = SkeletalImportOutcome::PayloadReady; input.Diagnostics.DecodeStatus = 0; break;
        }
        assert(!BuildSkeletalImportReport(input).bValid);
    }
    input = {};
    input.Options.MorphPolicy = SkeletalMorphPolicy::Drop;
    assert(BuildSkeletalImportReport(input).bValid && std::strstr(BuildSkeletalImportReport(input).Bytes, "\"morph_scan\":null"));
    Print(input);
    input.Diagnostics.bDecodeAttempted = true;
    input.Diagnostics.DecodeStatus = 6;
    Print(input); // 検査途中の失敗は未測定。
    input.Diagnostics.Report.bMorphScanComplete = true;
    input.Diagnostics.Report.DroppedMorphTargetCount = 2;
    input.Diagnostics.Report.MorphTargetWidth = 2;
    input.Diagnostics.Report.DroppedMorphMeshWeightCount = 2;
    input.Diagnostics.Report.DroppedMorphNodeWeightCount = 2;
    input.Diagnostics.Report.DroppedMorphAnimationChannelCount = 1;
    Print(input); // morph検査後に別部分で失敗した場合は検証済み数量を保持。
    const auto morphFailed = input;
    input.Outcome = SkeletalImportOutcome::PayloadReady;
    input.Diagnostics.DecodeStatus = 0;
    Print(input);
    input = {};
    input.Options.MorphPolicy = SkeletalMorphPolicy::Drop;
    input.Outcome = SkeletalImportOutcome::PayloadReady;
    input.Diagnostics.bDecodeAttempted = true;
    input.Diagnostics.Report.bMorphScanComplete = true;
    Print(input); // 除去対象が存在しないことを検査した場合だけ0件。
    input.Outcome = SkeletalImportOutcome::CacheHit;
    input.Diagnostics = {};
    Print(input);
    assert(std::strstr(BuildSkeletalImportReport(input).Bytes, "\"morph_scan\":null"));
    input = complete;
    input.Options.MorphPolicy = SkeletalMorphPolicy::Drop;
    input.Diagnostics.Report.bMorphScanComplete = true;
    input.Diagnostics.Report.DroppedMorphTargetCount = 1;
    input.Diagnostics.Report.MorphTargetWidth = 1;
    Print(input); // Bakeとの組合せでも両方の単位/数量を失わない。
    const auto combinedJson = BuildSkeletalImportReport(input);
    assert(std::strstr(combinedJson.Bytes, "\"cubic_scan\":{") && std::strstr(combinedJson.Bytes, "\"morph_scan\":{"));
    for (int invalid = 0; invalid < 8; ++invalid)
    {
        input = morphFailed;
        switch (invalid)
        {
        case 0: input.Diagnostics.Report.bMorphScanComplete = false; break;
        case 1: input.Diagnostics.bDecodeAttempted = false; break;
        case 2: input.Diagnostics.Report.DroppedMorphTargetCount = UINT64_MAX; break;
        case 3: input.Diagnostics.Report.DroppedMorphMeshWeightCount = 1; break;
        case 4: input.Diagnostics.Report.DroppedMorphNodeWeightCount = 1; break;
        case 5: input.Diagnostics.Report.DroppedMorphAnimationChannelCount = uint64_t{UINT32_MAX}+1; break;
        case 6: input.Diagnostics.Report = {}; input.Diagnostics.Report.bMorphScanComplete = true; input.Diagnostics.Report.DroppedMorphAnimationChannelCount = 1; break;
        case 7: input.Outcome = SkeletalImportOutcome::PayloadReady; input.Diagnostics.DecodeStatus = 0; input.Diagnostics.Report = {}; break;
        }
        assert(!BuildSkeletalImportReport(input).bValid);
    }
    input = morphFailed;
    input.Diagnostics.Report.DroppedMorphTargetCount = 4;
    const auto multiMorph = BuildSkeletalImportReport(input);
    assert(multiMorph.bValid && std::strstr(multiMorph.Bytes, "\"mesh_target_width\":2") &&
        std::strstr(multiMorph.Bytes, "\"dropped_targets\":4"));
    input.Diagnostics.Report.DroppedMorphAnimationChannelCount = 3;
    const auto multiClipMorph = BuildSkeletalImportReport(input);
    Print(input);
    assert(multiClipMorph.bValid && std::strstr(multiClipMorph.Bytes,"\"animation_channels\":3"));
    input.Diagnostics.Report.DroppedMorphTargetCount = 3;
    assert(!BuildSkeletalImportReport(input).bValid);
    input.Diagnostics.Report.DroppedMorphTargetCount = 18;
    assert(!BuildSkeletalImportReport(input).bValid);
    std::cout << "SkeletalImportReportTest PASS: json_stages_measurements_prefix_failure_finite_bounds\n";
    return 0;
}
