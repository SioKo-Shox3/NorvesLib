#pragma once
// 一つのimmutable manifest snapshotで全roleを解決するcooked-only CPU入口。
#include "Animation/RigSplitBinding.h"
#include "Asset/AssetSystem.h"
#include "Animation/RigSplitAssetRequest.h"
namespace NorvesLib::Core::ResourceIO
{
    struct RigSplitLoadPlan
    {
        Container::TSharedPtr<const Asset::AssetSystem> Assets;
        Container::AnsiString SkeletonPath, MeshPath;
        Container::VariableArray<Container::AnsiString> BankPaths;
        Container::AnsiString Variant = Asset::AssetManifest::DefaultVariant;
        Skeletal::RigBindingPolicy Policy;
        Skeletal::RigV1Limits Limits;
        uint64_t MaxPackageBytes = 68ull * 1024 * 1024;
        uint64_t MaxTotalPackageBytes = 256ull * 1024 * 1024;
        Skeletal::RigImportProfile Profile = Skeletal::RigImportProfile::DirectTrs128;
    };
    struct RigSplitResolvedEntry
    {
        Asset::AssetCookedReference Reference;
        uint64_t FullBlobHash = 0;
    };
    struct RigSplitLoadEvidence
    {
        Container::VariableArray<RigSplitResolvedEntry> Entries;
    };
    [[nodiscard]] bool IsRigSplitReferenceFormat(const Asset::AssetCookedReference&, uint32_t role,
                                                 Skeletal::RigImportProfile = Skeletal::RigImportProfile::DirectTrs128);
    [[nodiscard]] bool LoadRigSplitForWorker(const RigSplitLoadPlan&, Skeletal::CookedRigSplitCpuAsset& out,
                                             RigSplitLoadReport&, RigSplitLoadEvidence* evidence = nullptr);
} // namespace NorvesLib::Core::ResourceIO
