#pragma once
#include "CookStateFile.h"
#include "CookOutputPlan.h"
namespace NorvesLib::Tools::AssetCook
{
    enum class CookBeforeImageRequirement
    {
        CaptureOwnedFileOrProveAbsence,
        CaptureExactPreviousStateFile,
        RequireAbsence
    };
    struct CookManagedPackageTarget
    {
        size_t PlanOutputIndex = 0, PreviousOutputIndex = 0;
        std::filesystem::path TargetPath;
        CookBeforeImageRequirement Before = CookBeforeImageRequirement::CaptureOwnedFileOrProveAbsence;
    };
    struct CookManagedAssetInventory
    {
        CookPreparedPlan FinalPlan;
        size_t PreviousRecordIndex = 0;
        Core::Container::VariableArray<CookManagedPackageTarget> Packages;
    };
    struct CookManagedUpdateInventory
    {
        CookStateFileRequest Scope;
        CookOwnedState PreviousState;
        Core::Container::VariableArray<CookManagedAssetInventory> Assets;
        std::filesystem::path ManifestTarget;
        CookBeforeImageRequirement ManifestBefore = CookBeforeImageRequirement::CaptureOwnedFileOrProveAbsence;
        CookBeforeImageRequirement StateBefore = CookBeforeImageRequirement::CaptureExactPreviousStateFile;
        uint64_t BaseGeneration = 0, ProposedMutationGeneration = 0;
        // 上限ではProposedMutationGeneration=0。no-opの世代維持は可能、変更時は必ず拒否する。
        bool bCanAdvanceGeneration = false;
    };
    inline constexpr size_t MaximumCookManagedInventoryBytes = 32 * 1024 * 1024;
    // file I/Oをしない値の対応検査。scope.ExpectedBindingは旧stateから採用せず独立に渡す。
    // flat manifest、同じprimary/output keyとpackage名だけを受理。source/hash/format/revision変更は許す。
    // 成功はfilesystem所有/現在性/Skip/公開許可ではない。lock・recovery・再読込・共通再検証が必須。
    // errorは入力/out内の文字列とは独立に渡す。false時outを保持する。
    [[nodiscard]] bool BuildCookManagedUpdateInventory(const CookStateFileRequest& scope,
                                                       const CookOwnedState& previous,
                                                       Core::Container::Span<const CookPreparedPlan> finalPlans,
                                                       CookManagedUpdateInventory& out,
                                                       Core::Container::AnsiString& error,
                                                       bool bAllowInventoryChanges = false);
} // namespace NorvesLib::Tools::AssetCook
