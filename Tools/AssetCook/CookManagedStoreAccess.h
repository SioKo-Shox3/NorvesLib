#pragma once
#include "CookManagedStoreObservation.h"
#include "CookDestinationLock.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    // 同期volume lock callback内専用。mutexを再取得せずordinaryの全検査を共有する。
    [[nodiscard]] CookManagedStoreResult ObserveCookManagedStoreLocked(const CookDestinationLockContext& lock,
                                                                       const CookOwnerResolveRequest& request,
                                                                       CookManagedStoreObservation& out,
                                                                       Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
