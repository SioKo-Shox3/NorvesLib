#pragma once
// wireから独立した所有材質値。GPUへ未接続の間は製品finalizeで拒否する。
#include "Asset/CookedMeshFormat.h"
#include "Rendering/MaterialTypes.h"
namespace NorvesLib::Core::ResourceIO::ModelStaging
{
    enum class ImportedMaterialLayout : uint8_t
    {
        Absent,
        PackedArmV1
    };
    enum class ImportedAlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend
    };
    struct ImportedMaterialStaging
    {
        ImportedMaterialLayout Layout = ImportedMaterialLayout::Absent;
        Rendering::ShadingModel Shading = Rendering::ShadingModel::DefaultLit;
        Container::AnsiString AlbedoPath, NormalPath, ArmPath, EmissivePath;
        float BaseColor[4] = {1, 1, 1, 1};
        float EmissiveColor[3]{};
        float EmissiveLuminanceNits = 0;
        float Metallic = -1, Roughness = -1;
        float OcclusionStrength = 1, NormalScale = 1, AlphaCutoff = .5f;
        uint8_t ArmMask = 0;
        ImportedAlphaMode Alpha = ImportedAlphaMode::Opaque;
        bool bDoubleSided = false;
    };
    enum class MaterialStagingStatus : uint8_t
    {
        Success,
        UnsupportedVersion,
        InvalidMaterialIndex,
        InvalidRecord,
        InvalidReference,
        InvalidPath
    };
    // UTF-8論理pathを所有し、係数はbitを保ったままコピーする。blob/ファイル/GPUを保持・生成しない。
    // 手組みCookedMeshDataでも材質値と参照境界を検査し、成功時だけoutを置換する。
    // これは製品ロードの許可ではない。geometry/submesh/描画対応は呼出側の別gate。
    [[nodiscard]] MaterialStagingStatus BuildImportedMaterialStaging(const Asset::CookedMeshData& mesh,
                                                                     size_t materialIndex,
                                                                     ImportedMaterialStaging& out);
    // 3 資産の SkinnedMesh の材質(StringRef が 0 の record と、所有の 4 つの論理パス)を写す。
    // texturePaths は albedo・normal・arm・emissive の 4 つちょうど。検査と写し方は CookedMeshData 版と同じで、成功時だけ out を置き換える。
    [[nodiscard]] MaterialStagingStatus BuildImportedMaterialStaging(const Asset::CookedMaterialRecord& record,
                                                                    Container::Span<const Container::AnsiString> texturePaths,
                                                                    ImportedMaterialStaging& out);
} // namespace NorvesLib::Core::ResourceIO::ModelStaging
