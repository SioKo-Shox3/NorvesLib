#pragma once
// 明示library入口。候補bytes/manifestを所有し、file公開や増分skipは行わない。
#include "RigSplitImageInputs.h"
#include "SkeletalImportReport.h"
#include "SkinnedLodBuilder.h"
#include "ClipOptimizeCliOptions.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Asset/AssetManifest.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/ClipImportSettings.h"
namespace NorvesLib::Tools::AssetCook
{
    struct RigSplitCookRequest
    {
        // inventory観測ではpayload/packageを作らず、同じ入力解析とsource hashを使う。
        bool bInventoryOnly = false;
        bool bAnalyzeClips = false;
        // 呼出側が明示したClipImportKeyのビット。
        uint32_t ClipExplicitKeys = 0;
        Core::Skeletal::RigClipAnalysisOptions ClipAnalysis;
        ClipOptimizeCookOptions ClipOptimize;
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
    // LOD の作成結果と、入力の整理で検出した件数。
    struct RigSplitSkinnedLodReport
    {
        bool bBuilt = false;
        uint32_t PartCount = 0, ZeroWeightVertexCount = 0, DegenerateTriangleCount = 0, DuplicateTriangleCount = 0;
        Core::Container::VariableArray<SkinnedLodLevelResult> Lods;
    };
    struct RigSplitCookResult
    {
        RigSplitCookEntry Skeleton, Mesh, Bank;
        Core::Container::AnsiString ManifestJson;
        Core::Container::VariableArray<MeshEmbeddedImage> TexturePlans;
        // baseColorFactorをアルベドへ焼いた材質。元の材質番号の昇順。
        Core::Container::VariableArray<SkeletalBaseColorBake> BaseColorBakes;
        Core::Container::VariableArray<uint64_t> SlotSourceMaterials;
        Core::AssetImport::LoadedImportSettingsDocument Import;
        Core::Skeletal::SkeletalGltfDecodeReport DecodeReport;
        RigSplitSkinnedLodReport SkinnedLod;
        uint64_t SourceHash = 0;
        uint32_t DuplicateMaterialNameGroups = 0;
        Core::Container::VariableArray<Core::Container::AnsiString> Warnings;
        // CPU記録だけ。texture package/renderer materialが揃ったことを意味しない。
        bool bMaterialsRenderStaged = false;
    };
    // sidecarのclip節を呼出側の解析設定へ重ねる。explicitKeysは呼出側が明示したClipImportKeyのビット。
    // 両方にあるキーの値が違えば、errorへ理由の名前を入れてfalseを返し、引数を書き換えない。
    [[nodiscard]] bool ApplyClipImportSettings(const Core::AssetImport::ClipImportSettings& sidecar, uint32_t explicitKeys,
                                               bool& bAnalyzeClips, Core::Skeletal::RigClipAnalysisOptions& analysis,
                                               Core::Container::AnsiString& rootJoint, Core::Container::AnsiString& error);
    [[nodiscard]] bool SerializeRigSplitManifest(
        Core::Container::Span<const Core::Asset::AssetCookedReference> references, Core::Container::AnsiString& out,
        Core::Container::AnsiString& error);
    [[nodiscard]] bool CookRigSplitV1NativePath(Core::Container::Span<const uint8_t> source, const RigSplitCookRequest&,
                                                RigSplitCookResult& out, Core::Skeletal::RigV1Report&,
                                                Core::Container::AnsiString& error);
    // 焼いた材質があるときは import_report、LOD を焼いたときは skinned_lod_report を stderr に 1 行ずつ出す。
    void PrintRigSplitBaseColorImportReport(const RigSplitCookRequest&, const RigSplitCookResult&);
} // namespace NorvesLib::Tools::AssetCook
