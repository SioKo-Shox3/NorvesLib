#include "CookOutputPackage.h"
#include "CookRigPayload.h"
#include "CookReferenceValues.h"
#include "Asset/CookedAudioFormat.h"
#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "FileStream/Package.h"
#include <cstring>
#include <exception>
namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        using namespace Core::Asset;
        using Core::Container::AnsiString;
        using Core::Container::AnsiStringView;
        bool Equal(AnsiStringView a, const char* b)
        {
            return a.size() == std::strlen(b) && (a.empty() || std::memcmp(a.data(), b, a.size()) == 0);
        }
        bool Fail(AnsiString& error, const char* code)
        {
            error = "cook_output_package: ";
            error.append(code);
            return false;
        }
        bool Profile(const AssetCookedReference& row, const AssetBlob& payload, CookAssetMetrics& metrics,
                     AnsiString& error)
        {
            const auto format = AnsiStringView(row.Format);
            if (row.bHasRigSplitMetadata)
            {
                AssetRigSplitMetadata actual;
                const AssetKind kinds[] = {AssetKind::Unknown, AssetKind::Skeleton, AssetKind::Model,
                                           AssetKind::Animation};
                const AssetPackageFourCC types[] = {0, MakeAssetPackageFourCC('S', 'k', 'e', '1'),
                                                    MakeAssetPackageFourCC('S', 'k', 'm', '1'),
                                                    MakeAssetPackageFourCC('A', 'n', 'm', '1')};
                if (!Detail::InspectCookedRigPayload(format, {payload.GetData(), payload.GetSize()},
                                                     row.RigSplitMetadata.Profile, actual, error) ||
                    actual.Role < 1 || actual.Role > 3 || row.Kind != kinds[actual.Role] ||
                    row.EntryType != types[actual.Role] ||
                    !Detail::CookReferenceValues::SameRigMetadata(row.RigSplitMetadata, actual))
                {
                    return Fail(error, "rig_metadata_mismatch");
                }
                metrics.Joints = actual.JointCount;
                metrics.Triangles = actual.IndexCount / 3;
                return true;
            }
            if (row.Kind == AssetKind::Raw)
            {
                // rawは任意の有効FourCCを保持する。型名だけで他kindのparserへ送らない。
                const auto text = FormatAssetPackageFourCCText(row.EntryType);
                AssetPackageFourCC parsed = 0;
                return (Equal(format, "raw.v0") && TryParseAssetPackageFourCCText(text, parsed) &&
                        parsed == row.EntryType) ||
                       Fail(error, "raw_profile_mismatch");
            }
            if (row.Kind == AssetKind::Texture)
            {
                CookedTexturePixelFormat pixel = CookedTexturePixelFormat::RGBA8UNorm;
                CookedTextureColorSpace color = CookedTextureColorSpace::Linear;
                if (Equal(format, "nvtex.v0.rgba8.srgb"))
                {
                    color = CookedTextureColorSpace::SRGB;
                }
                else if (Equal(format, "nvtex.v0.rgba8.linear"))
                {
                }
                else if (Equal(format, "nvtex.v0.rg8.linear"))
                {
                    pixel = CookedTexturePixelFormat::RG8UNorm;
                }
                else if (Equal(format, "nvtex.v0.r8.linear"))
                {
                    pixel = CookedTexturePixelFormat::R8UNorm;
                }
                else
                {
                    return Fail(error, "texture_format_unsupported");
                }
                if (row.EntryType != MakeAssetPackageFourCC('T', 'e', 'x', '0'))
                {
                    return Fail(error, "texture_type_mismatch");
                }
                const auto parsed = ParseCookedTexture(payload);
                if (parsed.Succeeded())
                {
                    for (const auto& mip : parsed.Texture.Mips)
                    {
                        metrics.TextureBytes += mip.DataSize;
                    }
                }
                return (parsed.Succeeded() && parsed.Texture.PixelFormat == pixel &&
                        parsed.Texture.ColorSpace == color) ||
                       Fail(error, "texture_payload_mismatch");
            }
            if (row.Kind == AssetKind::Audio)
            {
                if (!Equal(format, "nvaud.v0.pcm16") || row.EntryType != CookedAudioFormatV0::EntryType)
                {
                    return Fail(error, "audio_profile_mismatch");
                }
                const auto parsed = ParseCookedAudio(payload);
                return (parsed.Succeeded() && parsed.Audio.BitsPerSample == 16) ||
                       Fail(error, "audio_payload_mismatch");
            }
            if ((row.Kind == AssetKind::Model && (Equal(format, "nvmesh.v0.mesh3d.pnt.u32.clustered") ||
                                                  Equal(format, "nvmesh.v1.mesh3d.pnt.u32.clustered"))))
            {
                if (row.EntryType != MakeAssetPackageFourCC('M', 's', 'h', '0'))
                {
                    return Fail(error, "mesh_type_mismatch");
                }
                const auto parsed = ParseCookedMesh(payload);
                if (parsed.Succeeded())
                {
                    metrics.Triangles = parsed.Mesh.Indices.size() / 3;
                }
                return (parsed.Succeeded() &&
                        parsed.Mesh.VersionMajor == (Equal(format, "nvmesh.v1.mesh3d.pnt.u32.clustered") ? 1 : 0)) ||
                       Fail(error, "mesh_payload_mismatch");
            }
            if (row.Kind == AssetKind::Model && Equal(format, "nvskel.v0.skinned.pnujiw.u32"))
            {
                if (row.EntryType != CookedSkeletalFormatV0::EntryType || !row.bHasSkeletalMetadata ||
                    !row.SkeletalMetadata.bHasSubmeshCounts)
                {
                    return Fail(error, "skeletal_profile_mismatch");
                }
                const auto parsed = ParseCookedSkeletal(payload);
                if (!parsed.Succeeded() || parsed.Data.VersionMinor != CookedSkeletalFormatV02::VersionMinor)
                {
                    return Fail(error, "skeletal_payload_mismatch");
                }
                const auto& data = parsed.Data.Skeletal;
                metrics.Triangles = data.Indices.size() / 3;
                metrics.Joints = data.Joints.size();
                const auto& counts = row.SkeletalMetadata;
                return (counts.VertexCount == data.Vertices.size() && counts.IndexCount == data.Indices.size() &&
                        counts.JointCount == data.Joints.size() && counts.ClipCount == data.Clips.size() &&
                        counts.SubmeshCount == data.SubMeshes.size() &&
                        counts.MaterialSlotCount == data.MaterialSlots.size()) ||
                       Fail(error, "skeletal_metadata_mismatch");
            }
            return Fail(error, "profile_unsupported");
        }
    } // namespace
    bool ValidateCookOutputPackage(const Core::Asset::AssetCookedReference& expected,
                                   Core::Container::Span<const uint8_t> bytes, CookOutputPackageFingerprint& out,
                                   AnsiString& error)
    {
        error.clear();
        try
        {
            const uint32_t version =
                (expected.Kind == AssetKind::Model && Equal(expected.Format, "nvmesh.v1.mesh3d.pnt.u32.clustered")) ||
                        Equal(expected.Format, "nvskel.v1.skeleton") ||
                        Equal(expected.Format, "nvskel.v1.skinmesh.pnujiw.u32") ||
                        Equal(expected.Format, "nvskel.v1.clips")
                    ? 1u
                    : 0u;
            if (expected.CookedVersion != version || expected.EntryName.empty() || bytes.empty() ||
                bytes.data() == nullptr)
            {
                return Fail(error, "invalid_reference_or_bytes");
            }
            const bool bSkeletal =
                expected.Kind == AssetKind::Model && Equal(expected.Format, "nvskel.v0.skinned.pnujiw.u32");
            if (!bSkeletal && expected.bHasSkeletalMetadata)
            {
                return Fail(error, "unexpected_skeletal_metadata");
            }
            FileStream::Package package;
            FileStream::PackageEntry entry;
            if (!package.LoadFromMemory(bytes) || package.GetFormat() != FileStream::PackageFormat::V1 ||
                package.GetEntryCount() != 1)
            {
                return Fail(error, "package_shape_mismatch");
            }
            if (!package.FindEntry(expected.EntryName, expected.EntryType, entry) ||
                entry.PayloadHash != expected.CookedHash)
            {
                return Fail(error, "entry_mismatch");
            }
            const auto payload = package.OpenEntry(entry);
            if (!payload.IsValid() ||
                ComputeAssetPackagePayloadHash(payload.GetData(), payload.GetSize()) != expected.CookedHash)
            {
                return Fail(error, "payload_hash_mismatch");
            }
            CookAssetMetrics metrics;
            metrics.CookedBytes = bytes.size();
            if (!Profile(expected, payload, metrics, error))
            {
                return false;
            }
            out = {static_cast<uint64_t>(bytes.size()), ComputeAssetPackagePayloadHash(bytes.data(), bytes.size()),
                   metrics};
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "validation_exception");
        }
    }
} // namespace NorvesLib::Tools::AssetCook
