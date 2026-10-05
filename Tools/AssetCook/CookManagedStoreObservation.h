#pragma once
#include "CookOwnerResolver.h"
#include "Container/FixedArray.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>
namespace NorvesLib::Tools::AssetCook
{
    struct CookManagedRootClaim
    {
        Core::Container::AnsiString ClaimId, RootLeaf, OwnerId;
        Core::Container::FixedArray<uint8_t, 16> DirectoryId;
    };
    struct CookManagedStoreView
    {
        // handle由来の比較・診断用。保存locatorとして後のI/Oへ渡さない。
        std::filesystem::path CanonicalWorkspace, CanonicalStore;
        Core::Container::AnsiString StoreId;
        Core::Container::FixedArray<uint8_t, 16> WorkspaceId, StoreDirectoryId;
        Core::Container::VariableArray<CookManagedRootClaim> Roots;
        uint64_t VolumeSerial = 0, IndexGeneration = 0;
        bool bCurrentWorkspace = false;
    };
    struct CookManagedStoreObservation
    {
        CookResolvedOwnerBinding Owner;
        Core::Container::AnsiString CanonicalVolumeGuid, CurrentRootClaimId;
        Core::Container::VariableArray<CookManagedStoreView> Stores;
        bool bRuntimeRootClaimed = false;
    };
    enum class CookManagedStoreResult
    {
        StoreMissing,
        Observed,
        NeedsRecovery,
        Conflict,
        Busy,
        Error
    };
    inline constexpr size_t MaximumCookStoreEntries = 65536;
    inline constexpr size_t MaximumCookStoreRoots = 4096;
    inline constexpr size_t MaximumCookStoreMetadataBytes = 32 * 1024 * 1024;
    // 同volume lock内の読み取り専用観測。StoreMissing/Observedだけoutを更新する。
    // stable namespaceと協調writerが前提。全volumeの復旧・認証・書込許可を意味しない。
    // 既存unknown root/store、物理volume直下workspace、入れ子/control領域、未完pendingを拒否。
    // errorはrequest/out内文字列と独立に渡す。初期化・state採用・回復・公開はしない。
    [[nodiscard]] CookManagedStoreResult ObserveCookManagedStore(const CookOwnerResolveRequest& request,
                                                                 CookManagedStoreObservation& out,
                                                                 Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
