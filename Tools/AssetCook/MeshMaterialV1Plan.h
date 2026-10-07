#pragma once
// 明示NVMESH v1だけの材質/画像解析。cookとfingerprintで同じ結果を用いる。
#include "MeshCooker.h"
#include "TextureCooker.h"
#include "GeometryInspection.h"
#include "Asset/CookedMaterialFormat.h"
#include "Resource/ImportSettingsFile.h"
#include "Resource/GltfBufferSet.h"
namespace NorvesLib::Core
{
    class JsonValue;
}
namespace NorvesLib::Tools::AssetCook
{
    // splitのsame-read入口。旧入口はnullptrで従来read/hash経路を保つ。
    struct LoadedMeshMaterialV1Input
    {
        const Core::AssetImport::LoadedImportSettingsDocument* Import = nullptr;
        const Core::AssetImport::SourceMaterialCatalog* Catalog = nullptr;
        const Core::AssetImport::ResolvedMaterialImportPlan* Resolved = nullptr;
        void* ImageContext = nullptr;
        bool (*ReadImage)(uint32_t, MeshEmbeddedImage&, DecodedTextureRgba8&, Core::Gltf::DataUriMime&, void*,
                          Core::Container::AnsiString&) = nullptr;
        bool (*ReserveDerived)(uint64_t, uint32_t, uint32_t, void*, Core::Container::AnsiString&) = nullptr;
    };
    struct MeshMaterialV1Plan
    {
        Core::AssetImport::LoadedImportSettingsDocument Import;
        Core::AssetImport::ResolvedMaterialImportPlan Resolved;
        Core::AssetImport::DoubleSidedSetting Sidedness = Core::AssetImport::DoubleSidedSetting::Auto;
        GeometryClosurePolicy Closure;
        Core::Asset::CookedMaterialRecord Material;
        Core::Container::AnsiString Textures[4];
        Core::Container::VariableArray<MeshEmbeddedImage> Images;
        uint64_t SourceHash = 0, SettingsHash = 0;
    };
    // 元材質の番号順。暗黙材質は番号0とは別の末尾slotとする。
    inline constexpr uint64_t ImplicitMeshMaterialIndex = UINT64_MAX;
    struct MeshMaterialV1Entry
    {
        Core::Asset::CookedMaterialRecord Material;
        Core::AssetImport::DoubleSidedSetting Sidedness = Core::AssetImport::DoubleSidedSetting::Auto;
        Core::Container::AnsiString Textures[4];
    };
    struct MeshMaterialV1SetPlan
    {
        Core::AssetImport::LoadedImportSettingsDocument Import;
        GeometryClosurePolicy Closure;
        Core::Container::VariableArray<MeshMaterialV1Entry> Materials;
        Core::Container::VariableArray<MeshEmbeddedImage> Images;
        uint32_t DuplicateMaterialNameGroups = 0, FirstDuplicateMaterialIndex = UINT32_MAX,
                 SecondDuplicateMaterialIndex = UINT32_MAX;
        uint64_t SourceHash = 0, SettingsHash = 0;
    };
    [[nodiscard]] bool PrepareMeshMaterialV1Set(const Core::JsonValue& root, const Core::Gltf::BufferSet& buffers,
                                                const std::filesystem::path& sourcePath,
                                                Core::Container::AnsiStringView logicalPath,
                                                Core::Container::Span<const uint64_t> sortedMaterialIndices,
                                                uint64_t gltfSourceHash,
                                                const Core::AssetImport::ImportSettingsFileOptions* options,
                                                MeshMaterialV1SetPlan& out, Core::Container::AnsiString& error);
    inline constexpr uint32_t SyntheticArmImageIndex = UINT32_MAX;
    [[nodiscard]] bool PrepareMeshMaterialV1(const Core::JsonValue& root, const Core::Gltf::BufferSet& buffers,
                                             const std::filesystem::path& sourcePath,
                                             Core::Container::AnsiStringView logicalPath, bool bHasMaterial,
                                             uint32_t materialIndex, uint64_t gltfSourceHash,
                                             const Core::AssetImport::ImportSettingsFileOptions* options,
                                             MeshMaterialV1Plan& out, Core::Container::AnsiString& error,
                                             const LoadedMeshMaterialV1Input* loaded = nullptr);
    void WarnDuplicateMeshMaterials(Core::Container::AnsiStringView asset, uint32_t groups, uint32_t first,
                                    uint32_t second);
} // namespace NorvesLib::Tools::AssetCook
