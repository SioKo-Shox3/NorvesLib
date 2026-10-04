#pragma once
#include "CookCacheDecision.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookPreparedOutput
    {
        // cook前のidentity。CookedHash/骨格数量の実測や所有の証明ではない。
        Core::Asset::AssetCookedReference ExpectedIdentity;
        std::filesystem::path TargetPath;
    };
    struct CookPreparedPlan
    {
        CookDecisionContext Context;
        // primaryが先、参照された内包画像はImageIndex順。外部画像は依存にだけ入る。
        Core::Container::VariableArray<CookPreparedOutput> Outputs;
    };
    // 同じPrepare/BuildInventoryで読み取り専用の観測を作る。Skip/公開許可は決定しない。
    [[nodiscard]] bool PrepareCookOutputPlan(const SingleAssetCookRequest& request, uint64_t cookerRevision,
                                             const Core::Asset::AssetManifest* currentManifest, CookPreparedPlan& out,
                                             Core::Container::AnsiString& error);
    // callerが排他所有する空の既存stage directoryへ、出力rootだけを写す。fileは作らない。
    // final rootとstageは物理的に非包含。source/base/設定/要求意味と始点依存を保持する。
    [[nodiscard]] bool PrepareCookStagingPlan(const CookPreparedPlan& finalPlan, const std::filesystem::path& stageRoot,
                                              CookPreparedPlan& outStage, Core::Container::AnsiString& error);
    // 全fragment inventoryと始点依存を検証し、共通captureへ渡す。失敗時outを保持する。
    // stageもfinalもcallerが上記入口で作った値を渡す。改ざん防止tokenや公開権限ではない。
    [[nodiscard]] bool CaptureStagedCookOutputRecord(const CookPreparedPlan& finalPlan,
                                                     const CookPreparedPlan& stagePlan,
                                                     const Core::Asset::AssetManifest& stageManifest,
                                                     CookOutputRecord& out, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
