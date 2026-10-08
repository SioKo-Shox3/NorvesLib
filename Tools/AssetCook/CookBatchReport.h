#pragma once
// cook済みpackageから得る統計。予算はcook形式のハード上限とは別に扱う。
#include "Container/String.h"
#include "Container/VariableArray.h"
#include <filesystem>
#include <cstdint>
namespace NorvesLib::Tools::AssetCook
{
    struct CookAssetMetrics
    {
        uint64_t Triangles = 0, Joints = 0, TextureBytes = 0, CookedBytes = 0;
    };
    struct CookAssetBudget
    {
        uint64_t MaxTriangles = UINT64_MAX, MaxJoints = UINT64_MAX;
        uint64_t MaxTextureBytes = UINT64_MAX, MaxCookedBytes = UINT64_MAX;
    };
    struct CookAssetReportRow
    {
        Core::Container::AnsiString LogicalPath, Kind;
        bool bSkipped = false, bProcessed = false;
        uint64_t SourceBytes = 0;
        CookAssetMetrics Metrics;
        CookAssetBudget Budget;
    };
    struct CookBatchReport
    {
        bool bEnabled = false, bWarnBudget = false;
        std::filesystem::path RuntimeRoot, ReportDirectory;
        Core::Container::VariableArray<CookAssetReportRow> Assets;
        CookAssetMetrics Totals;
        uint32_t BudgetErrors = 0;
        bool bFailed = false;
        Core::Container::AnsiString Error;
    };
    // 同一assetの複数roleではjointを重複加算しない。
    [[nodiscard]] bool MergeCookOutputMetrics(CookAssetMetrics& total, const CookAssetMetrics& value);
    [[nodiscard]] bool CheckCookBatchBudgets(CookBatchReport&, const CookAssetBudget& totalBudget);
    [[nodiscard]] bool WriteCookBatchReport(CookBatchReport&, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
