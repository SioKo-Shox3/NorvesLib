#pragma once
#include "CookManagedStoreObservation.h"
namespace NorvesLib::Tools::AssetCook
{
    enum class CookManagedStoreInitializationResult
    {
        Created,
        StoreExists,
        NeedsRecovery,
        Conflict,
        Busy,
        Error,
        PublishedButError
    };
    // fresh control storeだけを作る。Createdだけoutを更新し、その他はoutを保持する。
    // PublishedButErrorではstoreが公開済み。削除/自動再試行せず、明示的に再観測する。
    // root/state/package/claim登録やjournalの作成は含まない。errorはrequest/outと独立に渡す。
    [[nodiscard]] CookManagedStoreInitializationResult InitializeNewCookManagedStore(
        const CookOwnerResolveRequest& request, CookManagedStoreObservation& out, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
