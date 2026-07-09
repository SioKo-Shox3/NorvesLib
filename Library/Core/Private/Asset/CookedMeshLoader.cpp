#include "Asset/CookedMeshFormat.h"

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

    CookedMeshParseResult ParseCookedMesh(AssetBlob sourceBlob)
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
        result.Mesh.TotalBoundsCenter = totalBoundsCenter;
        result.Mesh.TotalBoundsRadius = totalBoundsRadius;
        result.Mesh.PayloadHash = payloadHash;
        result.Mesh.StringTableOffset = stringTableOffset;
        result.Mesh.StringTableSize = stringTableSize;
        result.Mesh.Vertices = std::move(vertices);
        result.Mesh.Submeshes.push_back(submesh);
        result.Mesh.Materials.push_back(material);
        result.Mesh.Clusters = std::move(clusters);
        result.Mesh.Indices = std::move(indices);
        return result;
    }
} // namespace NorvesLib::Core::Asset
