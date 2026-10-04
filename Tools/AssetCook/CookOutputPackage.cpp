#include "CookOutputPackage.h"
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
        bool Equal(AnsiStringView a,const char* b)
        { return a.size()==std::strlen(b) && (a.empty() || std::memcmp(a.data(),b,a.size())==0); }
        bool Fail(AnsiString& error,const char* code)
        { error="cook_output_package: ";error.append(code);return false; }
        bool Profile(const AssetCookedReference& row,const AssetBlob& payload,AnsiString& error)
        {
            const auto format=AnsiStringView(row.Format);
            if (row.Kind==AssetKind::Raw)
            {
                // rawは任意の有効FourCCを保持する。型名だけで他kindのparserへ送らない。
                const auto text=FormatAssetPackageFourCCText(row.EntryType);
                AssetPackageFourCC parsed=0;
                return (Equal(format,"raw.v0") && TryParseAssetPackageFourCCText(text,parsed) && parsed==row.EntryType) ||
                    Fail(error,"raw_profile_mismatch");
            }
            if (row.Kind==AssetKind::Texture)
            {
                CookedTexturePixelFormat pixel=CookedTexturePixelFormat::RGBA8UNorm;
                CookedTextureColorSpace color=CookedTextureColorSpace::Linear;
                if (Equal(format,"nvtex.v0.rgba8.srgb")) color=CookedTextureColorSpace::SRGB;
                else if (Equal(format,"nvtex.v0.rgba8.linear")) {}
                else if (Equal(format,"nvtex.v0.rg8.linear")) pixel=CookedTexturePixelFormat::RG8UNorm;
                else if (Equal(format,"nvtex.v0.r8.linear")) pixel=CookedTexturePixelFormat::R8UNorm;
                else return Fail(error,"texture_format_unsupported");
                if (row.EntryType!=MakeAssetPackageFourCC('T','e','x','0')) return Fail(error,"texture_type_mismatch");
                const auto parsed=ParseCookedTexture(payload);
                return (parsed.Succeeded() && parsed.Texture.PixelFormat==pixel && parsed.Texture.ColorSpace==color) ||
                    Fail(error,"texture_payload_mismatch");
            }
            if (row.Kind==AssetKind::Audio)
            {
                if (!Equal(format,"nvaud.v0.pcm16") || row.EntryType!=CookedAudioFormatV0::EntryType)
                    return Fail(error,"audio_profile_mismatch");
                const auto parsed=ParseCookedAudio(payload);
                return (parsed.Succeeded() && parsed.Audio.BitsPerSample==16) || Fail(error,"audio_payload_mismatch");
            }
            if (row.Kind==AssetKind::Model && Equal(format,"nvmesh.v0.mesh3d.pnt.u32.clustered"))
            {
                if (row.EntryType!=MakeAssetPackageFourCC('M','s','h','0')) return Fail(error,"mesh_type_mismatch");
                const auto parsed=ParseCookedMesh(payload);
                return (parsed.Succeeded() && parsed.Mesh.VersionMajor==0) || Fail(error,"mesh_payload_mismatch");
            }
            if (row.Kind==AssetKind::Model && Equal(format,"nvskel.v0.skinned.pnujiw.u32"))
            {
                if (row.EntryType!=CookedSkeletalFormatV0::EntryType || !row.bHasSkeletalMetadata ||
                    !row.SkeletalMetadata.bHasSubmeshCounts) return Fail(error,"skeletal_profile_mismatch");
                const auto parsed=ParseCookedSkeletal(payload);
                if (!parsed.Succeeded() || parsed.Data.VersionMinor!=CookedSkeletalFormatV02::VersionMinor)
                    return Fail(error,"skeletal_payload_mismatch");
                const auto& data=parsed.Data.Skeletal;const auto& counts=row.SkeletalMetadata;
                return (counts.VertexCount==data.Vertices.size() && counts.IndexCount==data.Indices.size() &&
                    counts.JointCount==data.Joints.size() && counts.ClipCount==data.Clips.size() &&
                    counts.SubmeshCount==data.SubMeshes.size() && counts.MaterialSlotCount==data.MaterialSlots.size()) ||
                    Fail(error,"skeletal_metadata_mismatch");
            }
            return Fail(error,"profile_unsupported");
        }
    }
    bool ValidateCookOutputPackage(const Core::Asset::AssetCookedReference& expected,
        Core::Container::Span<const uint8_t> bytes,CookOutputPackageFingerprint& out,AnsiString& error)
    {
        error.clear();
        try
        {
            if (expected.CookedVersion!=0 || expected.EntryName.empty() || bytes.empty() || bytes.data()==nullptr)
                return Fail(error,"invalid_reference_or_bytes");
            const bool skeletal=expected.Kind==AssetKind::Model && Equal(expected.Format,"nvskel.v0.skinned.pnujiw.u32");
            if (!skeletal && expected.bHasSkeletalMetadata) return Fail(error,"unexpected_skeletal_metadata");
            FileStream::Package package;FileStream::PackageEntry entry;
            if (!package.LoadFromMemory(bytes) || package.GetFormat()!=FileStream::PackageFormat::V1 ||
                package.GetEntryCount()!=1) return Fail(error,"package_shape_mismatch");
            if (!package.FindEntry(expected.EntryName,expected.EntryType,entry) || entry.PayloadHash!=expected.CookedHash)
                return Fail(error,"entry_mismatch");
            const auto payload=package.OpenEntry(entry);
            if (!payload.IsValid() || ComputeAssetPackagePayloadHash(payload.GetData(),payload.GetSize())!=expected.CookedHash)
                return Fail(error,"payload_hash_mismatch");
            if (!Profile(expected,payload,error)) return false;
            out={static_cast<uint64_t>(bytes.size()),ComputeAssetPackagePayloadHash(bytes.data(),bytes.size())};
            return true;
        }
        catch (const std::exception&) { return Fail(error,"validation_exception"); }
    }
}
