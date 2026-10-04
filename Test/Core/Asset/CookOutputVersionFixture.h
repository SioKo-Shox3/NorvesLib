#pragma once
#include "Tools/AssetCook/CookOutputPackage.h"
#include "FileStream/Package.h"
#include <cstring>
// 既存の版別goldenをそのまま包み、正常packageでも現cook対象外の版を拒否する。
namespace NorvesLib::Tests::AssetFixtures
{
    inline bool RejectCookOutputVersion(Core::Container::Span<const uint8_t> payload,bool skeletal)
    {
        using namespace Core::Asset;
        using namespace AssetPackageFormatV1;
        Core::Container::VariableArray<uint8_t> bytes(168+payload.size(),0);
        const auto put=[&](size_t offset,uint64_t value,size_t count)
        { for(size_t i=0;i<count;++i)bytes[offset+i]=static_cast<uint8_t>(value>>(8*i)); };
        std::memcpy(bytes.data(),Magic,MagicSize);
        put(HeaderOffset::HeaderSize,HeaderSize,4);put(HeaderOffset::VersionMajor,VersionMajor,2);
        put(HeaderOffset::EndianMarker,EndianMarker,4);put(HeaderOffset::EntryRecordSize,EntryRecordSize,4);
        put(HeaderOffset::PackageSize,bytes.size(),8);put(HeaderOffset::EntryCount,1,4);
        put(HeaderOffset::EntryTableOffset,96,8);put(HeaderOffset::EntryTableSize,64,8);
        put(HeaderOffset::NameTableOffset,160,8);put(HeaderOffset::NameTableSize,1,8);
        put(HeaderOffset::BlobDataOffset,168,8);put(HeaderOffset::Alignment,8,4);
        const auto type=skeletal?MakeAssetPackageFourCC('S','k','l','0'):MakeAssetPackageFourCC('M','s','h','0');
        const auto hash=ComputeAssetPackagePayloadHash(payload.data(),payload.size());
        put(96+EntryOffset::NameOffset,160,8);put(96+EntryOffset::NameSize,1,4);put(96+EntryOffset::Type,type,4);
        put(96+EntryOffset::DataOffset,168,8);put(96+EntryOffset::StoredSize,payload.size(),8);
        put(96+EntryOffset::UncompressedSize,payload.size(),8);put(96+EntryOffset::PayloadHash,hash,8);
        bytes[160]='a';if(!payload.empty())std::memcpy(bytes.data()+168,payload.data(),payload.size());
        FileStream::Package package;
        if(!package.LoadFromMemory(bytes) || package.GetFormat()!=FileStream::PackageFormat::V1 || package.GetEntryCount()!=1)return false;
        AssetCookedReference row;row.Kind=AssetKind::Model;row.EntryName="a";row.EntryType=type;row.CookedHash=hash;
        row.Format=skeletal?"nvskel.v0.skinned.pnujiw.u32":"nvmesh.v0.mesh3d.pnt.u32.clustered";
        row.bHasSkeletalMetadata=skeletal;row.SkeletalMetadata.bHasSubmeshCounts=skeletal;
        Tools::AssetCook::CookOutputPackageFingerprint result{77,88};Core::Container::AnsiString error;
        return !Tools::AssetCook::ValidateCookOutputPackage(row,bytes,result,error) && result.Size==77 && result.ContentHash==88 &&
            error==(skeletal?"cook_output_package: skeletal_payload_mismatch":"cook_output_package: mesh_payload_mismatch");
    }
}
