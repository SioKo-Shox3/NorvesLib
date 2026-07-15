#pragma once

#include "Asset/AssetManifest.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/CookedMeshFormat.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace NorvesLib::Test::CookedModelSupport
{
    namespace MeshFormat = Core::Asset::CookedMeshFormatV0;
    namespace PackageFormat = Core::Asset::AssetPackageFormatV1;

    using ByteArray = std::vector<uint8_t>;

    inline size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    inline void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    inline void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    inline void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    inline void WriteFloat(ByteArray& bytes, size_t offset, float value)
    {
        WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
    }

    inline void WriteVertex(ByteArray& bytes, size_t offset, float x, float y, float z, float u, float v)
    {
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::PositionX, x);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::PositionY, y);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::PositionZ, z);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::NormalX, 0.0f);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::NormalY, 0.0f);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::NormalZ, 1.0f);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::TexCoordU, u);
        WriteFloat(bytes, offset + MeshFormat::VertexRecordOffset::TexCoordV, v);
    }

    inline ByteArray BuildCookedModelMesh()
    {
        const size_t submeshOffset = MeshFormat::HeaderSize;
        const size_t materialOffset = AlignUp(
            submeshOffset + MeshFormat::SubmeshRecordSize,
            MeshFormat::SectionAlignment);
        const size_t clusterOffset = AlignUp(
            materialOffset + MeshFormat::MaterialRecordSize,
            MeshFormat::SectionAlignment);
        const size_t stringOffset = AlignUp(
            clusterOffset + MeshFormat::ClusterRecordSize,
            MeshFormat::SectionAlignment);
        const size_t vertexOffset = stringOffset;
        const size_t indexOffset = AlignUp(
            vertexOffset + 3 * MeshFormat::VertexRecordSize,
            MeshFormat::SectionAlignment);
        const size_t fileSize = indexOffset + 3 * sizeof(uint32_t);

        ByteArray bytes(fileSize, 0);
        std::memcpy(
            bytes.data() + MeshFormat::HeaderOffset::Magic,
            MeshFormat::Magic,
            MeshFormat::MagicSize);
        WriteLe32(bytes, MeshFormat::HeaderOffset::HeaderSize, static_cast<uint32_t>(MeshFormat::HeaderSize));
        WriteLe16(bytes, MeshFormat::HeaderOffset::VersionMajor, MeshFormat::VersionMajor);
        WriteLe16(bytes, MeshFormat::HeaderOffset::VersionMinor, MeshFormat::VersionMinor);
        WriteLe32(bytes, MeshFormat::HeaderOffset::EndianMarker, MeshFormat::EndianMarker);
        WriteLe32(
            bytes,
            MeshFormat::HeaderOffset::VertexRecordSize,
            static_cast<uint32_t>(MeshFormat::VertexRecordSize));
        WriteLe32(
            bytes,
            MeshFormat::HeaderOffset::SubmeshRecordSize,
            static_cast<uint32_t>(MeshFormat::SubmeshRecordSize));
        WriteLe32(
            bytes,
            MeshFormat::HeaderOffset::MaterialRecordSize,
            static_cast<uint32_t>(MeshFormat::MaterialRecordSize));
        WriteLe32(
            bytes,
            MeshFormat::HeaderOffset::ClusterRecordSize,
            static_cast<uint32_t>(MeshFormat::ClusterRecordSize));
        WriteLe32(
            bytes,
            MeshFormat::HeaderOffset::StringRefRecordSize,
            static_cast<uint32_t>(MeshFormat::StringRefRecordSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::SubmeshTableOffset, static_cast<uint64_t>(submeshOffset));
        WriteLe64(
            bytes,
            MeshFormat::HeaderOffset::SubmeshTableSize,
            static_cast<uint64_t>(MeshFormat::SubmeshRecordSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::MaterialTableOffset, static_cast<uint64_t>(materialOffset));
        WriteLe64(
            bytes,
            MeshFormat::HeaderOffset::MaterialTableSize,
            static_cast<uint64_t>(MeshFormat::MaterialRecordSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::ClusterTableOffset, static_cast<uint64_t>(clusterOffset));
        WriteLe64(
            bytes,
            MeshFormat::HeaderOffset::ClusterTableSize,
            static_cast<uint64_t>(MeshFormat::ClusterRecordSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::StringTableOffset, static_cast<uint64_t>(stringOffset));
        WriteLe64(bytes, MeshFormat::HeaderOffset::StringTableSize, 0);
        WriteLe64(bytes, MeshFormat::HeaderOffset::VertexPayloadOffset, static_cast<uint64_t>(vertexOffset));
        WriteLe64(
            bytes,
            MeshFormat::HeaderOffset::VertexPayloadSize,
            static_cast<uint64_t>(3 * MeshFormat::VertexRecordSize));
        WriteLe64(bytes, MeshFormat::HeaderOffset::IndexPayloadOffset, static_cast<uint64_t>(indexOffset));
        WriteLe64(
            bytes,
            MeshFormat::HeaderOffset::IndexPayloadSize,
            static_cast<uint64_t>(3 * sizeof(uint32_t)));
        WriteLe32(bytes, MeshFormat::HeaderOffset::VertexCount, 3);
        WriteLe32(bytes, MeshFormat::HeaderOffset::IndexCount, 3);
        WriteLe32(bytes, MeshFormat::HeaderOffset::SubmeshCount, 1);
        WriteLe32(bytes, MeshFormat::HeaderOffset::MaterialCount, 1);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterCount, 1);
        WriteLe32(bytes, MeshFormat::HeaderOffset::StringByteCount, 0);
        WriteFloat(bytes, MeshFormat::HeaderOffset::TotalBoundsCenterX, 0.5f);
        WriteFloat(bytes, MeshFormat::HeaderOffset::TotalBoundsCenterY, 0.5f);
        WriteFloat(bytes, MeshFormat::HeaderOffset::TotalBoundsCenterZ, 0.0f);
        WriteFloat(bytes, MeshFormat::HeaderOffset::TotalBoundsRadius, 1.0f);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterAlgorithmId, MeshFormat::ClusterAlgorithmId);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterAlgorithmVersion, MeshFormat::ClusterAlgorithmVersion);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterMaxTriangles, MeshFormat::ClusterMaxTriangles);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterMaxVertices, MeshFormat::ClusterMaxVertices);
        WriteLe32(bytes, MeshFormat::HeaderOffset::ClusterSettingsFlags, MeshFormat::ClusterSettingsFlags);

        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::IndexOffset, 0);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::IndexCount, 3);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::VertexCount, 3);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::ClusterOffset, 0);
        WriteLe32(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::ClusterCount, 1);
        WriteFloat(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::BoundsCenterX, 0.5f);
        WriteFloat(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::BoundsCenterY, 0.5f);
        WriteFloat(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::BoundsCenterZ, 0.0f);
        WriteFloat(bytes, submeshOffset + MeshFormat::SubmeshRecordOffset::BoundsRadius, 1.0f);

        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::BoundsCenterX, 0.5f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::BoundsCenterY, 0.5f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::BoundsCenterZ, 0.0f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::BoundsRadius, 1.0f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ConeAxisX, 0.0f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ConeAxisY, 0.0f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ConeAxisZ, 1.0f);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ConeCutoff, 0.5f);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::IndexOffset, 0);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::IndexCount, 3);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::VertexCount, 3);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::LODLevel, 0);
        WriteFloat(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::LODError, 0.0f);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ParentStart, 0);
        WriteLe32(bytes, clusterOffset + MeshFormat::ClusterRecordOffset::ParentCount, 0);

        WriteVertex(bytes, vertexOffset + 0 * MeshFormat::VertexRecordSize, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        WriteVertex(bytes, vertexOffset + 1 * MeshFormat::VertexRecordSize, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        WriteVertex(bytes, vertexOffset + 2 * MeshFormat::VertexRecordSize, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        WriteLe32(bytes, indexOffset + 0 * sizeof(uint32_t), 0);
        WriteLe32(bytes, indexOffset + 1 * sizeof(uint32_t), 1);
        WriteLe32(bytes, indexOffset + 2 * sizeof(uint32_t), 2);

        WriteLe64(bytes,
                  MeshFormat::HeaderOffset::PayloadHash,
                  Core::Asset::ComputeCookedMeshPayloadHash(bytes.data() + submeshOffset, fileSize - submeshOffset));
        return bytes;
    }

    inline ByteArray BuildModelPackage(const ByteArray& payload,
                                       const std::string& entryName = "Models/Triangle.nvmesh")
    {
        const size_t entryTableOffset = PackageFormat::HeaderSize;
        const size_t entryTableSize = PackageFormat::EntryRecordSize;
        const size_t nameTableOffset = AlignUp(
            entryTableOffset + entryTableSize,
            PackageFormat::MinimumAlignment);
        const size_t blobDataOffset = AlignUp(
            nameTableOffset + entryName.size(),
            PackageFormat::MinimumAlignment);
        const size_t packageSize = blobDataOffset + payload.size();

        ByteArray bytes(packageSize, 0);
        std::memcpy(
            bytes.data() + PackageFormat::HeaderOffset::Magic,
            PackageFormat::Magic,
            PackageFormat::MagicSize);
        WriteLe32(
            bytes,
            PackageFormat::HeaderOffset::HeaderSize,
            static_cast<uint32_t>(PackageFormat::HeaderSize));
        WriteLe16(bytes, PackageFormat::HeaderOffset::VersionMajor, PackageFormat::VersionMajor);
        WriteLe16(bytes, PackageFormat::HeaderOffset::VersionMinor, PackageFormat::VersionMinor);
        WriteLe32(bytes, PackageFormat::HeaderOffset::EndianMarker, PackageFormat::EndianMarker);
        WriteLe32(
            bytes,
            PackageFormat::HeaderOffset::EntryRecordSize,
            static_cast<uint32_t>(PackageFormat::EntryRecordSize));
        WriteLe64(bytes, PackageFormat::HeaderOffset::PackageSize, static_cast<uint64_t>(packageSize));
        WriteLe32(bytes, PackageFormat::HeaderOffset::EntryCount, 1);
        WriteLe64(
            bytes,
            PackageFormat::HeaderOffset::EntryTableOffset,
            static_cast<uint64_t>(entryTableOffset));
        WriteLe64(
            bytes,
            PackageFormat::HeaderOffset::EntryTableSize,
            static_cast<uint64_t>(entryTableSize));
        WriteLe64(
            bytes,
            PackageFormat::HeaderOffset::NameTableOffset,
            static_cast<uint64_t>(nameTableOffset));
        WriteLe64(
            bytes,
            PackageFormat::HeaderOffset::NameTableSize,
            static_cast<uint64_t>(entryName.size()));
        WriteLe64(
            bytes,
            PackageFormat::HeaderOffset::BlobDataOffset,
            static_cast<uint64_t>(blobDataOffset));
        WriteLe32(
            bytes,
            PackageFormat::HeaderOffset::Alignment,
            static_cast<uint32_t>(PackageFormat::MinimumAlignment));

        std::memcpy(bytes.data() + nameTableOffset, entryName.data(), entryName.size());
        if (!payload.empty())
        {
            std::memcpy(bytes.data() + blobDataOffset, payload.data(), payload.size());
        }

        const Core::Asset::AssetPackageFourCC entryType =
            Core::Asset::MakeAssetPackageFourCC('M', 's', 'h', '0');
        WriteLe64(
            bytes,
            entryTableOffset + PackageFormat::EntryOffset::NameOffset,
            static_cast<uint64_t>(nameTableOffset));
        WriteLe32(
            bytes,
            entryTableOffset + PackageFormat::EntryOffset::NameSize,
            static_cast<uint32_t>(entryName.size()));
        WriteLe32(bytes, entryTableOffset + PackageFormat::EntryOffset::Type, entryType);
        WriteLe32(bytes,
                  entryTableOffset + PackageFormat::EntryOffset::Compression,
                  static_cast<uint32_t>(Core::Asset::AssetPackageCompression::None));
        WriteLe64(
            bytes,
            entryTableOffset + PackageFormat::EntryOffset::DataOffset,
            static_cast<uint64_t>(blobDataOffset));
        WriteLe64(
            bytes,
            entryTableOffset + PackageFormat::EntryOffset::StoredSize,
            static_cast<uint64_t>(payload.size()));
        WriteLe64(
            bytes,
            entryTableOffset + PackageFormat::EntryOffset::UncompressedSize,
            static_cast<uint64_t>(payload.size()));
        WriteLe64(bytes,
                  entryTableOffset + PackageFormat::EntryOffset::PayloadHash,
                  Core::Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size()));
        return bytes;
    }

    inline Core::Container::String ToCoreString(const std::string& text)
    {
#if defined(UNICODE)
        std::wstring wide;
        wide.reserve(text.size());
        for (const char character : text)
        {
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return Core::Container::String(wide.c_str());
#else
        return Core::Container::String(text.c_str());
#endif
    }

    inline Core::Container::String BuildModelManifest(
        uint64_t cookedHash,
        const std::string& logicalPath = "Models/Triangle.nvmesh",
        const std::string& cookedPackage = "Cooked/Models.nvpkg",
        const std::string& entryName = "Models/Triangle.nvmesh")
    {
        const Core::Container::AnsiString cookedHashText = Core::Asset::FormatAssetHashHex(cookedHash);
        const std::string hash(cookedHashText.data(), cookedHashText.size());
        const std::string json =
            "{\"version\":1,\"assets\":[{"
            "\"logical_path\":\"" + logicalPath + "\","
            "\"kind\":\"model\","
            "\"source_hash\":\"0000000000000001\","
            "\"variant\":\"default\","
            "\"format\":\"nvmesh.v0\","
            "\"cooked_package\":\"" + cookedPackage + "\","
            "\"entry_name\":\"" + entryName + "\","
            "\"entry_type\":\"Msh0\","
            "\"cooked_hash\":\"" + hash + "\","
            "\"cooked_version\":0}]}";
        return ToCoreString(json);
    }

    inline void WriteBinaryFile(const std::filesystem::path& path, const ByteArray& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
} // namespace NorvesLib::Test::CookedModelSupport
