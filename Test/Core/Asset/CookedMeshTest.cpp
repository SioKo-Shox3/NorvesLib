#include "Asset/CookedMeshFormat.h"
#include "Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";         \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Asset::CookedMeshFormatV0;
using NorvesLib::Core::Container::AnsiStringView;
using NorvesLib::Core::Container::Span;
using NorvesLib::Core::Container::VariableArray;

namespace
{
    using ByteArray = VariableArray<uint8_t>;

    static_assert(HeaderOffset::Reserved4 + sizeof(uint64_t) == HeaderSize);
    static_assert(VertexRecordOffset::TexCoordV + sizeof(float) == VertexRecordSize);
    static_assert(SubmeshRecordOffset::Reserved1 + sizeof(uint64_t) == SubmeshRecordSize);
    static_assert(MaterialRecordOffset::Reserved1 + sizeof(uint64_t) == MaterialRecordSize);
    static_assert(ClusterRecordOffset::Reserved0 + sizeof(uint64_t) == ClusterRecordSize);
    static_assert(StringRefRecordOffset::Reserved0 + sizeof(uint32_t) == StringRefRecordSize);

    size_t AlignUp(size_t value, size_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    void WriteLe16(ByteArray& bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(ByteArray& bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(ByteArray& bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    void WriteFloat(ByteArray& bytes, size_t offset, float value)
    {
        WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
    }

    uint32_t ReadLe32(const ByteArray& bytes, size_t offset)
    {
        return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    uint64_t ReadLe64(const ByteArray& bytes, size_t offset)
    {
        return static_cast<uint64_t>(ReadLe32(bytes, offset)) |
               (static_cast<uint64_t>(ReadLe32(bytes, offset + 4)) << 32);
    }

    AssetBlob MakeBlob(const ByteArray& bytes)
    {
        return AssetBlob::CopyBytes(Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvmesh");
    }

    void WriteVertex(ByteArray& bytes, size_t recordOffset, float x, float y, float z, float u, float v)
    {
        WriteFloat(bytes, recordOffset + VertexRecordOffset::PositionX, x);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::PositionY, y);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::PositionZ, z);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::NormalX, 0.0f);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::NormalY, 0.0f);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::NormalZ, 1.0f);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::TexCoordU, u);
        WriteFloat(bytes, recordOffset + VertexRecordOffset::TexCoordV, v);
    }

    void WriteCluster(ByteArray& bytes, size_t recordOffset, uint32_t indexOffset, uint32_t indexCount)
    {
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::BoundsCenterX, 0.5f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::BoundsCenterY, 0.5f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::BoundsCenterZ, 0.0f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::BoundsRadius, 1.0f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::ConeAxisX, 0.0f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::ConeAxisY, 0.0f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::ConeAxisZ, 1.0f);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::ConeCutoff, 0.5f);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::IndexOffset, indexOffset);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::IndexCount, indexCount);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::VertexCount, 0);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::LODLevel, 0);
        WriteFloat(bytes, recordOffset + ClusterRecordOffset::LODError, 0.0f);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::ParentStart, 0);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::ParentCount, 0);
        WriteLe32(bytes, recordOffset + ClusterRecordOffset::Flags, 0);
        WriteLe64(bytes, recordOffset + ClusterRecordOffset::Reserved0, 0);
    }

    namespace GoldenWire
    {
        constexpr size_t HeaderSize = 256;
        constexpr size_t SubmeshOffset = 256;
        constexpr size_t MaterialOffset = 320;
        constexpr size_t ClusterOffset = 384;
        constexpr size_t StringOffset = 464;
        constexpr size_t VertexOffset = 464;
        constexpr size_t IndexOffset = 560;
        constexpr size_t FileSize = 572;
        constexpr uint64_t PayloadHash = 0x28de1112fb93d08aull;
        constexpr uint8_t Magic[8] = {'N', 'V', 'M', 'E', 'S', 'H', 'v', '0'};
    } // namespace GoldenWire

    void WriteGoldenVertex(ByteArray& bytes, size_t recordOffset, float x, float y, float z, float u, float v)
    {
        WriteFloat(bytes, recordOffset + 0, x);
        WriteFloat(bytes, recordOffset + 4, y);
        WriteFloat(bytes, recordOffset + 8, z);
        WriteFloat(bytes, recordOffset + 12, 0.0f);
        WriteFloat(bytes, recordOffset + 16, 0.0f);
        WriteFloat(bytes, recordOffset + 20, 1.0f);
        WriteFloat(bytes, recordOffset + 24, u);
        WriteFloat(bytes, recordOffset + 28, v);
    }

    ByteArray BuildGoldenMesh()
    {
        ByteArray bytes(GoldenWire::FileSize, 0);
        std::memcpy(bytes.data(), GoldenWire::Magic, sizeof(GoldenWire::Magic));

        WriteLe32(bytes, 8, static_cast<uint32_t>(GoldenWire::HeaderSize));
        WriteLe16(bytes, 12, 0);
        WriteLe16(bytes, 14, 0);
        WriteLe32(bytes, 16, 0x01020304u);
        WriteLe32(bytes, 20, 32);
        WriteLe32(bytes, 24, 64);
        WriteLe32(bytes, 28, 64);
        WriteLe32(bytes, 32, 80);
        WriteLe32(bytes, 36, 16);
        WriteLe64(bytes, 40, GoldenWire::FileSize);
        WriteLe64(bytes, 48, GoldenWire::SubmeshOffset);
        WriteLe64(bytes, 56, 64);
        WriteLe64(bytes, 64, GoldenWire::MaterialOffset);
        WriteLe64(bytes, 72, 64);
        WriteLe64(bytes, 80, GoldenWire::ClusterOffset);
        WriteLe64(bytes, 88, 80);
        WriteLe64(bytes, 96, GoldenWire::StringOffset);
        WriteLe64(bytes, 104, 0);
        WriteLe64(bytes, 112, GoldenWire::VertexOffset);
        WriteLe64(bytes, 120, 96);
        WriteLe64(bytes, 128, GoldenWire::IndexOffset);
        WriteLe64(bytes, 136, 12);
        WriteLe64(bytes, 144, GoldenWire::PayloadHash);
        WriteLe32(bytes, 152, 3);
        WriteLe32(bytes, 156, 3);
        WriteLe32(bytes, 160, 1);
        WriteLe32(bytes, 164, 1);
        WriteLe32(bytes, 168, 1);
        WriteLe32(bytes, 172, 0);
        WriteFloat(bytes, 176, 0.5f);
        WriteFloat(bytes, 180, 0.5f);
        WriteFloat(bytes, 184, 0.0f);
        WriteFloat(bytes, 188, 1.0f);
        WriteLe32(bytes, 192, 1);
        WriteLe32(bytes, 196, 0);
        WriteLe32(bytes, 200, 128);
        WriteLe32(bytes, 204, 128);
        WriteLe32(bytes, 208, 0);

        WriteLe32(bytes, GoldenWire::SubmeshOffset + 0, 0);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 4, 3);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 8, 0);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 12, 3);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 16, 0);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 20, 0);
        WriteLe32(bytes, GoldenWire::SubmeshOffset + 24, 1);
        WriteFloat(bytes, GoldenWire::SubmeshOffset + 32, 0.5f);
        WriteFloat(bytes, GoldenWire::SubmeshOffset + 36, 0.5f);
        WriteFloat(bytes, GoldenWire::SubmeshOffset + 40, 0.0f);
        WriteFloat(bytes, GoldenWire::SubmeshOffset + 44, 1.0f);

        WriteFloat(bytes, GoldenWire::ClusterOffset + 0, 0.5f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 4, 0.5f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 8, 0.0f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 12, 1.0f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 16, 0.0f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 20, 0.0f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 24, 1.0f);
        WriteFloat(bytes, GoldenWire::ClusterOffset + 28, 0.5f);
        WriteLe32(bytes, GoldenWire::ClusterOffset + 32, 0);
        WriteLe32(bytes, GoldenWire::ClusterOffset + 36, 3);
        WriteLe32(bytes, GoldenWire::ClusterOffset + 40, 0);
        WriteLe32(bytes, GoldenWire::ClusterOffset + 44, 3);

        WriteGoldenVertex(bytes, GoldenWire::VertexOffset + 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        WriteGoldenVertex(bytes, GoldenWire::VertexOffset + 32, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        WriteGoldenVertex(bytes, GoldenWire::VertexOffset + 64, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        WriteLe32(bytes, GoldenWire::IndexOffset + 0, 0);
        WriteLe32(bytes, GoldenWire::IndexOffset + 4, 1);
        WriteLe32(bytes, GoldenWire::IndexOffset + 8, 2);
        return bytes;
    }

    ByteArray BuildMesh(const char* albedoPath = "Textures/A.png")
    {
        constexpr uint32_t vertexCount = 4;
        constexpr uint32_t indexCount = 12;
        constexpr uint32_t clusterCount = 3;
        const size_t stringSize = std::strlen(albedoPath);
        const size_t submeshTableOffset = HeaderSize;
        const size_t submeshTableSize = SubmeshRecordSize;
        const size_t materialTableOffset = AlignUp(submeshTableOffset + submeshTableSize, SectionAlignment);
        const size_t materialTableSize = MaterialRecordSize;
        const size_t clusterTableOffset = AlignUp(materialTableOffset + materialTableSize, SectionAlignment);
        const size_t clusterTableSize = clusterCount * ClusterRecordSize;
        const size_t stringTableOffset = AlignUp(clusterTableOffset + clusterTableSize, SectionAlignment);
        const size_t vertexPayloadOffset = AlignUp(stringTableOffset + stringSize, SectionAlignment);
        const size_t vertexPayloadSize = vertexCount * VertexRecordSize;
        const size_t indexPayloadOffset = AlignUp(vertexPayloadOffset + vertexPayloadSize, SectionAlignment);
        const size_t indexPayloadSize = indexCount * sizeof(uint32_t);
        const size_t fileSize = indexPayloadOffset + indexPayloadSize;

        ByteArray bytes(fileSize, 0);
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::VertexRecordSize, static_cast<uint32_t>(VertexRecordSize));
        WriteLe32(bytes, HeaderOffset::SubmeshRecordSize, static_cast<uint32_t>(SubmeshRecordSize));
        WriteLe32(bytes, HeaderOffset::MaterialRecordSize, static_cast<uint32_t>(MaterialRecordSize));
        WriteLe32(bytes, HeaderOffset::ClusterRecordSize, static_cast<uint32_t>(ClusterRecordSize));
        WriteLe32(bytes, HeaderOffset::StringRefRecordSize, static_cast<uint32_t>(StringRefRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::SubmeshTableOffset, static_cast<uint64_t>(submeshTableOffset));
        WriteLe64(bytes, HeaderOffset::SubmeshTableSize, static_cast<uint64_t>(submeshTableSize));
        WriteLe64(bytes, HeaderOffset::MaterialTableOffset, static_cast<uint64_t>(materialTableOffset));
        WriteLe64(bytes, HeaderOffset::MaterialTableSize, static_cast<uint64_t>(materialTableSize));
        WriteLe64(bytes, HeaderOffset::ClusterTableOffset, static_cast<uint64_t>(clusterTableOffset));
        WriteLe64(bytes, HeaderOffset::ClusterTableSize, static_cast<uint64_t>(clusterTableSize));
        WriteLe64(bytes, HeaderOffset::StringTableOffset, static_cast<uint64_t>(stringTableOffset));
        WriteLe64(bytes, HeaderOffset::StringTableSize, static_cast<uint64_t>(stringSize));
        WriteLe64(bytes, HeaderOffset::VertexPayloadOffset, static_cast<uint64_t>(vertexPayloadOffset));
        WriteLe64(bytes, HeaderOffset::VertexPayloadSize, static_cast<uint64_t>(vertexPayloadSize));
        WriteLe64(bytes, HeaderOffset::IndexPayloadOffset, static_cast<uint64_t>(indexPayloadOffset));
        WriteLe64(bytes, HeaderOffset::IndexPayloadSize, static_cast<uint64_t>(indexPayloadSize));
        WriteLe32(bytes, HeaderOffset::VertexCount, vertexCount);
        WriteLe32(bytes, HeaderOffset::IndexCount, indexCount);
        WriteLe32(bytes, HeaderOffset::SubmeshCount, 1);
        WriteLe32(bytes, HeaderOffset::MaterialCount, 1);
        WriteLe32(bytes, HeaderOffset::ClusterCount, clusterCount);
        WriteLe32(bytes, HeaderOffset::StringByteCount, static_cast<uint32_t>(stringSize));
        WriteFloat(bytes, HeaderOffset::TotalBoundsCenterX, 0.5f);
        WriteFloat(bytes, HeaderOffset::TotalBoundsCenterY, 0.5f);
        WriteFloat(bytes, HeaderOffset::TotalBoundsCenterZ, 0.0f);
        WriteFloat(bytes, HeaderOffset::TotalBoundsRadius, 2.0f);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmId, ClusterAlgorithmId);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmVersion, ClusterAlgorithmVersion);
        WriteLe32(bytes, HeaderOffset::ClusterMaxTriangles, ClusterMaxTriangles);
        WriteLe32(bytes, HeaderOffset::ClusterMaxVertices, ClusterMaxVertices);
        WriteLe32(bytes, HeaderOffset::ClusterSettingsFlags, ClusterSettingsFlags);

        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexCount, indexCount);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexCount, vertexCount);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterCount, clusterCount);
        WriteFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX, 0.5f);
        WriteFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterY, 0.5f);
        WriteFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterZ, 0.0f);
        WriteFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsRadius, 2.0f);

        WriteLe64(bytes,
                  materialTableOffset + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::StringOffset, 0);
        WriteLe32(bytes,
                  materialTableOffset + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::StringLength,
                  static_cast<uint32_t>(stringSize));

        WriteCluster(bytes, clusterTableOffset + 0 * ClusterRecordSize, 0, 3);
        WriteCluster(bytes, clusterTableOffset + 1 * ClusterRecordSize, 3, 3);
        WriteCluster(bytes, clusterTableOffset + 2 * ClusterRecordSize, 6, 6);

        if (stringSize > 0)
        {
            std::memcpy(bytes.data() + stringTableOffset, albedoPath, stringSize);
        }

        WriteVertex(bytes, vertexPayloadOffset + 0 * VertexRecordSize, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        WriteVertex(bytes, vertexPayloadOffset + 1 * VertexRecordSize, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        WriteVertex(bytes, vertexPayloadOffset + 2 * VertexRecordSize, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f);
        WriteVertex(bytes, vertexPayloadOffset + 3 * VertexRecordSize, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);

        constexpr uint32_t indices[] = {0, 1, 2, 0, 2, 3, 0, 1, 3, 1, 2, 3};
        for (size_t index = 0; index < indexCount; ++index)
        {
            WriteLe32(bytes, indexPayloadOffset + index * sizeof(uint32_t), indices[index]);
        }

        WriteLe64(bytes, HeaderOffset::PayloadHash,
                  ComputeCookedMeshPayloadHash(bytes.data() + submeshTableOffset, fileSize - submeshTableOffset));
        return bytes;
    }

    void RefreshPayloadHash(ByteArray& bytes)
    {
        const size_t payloadOffset = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::SubmeshTableOffset));
        const size_t payloadEnd = static_cast<size_t>(ReadLe64(bytes, HeaderOffset::IndexPayloadOffset) +
                                                      ReadLe64(bytes, HeaderOffset::IndexPayloadSize));
        WriteLe64(bytes, HeaderOffset::PayloadHash,
                  ComputeCookedMeshPayloadHash(bytes.data() + payloadOffset, payloadEnd - payloadOffset));
    }

    size_t SubmeshOffset(const ByteArray& bytes);
    size_t MaterialOffset(const ByteArray& bytes);
    size_t ClusterOffset(const ByteArray& bytes, size_t clusterIndex);

    ByteArray BuildTwoMaterialMesh()
    {
        ByteArray bytes = BuildMesh();
        const size_t secondMaterialOffset = MaterialOffset(bytes) + MaterialRecordSize;
        bytes.insert(bytes.begin() + secondMaterialOffset, MaterialRecordSize, 0);

        WriteLe32(bytes, HeaderOffset::MaterialCount, 2);
        WriteLe64(bytes, HeaderOffset::MaterialTableSize, MaterialRecordSize * 2);
        WriteLe64(bytes, HeaderOffset::ClusterTableOffset,
                  ReadLe64(bytes, HeaderOffset::ClusterTableOffset) + MaterialRecordSize);
        WriteLe64(bytes, HeaderOffset::StringTableOffset,
                  ReadLe64(bytes, HeaderOffset::StringTableOffset) + MaterialRecordSize);
        WriteLe64(bytes, HeaderOffset::VertexPayloadOffset,
                  ReadLe64(bytes, HeaderOffset::VertexPayloadOffset) + MaterialRecordSize);
        WriteLe64(bytes, HeaderOffset::IndexPayloadOffset,
                  ReadLe64(bytes, HeaderOffset::IndexPayloadOffset) + MaterialRecordSize);
        WriteLe64(bytes, HeaderOffset::FileSize, bytes.size());
        RefreshPayloadHash(bytes);
        return bytes;
    }

    ByteArray BuildTriangleMisalignedMesh()
    {
        ByteArray bytes = BuildMesh();
        bytes.resize(bytes.size() - sizeof(uint32_t));
        WriteLe64(bytes, HeaderOffset::FileSize, bytes.size());
        WriteLe32(bytes, HeaderOffset::IndexCount, 11);
        WriteLe64(bytes, HeaderOffset::IndexPayloadSize, 11 * sizeof(uint32_t));
        WriteLe32(bytes, SubmeshOffset(bytes) + SubmeshRecordOffset::IndexCount, 11);
        WriteLe32(bytes, ClusterOffset(bytes, 2) + ClusterRecordOffset::IndexCount, 5);
        RefreshPayloadHash(bytes);
        return bytes;
    }

    ByteArray BuildMeshWithPureClusterHole()
    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, ClusterOffset(bytes, 1) + ClusterRecordOffset::IndexOffset, 6);
        WriteLe32(bytes, ClusterOffset(bytes, 2) + ClusterRecordOffset::IndexOffset, 9);
        WriteLe32(bytes, ClusterOffset(bytes, 2) + ClusterRecordOffset::IndexCount, 3);
        RefreshPayloadHash(bytes);
        return bytes;
    }

    ByteArray BuildMeshWithPureClusterOverlap()
    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, ClusterOffset(bytes, 0) + ClusterRecordOffset::IndexCount, 6);
        RefreshPayloadHash(bytes);
        return bytes;
    }

    size_t SubmeshOffset(const ByteArray& bytes)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::SubmeshTableOffset));
    }

    size_t MaterialOffset(const ByteArray& bytes)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::MaterialTableOffset));
    }

    size_t ClusterOffset(const ByteArray& bytes, size_t clusterIndex)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::ClusterTableOffset)) +
               clusterIndex * ClusterRecordSize;
    }

    size_t StringOffset(const ByteArray& bytes)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::StringTableOffset));
    }

    size_t VertexOffset(const ByteArray& bytes, size_t vertexIndex)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::VertexPayloadOffset)) + vertexIndex * VertexRecordSize;
    }

    size_t IndexOffset(const ByteArray& bytes, size_t index)
    {
        return static_cast<size_t>(ReadLe64(bytes, HeaderOffset::IndexPayloadOffset)) + index * sizeof(uint32_t);
    }

    void ExpectStatus(ByteArray bytes, CookedMeshParseStatus expectedStatus)
    {
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        if (result.Status != expectedStatus)
        {
            std::cerr << "Expected status " << static_cast<int>(expectedStatus) << ", actual "
                      << static_cast<int>(result.Status) << "\n";
        }
        assert(result.Status == expectedStatus);
        assert(!result.Succeeded());
    }
    namespace V1 = NorvesLib::Core::Asset::CookedMeshFormatV1;

    static_assert(V1::ClusterRecordOffset::Reserved4 + sizeof(uint64_t) == V1::ClusterRecordSize);
    static_assert(V1::GroupRecordOffset::Reserved1 + sizeof(uint64_t) == V1::GroupRecordSize);
    static_assert(V1::HeaderOffset::FallbackError + sizeof(float) == V1::HeaderSize);

    // 2段の小さな階層。頂点 5、クラスタ 3（段0 が2つ＝グループ0 のメンバ、段1 が根）、
    // クラスタのインデックス 9 とフォールバックのインデックス 3。根は頂点の基点が 1。
    CookedMeshV1WriteInput BuildV1Input(const char* albedoPath = "Textures/A.png")
    {
        CookedMeshV1WriteInput input;
        input.TotalBoundsCenter = {0.5f, 0.5f, 0.0f};
        input.TotalBoundsRadius = 2.0f;
        input.LODLevelCount = 2;
        input.FallbackError = 0.25f;
        input.AlbedoTexture = AnsiStringView(albedoPath);

        const CookedMeshFloat3 normal = {0.0f, 0.0f, 1.0f};
        input.Vertices.push_back({{0.0f, 0.0f, 0.0f}, normal, {0.0f, 0.0f}});
        input.Vertices.push_back({{1.0f, 0.0f, 0.0f}, normal, {1.0f, 0.0f}});
        input.Vertices.push_back({{1.0f, 1.0f, 0.0f}, normal, {1.0f, 1.0f}});
        input.Vertices.push_back({{0.0f, 1.0f, 0.0f}, normal, {0.0f, 1.0f}});
        input.Vertices.push_back({{2.0f, 2.0f, 0.0f}, normal, {1.0f, 1.0f}});

        const CookedMeshFloat3 center = {0.5f, 0.5f, 0.0f};
        for (uint32_t clusterIndex = 0; clusterIndex < 3; ++clusterIndex)
        {
            CookedMeshCluster cluster;
            cluster.BoundsCenter = center;
            cluster.ConeAxis = {0.0f, 0.0f, 1.0f};
            cluster.ConeCutoff = 0.5f;
            cluster.IndexOffset = clusterIndex * 3;
            cluster.IndexCount = 3;
            cluster.VertexCount = 4;
            if (clusterIndex < 2)
            {
                cluster.BoundsRadius = 0.8f;
                cluster.LODError = 0.0f;
                cluster.LODLevel = 0;
                cluster.GroupId = 0;
                cluster.ParentBoundsCenter = center;
                cluster.ParentBoundsRadius = 1.0f;
                cluster.ParentError = 0.25f;
                cluster.bIsRoot = false;
            }
            else
            {
                cluster.BoundsRadius = 1.0f;
                cluster.LODError = 0.25f;
                cluster.LODLevel = 1;
                cluster.VertexOffset = 1;
                // 根: GroupId=InvalidGroupId、親の境界球は 0、ParentError は最大値（既定のまま）
                cluster.bIsRoot = true;
            }
            input.Clusters.push_back(cluster);
        }

        CookedMeshClusterGroup group;
        group.BoundsCenter = center;
        group.BoundsRadius = 1.0f;
        group.Error = 0.25f;
        group.ClusterOffset = 0;
        group.ClusterCount = 2;
        group.LODLevel = 0;
        input.Groups.push_back(group);

        for (const uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u, 0u, 1u, 3u})
        {
            input.ClusterIndices.push_back(index);
        }
        for (const uint32_t index : {1u, 2u, 4u})
        {
            input.FallbackIndices.push_back(index);
        }
        return input;
    }

    ByteArray SerializeV1(const CookedMeshV1WriteInput& input)
    {
        ByteArray bytes;
        const bool serialized = SerializeCookedMeshV1(input, bytes);
        assert(serialized);
        return bytes;
    }

    size_t V1ClusterOffset(const ByteArray& bytes, size_t clusterIndex)
    {
        return static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::ClusterTableOffset)) +
               clusterIndex * V1::ClusterRecordSize;
    }

    size_t V1GroupOffset(const ByteArray& bytes, size_t groupIndex)
    {
        return static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::GroupTableOffset)) +
               groupIndex * V1::GroupRecordSize;
    }

    size_t V1IndexOffset(const ByteArray& bytes, size_t index)
    {
        return static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::IndexPayloadOffset)) + index * sizeof(uint32_t);
    }

    // 組み立てたバイト列を壊して（hash は直して）拒否の理由を確かめる
    template <typename Mutate>
    void ExpectV1Mutation(Mutate&& mutate, CookedMeshParseStatus expectedStatus)
    {
        ByteArray bytes = SerializeV1(BuildV1Input());
        mutate(bytes);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), expectedStatus);
    }
} // namespace

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "CookedMeshTest start\n";

    {
        constexpr uint8_t foobar[] = {'f', 'o', 'o', 'b', 'a', 'r'};
        assert(ComputeCookedMeshPayloadHash(static_cast<const uint8_t*>(nullptr), 0) == 0xcbf29ce484222325ull);
        assert(ComputeCookedMeshPayloadHash(foobar, sizeof(foobar)) == 0x85944171f73967e8ull);
    }

    {
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(BuildMesh()));
        assert(result.Succeeded());
        assert(result.Status == CookedMeshParseStatus::Success);
        assert(result.Mesh.SourceBlob.IsValid());
        assert(result.Mesh.Vertices.size() == 4);
        assert(result.Mesh.Submeshes.size() == 1);
        assert(result.Mesh.Materials.size() == 1);
        assert(result.Mesh.Clusters.size() == 3);
        assert(result.Mesh.Indices.size() == 12);
        assert(result.Mesh.TotalBoundsCenter.X == 0.5f);
        assert(result.Mesh.TotalBoundsRadius == 2.0f);
        assert(result.Mesh.Vertices[2].Position.X == 1.0f);
        assert(result.Mesh.Vertices[2].Position.Y == 1.0f);
        assert(result.Mesh.Vertices[2].Normal.Z == 1.0f);
        assert(result.Mesh.Vertices[2].TexCoord.V == 1.0f);
        assert(result.Mesh.Submeshes[0].IndexCount == 12);
        assert(result.Mesh.Submeshes[0].ClusterCount == 3);
        assert(result.Mesh.Clusters[2].IndexOffset == 6);
        assert(result.Mesh.Clusters[2].IndexCount == 6);
        assert(result.Mesh.Indices[11] == 3);
        assert(result.Mesh.GetString(result.Mesh.Materials[0].AlbedoTexture) == AnsiStringView("Textures/A.png"));
        assert(result.Mesh.GetString(result.Mesh.Materials[0].NormalTexture).empty());
        assert(result.Mesh.GetString(result.Mesh.Materials[0].ArmTexture).empty());
    }

    {
        const CookedMeshParseResult normalizedPrefix = ParseCookedMesh(MakeBlob(BuildMesh("AssetsX/Textures/A.png")));
        assert(normalizedPrefix.Succeeded());
        ExpectStatus(BuildMesh("Assets/Textures/A.png"), CookedMeshParseStatus::InvalidPath);
        ExpectStatus(BuildMesh("Assets"), CookedMeshParseStatus::InvalidPath);
    }

    {
        const ByteArray goldenBytes = BuildGoldenMesh();
        assert(goldenBytes.size() == 572);
        assert(ReadLe64(goldenBytes, 144) == 0x28de1112fb93d08aull);
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(goldenBytes));
        assert(result.Succeeded());
        assert(result.Mesh.PayloadHash == 0x28de1112fb93d08aull);
        assert(result.Mesh.Vertices.size() == 3);
        assert(result.Mesh.Materials.size() == 1);
        assert(result.Mesh.Clusters.size() == 1);
        assert(result.Mesh.Indices.size() == 3);
        assert(result.Mesh.Indices[0] == 0);
        assert(result.Mesh.Indices[1] == 1);
        assert(result.Mesh.Indices[2] == 2);
    }

    {
        CookedMeshParseResult retainedResult;
        {
            AssetBlob sourceBlob = MakeBlob(BuildMesh());
            retainedResult = ParseCookedMesh(sourceBlob);
            sourceBlob = AssetBlob::Invalid();
        }
        assert(retainedResult.Succeeded());
        assert(retainedResult.Mesh.SourceBlob.IsValid());
        assert(retainedResult.Mesh.Vertices[1].Position.X == 1.0f);
        assert(retainedResult.Mesh.GetString(retainedResult.Mesh.Materials[0].AlbedoTexture) ==
               AnsiStringView("Textures/A.png"));
    }

    {
        assert(ParseCookedMesh(AssetBlob::Invalid()).Status == CookedMeshParseStatus::InvalidBlob);
        const ByteArray empty;
        assert(ParseCookedMesh(MakeBlob(empty)).Status == CookedMeshParseStatus::EmptyBlob);
        const ByteArray shortHeader(HeaderSize - 1, 0);
        assert(ParseCookedMesh(MakeBlob(shortHeader)).Status == CookedMeshParseStatus::HeaderTooSmall);
    }

    {
        ByteArray bytes = BuildMesh();
        bytes[HeaderOffset::Magic] = 'X';
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::BadMagic);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe16(bytes, HeaderOffset::VersionMajor, 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedVersion);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::EndianMarker, 0x04030201u);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::EndianMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize - 1));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::HeaderSizeMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::VertexRecordSize, static_cast<uint32_t>(VertexRecordSize - 1));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::RecordSizeMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(bytes.size() + 1));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::FileSizeMismatch);

        bytes = BuildMesh();
        bytes.push_back(0);
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(bytes.size()));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::FileSizeMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::Reserved0, 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::ReservedFieldNonZero);

        bytes = BuildMesh();
        WriteLe32(bytes, MaterialOffset(bytes) + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::Reserved0,
                  1);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::ReservedFieldNonZero);
    }

    {
        ByteArray bytes = BuildMesh();
        const size_t paddingOffset =
            StringOffset(bytes) + static_cast<size_t>(ReadLe64(bytes, HeaderOffset::StringTableSize));
        assert(paddingOffset < ReadLe64(bytes, HeaderOffset::VertexPayloadOffset));
        bytes[paddingOffset] = 1;
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::PaddingByteNonZero);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::StringTableOffset, static_cast<uint64_t>(bytes.size() + SectionAlignment));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::SectionOutOfRange);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::MaterialTableOffset, ReadLe64(bytes, HeaderOffset::MaterialTableOffset) + 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::SectionMisalignment);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::MaterialTableOffset,
                  ReadLe64(bytes, HeaderOffset::MaterialTableOffset) + SectionAlignment);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::SectionPackingMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::PayloadHash, 0);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::PayloadHashMismatch);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::SubmeshCount, 2);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidCounts);

        ExpectStatus(BuildTwoMaterialMesh(), CookedMeshParseStatus::InvalidCounts);

        bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::ClusterCount, 0);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidCounts);

        ExpectStatus(BuildTriangleMisalignedMesh(), CookedMeshParseStatus::InvalidCounts);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteFloat(bytes, HeaderOffset::TotalBoundsCenterX, std::numeric_limits<float>::quiet_NaN());
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidFloatOrBounds);

        bytes = BuildMesh();
        WriteFloat(bytes, VertexOffset(bytes, 0) + VertexRecordOffset::PositionX,
                   std::numeric_limits<float>::infinity());
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidFloatOrBounds);

        bytes = BuildMesh();
        WriteFloat(bytes, ClusterOffset(bytes, 0) + ClusterRecordOffset::BoundsRadius, -1.0f);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidFloatOrBounds);

        bytes = BuildMesh();
        WriteFloat(bytes, SubmeshOffset(bytes) + SubmeshRecordOffset::BoundsCenterX,
                   std::numeric_limits<float>::quiet_NaN());
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidFloatOrBounds);

        bytes = BuildMesh();
        WriteFloat(bytes, SubmeshOffset(bytes) + SubmeshRecordOffset::BoundsRadius, -1.0f);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidFloatOrBounds);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, IndexOffset(bytes, 0), 4);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidIndexRange);

        bytes = BuildMesh();
        WriteLe32(bytes, SubmeshOffset(bytes) + SubmeshRecordOffset::IndexCount, 15);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidIndexRange);
    }

    {
        ExpectStatus(BuildMeshWithPureClusterHole(), CookedMeshParseStatus::InvalidClusterRange);
        ExpectStatus(BuildMeshWithPureClusterOverlap(), CookedMeshParseStatus::InvalidClusterRange);

        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, ClusterOffset(bytes, 2) + ClusterRecordOffset::VertexCount, 3);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidClusterRange);
    }

    {
        ByteArray bytes = BuildMesh();
        bytes[StringOffset(bytes)] = 0x1fu;
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidStringTable);
    }

    {
        ExpectStatus(BuildMesh("../A.png"), CookedMeshParseStatus::InvalidPath);
        ExpectStatus(BuildMesh("/Textures/A.png"), CookedMeshParseStatus::InvalidPath);
        ExpectStatus(BuildMesh("//server/share/A.png"), CookedMeshParseStatus::InvalidPath);
        ExpectStatus(BuildMesh("C:Textures/A.png"), CookedMeshParseStatus::InvalidPath);
        ExpectStatus(BuildMesh("Textures\\A.png"), CookedMeshParseStatus::InvalidPath);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes,
                  MaterialOffset(bytes) + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::StringLength,
                  static_cast<uint32_t>(ReadLe64(bytes, HeaderOffset::StringTableSize) + 1));
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidMaterialTextureReference);

        bytes = BuildMesh();
        WriteLe64(bytes,
                  MaterialOffset(bytes) + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::StringOffset, 1);
        WriteLe32(bytes,
                  MaterialOffset(bytes) + MaterialRecordOffset::AlbedoTexture + StringRefRecordOffset::StringLength, 0);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::InvalidMaterialTextureReference);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmId, ClusterAlgorithmId + 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedV0Feature);

        bytes = BuildMesh();
        WriteLe32(bytes, ClusterOffset(bytes, 0) + ClusterRecordOffset::LODLevel, 1);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedV0Feature);

        bytes = BuildMesh();
        WriteLe32(bytes, SubmeshOffset(bytes) + SubmeshRecordOffset::VertexOffset, 1);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedV0Feature);
    }

    {
        ByteArray bytes = BuildMesh();
        WriteLe64(bytes, HeaderOffset::IndexPayloadOffset, std::numeric_limits<uint64_t>::max() - 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::IntegerOverflow);
    }

    // ---- NVMESH v1 ----

    {
        // 書き出し → 読み込みの往復で、階層・グループ・フォールバック・材質が一致する
        const CookedMeshV1WriteInput input = BuildV1Input();
        const ByteArray bytes = SerializeV1(input);
        assert(std::memcmp(bytes.data(), V1::Magic, V1::MagicSize) == 0);
        assert(ReadLe64(bytes, V1::HeaderOffset::FileSize) == bytes.size());
        assert(ReadLe32(bytes, V1::HeaderOffset::ClusterRecordSize) == 128);

        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded());
        const CookedMeshData& mesh = result.Mesh;
        assert(mesh.FormatMajor == 1);
        assert(mesh.LODLevelCount == 2);
        assert(mesh.Vertices.size() == 5);
        assert(mesh.Clusters.size() == 3);
        assert(mesh.Groups.size() == 1);
        assert(mesh.Indices.size() == 12);
        assert(mesh.Submeshes.size() == 1 && mesh.Submeshes[0].IndexCount == 9 && mesh.Submeshes[0].ClusterCount == 3);
        assert(mesh.FallbackIndexOffset == 9 && mesh.FallbackIndexCount == 3 && mesh.FallbackError == 0.25f);
        assert(mesh.Indices[9] == 1 && mesh.Indices[10] == 2 && mesh.Indices[11] == 4);
        assert(mesh.Vertices[4].Position.X == 2.0f && mesh.Vertices[4].TexCoord.V == 1.0f);
        assert(mesh.TotalBoundsRadius == 2.0f);

        const CookedMeshCluster& leaf = mesh.Clusters[1];
        assert(!leaf.bIsRoot && leaf.GroupId == 0 && leaf.LODLevel == 0 && leaf.LODError == 0.0f);
        assert(leaf.BoundsRadius == 0.8f && leaf.ParentBoundsRadius == 1.0f && leaf.ParentError == 0.25f);
        assert(leaf.ParentBoundsCenter.X == 0.5f && leaf.IndexOffset == 3 && leaf.IndexCount == 3);
        assert(leaf.VertexOffset == 0 && leaf.VertexCount == 4 && leaf.PageId == 0);
        assert(leaf.ConeAxis.Z == 1.0f && leaf.ConeCutoff == 0.5f);
        const CookedMeshCluster& root = mesh.Clusters[2];
        assert(root.bIsRoot && root.GroupId == V1::InvalidGroupId && root.ParentError == V1::RootParentError);
        assert(root.LODLevel == 1 && root.LODError == 0.25f && root.VertexOffset == 1 &&
               root.ParentBoundsRadius == 0.0f);
        assert(mesh.Groups[0].ClusterOffset == 0 && mesh.Groups[0].ClusterCount == 2 && mesh.Groups[0].Error == 0.25f);
        assert(mesh.Groups[0].BoundsRadius == 1.0f && mesh.Groups[0].LODLevel == 0);
        assert(mesh.GetString(mesh.Materials[0].AlbedoTexture) == AnsiStringView("Textures/A.png"));
        assert(mesh.GetString(mesh.Materials[0].NormalTexture).empty());

        // 読んだ内容から組み直したバイト列は元と一致する
        CookedMeshV1WriteInput rebuilt;
        rebuilt.TotalBoundsCenter = mesh.TotalBoundsCenter;
        rebuilt.TotalBoundsRadius = mesh.TotalBoundsRadius;
        rebuilt.LODLevelCount = mesh.LODLevelCount;
        rebuilt.FallbackError = mesh.FallbackError;
        rebuilt.Vertices = mesh.Vertices;
        rebuilt.Clusters = mesh.Clusters;
        rebuilt.Groups = mesh.Groups;
        for (size_t index = 0; index < mesh.FallbackIndexOffset; ++index)
        {
            rebuilt.ClusterIndices.push_back(mesh.Indices[index]);
        }
        for (size_t index = mesh.FallbackIndexOffset; index < mesh.Indices.size(); ++index)
        {
            rebuilt.FallbackIndices.push_back(mesh.Indices[index]);
        }
        rebuilt.AlbedoTexture = mesh.GetString(mesh.Materials[0].AlbedoTexture);
        const ByteArray rebuiltBytes = SerializeV1(rebuilt);
        assert(rebuiltBytes.size() == bytes.size());
        assert(std::memcmp(rebuiltBytes.data(), bytes.data(), bytes.size()) == 0);
    }

    {
        // 材質の3種の文字列を詰める。空の参照は (0, 0)
        CookedMeshV1WriteInput input = BuildV1Input("Textures/A.png");
        input.NormalTexture = AnsiStringView("Textures/N.png");
        input.ArmTexture = AnsiStringView("Textures/R.png");
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(SerializeV1(input)));
        assert(result.Succeeded());
        assert(result.Mesh.GetString(result.Mesh.Materials[0].AlbedoTexture) == AnsiStringView("Textures/A.png"));
        assert(result.Mesh.GetString(result.Mesh.Materials[0].NormalTexture) == AnsiStringView("Textures/N.png"));
        assert(result.Mesh.GetString(result.Mesh.Materials[0].ArmTexture) == AnsiStringView("Textures/R.png"));

        input = BuildV1Input("");
        const CookedMeshParseResult noTexture = ParseCookedMesh(MakeBlob(SerializeV1(input)));
        assert(noTexture.Succeeded());
        assert(noTexture.Mesh.GetString(noTexture.Mesh.Materials[0].AlbedoTexture).empty());
    }

    {
        // グループの無い1段の v1（全クラスタが根）も読める
        CookedMeshV1WriteInput input = BuildV1Input();
        input.LODLevelCount = 1;
        input.Groups.clear();
        input.Clusters.resize(1);
        input.Clusters[0].BoundsRadius = 1.0f;
        input.Clusters[0].LODError = 0.0f;
        input.Clusters[0].LODLevel = 0;
        input.Clusters[0].GroupId = V1::InvalidGroupId;
        input.Clusters[0].ParentBoundsCenter = {};
        input.Clusters[0].ParentBoundsRadius = 0.0f;
        input.Clusters[0].ParentError = V1::RootParentError;
        input.Clusters[0].bIsRoot = true;
        input.ClusterIndices.resize(3);
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(SerializeV1(input)));
        assert(result.Succeeded());
        assert(result.Mesh.Groups.empty() && result.Mesh.Clusters.size() == 1 && result.Mesh.LODLevelCount == 1);
        assert(result.Mesh.FallbackIndexOffset == 3 && result.Mesh.FallbackIndexCount == 3);
    }

    {
        // v0 は従来の1段のメッシュとして読む（階層・グループ無し、全体の範囲が粗い段）
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(BuildMesh()));
        assert(result.Succeeded());
        assert(result.Mesh.FormatMajor == 0 && result.Mesh.LODLevelCount == 1 && result.Mesh.Groups.empty());
        assert(result.Mesh.FallbackIndexOffset == 0 && result.Mesh.FallbackIndexCount == 12);
        assert(result.Mesh.FallbackError == 0.0f);
        for (const CookedMeshCluster& cluster : result.Mesh.Clusters)
        {
            assert(cluster.bIsRoot && cluster.GroupId == V1::InvalidGroupId && cluster.PageId == 0);
            assert(cluster.ParentError == V1::RootParentError && cluster.LODLevel == 0);
        }
    }

    {
        // magic の判別: v1 の magic で短い入力は、短いヘッダとして拒否される
        ByteArray bytes(100, 0);
        std::memcpy(bytes.data(), V1::Magic, V1::MagicSize);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::HeaderTooSmall);
    }

    {
        // MegaMeshCreateInfo へ渡せる形（v1: 全段のクラスタ・グループ・フォールバック）
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(SerializeV1(BuildV1Input())));
        assert(result.Succeeded());
        Mega::MegaMeshCreateInfo createInfo;
        assert(Mega::BuildMegaMeshCreateInfoFromCookedMesh(result.Mesh, createInfo));
        assert(createInfo.VertexCount == 5 && createInfo.VertexStride == 32 && createInfo.VertexDataSize == 5 * 32);
        assert(createInfo.IndexCount == 12 && createInfo.IndexData == result.Mesh.Indices.data());
        assert(!createInfo.bBuildLODHierarchy && createInfo.bBakedLODHierarchy && createInfo.BakedLODLevelCount == 2);
        assert(createInfo.Clusters.size() == 3 && createInfo.ClusterGroups.size() == 1);
        assert(createInfo.FallbackIndexOffset == 9 && createInfo.FallbackIndexCount == 3);
        assert(createInfo.FallbackError == 0.25f);
        assert(createInfo.Clusters[1].GroupId == 0 && createInfo.Clusters[1].ParentError == 0.25f);
        assert(createInfo.Clusters[1].ParentBounds.Radius == 1.0f && createInfo.Clusters[1].Bounds.Radius == 0.8f);
        assert(createInfo.Clusters[2].GroupId == Mega::INVALID_CLUSTER_GROUP_ID);
        assert(createInfo.Clusters[2].VertexOffset == 1 && createInfo.Clusters[2].LODLevel == 1);
        assert(createInfo.ClusterGroups[0].ClusterCount == 2 && createInfo.ClusterGroups[0].Error == 0.25f);
        assert(createInfo.TotalBounds.Radius == 2.0f);

        // v0 は従来の1段のメッシュのまま（焼き込みの階層・フォールバック無し）
        const CookedMeshParseResult v0Result = ParseCookedMesh(MakeBlob(BuildMesh()));
        assert(v0Result.Succeeded());
        Mega::MegaMeshCreateInfo v0CreateInfo;
        assert(Mega::BuildMegaMeshCreateInfoFromCookedMesh(v0Result.Mesh, v0CreateInfo));
        assert(!v0CreateInfo.bBakedLODHierarchy && v0CreateInfo.ClusterGroups.empty());
        assert(v0CreateInfo.FallbackIndexCount == 0 && v0CreateInfo.Clusters.size() == 3);
        assert(v0CreateInfo.Clusters[2].IndexOffset == 6 && v0CreateInfo.Clusters[2].IndexCount == 6);
    }

    {
        // 壊れた入力の拒否: ファイルの大きさ・hash・版・端数・パス・クラスタ無し
        ByteArray bytes = SerializeV1(BuildV1Input());
        bytes.resize(bytes.size() - sizeof(uint32_t));
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::FileSizeMismatch);

        bytes = SerializeV1(BuildV1Input());
        bytes[static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::VertexPayloadOffset)) + 3] ^= 0x40u;
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::PayloadHashMismatch);

        bytes = SerializeV1(BuildV1Input());
        WriteLe16(bytes, V1::HeaderOffset::VersionMinor, 1);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedVersion);

        bytes = SerializeV1(BuildV1Input());
        WriteLe16(bytes, V1::HeaderOffset::VersionMajor, 0);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::UnsupportedVersion);

        bytes = SerializeV1(BuildV1Input());
        WriteLe32(bytes, V1::HeaderOffset::EndianMarker, 0x04030201u);
        ExpectStatus(std::move(bytes), CookedMeshParseStatus::EndianMismatch);

        ExpectStatus(SerializeV1(BuildV1Input("../a.png")), CookedMeshParseStatus::InvalidPath);

        CookedMeshV1WriteInput noClusters = BuildV1Input();
        noClusters.Clusters.clear();
        noClusters.Groups.clear();
        ExpectStatus(SerializeV1(noClusters), CookedMeshParseStatus::InvalidCounts);
    }

    // ヘッダ
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::ClusterRecordSize, 80); },
                     CookedMeshParseStatus::RecordSizeMismatch);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::GroupRecordSize, 40); },
                     CookedMeshParseStatus::RecordSizeMismatch);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::Flags, 1); },
                     CookedMeshParseStatus::ReservedFieldNonZero);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::GroupCount, 4); },
                     CookedMeshParseStatus::InvalidCounts);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::LODLevelCount, 0); },
                     CookedMeshParseStatus::InvalidCounts);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::LODLevelCount, V1::MaxLODLevels + 1); },
                     CookedMeshParseStatus::InvalidCounts);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        { WriteLe64(bytes, V1::HeaderOffset::GroupTableSize, ReadLe64(bytes, V1::HeaderOffset::GroupTableSize) + 8); },
        CookedMeshParseStatus::InvalidCounts);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::ClusterAlgorithmId, 1); },
                     CookedMeshParseStatus::UnsupportedV1Feature);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            // 文字列表の末尾と頂点の先頭の間の詰め物（0 であること）
            const size_t paddingOffset = static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::StringTableOffset)) +
                                         static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::StringTableSize));
            bytes[paddingOffset] = 1;
        },
        CookedMeshParseStatus::PaddingByteNonZero);

    // フォールバックの範囲
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::FallbackIndexCount, 0); },
                     CookedMeshParseStatus::InvalidFallbackRange);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::FallbackIndexCount, 2); },
                     CookedMeshParseStatus::InvalidFallbackRange);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::FallbackIndexOffset, 8); },
                     CookedMeshParseStatus::InvalidFallbackRange);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            // 範囲は末尾まで届くが、クラスタのインデックスの範囲（サブメッシュ）と食い違う
            WriteLe32(bytes, V1::HeaderOffset::FallbackIndexOffset, 6);
            WriteLe32(bytes, V1::HeaderOffset::FallbackIndexCount, 6);
        },
        CookedMeshParseStatus::InvalidIndexRange);
    ExpectV1Mutation([](ByteArray& bytes) { WriteFloat(bytes, V1::HeaderOffset::FallbackError, -1.0f); },
                     CookedMeshParseStatus::InvalidFloatOrBounds);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1IndexOffset(bytes, 11), 99); },
                     CookedMeshParseStatus::InvalidIndexRange);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            WriteLe32(bytes,
                      static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::SubmeshTableOffset)) +
                          V1::SubmeshRecordOffset::VertexCount,
                      4);
        },
        CookedMeshParseStatus::InvalidIndexRange);

    // クラスタの記録
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::IndexCount, 4); },
        CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 1) + V1::ClusterRecordOffset::IndexOffset, 4); },
        CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1IndexOffset(bytes, 0), 4); },
                     CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::VertexCount, 129); },
        CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::VertexCount, 0); },
        CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 2) + V1::ClusterRecordOffset::VertexOffset, 2); },
        CookedMeshParseStatus::InvalidClusterRange);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::PageId, 1); },
        CookedMeshParseStatus::UnsupportedV1Feature);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::MaterialIndex, 1); },
        CookedMeshParseStatus::UnsupportedV1Feature);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::Flags, 2); },
        CookedMeshParseStatus::ReservedFieldNonZero);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::Reserved0, 1); },
        CookedMeshParseStatus::ReservedFieldNonZero);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            WriteFloat(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::SelfRadius,
                       std::numeric_limits<float>::quiet_NaN());
        },
        CookedMeshParseStatus::InvalidFloatOrBounds);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::SelfError, -1.0f); },
        CookedMeshParseStatus::InvalidFloatOrBounds);

    // LOD の階層（根・グループ・誤差の整合）
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::LODLevel, 2); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 2) + V1::ClusterRecordOffset::Flags, 0); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::GroupId, 5); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::SelfError, 0.5f); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::ParentError, 0.3f); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::SelfRadius, 5.0f); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::Error, 0.3f); },
        CookedMeshParseStatus::InvalidLODGraph);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteFloat(bytes, V1ClusterOffset(bytes, 2) + V1::ClusterRecordOffset::ParentRadius, 1.0f); },
        CookedMeshParseStatus::InvalidLODGraph);

    // グループの表
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::ClusterCount, 0); },
        CookedMeshParseStatus::InvalidGroupTable);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::ClusterOffset, 2); },
        CookedMeshParseStatus::InvalidGroupTable);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::ClusterCount, 1); },
        CookedMeshParseStatus::InvalidGroupTable);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::LODLevel, 2); },
        CookedMeshParseStatus::InvalidGroupTable);
    ExpectV1Mutation(
        [](ByteArray& bytes) { WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::Flags, 1); },
        CookedMeshParseStatus::ReservedFieldNonZero);
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            WriteFloat(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::BoundsRadius,
                       std::numeric_limits<float>::infinity());
        },
        CookedMeshParseStatus::InvalidFloatOrBounds);

    std::cout << "CookedMeshTest passed\n";
    return 0;
}
