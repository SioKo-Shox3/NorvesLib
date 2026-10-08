#pragma once
#include "CookManagedStoreInitialization.h"
namespace NorvesLib::Tools::AssetCook::Detail
{
    enum class CookStoreInitFault
    {
        None,
        Rng,
        StageOpen,
        StageIdentity,
        HeaderCreate,
        IndexCreate,
        HeaderPartialWrite,
        IndexPartialWrite,
        ZeroWrite,
        Flush,
        Seek,
        ReadBack,
        ByteMismatch,
        Parse,
        ChildClose,
        Rename,
        CleanupDisposition,
        AfterPublishIdentity,
        AfterPublishClose,
        AfterPublishObservation,
        MutexRelease
    };
    enum class CookStoreInitPoint
    {
        StageCreated,
        HeaderWritten,
        IndexWritten,
        ChildrenClosed,
        BeforeRename,
        Renamed
    };
    struct CookStoreInitProbe
    {
        CookStoreInitFault Fault = CookStoreInitFault::None;
        // 固定prefix＋hex32の最初の候補だけ。既存衝突は採用も削除もしない。
        Core::Container::AnsiString FirstStageLeaf;
        void (*Checkpoint)(CookStoreInitPoint point, const std::filesystem::path& stage,
                           const std::filesystem::path& destination, void* context) = nullptr;
        void* Context = nullptr;
    };
    [[nodiscard]] CookManagedStoreInitializationResult InitializeNewCookManagedStoreForTest(
        const CookOwnerResolveRequest& request, const CookStoreInitProbe& probe, CookManagedStoreObservation& out,
        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook::Detail
