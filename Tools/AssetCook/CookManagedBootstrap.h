#pragma once
#include "CookOwnerResolver.h"
#include "CookOwnedState.h"
#include "SingleAssetCook.h"
#include "CookBatchReport.h"
namespace NorvesLib::Tools::AssetCook
{
    struct CookManagedBootstrapRequest
    {
        CookOwnerResolveRequest Owner;
        Core::Container::Span<const SingleAssetCookRequest> Assets;
        // 解析に使ったspecの原bytes。lock取得後と公開直前に実fileと一致させる。
        Core::Container::Span<const uint8_t> ExpectedSpecBytes;
        // CookerRevisions が空のときに全 asset に使う cooker の版。
        uint64_t CookerRevision = 1;
        bool bLegacyTextureManifest = true;
        Core::Container::Span<const CookAssetBudget> Budgets;
        CookAssetBudget TotalBudget;
        CookBatchReport* Report = nullptr;
        uint32_t Jobs = 1;
        bool bForce = false, bPrune = false;
        // generator の記録。nullptr なら前回の state の記録を引き継ぐ(新規 root では空)。指すときはこの列で置き換える。
        const Core::Container::VariableArray<CookGeneratorRecord>* Generators = nullptr;
        // asset ごとの cooker の版(Assets と同じ並び)。空なら全 asset に CookerRevision を使う。
        Core::Container::Span<const uint64_t> CookerRevisions;
    };
    [[nodiscard]] inline uint64_t CookerRevisionOf(const CookManagedBootstrapRequest& request, size_t index)
    {
        return request.CookerRevisions.empty() ? request.CookerRevision : request.CookerRevisions[index];
    }
    [[nodiscard]] inline bool ValidCookerRevisions(const CookManagedBootstrapRequest& request)
    {
        if (request.CookerRevisions.empty())
        {
            return request.CookerRevision != 0;
        }
        if (!request.CookerRevisions.data() || request.CookerRevisions.size() != request.Assets.size())
        {
            return false;
        }
        for (const uint64_t revision : request.CookerRevisions)
        {
            if (revision == 0)
            {
                return false;
            }
        }
        return true;
    }
    struct CookManagedBootstrapOutcome
    {
        Core::Container::AnsiString ClaimId;
        uint64_t IndexGeneration = 0, StateGeneration = 0;
        std::filesystem::path RetiredDirectory;
        bool bCleanupIncomplete = false;
    };
    enum class CookManagedBootstrapResult
    {
        Created,
        NeedsRecovery,
        CommittedButError,
        Conflict,
        Busy,
        Error
    };
    enum class CookManagedRecoveryResult
    {
        NoPending,
        RolledBack,
        Committed,
        Conflict,
        Busy,
        Error
    };
    // 初期profileはtexture spec v1、初期化済みstore、不在runtimeのみ。
    // controllerが共通prepare/captureからintentを作る。外からintentをwrite capabilityとして受け取らない。
    // Createdだけoutを更新する。公開済みpendingがある失敗はNeedsRecoveryとして保存する。
    // errorはrequest/outと独立に渡す。CommittedButErrorでは公開済みの安定状態を保持する。
    [[nodiscard]] CookManagedBootstrapResult BootstrapCookManagedAssetSet(const CookManagedBootstrapRequest& request,
                                                                          CookManagedBootstrapOutcome& out,
                                                                          Core::Container::AnsiString& error);
    // source/specを読まない。caller locatorからlock/物理workspaceを取得し、その固定pendingだけを扱う。
    // RolledBack/Committedだけoutを更新。未知objectは削除・採用しない。errorは入力/outと独立に渡す。
    [[nodiscard]] CookManagedRecoveryResult RecoverCookManagedPending(const std::filesystem::path& runtimeLocator,
                                                                      CookManagedBootstrapOutcome& out,
                                                                      Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
