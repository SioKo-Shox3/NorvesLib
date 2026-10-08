#pragma once
// 明示library入口。候補bytes/manifestを所有し、file公開や増分skipは行わない。
#include "RigSplitImageInputs.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Asset/AssetManifest.h"
#include "Resource/ImportSettingsFile.h"
namespace NorvesLib::Tools::AssetCook
{
    struct RigSplitCookRequest
    {
        // inventory観測ではpayload/packageを作らず、同じ入力解析とsource hashを使う。
        bool bInventoryOnly = false;
        bool bAnalyzeClips = false;
        Core::Skeletal::RigClipAnalysisOptions ClipAnalysis;
        Core::Container::AnsiString ClipRootJoint;
        std::filesystem::path SourcePath;
        Core::Container::AnsiString SkeletonPath, MeshPath, BankPath;
        Core::Container::AnsiString Variant = "default";
        Core::Container::AnsiString PackageDirectory = "Cooked/Rigs";
        Core::AssetImport::ImportSettingsFileOptions ImportOptions;
        Core::AssetImport::EmissiveScale AssetSetEmission;
        Core::Skeletal::SkeletalGltfDecodeOptions DecodeOptions;
        Core::Skeletal::RigV1Limits Limits;
        RigSplitImageLimits ImageLimits;
        Core::Skeletal::RigImportProfile Profile = Core::Skeletal::RigImportProfile::DirectTrs128;
    };
    struct RigSplitCookEntry
    {
        Core::Asset::AssetCookedReference Reference;
        Core::Container::VariableArray<uint8_t> Payload, Package;
    };
    struct RigSplitCookResult
    {
        RigSplitCookEntry Skeleton, Mesh, Bank;
        Core::Container::AnsiString ManifestJson;
        Core::Container::VariableArray<MeshEmbeddedImage> TexturePlans;
        Core::Container::VariableArray<uint64_t> SlotSourceMaterials;
        Core::AssetImport::LoadedImportSettingsDocument Import;
        Core::Skeletal::SkeletalGltfDecodeReport DecodeReport;
        uint64_t SourceHash = 0;
        uint32_t DuplicateMaterialNameGroups = 0;
        Core::Container::VariableArray<Core::Container::AnsiString> Warnings;
        // CPU記録だけ。texture package/renderer materialが揃ったことを意味しない。
        bool bMaterialsRenderStaged = false;
    };
    [[nodiscard]] bool SerializeRigSplitManifest(
        Core::Container::Span<const Core::Asset::AssetCookedReference> references, Core::Container::AnsiString& out,
        Core::Container::AnsiString& error);
    [[nodiscard]] bool CookRigSplitV1NativePath(Core::Container::Span<const uint8_t> source, const RigSplitCookRequest&,
                                                RigSplitCookResult& out, Core::Skeletal::RigV1Report&,
                                                Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook
