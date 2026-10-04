#pragma once
#include "Tools/AssetCook/CookOutputPackage.h"
#include "FileStream/Package.h"
#include <cstring>
// 既存の版別goldenをそのまま包み、正常packageでも現cook対象外の版を拒否する。
namespace NorvesLib::Tests::AssetFixtures
{
    inline bool RejectCookOutputVersion(Core::Container::Span<const uint8_t> payload, bool bSkeletal)
    {
        using Core::Asset::AssetCookedReference;
        using Core::Asset::AssetKind;
        using Core::Asset::ComputeAssetPackagePayloadHash;
        using Core::Asset::MakeAssetPackageFourCC;
        namespace Wire = Core::Asset::AssetPackageFormatV1;
        Core::Container::VariableArray<uint8_t> bytes(168 + payload.size(), 0);
        const auto put = [&](size_t offset, uint64_t value, size_t count)
        {
            for (size_t i = 0; i < count; ++i)
            {
                bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
            }
        };
        std::memcpy(bytes.data(), Wire::Magic, Wire::MagicSize);
        put(Wire::HeaderOffset::HeaderSize, Wire::HeaderSize, 4);
        put(Wire::HeaderOffset::VersionMajor, Wire::VersionMajor, 2);
        put(Wire::HeaderOffset::EndianMarker, Wire::EndianMarker, 4);
        put(Wire::HeaderOffset::EntryRecordSize, Wire::EntryRecordSize, 4);
        put(Wire::HeaderOffset::PackageSize, bytes.size(), 8);
        put(Wire::HeaderOffset::EntryCount, 1, 4);
        put(Wire::HeaderOffset::EntryTableOffset, 96, 8);
        put(Wire::HeaderOffset::EntryTableSize, 64, 8);
        put(Wire::HeaderOffset::NameTableOffset, 160, 8);
        put(Wire::HeaderOffset::NameTableSize, 1, 8);
        put(Wire::HeaderOffset::BlobDataOffset, 168, 8);
        put(Wire::HeaderOffset::Alignment, 8, 4);
        const auto type =
            bSkeletal ? MakeAssetPackageFourCC('S', 'k', 'l', '0') : MakeAssetPackageFourCC('M', 's', 'h', '0');
        const auto hash = ComputeAssetPackagePayloadHash(payload.data(), payload.size());
        put(96 + Wire::EntryOffset::NameOffset, 160, 8);
        put(96 + Wire::EntryOffset::NameSize, 1, 4);
        put(96 + Wire::EntryOffset::Type, type, 4);
        put(96 + Wire::EntryOffset::DataOffset, 168, 8);
        put(96 + Wire::EntryOffset::StoredSize, payload.size(), 8);
        put(96 + Wire::EntryOffset::UncompressedSize, payload.size(), 8);
        put(96 + Wire::EntryOffset::PayloadHash, hash, 8);
        bytes[160] = 'a';
        if (!payload.empty())
        {
            std::memcpy(bytes.data() + 168, payload.data(), payload.size());
        }
        FileStream::Package package;
        if (!package.LoadFromMemory(bytes) || package.GetFormat() != FileStream::PackageFormat::V1 ||
            package.GetEntryCount() != 1)
        {
            return false;
        }
        AssetCookedReference row;
        row.Kind = AssetKind::Model;
        row.EntryName = "a";
        row.EntryType = type;
        row.CookedHash = hash;
        row.Format = bSkeletal ? "nvskel.v0.skinned.pnujiw.u32" : "nvmesh.v0.mesh3d.pnt.u32.clustered";
        row.bHasSkeletalMetadata = bSkeletal;
        row.SkeletalMetadata.bHasSubmeshCounts = bSkeletal;
        Tools::AssetCook::CookOutputPackageFingerprint result{77, 88};
        Core::Container::AnsiString error;
        return !Tools::AssetCook::ValidateCookOutputPackage(row, bytes, result, error) && result.Size == 77 &&
               result.ContentHash == 88 &&
               error == (bSkeletal ? "cook_output_package: skeletal_payload_mismatch"
                                   : "cook_output_package: mesh_payload_mismatch");
    }
} // namespace NorvesLib::Tests::AssetFixtures
