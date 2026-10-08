#pragma once

#include "Resource/SkeletalImportOptions.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Tools::AssetCook
{
    // payload生成の診断。CLIのpackage書込完了とは区別し、失敗resultを置換せず返す。
    struct SkeletalCookDiagnostics
    {
        bool bDecodeAttempted = false;
        uint32_t DecodeStatus = 0;
        Core::Skeletal::SkeletalGltfDecodeReport Report;
    };
    enum class SkeletalImportOutcome { Failed, PayloadReady, CacheHit };
    struct SkeletalImportReportInput
    {
        SkeletalImportOutcome Outcome = SkeletalImportOutcome::Failed;
        uint64_t SourceHash = 0;
        Core::Skeletal::SkeletalGltfDecodeOptions Options;
        SkeletalCookDiagnostics Diagnostics;
    };
    struct SkeletalImportReportJson
    {
        char Bytes[4096] = {};
        size_t Size = 0;
        bool bValid = false;
    };
    // 版付きImportReportのskeletal部分。未計測はnull、既知のhashは16桁hex文字列。
    // 自由文/パスを含めず、locale非依存の有限数だけ出す。失敗は空の結果。
    [[nodiscard]] SkeletalImportReportJson BuildSkeletalImportReport(const SkeletalImportReportInput& input) noexcept;
} // namespace NorvesLib::Tools::AssetCook
