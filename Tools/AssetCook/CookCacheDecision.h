#pragma once
#include "CookDependencySnapshot.h"
#include "CookOutputPackage.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookRecordedOutput
    {
        Core::Asset::AssetCookedReference Reference;
        CookOutputPackageFingerprint Package;
    };
    struct CookOutputRecord
    {
        uint32_t SchemaVersion = 1, DependencySchemaVersion = 1;
        uint64_t CookerRevision = 0, DependencyFingerprint = 0;
        Core::Container::VariableArray<CookRecordedOutput> Outputs;
    };
    enum class CookDecision
    {
        Cook,
        Skip,
        Error
    };
    enum class CookDecisionReason
    {
        Forced,
        MissingRecord,
        RecordVersion,
        DependenciesChanged,
        InventoryChanged,
        ManifestMismatch,
        PackageInvalid,
        PackageChanged,
        Current
    };
    struct CookDecisionContext
    {
        SingleAssetCookRequest Request;
        CookDependencySnapshot Dependencies;
        CookDecisionReason Reason = CookDecisionReason::MissingRecord;
        CookAssetMetrics VerifiedMetrics;
    };
    // currentManifestはcallerがRequest.ManifestPathの現在の集約状態として読んだ値。
    // batchで一度だけparseできる。nullptr/未読込は利用可能なmanifest無しとして扱う。
    // 成功時の実行要求は常にbSkipIfUnchanged=false。Error時だけoutContextを保持する。
    // これは読取専用判断であり、既存fileの上書き・所有権・publishを許可しない。
    [[nodiscard]] CookDecision DecideCookCache(const SingleAssetCookRequest& request, uint64_t cookerRevision,
                                               bool bAllowSkip, const CookOutputRecord* previous,
                                               const Core::Asset::AssetManifest* currentManifest,
                                               CookDecisionContext& outContext, Core::Container::AnsiString& error);
    // beforeCookはcook前のDecideCookCache結果。実cook成功後に生成manifestと全出力を照合する。
    // 依存を採取前後で再確認し、成功時だけoutRecordを置換する。stateの保存は別層。
    [[nodiscard]] bool CaptureCookOutputRecord(const CookDecisionContext& beforeCook,
                                               const Core::Asset::AssetManifest& cookedManifest,
                                               CookOutputRecord& outRecord, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
