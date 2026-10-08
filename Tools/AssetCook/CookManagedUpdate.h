#pragma once
#include "CookManagedBootstrap.h"
namespace NorvesLib::Tools::AssetCook
{
    // texture v1の入力要求と成功payloadは新規作成と共通。既存claimの固定inventoryだけを更新する。
    using CookManagedUpdateRequest = CookManagedBootstrapRequest;
    using CookManagedUpdateOutcome = CookManagedBootstrapOutcome;
    enum class CookManagedUpdateResult
    {
        NoChange,
        Updated,
        NeedsRecovery,
        CommittedButError,
        Conflict,
        Busy,
        Error
    };
    // NoChange/Updatedだけoutを更新。NoChangeはstageを作らず、世代上限でも現在値を返す。
    // 不在packageの親は作り直さない。source不在復旧は既存RecoverCookManagedPendingを使う。
    [[nodiscard]] CookManagedUpdateResult UpdateCookManagedAssetSet(const CookManagedUpdateRequest& request,
                                                                    CookManagedUpdateOutcome& out,
                                                                    Core::Container::AnsiString& error);
} // 名前空間 NorvesLib::Tools::AssetCook
