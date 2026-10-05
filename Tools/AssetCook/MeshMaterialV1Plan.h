#pragma once
// 明示NVMESH v1だけの材質/画像解析。cookとfingerprintで同じ結果を用いる。
#include "MeshCooker.h"
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
    inline constexpr uint32_t SyntheticArmImageIndex = UINT32_MAX;
    [[nodiscard]] bool PrepareMeshMaterialV1(const Core::JsonValue& root, const Core::Gltf::BufferSet& buffers,
                                             const std::filesystem::path& sourcePath,
                                             Core::Container::AnsiStringView logicalPath, bool bHasMaterial,
                                             uint32_t materialIndex, uint64_t gltfSourceHash,
                                             const Core::AssetImport::ImportSettingsFileOptions* options,
                                             MeshMaterialV1Plan& out, Core::Container::AnsiString& error);
    void WarnDuplicateMeshMaterials(Core::Container::AnsiStringView asset, uint32_t groups, uint32_t first,
                                    uint32_t second);
} // namespace NorvesLib::Tools::AssetCook
