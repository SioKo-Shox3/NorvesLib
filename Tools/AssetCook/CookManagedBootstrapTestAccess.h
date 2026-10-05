#pragma once
#include "CookManagedBootstrap.h"
#include "CookDestinationLockTestAccess.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    enum class CookManagedBootstrapPoint
    {
        Prepared,
        PendingPublished,
        RootPublished,
        StatePublished,
        IndexBackedUp,
        IndexPublished,
        BeforeReceipt,
        ReceiptCommitted,
        BeforeRetire,
        Retired,
        RollbackIndexRemoved,
        RollbackIndexRestored,
        RollbackStateRestored,
        RollbackRootRestored
    };
    struct CookManagedBootstrapProbe
    {
        // falseは通常の途中失敗を報告する。実process終了はfixtureがcallback内で行う。
        bool (*Checkpoint)(CookManagedBootstrapPoint point, const std::filesystem::path& transaction,
                           const std::filesystem::path& runtime, void* context) = nullptr;
        void* Context = nullptr;
        CookLockFault LockFault = CookLockFault::None;
        bool* bAbandonedObserved = nullptr;
    };
    [[nodiscard]] CookManagedBootstrapResult BootstrapCookManagedAssetSetForTest(
        const CookManagedBootstrapRequest& request, const CookManagedBootstrapProbe& probe,
        CookManagedBootstrapOutcome& out, Core::Container::AnsiString& error);
    [[nodiscard]] CookManagedRecoveryResult RecoverCookManagedPendingForTest(
        const std::filesystem::path& runtimeLocator, const CookManagedBootstrapProbe& probe,
        CookManagedBootstrapOutcome& out, Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
