#include "Resource/ModelMaterialStaging.h"
#include "Asset/AssetPath.h"
#include "Text/UnicodeText.h"
#include <utility>
namespace NorvesLib::Core::ResourceIO::ModelStaging
{
    namespace
    {
        MaterialStagingStatus CopyPath(const Asset::CookedMeshData& mesh, const Asset::CookedMeshStringRef& reference,
                                       const Asset::CookedMaterialStringRef& record, Container::AnsiString& out)
        {
            if (reference.StringOffset != record.Offset || reference.StringLength != record.Length ||
                (!record.Length && record.Offset))
            {
                return MaterialStagingStatus::InvalidReference;
            }
            const auto text = mesh.GetString(reference);
            if (text.size() != record.Length)
            {
                return MaterialStagingStatus::InvalidReference;
            }
            if (text.empty())
            {
                return MaterialStagingStatus::Success;
            }
            bool bAllowedScalars = true;
            if (!TextDetail::ForEachUnicodeScalar(Container::Span<const char>(text.data(), text.size()),
                                                  [&](uint32_t scalar)
                                                  {
                                                      bAllowedScalars =
                                                          bAllowedScalars && scalar >= 0x20 && scalar != 0x7f;
                                                  }) ||
                !bAllowedScalars)
            {
                return MaterialStagingStatus::InvalidPath;
            }
            const auto path = Asset::AssetPath::Normalize(text);
            if (!path.HasLogicalPath() || Container::AnsiStringView(path.GetLogicalPath()) != text)
            {
                return MaterialStagingStatus::InvalidPath;
            }
            out = Container::AnsiString(text);
            return MaterialStagingStatus::Success;
        }
    } // namespace
    MaterialStagingStatus BuildImportedMaterialStaging(const Asset::CookedMeshData& mesh, size_t materialIndex,
                                                       ImportedMaterialStaging& out)
    {
        if (mesh.VersionMajor != 1)
        {
            return MaterialStagingStatus::UnsupportedVersion;
        }
        if (materialIndex >= mesh.Materials.size())
        {
            return MaterialStagingStatus::InvalidMaterialIndex;
        }
        const auto& material = mesh.Materials[materialIndex];
        const auto& record = material.Pbr;
        if (Asset::ValidateCookedMaterialRecord(record, mesh.StringTableSize) != Asset::CookedMaterialStatus::Success)
        {
            return MaterialStagingStatus::InvalidRecord;
        }
        ImportedMaterialStaging candidate;
        const Asset::CookedMeshStringRef references[] = {material.AlbedoTexture, material.NormalTexture,
                                                         material.ArmTexture, material.EmissiveTexture};
        const Asset::CookedMaterialStringRef wireReferences[] = {record.Albedo, record.Normal, record.Arm,
                                                                 record.Emissive};
        Container::AnsiString* paths[] = {&candidate.AlbedoPath, &candidate.NormalPath, &candidate.ArmPath,
                                          &candidate.EmissivePath};
        for (size_t i = 0; i < 4; ++i)
        {
            const auto status = CopyPath(mesh, references[i], wireReferences[i], *paths[i]);
            if (status != MaterialStagingStatus::Success)
            {
                return status;
            }
        }
        // wire 0とruntime Unlit 0を混同しない。
        switch (record.ShadingModelId)
        {
        case Asset::CookedMaterialFormatV1::DefaultLit:
            candidate.Shading = Rendering::ShadingModel::DefaultLit;
            break;
        default:
            return MaterialStagingStatus::InvalidRecord;
        }
        for (size_t i = 0; i < 4; ++i)
        {
            candidate.BaseColor[i] = record.BaseColor[i];
        }
        for (size_t i = 0; i < 3; ++i)
        {
            candidate.EmissiveColor[i] = record.EmissiveColor[i];
        }
        candidate.EmissiveLuminanceNits = record.EmissiveNits;
        candidate.Metallic = record.Metallic;
        candidate.Roughness = record.Roughness;
        candidate.OcclusionStrength = record.OcclusionStrength;
        candidate.NormalScale = record.NormalScale;
        candidate.AlphaCutoff = record.AlphaCutoff;
        candidate.ArmMask = static_cast<uint8_t>((record.Flags & Asset::CookedMaterialFormatV1::ArmUseMask) >> 3);
        candidate.bDoubleSided = (record.Flags & Asset::CookedMaterialFormatV1::DoubleSided) != 0;
        switch ((record.Flags & Asset::CookedMaterialFormatV1::AlphaModeMask) >>
                Asset::CookedMaterialFormatV1::AlphaModeShift)
        {
        case 0:
            candidate.Alpha = ImportedAlphaMode::Opaque;
            break;
        case 1:
            candidate.Alpha = ImportedAlphaMode::Mask;
            break;
        case 2:
            candidate.Alpha = ImportedAlphaMode::Blend;
            break;
        default:
            return MaterialStagingStatus::InvalidRecord;
        }
        candidate.Layout = ImportedMaterialLayout::PackedArmV1;
        out = std::move(candidate);
        return MaterialStagingStatus::Success;
    }
} // namespace NorvesLib::Core::ResourceIO::ModelStaging
