#pragma once

#include "Container/Span.h"
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
    // cookでbaseColorFactorをアルベドへ焼いた材質の記録。スキン描画が材質の色を読まない間の暫定。
    struct SkeletalBaseColorBake
    {
        uint64_t MaterialIndex = UINT64_MAX; // 元のglTF材質番号。暗黙材質はUINT64_MAX。
        float Factor[3] = {1.0f, 1.0f, 1.0f}; // 焼いた元のbaseColorFactorのRGB。
        bool bSolid = false; // アルベドのテクスチャがなく4×4の単色画像を生成。
    };
    struct SkeletalImportReportInput
    {
        SkeletalImportOutcome Outcome = SkeletalImportOutcome::Failed;
        uint64_t SourceHash = 0;
        Core::Skeletal::SkeletalGltfDecodeOptions Options;
        SkeletalCookDiagnostics Diagnostics;
        Core::Container::Span<const SkeletalBaseColorBake> BaseColorBakes;
    };
    struct SkeletalImportReportJson
    {
        char Bytes[4096] = {};
        size_t Size = 0;
        bool bValid = false;
    };
    // 版付きImportReportのskeletal部分。未計測はnull、既知のhashは16桁hex文字列。
    // 焼いた記録があるときだけ、ルートにbase_color_bakesを出力する。
    // 自由文/パスを含めず、locale非依存の有限数だけ出す。失敗は空の結果。
    [[nodiscard]] SkeletalImportReportJson BuildSkeletalImportReport(const SkeletalImportReportInput& input) noexcept;
} // namespace NorvesLib::Tools::AssetCook
