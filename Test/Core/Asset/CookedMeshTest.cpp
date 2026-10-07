#include "Asset/CookedMeshFormat.h"
#include "Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Rendering/MegaGeometry/GeometryPageLinks.h"

#include <algorithm>
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
    // ---- v1.1（クラスタのグループをページに詰めた形式）----

    // 4段・クラスタ 15・グループ 7 の階層。段0 は 8 クラスタ（グループ 0〜3、2 つずつ）、段1 は 4 クラスタ
    // （グループ 4・5）、段2 は 2 クラスタ（グループ 6）、段3 は根 1 つ。次の段のクラスタは、自分の境界球・誤差として
    // 親のグループの値を持つ（クッカーと同じ）。クラスタ i は自分の頂点 3 つ（位置 X = i、Y = 0〜2）と三角形 1 つを持ち、
    // ConeAxis.X にクラスタの番号を入れて、並べ替えのあとも見分けられるようにする。フォールバックは八面体
    // （頂点 45〜50、三角形 8）で、閉じている。
    CookedMeshV1WriteInput BuildPagedInput()
    {
        CookedMeshV1WriteInput input;
        input.TotalBoundsCenter = {7.0f, 0.0f, 0.0f};
        input.TotalBoundsRadius = 120.0f;
        input.LODLevelCount = 4;
        input.FallbackError = 0.25f;
        input.AlbedoTexture = AnsiStringView("Textures/A.png");

        for (uint32_t groupIndex = 0; groupIndex < 7; ++groupIndex)
        {
            CookedMeshClusterGroup group;
            if (groupIndex < 4)
            {
                group.BoundsCenter = {static_cast<float>(4 * groupIndex + 1), 0.0f, 0.0f};
                group.BoundsRadius = 2.0f;
                group.Error = 0.1f;
                group.ClusterOffset = 2 * groupIndex;
                group.LODLevel = 0;
            }
            else if (groupIndex < 6)
            {
                group.BoundsCenter = {static_cast<float>(8 * (groupIndex - 4) + 3), 0.0f, 0.0f};
                group.BoundsRadius = 6.0f;
                group.Error = 0.2f;
                group.ClusterOffset = 8 + 2 * (groupIndex - 4);
                group.LODLevel = 1;
            }
            else
            {
                group.BoundsCenter = {7.0f, 0.0f, 0.0f};
                group.BoundsRadius = 10.0f;
                group.Error = 0.3f;
                group.ClusterOffset = 12;
                group.LODLevel = 2;
            }
            group.ClusterCount = 2;
            input.Groups.push_back(group);
        }

        const CookedMeshFloat3 normal = {0.0f, 0.0f, 1.0f};
        for (uint32_t clusterIndex = 0; clusterIndex < 15; ++clusterIndex)
        {
            CookedMeshCluster cluster;
            cluster.bIsRoot = false;
            cluster.ConeAxis = {static_cast<float>(clusterIndex), 0.0f, 1.0f};
            cluster.ConeCutoff = 0.5f;
            cluster.IndexOffset = 3 * clusterIndex;
            cluster.IndexCount = 3;
            cluster.VertexOffset = 3 * clusterIndex;
            cluster.VertexCount = 3;
            if (clusterIndex < 8)
            {
                cluster.LODLevel = 0;
                cluster.BoundsCenter = {static_cast<float>(2 * clusterIndex), 0.0f, 0.0f};
                cluster.BoundsRadius = 1.0f;
                cluster.LODError = 0.0f;
                cluster.GroupId = clusterIndex / 2;
            }
            else if (clusterIndex < 12)
            {
                const CookedMeshClusterGroup& born = input.Groups[clusterIndex - 8];
                cluster.LODLevel = 1;
                cluster.BoundsCenter = born.BoundsCenter;
                cluster.BoundsRadius = born.BoundsRadius;
                cluster.LODError = born.Error;
                cluster.GroupId = 4 + (clusterIndex - 8) / 2;
                cluster.SourceGroupId = clusterIndex - 8;
            }
            else if (clusterIndex < 14)
            {
                const CookedMeshClusterGroup& born = input.Groups[4 + (clusterIndex - 12)];
                cluster.LODLevel = 2;
                cluster.BoundsCenter = born.BoundsCenter;
                cluster.BoundsRadius = born.BoundsRadius;
                cluster.LODError = born.Error;
                cluster.GroupId = 6;
                cluster.SourceGroupId = 4 + (clusterIndex - 12);
            }
            else
            {
                const CookedMeshClusterGroup& born = input.Groups[6];
                cluster.LODLevel = 3;
                cluster.BoundsCenter = born.BoundsCenter;
                cluster.BoundsRadius = born.BoundsRadius;
                cluster.LODError = born.Error;
                cluster.SourceGroupId = 6;
                cluster.bIsRoot = true;
            }

            if (!cluster.bIsRoot)
            {
                const CookedMeshClusterGroup& parent = input.Groups[cluster.GroupId];
                cluster.ParentBoundsCenter = parent.BoundsCenter;
                cluster.ParentBoundsRadius = parent.BoundsRadius;
                cluster.ParentError = parent.Error;
            }
            input.Clusters.push_back(cluster);

            for (uint32_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                input.Vertices.push_back({{static_cast<float>(clusterIndex), static_cast<float>(vertexIndex), 0.0f},
                                          normal,
                                          {static_cast<float>(vertexIndex), 0.0f}});
                input.ClusterIndices.push_back(vertexIndex);
            }
        }

        // フォールバック: 八面体（+x -x +y -y +z -z の 6 頂点、向きの揃った 8 三角形）
        const CookedMeshFloat3 octahedron[6] = {{100.0f, 0.0f, 0.0f}, {-100.0f, 0.0f, 0.0f}, {0.0f, 100.0f, 0.0f},
                                                {0.0f, -100.0f, 0.0f}, {0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, -100.0f}};
        for (const CookedMeshFloat3& position : octahedron)
        {
            input.Vertices.push_back({position, normal, {0.0f, 0.0f}});
        }
        for (const uint32_t index : {0u, 2u, 4u, 2u, 1u, 4u, 1u, 3u, 4u, 3u, 0u, 4u,
                                     2u, 0u, 5u, 1u, 2u, 5u, 3u, 1u, 5u, 0u, 3u, 5u})
        {
            input.FallbackIndices.push_back(45 + index);
        }
        return input;
    }

    // 段2 と段3 が根のページ、段1 が 1 ページ、段0 が 2 ページ（合計 4 ページ）になる設定
    CookedMeshPagedWriteOptions SmallPageOptions()
    {
        CookedMeshPagedWriteOptions options;
        options.PageSizeBytes = 1100;
        options.RootClusterBudgetBytes = 800;
        return options;
    }

    ByteArray SerializePaged(const CookedMeshV1WriteInput& input, const CookedMeshPagedWriteOptions& options,
                             CookedMeshPagedWriteInfo* outInfo = nullptr)
    {
        ByteArray bytes;
        CookedMeshPagedWriteInfo info;
        const CookedMeshPagedWriteStatus status = SerializeCookedMeshV1Paged(input, options, bytes, info);
        assert(status == CookedMeshPagedWriteStatus::Success);
        if (outInfo != nullptr)
        {
            *outInfo = info;
        }
        return bytes;
    }

    size_t PagedTableRecord(const ByteArray& bytes, size_t pageIndex)
    {
        return static_cast<size_t>(ReadLe64(bytes, V1::PagedHeaderOffset::PageTableOffset)) +
               pageIndex * V1::PageTableRecordSize;
    }

    size_t PagedPageOffset(const ByteArray& bytes, size_t pageIndex)
    {
        return static_cast<size_t>(
            ReadLe64(bytes, PagedTableRecord(bytes, pageIndex) + V1::PageTableRecordOffset::FileOffset));
    }

    size_t PagedPageSize(const ByteArray& bytes, size_t pageIndex)
    {
        return ReadLe32(bytes, PagedTableRecord(bytes, pageIndex) + V1::PageTableRecordOffset::Size);
    }

    // ページの中身を壊したあとに、表の hash をページに合わせ直す（ページの検査まで進めるため）
    void RefreshPageHash(ByteArray& bytes, size_t pageIndex)
    {
        WriteLe64(bytes, PagedTableRecord(bytes, pageIndex) + V1::PageTableRecordOffset::PageHash,
                  ComputeCookedMeshPayloadHash(bytes.data() + PagedPageOffset(bytes, pageIndex),
                                               PagedPageSize(bytes, pageIndex)));
    }

    template <typename Mutate>
    void ExpectPagedMutation(Mutate&& mutate, CookedMeshParseStatus expectedStatus)
    {
        ByteArray bytes = SerializePaged(BuildPagedInput(), SmallPageOptions());
        mutate(bytes);
        RefreshPayloadHash(bytes);
        ExpectStatus(std::move(bytes), expectedStatus);
    }

    // 三角形の列が閉じているか（向きつきの辺がどれも1回だけ現れ、逆向きの辺も1回だけ現れる）
    bool IsClosedTriangleList(const VariableArray<uint32_t>& indices)
    {
        if (indices.empty() || indices.size() % 3 != 0)
        {
            return false;
        }

        VariableArray<uint64_t> edges;
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            for (size_t corner = 0; corner < 3; ++corner)
            {
                const uint64_t from = indices[triangle + corner];
                const uint64_t to = indices[triangle + (corner + 1) % 3];
                edges.push_back((from << 32) | to);
            }
        }
        std::sort(edges.begin(), edges.end());
        for (size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex)
        {
            if (edgeIndex > 0 && edges[edgeIndex] == edges[edgeIndex - 1])
            {
                return false;
            }
            const uint64_t reversed = (edges[edgeIndex] << 32) | (edges[edgeIndex] >> 32);
            if (!std::binary_search(edges.begin(), edges.end(), reversed))
            {
                return false;
            }
        }
        return true;
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
        WriteLe16(bytes, V1::HeaderOffset::VersionMinor, 2);
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
                     CookedMeshParseStatus::UnsupportedVersion);
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
        [](ByteArray& bytes) { WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::Reserved1, 1); },
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
    // 段の構成: 非根のクラスタが最上位の段にある（親の段が無い）
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            WriteLe32(bytes, V1ClusterOffset(bytes, 0) + V1::ClusterRecordOffset::LODLevel, 1);
            WriteLe32(bytes, V1ClusterOffset(bytes, 1) + V1::ClusterRecordOffset::LODLevel, 1);
            WriteLe32(bytes, V1GroupOffset(bytes, 0) + V1::GroupRecordOffset::LODLevel, 1);
        },
        CookedMeshParseStatus::InvalidLODGraph);
    // 宣言した段数だけが増え、最上位に空の段ができる
    ExpectV1Mutation([](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::LODLevelCount, 3); },
                     CookedMeshParseStatus::InvalidLODGraph);
    // 根が最も粗い段にあり、途中の段（段1）が空
    ExpectV1Mutation(
        [](ByteArray& bytes)
        {
            WriteLe32(bytes, V1::HeaderOffset::LODLevelCount, 3);
            WriteLe32(bytes, V1ClusterOffset(bytes, 2) + V1::ClusterRecordOffset::LODLevel, 2);
        },
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

    // v1.1: クラスタのグループをページに詰めた形式
    {
        const CookedMeshV1WriteInput input = BuildPagedInput();
        const CookedMeshPagedWriteOptions options = SmallPageOptions();
        CookedMeshPagedWriteInfo info;
        const ByteArray bytes = SerializePaged(input, options, &info);
        assert(info.PageCount == 4 && info.RootPageCount == 1 && info.RootPageClusterCount == 3 &&
               info.RootPageMinLODLevel == 2);
        assert(info.MaxPageBytes <= options.PageSizeBytes && info.LargestGroupBytes == 472);

        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded());
        const CookedMeshData& mesh = result.Mesh;
        assert(mesh.FormatMajor == 1 && mesh.FormatMinor == 1);
        assert(mesh.LODLevelCount == 4 && mesh.Clusters.size() == 15 && mesh.Groups.size() == 7);
        assert(mesh.Pages.size() == 4);
        assert(mesh.FallbackIndexCount == 24 && mesh.FallbackError == 0.25f);
        assert(mesh.FallbackIndexOffset == 45 && mesh.Indices.size() == 45 + 24);

        // ページの表: ファイルの中のオフセット・大きさ・親のページの番号。ページは隙間なく並び、通常のページは上限以下
        uint64_t cursor = mesh.Pages[0].FileOffset;
        uint32_t clusterBase = 0;
        uint32_t vertexBase = 0;
        uint32_t indexBase = 0;
        for (uint32_t pageId = 0; pageId < mesh.Pages.size(); ++pageId)
        {
            const CookedMeshPage& page = mesh.Pages[pageId];
            assert(page.FileOffset == cursor);
            cursor += page.Size;
            assert(page.bIsRoot == (pageId == V1::RootPageId));
            if (pageId == V1::RootPageId)
            {
                assert(page.ParentPageId == V1::InvalidPageId);
            }
            else
            {
                assert(page.ParentPageId < pageId && page.Size <= options.PageSizeBytes && page.Size <= V1::PageSize);
            }
            assert(page.FirstClusterIndex == clusterBase && page.FirstVertex == vertexBase &&
                   page.FirstIndex == indexBase);
            clusterBase += page.ClusterCount;
            vertexBase += page.VertexCount;
            indexBase += page.IndexCount;
        }
        assert(cursor == bytes.size() && clusterBase == 15 && vertexBase == mesh.Vertices.size() &&
               indexBase == mesh.FallbackIndexOffset);
        // 段1 のページの親は根のページ、段0 のページの親は段1 のページ
        assert(mesh.Pages[1].ParentPageId == 0 && mesh.Pages[2].ParentPageId == 1 && mesh.Pages[3].ParentPageId == 1);
        assert(mesh.Pages[0].ClusterCount == 3 && mesh.Pages[1].ClusterCount == 4 && mesh.Pages[2].ClusterCount == 4 &&
               mesh.Pages[3].ClusterCount == 4);

        // クラスタはすべて1回ずつ現れ、PageId は入っているページの番号。頂点・インデックスは自分のページの中を指し、
        // 元のクラスタと同じ頂点・三角形を持つ
        VariableArray<bool> seen(15, false);
        for (uint32_t pageId = 0; pageId < mesh.Pages.size(); ++pageId)
        {
            const CookedMeshPage& page = mesh.Pages[pageId];
            for (uint32_t clusterIndex = page.FirstClusterIndex; clusterIndex < page.FirstClusterIndex + page.ClusterCount;
                 ++clusterIndex)
            {
                const CookedMeshCluster& cluster = mesh.Clusters[clusterIndex];
                assert(cluster.PageId == pageId);
                const uint32_t sourceIndex = static_cast<uint32_t>(cluster.ConeAxis.X);
                assert(sourceIndex < 15 && !seen[sourceIndex]);
                seen[sourceIndex] = true;

                const CookedMeshCluster& source = input.Clusters[sourceIndex];
                assert(cluster.LODLevel == source.LODLevel && cluster.bIsRoot == source.bIsRoot &&
                       cluster.GroupId == source.GroupId && cluster.SourceGroupId == source.SourceGroupId &&
                       cluster.LODError == source.LODError &&
                       cluster.ParentError == source.ParentError && cluster.BoundsRadius == source.BoundsRadius);
                assert(cluster.VertexOffset >= page.FirstVertex &&
                       cluster.VertexOffset + cluster.VertexCount <= page.FirstVertex + page.VertexCount);
                assert(cluster.IndexOffset >= page.FirstIndex &&
                       cluster.IndexOffset + cluster.IndexCount <= page.FirstIndex + page.IndexCount);
                for (uint32_t vertexIndex = 0; vertexIndex < cluster.VertexCount; ++vertexIndex)
                {
                    const CookedMeshVertex& vertex = mesh.Vertices[cluster.VertexOffset + vertexIndex];
                    assert(vertex.Position.X == static_cast<float>(sourceIndex) &&
                           vertex.Position.Y == static_cast<float>(vertexIndex));
                }
                for (uint32_t index = 0; index < cluster.IndexCount; ++index)
                {
                    assert(mesh.Indices[cluster.IndexOffset + index] == index);
                }
            }
        }
        for (const bool wasSeen : seen)
        {
            assert(wasSeen);
        }

        // 1つのグループは1つのページに収まる（メンバが別のページへまたがらない）。グループは元の番号のまま
        VariableArray<uint32_t> groupsPerPage(mesh.Pages.size(), 0);
        for (uint32_t groupIndex = 0; groupIndex < mesh.Groups.size(); ++groupIndex)
        {
            const CookedMeshClusterGroup& group = mesh.Groups[groupIndex];
            const uint32_t groupPage = mesh.Clusters[group.ClusterOffset].PageId;
            for (uint32_t member = group.ClusterOffset; member < group.ClusterOffset + group.ClusterCount; ++member)
            {
                assert(mesh.Clusters[member].PageId == groupPage && mesh.Clusters[member].GroupId == groupIndex);
            }
            ++groupsPerPage[groupPage];
        }
        assert(groupsPerPage[0] == 1 && groupsPerPage[1] == 2 && groupsPerPage[2] == 2 && groupsPerPage[3] == 2);
        // 根のクラスタは根のページ
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            assert(!cluster.bIsRoot || cluster.PageId == V1::RootPageId);
        }

        // 往復: 読んだ内容から組み直して、同じ設定で書くとバイト列が一致する
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
        const ByteArray rebuiltBytes = SerializePaged(rebuilt, options);
        assert(rebuiltBytes.size() == bytes.size());
        assert(std::memcmp(rebuiltBytes.data(), bytes.data(), bytes.size()) == 0);

        // 根のページだけで閉じたメッシュ（フォールバック）が描ける: ほかのページを見ずに根のページの範囲だけを読み、
        // 根のページの頂点とフォールバックのインデックスだけで、元の八面体と同じ閉じた形になる
        const CookedMeshPage& rootPage = mesh.Pages[0];
        CookedMeshPageContent rootContent;
        assert(ParseCookedMeshPage(Span<const uint8_t>(bytes.data() + rootPage.FileOffset, rootPage.Size),
                                   V1::RootPageId, true, mesh.LODLevelCount, static_cast<uint32_t>(mesh.Groups.size()),
                                   rootContent) == CookedMeshParseStatus::Success);
        assert(rootContent.Clusters.size() == 3 && rootContent.FallbackIndices.size() == 24);
        assert(rootContent.Vertices.size() == 9 + 6);
        for (const uint32_t index : rootContent.FallbackIndices)
        {
            assert(index < rootContent.Vertices.size());
        }
        assert(IsClosedTriangleList(rootContent.FallbackIndices));
        for (size_t index = 0; index < rootContent.FallbackIndices.size(); ++index)
        {
            const CookedMeshFloat3& page = rootContent.Vertices[rootContent.FallbackIndices[index]].Position;
            const CookedMeshFloat3& source = input.Vertices[input.FallbackIndices[index]].Position;
            assert(page.X == source.X && page.Y == source.Y && page.Z == source.Z);
        }
        // 根のページが持つ最も粗い段のクラスタ（段2・段3）と、その頂点・三角形
        for (const CookedMeshCluster& cluster : rootContent.Clusters)
        {
            assert(cluster.LODLevel >= 2 && cluster.PageId == V1::RootPageId);
            assert(cluster.VertexOffset + cluster.VertexCount <= rootContent.Vertices.size());
        }
        // 根でないページを根のページとして読むと拒否される（ページの番号・フォールバックの有無が合わない）
        const CookedMeshPage& childPage = mesh.Pages[1];
        CookedMeshPageContent childContent;
        const Span<const uint8_t> childBytes(bytes.data() + childPage.FileOffset, childPage.Size);
        assert(ParseCookedMeshPage(childBytes, 1, false, mesh.LODLevelCount, static_cast<uint32_t>(mesh.Groups.size()),
                                   childContent) == CookedMeshParseStatus::Success);
        assert(childContent.FallbackIndices.empty() && childContent.Clusters.size() == 4);
        assert(ParseCookedMeshPage(childBytes, V1::RootPageId, true, mesh.LODLevelCount,
                                   static_cast<uint32_t>(mesh.Groups.size()), childContent) !=
               CookedMeshParseStatus::Success);
        assert(ParseCookedMeshPage(childBytes, 1, true, mesh.LODLevelCount, static_cast<uint32_t>(mesh.Groups.size()),
                                   childContent) != CookedMeshParseStatus::Success);
    }

    // v1.1: 根のページ（常駐）も 128 KiB 以下で、複数のページに分かれる。フォールバックは根のページを合わせて閉じる
    {
        // フォールバックを、離れた 4 つの八面体（頂点 24・三角形 32）にして、ページの上限を小さくする。
        // 根のクラスタ・段2 のグループ・フォールバックが、それぞれ別の根のページになる
        CookedMeshV1WriteInput input = BuildPagedInput();
        const CookedMeshFloat3 normal = {0.0f, 0.0f, 1.0f};
        const CookedMeshFloat3 octahedron[6] = {{100.0f, 0.0f, 0.0f}, {-100.0f, 0.0f, 0.0f}, {0.0f, 100.0f, 0.0f},
                                                {0.0f, -100.0f, 0.0f}, {0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, -100.0f}};
        const uint32_t octahedronIndices[24] = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
        input.Vertices.resize(45);
        input.FallbackIndices.clear();
        for (uint32_t copy = 0; copy < 4; ++copy)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(input.Vertices.size());
            for (const CookedMeshFloat3& position : octahedron)
            {
                input.Vertices.push_back(
                    {{position.X + 1000.0f * static_cast<float>(copy), position.Y, position.Z}, normal, {0.0f, 0.0f}});
            }
            for (const uint32_t index : octahedronIndices)
            {
                input.FallbackIndices.push_back(baseVertex + index);
            }
        }

        CookedMeshPagedWriteOptions options = SmallPageOptions();
        options.PageSizeBytes = 600;
        CookedMeshPagedWriteInfo info;
        const ByteArray bytes = SerializePaged(input, options, &info);
        assert(info.RootPageCount >= 4 && info.RootPageCount < info.PageCount);
        assert(info.MaxPageBytes <= options.PageSizeBytes);

        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded());
        const CookedMeshData& mesh = result.Mesh;
        assert(mesh.Pages.size() == info.PageCount && mesh.FallbackIndexCount == 96);

        // 根のページは 0 番から連続し、親を持たない。クラスタを持たない（フォールバックだけの）根のページもある
        uint32_t residentPages = 0;
        uint32_t residentBytes = 0;
        bool bFallbackOnlyPage = false;
        for (uint32_t pageId = 0; pageId < mesh.Pages.size(); ++pageId)
        {
            const CookedMeshPage& page = mesh.Pages[pageId];
            assert(page.Size <= options.PageSizeBytes && page.Size <= V1::PageSize);
            if (page.bIsRoot)
            {
                assert(pageId == residentPages && page.ParentPageId == V1::InvalidPageId);
                ++residentPages;
                residentBytes += page.Size;
                bFallbackOnlyPage = bFallbackOnlyPage || page.ClusterCount == 0;
            }
            else
            {
                assert(page.ParentPageId < pageId);
            }
        }
        assert(residentPages == info.RootPageCount && residentBytes == info.RootPageBytes && bFallbackOnlyPage);
        for (const CookedMeshCluster& cluster : mesh.Clusters)
        {
            assert(!cluster.bIsRoot || mesh.Pages[cluster.PageId].bIsRoot);
        }

        // 根のページだけを1ページずつ読み、フォールバックを集める。どのページも自分の頂点だけを指し、
        // 全部を合わせると元の八面体と同じ位置の、閉じた形になる
        VariableArray<CookedMeshFloat3> positions;
        VariableArray<uint32_t> positionIds;
        for (uint32_t pageId = 0; pageId < residentPages; ++pageId)
        {
            const CookedMeshPage& page = mesh.Pages[pageId];
            CookedMeshPageContent content;
            assert(ParseCookedMeshPage(Span<const uint8_t>(bytes.data() + page.FileOffset, page.Size), pageId, true,
                                       mesh.LODLevelCount, static_cast<uint32_t>(mesh.Groups.size()), content) ==
                   CookedMeshParseStatus::Success);
            for (const uint32_t index : content.FallbackIndices)
            {
                assert(index < content.Vertices.size());
                const CookedMeshFloat3& position = content.Vertices[index].Position;
                size_t found = 0;
                while (found < positions.size() &&
                       !(positions[found].X == position.X && positions[found].Y == position.Y &&
                         positions[found].Z == position.Z))
                {
                    ++found;
                }
                if (found == positions.size())
                {
                    positions.push_back(position);
                }
                positionIds.push_back(static_cast<uint32_t>(found));
            }
        }
        assert(positionIds.size() == 96 && positions.size() == 24 && IsClosedTriangleList(positionIds));
        for (size_t index = 0; index < positionIds.size(); ++index)
        {
            const CookedMeshFloat3& source = input.Vertices[input.FallbackIndices[index]].Position;
            assert(positions[positionIds[index]].X == source.X && positions[positionIds[index]].Y == source.Y &&
                   positions[positionIds[index]].Z == source.Z);
        }

        // 読んだ全体のフォールバック（ページの頂点へ直した添字）も、元の位置の三角形を元の順に指す
        for (size_t index = 0; index < 96; ++index)
        {
            const CookedMeshFloat3& page = mesh.Vertices[mesh.Indices[mesh.FallbackIndexOffset + index]].Position;
            const CookedMeshFloat3& source = input.Vertices[input.FallbackIndices[index]].Position;
            assert(page.X == source.X && page.Y == source.Y && page.Z == source.Z);
        }

        // 往復: 読んだ内容から組み直して、同じ設定で書くとバイト列が一致する
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
        const ByteArray rebuiltBytes = SerializePaged(rebuilt, options);
        assert(rebuiltBytes.size() == bytes.size());
        assert(std::memcmp(rebuiltBytes.data(), bytes.data(), bytes.size()) == 0);
    }

    // v1.1: 既定の設定（128 KiB）で、大きなフォールバックを持つメッシュ。根のページも 128 KiB 以下に分かれる
    {
        CookedMeshV1WriteInput input = BuildPagedInput();
        const CookedMeshFloat3 normal = {0.0f, 0.0f, 1.0f};
        constexpr uint32_t Cells = 100;
        constexpr uint32_t Stride = Cells + 1;
        input.Vertices.resize(45);
        input.FallbackIndices.clear();
        for (uint32_t row = 0; row < Stride; ++row)
        {
            for (uint32_t column = 0; column < Stride; ++column)
            {
                input.Vertices.push_back({{static_cast<float>(column), static_cast<float>(row), 0.0f}, normal, {0.0f, 0.0f}});
            }
        }
        for (uint32_t row = 0; row < Cells; ++row)
        {
            for (uint32_t column = 0; column < Cells; ++column)
            {
                const uint32_t v00 = 45 + row * Stride + column;
                const uint32_t v10 = v00 + 1;
                const uint32_t v01 = v00 + Stride;
                const uint32_t v11 = v01 + 1;
                for (const uint32_t index : {v00, v10, v01, v10, v11, v01})
                {
                    input.FallbackIndices.push_back(index);
                }
            }
        }

        CookedMeshPagedWriteInfo info;
        const ByteArray bytes = SerializePaged(input, CookedMeshPagedWriteOptions{}, &info);
        assert(info.RootPageCount >= 4 && info.RootPageBytes > V1::PageSize);
        assert(info.MaxPageBytes <= V1::PageSize);

        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded());
        const CookedMeshData& mesh = result.Mesh;
        assert(mesh.FallbackIndexCount == 6 * Cells * Cells);
        for (const CookedMeshPage& page : mesh.Pages)
        {
            assert(page.Size <= V1::PageSize);
        }
        for (size_t index = 0; index < input.FallbackIndices.size(); ++index)
        {
            const CookedMeshFloat3& page = mesh.Vertices[mesh.Indices[mesh.FallbackIndexOffset + index]].Position;
            const CookedMeshFloat3& source = input.Vertices[input.FallbackIndices[index]].Position;
            assert(page.X == source.X && page.Y == source.Y && page.Z == source.Z);
        }
    }

    // v1.1: 全部が根のページに収まる小さなメッシュは 1 ページ
    {
        CookedMeshPagedWriteOptions options;
        options.RootClusterBudgetBytes = 1u << 20;
        CookedMeshPagedWriteInfo info;
        const ByteArray bytes = SerializePaged(BuildPagedInput(), options, &info);
        assert(info.PageCount == 1 && info.RootPageMinLODLevel == 0 && info.RootPageClusterCount == 15);
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded() && result.Mesh.Pages.size() == 1 && result.Mesh.Pages[0].bIsRoot);
        for (const CookedMeshCluster& cluster : result.Mesh.Clusters)
        {
            assert(cluster.PageId == 0);
        }
    }

    // v1.1 の書き出しの失敗: グループがページに収まらない・入力の範囲の食い違い
    {
        CookedMeshPagedWriteOptions options = SmallPageOptions();
        options.PageSizeBytes = 400;
        ByteArray bytes;
        CookedMeshPagedWriteInfo info;
        assert(SerializeCookedMeshV1Paged(BuildPagedInput(), options, bytes, info) ==
                   CookedMeshPagedWriteStatus::GroupExceedsPage &&
               bytes.empty());
        // 収まらないのは、根のページに入る段2 のグループ（根のクラスタ 1 つは 300 バイトで収まる）
        assert(info.LargestGroupBytes == 472 && info.LargestGroupIndex == 6);

        // 通常のページの上限（128 KiB）を超える設定と、ヘッダだけのページは受け付けない
        options.PageSizeBytes = static_cast<uint32_t>(V1::PageSize) + 8;
        assert(SerializeCookedMeshV1Paged(BuildPagedInput(), options, bytes, info) ==
               CookedMeshPagedWriteStatus::InvalidInput);
        options.PageSizeBytes = static_cast<uint32_t>(V1::PageHeaderSize);
        assert(SerializeCookedMeshV1Paged(BuildPagedInput(), options, bytes, info) ==
               CookedMeshPagedWriteStatus::InvalidInput);

        CookedMeshV1WriteInput brokenInput = BuildPagedInput();
        brokenInput.Clusters[3].VertexOffset = 100;
        assert(SerializeCookedMeshV1Paged(brokenInput, SmallPageOptions(), bytes, info) ==
               CookedMeshPagedWriteStatus::InvalidInput);
        brokenInput = BuildPagedInput();
        brokenInput.FallbackIndices[0] = 1000;
        assert(SerializeCookedMeshV1Paged(brokenInput, SmallPageOptions(), bytes, info) ==
               CookedMeshPagedWriteStatus::InvalidInput);
        brokenInput = BuildPagedInput();
        brokenInput.Groups[1].ClusterOffset = 1;
        assert(SerializeCookedMeshV1Paged(brokenInput, SmallPageOptions(), bytes, info) ==
               CookedMeshPagedWriteStatus::InvalidInput);
    }

    // 作ったグループの番号（クラスタの記録の +92）: 往復・旧い資産（番号なし）・壊れた番号の拒否・子のページの決まり方
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        const CookedMeshV1WriteInput input = BuildPagedInput();
        const CookedMeshPagedWriteOptions options = SmallPageOptions();

        // 番号を持つ資産: 粗い段のクラスタが、自分を作った1つ細かい段のグループの番号を持つ
        {
            const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(SerializePaged(input, options)));
            assert(result.Succeeded() && result.Mesh.FormatMinor == 1);
            uint32_t numbered = 0;
            for (const CookedMeshCluster& cluster : result.Mesh.Clusters)
            {
                if (cluster.LODLevel == 0)
                {
                    assert(cluster.SourceGroupId == V1::InvalidGroupId);
                    continue;
                }
                assert(cluster.SourceGroupId < result.Mesh.Groups.size());
                const CookedMeshClusterGroup& source = result.Mesh.Groups[cluster.SourceGroupId];
                assert(source.LODLevel + 1 == cluster.LODLevel && source.BoundsRadius == cluster.BoundsRadius &&
                       source.Error == cluster.LODError);
                ++numbered;
            }
            assert(numbered == 7);
        }

        // 旧い資産（番号の項目が 0）は、番号なしとして読める
        CookedMeshV1WriteInput legacyInput = input;
        for (CookedMeshCluster& cluster : legacyInput.Clusters)
        {
            cluster.SourceGroupId = V1::InvalidGroupId;
        }
        const CookedMeshParseResult legacy = ParseCookedMesh(MakeBlob(SerializePaged(legacyInput, options)));
        assert(legacy.Succeeded() && legacy.Mesh.FormatMinor == 1);
        for (const CookedMeshCluster& cluster : legacy.Mesh.Clusters)
        {
            assert(cluster.SourceGroupId == V1::InvalidGroupId);
        }

        // 範囲外の番号は書き出しが拒否する
        {
            CookedMeshV1WriteInput brokenInput = input;
            brokenInput.Clusters[8].SourceGroupId = 7;
            ByteArray bytes;
            CookedMeshPagedWriteInfo info;
            assert(SerializeCookedMeshV1Paged(brokenInput, options, bytes, info) == CookedMeshPagedWriteStatus::InvalidInput);
        }

        // 壊れた番号の拒否（ページの hash とファイルの hash は直す）。ページ 2 は最も細かい段、ページ 1 の先頭は段1 のクラスタ
        const auto sourceGroupSlot = [](const ByteArray& bytes, size_t pageIndex)
        {
            return PagedPageOffset(bytes, pageIndex) + V1::PageHeaderSize + V1::ClusterRecordOffset::SourceGroupIdPlusOne;
        };
        // 最も細かい段のクラスタは、作られたものではないので番号を持てない
        ExpectPagedMutation(
            [&](ByteArray& bytes)
            {
                WriteLe32(bytes, sourceGroupSlot(bytes, 2), 1);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidLODGraph);
        // グループの数以上の番号
        ExpectPagedMutation(
            [&](ByteArray& bytes)
            {
                WriteLe32(bytes, sourceGroupSlot(bytes, 1), 8);
                RefreshPageHash(bytes, 1);
            },
            CookedMeshParseStatus::InvalidLODGraph);
        // 同じ段のグループ（境界球が違う）・同じ段でないグループを指す
        ExpectPagedMutation(
            [&](ByteArray& bytes)
            {
                WriteLe32(bytes, sourceGroupSlot(bytes, 1), 2);
                RefreshPageHash(bytes, 1);
            },
            CookedMeshParseStatus::InvalidLODGraph);
        ExpectPagedMutation(
            [&](ByteArray& bytes)
            {
                WriteLe32(bytes, sourceGroupSlot(bytes, 1), 5);
                RefreshPageHash(bytes, 1);
            },
            CookedMeshParseStatus::InvalidLODGraph);

        // 子のページ: 同じ境界球・誤差の別グループ（グループ 0 とグループ 2。別のページ）を持つメッシュでも、親ごとの子のページは
        // 生成元のグループのページになる。値の照合なら決められないので、候補のページを全て固定する
        const CookedMeshParseResult numberedResult = ParseCookedMesh(MakeBlob(SerializePaged(input, options)));
        assert(numberedResult.Succeeded());
        Mega::MegaMeshCreateInfo createInfo;
        assert(Mega::BuildMegaMeshCreateInfoFromCookedMesh(numberedResult.Mesh, createInfo));
        VariableArray<Mega::MeshCluster>& clusters = createInfo.Clusters;
        VariableArray<Mega::MeshClusterGroup>& groups = createInfo.ClusterGroups;
        assert(groups.size() == 7 && groups[0].LODLevel == 0 && groups[2].LODLevel == 0);
        const uint32_t pageOfGroup0 = clusters[groups[0].ClusterOffset].PageId;
        const uint32_t pageOfGroup2 = clusters[groups[2].ClusterOffset].PageId;
        assert(pageOfGroup0 != pageOfGroup2);

        groups[2].Bounds = groups[0].Bounds;
        groups[2].Error = groups[0].Error;
        uint32_t parentOfGroup0 = ~0u;
        uint32_t parentOfGroup2 = ~0u;
        for (uint32_t index = 0; index < clusters.size(); ++index)
        {
            Mega::MeshCluster& cluster = clusters[index];
            if (cluster.SourceGroupId == 0 || cluster.SourceGroupId == 2)
            {
                (cluster.SourceGroupId == 0 ? parentOfGroup0 : parentOfGroup2) = index;
                cluster.Bounds = groups[0].Bounds;
                cluster.LODError = groups[0].Error;
            }
        }
        assert(parentOfGroup0 != ~0u && parentOfGroup2 != ~0u);

        {
            VariableArray<uint32_t> childPages;
            Mega::GeometryPageLinkResult links;
            assert(Mega::ComputeGeometryPageLinks(clusters, groups, childPages, links));
            assert(links.LinkedClusters == 7 && links.LinkedByIdClusters == 7);
            assert(links.AmbiguousClusters == 0 && links.SplitGroups == 0 && links.PinnedPages.empty());
            assert(childPages[parentOfGroup0] == pageOfGroup0 && childPages[parentOfGroup2] == pageOfGroup2);
            // 段1 以上の全クラスタは、作ったグループのメンバのページを子のページに持つ
            for (uint32_t index = 0; index < clusters.size(); ++index)
            {
                if (clusters[index].LODLevel == 0)
                {
                    assert(childPages[index] == Mega::INVALID_PAGE_ID);
                    continue;
                }
                assert(childPages[index] == clusters[groups[clusters[index].SourceGroupId].ClusterOffset].PageId);
            }
        }

        // 番号を外すと、値の照合（旧い資産）になる。同じ値の2つのグループが別のページなので、親は決められず、両方のページを固定する
        {
            VariableArray<Mega::MeshCluster> unnumbered = clusters;
            for (Mega::MeshCluster& cluster : unnumbered)
            {
                cluster.SourceGroupId = Mega::INVALID_CLUSTER_GROUP_ID;
            }
            VariableArray<uint32_t> childPages;
            Mega::GeometryPageLinkResult links;
            assert(Mega::ComputeGeometryPageLinks(unnumbered, groups, childPages, links));
            assert(links.LinkedByIdClusters == 0 && links.LinkedClusters == 7 && links.AmbiguousClusters == 2);
            assert(links.PinnedPages.size() == 2 && links.PinnedPages[0] == std::min(pageOfGroup0, pageOfGroup2) &&
                   links.PinnedPages[1] == std::max(pageOfGroup0, pageOfGroup2));
        }

        // 番号が一部のクラスタだけにあるときは、番号のあるクラスタは番号で、無いクラスタは値の照合で決める
        {
            VariableArray<Mega::MeshCluster> mixed = clusters;
            mixed[parentOfGroup2].SourceGroupId = Mega::INVALID_CLUSTER_GROUP_ID;
            VariableArray<uint32_t> childPages;
            Mega::GeometryPageLinkResult links;
            assert(Mega::ComputeGeometryPageLinks(mixed, groups, childPages, links));
            assert(links.LinkedByIdClusters == 6 && links.LinkedClusters == 7 && links.AmbiguousClusters == 1);
            assert(childPages[parentOfGroup0] == pageOfGroup0);
        }

        // 指す先のグループの段が合わない番号は使わず、値の照合へ戻る
        {
            VariableArray<Mega::MeshCluster> wrong = clusters;
            wrong[parentOfGroup0].SourceGroupId = 5;
            VariableArray<uint32_t> childPages;
            Mega::GeometryPageLinkResult links;
            assert(Mega::ComputeGeometryPageLinks(wrong, groups, childPages, links));
            assert(links.LinkedByIdClusters == 6 && links.AmbiguousClusters == 1);
        }
    }

    // v1.1 の壊れた表・壊れたページの拒否
    {
        // 表: ページの位置・大きさ・根の印・親のページ・件数
        ExpectPagedMutation(
            [](ByteArray& bytes)
            { WriteLe64(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::FileOffset, PagedPageOffset(bytes, 1) + 8); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            { WriteLe32(bytes, PagedTableRecord(bytes, 3) + V1::PageTableRecordOffset::Size, static_cast<uint32_t>(PagedPageSize(bytes, 3)) + 8); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 0) + V1::PageTableRecordOffset::Flags, 0); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            { WriteLe32(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::Flags, V1::PageFlagRoot); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::Flags, 2); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 0) + V1::PageTableRecordOffset::ParentPageId, 0); },
            CookedMeshParseStatus::InvalidPageTable);
        // 親のページが自分以降の番号・無効な番号
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::ParentPageId, 2); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            { WriteLe32(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::ParentPageId, V1::InvalidPageId); },
            CookedMeshParseStatus::InvalidPageTable);
        // 親のページが、自分と同じ段しか持たない（粗い段のクラスタが無い）
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 3) + V1::PageTableRecordOffset::ParentPageId, 2); },
            CookedMeshParseStatus::InvalidPageTable);
        // 親のページの番号が、先に並ぶページだが実際の親のクラスタを持つページと違う（段0 のページ 2 の親は、段1 のページ 1）
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::ParentPageId, 0); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::ParentPageId, 1); },
            CookedMeshParseStatus::InvalidPageTable);
        // 根のページが先頭から連続しない（根でないページの後ろに根のページ）
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::Flags, V1::PageFlagRoot);
                WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::ParentPageId, V1::InvalidPageId);
            },
            CookedMeshParseStatus::InvalidPageTable);
        // 件数: ヘッダとサブメッシュの頂点数・クラスタ数を揃えて巨大にしても、ページの領域に収まらない件数は、
        // 確保の前に拒否される（ファイル全体の hash は直す）
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                const size_t submeshOffset = static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::SubmeshTableOffset));
                WriteLe32(bytes, V1::HeaderOffset::VertexCount, 0xffffffffu);
                WriteLe32(bytes, submeshOffset + V1::SubmeshRecordOffset::VertexCount, 0xffffffffu);
            },
            CookedMeshParseStatus::InvalidCounts);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                const size_t submeshOffset = static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::SubmeshTableOffset));
                WriteLe32(bytes, V1::HeaderOffset::ClusterCount, 0x7fffffffu);
                WriteLe32(bytes, submeshOffset + V1::SubmeshRecordOffset::ClusterCount, 0x7fffffffu);
            },
            CookedMeshParseStatus::InvalidCounts);
        // クラスタの範囲・件数が、ページの中の件数と食い違う
        ExpectPagedMutation(
            [](ByteArray& bytes)
            { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::FirstClusterIndex, 6); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::ClusterCount, 5); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::ClusterCount, 0); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::VertexCount, 1); },
            CookedMeshParseStatus::InvalidPageTable);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe32(bytes, PagedTableRecord(bytes, 2) + V1::PageTableRecordOffset::Reserved0, 1); },
            CookedMeshParseStatus::ReservedFieldNonZero);
        // 表の節: 件数の端数・ページの領域の末尾に余り・インデックスの節に大きさ
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe64(bytes, V1::PagedHeaderOffset::PageTableSize, V1::PageTableRecordSize * 4 + 8); },
            CookedMeshParseStatus::InvalidCounts);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe64(bytes, V1::HeaderOffset::IndexPayloadSize, 8); },
            CookedMeshParseStatus::InvalidCounts);

        // ページの hash: ページの中身の1バイトを壊すと、ファイル全体の hash を直しても、ページの hash で拒否される
        ExpectPagedMutation(
            [](ByteArray& bytes) { bytes[PagedPageOffset(bytes, 2) + V1::PageHeaderSize + 3] ^= 0x40u; },
            CookedMeshParseStatus::PageHashMismatch);
        ExpectPagedMutation(
            [](ByteArray& bytes) { WriteLe64(bytes, PagedTableRecord(bytes, 1) + V1::PageTableRecordOffset::PageHash, 1); },
            CookedMeshParseStatus::PageHashMismatch);

        // ページの中身: ページの hash を直したうえで、ページの先頭・クラスタの記録・インデックスを壊す
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                WriteLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderOffset::PageId, 3);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidPageData);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                WriteLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderOffset::VertexOffset, 0);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidPageData);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                WriteLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderOffset::Reserved1, 1);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::ReservedFieldNonZero);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                // クラスタの PageId が、入っているページの番号と違う
                WriteLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderSize + V1::ClusterRecordOffset::PageId, 1);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidPageData);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                // ページの中の頂点の範囲が、ページの頂点数を超える
                WriteLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderSize + V1::ClusterRecordOffset::VertexOffset, 11);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidClusterRange);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                // クラスタのインデックスが、クラスタの頂点数以上
                const size_t indexOffset = PagedPageOffset(bytes, 2) +
                                           ReadLe32(bytes, PagedPageOffset(bytes, 2) + V1::PageHeaderOffset::IndexOffset);
                WriteLe32(bytes, indexOffset, 3);
                RefreshPageHash(bytes, 2);
            },
            CookedMeshParseStatus::InvalidClusterRange);
        ExpectPagedMutation(
            [](ByteArray& bytes)
            {
                // 根のページのフォールバックのインデックスが、根のページの頂点数以上
                const size_t fallbackOffset = PagedPageOffset(bytes, 0) +
                                              ReadLe32(bytes, PagedPageOffset(bytes, 0) + V1::PageHeaderOffset::FallbackIndexOffset);
                WriteLe32(bytes, fallbackOffset, 15);
                RefreshPageHash(bytes, 0);
            },
            CookedMeshParseStatus::InvalidIndexRange);
    }

    // ---- v1.1 のグループの BVH ----
    {
        // ページに詰めて書くと、グループの BVH が付く。グループ 7 つ（各 2 クラスタ）と根のクラスタ 1 つが葉の単位になり、
        // 8 つの葉を 1 つの内部の節（根）がまとめる（2 段・9 節）
        CookedMeshPagedWriteInfo info;
        const ByteArray bytes = SerializePaged(BuildPagedInput(), SmallPageOptions(), &info);
        assert(info.GroupBVHNodeCount == 9 && info.GroupBVHLeafCount == 8 && info.GroupBVHLevelCount == 2);
        assert((ReadLe32(bytes, V1::HeaderOffset::Flags) & V1::HeaderFlagGroupBVH) != 0);

        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(bytes));
        assert(result.Succeeded() && result.Mesh.FormatMinor == 1);
        const VariableArray<CookedMeshGroupBVHNode>& nodes = result.Mesh.GroupBVH;
        assert(nodes.size() == 9);
        assert(result.Mesh.GroupBVHLevelNodeCounts.size() == 2 && result.Mesh.GroupBVHLevelNodeCounts[0] == 1 &&
               result.Mesh.GroupBVHLevelNodeCounts[1] == 8);
        assert(!nodes[0].bLeaf && nodes[0].First == 1 && nodes[0].Count == 8);
        // 根のクラスタを含む葉があるので、根の節は親の誤差で枝を切れない（最大値）
        assert(nodes[0].MaxParentError == V1::RootParentError);

        // 葉は全クラスタをちょうど 1 回ずつ覆い、1 つのグループ（または根）のメンバだけを持つ。読み込んだ並びで確かめる
        VariableArray<uint32_t> coverage(result.Mesh.Clusters.size(), 0);
        uint32_t groupLeaves = 0;
        for (size_t nodeIndex = 1; nodeIndex < nodes.size(); ++nodeIndex)
        {
            const CookedMeshGroupBVHNode& node = nodes[nodeIndex];
            assert(node.bLeaf && node.Count >= 1 && node.Count <= V1::GroupBVHMaxLeafClusters);
            const CookedMeshCluster& first = result.Mesh.Clusters[node.First];
            for (uint32_t member = node.First; member < node.First + node.Count; ++member)
            {
                const CookedMeshCluster& cluster = result.Mesh.Clusters[member];
                assert(cluster.GroupId == first.GroupId && cluster.bIsRoot == first.bIsRoot);
                ++coverage[member];
                // 枝を切る条件が保守的: 葉の球はメンバの球と親の球を包み、親の誤差の最大はメンバ以上
                assert(nodes[0].BoundsRadius >= 0.0f && node.MaxParentError >= cluster.ParentError);
            }
            if (!first.bIsRoot)
            {
                ++groupLeaves;
                assert(node.MaxParentError == result.Mesh.Groups[first.GroupId].Error);
            }
        }
        for (const uint32_t count : coverage)
        {
            assert(count == 1);
        }
        assert(groupLeaves == 7);

        // 根の節の球は、全クラスタの球と親の球を包む
        for (const CookedMeshCluster& cluster : result.Mesh.Clusters)
        {
            const double dx = static_cast<double>(cluster.BoundsCenter.X) - nodes[0].BoundsCenter.X;
            const double dy = static_cast<double>(cluster.BoundsCenter.Y) - nodes[0].BoundsCenter.Y;
            const double dz = static_cast<double>(cluster.BoundsCenter.Z) - nodes[0].BoundsCenter.Z;
            assert(std::sqrt(dx * dx + dy * dy + dz * dz) + cluster.BoundsRadius <= nodes[0].BoundsRadius);
        }
    }

    // 多数のグループ（600 クラスタ・300 グループ・根 1）から作る BVH は、複数の段になり、同じ入力から同じ並びが出る
    {
        VariableArray<CookedMeshCluster> clusters;
        VariableArray<CookedMeshClusterGroup> groups;
        for (uint32_t groupIndex = 0; groupIndex < 300; ++groupIndex)
        {
            CookedMeshClusterGroup group;
            group.BoundsCenter = {static_cast<float>(groupIndex % 20) * 3.0f, static_cast<float>(groupIndex / 20) * 3.0f,
                                  static_cast<float>(groupIndex % 3)};
            group.BoundsRadius = 2.5f;
            group.Error = 0.01f * static_cast<float>(1 + groupIndex % 7);
            group.ClusterOffset = 2 * groupIndex;
            group.ClusterCount = 2;
            groups.push_back(group);
            for (uint32_t member = 0; member < 2; ++member)
            {
                CookedMeshCluster cluster;
                cluster.bIsRoot = false;
                cluster.GroupId = groupIndex;
                cluster.BoundsCenter = {group.BoundsCenter.X + (member == 0 ? -0.5f : 0.5f), group.BoundsCenter.Y,
                                        group.BoundsCenter.Z};
                cluster.BoundsRadius = 1.0f;
                cluster.ParentBoundsCenter = group.BoundsCenter;
                cluster.ParentBoundsRadius = group.BoundsRadius;
                cluster.ParentError = group.Error;
                clusters.push_back(cluster);
            }
        }
        CookedMeshCluster root;
        root.BoundsCenter = {30.0f, 20.0f, 1.0f};
        root.BoundsRadius = 60.0f;
        root.LODError = 1.0f;
        clusters.push_back(root);

        VariableArray<CookedMeshGroupBVHNode> nodes;
        assert(BuildCookedMeshGroupBVH(clusters, groups, nodes));
        VariableArray<CookedMeshGroupBVHNode> again;
        assert(BuildCookedMeshGroupBVH(clusters, groups, again));
        assert(nodes.size() == again.size());
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            assert(nodes[index].First == again[index].First && nodes[index].Count == again[index].Count &&
                   nodes[index].bLeaf == again[index].bLeaf && nodes[index].BoundsRadius == again[index].BoundsRadius &&
                   nodes[index].MaxParentError == again[index].MaxParentError);
        }

        VariableArray<uint32_t> levelCounts;
        assert(CheckCookedMeshGroupBVH(nodes, clusters, levelCounts) == CookedMeshParseStatus::Success);
        // 301 の葉を 8 分岐でまとめるので、根の下に 3 段以上の節がある
        assert(levelCounts.size() >= 3 && levelCounts[0] == 1 && levelCounts.size() <= V1::GroupBVHMaxLevels);
        uint32_t internalCount = 0;
        uint32_t leafCount = 0;
        for (const CookedMeshGroupBVHNode& node : nodes)
        {
            if (node.bLeaf)
            {
                ++leafCount;
            }
            else
            {
                ++internalCount;
                assert(node.Count >= 1 && node.Count <= V1::GroupBVHMaxChildren);
            }
        }
        assert(leafCount == 301 && internalCount + leafCount == nodes.size());

        // 親の誤差の最大は、下の葉の最大。誤差の小さい枝（根のクラスタを含まない節）は、より小さい値で切れる
        float smallest = V1::RootParentError;
        for (const CookedMeshGroupBVHNode& node : nodes)
        {
            smallest = std::min(smallest, node.MaxParentError);
        }
        assert(nodes[0].MaxParentError == V1::RootParentError && smallest == 0.01f);
        bool bPrunableInternal = false;
        for (const CookedMeshGroupBVHNode& node : nodes)
        {
            bPrunableInternal |= !node.bLeaf && node.MaxParentError < V1::RootParentError;
        }
        assert(bPrunableInternal);

        // 葉を持たない（クラスタが無い）入力や、範囲が表の外のグループは作らない
        VariableArray<CookedMeshGroupBVHNode> none;
        assert(!BuildCookedMeshGroupBVH(VariableArray<CookedMeshCluster>(), groups, none));
        VariableArray<CookedMeshClusterGroup> outOfRange = groups;
        outOfRange[5].ClusterOffset = 700;
        assert(!BuildCookedMeshGroupBVH(clusters, outOfRange, none));
    }

    // 壊れたグループの BVH の拒否
    {
        const auto bvhOffset = [](const ByteArray& bytes)
        {
            return static_cast<size_t>(ReadLe64(bytes, V1::HeaderOffset::GroupTableOffset)) +
                   static_cast<size_t>(ReadLe32(bytes, V1::HeaderOffset::GroupCount)) * V1::GroupRecordSize;
        };
        const auto nodeOffset = [&bvhOffset](const ByteArray& bytes, size_t nodeIndex)
        { return bvhOffset(bytes) + V1::GroupBVHHeaderSize + nodeIndex * V1::GroupBVHNodeRecordSize; };

        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, bvhOffset(bytes) + V1::GroupBVHHeaderOffset::NodeCount, 0); },
                            CookedMeshParseStatus::InvalidCounts);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, bvhOffset(bytes) + V1::GroupBVHHeaderOffset::NodeCount, 10); },
                            CookedMeshParseStatus::InvalidCounts);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, bvhOffset(bytes) + V1::GroupBVHHeaderOffset::Reserved0, 1); },
                            CookedMeshParseStatus::ReservedFieldNonZero);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, bvhOffset(bytes) + V1::GroupBVHHeaderOffset::LevelCount, 3); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, bvhOffset(bytes) + V1::GroupBVHHeaderOffset::LeafCount, 7); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        // 根の節の球が小さすぎて、子の球を包まない
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteFloat(bytes, nodeOffset(bytes, 0) + V1::GroupBVHNodeOffset::BoundsRadius, 0.5f); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        // 葉の球がメンバの球を包まない
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteFloat(bytes, nodeOffset(bytes, 2) + V1::GroupBVHNodeOffset::BoundsRadius, 0.1f); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        // 親の誤差の最大が、下より小さい（枝を切る条件が保守的でなくなる）
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteFloat(bytes, nodeOffset(bytes, 0) + V1::GroupBVHNodeOffset::MaxParentError, 0.05f); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        for (const size_t leafIndex : {size_t{1}, size_t{4}, size_t{8}})
        {
            ExpectPagedMutation(
                [&](ByteArray& bytes)
                { WriteFloat(bytes, nodeOffset(bytes, leafIndex) + V1::GroupBVHNodeOffset::MaxParentError, 0.0f); },
                CookedMeshParseStatus::InvalidGroupBVH);
        }
        // 子の位置が幅優先の並びと違う
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, nodeOffset(bytes, 0) + V1::GroupBVHNodeOffset::First, 2); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        // 葉が空・クラスタを二重に覆う・範囲が外
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, nodeOffset(bytes, 3) + V1::GroupBVHNodeOffset::Count, 0); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        ExpectPagedMutation(
            [&](ByteArray& bytes)
            {
                const uint32_t other = ReadLe32(bytes, nodeOffset(bytes, 2) + V1::GroupBVHNodeOffset::First);
                WriteLe32(bytes, nodeOffset(bytes, 3) + V1::GroupBVHNodeOffset::First, other);
            },
            CookedMeshParseStatus::InvalidGroupBVH);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, nodeOffset(bytes, 3) + V1::GroupBVHNodeOffset::First, 15); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        // 葉でない節の印を葉に付ける（子の範囲が空いている位置と合わなくなる）／予約のビット
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, nodeOffset(bytes, 5) + V1::GroupBVHNodeOffset::Flags, 0); },
                            CookedMeshParseStatus::InvalidGroupBVH);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteLe32(bytes, nodeOffset(bytes, 5) + V1::GroupBVHNodeOffset::Flags, 3); },
                            CookedMeshParseStatus::ReservedFieldNonZero);
        ExpectPagedMutation([&](ByteArray& bytes)
                            { WriteFloat(bytes, nodeOffset(bytes, 6) + V1::GroupBVHNodeOffset::BoundsRadius, -1.0f); },
                            CookedMeshParseStatus::InvalidFloatOrBounds);
        // ヘッダの Flags の未定義のビット／BVH の印だけ消す（節の大きさが合わなくなる）
        ExpectPagedMutation([&](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::Flags, 3); },
                            CookedMeshParseStatus::ReservedFieldNonZero);
        ExpectPagedMutation([&](ByteArray& bytes) { WriteLe32(bytes, V1::HeaderOffset::Flags, 0); },
                            CookedMeshParseStatus::InvalidCounts);
        // v1.0 は BVH を持てない
        {
            ByteArray bytes = SerializeV1(BuildV1Input());
            WriteLe32(bytes, V1::HeaderOffset::Flags, V1::HeaderFlagGroupBVH);
            RefreshPayloadHash(bytes);
            ExpectStatus(std::move(bytes), CookedMeshParseStatus::ReservedFieldNonZero);
        }
    }

    // v1.0 は v1.1 の読み込みを入れても従来どおりに読める（ページの表は空、副版は 0）
    {
        const CookedMeshParseResult result = ParseCookedMesh(MakeBlob(SerializeV1(BuildV1Input())));
        assert(result.Succeeded() && result.Mesh.FormatMajor == 1 && result.Mesh.FormatMinor == 0 &&
               result.Mesh.Pages.empty());
    }

    std::cout << "CookedMeshTest passed\n";
    return 0;
}
