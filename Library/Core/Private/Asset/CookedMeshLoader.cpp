#include "Asset/CookedMeshFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        struct SectionRange
        {
            uint64_t Offset = 0;
            uint64_t Size = 0;
            uint64_t End = 0;
        };

        uint16_t ReadLe16(const uint8_t* data, size_t offset)
        {
            return static_cast<uint16_t>(data[offset]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t* data, size_t offset)
        {
            return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t ReadLe64(const uint8_t* data, size_t offset)
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        float ReadLeFloat(const uint8_t* data, size_t offset)
        {
            return std::bit_cast<float>(ReadLe32(data, offset));
        }

        CookedMeshParseResult Fail(CookedMeshParseStatus status)
        {
            CookedMeshParseResult result;
            result.Status = status;
            return result;
        }

        bool AddChecked64(uint64_t left, uint64_t right, uint64_t& outValue)
        {
            if (right > std::numeric_limits<uint64_t>::max() - left)
            {
                return false;
            }

            outValue = left + right;
            return true;
        }

        bool MultiplyChecked64(uint64_t left, uint64_t right, uint64_t& outValue)
        {
            if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
            {
                return false;
            }

            outValue = left * right;
            return true;
        }

        bool AlignUpChecked64(uint64_t value, uint64_t alignment, uint64_t& outValue)
        {
            uint64_t adjusted = 0;
            if (!AddChecked64(value, alignment - 1, adjusted))
            {
                return false;
            }

            outValue = adjusted & ~(alignment - 1);
            return true;
        }

        bool HasExactMagic(Container::Span<const uint8_t> bytes)
        {
            return bytes.size() >= CookedMeshFormatV0::MagicSize &&
                   std::memcmp(bytes.data(), CookedMeshFormatV0::Magic, CookedMeshFormatV0::MagicSize) == 0;
        }

        bool IsFinite(CookedMeshFloat2 value)
        {
            return std::isfinite(value.U) && std::isfinite(value.V);
        }

        bool IsFinite(CookedMeshFloat3 value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        bool IsPrintableAscii(const uint8_t* data, size_t size)
        {
            for (size_t index = 0; index < size; ++index)
            {
                if (data[index] < 0x20u || data[index] > 0x7eu)
                {
                    return false;
                }
            }
            return true;
        }

        bool IsAsciiLetter(uint8_t value)
        {
            return (value >= static_cast<uint8_t>('A') && value <= static_cast<uint8_t>('Z')) ||
                   (value >= static_cast<uint8_t>('a') && value <= static_cast<uint8_t>('z'));
        }

        bool IsValidLogicalPath(const uint8_t* data, size_t size)
        {
            constexpr uint8_t assetsSegment[] = {'A', 's', 's', 'e', 't', 's'};

            if (size == 0 || data[0] == static_cast<uint8_t>('/'))
            {
                return false;
            }

            if (size >= sizeof(assetsSegment) && std::memcmp(data, assetsSegment, sizeof(assetsSegment)) == 0 &&
                (size == sizeof(assetsSegment) || data[sizeof(assetsSegment)] == static_cast<uint8_t>('/')))
            {
                return false;
            }

            if (size >= 2 && IsAsciiLetter(data[0]) && data[1] == static_cast<uint8_t>(':'))
            {
                return false;
            }

            size_t segmentStart = 0;
            for (size_t index = 0; index <= size; ++index)
            {
                if (index < size && data[index] == static_cast<uint8_t>('\\'))
                {
                    return false;
                }

                if (index == size || data[index] == static_cast<uint8_t>('/'))
                {
                    const size_t segmentLength = index - segmentStart;
                    if (segmentLength == 0 || (segmentLength == 1 && data[segmentStart] == static_cast<uint8_t>('.')) ||
                        (segmentLength == 2 && data[segmentStart] == static_cast<uint8_t>('.') &&
                         data[segmentStart + 1] == static_cast<uint8_t>('.')))
                    {
                        return false;
                    }
                    segmentStart = index + 1;
                }
            }

            return true;
        }

        CookedMeshParseStatus ParseStringReference(const uint8_t* data, size_t recordOffset, size_t stringTableOffset,
                                                   size_t stringTableSize, CookedMeshStringRef& outReference)
        {
            using namespace CookedMeshFormatV0;

            const uint64_t stringOffset = ReadLe64(data, recordOffset + StringRefRecordOffset::StringOffset);
            const uint32_t stringLength = ReadLe32(data, recordOffset + StringRefRecordOffset::StringLength);
            const uint32_t reserved0 = ReadLe32(data, recordOffset + StringRefRecordOffset::Reserved0);

            if (reserved0 != 0)
            {
                return CookedMeshParseStatus::ReservedFieldNonZero;
            }

            if (stringLength == 0)
            {
                if (stringOffset != 0)
                {
                    return CookedMeshParseStatus::InvalidMaterialTextureReference;
                }
                outReference = {};
                return CookedMeshParseStatus::Success;
            }

            uint64_t stringEnd = 0;
            if (!AddChecked64(stringOffset, stringLength, stringEnd))
            {
                return CookedMeshParseStatus::IntegerOverflow;
            }

            if (stringEnd > stringTableSize)
            {
                return CookedMeshParseStatus::InvalidMaterialTextureReference;
            }

            const size_t offset = static_cast<size_t>(stringOffset);
            const size_t length = static_cast<size_t>(stringLength);
            if (!IsValidLogicalPath(data + stringTableOffset + offset, length))
            {
                return CookedMeshParseStatus::InvalidPath;
            }

            outReference.StringOffset = offset;
            outReference.StringLength = length;
            return CookedMeshParseStatus::Success;
        }
    } // namespace

    Container::AnsiStringView CookedMeshData::GetString(const CookedMeshStringRef& stringRef) const noexcept
    {
        if (stringRef.StringLength == 0)
        {
            return {};
        }

        if (!SourceBlob.IsValid() || StringTableOffset > SourceBlob.GetSize() ||
            StringTableSize > SourceBlob.GetSize() - StringTableOffset || stringRef.StringOffset > StringTableSize ||
            stringRef.StringLength > StringTableSize - stringRef.StringOffset)
        {
            return {};
        }

        const uint8_t* stringData = SourceBlob.GetData() + StringTableOffset + stringRef.StringOffset;
        return Container::AnsiStringView(reinterpret_cast<const char*>(stringData), stringRef.StringLength);
    }

    // v0 の読み込み。ParseCookedMesh が magic で振り分ける。
    static CookedMeshParseResult ParseCookedMeshV0(AssetBlob sourceBlob)
    {
        using namespace CookedMeshFormatV0;

        if (!sourceBlob.IsValid())
        {
            return Fail(CookedMeshParseStatus::InvalidBlob);
        }

        const Container::Span<const uint8_t> bytes = sourceBlob.GetSpan();
        if (bytes.empty())
        {
            return Fail(CookedMeshParseStatus::EmptyBlob);
        }

        if (bytes.size() < HeaderSize)
        {
            return Fail(CookedMeshParseStatus::HeaderTooSmall);
        }

        if (!HasExactMagic(bytes))
        {
            return Fail(CookedMeshParseStatus::BadMagic);
        }

        const uint8_t* data = bytes.data();
        const uint32_t headerSize = ReadLe32(data, HeaderOffset::HeaderSize);
        const uint16_t versionMajor = ReadLe16(data, HeaderOffset::VersionMajor);
        const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
        const uint32_t endianMarker = ReadLe32(data, HeaderOffset::EndianMarker);
        const uint32_t vertexRecordSize = ReadLe32(data, HeaderOffset::VertexRecordSize);
        const uint32_t submeshRecordSize = ReadLe32(data, HeaderOffset::SubmeshRecordSize);
        const uint32_t materialRecordSize = ReadLe32(data, HeaderOffset::MaterialRecordSize);
        const uint32_t clusterRecordSize = ReadLe32(data, HeaderOffset::ClusterRecordSize);
        const uint32_t stringRefRecordSize = ReadLe32(data, HeaderOffset::StringRefRecordSize);
        const uint64_t declaredFileSize = ReadLe64(data, HeaderOffset::FileSize);
        const uint64_t payloadHash = ReadLe64(data, HeaderOffset::PayloadHash);
        const uint32_t vertexCount = ReadLe32(data, HeaderOffset::VertexCount);
        const uint32_t indexCount = ReadLe32(data, HeaderOffset::IndexCount);
        const uint32_t submeshCount = ReadLe32(data, HeaderOffset::SubmeshCount);
        const uint32_t materialCount = ReadLe32(data, HeaderOffset::MaterialCount);
        const uint32_t clusterCount = ReadLe32(data, HeaderOffset::ClusterCount);
        const uint32_t stringByteCount = ReadLe32(data, HeaderOffset::StringByteCount);

        if (versionMajor != VersionMajor || versionMinor != VersionMinor)
        {
            return Fail(CookedMeshParseStatus::UnsupportedVersion);
        }

        if (endianMarker != EndianMarker)
        {
            return Fail(CookedMeshParseStatus::EndianMismatch);
        }

        if (headerSize != HeaderSize)
        {
            return Fail(CookedMeshParseStatus::HeaderSizeMismatch);
        }

        if (vertexRecordSize != VertexRecordSize || submeshRecordSize != SubmeshRecordSize ||
            materialRecordSize != MaterialRecordSize || clusterRecordSize != ClusterRecordSize ||
            stringRefRecordSize != StringRefRecordSize)
        {
            return Fail(CookedMeshParseStatus::RecordSizeMismatch);
        }

        if (declaredFileSize != static_cast<uint64_t>(bytes.size()))
        {
            return Fail(CookedMeshParseStatus::FileSizeMismatch);
        }

        if (ReadLe32(data, HeaderOffset::Flags) != 0 || ReadLe64(data, HeaderOffset::Reserved0) != 0 ||
            ReadLe64(data, HeaderOffset::Reserved1) != 0 || ReadLe64(data, HeaderOffset::Reserved2) != 0 ||
            ReadLe64(data, HeaderOffset::Reserved3) != 0 || ReadLe64(data, HeaderOffset::Reserved4) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

        if (submeshCount != 1 || materialCount != 1 || clusterCount == 0 || indexCount % 3 != 0)
        {
            return Fail(CookedMeshParseStatus::InvalidCounts);
        }

        SectionRange sections[] = {
            {ReadLe64(data, HeaderOffset::SubmeshTableOffset), ReadLe64(data, HeaderOffset::SubmeshTableSize)},
            {ReadLe64(data, HeaderOffset::MaterialTableOffset), ReadLe64(data, HeaderOffset::MaterialTableSize)},
            {ReadLe64(data, HeaderOffset::ClusterTableOffset), ReadLe64(data, HeaderOffset::ClusterTableSize)},
            {ReadLe64(data, HeaderOffset::StringTableOffset), ReadLe64(data, HeaderOffset::StringTableSize)},
            {ReadLe64(data, HeaderOffset::VertexPayloadOffset), ReadLe64(data, HeaderOffset::VertexPayloadSize)},
            {ReadLe64(data, HeaderOffset::IndexPayloadOffset), ReadLe64(data, HeaderOffset::IndexPayloadSize)}};

        uint64_t expectedSubmeshSize = 0;
        uint64_t expectedMaterialSize = 0;
        uint64_t expectedClusterSize = 0;
        uint64_t expectedVertexSize = 0;
        uint64_t expectedIndexSize = 0;
        if (!MultiplyChecked64(submeshCount, SubmeshRecordSize, expectedSubmeshSize) ||
            !MultiplyChecked64(materialCount, MaterialRecordSize, expectedMaterialSize) ||
            !MultiplyChecked64(clusterCount, ClusterRecordSize, expectedClusterSize) ||
            !MultiplyChecked64(vertexCount, VertexRecordSize, expectedVertexSize) ||
            !MultiplyChecked64(indexCount, sizeof(uint32_t), expectedIndexSize))
        {
            return Fail(CookedMeshParseStatus::IntegerOverflow);
        }

        if (sections[0].Size != expectedSubmeshSize || sections[1].Size != expectedMaterialSize ||
            sections[2].Size != expectedClusterSize || sections[3].Size != stringByteCount ||
            sections[4].Size != expectedVertexSize || sections[5].Size != expectedIndexSize)
        {
            return Fail(CookedMeshParseStatus::InvalidCounts);
        }

        for (SectionRange& section : sections)
        {
            if (!AddChecked64(section.Offset, section.Size, section.End))
            {
                return Fail(CookedMeshParseStatus::IntegerOverflow);
            }
        }

        for (const SectionRange& section : sections)
        {
            if (section.Offset < HeaderSize || section.End > declaredFileSize)
            {
                return Fail(CookedMeshParseStatus::SectionOutOfRange);
            }
        }

        for (const SectionRange& section : sections)
        {
            if (section.Offset % SectionAlignment != 0)
            {
                return Fail(CookedMeshParseStatus::SectionMisalignment);
            }
        }

        uint64_t cursor = HeaderSize;
        for (const SectionRange& section : sections)
        {
            uint64_t expectedOffset = 0;
            if (!AlignUpChecked64(cursor, SectionAlignment, expectedOffset))
            {
                return Fail(CookedMeshParseStatus::IntegerOverflow);
            }

            if (section.Offset != expectedOffset)
            {
                return Fail(CookedMeshParseStatus::SectionPackingMismatch);
            }

            for (uint64_t paddingOffset = cursor; paddingOffset < section.Offset; ++paddingOffset)
            {
                if (data[static_cast<size_t>(paddingOffset)] != 0)
                {
                    return Fail(CookedMeshParseStatus::PaddingByteNonZero);
                }
            }
            cursor = section.End;
        }

        if (sections[5].End != declaredFileSize)
        {
            return Fail(CookedMeshParseStatus::FileSizeMismatch);
        }

        const size_t payloadOffset = static_cast<size_t>(sections[0].Offset);
        const size_t payloadSize = static_cast<size_t>(sections[5].End - sections[0].Offset);
        if (ComputeCookedMeshPayloadHash(data + payloadOffset, payloadSize) != payloadHash)
        {
            return Fail(CookedMeshParseStatus::PayloadHashMismatch);
        }

        const CookedMeshFloat3 totalBoundsCenter = {ReadLeFloat(data, HeaderOffset::TotalBoundsCenterX),
                                                    ReadLeFloat(data, HeaderOffset::TotalBoundsCenterY),
                                                    ReadLeFloat(data, HeaderOffset::TotalBoundsCenterZ)};
        const float totalBoundsRadius = ReadLeFloat(data, HeaderOffset::TotalBoundsRadius);
        if (!IsFinite(totalBoundsCenter) || !std::isfinite(totalBoundsRadius) || totalBoundsRadius < 0.0f)
        {
            return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
        }

        if (ReadLe32(data, HeaderOffset::ClusterAlgorithmId) != ClusterAlgorithmId ||
            ReadLe32(data, HeaderOffset::ClusterAlgorithmVersion) != ClusterAlgorithmVersion ||
            ReadLe32(data, HeaderOffset::ClusterMaxTriangles) != ClusterMaxTriangles ||
            ReadLe32(data, HeaderOffset::ClusterMaxVertices) != ClusterMaxVertices ||
            ReadLe32(data, HeaderOffset::ClusterSettingsFlags) != ClusterSettingsFlags)
        {
            return Fail(CookedMeshParseStatus::UnsupportedV0Feature);
        }

        const size_t submeshTableOffset = static_cast<size_t>(sections[0].Offset);
        CookedMeshSubmesh submesh;
        submesh.IndexOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::IndexOffset);
        submesh.IndexCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::IndexCount);
        submesh.VertexOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::VertexOffset);
        submesh.VertexCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::VertexCount);
        submesh.MaterialIndex = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::MaterialIndex);
        submesh.ClusterOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::ClusterOffset);
        submesh.ClusterCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::ClusterCount);
        submesh.BoundsCenter = {ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX),
                                ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterY),
                                ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterZ)};
        submesh.BoundsRadius = ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsRadius);

        if (ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::Flags) != 0 ||
            ReadLe64(data, submeshTableOffset + SubmeshRecordOffset::Reserved0) != 0 ||
            ReadLe64(data, submeshTableOffset + SubmeshRecordOffset::Reserved1) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

        if (!IsFinite(submesh.BoundsCenter) || !std::isfinite(submesh.BoundsRadius) || submesh.BoundsRadius < 0.0f)
        {
            return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
        }

        if (submesh.VertexOffset != 0 || submesh.MaterialIndex != 0)
        {
            return Fail(CookedMeshParseStatus::UnsupportedV0Feature);
        }

        const uint64_t submeshIndexEnd = static_cast<uint64_t>(submesh.IndexOffset) + submesh.IndexCount;
        if (submesh.IndexOffset % 3 != 0 || submesh.IndexCount % 3 != 0 || submeshIndexEnd > indexCount ||
            submesh.IndexOffset != 0 || submesh.IndexCount != indexCount ||
            (submesh.VertexCount != 0 && submesh.VertexCount > vertexCount))
        {
            return Fail(CookedMeshParseStatus::InvalidIndexRange);
        }

        const uint64_t submeshClusterEnd = static_cast<uint64_t>(submesh.ClusterOffset) + submesh.ClusterCount;
        if (submeshClusterEnd > clusterCount || submesh.ClusterOffset != 0 || submesh.ClusterCount != clusterCount)
        {
            return Fail(CookedMeshParseStatus::InvalidClusterRange);
        }

        const size_t stringTableOffset = static_cast<size_t>(sections[3].Offset);
        const size_t stringTableSize = static_cast<size_t>(sections[3].Size);
        if (!IsPrintableAscii(data + stringTableOffset, stringTableSize))
        {
            return Fail(CookedMeshParseStatus::InvalidStringTable);
        }

        const size_t materialTableOffset = static_cast<size_t>(sections[1].Offset);
        if (ReadLe32(data, materialTableOffset + MaterialRecordOffset::Flags) != 0 ||
            ReadLe32(data, materialTableOffset + MaterialRecordOffset::Reserved0) != 0 ||
            ReadLe64(data, materialTableOffset + MaterialRecordOffset::Reserved1) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

        CookedMeshMaterial material;
        CookedMeshParseStatus stringStatus =
            ParseStringReference(data, materialTableOffset + MaterialRecordOffset::AlbedoTexture, stringTableOffset,
                                 stringTableSize, material.AlbedoTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }
        stringStatus = ParseStringReference(data, materialTableOffset + MaterialRecordOffset::NormalTexture,
                                            stringTableOffset, stringTableSize, material.NormalTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }
        stringStatus = ParseStringReference(data, materialTableOffset + MaterialRecordOffset::ArmTexture,
                                            stringTableOffset, stringTableSize, material.ArmTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }

        Container::VariableArray<CookedMeshCluster> clusters;
        clusters.reserve(clusterCount);
        const size_t clusterTableOffset = static_cast<size_t>(sections[2].Offset);
        for (uint32_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            const size_t recordOffset = clusterTableOffset + static_cast<size_t>(clusterIndex) * ClusterRecordSize;
            CookedMeshCluster cluster;
            cluster.BoundsCenter = {ReadLeFloat(data, recordOffset + ClusterRecordOffset::BoundsCenterX),
                                    ReadLeFloat(data, recordOffset + ClusterRecordOffset::BoundsCenterY),
                                    ReadLeFloat(data, recordOffset + ClusterRecordOffset::BoundsCenterZ)};
            cluster.BoundsRadius = ReadLeFloat(data, recordOffset + ClusterRecordOffset::BoundsRadius);
            cluster.ConeAxis = {ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisX),
                                ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisY),
                                ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisZ)};
            cluster.ConeCutoff = ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeCutoff);
            cluster.IndexOffset = ReadLe32(data, recordOffset + ClusterRecordOffset::IndexOffset);
            cluster.IndexCount = ReadLe32(data, recordOffset + ClusterRecordOffset::IndexCount);
            cluster.VertexOffset = ReadLe32(data, recordOffset + ClusterRecordOffset::VertexOffset);
            cluster.VertexCount = ReadLe32(data, recordOffset + ClusterRecordOffset::VertexCount);
            cluster.MaterialIndex = ReadLe32(data, recordOffset + ClusterRecordOffset::MaterialIndex);
            cluster.LODLevel = ReadLe32(data, recordOffset + ClusterRecordOffset::LODLevel);
            cluster.LODError = ReadLeFloat(data, recordOffset + ClusterRecordOffset::LODError);
            cluster.ParentStart = ReadLe32(data, recordOffset + ClusterRecordOffset::ParentStart);
            cluster.ParentCount = ReadLe32(data, recordOffset + ClusterRecordOffset::ParentCount);

            if (ReadLe32(data, recordOffset + ClusterRecordOffset::Flags) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved0) != 0)
            {
                return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
            }

            if (!IsFinite(cluster.BoundsCenter) || !std::isfinite(cluster.BoundsRadius) ||
                cluster.BoundsRadius < 0.0f || !IsFinite(cluster.ConeAxis) || !std::isfinite(cluster.ConeCutoff) ||
                !std::isfinite(cluster.LODError))
            {
                return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
            }

            if (cluster.VertexOffset != 0 || cluster.MaterialIndex != 0 || cluster.LODLevel != 0 ||
                cluster.LODError != 0.0f || cluster.ParentStart != 0 || cluster.ParentCount != 0)
            {
                return Fail(CookedMeshParseStatus::UnsupportedV0Feature);
            }

            const uint64_t clusterIndexEnd = static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount;
            if (cluster.IndexOffset % 3 != 0 || cluster.IndexCount % 3 != 0 || clusterIndexEnd > indexCount ||
                (cluster.VertexCount != 0 && cluster.VertexCount > vertexCount))
            {
                return Fail(CookedMeshParseStatus::InvalidClusterRange);
            }

            clusters.push_back(cluster);
        }

        Container::VariableArray<CookedMeshVertex> vertices;
        vertices.reserve(vertexCount);
        const size_t vertexPayloadOffset = static_cast<size_t>(sections[4].Offset);
        for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            const size_t recordOffset = vertexPayloadOffset + static_cast<size_t>(vertexIndex) * VertexRecordSize;
            CookedMeshVertex vertex;
            vertex.Position = {ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionX),
                               ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionY),
                               ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionZ)};
            vertex.Normal = {ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalX),
                             ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalY),
                             ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalZ)};
            vertex.TexCoord = {ReadLeFloat(data, recordOffset + VertexRecordOffset::TexCoordU),
                               ReadLeFloat(data, recordOffset + VertexRecordOffset::TexCoordV)};

            if (!IsFinite(vertex.Position) || !IsFinite(vertex.Normal) || !IsFinite(vertex.TexCoord))
            {
                return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
            }
            vertices.push_back(vertex);
        }

        Container::VariableArray<uint32_t> indices;
        indices.reserve(indexCount);
        const size_t indexPayloadOffset = static_cast<size_t>(sections[5].Offset);
        for (uint32_t index = 0; index < indexCount; ++index)
        {
            const uint32_t vertexIndex =
                ReadLe32(data, indexPayloadOffset + static_cast<size_t>(index) * sizeof(uint32_t));
            if (vertexIndex >= vertexCount)
            {
                return Fail(CookedMeshParseStatus::InvalidIndexRange);
            }
            indices.push_back(vertexIndex);
        }

        if (submesh.VertexCount != 0)
        {
            for (uint32_t index = submesh.IndexOffset; index < submeshIndexEnd; ++index)
            {
                if (indices[index] >= submesh.VertexCount)
                {
                    return Fail(CookedMeshParseStatus::InvalidIndexRange);
                }
            }
        }

        uint32_t expectedClusterIndexOffset = 0;
        for (const CookedMeshCluster& cluster : clusters)
        {
            if (cluster.IndexOffset != expectedClusterIndexOffset)
            {
                return Fail(CookedMeshParseStatus::InvalidClusterRange);
            }

            if (cluster.VertexCount != 0)
            {
                const uint32_t clusterIndexEnd = cluster.IndexOffset + cluster.IndexCount;
                for (uint32_t index = cluster.IndexOffset; index < clusterIndexEnd; ++index)
                {
                    if (indices[index] >= cluster.VertexCount)
                    {
                        return Fail(CookedMeshParseStatus::InvalidClusterRange);
                    }
                }
            }
            expectedClusterIndexOffset += cluster.IndexCount;
        }

        if (expectedClusterIndexOffset != indexCount)
        {
            return Fail(CookedMeshParseStatus::InvalidClusterRange);
        }

        CookedMeshParseResult result;
        result.Status = CookedMeshParseStatus::Success;
        result.Mesh.SourceBlob = std::move(sourceBlob);
        result.Mesh.FormatMajor = 0;
        result.Mesh.TotalBoundsCenter = totalBoundsCenter;
        result.Mesh.TotalBoundsRadius = totalBoundsRadius;
        result.Mesh.PayloadHash = payloadHash;
        result.Mesh.StringTableOffset = stringTableOffset;
        result.Mesh.StringTableSize = stringTableSize;
        // v0 は LOD の階層を持たない1段のメッシュ。全体のインデックスの範囲をそのまま粗い段として扱う
        result.Mesh.LODLevelCount = 1;
        result.Mesh.FallbackIndexOffset = 0;
        result.Mesh.FallbackIndexCount = indexCount;
        result.Mesh.FallbackError = 0.0f;
        result.Mesh.Vertices = std::move(vertices);
        result.Mesh.Submeshes.push_back(submesh);
        result.Mesh.Materials.push_back(material);
        result.Mesh.Clusters = std::move(clusters);
        result.Mesh.Indices = std::move(indices);
        return result;
    }

    namespace
    {
        // 内側の球が外側の球に（浮動小数の丸めの分だけ余裕を見て）収まっているか
        bool IsSphereContained(CookedMeshFloat3 innerCenter, float innerRadius, CookedMeshFloat3 outerCenter,
                               float outerRadius)
        {
            const double dx = static_cast<double>(innerCenter.X) - static_cast<double>(outerCenter.X);
            const double dy = static_cast<double>(innerCenter.Y) - static_cast<double>(outerCenter.Y);
            const double dz = static_cast<double>(innerCenter.Z) - static_cast<double>(outerCenter.Z);
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double slack =
                1.0e-4 * (distance + static_cast<double>(innerRadius) + static_cast<double>(outerRadius)) + 1.0e-6;
            return distance + static_cast<double>(innerRadius) <= static_cast<double>(outerRadius) + slack;
        }

        bool IsZero(CookedMeshFloat3 value)
        {
            return value.X == 0.0f && value.Y == 0.0f && value.Z == 0.0f;
        }

        bool IsSameFloat3(CookedMeshFloat3 left, CookedMeshFloat3 right)
        {
            return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
        }
    } // namespace

    namespace
    {
        // v1 のクラスタのレコード（128B）1件を読み、記録の中で閉じる検査をする。v1.0 のクラスタの表も v1.1 のページの中も
        // 同じ並び。vertexLimit はクラスタの頂点の範囲が収まるべき頂点数（v1.0 は全体、v1.1 はページの中）。
        // PageId は expectedPageId と一致しなければならず、違うときは pageMismatchStatus を返す。
        CookedMeshParseStatus ReadV1ClusterRecord(const uint8_t* data, size_t recordOffset, uint32_t lodLevelCount,
                                                  uint32_t groupCount, uint64_t vertexLimit, uint32_t expectedPageId,
                                                  CookedMeshParseStatus pageMismatchStatus,
                                                  CookedMeshCluster& outCluster)
        {
            using namespace CookedMeshFormatV1;

            CookedMeshCluster cluster;
            cluster.BoundsCenter = {ReadLeFloat(data, recordOffset + ClusterRecordOffset::SelfCenterX),
                                    ReadLeFloat(data, recordOffset + ClusterRecordOffset::SelfCenterY),
                                    ReadLeFloat(data, recordOffset + ClusterRecordOffset::SelfCenterZ)};
            cluster.BoundsRadius = ReadLeFloat(data, recordOffset + ClusterRecordOffset::SelfRadius);
            cluster.LODError = ReadLeFloat(data, recordOffset + ClusterRecordOffset::SelfError);
            cluster.GroupId = ReadLe32(data, recordOffset + ClusterRecordOffset::GroupId);
            cluster.ParentBoundsCenter = {ReadLeFloat(data, recordOffset + ClusterRecordOffset::ParentCenterX),
                                          ReadLeFloat(data, recordOffset + ClusterRecordOffset::ParentCenterY),
                                          ReadLeFloat(data, recordOffset + ClusterRecordOffset::ParentCenterZ)};
            cluster.ParentBoundsRadius = ReadLeFloat(data, recordOffset + ClusterRecordOffset::ParentRadius);
            cluster.ParentError = ReadLeFloat(data, recordOffset + ClusterRecordOffset::ParentError);
            cluster.LODLevel = ReadLe32(data, recordOffset + ClusterRecordOffset::LODLevel);
            cluster.ConeAxis = {ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisX),
                                ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisY),
                                ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeAxisZ)};
            cluster.ConeCutoff = ReadLeFloat(data, recordOffset + ClusterRecordOffset::ConeCutoff);
            cluster.IndexOffset = ReadLe32(data, recordOffset + ClusterRecordOffset::IndexOffset);
            cluster.IndexCount = ReadLe32(data, recordOffset + ClusterRecordOffset::IndexCount);
            cluster.VertexOffset = ReadLe32(data, recordOffset + ClusterRecordOffset::VertexOffset);
            cluster.VertexCount = ReadLe32(data, recordOffset + ClusterRecordOffset::VertexCount);
            cluster.MaterialIndex = ReadLe32(data, recordOffset + ClusterRecordOffset::MaterialIndex);
            cluster.PageId = ReadLe32(data, recordOffset + ClusterRecordOffset::PageId);
            const uint32_t flags = ReadLe32(data, recordOffset + ClusterRecordOffset::Flags);
            cluster.bIsRoot = (flags & ClusterFlagRoot) != 0;
            // 記録は「番号 + 1」で、0 が番号なし。符号なしの折り返しで 0 が InvalidGroupId になる
            cluster.SourceGroupId = ReadLe32(data, recordOffset + ClusterRecordOffset::SourceGroupIdPlusOne) - 1u;

            if ((flags & ~ClusterFlagRoot) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved1) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved2) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved3) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved4) != 0)
            {
                return CookedMeshParseStatus::ReservedFieldNonZero;
            }

            if (!IsFinite(cluster.BoundsCenter) || !std::isfinite(cluster.BoundsRadius) ||
                cluster.BoundsRadius < 0.0f || !std::isfinite(cluster.LODError) || cluster.LODError < 0.0f ||
                !IsFinite(cluster.ParentBoundsCenter) || !std::isfinite(cluster.ParentBoundsRadius) ||
                cluster.ParentBoundsRadius < 0.0f || !std::isfinite(cluster.ParentError) ||
                cluster.ParentError < 0.0f || !IsFinite(cluster.ConeAxis) || !std::isfinite(cluster.ConeCutoff))
            {
                return CookedMeshParseStatus::InvalidFloatOrBounds;
            }

            // 材質は 1 つだけ。ページの番号は、v1.0 は 0 固定、v1.1 は自分が入っているページの番号
            if (cluster.MaterialIndex != 0)
            {
                return CookedMeshParseStatus::UnsupportedV1Feature;
            }
            if (cluster.PageId != expectedPageId)
            {
                return pageMismatchStatus;
            }

            if (cluster.LODLevel >= lodLevelCount)
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            if (cluster.IndexCount == 0 || cluster.IndexCount % 3 != 0 ||
                cluster.IndexCount / 3 > ClusterMaxTriangles || cluster.VertexCount == 0 ||
                cluster.VertexCount > ClusterMaxVertices ||
                static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount > vertexLimit)
            {
                return CookedMeshParseStatus::InvalidClusterRange;
            }

            // 根 ⇔ グループが無い ⇔ 親の誤差が最大値。この3つが食い違う記録は壊れている
            const bool hasNoGroup = cluster.GroupId == InvalidGroupId;
            const bool hasRootError = cluster.ParentError == RootParentError;
            if (cluster.bIsRoot != hasNoGroup || cluster.bIsRoot != hasRootError)
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            // 根は最も粗い段にだけ存在する。根でないクラスタは親の段（自分の段 + 1）が必ず存在する
            if (cluster.bIsRoot != (cluster.LODLevel + 1 == lodLevelCount))
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            if (cluster.bIsRoot)
            {
                if (!IsZero(cluster.ParentBoundsCenter) || cluster.ParentBoundsRadius != 0.0f)
                {
                    return CookedMeshParseStatus::InvalidLODGraph;
                }
            }
            else if (cluster.GroupId >= groupCount || cluster.ParentError < cluster.LODError)
            {
                // 親の誤差は自分の誤差以上（階層をたどると誤差が単調に増える）
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            // 最も細かい段（元の形）の誤差は 0
            if (cluster.LODLevel == 0 && cluster.LODError != 0.0f)
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            // 作ったグループの番号は、1つ細かい段のグループを指す（最も細かい段のクラスタは作られたものではない）。
            // グループの表との一致（段・境界球・誤差）は、グループの表を読む ReadV1Groups が検査する
            if (cluster.SourceGroupId != InvalidGroupId && (cluster.LODLevel == 0 || cluster.SourceGroupId >= groupCount))
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            outCluster = cluster;
            return CookedMeshParseStatus::Success;
        }

        // 根の数を数え、段の構成（根がある・空の段が無い）を検査する
        CookedMeshParseStatus CheckV1LevelStructure(const Container::VariableArray<CookedMeshCluster>& clusters,
                                                    uint32_t lodLevelCount, uint32_t& outRootCount)
        {
            Container::VariableArray<uint32_t> levelClusterCounts(lodLevelCount, 0);
            uint32_t rootCount = 0;
            for (const CookedMeshCluster& cluster : clusters)
            {
                ++levelClusterCounts[cluster.LODLevel];
                if (cluster.bIsRoot)
                {
                    ++rootCount;
                }
            }

            if (rootCount == 0)
            {
                return CookedMeshParseStatus::InvalidLODGraph;
            }

            // 宣言した段数と実際の段構成が一致する（空の段は無い）
            for (const uint32_t levelClusterCount : levelClusterCounts)
            {
                if (levelClusterCount == 0)
                {
                    return CookedMeshParseStatus::InvalidLODGraph;
                }
            }

            outRootCount = rootCount;
            return CookedMeshParseStatus::Success;
        }

        // グループの表を読み、メンバのクラスタとの整合を検査する
        CookedMeshParseStatus ReadV1Groups(const uint8_t* data, size_t groupTableOffset, uint32_t groupCount,
                                           const Container::VariableArray<CookedMeshCluster>& clusters,
                                           uint32_t lodLevelCount, uint32_t rootCount,
                                           Container::VariableArray<CookedMeshClusterGroup>& outGroups)
        {
            using namespace CookedMeshFormatV1;

            const uint64_t clusterCount = clusters.size();
            outGroups.reserve(groupCount);
            uint64_t memberTotal = 0;
            for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
            {
                const size_t recordOffset = groupTableOffset + static_cast<size_t>(groupIndex) * GroupRecordSize;
                CookedMeshClusterGroup group;
                group.BoundsCenter = {ReadLeFloat(data, recordOffset + GroupRecordOffset::BoundsCenterX),
                                      ReadLeFloat(data, recordOffset + GroupRecordOffset::BoundsCenterY),
                                      ReadLeFloat(data, recordOffset + GroupRecordOffset::BoundsCenterZ)};
                group.BoundsRadius = ReadLeFloat(data, recordOffset + GroupRecordOffset::BoundsRadius);
                group.Error = ReadLeFloat(data, recordOffset + GroupRecordOffset::Error);
                group.ClusterOffset = ReadLe32(data, recordOffset + GroupRecordOffset::ClusterOffset);
                group.ClusterCount = ReadLe32(data, recordOffset + GroupRecordOffset::ClusterCount);
                group.LODLevel = ReadLe32(data, recordOffset + GroupRecordOffset::LODLevel);

                if (ReadLe32(data, recordOffset + GroupRecordOffset::Flags) != 0 ||
                    ReadLe32(data, recordOffset + GroupRecordOffset::Reserved0) != 0 ||
                    ReadLe64(data, recordOffset + GroupRecordOffset::Reserved1) != 0)
                {
                    return CookedMeshParseStatus::ReservedFieldNonZero;
                }

                if (!IsFinite(group.BoundsCenter) || !std::isfinite(group.BoundsRadius) || group.BoundsRadius < 0.0f ||
                    !std::isfinite(group.Error) || group.Error < 0.0f || group.Error >= RootParentError)
                {
                    return CookedMeshParseStatus::InvalidFloatOrBounds;
                }

                if (group.ClusterCount == 0 ||
                    static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount > clusterCount ||
                    group.LODLevel >= lodLevelCount)
                {
                    return CookedMeshParseStatus::InvalidGroupTable;
                }

                for (uint32_t memberIndex = group.ClusterOffset;
                     memberIndex < group.ClusterOffset + group.ClusterCount; ++memberIndex)
                {
                    const CookedMeshCluster& member = clusters[memberIndex];
                    if (member.bIsRoot || member.GroupId != groupIndex || member.LODLevel != group.LODLevel ||
                        !IsSameFloat3(member.ParentBoundsCenter, group.BoundsCenter) ||
                        member.ParentBoundsRadius != group.BoundsRadius || member.ParentError != group.Error ||
                        !IsSphereContained(member.BoundsCenter, member.BoundsRadius, group.BoundsCenter,
                                           group.BoundsRadius))
                    {
                        return CookedMeshParseStatus::InvalidLODGraph;
                    }
                }

                memberTotal += group.ClusterCount;
                outGroups.push_back(group);
            }

            // 根でないクラスタは、ちょうど1つのグループのメンバ（メンバは自分の GroupId のグループにだけ並ぶので、
            // メンバの総数が根以外の数に一致すれば、漏れも重複も無い）
            if (memberTotal != clusterCount - rootCount)
            {
                return CookedMeshParseStatus::InvalidGroupTable;
            }

            // 作ったグループの番号を持つクラスタは、そのグループを簡略化した結果（1つ粗い段で、グループの境界球と誤差が自分の値）
            for (const CookedMeshCluster& cluster : clusters)
            {
                if (cluster.SourceGroupId == InvalidGroupId)
                {
                    continue;
                }
                const CookedMeshClusterGroup& source = outGroups[cluster.SourceGroupId];
                if (source.LODLevel + 1 != cluster.LODLevel || !IsSameFloat3(cluster.BoundsCenter, source.BoundsCenter) ||
                    cluster.BoundsRadius != source.BoundsRadius || cluster.LODError != source.Error)
                {
                    return CookedMeshParseStatus::InvalidLODGraph;
                }
            }
            return CookedMeshParseStatus::Success;
        }

        // グループの BVH の頭と節を読み、構造と保守的な条件を検査して、段ごとの節の数を返す
        CookedMeshParseStatus ReadV1GroupBVH(const uint8_t* data, size_t bvhOffset, uint32_t nodeCount,
                                             const Container::VariableArray<CookedMeshCluster>& clusters,
                                             Container::VariableArray<CookedMeshGroupBVHNode>& outNodes,
                                             Container::VariableArray<uint32_t>& outLevelNodeCounts)
        {
            using namespace CookedMeshFormatV1;

            const uint32_t declaredLevelCount = ReadLe32(data, bvhOffset + GroupBVHHeaderOffset::LevelCount);
            const uint32_t declaredLeafCount = ReadLe32(data, bvhOffset + GroupBVHHeaderOffset::LeafCount);
            if (ReadLe32(data, bvhOffset + GroupBVHHeaderOffset::Reserved0) != 0)
            {
                return CookedMeshParseStatus::ReservedFieldNonZero;
            }

            outNodes.clear();
            outNodes.reserve(nodeCount);
            uint32_t leafCount = 0;
            for (uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex)
            {
                const size_t recordOffset =
                    bvhOffset + GroupBVHHeaderSize + static_cast<size_t>(nodeIndex) * GroupBVHNodeRecordSize;
                CookedMeshGroupBVHNode node;
                node.BoundsCenter = {ReadLeFloat(data, recordOffset + GroupBVHNodeOffset::BoundsCenterX),
                                     ReadLeFloat(data, recordOffset + GroupBVHNodeOffset::BoundsCenterY),
                                     ReadLeFloat(data, recordOffset + GroupBVHNodeOffset::BoundsCenterZ)};
                node.BoundsRadius = ReadLeFloat(data, recordOffset + GroupBVHNodeOffset::BoundsRadius);
                node.MaxParentError = ReadLeFloat(data, recordOffset + GroupBVHNodeOffset::MaxParentError);
                node.First = ReadLe32(data, recordOffset + GroupBVHNodeOffset::First);
                node.Count = ReadLe32(data, recordOffset + GroupBVHNodeOffset::Count);
                const uint32_t flags = ReadLe32(data, recordOffset + GroupBVHNodeOffset::Flags);
                if ((flags & ~GroupBVHNodeFlagLeaf) != 0)
                {
                    return CookedMeshParseStatus::ReservedFieldNonZero;
                }
                node.bLeaf = (flags & GroupBVHNodeFlagLeaf) != 0;
                leafCount += node.bLeaf ? 1u : 0u;
                outNodes.push_back(node);
            }

            const CookedMeshParseStatus status = CheckCookedMeshGroupBVH(outNodes, clusters, outLevelNodeCounts);
            if (status != CookedMeshParseStatus::Success)
            {
                return status;
            }
            if (declaredLevelCount != outLevelNodeCounts.size() || declaredLeafCount != leafCount)
            {
                return CookedMeshParseStatus::InvalidGroupBVH;
            }
            return CookedMeshParseStatus::Success;
        }

        CookedMeshParseStatus ReadV1Vertices(const uint8_t* data, size_t vertexOffset, uint64_t vertexCount,
                                             Container::VariableArray<CookedMeshVertex>& outVertices)
        {
            using namespace CookedMeshFormatV1;

            outVertices.reserve(outVertices.size() + static_cast<size_t>(vertexCount));
            for (uint64_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
            {
                const size_t recordOffset = vertexOffset + static_cast<size_t>(vertexIndex) * VertexRecordSize;
                CookedMeshVertex vertex;
                vertex.Position = {ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionX),
                                   ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionY),
                                   ReadLeFloat(data, recordOffset + VertexRecordOffset::PositionZ)};
                vertex.Normal = {ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalX),
                                 ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalY),
                                 ReadLeFloat(data, recordOffset + VertexRecordOffset::NormalZ)};
                vertex.TexCoord = {ReadLeFloat(data, recordOffset + VertexRecordOffset::TexCoordU),
                                   ReadLeFloat(data, recordOffset + VertexRecordOffset::TexCoordV)};

                if (!IsFinite(vertex.Position) || !IsFinite(vertex.Normal) || !IsFinite(vertex.TexCoord))
                {
                    return CookedMeshParseStatus::InvalidFloatOrBounds;
                }
                outVertices.push_back(vertex);
            }
            return CookedMeshParseStatus::Success;
        }

        // クラスタのインデックスは、並びの先頭から隙間なく連続し（総数は expectedTotal）、そのクラスタの頂点の範囲の
        // 先頭からの相対の位置（クラスタの頂点数未満）。indices はクラスタのインデックスで始まる並び。
        CookedMeshParseStatus CheckV1ClusterIndexLayout(const Container::VariableArray<CookedMeshCluster>& clusters,
                                                        const Container::VariableArray<uint32_t>& indices,
                                                        uint64_t expectedTotal)
        {
            uint64_t expectedClusterIndexOffset = 0;
            for (const CookedMeshCluster& cluster : clusters)
            {
                if (cluster.IndexOffset != expectedClusterIndexOffset ||
                    expectedClusterIndexOffset + cluster.IndexCount > expectedTotal ||
                    expectedClusterIndexOffset + cluster.IndexCount > indices.size())
                {
                    return CookedMeshParseStatus::InvalidClusterRange;
                }

                const uint32_t clusterIndexEnd = cluster.IndexOffset + cluster.IndexCount;
                for (uint32_t index = cluster.IndexOffset; index < clusterIndexEnd; ++index)
                {
                    if (indices[index] >= cluster.VertexCount)
                    {
                        return CookedMeshParseStatus::InvalidClusterRange;
                    }
                }
                expectedClusterIndexOffset += cluster.IndexCount;
            }

            if (expectedClusterIndexOffset != expectedTotal)
            {
                return CookedMeshParseStatus::InvalidClusterRange;
            }
            return CookedMeshParseStatus::Success;
        }
    } // namespace

    CookedMeshParseStatus ParseCookedMeshPage(Container::Span<const uint8_t> pageBytes, uint32_t pageId, bool bIsRoot,
                                              uint32_t lodLevelCount, uint32_t groupCount,
                                              CookedMeshPageContent& outContent)
    {
        using namespace CookedMeshFormatV1;

        outContent = {};
        if (pageBytes.data() == nullptr || pageBytes.size() < PageHeaderSize)
        {
            return CookedMeshParseStatus::InvalidPageData;
        }

        const uint8_t* data = pageBytes.data();
        const uint32_t clusterCount = ReadLe32(data, PageHeaderOffset::ClusterCount);
        const uint32_t vertexCount = ReadLe32(data, PageHeaderOffset::VertexCount);
        const uint32_t indexCount = ReadLe32(data, PageHeaderOffset::IndexCount);
        const uint32_t fallbackIndexCount = ReadLe32(data, PageHeaderOffset::FallbackIndexCount);

        if (ReadLe64(data, PageHeaderOffset::Reserved0) != 0 || ReadLe64(data, PageHeaderOffset::Reserved1) != 0 ||
            ReadLe64(data, PageHeaderOffset::Reserved2) != 0)
        {
            return CookedMeshParseStatus::ReservedFieldNonZero;
        }

        // クラスタを持たないページは、フォールバックだけを持つ根のページに限る
        if (ReadLe32(data, PageHeaderOffset::PageId) != pageId ||
            ReadLe32(data, PageHeaderOffset::Flags) != (bIsRoot ? PageFlagRoot : 0u) ||
            (clusterCount == 0 && !(bIsRoot && fallbackIndexCount != 0)))
        {
            return CookedMeshParseStatus::InvalidPageData;
        }

        // ページの中の並び: header -> cluster -> vertex -> index -> fallback index（8 バイトに詰める）
        const uint64_t clusterOffset = PageHeaderSize;
        const uint64_t vertexOffset = clusterOffset + static_cast<uint64_t>(clusterCount) * ClusterRecordSize;
        const uint64_t indexOffset = vertexOffset + static_cast<uint64_t>(vertexCount) * VertexRecordSize;
        const uint64_t fallbackOffset = indexOffset + static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
        const uint64_t contentEnd = fallbackOffset + static_cast<uint64_t>(fallbackIndexCount) * sizeof(uint32_t);
        if (ReadLe32(data, PageHeaderOffset::ClusterOffset) != clusterOffset ||
            ReadLe32(data, PageHeaderOffset::VertexOffset) != vertexOffset ||
            ReadLe32(data, PageHeaderOffset::IndexOffset) != indexOffset ||
            ReadLe32(data, PageHeaderOffset::FallbackIndexOffset) != fallbackOffset ||
            contentEnd > std::numeric_limits<uint32_t>::max() ||
            static_cast<uint64_t>(pageBytes.size()) != ((contentEnd + SectionAlignment - 1) & ~(SectionAlignment - 1)))
        {
            return CookedMeshParseStatus::InvalidPageData;
        }

        for (uint64_t paddingOffset = contentEnd; paddingOffset < pageBytes.size(); ++paddingOffset)
        {
            if (data[static_cast<size_t>(paddingOffset)] != 0)
            {
                return CookedMeshParseStatus::PaddingByteNonZero;
            }
        }

        // フォールバックは根のページだけが持つ（0 個以上の三角形。全部の根のページを合わせて 1 つ以上は読み込みで検査する）
        if (bIsRoot ? (fallbackIndexCount % 3 != 0) : (fallbackIndexCount != 0))
        {
            return CookedMeshParseStatus::InvalidFallbackRange;
        }

        CookedMeshPageContent content;
        content.Clusters.reserve(clusterCount);
        for (uint32_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            CookedMeshCluster cluster;
            const CookedMeshParseStatus status =
                ReadV1ClusterRecord(data, static_cast<size_t>(clusterOffset) + clusterIndex * ClusterRecordSize,
                                    lodLevelCount, groupCount, vertexCount, pageId,
                                    CookedMeshParseStatus::InvalidPageData, cluster);
            if (status != CookedMeshParseStatus::Success)
            {
                return status;
            }
            content.Clusters.push_back(cluster);
        }

        CookedMeshParseStatus status =
            ReadV1Vertices(data, static_cast<size_t>(vertexOffset), vertexCount, content.Vertices);
        if (status != CookedMeshParseStatus::Success)
        {
            return status;
        }

        content.Indices.reserve(indexCount);
        for (uint32_t index = 0; index < indexCount; ++index)
        {
            content.Indices.push_back(
                ReadLe32(data, static_cast<size_t>(indexOffset) + static_cast<size_t>(index) * sizeof(uint32_t)));
        }
        status = CheckV1ClusterIndexLayout(content.Clusters, content.Indices, indexCount);
        if (status != CookedMeshParseStatus::Success)
        {
            return status;
        }

        content.FallbackIndices.reserve(fallbackIndexCount);
        for (uint32_t index = 0; index < fallbackIndexCount; ++index)
        {
            const uint32_t vertexIndex =
                ReadLe32(data, static_cast<size_t>(fallbackOffset) + static_cast<size_t>(index) * sizeof(uint32_t));
            if (vertexIndex >= vertexCount)
            {
                return CookedMeshParseStatus::InvalidIndexRange;
            }
            content.FallbackIndices.push_back(vertexIndex);
        }

        outContent = std::move(content);
        return CookedMeshParseStatus::Success;
    }

    namespace
    {
        // 親のクラスタを引く鍵。次の段のクラスタは自分の境界球・誤差としてグループの値を持つので、
        // クラスタの「親の境界球と誤差」と一致する、1つ上の段のクラスタがその親になる
        struct PageParentKey
        {
            uint32_t Level = 0;
            float Center[3] = {0.0f, 0.0f, 0.0f};
            float Radius = 0.0f;
            float Error = 0.0f;
            uint32_t ClusterIndex = 0;
        };

        bool IsSameParentTriple(const PageParentKey& left, const PageParentKey& right)
        {
            return left.Level == right.Level && left.Center[0] == right.Center[0] &&
                   left.Center[1] == right.Center[1] && left.Center[2] == right.Center[2] &&
                   left.Radius == right.Radius && left.Error == right.Error;
        }

        bool PageParentKeyLess(const PageParentKey& left, const PageParentKey& right)
        {
            if (left.Level != right.Level)
            {
                return left.Level < right.Level;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                if (left.Center[axis] != right.Center[axis])
                {
                    return left.Center[axis] < right.Center[axis];
                }
            }
            if (left.Radius != right.Radius)
            {
                return left.Radius < right.Radius;
            }
            if (left.Error != right.Error)
            {
                return left.Error < right.Error;
            }
            return left.ClusterIndex < right.ClusterIndex;
        }

        // 各ページの親のページを、クラスタの親の境界球・誤差から導く（書き出しと読み込みで同じ規則）。
        // clusters はページの順に並んだ全体の表（PageId つき）。先頭の residentPageCount 個の根のページは親を持たない。
        // 根でないページの親は、ページのクラスタの親（1つ上の段で、自分の親の境界球・誤差を自分の値として持つクラスタ）を
        // 持つ、自分より前のページのうち最も小さい番号。見つからなければ根のページ（0 番）
        void ComputePageParentPages(const Container::VariableArray<CookedMeshCluster>& clusters, size_t pageCount,
                                    size_t residentPageCount, Container::VariableArray<uint32_t>& outParents)
        {
            using namespace CookedMeshFormatV1;

            Container::VariableArray<PageParentKey> parentKeys;
            parentKeys.reserve(clusters.size());
            for (uint32_t clusterIndex = 0; clusterIndex < clusters.size(); ++clusterIndex)
            {
                const CookedMeshCluster& cluster = clusters[clusterIndex];
                PageParentKey key;
                key.Level = cluster.LODLevel;
                key.Center[0] = cluster.BoundsCenter.X;
                key.Center[1] = cluster.BoundsCenter.Y;
                key.Center[2] = cluster.BoundsCenter.Z;
                key.Radius = cluster.BoundsRadius;
                key.Error = cluster.LODError;
                key.ClusterIndex = clusterIndex;
                parentKeys.push_back(key);
            }
            std::sort(parentKeys.begin(), parentKeys.end(), PageParentKeyLess);

            outParents.assign(pageCount, InvalidPageId);
            Container::VariableArray<uint32_t> nearestParent(pageCount, std::numeric_limits<uint32_t>::max());
            for (const CookedMeshCluster& cluster : clusters)
            {
                if (cluster.bIsRoot || cluster.PageId < residentPageCount)
                {
                    continue;
                }

                PageParentKey probe;
                probe.Level = cluster.LODLevel + 1;
                probe.Center[0] = cluster.ParentBoundsCenter.X;
                probe.Center[1] = cluster.ParentBoundsCenter.Y;
                probe.Center[2] = cluster.ParentBoundsCenter.Z;
                probe.Radius = cluster.ParentBoundsRadius;
                probe.Error = cluster.ParentError;
                probe.ClusterIndex = 0;
                for (auto match = std::lower_bound(parentKeys.begin(), parentKeys.end(), probe, PageParentKeyLess);
                     match != parentKeys.end() && IsSameParentTriple(*match, probe); ++match)
                {
                    const uint32_t matchPage = clusters[match->ClusterIndex].PageId;
                    if (matchPage < cluster.PageId)
                    {
                        nearestParent[cluster.PageId] = std::min(nearestParent[cluster.PageId], matchPage);
                    }
                }
            }
            for (size_t pageIndex = residentPageCount; pageIndex < pageCount; ++pageIndex)
            {
                outParents[pageIndex] = nearestParent[pageIndex] < pageIndex ? nearestParent[pageIndex] : RootPageId;
            }
        }
    } // namespace

    namespace
    {
        // v1.1 のページの表を読んで、各ページを検査しながら、v1.0 と同じ形の全体の配列（クラスタ・頂点・インデックス）へ
        // 組み直す。ページは表の順に並び、クラスタ・頂点・クラスタのインデックスはページごとに連続する。
        // クラスタの IndexOffset・VertexOffset は全体の位置へ直す（ページの中の位置は読み込みの間だけ）。
        CookedMeshParseStatus DecodeV1Pages(const uint8_t* data, const SectionRange& tableSection,
                                            const SectionRange& regionSection, uint32_t lodLevelCount,
                                            uint32_t groupCount, uint32_t clusterCount, uint32_t vertexCount,
                                            uint32_t indexCount, uint32_t fallbackIndexOffset,
                                            uint32_t fallbackIndexCount,
                                            Container::VariableArray<CookedMeshCluster>& outClusters,
                                            Container::VariableArray<CookedMeshVertex>& outVertices,
                                            Container::VariableArray<uint32_t>& outIndices,
                                            Container::VariableArray<CookedMeshPage>& outPages)
        {
            using namespace CookedMeshFormatV1;

            const uint64_t pageCount = tableSection.Size / PageTableRecordSize;

            // 件数は壊れていてもよいので、確保する前に、ページの領域の大きさで収まる件数かを確かめる
            // （各ページは先頭と、クラスタ・頂点・インデックスの実体を領域の中に持つ）
            if (pageCount > regionSection.Size / PageHeaderSize || clusterCount > regionSection.Size / ClusterRecordSize ||
                vertexCount > regionSection.Size / VertexRecordSize || indexCount > regionSection.Size / sizeof(uint32_t))
            {
                return CookedMeshParseStatus::InvalidCounts;
            }

            outClusters.reserve(clusterCount);
            outVertices.reserve(vertexCount);
            outIndices.reserve(indexCount);
            outPages.reserve(static_cast<size_t>(pageCount));

            uint64_t cursor = regionSection.Offset;
            uint64_t clusterBase = 0;
            uint64_t vertexBase = 0;
            uint64_t indexBase = 0;
            Container::VariableArray<uint32_t> fallbackIndices;
            for (uint64_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
            {
                const uint32_t pageId = static_cast<uint32_t>(pageIndex);
                const size_t recordOffset =
                    static_cast<size_t>(tableSection.Offset) + static_cast<size_t>(pageIndex) * PageTableRecordSize;
                CookedMeshPage page;
                page.FileOffset = ReadLe64(data, recordOffset + PageTableRecordOffset::FileOffset);
                page.Size = ReadLe32(data, recordOffset + PageTableRecordOffset::Size);
                page.ParentPageId = ReadLe32(data, recordOffset + PageTableRecordOffset::ParentPageId);
                const uint32_t flags = ReadLe32(data, recordOffset + PageTableRecordOffset::Flags);
                page.ClusterCount = ReadLe32(data, recordOffset + PageTableRecordOffset::ClusterCount);
                page.FirstClusterIndex = ReadLe32(data, recordOffset + PageTableRecordOffset::FirstClusterIndex);
                page.VertexCount = ReadLe32(data, recordOffset + PageTableRecordOffset::VertexCount);
                page.IndexCount = ReadLe32(data, recordOffset + PageTableRecordOffset::IndexCount);
                page.Hash = ReadLe64(data, recordOffset + PageTableRecordOffset::PageHash);
                page.bIsRoot = (flags & PageFlagRoot) != 0;

                if (ReadLe32(data, recordOffset + PageTableRecordOffset::Reserved0) != 0)
                {
                    return CookedMeshParseStatus::ReservedFieldNonZero;
                }

                // 根のページは 0 番から連続した先頭の1ページ以上。根のページは親を持たず、それ以外は先に並ぶページを親にする
                if ((flags & ~PageFlagRoot) != 0 || (pageId == RootPageId && !page.bIsRoot) ||
                    (page.bIsRoot && pageId != RootPageId && !outPages[pageId - 1].bIsRoot) ||
                    (page.bIsRoot ? page.ParentPageId != InvalidPageId : page.ParentPageId >= pageId))
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }

                // ページはページの領域に隙間なく並ぶ。根のページも含めて 128 KiB 以下
                uint64_t pageEnd = 0;
                if (page.FileOffset != cursor || page.Size < PageHeaderSize ||
                    !AddChecked64(page.FileOffset, page.Size, pageEnd) || pageEnd > regionSection.End ||
                    page.Size > PageSize)
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }

                if ((page.ClusterCount == 0 && !page.bIsRoot) || page.FirstClusterIndex != clusterBase)
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }

                const Container::Span<const uint8_t> pageBytes(data + static_cast<size_t>(page.FileOffset), page.Size);
                if (ComputeCookedMeshPayloadHash(pageBytes) != page.Hash)
                {
                    return CookedMeshParseStatus::PageHashMismatch;
                }

                CookedMeshPageContent content;
                const CookedMeshParseStatus pageStatus =
                    ParseCookedMeshPage(pageBytes, pageId, page.bIsRoot, lodLevelCount, groupCount, content);
                if (pageStatus != CookedMeshParseStatus::Success)
                {
                    return pageStatus;
                }

                // 表の件数と、ページの中の件数が一致する
                if (content.Clusters.size() != page.ClusterCount || content.Vertices.size() != page.VertexCount ||
                    content.Indices.size() != page.IndexCount)
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }

                if (clusterBase + page.ClusterCount > clusterCount || vertexBase + page.VertexCount > vertexCount ||
                    indexBase + page.IndexCount > fallbackIndexOffset)
                {
                    return CookedMeshParseStatus::InvalidCounts;
                }

                page.FirstVertex = static_cast<uint32_t>(vertexBase);
                page.FirstIndex = static_cast<uint32_t>(indexBase);
                for (CookedMeshCluster& cluster : content.Clusters)
                {
                    cluster.IndexOffset += static_cast<uint32_t>(indexBase);
                    cluster.VertexOffset += static_cast<uint32_t>(vertexBase);
                    outClusters.push_back(cluster);
                }
                for (const CookedMeshVertex& vertex : content.Vertices)
                {
                    outVertices.push_back(vertex);
                }
                for (const uint32_t index : content.Indices)
                {
                    outIndices.push_back(index);
                }
                // 根のページのフォールバックのインデックスは、ページの頂点への添字から全体の頂点への添字へ直して並べる
                for (const uint32_t localIndex : content.FallbackIndices)
                {
                    fallbackIndices.push_back(localIndex + static_cast<uint32_t>(vertexBase));
                }

                clusterBase += page.ClusterCount;
                vertexBase += page.VertexCount;
                indexBase += page.IndexCount;
                cursor = pageEnd;
                outPages.push_back(page);
            }

            if (cursor != regionSection.End)
            {
                return CookedMeshParseStatus::InvalidPageTable;
            }
            if (clusterBase != clusterCount || vertexBase != vertexCount)
            {
                return CookedMeshParseStatus::InvalidCounts;
            }
            if (indexBase != fallbackIndexOffset || fallbackIndices.size() != fallbackIndexCount)
            {
                return CookedMeshParseStatus::InvalidFallbackRange;
            }
            for (const uint32_t index : fallbackIndices)
            {
                outIndices.push_back(index);
            }
            if (outIndices.size() != indexCount)
            {
                return CookedMeshParseStatus::InvalidCounts;
            }
            return CookedMeshParseStatus::Success;
        }

        // v1.1 のページの表と、クラスタ・グループの整合を検査する。
        //   - 根のクラスタは根のページ（常駐）に入る
        //   - 1つのグループのメンバは同じページに入る（ページをまたがない）
        //   - 親のページは、書き出しと同じ規則（ComputePageParentPages）で導いた番号と一致する
        CookedMeshParseStatus CheckV1PageConsistency(const Container::VariableArray<CookedMeshCluster>& clusters,
                                                     const Container::VariableArray<CookedMeshClusterGroup>& groups,
                                                     const Container::VariableArray<CookedMeshPage>& pages)
        {
            using namespace CookedMeshFormatV1;

            for (const CookedMeshCluster& cluster : clusters)
            {
                if (cluster.bIsRoot && !pages[cluster.PageId].bIsRoot)
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }
            }

            for (const CookedMeshClusterGroup& group : groups)
            {
                const uint32_t groupPage = clusters[group.ClusterOffset].PageId;
                for (uint32_t memberIndex = group.ClusterOffset + 1;
                     memberIndex < group.ClusterOffset + group.ClusterCount; ++memberIndex)
                {
                    if (clusters[memberIndex].PageId != groupPage)
                    {
                        return CookedMeshParseStatus::InvalidGroupTable;
                    }
                }
            }

            size_t residentPageCount = 0;
            while (residentPageCount < pages.size() && pages[residentPageCount].bIsRoot)
            {
                ++residentPageCount;
            }
            Container::VariableArray<uint32_t> expectedParents;
            ComputePageParentPages(clusters, pages.size(), residentPageCount, expectedParents);
            for (size_t pageIndex = residentPageCount; pageIndex < pages.size(); ++pageIndex)
            {
                if (pages[pageIndex].ParentPageId != expectedParents[pageIndex])
                {
                    return CookedMeshParseStatus::InvalidPageTable;
                }
            }
            return CookedMeshParseStatus::Success;
        }
    } // namespace

    // v1 の読み込み（v1.0・v1.1）。ParseCookedMesh が、先頭の magic と最小のヘッダ長を確かめたうえで呼ぶ。
    static CookedMeshParseResult ParseCookedMeshV1(AssetBlob sourceBlob)
    {
        using namespace CookedMeshFormatV1;

        const Container::Span<const uint8_t> bytes = sourceBlob.GetSpan();
        const uint8_t* data = bytes.data();
        const uint32_t headerSize = ReadLe32(data, HeaderOffset::HeaderSize);
        const uint16_t versionMajor = ReadLe16(data, HeaderOffset::VersionMajor);
        const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
        const uint32_t endianMarker = ReadLe32(data, HeaderOffset::EndianMarker);
        const uint32_t vertexRecordSize = ReadLe32(data, HeaderOffset::VertexRecordSize);
        const uint32_t submeshRecordSize = ReadLe32(data, HeaderOffset::SubmeshRecordSize);
        const uint32_t materialRecordSize = ReadLe32(data, HeaderOffset::MaterialRecordSize);
        const uint32_t clusterRecordSize = ReadLe32(data, HeaderOffset::ClusterRecordSize);
        const uint32_t stringRefRecordSize = ReadLe32(data, HeaderOffset::StringRefRecordSize);
        const uint32_t groupRecordSize = ReadLe32(data, HeaderOffset::GroupRecordSize);
        const uint64_t declaredFileSize = ReadLe64(data, HeaderOffset::FileSize);
        const uint64_t payloadHash = ReadLe64(data, HeaderOffset::PayloadHash);
        const uint32_t vertexCount = ReadLe32(data, HeaderOffset::VertexCount);
        const uint32_t indexCount = ReadLe32(data, HeaderOffset::IndexCount);
        const uint32_t submeshCount = ReadLe32(data, HeaderOffset::SubmeshCount);
        const uint32_t materialCount = ReadLe32(data, HeaderOffset::MaterialCount);
        const uint32_t clusterCount = ReadLe32(data, HeaderOffset::ClusterCount);
        const uint32_t groupCount = ReadLe32(data, HeaderOffset::GroupCount);
        const uint32_t stringByteCount = ReadLe32(data, HeaderOffset::StringByteCount);
        const uint32_t lodLevelCount = ReadLe32(data, HeaderOffset::LODLevelCount);
        const uint32_t fallbackIndexOffset = ReadLe32(data, HeaderOffset::FallbackIndexOffset);
        const uint32_t fallbackIndexCount = ReadLe32(data, HeaderOffset::FallbackIndexCount);

        if (versionMajor != VersionMajor || (versionMinor != VersionMinor && versionMinor != VersionMinorPaged))
        {
            return Fail(CookedMeshParseStatus::UnsupportedVersion);
        }
        // v1.1 はクラスタ・頂点・インデックスをページに詰めた並び（ヘッダの意味の違いは PagedHeaderOffset を参照）
        const bool bPaged = versionMinor == VersionMinorPaged;

        if (endianMarker != EndianMarker)
        {
            return Fail(CookedMeshParseStatus::EndianMismatch);
        }

        if (headerSize != HeaderSize)
        {
            return Fail(CookedMeshParseStatus::HeaderSizeMismatch);
        }

        if (vertexRecordSize != VertexRecordSize || submeshRecordSize != SubmeshRecordSize ||
            materialRecordSize != MaterialRecordSize || clusterRecordSize != ClusterRecordSize ||
            stringRefRecordSize != StringRefRecordSize || groupRecordSize != GroupRecordSize)
        {
            return Fail(CookedMeshParseStatus::RecordSizeMismatch);
        }

        if (declaredFileSize != static_cast<uint64_t>(bytes.size()))
        {
            return Fail(CookedMeshParseStatus::FileSizeMismatch);
        }

        // v1.1 だけが、グループの BVH を持つ（Flags の bit0）。それ以外のビットは予約
        const uint32_t headerFlags = ReadLe32(data, HeaderOffset::Flags);
        if ((headerFlags & ~HeaderFlagGroupBVH) != 0 || ((headerFlags & HeaderFlagGroupBVH) != 0 && !bPaged))
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }
        const bool bGroupBVH = (headerFlags & HeaderFlagGroupBVH) != 0;

        if (submeshCount != 1 || materialCount != 1 || clusterCount == 0 || groupCount > clusterCount ||
            indexCount % 3 != 0 || lodLevelCount == 0 || lodLevelCount > MaxLODLevels)
        {
            return Fail(CookedMeshParseStatus::InvalidCounts);
        }

        SectionRange sections[] = {
            {ReadLe64(data, HeaderOffset::SubmeshTableOffset), ReadLe64(data, HeaderOffset::SubmeshTableSize)},
            {ReadLe64(data, HeaderOffset::MaterialTableOffset), ReadLe64(data, HeaderOffset::MaterialTableSize)},
            {ReadLe64(data, HeaderOffset::ClusterTableOffset), ReadLe64(data, HeaderOffset::ClusterTableSize)},
            {ReadLe64(data, HeaderOffset::GroupTableOffset), ReadLe64(data, HeaderOffset::GroupTableSize)},
            {ReadLe64(data, HeaderOffset::StringTableOffset), ReadLe64(data, HeaderOffset::StringTableSize)},
            {ReadLe64(data, HeaderOffset::VertexPayloadOffset), ReadLe64(data, HeaderOffset::VertexPayloadSize)},
            {ReadLe64(data, HeaderOffset::IndexPayloadOffset), ReadLe64(data, HeaderOffset::IndexPayloadSize)}};

        uint64_t expectedSubmeshSize = 0;
        uint64_t expectedMaterialSize = 0;
        uint64_t expectedClusterSize = 0;
        uint64_t expectedGroupSize = 0;
        uint64_t expectedVertexSize = 0;
        uint64_t expectedIndexSize = 0;
        if (!MultiplyChecked64(submeshCount, SubmeshRecordSize, expectedSubmeshSize) ||
            !MultiplyChecked64(materialCount, MaterialRecordSize, expectedMaterialSize) ||
            !MultiplyChecked64(clusterCount, ClusterRecordSize, expectedClusterSize) ||
            !MultiplyChecked64(groupCount, GroupRecordSize, expectedGroupSize) ||
            !MultiplyChecked64(vertexCount, VertexRecordSize, expectedVertexSize) ||
            !MultiplyChecked64(indexCount, sizeof(uint32_t), expectedIndexSize))
        {
            return Fail(CookedMeshParseStatus::IntegerOverflow);
        }

        if (bPaged)
        {
            // v1.1 はクラスタ・頂点・インデックスの節が無い。ClusterTable の位置はページの表（ページ数 × 48B）、
            // VertexPayload の位置はページの領域（ページが隙間なく並ぶ）、IndexPayload は大きさ 0
            if (sections[2].Size == 0 || sections[2].Size % PageTableRecordSize != 0)
            {
                return Fail(CookedMeshParseStatus::InvalidCounts);
            }
            expectedClusterSize = sections[2].Size;
            expectedVertexSize = sections[5].Size;
            expectedIndexSize = 0;
        }

        // BVH を持つときのグループの節は、グループの記録の後ろに BVH の頭と節が続く（大きさは BVH の頭を読んでから検査する）
        const bool bGroupSectionSizeValid =
            bGroupBVH ? sections[3].Size >= expectedGroupSize + GroupBVHHeaderSize : sections[3].Size == expectedGroupSize;
        if (sections[0].Size != expectedSubmeshSize || sections[1].Size != expectedMaterialSize ||
            sections[2].Size != expectedClusterSize || !bGroupSectionSizeValid ||
            sections[4].Size != stringByteCount || sections[5].Size != expectedVertexSize ||
            sections[6].Size != expectedIndexSize)
        {
            return Fail(CookedMeshParseStatus::InvalidCounts);
        }

        for (SectionRange& section : sections)
        {
            if (!AddChecked64(section.Offset, section.Size, section.End))
            {
                return Fail(CookedMeshParseStatus::IntegerOverflow);
            }
        }

        for (const SectionRange& section : sections)
        {
            if (section.Offset < HeaderSize || section.End > declaredFileSize)
            {
                return Fail(CookedMeshParseStatus::SectionOutOfRange);
            }
        }

        // グループの BVH の節の数（頭の値）が、グループの節の大きさと一致する
        uint32_t groupBVHNodeCount = 0;
        if (bGroupBVH)
        {
            const size_t bvhHeaderOffset = static_cast<size_t>(sections[3].Offset + expectedGroupSize);
            groupBVHNodeCount = ReadLe32(data, bvhHeaderOffset + GroupBVHHeaderOffset::NodeCount);
            uint64_t expectedBVHNodeBytes = 0;
            if (!MultiplyChecked64(groupBVHNodeCount, GroupBVHNodeRecordSize, expectedBVHNodeBytes))
            {
                return Fail(CookedMeshParseStatus::IntegerOverflow);
            }
            // 葉の数は最大でクラスタ数で、内部の節は葉より少ない
            if (groupBVHNodeCount == 0 ||
                static_cast<uint64_t>(groupBVHNodeCount) > static_cast<uint64_t>(clusterCount) * 2 ||
                sections[3].Size != expectedGroupSize + GroupBVHHeaderSize + expectedBVHNodeBytes)
            {
                return Fail(CookedMeshParseStatus::InvalidCounts);
            }
        }

        for (const SectionRange& section : sections)
        {
            if (section.Offset % SectionAlignment != 0)
            {
                return Fail(CookedMeshParseStatus::SectionMisalignment);
            }
        }

        uint64_t cursor = HeaderSize;
        for (const SectionRange& section : sections)
        {
            uint64_t expectedOffset = 0;
            if (!AlignUpChecked64(cursor, SectionAlignment, expectedOffset))
            {
                return Fail(CookedMeshParseStatus::IntegerOverflow);
            }

            if (section.Offset != expectedOffset)
            {
                return Fail(CookedMeshParseStatus::SectionPackingMismatch);
            }

            for (uint64_t paddingOffset = cursor; paddingOffset < section.Offset; ++paddingOffset)
            {
                if (data[static_cast<size_t>(paddingOffset)] != 0)
                {
                    return Fail(CookedMeshParseStatus::PaddingByteNonZero);
                }
            }
            cursor = section.End;
        }

        if (sections[6].End != declaredFileSize)
        {
            return Fail(CookedMeshParseStatus::FileSizeMismatch);
        }

        const size_t payloadOffset = static_cast<size_t>(sections[0].Offset);
        const size_t payloadSize = static_cast<size_t>(sections[6].End - sections[0].Offset);
        if (ComputeCookedMeshPayloadHash(data + payloadOffset, payloadSize) != payloadHash)
        {
            return Fail(CookedMeshParseStatus::PayloadHashMismatch);
        }

        const CookedMeshFloat3 totalBoundsCenter = {ReadLeFloat(data, HeaderOffset::TotalBoundsCenterX),
                                                    ReadLeFloat(data, HeaderOffset::TotalBoundsCenterY),
                                                    ReadLeFloat(data, HeaderOffset::TotalBoundsCenterZ)};
        const float totalBoundsRadius = ReadLeFloat(data, HeaderOffset::TotalBoundsRadius);
        const float fallbackError = ReadLeFloat(data, HeaderOffset::FallbackError);
        if (!IsFinite(totalBoundsCenter) || !std::isfinite(totalBoundsRadius) || totalBoundsRadius < 0.0f ||
            !std::isfinite(fallbackError) || fallbackError < 0.0f)
        {
            return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
        }

        if (ReadLe32(data, HeaderOffset::ClusterAlgorithmId) != ClusterAlgorithmId ||
            ReadLe32(data, HeaderOffset::ClusterAlgorithmVersion) != ClusterAlgorithmVersion ||
            ReadLe32(data, HeaderOffset::ClusterMaxTriangles) != ClusterMaxTriangles ||
            ReadLe32(data, HeaderOffset::ClusterMaxVertices) != ClusterMaxVertices ||
            ReadLe32(data, HeaderOffset::ClusterSettingsFlags) != ClusterSettingsFlags)
        {
            return Fail(CookedMeshParseStatus::UnsupportedV1Feature);
        }

        // フォールバックの範囲は、クラスタのインデックスの後ろに続く三角形の並びで、末尾まで
        if (fallbackIndexCount == 0 || fallbackIndexCount % 3 != 0 || fallbackIndexOffset % 3 != 0 ||
            static_cast<uint64_t>(fallbackIndexOffset) + fallbackIndexCount != indexCount)
        {
            return Fail(CookedMeshParseStatus::InvalidFallbackRange);
        }

        const size_t submeshTableOffset = static_cast<size_t>(sections[0].Offset);
        CookedMeshSubmesh submesh;
        submesh.IndexOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::IndexOffset);
        submesh.IndexCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::IndexCount);
        submesh.VertexOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::VertexOffset);
        submesh.VertexCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::VertexCount);
        submesh.MaterialIndex = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::MaterialIndex);
        submesh.ClusterOffset = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::ClusterOffset);
        submesh.ClusterCount = ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::ClusterCount);
        submesh.BoundsCenter = {ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX),
                                ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterY),
                                ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsCenterZ)};
        submesh.BoundsRadius = ReadLeFloat(data, submeshTableOffset + SubmeshRecordOffset::BoundsRadius);

        if (ReadLe32(data, submeshTableOffset + SubmeshRecordOffset::Flags) != 0 ||
            ReadLe64(data, submeshTableOffset + SubmeshRecordOffset::Reserved0) != 0 ||
            ReadLe64(data, submeshTableOffset + SubmeshRecordOffset::Reserved1) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

        if (!IsFinite(submesh.BoundsCenter) || !std::isfinite(submesh.BoundsRadius) || submesh.BoundsRadius < 0.0f)
        {
            return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
        }

        if (submesh.VertexOffset != 0 || submesh.MaterialIndex != 0)
        {
            return Fail(CookedMeshParseStatus::UnsupportedV1Feature);
        }

        // サブメッシュのインデックスはクラスタの範囲（フォールバックの手前まで）。頂点は全体
        if (submesh.IndexOffset != 0 || submesh.IndexCount != fallbackIndexOffset ||
            submesh.VertexCount != vertexCount)
        {
            return Fail(CookedMeshParseStatus::InvalidIndexRange);
        }

        if (submesh.ClusterOffset != 0 || submesh.ClusterCount != clusterCount)
        {
            return Fail(CookedMeshParseStatus::InvalidClusterRange);
        }

        const size_t stringTableOffset = static_cast<size_t>(sections[4].Offset);
        const size_t stringTableSize = static_cast<size_t>(sections[4].Size);
        if (!IsPrintableAscii(data + stringTableOffset, stringTableSize))
        {
            return Fail(CookedMeshParseStatus::InvalidStringTable);
        }

        const size_t materialTableOffset = static_cast<size_t>(sections[1].Offset);
        if (ReadLe32(data, materialTableOffset + MaterialRecordOffset::Flags) != 0 ||
            ReadLe32(data, materialTableOffset + MaterialRecordOffset::Reserved0) != 0 ||
            ReadLe64(data, materialTableOffset + MaterialRecordOffset::Reserved1) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

        CookedMeshMaterial material;
        CookedMeshParseStatus stringStatus =
            ParseStringReference(data, materialTableOffset + MaterialRecordOffset::AlbedoTexture, stringTableOffset,
                                 stringTableSize, material.AlbedoTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }
        stringStatus = ParseStringReference(data, materialTableOffset + MaterialRecordOffset::NormalTexture,
                                            stringTableOffset, stringTableSize, material.NormalTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }
        stringStatus = ParseStringReference(data, materialTableOffset + MaterialRecordOffset::ArmTexture,
                                            stringTableOffset, stringTableSize, material.ArmTexture);
        if (stringStatus != CookedMeshParseStatus::Success)
        {
            return Fail(stringStatus);
        }

        Container::VariableArray<CookedMeshCluster> clusters;
        Container::VariableArray<CookedMeshVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        Container::VariableArray<CookedMeshPage> pages;
        Container::VariableArray<CookedMeshClusterGroup> groups;
        uint32_t rootCount = 0;
        const size_t groupTableOffset = static_cast<size_t>(sections[3].Offset);
        const size_t vertexPayloadOffset = static_cast<size_t>(sections[5].Offset);
        const size_t indexPayloadOffset = static_cast<size_t>(sections[6].Offset);

        if (bPaged)
        {
            // v1.1: ページの表のとおりに各ページを読み、v1.0 と同じ形の全体の配列へ組み直す
            const CookedMeshParseStatus pageStatus =
                DecodeV1Pages(data, sections[2], sections[5], lodLevelCount, groupCount, clusterCount, vertexCount,
                              indexCount, fallbackIndexOffset, fallbackIndexCount, clusters, vertices, indices, pages);
            if (pageStatus != CookedMeshParseStatus::Success)
            {
                return Fail(pageStatus);
            }
        }
        else
        {
            clusters.reserve(clusterCount);
            const size_t clusterTableOffset = static_cast<size_t>(sections[2].Offset);
            for (uint32_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
            {
                // ページの番号は v1.0 では 0 固定（ページは v1.1 から）
                CookedMeshCluster cluster;
                const CookedMeshParseStatus clusterStatus = ReadV1ClusterRecord(
                    data, clusterTableOffset + static_cast<size_t>(clusterIndex) * ClusterRecordSize, lodLevelCount,
                    groupCount, vertexCount, 0, CookedMeshParseStatus::UnsupportedV1Feature, cluster);
                if (clusterStatus != CookedMeshParseStatus::Success)
                {
                    return Fail(clusterStatus);
                }
                clusters.push_back(cluster);
            }
        }

        CookedMeshParseStatus structureStatus = CheckV1LevelStructure(clusters, lodLevelCount, rootCount);
        if (structureStatus != CookedMeshParseStatus::Success)
        {
            return Fail(structureStatus);
        }

        structureStatus = ReadV1Groups(data, groupTableOffset, groupCount, clusters, lodLevelCount, rootCount, groups);
        if (structureStatus != CookedMeshParseStatus::Success)
        {
            return Fail(structureStatus);
        }

        Container::VariableArray<CookedMeshGroupBVHNode> groupBVH;
        Container::VariableArray<uint32_t> groupBVHLevelNodeCounts;
        if (bGroupBVH)
        {
            structureStatus = ReadV1GroupBVH(data, groupTableOffset + static_cast<size_t>(groupCount) * GroupRecordSize,
                                             groupBVHNodeCount, clusters, groupBVH, groupBVHLevelNodeCounts);
            if (structureStatus != CookedMeshParseStatus::Success)
            {
                return Fail(structureStatus);
            }
        }

        if (!bPaged)
        {
            structureStatus = ReadV1Vertices(data, vertexPayloadOffset, vertexCount, vertices);
            if (structureStatus != CookedMeshParseStatus::Success)
            {
                return Fail(structureStatus);
            }

            indices.reserve(indexCount);
            for (uint32_t index = 0; index < indexCount; ++index)
            {
                const uint32_t vertexIndex =
                    ReadLe32(data, indexPayloadOffset + static_cast<size_t>(index) * sizeof(uint32_t));
                if (vertexIndex >= vertexCount)
                {
                    return Fail(CookedMeshParseStatus::InvalidIndexRange);
                }
                indices.push_back(vertexIndex);
            }
        }

        // クラスタのインデックスは Clusters の並びで 0 から隙間なく並び、フォールバックの範囲の手前で終わる
        structureStatus = CheckV1ClusterIndexLayout(clusters, indices, fallbackIndexOffset);
        if (structureStatus != CookedMeshParseStatus::Success)
        {
            return Fail(structureStatus);
        }

        if (bPaged)
        {
            structureStatus = CheckV1PageConsistency(clusters, groups, pages);
            if (structureStatus != CookedMeshParseStatus::Success)
            {
                return Fail(structureStatus);
            }
        }

        CookedMeshParseResult result;
        result.Status = CookedMeshParseStatus::Success;
        result.Mesh.SourceBlob = std::move(sourceBlob);
        result.Mesh.FormatMajor = VersionMajor;
        result.Mesh.FormatMinor = bPaged ? VersionMinorPaged : VersionMinor;
        result.Mesh.TotalBoundsCenter = totalBoundsCenter;
        result.Mesh.TotalBoundsRadius = totalBoundsRadius;
        result.Mesh.PayloadHash = payloadHash;
        result.Mesh.StringTableOffset = stringTableOffset;
        result.Mesh.StringTableSize = stringTableSize;
        result.Mesh.LODLevelCount = lodLevelCount;
        result.Mesh.FallbackIndexOffset = fallbackIndexOffset;
        result.Mesh.FallbackIndexCount = fallbackIndexCount;
        result.Mesh.FallbackError = fallbackError;
        result.Mesh.Vertices = std::move(vertices);
        result.Mesh.Submeshes.push_back(submesh);
        result.Mesh.Materials.push_back(material);
        result.Mesh.Clusters = std::move(clusters);
        result.Mesh.Groups = std::move(groups);
        result.Mesh.Pages = std::move(pages);
        result.Mesh.GroupBVH = std::move(groupBVH);
        result.Mesh.GroupBVHLevelNodeCounts = std::move(groupBVHLevelNodeCounts);
        result.Mesh.Indices = std::move(indices);
        return result;
    }

    namespace
    {
        using WriteBuffer = Container::VariableArray<uint8_t>;

        void WriteLe16(WriteBuffer& bytes, size_t offset, uint16_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        }

        void WriteLe32(WriteBuffer& bytes, size_t offset, uint32_t value)
        {
            bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
            bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
            bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
        }

        void WriteLe64(WriteBuffer& bytes, size_t offset, uint64_t value)
        {
            WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
            WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
        }

        void WriteLeFloat(WriteBuffer& bytes, size_t offset, float value)
        {
            WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
        }

        void WriteFloat3(WriteBuffer& bytes, size_t offset, CookedMeshFloat3 value)
        {
            WriteLeFloat(bytes, offset + 0, value.X);
            WriteLeFloat(bytes, offset + 4, value.Y);
            WriteLeFloat(bytes, offset + 8, value.Z);
        }

        size_t AlignUp(size_t value, size_t alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        void WriteV1ClusterRecord(WriteBuffer& bytes, size_t recordOffset, const CookedMeshCluster& cluster)
        {
            using namespace CookedMeshFormatV1;

            WriteFloat3(bytes, recordOffset + ClusterRecordOffset::SelfCenterX, cluster.BoundsCenter);
            WriteLeFloat(bytes, recordOffset + ClusterRecordOffset::SelfRadius, cluster.BoundsRadius);
            WriteLeFloat(bytes, recordOffset + ClusterRecordOffset::SelfError, cluster.LODError);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::GroupId, cluster.GroupId);
            WriteFloat3(bytes, recordOffset + ClusterRecordOffset::ParentCenterX, cluster.ParentBoundsCenter);
            WriteLeFloat(bytes, recordOffset + ClusterRecordOffset::ParentRadius, cluster.ParentBoundsRadius);
            WriteLeFloat(bytes, recordOffset + ClusterRecordOffset::ParentError, cluster.ParentError);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::LODLevel, cluster.LODLevel);
            WriteFloat3(bytes, recordOffset + ClusterRecordOffset::ConeAxisX, cluster.ConeAxis);
            WriteLeFloat(bytes, recordOffset + ClusterRecordOffset::ConeCutoff, cluster.ConeCutoff);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::IndexOffset, cluster.IndexOffset);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::IndexCount, cluster.IndexCount);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::VertexOffset, cluster.VertexOffset);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::VertexCount, cluster.VertexCount);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::MaterialIndex, cluster.MaterialIndex);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::PageId, cluster.PageId);
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::Flags, cluster.bIsRoot ? ClusterFlagRoot : 0u);
            // 番号 + 1 で書く（InvalidGroupId の + 1 は 0 に折り返し、番号なしになる）
            WriteLe32(bytes, recordOffset + ClusterRecordOffset::SourceGroupIdPlusOne, cluster.SourceGroupId + 1u);
        }

        void WriteV1GroupRecord(WriteBuffer& bytes, size_t recordOffset, const CookedMeshClusterGroup& group)
        {
            using namespace CookedMeshFormatV1;

            WriteFloat3(bytes, recordOffset + GroupRecordOffset::BoundsCenterX, group.BoundsCenter);
            WriteLeFloat(bytes, recordOffset + GroupRecordOffset::BoundsRadius, group.BoundsRadius);
            WriteLeFloat(bytes, recordOffset + GroupRecordOffset::Error, group.Error);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::ClusterOffset, group.ClusterOffset);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::ClusterCount, group.ClusterCount);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::LODLevel, group.LODLevel);
        }

        void WriteV1Vertex(WriteBuffer& bytes, size_t recordOffset, const CookedMeshVertex& vertex)
        {
            using namespace CookedMeshFormatV1;

            WriteFloat3(bytes, recordOffset + VertexRecordOffset::PositionX, vertex.Position);
            WriteFloat3(bytes, recordOffset + VertexRecordOffset::NormalX, vertex.Normal);
            WriteLeFloat(bytes, recordOffset + VertexRecordOffset::TexCoordU, vertex.TexCoord.U);
            WriteLeFloat(bytes, recordOffset + VertexRecordOffset::TexCoordV, vertex.TexCoord.V);
        }

        // 材質の3つのテクスチャの参照と文字列の節を書く。文字列は albedo・normal・arm の順に詰める。空の参照は (0, 0)
        void WriteV1MaterialStrings(WriteBuffer& bytes, size_t materialTableOffset, size_t stringTableOffset,
                                    const CookedMeshV1WriteInput& input)
        {
            using namespace CookedMeshFormatV1;

            size_t stringCursor = 0;
            const Container::AnsiStringView textures[] = {input.AlbedoTexture, input.NormalTexture,
                                                          input.ArmTexture};
            const size_t textureRecordOffsets[] = {MaterialRecordOffset::AlbedoTexture,
                                                   MaterialRecordOffset::NormalTexture,
                                                   MaterialRecordOffset::ArmTexture};
            for (size_t textureIndex = 0; textureIndex < 3; ++textureIndex)
            {
                const Container::AnsiStringView texture = textures[textureIndex];
                if (texture.empty())
                {
                    continue;
                }

                const size_t recordOffset = materialTableOffset + textureRecordOffsets[textureIndex];
                WriteLe64(bytes, recordOffset + StringRefRecordOffset::StringOffset,
                          static_cast<uint64_t>(stringCursor));
                WriteLe32(bytes, recordOffset + StringRefRecordOffset::StringLength,
                          static_cast<uint32_t>(texture.size()));
                std::memcpy(bytes.data() + stringTableOffset + stringCursor, texture.data(), texture.size());
                stringCursor += texture.size();
            }
        }
    } // namespace

    bool SerializeCookedMeshV1(const CookedMeshV1WriteInput& input, Container::VariableArray<uint8_t>& outBytes)
    {
        using namespace CookedMeshFormatV1;

        outBytes.clear();

        constexpr uint64_t max32 = std::numeric_limits<uint32_t>::max();
        const uint64_t indexTotal = static_cast<uint64_t>(input.ClusterIndices.size()) + input.FallbackIndices.size();
        const uint64_t stringTotal = static_cast<uint64_t>(input.AlbedoTexture.size()) + input.NormalTexture.size() +
                                     input.ArmTexture.size();
        if (input.Vertices.size() > max32 || input.Clusters.size() > max32 || input.Groups.size() > max32 ||
            indexTotal > max32 || stringTotal > max32)
        {
            return false;
        }

        const size_t vertexCount = input.Vertices.size();
        const size_t clusterCount = input.Clusters.size();
        const size_t groupCount = input.Groups.size();
        const size_t clusterIndexCount = input.ClusterIndices.size();
        const size_t indexCount = static_cast<size_t>(indexTotal);
        const size_t stringSize = static_cast<size_t>(stringTotal);

        const size_t submeshTableOffset = HeaderSize;
        const size_t materialTableOffset = AlignUp(submeshTableOffset + SubmeshRecordSize, SectionAlignment);
        const size_t clusterTableOffset = AlignUp(materialTableOffset + MaterialRecordSize, SectionAlignment);
        const size_t clusterTableSize = clusterCount * ClusterRecordSize;
        const size_t groupTableOffset = AlignUp(clusterTableOffset + clusterTableSize, SectionAlignment);
        const size_t groupTableSize = groupCount * GroupRecordSize;
        const size_t stringTableOffset = AlignUp(groupTableOffset + groupTableSize, SectionAlignment);
        const size_t vertexPayloadOffset = AlignUp(stringTableOffset + stringSize, SectionAlignment);
        const size_t vertexPayloadSize = vertexCount * VertexRecordSize;
        const size_t indexPayloadOffset = AlignUp(vertexPayloadOffset + vertexPayloadSize, SectionAlignment);
        const size_t indexPayloadSize = indexCount * sizeof(uint32_t);
        const size_t fileSize = indexPayloadOffset + indexPayloadSize;

        WriteBuffer bytes(fileSize, 0);
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
        WriteLe32(bytes, HeaderOffset::GroupRecordSize, static_cast<uint32_t>(GroupRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::SubmeshTableOffset, static_cast<uint64_t>(submeshTableOffset));
        WriteLe64(bytes, HeaderOffset::SubmeshTableSize, static_cast<uint64_t>(SubmeshRecordSize));
        WriteLe64(bytes, HeaderOffset::MaterialTableOffset, static_cast<uint64_t>(materialTableOffset));
        WriteLe64(bytes, HeaderOffset::MaterialTableSize, static_cast<uint64_t>(MaterialRecordSize));
        WriteLe64(bytes, HeaderOffset::ClusterTableOffset, static_cast<uint64_t>(clusterTableOffset));
        WriteLe64(bytes, HeaderOffset::ClusterTableSize, static_cast<uint64_t>(clusterTableSize));
        WriteLe64(bytes, HeaderOffset::GroupTableOffset, static_cast<uint64_t>(groupTableOffset));
        WriteLe64(bytes, HeaderOffset::GroupTableSize, static_cast<uint64_t>(groupTableSize));
        WriteLe64(bytes, HeaderOffset::StringTableOffset, static_cast<uint64_t>(stringTableOffset));
        WriteLe64(bytes, HeaderOffset::StringTableSize, static_cast<uint64_t>(stringSize));
        WriteLe64(bytes, HeaderOffset::VertexPayloadOffset, static_cast<uint64_t>(vertexPayloadOffset));
        WriteLe64(bytes, HeaderOffset::VertexPayloadSize, static_cast<uint64_t>(vertexPayloadSize));
        WriteLe64(bytes, HeaderOffset::IndexPayloadOffset, static_cast<uint64_t>(indexPayloadOffset));
        WriteLe64(bytes, HeaderOffset::IndexPayloadSize, static_cast<uint64_t>(indexPayloadSize));
        WriteLe32(bytes, HeaderOffset::VertexCount, static_cast<uint32_t>(vertexCount));
        WriteLe32(bytes, HeaderOffset::IndexCount, static_cast<uint32_t>(indexCount));
        WriteLe32(bytes, HeaderOffset::SubmeshCount, 1);
        WriteLe32(bytes, HeaderOffset::MaterialCount, 1);
        WriteLe32(bytes, HeaderOffset::ClusterCount, static_cast<uint32_t>(clusterCount));
        WriteLe32(bytes, HeaderOffset::GroupCount, static_cast<uint32_t>(groupCount));
        WriteLe32(bytes, HeaderOffset::StringByteCount, static_cast<uint32_t>(stringSize));
        WriteFloat3(bytes, HeaderOffset::TotalBoundsCenterX, input.TotalBoundsCenter);
        WriteLeFloat(bytes, HeaderOffset::TotalBoundsRadius, input.TotalBoundsRadius);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmId, ClusterAlgorithmId);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmVersion, ClusterAlgorithmVersion);
        WriteLe32(bytes, HeaderOffset::ClusterMaxTriangles, ClusterMaxTriangles);
        WriteLe32(bytes, HeaderOffset::ClusterMaxVertices, ClusterMaxVertices);
        WriteLe32(bytes, HeaderOffset::ClusterSettingsFlags, ClusterSettingsFlags);
        WriteLe32(bytes, HeaderOffset::LODLevelCount, input.LODLevelCount);
        WriteLe32(bytes, HeaderOffset::FallbackIndexOffset, static_cast<uint32_t>(clusterIndexCount));
        WriteLe32(bytes, HeaderOffset::FallbackIndexCount, static_cast<uint32_t>(input.FallbackIndices.size()));
        WriteLeFloat(bytes, HeaderOffset::FallbackError, input.FallbackError);

        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexCount, static_cast<uint32_t>(clusterIndexCount));
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexCount, static_cast<uint32_t>(vertexCount));
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterCount, static_cast<uint32_t>(clusterCount));
        WriteFloat3(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX, input.TotalBoundsCenter);
        WriteLeFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsRadius, input.TotalBoundsRadius);

        WriteV1MaterialStrings(bytes, materialTableOffset, stringTableOffset, input);

        for (size_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            WriteV1ClusterRecord(bytes, clusterTableOffset + clusterIndex * ClusterRecordSize,
                                 input.Clusters[clusterIndex]);
        }

        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            WriteV1GroupRecord(bytes, groupTableOffset + groupIndex * GroupRecordSize, input.Groups[groupIndex]);
        }

        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            WriteV1Vertex(bytes, vertexPayloadOffset + vertexIndex * VertexRecordSize, input.Vertices[vertexIndex]);
        }

        for (size_t index = 0; index < clusterIndexCount; ++index)
        {
            WriteLe32(bytes, indexPayloadOffset + index * sizeof(uint32_t), input.ClusterIndices[index]);
        }
        for (size_t index = 0; index < input.FallbackIndices.size(); ++index)
        {
            WriteLe32(bytes, indexPayloadOffset + (clusterIndexCount + index) * sizeof(uint32_t),
                      input.FallbackIndices[index]);
        }

        WriteLe64(bytes, HeaderOffset::PayloadHash,
                  ComputeCookedMeshPayloadHash(bytes.data() + submeshTableOffset, fileSize - submeshTableOffset));
        outBytes = std::move(bytes);
        return true;
    }

    namespace
    {
        // ページへ詰める単位。グループ（メンバは連続したクラスタ）か、グループを持たない1つのクラスタ（根）
        struct PageItem
        {
            uint32_t FirstCluster = 0;
            uint32_t ClusterCount = 0;
            uint32_t Level = 0;
            // ページの中で占めるバイト数（クラスタの記録・頂点・インデックス）
            uint32_t Bytes = 0;
            uint32_t GroupIndex = CookedMeshFormatV1::InvalidGroupId;
        };

        // 1ページに入れるものの割り当て（items の連続した範囲と、根のページならフォールバックの三角形の連続した範囲）
        struct PageAssignment
        {
            uint32_t Level = 0;
            uint32_t ItemBegin = 0;
            uint32_t ItemEnd = 0;
            // items の範囲のバイト数（クラスタの記録・頂点・インデックス）
            uint64_t ContentBytes = 0;
            // フォールバックの三角形の範囲（三角形の番号。ページが持つ頂点を含めたバイト数も数える）
            uint32_t FallbackTriangleBegin = 0;
            uint32_t FallbackTriangleEnd = 0;
            uint64_t FallbackBytes = 0;
        };

        uint64_t PageContentBytes(uint64_t clusterCount, uint64_t vertexCount, uint64_t indexCount,
                                  uint64_t fallbackIndexCount)
        {
            using namespace CookedMeshFormatV1;
            return PageHeaderSize + clusterCount * ClusterRecordSize + vertexCount * VertexRecordSize +
                   (indexCount + fallbackIndexCount) * sizeof(uint32_t);
        }

        bool IsFiniteCluster(const CookedMeshCluster& cluster)
        {
            return IsFinite(cluster.BoundsCenter) && std::isfinite(cluster.BoundsRadius) &&
                   std::isfinite(cluster.LODError) && IsFinite(cluster.ParentBoundsCenter) &&
                   std::isfinite(cluster.ParentBoundsRadius) && std::isfinite(cluster.ParentError);
        }
    } // namespace

    CookedMeshPagedWriteStatus SerializeCookedMeshV1Paged(const CookedMeshV1WriteInput& input,
                                                          const CookedMeshPagedWriteOptions& options,
                                                          Container::VariableArray<uint8_t>& outBytes,
                                                          CookedMeshPagedWriteInfo& outInfo)
    {
        using namespace CookedMeshFormatV1;

        outBytes.clear();
        outInfo = {};

        constexpr uint64_t max32 = std::numeric_limits<uint32_t>::max();
        const uint64_t indexTotal = static_cast<uint64_t>(input.ClusterIndices.size()) + input.FallbackIndices.size();
        const uint64_t stringTotal = static_cast<uint64_t>(input.AlbedoTexture.size()) + input.NormalTexture.size() +
                                     input.ArmTexture.size();
        if (input.Vertices.size() > max32 || input.Clusters.size() > max32 || input.Groups.size() > max32 ||
            indexTotal > max32 || stringTotal > max32)
        {
            return CookedMeshPagedWriteStatus::TooLarge;
        }

        const size_t vertexCount = input.Vertices.size();
        const size_t clusterCount = input.Clusters.size();
        const size_t groupCount = input.Groups.size();
        const size_t clusterIndexCount = input.ClusterIndices.size();
        const size_t stringSize = static_cast<size_t>(stringTotal);

        if (clusterCount == 0 || input.LODLevelCount == 0 || input.LODLevelCount > MaxLODLevels ||
            input.FallbackIndices.empty() || input.FallbackIndices.size() % 3 != 0 ||
            options.PageSizeBytes <= PageHeaderSize || options.PageSizeBytes > PageSize)
        {
            return CookedMeshPagedWriteStatus::InvalidInput;
        }

        // 入力の範囲の検査（読み出しが配列の外へ出ないことと、並べ替えに使う値が有限なこと）
        for (const CookedMeshCluster& cluster : input.Clusters)
        {
            if (cluster.IndexCount == 0 ||
                static_cast<uint64_t>(cluster.IndexOffset) + cluster.IndexCount > clusterIndexCount ||
                cluster.VertexCount == 0 || static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount > vertexCount ||
                cluster.LODLevel >= input.LODLevelCount || !IsFiniteCluster(cluster) ||
                (cluster.SourceGroupId != InvalidGroupId && cluster.SourceGroupId >= groupCount))
            {
                return CookedMeshPagedWriteStatus::InvalidInput;
            }
        }
        for (const uint32_t index : input.FallbackIndices)
        {
            if (index >= vertexCount)
            {
                return CookedMeshPagedWriteStatus::InvalidInput;
            }
        }

        Container::VariableArray<uint32_t> groupOfCluster(clusterCount, InvalidGroupId);
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const CookedMeshClusterGroup& group = input.Groups[groupIndex];
            if (group.ClusterCount == 0 || static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount > clusterCount)
            {
                return CookedMeshPagedWriteStatus::InvalidInput;
            }
            for (uint32_t member = group.ClusterOffset; member < group.ClusterOffset + group.ClusterCount; ++member)
            {
                if (groupOfCluster[member] != InvalidGroupId)
                {
                    return CookedMeshPagedWriteStatus::InvalidInput;
                }
                groupOfCluster[member] = static_cast<uint32_t>(groupIndex);
            }
        }

        // 詰める単位（グループ、またはグループを持たないクラスタ）。粗い段から、段の中は元の並びで並べる
        const auto clusterBytes = [&input](uint32_t clusterIndex) -> uint32_t
        {
            const CookedMeshCluster& cluster = input.Clusters[clusterIndex];
            return static_cast<uint32_t>(ClusterRecordSize + static_cast<size_t>(cluster.VertexCount) * VertexRecordSize +
                                         static_cast<size_t>(cluster.IndexCount) * sizeof(uint32_t));
        };
        Container::VariableArray<PageItem> items;
        items.reserve(clusterCount);
        Container::VariableArray<uint64_t> levelBytes(input.LODLevelCount, 0);
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const CookedMeshClusterGroup& group = input.Groups[groupIndex];
            PageItem item;
            item.FirstCluster = group.ClusterOffset;
            item.ClusterCount = group.ClusterCount;
            item.Level = input.Clusters[group.ClusterOffset].LODLevel;
            item.GroupIndex = static_cast<uint32_t>(groupIndex);
            uint64_t bytes = 0;
            for (uint32_t member = group.ClusterOffset; member < group.ClusterOffset + group.ClusterCount; ++member)
            {
                bytes += clusterBytes(member);
            }
            if (bytes > max32)
            {
                return CookedMeshPagedWriteStatus::TooLarge;
            }
            item.Bytes = static_cast<uint32_t>(bytes);
            levelBytes[item.Level] += bytes;
            items.push_back(item);
        }
        for (uint32_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            if (groupOfCluster[clusterIndex] != InvalidGroupId)
            {
                continue;
            }
            PageItem item;
            item.FirstCluster = clusterIndex;
            item.ClusterCount = 1;
            item.Level = input.Clusters[clusterIndex].LODLevel;
            item.Bytes = clusterBytes(clusterIndex);
            levelBytes[item.Level] += item.Bytes;
            items.push_back(item);
        }
        std::sort(items.begin(), items.end(),
                  [](const PageItem& left, const PageItem& right)
                  {
                      return left.Level != right.Level ? left.Level > right.Level : left.FirstCluster < right.FirstCluster;
                  });

        // 根のページに入れる段: 最も粗い段は常に入り、その下の段は目安に収まる間だけ段ごとまとめて入る
        const uint32_t topLevel = input.LODLevelCount - 1;
        uint32_t rootMinLevel = topLevel;
        uint64_t rootClusterBytes = levelBytes[topLevel];
        for (uint32_t level = topLevel; level-- > 0;)
        {
            if (rootClusterBytes + levelBytes[level] > options.RootClusterBudgetBytes)
            {
                break;
            }
            rootClusterBytes += levelBytes[level];
            rootMinLevel = level;
        }

        uint32_t rootItemCount = 0;
        while (rootItemCount < items.size() && items[rootItemCount].Level >= rootMinLevel)
        {
            ++rootItemCount;
        }

        // 割り当て。0 番から連続する根のページ（常駐）→ 他のページの順に並べる。
        //   - 根のページ: 根に入る段のグループを、1つのグループが1つのページに収まるように順に詰め、続けてフォールバックの
        //     三角形を（その三角形が使う頂点つきで）空きから順に入れる。入りきらなければクラスタを持たない根のページを足す
        //   - 他のページ: 1つの段のグループを順に詰める（段が変わるか、入りきらなければ次のページ）
        const auto alignedPageBytes = [](uint64_t contentBytes) -> uint64_t
        { return (contentBytes + SectionAlignment - 1) & ~static_cast<uint64_t>(SectionAlignment - 1); };
        for (const PageItem& item : items)
        {
            if (alignedPageBytes(PageHeaderSize + static_cast<uint64_t>(item.Bytes)) > options.PageSizeBytes)
            {
                outInfo.LargestGroupBytes = item.Bytes;
                outInfo.LargestGroupIndex = item.GroupIndex != InvalidGroupId ? item.GroupIndex : item.FirstCluster;
                return CookedMeshPagedWriteStatus::GroupExceedsPage;
            }
            if (item.Bytes > outInfo.LargestGroupBytes)
            {
                outInfo.LargestGroupBytes = item.Bytes;
                outInfo.LargestGroupIndex = item.GroupIndex != InvalidGroupId ? item.GroupIndex : item.FirstCluster;
            }
        }

        Container::VariableArray<PageAssignment> assignments;
        for (uint32_t itemIndex = 0; itemIndex < rootItemCount; ++itemIndex)
        {
            const PageItem& item = items[itemIndex];
            if (assignments.empty() ||
                alignedPageBytes(PageHeaderSize + assignments.back().ContentBytes + item.Bytes) > options.PageSizeBytes)
            {
                PageAssignment page;
                page.Level = item.Level;
                page.ItemBegin = itemIndex;
                page.ItemEnd = itemIndex;
                assignments.push_back(page);
            }
            assignments.back().ItemEnd = itemIndex + 1;
            assignments.back().ContentBytes += item.Bytes;
        }

        // フォールバックの三角形を、根のページの空きへ順に入れる。ページごとに、使う頂点を1回だけ数える
        {
            const uint32_t fallbackTriangleCount = static_cast<uint32_t>(input.FallbackIndices.size() / 3);
            Container::VariableArray<uint32_t> countedInPage(vertexCount, InvalidPageId);
            const auto newVertexCount = [&](const uint32_t* triangle, size_t pageIndex) -> uint32_t
            {
                uint32_t count = 0;
                for (int corner = 0; corner < 3; ++corner)
                {
                    const uint32_t vertex = triangle[corner];
                    const bool bDuplicate = (corner >= 1 && vertex == triangle[0]) || (corner == 2 && vertex == triangle[1]);
                    if (countedInPage[vertex] != pageIndex && !bDuplicate)
                    {
                        ++count;
                    }
                }
                return count;
            };
            for (uint32_t triangleIndex = 0; triangleIndex < fallbackTriangleCount; ++triangleIndex)
            {
                const uint32_t* triangle = input.FallbackIndices.data() + static_cast<size_t>(triangleIndex) * 3;
                size_t pageIndex = assignments.size() - 1;
                uint64_t triangleBytes =
                    3 * sizeof(uint32_t) + static_cast<uint64_t>(newVertexCount(triangle, pageIndex)) * VertexRecordSize;
                if (alignedPageBytes(PageHeaderSize + assignments[pageIndex].ContentBytes +
                                     assignments[pageIndex].FallbackBytes + triangleBytes) > options.PageSizeBytes)
                {
                    PageAssignment page;
                    page.Level = topLevel;
                    page.ItemBegin = rootItemCount;
                    page.ItemEnd = rootItemCount;
                    assignments.push_back(page);
                    pageIndex = assignments.size() - 1;
                    triangleBytes = 3 * sizeof(uint32_t) +
                                    static_cast<uint64_t>(newVertexCount(triangle, pageIndex)) * VertexRecordSize;
                    if (alignedPageBytes(PageHeaderSize + triangleBytes) > options.PageSizeBytes)
                    {
                        outInfo.LargestGroupBytes = static_cast<uint32_t>(triangleBytes);
                        outInfo.LargestGroupIndex = triangleIndex;
                        return CookedMeshPagedWriteStatus::GroupExceedsPage;
                    }
                }

                PageAssignment& target = assignments[pageIndex];
                if (target.FallbackTriangleBegin == target.FallbackTriangleEnd)
                {
                    target.FallbackTriangleBegin = triangleIndex;
                }
                target.FallbackTriangleEnd = triangleIndex + 1;
                target.FallbackBytes += triangleBytes;
                for (int corner = 0; corner < 3; ++corner)
                {
                    countedInPage[triangle[corner]] = static_cast<uint32_t>(pageIndex);
                }
            }
        }

        const size_t residentPageCount = assignments.size();
        for (uint32_t itemIndex = rootItemCount; itemIndex < items.size(); ++itemIndex)
        {
            const PageItem& item = items[itemIndex];
            const bool bOpenNewPage =
                assignments.size() == residentPageCount || assignments.back().Level != item.Level ||
                alignedPageBytes(PageHeaderSize + assignments.back().ContentBytes + item.Bytes) > options.PageSizeBytes;
            if (bOpenNewPage)
            {
                PageAssignment page;
                page.Level = item.Level;
                page.ItemBegin = itemIndex;
                page.ItemEnd = itemIndex;
                assignments.push_back(page);
            }
            assignments.back().ItemEnd = itemIndex + 1;
            assignments.back().ContentBytes += item.Bytes;
        }
        if (assignments.size() > max32)
        {
            return CookedMeshPagedWriteStatus::TooLarge;
        }

        // 新しいクラスタの並び（ページの順、ページの中は詰めた順）。グループのメンバは連続したまま
        const size_t pageCount = assignments.size();
        Container::VariableArray<uint32_t> newOrder;
        newOrder.reserve(clusterCount);
        Container::VariableArray<uint32_t> newIndexOfCluster(clusterCount, 0);
        Container::VariableArray<uint32_t> pageFirstCluster(pageCount, 0);
        Container::VariableArray<uint32_t> pageClusterCount(pageCount, 0);
        for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            const PageAssignment& assignment = assignments[pageIndex];
            pageFirstCluster[pageIndex] = static_cast<uint32_t>(newOrder.size());
            for (uint32_t itemIndex = assignment.ItemBegin; itemIndex < assignment.ItemEnd; ++itemIndex)
            {
                const PageItem& item = items[itemIndex];
                for (uint32_t member = item.FirstCluster; member < item.FirstCluster + item.ClusterCount; ++member)
                {
                    newIndexOfCluster[member] = static_cast<uint32_t>(newOrder.size());
                    newOrder.push_back(member);
                }
            }
            pageClusterCount[pageIndex] = static_cast<uint32_t>(newOrder.size()) - pageFirstCluster[pageIndex];
        }

        // 各ページのバイト列。ページの中でクラスタごとに自分の頂点・インデックスを連続して持ち、
        // クラスタの記録は、ページの中の位置を指す。根のページは、フォールバックの専用の頂点（使う頂点だけ）を持つ
        Container::VariableArray<Container::VariableArray<uint8_t>> pageBytes(pageCount);
        Container::VariableArray<uint32_t> pageVertexCount(pageCount, 0);
        Container::VariableArray<uint32_t> pageIndexCount(pageCount, 0);
        // フォールバックの頂点を、ページの中のどの位置へ置いたか（置いたページの番号と、その位置）
        Container::VariableArray<uint32_t> fallbackPageOfVertex(vertexCount, InvalidPageId);
        Container::VariableArray<uint32_t> fallbackLocalIndex(vertexCount, 0);
        // 全ページのクラスタ（ページの順。親のページの導出に使う）
        Container::VariableArray<CookedMeshCluster> orderedClusters;
        orderedClusters.reserve(clusterCount);
        uint64_t totalVertices = 0;
        uint64_t regionSize = 0;
        for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            Container::VariableArray<CookedMeshCluster> pageClusters;
            Container::VariableArray<CookedMeshVertex> pageVertices;
            Container::VariableArray<uint32_t> pageIndices;
            Container::VariableArray<uint32_t> pageFallback;
            const bool bResident = pageIndex < residentPageCount;
            pageClusters.reserve(pageClusterCount[pageIndex]);
            for (uint32_t order = pageFirstCluster[pageIndex]; order < pageFirstCluster[pageIndex] + pageClusterCount[pageIndex];
                 ++order)
            {
                CookedMeshCluster cluster = input.Clusters[newOrder[order]];
                for (uint32_t vertex = 0; vertex < cluster.VertexCount; ++vertex)
                {
                    pageVertices.push_back(input.Vertices[cluster.VertexOffset + vertex]);
                }
                for (uint32_t index = 0; index < cluster.IndexCount; ++index)
                {
                    pageIndices.push_back(input.ClusterIndices[cluster.IndexOffset + index]);
                }
                cluster.VertexOffset = static_cast<uint32_t>(pageVertices.size()) - cluster.VertexCount;
                cluster.IndexOffset = static_cast<uint32_t>(pageIndices.size()) - cluster.IndexCount;
                cluster.PageId = static_cast<uint32_t>(pageIndex);
                pageClusters.push_back(cluster);
                orderedClusters.push_back(cluster);
            }

            if (bResident)
            {
                const PageAssignment& assignment = assignments[pageIndex];
                pageFallback.reserve(static_cast<size_t>(assignment.FallbackTriangleEnd - assignment.FallbackTriangleBegin) * 3);
                for (size_t fallbackIndex = static_cast<size_t>(assignment.FallbackTriangleBegin) * 3;
                     fallbackIndex < static_cast<size_t>(assignment.FallbackTriangleEnd) * 3; ++fallbackIndex)
                {
                    const uint32_t sourceVertex = input.FallbackIndices[fallbackIndex];
                    if (fallbackPageOfVertex[sourceVertex] != pageIndex)
                    {
                        fallbackPageOfVertex[sourceVertex] = static_cast<uint32_t>(pageIndex);
                        fallbackLocalIndex[sourceVertex] = static_cast<uint32_t>(pageVertices.size());
                        pageVertices.push_back(input.Vertices[sourceVertex]);
                    }
                    pageFallback.push_back(fallbackLocalIndex[sourceVertex]);
                }
            }

            const uint64_t contentBytes =
                PageContentBytes(pageClusters.size(), pageVertices.size(), pageIndices.size(), pageFallback.size());
            const uint64_t pageSize = alignedPageBytes(contentBytes);
            if (pageSize > max32 || pageVertices.size() > max32)
            {
                return CookedMeshPagedWriteStatus::TooLarge;
            }

            const size_t clusterOffset = PageHeaderSize;
            const size_t vertexOffset = clusterOffset + pageClusters.size() * ClusterRecordSize;
            const size_t indexOffset = vertexOffset + pageVertices.size() * VertexRecordSize;
            const size_t fallbackOffset = indexOffset + pageIndices.size() * sizeof(uint32_t);
            WriteBuffer page(static_cast<size_t>(pageSize), 0);
            WriteLe32(page, PageHeaderOffset::ClusterCount, static_cast<uint32_t>(pageClusters.size()));
            WriteLe32(page, PageHeaderOffset::VertexCount, static_cast<uint32_t>(pageVertices.size()));
            WriteLe32(page, PageHeaderOffset::IndexCount, static_cast<uint32_t>(pageIndices.size()));
            WriteLe32(page, PageHeaderOffset::FallbackIndexCount, static_cast<uint32_t>(pageFallback.size()));
            WriteLe32(page, PageHeaderOffset::ClusterOffset, static_cast<uint32_t>(clusterOffset));
            WriteLe32(page, PageHeaderOffset::VertexOffset, static_cast<uint32_t>(vertexOffset));
            WriteLe32(page, PageHeaderOffset::IndexOffset, static_cast<uint32_t>(indexOffset));
            WriteLe32(page, PageHeaderOffset::FallbackIndexOffset, static_cast<uint32_t>(fallbackOffset));
            WriteLe32(page, PageHeaderOffset::PageId, static_cast<uint32_t>(pageIndex));
            WriteLe32(page, PageHeaderOffset::Flags, bResident ? PageFlagRoot : 0u);
            for (size_t clusterIndex = 0; clusterIndex < pageClusters.size(); ++clusterIndex)
            {
                WriteV1ClusterRecord(page, clusterOffset + clusterIndex * ClusterRecordSize, pageClusters[clusterIndex]);
            }
            for (size_t vertexIndex = 0; vertexIndex < pageVertices.size(); ++vertexIndex)
            {
                WriteV1Vertex(page, vertexOffset + vertexIndex * VertexRecordSize, pageVertices[vertexIndex]);
            }
            for (size_t index = 0; index < pageIndices.size(); ++index)
            {
                WriteLe32(page, indexOffset + index * sizeof(uint32_t), pageIndices[index]);
            }
            for (size_t index = 0; index < pageFallback.size(); ++index)
            {
                WriteLe32(page, fallbackOffset + index * sizeof(uint32_t), pageFallback[index]);
            }

            pageVertexCount[pageIndex] = static_cast<uint32_t>(pageVertices.size());
            pageIndexCount[pageIndex] = static_cast<uint32_t>(pageIndices.size());
            totalVertices += pageVertices.size();
            regionSize += pageSize;
            pageBytes[pageIndex] = std::move(page);

            outInfo.MaxPageBytes = std::max(outInfo.MaxPageBytes, static_cast<uint32_t>(pageSize));
            if (bResident)
            {
                outInfo.RootPageBytes += static_cast<uint32_t>(pageSize);
                outInfo.RootPageClusterCount += static_cast<uint32_t>(pageClusters.size());
            }
            else
            {
                outInfo.NonRootPageBytes += pageSize;
            }
        }
        if (totalVertices > max32)
        {
            return CookedMeshPagedWriteStatus::TooLarge;
        }
        outInfo.PageCount = static_cast<uint32_t>(pageCount);
        outInfo.RootPageCount = static_cast<uint32_t>(residentPageCount);
        outInfo.RootPageMinLODLevel = rootMinLevel;

        // 親のページ（読み込みが同じ規則で導いて、表の値と照合する）
        Container::VariableArray<uint32_t> parentPageOfPage;
        ComputePageParentPages(orderedClusters, pageCount, residentPageCount, parentPageOfPage);

        // グループの BVH（並べ替えた後のクラスタの番号で作る。葉の範囲が実際の並びを指す）
        Container::VariableArray<CookedMeshClusterGroup> orderedGroups;
        orderedGroups.reserve(groupCount);
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            CookedMeshClusterGroup group = input.Groups[groupIndex];
            group.ClusterOffset = newIndexOfCluster[group.ClusterOffset];
            orderedGroups.push_back(group);
        }
        Container::VariableArray<CookedMeshGroupBVHNode> groupBVH;
        Container::VariableArray<uint32_t> groupBVHLevelNodeCounts;
        if (!BuildCookedMeshGroupBVH(orderedClusters, orderedGroups, groupBVH))
        {
            return CookedMeshPagedWriteStatus::TooLarge;
        }
        if (CheckCookedMeshGroupBVH(groupBVH, orderedClusters, groupBVHLevelNodeCounts) != CookedMeshParseStatus::Success)
        {
            return CookedMeshPagedWriteStatus::InvalidInput;
        }
        uint32_t groupBVHLeafCount = 0;
        for (const CookedMeshGroupBVHNode& node : groupBVH)
        {
            groupBVHLeafCount += node.bLeaf ? 1u : 0u;
        }
        outInfo.GroupBVHNodeCount = static_cast<uint32_t>(groupBVH.size());
        outInfo.GroupBVHLeafCount = groupBVHLeafCount;
        outInfo.GroupBVHLevelCount = static_cast<uint32_t>(groupBVHLevelNodeCounts.size());

        // ファイル全体: header -> submesh -> material -> page table -> group（+ BVH） -> string -> page region
        const size_t submeshTableOffset = HeaderSize;
        const size_t materialTableOffset = AlignUp(submeshTableOffset + SubmeshRecordSize, SectionAlignment);
        const size_t pageTableOffset = AlignUp(materialTableOffset + MaterialRecordSize, SectionAlignment);
        const size_t pageTableSize = pageCount * PageTableRecordSize;
        const size_t groupTableOffset = AlignUp(pageTableOffset + pageTableSize, SectionAlignment);
        const size_t groupRecordBytes = groupCount * GroupRecordSize;
        const size_t groupTableSize = groupRecordBytes + GroupBVHHeaderSize + groupBVH.size() * GroupBVHNodeRecordSize;
        const size_t stringTableOffset = AlignUp(groupTableOffset + groupTableSize, SectionAlignment);
        const size_t pageRegionOffset = AlignUp(stringTableOffset + stringSize, SectionAlignment);
        const size_t fileSize = pageRegionOffset + static_cast<size_t>(regionSize);

        WriteBuffer bytes(fileSize, 0);
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinorPaged);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::VertexRecordSize, static_cast<uint32_t>(VertexRecordSize));
        WriteLe32(bytes, HeaderOffset::SubmeshRecordSize, static_cast<uint32_t>(SubmeshRecordSize));
        WriteLe32(bytes, HeaderOffset::MaterialRecordSize, static_cast<uint32_t>(MaterialRecordSize));
        WriteLe32(bytes, HeaderOffset::ClusterRecordSize, static_cast<uint32_t>(ClusterRecordSize));
        WriteLe32(bytes, HeaderOffset::StringRefRecordSize, static_cast<uint32_t>(StringRefRecordSize));
        WriteLe32(bytes, HeaderOffset::GroupRecordSize, static_cast<uint32_t>(GroupRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::SubmeshTableOffset, static_cast<uint64_t>(submeshTableOffset));
        WriteLe64(bytes, HeaderOffset::SubmeshTableSize, static_cast<uint64_t>(SubmeshRecordSize));
        WriteLe64(bytes, HeaderOffset::MaterialTableOffset, static_cast<uint64_t>(materialTableOffset));
        WriteLe64(bytes, HeaderOffset::MaterialTableSize, static_cast<uint64_t>(MaterialRecordSize));
        WriteLe64(bytes, PagedHeaderOffset::PageTableOffset, static_cast<uint64_t>(pageTableOffset));
        WriteLe64(bytes, PagedHeaderOffset::PageTableSize, static_cast<uint64_t>(pageTableSize));
        WriteLe64(bytes, HeaderOffset::GroupTableOffset, static_cast<uint64_t>(groupTableOffset));
        WriteLe64(bytes, HeaderOffset::GroupTableSize, static_cast<uint64_t>(groupTableSize));
        WriteLe64(bytes, HeaderOffset::StringTableOffset, static_cast<uint64_t>(stringTableOffset));
        WriteLe64(bytes, HeaderOffset::StringTableSize, static_cast<uint64_t>(stringSize));
        WriteLe64(bytes, PagedHeaderOffset::PageRegionOffset, static_cast<uint64_t>(pageRegionOffset));
        WriteLe64(bytes, PagedHeaderOffset::PageRegionSize, static_cast<uint64_t>(regionSize));
        WriteLe64(bytes, HeaderOffset::IndexPayloadOffset, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::IndexPayloadSize, 0);
        WriteLe32(bytes, HeaderOffset::VertexCount, static_cast<uint32_t>(totalVertices));
        WriteLe32(bytes, HeaderOffset::IndexCount, static_cast<uint32_t>(indexTotal));
        WriteLe32(bytes, HeaderOffset::Flags, HeaderFlagGroupBVH);
        WriteLe32(bytes, HeaderOffset::SubmeshCount, 1);
        WriteLe32(bytes, HeaderOffset::MaterialCount, 1);
        WriteLe32(bytes, HeaderOffset::ClusterCount, static_cast<uint32_t>(clusterCount));
        WriteLe32(bytes, HeaderOffset::GroupCount, static_cast<uint32_t>(groupCount));
        WriteLe32(bytes, HeaderOffset::StringByteCount, static_cast<uint32_t>(stringSize));
        WriteFloat3(bytes, HeaderOffset::TotalBoundsCenterX, input.TotalBoundsCenter);
        WriteLeFloat(bytes, HeaderOffset::TotalBoundsRadius, input.TotalBoundsRadius);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmId, ClusterAlgorithmId);
        WriteLe32(bytes, HeaderOffset::ClusterAlgorithmVersion, ClusterAlgorithmVersion);
        WriteLe32(bytes, HeaderOffset::ClusterMaxTriangles, ClusterMaxTriangles);
        WriteLe32(bytes, HeaderOffset::ClusterMaxVertices, ClusterMaxVertices);
        WriteLe32(bytes, HeaderOffset::ClusterSettingsFlags, ClusterSettingsFlags);
        WriteLe32(bytes, HeaderOffset::LODLevelCount, input.LODLevelCount);
        WriteLe32(bytes, HeaderOffset::FallbackIndexOffset, static_cast<uint32_t>(clusterIndexCount));
        WriteLe32(bytes, HeaderOffset::FallbackIndexCount, static_cast<uint32_t>(input.FallbackIndices.size()));
        WriteLeFloat(bytes, HeaderOffset::FallbackError, input.FallbackError);

        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::IndexCount, static_cast<uint32_t>(clusterIndexCount));
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::VertexCount, static_cast<uint32_t>(totalVertices));
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::MaterialIndex, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterOffset, 0);
        WriteLe32(bytes, submeshTableOffset + SubmeshRecordOffset::ClusterCount, static_cast<uint32_t>(clusterCount));
        WriteFloat3(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsCenterX, input.TotalBoundsCenter);
        WriteLeFloat(bytes, submeshTableOffset + SubmeshRecordOffset::BoundsRadius, input.TotalBoundsRadius);

        WriteV1MaterialStrings(bytes, materialTableOffset, stringTableOffset, input);

        // グループの表は元の並び（GroupId は変わらない）で、メンバの先頭だけ新しいクラスタの並びへ直す
        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            CookedMeshClusterGroup group = input.Groups[groupIndex];
            group.ClusterOffset = newIndexOfCluster[group.ClusterOffset];
            WriteV1GroupRecord(bytes, groupTableOffset + groupIndex * GroupRecordSize, group);
        }

        // グループの記録の後ろに、BVH の頭と節
        {
            const size_t bvhOffset = groupTableOffset + groupRecordBytes;
            WriteLe32(bytes, bvhOffset + GroupBVHHeaderOffset::NodeCount, static_cast<uint32_t>(groupBVH.size()));
            WriteLe32(bytes, bvhOffset + GroupBVHHeaderOffset::LevelCount,
                      static_cast<uint32_t>(groupBVHLevelNodeCounts.size()));
            WriteLe32(bytes, bvhOffset + GroupBVHHeaderOffset::LeafCount, groupBVHLeafCount);
            for (size_t nodeIndex = 0; nodeIndex < groupBVH.size(); ++nodeIndex)
            {
                const CookedMeshGroupBVHNode& node = groupBVH[nodeIndex];
                const size_t recordOffset = bvhOffset + GroupBVHHeaderSize + nodeIndex * GroupBVHNodeRecordSize;
                WriteFloat3(bytes, recordOffset + GroupBVHNodeOffset::BoundsCenterX, node.BoundsCenter);
                WriteLeFloat(bytes, recordOffset + GroupBVHNodeOffset::BoundsRadius, node.BoundsRadius);
                WriteLeFloat(bytes, recordOffset + GroupBVHNodeOffset::MaxParentError, node.MaxParentError);
                WriteLe32(bytes, recordOffset + GroupBVHNodeOffset::First, node.First);
                WriteLe32(bytes, recordOffset + GroupBVHNodeOffset::Count, node.Count);
                WriteLe32(bytes, recordOffset + GroupBVHNodeOffset::Flags, node.bLeaf ? GroupBVHNodeFlagLeaf : 0u);
            }
        }

        // ページの領域（ページを隙間なく並べる）とページの表
        size_t pageFileOffset = pageRegionOffset;
        for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            const WriteBuffer& page = pageBytes[pageIndex];
            std::memcpy(bytes.data() + pageFileOffset, page.data(), page.size());

            const size_t recordOffset = pageTableOffset + pageIndex * PageTableRecordSize;
            WriteLe64(bytes, recordOffset + PageTableRecordOffset::FileOffset, static_cast<uint64_t>(pageFileOffset));
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::Size, static_cast<uint32_t>(page.size()));
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::ParentPageId, parentPageOfPage[pageIndex]);
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::Flags,
                      pageIndex < residentPageCount ? PageFlagRoot : 0u);
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::ClusterCount, pageClusterCount[pageIndex]);
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::FirstClusterIndex, pageFirstCluster[pageIndex]);
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::VertexCount, pageVertexCount[pageIndex]);
            WriteLe32(bytes, recordOffset + PageTableRecordOffset::IndexCount, pageIndexCount[pageIndex]);
            WriteLe64(bytes, recordOffset + PageTableRecordOffset::PageHash,
                      ComputeCookedMeshPayloadHash(page.data(), page.size()));
            pageFileOffset += page.size();
        }

        WriteLe64(bytes, HeaderOffset::PayloadHash,
                  ComputeCookedMeshPayloadHash(bytes.data() + submeshTableOffset, fileSize - submeshTableOffset));
        outBytes = std::move(bytes);
        return CookedMeshPagedWriteStatus::Success;
    }

    // ========================================
    // グループの BVH（v1.1）
    // ========================================

    namespace
    {
        // BVH の葉になる1単位（1つのグループ、または連続した根のクラスタの、メンバの連続した範囲）。
        // 球は、メンバの境界球と（グループなら）親の境界球を包む。値は float で表せる値。
        struct GroupBVHUnit
        {
            double Center[3] = {0.0, 0.0, 0.0};
            double Radius = 0.0;
            float MaxParentError = 0.0f;
            uint32_t First = 0;
            uint32_t Count = 0;
        };

        // 構築中の節。子は構築中の節の表の添字
        struct GroupBVHBuildNode
        {
            double Center[3] = {0.0, 0.0, 0.0};
            double Radius = 0.0;
            float MaxParentError = 0.0f;
            uint32_t First = 0;
            uint32_t Count = 0;
            bool bLeaf = false;
            Container::VariableArray<uint32_t> Children;
        };

        // 球 a を、球 b も包むように広げる（どちらかが片方を含むなら大きい方）
        void GrowSphere(double (&centerA)[3], double& radiusA, const double (&centerB)[3], double radiusB)
        {
            const double dx = centerB[0] - centerA[0];
            const double dy = centerB[1] - centerA[1];
            const double dz = centerB[2] - centerA[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance + radiusB <= radiusA)
            {
                return;
            }
            if (distance + radiusA <= radiusB)
            {
                centerA[0] = centerB[0];
                centerA[1] = centerB[1];
                centerA[2] = centerB[2];
                radiusA = radiusB;
                return;
            }
            const double newRadius = (distance + radiusA + radiusB) * 0.5;
            const double t = (newRadius - radiusA) / distance;
            centerA[0] += dx * t;
            centerA[1] += dy * t;
            centerA[2] += dz * t;
            radiusA = newRadius;
        }

        // 球の中心を float で表せる値へ丸め、全ての子の球を（丸めた値のまま）包む半径にする。
        // 浮動小数の丸めと、GPU が行列で変換する際の誤差の分だけ半径に余裕を足す
        void FinalizeBVHSphere(double (&center)[3], double& radius, const double (*childCenters)[3],
                               const double* childRadii, size_t childCount)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                center[axis] = static_cast<double>(static_cast<float>(center[axis]));
            }
            double need = 0.0;
            for (size_t child = 0; child < childCount; ++child)
            {
                const double dx = childCenters[child][0] - center[0];
                const double dy = childCenters[child][1] - center[1];
                const double dz = childCenters[child][2] - center[2];
                need = std::max(need, std::sqrt(dx * dx + dy * dy + dz * dz) + childRadii[child]);
            }
            const double extent = std::fabs(center[0]) + std::fabs(center[1]) + std::fabs(center[2]) + need;
            float rounded = static_cast<float>(need * (1.0 + 1.0e-5) + extent * 1.0e-7);
            if (static_cast<double>(rounded) < need)
            {
                rounded = std::nextafter(rounded, std::numeric_limits<float>::infinity());
            }
            radius = static_cast<double>(rounded);
        }

        // 単位の並びの [begin, end) を、重心の最も広い軸の中央値で2つに分ける（同じ値は先頭のクラスタの番号で順を決める）
        uint32_t SplitUnitsInHalf(const Container::VariableArray<GroupBVHUnit>& units,
                                  Container::VariableArray<uint32_t>& order, uint32_t begin, uint32_t end)
        {
            double low[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                             std::numeric_limits<double>::max()};
            double high[3] = {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                              std::numeric_limits<double>::lowest()};
            for (uint32_t index = begin; index < end; ++index)
            {
                const GroupBVHUnit& unit = units[order[index]];
                for (int axis = 0; axis < 3; ++axis)
                {
                    low[axis] = std::min(low[axis], unit.Center[axis]);
                    high[axis] = std::max(high[axis], unit.Center[axis]);
                }
            }
            int widest = 0;
            for (int axis = 1; axis < 3; ++axis)
            {
                if (high[axis] - low[axis] > high[widest] - low[widest])
                {
                    widest = axis;
                }
            }
            const uint32_t middle = begin + (end - begin) / 2;
            std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                             [&units, widest](uint32_t left, uint32_t right)
                             {
                                 const GroupBVHUnit& a = units[left];
                                 const GroupBVHUnit& b = units[right];
                                 if (a.Center[widest] != b.Center[widest])
                                 {
                                     return a.Center[widest] < b.Center[widest];
                                 }
                                 return a.First < b.First;
                             });
            return middle;
        }

        // 単位の並びの [begin, end) から部分木を作り、根の節の番号を返す
        uint32_t BuildGroupBVHSubtree(const Container::VariableArray<GroupBVHUnit>& units,
                                      Container::VariableArray<uint32_t>& order, uint32_t begin, uint32_t end,
                                      Container::VariableArray<GroupBVHBuildNode>& nodes)
        {
            using namespace CookedMeshFormatV1;

            const auto makeLeaf = [&units, &nodes](uint32_t unitIndex) -> uint32_t
            {
                const GroupBVHUnit& unit = units[unitIndex];
                GroupBVHBuildNode node;
                node.Center[0] = unit.Center[0];
                node.Center[1] = unit.Center[1];
                node.Center[2] = unit.Center[2];
                node.Radius = unit.Radius;
                node.MaxParentError = unit.MaxParentError;
                node.First = unit.First;
                node.Count = unit.Count;
                node.bLeaf = true;
                nodes.push_back(std::move(node));
                return static_cast<uint32_t>(nodes.size() - 1);
            };

            if (end - begin == 1)
            {
                return makeLeaf(order[begin]);
            }

            Container::VariableArray<uint32_t> children;
            if (end - begin <= GroupBVHMaxChildren)
            {
                for (uint32_t index = begin; index < end; ++index)
                {
                    children.push_back(makeLeaf(order[index]));
                }
            }
            else
            {
                // 中央値で3回分けて、最大 8 つの部分に分ける
                Container::VariableArray<uint32_t> bounds;
                bounds.push_back(begin);
                bounds.push_back(end);
                for (int round = 0; round < 3; ++round)
                {
                    Container::VariableArray<uint32_t> next;
                    for (size_t part = 0; part + 1 < bounds.size(); ++part)
                    {
                        next.push_back(bounds[part]);
                        if (bounds[part + 1] - bounds[part] >= 2)
                        {
                            next.push_back(SplitUnitsInHalf(units, order, bounds[part], bounds[part + 1]));
                        }
                    }
                    next.push_back(end);
                    bounds = std::move(next);
                }
                for (size_t part = 0; part + 1 < bounds.size(); ++part)
                {
                    children.push_back(BuildGroupBVHSubtree(units, order, bounds[part], bounds[part + 1], nodes));
                }
            }

            GroupBVHBuildNode node;
            node.bLeaf = false;
            node.Count = static_cast<uint32_t>(children.size());
            double centers[GroupBVHMaxChildren][3] = {};
            double radii[GroupBVHMaxChildren] = {};
            node.Center[0] = nodes[children[0]].Center[0];
            node.Center[1] = nodes[children[0]].Center[1];
            node.Center[2] = nodes[children[0]].Center[2];
            node.Radius = nodes[children[0]].Radius;
            node.MaxParentError = 0.0f;
            for (size_t child = 0; child < children.size(); ++child)
            {
                const GroupBVHBuildNode& childNode = nodes[children[child]];
                centers[child][0] = childNode.Center[0];
                centers[child][1] = childNode.Center[1];
                centers[child][2] = childNode.Center[2];
                radii[child] = childNode.Radius;
                GrowSphere(node.Center, node.Radius, centers[child], radii[child]);
                node.MaxParentError = std::max(node.MaxParentError, childNode.MaxParentError);
            }
            FinalizeBVHSphere(node.Center, node.Radius, centers, radii, children.size());
            node.Children = std::move(children);
            nodes.push_back(std::move(node));
            return static_cast<uint32_t>(nodes.size() - 1);
        }
    } // namespace

    bool BuildCookedMeshGroupBVH(const Container::VariableArray<CookedMeshCluster>& clusters,
                                 const Container::VariableArray<CookedMeshClusterGroup>& groups,
                                 Container::VariableArray<CookedMeshGroupBVHNode>& outNodes)
    {
        using namespace CookedMeshFormatV1;

        outNodes.clear();
        if (clusters.empty())
        {
            return false;
        }

        // 葉の単位: グループのメンバ（最大 GroupBVHMaxLeafClusters 個ずつ）と、連続した根のクラスタ。
        // 球は、メンバの球と、グループなら親の球（メンバの親の境界球）を包む
        Container::VariableArray<GroupBVHUnit> units;
        const auto addUnits = [&clusters, &units](uint32_t first, uint32_t count, bool bWithParentSphere,
                                                  float maxParentError)
        {
            for (uint32_t offset = 0; offset < count; offset += GroupBVHMaxLeafClusters)
            {
                const uint32_t chunk = std::min(count - offset, GroupBVHMaxLeafClusters);
                GroupBVHUnit unit;
                unit.First = first + offset;
                unit.Count = chunk;
                unit.MaxParentError = maxParentError;

                double centers[2 * GroupBVHMaxLeafClusters][3];
                double radii[2 * GroupBVHMaxLeafClusters];
                size_t spheres = 0;
                for (uint32_t member = unit.First; member < unit.First + chunk; ++member)
                {
                    const CookedMeshCluster& cluster = clusters[member];
                    centers[spheres][0] = cluster.BoundsCenter.X;
                    centers[spheres][1] = cluster.BoundsCenter.Y;
                    centers[spheres][2] = cluster.BoundsCenter.Z;
                    radii[spheres] = cluster.BoundsRadius;
                    ++spheres;
                    if (bWithParentSphere)
                    {
                        centers[spheres][0] = cluster.ParentBoundsCenter.X;
                        centers[spheres][1] = cluster.ParentBoundsCenter.Y;
                        centers[spheres][2] = cluster.ParentBoundsCenter.Z;
                        radii[spheres] = cluster.ParentBoundsRadius;
                        ++spheres;
                    }
                }
                unit.Center[0] = centers[0][0];
                unit.Center[1] = centers[0][1];
                unit.Center[2] = centers[0][2];
                unit.Radius = radii[0];
                for (size_t sphere = 1; sphere < spheres; ++sphere)
                {
                    GrowSphere(unit.Center, unit.Radius, centers[sphere], radii[sphere]);
                }
                FinalizeBVHSphere(unit.Center, unit.Radius, centers, radii, spheres);
                units.push_back(unit);
            }
        };

        Container::VariableArray<uint8_t> inGroup(clusters.size(), 0);
        for (const CookedMeshClusterGroup& group : groups)
        {
            if (group.ClusterCount == 0 ||
                static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount > clusters.size())
            {
                return false;
            }
            addUnits(group.ClusterOffset, group.ClusterCount, true, group.Error);
            for (uint32_t member = group.ClusterOffset; member < group.ClusterOffset + group.ClusterCount; ++member)
            {
                inGroup[member] = 1;
            }
        }
        for (uint32_t clusterIndex = 0; clusterIndex < clusters.size();)
        {
            if (inGroup[clusterIndex] != 0)
            {
                ++clusterIndex;
                continue;
            }
            uint32_t runEnd = clusterIndex;
            while (runEnd < clusters.size() && inGroup[runEnd] == 0)
            {
                ++runEnd;
            }
            addUnits(clusterIndex, runEnd - clusterIndex, false, RootParentError);
            clusterIndex = runEnd;
        }
        if (units.empty() || units.size() > std::numeric_limits<uint32_t>::max() / 4)
        {
            return false;
        }

        Container::VariableArray<uint32_t> order;
        order.reserve(units.size());
        for (uint32_t unitIndex = 0; unitIndex < units.size(); ++unitIndex)
        {
            order.push_back(unitIndex);
        }
        Container::VariableArray<GroupBVHBuildNode> buildNodes;
        buildNodes.reserve(units.size() * 2);
        const uint32_t rootBuildIndex =
            BuildGroupBVHSubtree(units, order, 0, static_cast<uint32_t>(units.size()), buildNodes);

        // 幅優先に並べ直す（子が連続し、段ごとに連続する）
        Container::VariableArray<uint32_t> bfs;
        Container::VariableArray<uint32_t> levelOf;
        bfs.reserve(buildNodes.size());
        levelOf.reserve(buildNodes.size());
        bfs.push_back(rootBuildIndex);
        levelOf.push_back(0);
        for (size_t position = 0; position < bfs.size(); ++position)
        {
            const GroupBVHBuildNode& node = buildNodes[bfs[position]];
            for (const uint32_t child : node.Children)
            {
                bfs.push_back(child);
                levelOf.push_back(levelOf[position] + 1);
            }
        }
        if (bfs.size() != buildNodes.size() || levelOf.back() + 1 > GroupBVHMaxLevels)
        {
            return false;
        }

        outNodes.reserve(bfs.size());
        uint32_t nextChild = 1;
        for (size_t position = 0; position < bfs.size(); ++position)
        {
            const GroupBVHBuildNode& node = buildNodes[bfs[position]];
            CookedMeshGroupBVHNode out;
            out.BoundsCenter = {static_cast<float>(node.Center[0]), static_cast<float>(node.Center[1]),
                                static_cast<float>(node.Center[2])};
            out.BoundsRadius = static_cast<float>(node.Radius);
            out.MaxParentError = node.MaxParentError;
            out.Count = node.Count;
            out.bLeaf = node.bLeaf;
            if (node.bLeaf)
            {
                out.First = node.First;
            }
            else
            {
                out.First = nextChild;
                nextChild += node.Count;
            }
            outNodes.push_back(out);
        }
        return true;
    }

    CookedMeshParseStatus CheckCookedMeshGroupBVH(const Container::VariableArray<CookedMeshGroupBVHNode>& nodes,
                                                  const Container::VariableArray<CookedMeshCluster>& clusters,
                                                  Container::VariableArray<uint32_t>& outLevelNodeCounts)
    {
        using namespace CookedMeshFormatV1;

        outLevelNodeCounts.clear();
        if (nodes.empty() || nodes.size() > std::numeric_limits<uint32_t>::max() / 2)
        {
            return CookedMeshParseStatus::InvalidGroupBVH;
        }

        Container::VariableArray<uint32_t> levelOf(nodes.size(), 0);
        Container::VariableArray<uint8_t> covered(clusters.size(), 0);
        uint64_t coveredTotal = 0;
        uint64_t nextChild = 1;
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            const CookedMeshGroupBVHNode& node = nodes[index];
            if (!IsFinite(node.BoundsCenter) || !std::isfinite(node.BoundsRadius) || node.BoundsRadius < 0.0f ||
                !std::isfinite(node.MaxParentError) || node.MaxParentError < 0.0f)
            {
                return CookedMeshParseStatus::InvalidFloatOrBounds;
            }

            const uint32_t level = levelOf[index];
            if (outLevelNodeCounts.size() <= level)
            {
                if (level >= GroupBVHMaxLevels)
                {
                    return CookedMeshParseStatus::InvalidGroupBVH;
                }
                outLevelNodeCounts.resize(level + 1, 0);
            }
            ++outLevelNodeCounts[level];

            if (node.bLeaf)
            {
                if (node.Count == 0 || node.Count > GroupBVHMaxLeafClusters ||
                    static_cast<uint64_t>(node.First) + node.Count > clusters.size())
                {
                    return CookedMeshParseStatus::InvalidGroupBVH;
                }
                const CookedMeshCluster& first = clusters[node.First];
                for (uint32_t member = node.First; member < node.First + node.Count; ++member)
                {
                    const CookedMeshCluster& cluster = clusters[member];
                    // 葉は、1つのグループ（または根のクラスタだけ）のメンバ。球はメンバと親の球を包み、
                    // 親の誤差の最大はメンバの親の誤差以上
                    if (covered[member] != 0 || cluster.GroupId != first.GroupId || cluster.bIsRoot != first.bIsRoot ||
                        !IsSphereContained(cluster.BoundsCenter, cluster.BoundsRadius, node.BoundsCenter,
                                           node.BoundsRadius) ||
                        node.MaxParentError < cluster.ParentError)
                    {
                        return CookedMeshParseStatus::InvalidGroupBVH;
                    }
                    if (!cluster.bIsRoot && !IsSphereContained(cluster.ParentBoundsCenter, cluster.ParentBoundsRadius,
                                                               node.BoundsCenter, node.BoundsRadius))
                    {
                        return CookedMeshParseStatus::InvalidGroupBVH;
                    }
                    covered[member] = 1;
                    ++coveredTotal;
                }
                continue;
            }

            // 内部の節の子は、幅優先の並びで次に空いている位置から連続する
            if (node.Count == 0 || node.Count > GroupBVHMaxChildren || node.First != nextChild ||
                nextChild + node.Count > nodes.size())
            {
                return CookedMeshParseStatus::InvalidGroupBVH;
            }
            for (uint32_t child = node.First; child < node.First + node.Count; ++child)
            {
                const CookedMeshGroupBVHNode& childNode = nodes[child];
                if (!IsSphereContained(childNode.BoundsCenter, childNode.BoundsRadius, node.BoundsCenter,
                                       node.BoundsRadius) ||
                    childNode.MaxParentError > node.MaxParentError)
                {
                    return CookedMeshParseStatus::InvalidGroupBVH;
                }
                levelOf[child] = level + 1;
            }
            nextChild += node.Count;
        }

        // 全ての節が根から届き、全クラスタがちょうど1つの葉に入る
        if (nextChild != nodes.size() || coveredTotal != clusters.size())
        {
            return CookedMeshParseStatus::InvalidGroupBVH;
        }
        return CookedMeshParseStatus::Success;
    }

    CookedMeshParseResult ParseCookedMesh(AssetBlob sourceBlob)
    {
        if (sourceBlob.IsValid())
        {
            const Container::Span<const uint8_t> bytes = sourceBlob.GetSpan();
            if (bytes.size() >= CookedMeshFormatV1::HeaderSize &&
                std::memcmp(bytes.data(), CookedMeshFormatV1::Magic, CookedMeshFormatV1::MagicSize) == 0)
            {
                return ParseCookedMeshV1(std::move(sourceBlob));
            }
        }

        return ParseCookedMeshV0(std::move(sourceBlob));
    }
} // namespace NorvesLib::Core::Asset
