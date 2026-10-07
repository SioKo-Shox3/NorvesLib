#pragma once
// 一つのimmutable manifest snapshotで全roleを解決するcooked-only CPU入口。
#include "Animation/RigSplitBinding.h"
#include "Asset/AssetSystem.h"
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
    };
    enum class RigSplitLoadStatus : uint8_t
    {
        Success,
        InvalidRequest,
        ResolveRejected,
        FormatRejected,
        ParseRejected,
        MetadataMismatch,
        BindingRejected,
        Exception
    };
    struct RigSplitLoadReport
    {
        RigSplitLoadStatus Status = RigSplitLoadStatus::InvalidRequest;
        Container::AnsiString LogicalPath;
        uint64_t PackageBytesRead = 0;
        Asset::AssetResolveStatus ResolveStatus = Asset::AssetResolveStatus::InvalidRequest;
        Asset::AssetReadStatus PackageReadStatus = Asset::AssetReadStatus::InvalidRequest;
        Skeletal::RigV1Report ParseReport;
        Skeletal::RigSplitReport BindingReport;
    };
    [[nodiscard]] bool LoadRigSplitForWorker(const RigSplitLoadPlan&, Skeletal::CookedRigSplitCpuAsset& out,
                                             RigSplitLoadReport&);
} // namespace NorvesLib::Core::ResourceIO
