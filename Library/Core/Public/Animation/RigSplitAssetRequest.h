#pragma once
// ordered Bankと束縛方針を所有する公開要求。snapshotとRegistryはruntimeが注入する。
#include "Animation/RigBindingTypes.h"
#include "Asset/AssetResolveResult.h"
namespace NorvesLib::Core::Skeletal
{
    struct RigSplitRequest
    {
        Container::AnsiString SkeletonPath, MeshPath;
        Container::VariableArray<Container::AnsiString> BankPaths;
        Container::AnsiString Variant = Asset::AssetManifest::DefaultVariant;
        RigBindingPolicy Policy;
        RigV1Limits Limits;
        uint64_t MaxPackageBytes = 68ull * 1024 * 1024;
        uint64_t MaxTotalPackageBytes = 256ull * 1024 * 1024;
        RigImportProfile Profile = RigImportProfile::DirectTrs128;
    };
} // namespace NorvesLib::Core::Skeletal
namespace NorvesLib::Core::ResourceIO
{
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
} // namespace NorvesLib::Core::ResourceIO
namespace NorvesLib::Core
{
    // const共有所有でcallback/cache取得後にも有効。worker失敗の全Bank差を保持する。
    struct RigSplitAssetDiagnostics
    {
        ResourceIO::RigSplitLoadReport Load;
        Skeletal::RigSplitReport Assembly;
    };
} // namespace NorvesLib::Core
