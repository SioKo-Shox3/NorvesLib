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
    std::cout << "SkeletalImportReportTest PASS: json_stages_measurements_prefix_failure_finite_bounds\n";
    return 0;
}
