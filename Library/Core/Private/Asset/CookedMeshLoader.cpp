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

    // v1 の読み込み。ParseCookedMesh が、先頭の magic と最小のヘッダ長を確かめたうえで呼ぶ。
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
            stringRefRecordSize != StringRefRecordSize || groupRecordSize != GroupRecordSize)
        {
            return Fail(CookedMeshParseStatus::RecordSizeMismatch);
        }

        if (declaredFileSize != static_cast<uint64_t>(bytes.size()))
        {
            return Fail(CookedMeshParseStatus::FileSizeMismatch);
        }

        if (ReadLe32(data, HeaderOffset::Flags) != 0)
        {
            return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
        }

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

        if (sections[0].Size != expectedSubmeshSize || sections[1].Size != expectedMaterialSize ||
            sections[2].Size != expectedClusterSize || sections[3].Size != expectedGroupSize ||
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
        clusters.reserve(clusterCount);
        const size_t clusterTableOffset = static_cast<size_t>(sections[2].Offset);
        uint32_t rootCount = 0;
        for (uint32_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            const size_t recordOffset = clusterTableOffset + static_cast<size_t>(clusterIndex) * ClusterRecordSize;
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

            if ((flags & ~ClusterFlagRoot) != 0 || ReadLe32(data, recordOffset + ClusterRecordOffset::Reserved0) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved1) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved2) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved3) != 0 ||
                ReadLe64(data, recordOffset + ClusterRecordOffset::Reserved4) != 0)
            {
                return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
            }

            if (!IsFinite(cluster.BoundsCenter) || !std::isfinite(cluster.BoundsRadius) ||
                cluster.BoundsRadius < 0.0f || !std::isfinite(cluster.LODError) || cluster.LODError < 0.0f ||
                !IsFinite(cluster.ParentBoundsCenter) || !std::isfinite(cluster.ParentBoundsRadius) ||
                cluster.ParentBoundsRadius < 0.0f || !std::isfinite(cluster.ParentError) ||
                cluster.ParentError < 0.0f || !IsFinite(cluster.ConeAxis) || !std::isfinite(cluster.ConeCutoff))
            {
                return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
            }

            // ページの番号は段5（ページのストリーミング）まで 0
            if (cluster.MaterialIndex != 0 || cluster.PageId != 0)
            {
                return Fail(CookedMeshParseStatus::UnsupportedV1Feature);
            }

            if (cluster.LODLevel >= lodLevelCount)
            {
                return Fail(CookedMeshParseStatus::InvalidLODGraph);
            }

            if (cluster.IndexCount == 0 || cluster.IndexCount % 3 != 0 ||
                cluster.IndexCount / 3 > ClusterMaxTriangles || cluster.VertexCount == 0 ||
                cluster.VertexCount > ClusterMaxVertices ||
                static_cast<uint64_t>(cluster.VertexOffset) + cluster.VertexCount > vertexCount)
            {
                return Fail(CookedMeshParseStatus::InvalidClusterRange);
            }

            // 根 ⇔ グループが無い ⇔ 親の誤差が最大値。この3つが食い違う記録は壊れている
            const bool hasNoGroup = cluster.GroupId == InvalidGroupId;
            const bool hasRootError = cluster.ParentError == RootParentError;
            if (cluster.bIsRoot != hasNoGroup || cluster.bIsRoot != hasRootError)
            {
                return Fail(CookedMeshParseStatus::InvalidLODGraph);
            }

            if (cluster.bIsRoot)
            {
                if (!IsZero(cluster.ParentBoundsCenter) || cluster.ParentBoundsRadius != 0.0f)
                {
                    return Fail(CookedMeshParseStatus::InvalidLODGraph);
                }
                ++rootCount;
            }
            else if (cluster.GroupId >= groupCount || cluster.ParentError < cluster.LODError)
            {
                // 親の誤差は自分の誤差以上（階層をたどると誤差が単調に増える）
                return Fail(CookedMeshParseStatus::InvalidLODGraph);
            }

            // 最も細かい段（元の形）の誤差は 0
            if (cluster.LODLevel == 0 && cluster.LODError != 0.0f)
            {
                return Fail(CookedMeshParseStatus::InvalidLODGraph);
            }

            clusters.push_back(cluster);
        }

        if (rootCount == 0)
        {
            return Fail(CookedMeshParseStatus::InvalidLODGraph);
        }

        Container::VariableArray<CookedMeshClusterGroup> groups;
        groups.reserve(groupCount);
        const size_t groupTableOffset = static_cast<size_t>(sections[3].Offset);
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
                return Fail(CookedMeshParseStatus::ReservedFieldNonZero);
            }

            if (!IsFinite(group.BoundsCenter) || !std::isfinite(group.BoundsRadius) || group.BoundsRadius < 0.0f ||
                !std::isfinite(group.Error) || group.Error < 0.0f || group.Error >= RootParentError)
            {
                return Fail(CookedMeshParseStatus::InvalidFloatOrBounds);
            }

            if (group.ClusterCount == 0 ||
                static_cast<uint64_t>(group.ClusterOffset) + group.ClusterCount > clusterCount ||
                group.LODLevel >= lodLevelCount)
            {
                return Fail(CookedMeshParseStatus::InvalidGroupTable);
            }

            for (uint32_t memberIndex = group.ClusterOffset; memberIndex < group.ClusterOffset + group.ClusterCount;
                 ++memberIndex)
            {
                const CookedMeshCluster& member = clusters[memberIndex];
                if (member.bIsRoot || member.GroupId != groupIndex || member.LODLevel != group.LODLevel ||
                    !IsSameFloat3(member.ParentBoundsCenter, group.BoundsCenter) ||
                    member.ParentBoundsRadius != group.BoundsRadius || member.ParentError != group.Error ||
                    !IsSphereContained(member.BoundsCenter, member.BoundsRadius, group.BoundsCenter,
                                       group.BoundsRadius))
                {
                    return Fail(CookedMeshParseStatus::InvalidLODGraph);
                }
            }

            memberTotal += group.ClusterCount;
            groups.push_back(group);
        }

        // 根でないクラスタは、ちょうど1つのグループのメンバ（メンバは自分の GroupId のグループにだけ並ぶので、
        // メンバの総数が根以外の数に一致すれば、漏れも重複も無い）
        if (memberTotal != static_cast<uint64_t>(clusterCount) - rootCount)
        {
            return Fail(CookedMeshParseStatus::InvalidGroupTable);
        }

        Container::VariableArray<CookedMeshVertex> vertices;
        vertices.reserve(vertexCount);
        const size_t vertexPayloadOffset = static_cast<size_t>(sections[5].Offset);
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
        const size_t indexPayloadOffset = static_cast<size_t>(sections[6].Offset);
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

        // クラスタのインデックスは Clusters の並びで 0 から隙間なく並び、フォールバックの範囲の手前で終わる。
        // クラスタのインデックスは、そのクラスタの頂点の範囲の先頭からの相対の位置
        uint64_t expectedClusterIndexOffset = 0;
        for (const CookedMeshCluster& cluster : clusters)
        {
            if (cluster.IndexOffset != expectedClusterIndexOffset ||
                expectedClusterIndexOffset + cluster.IndexCount > fallbackIndexOffset)
            {
                return Fail(CookedMeshParseStatus::InvalidClusterRange);
            }

            const uint32_t clusterIndexEnd = cluster.IndexOffset + cluster.IndexCount;
            for (uint32_t index = cluster.IndexOffset; index < clusterIndexEnd; ++index)
            {
                if (indices[index] >= cluster.VertexCount)
                {
                    return Fail(CookedMeshParseStatus::InvalidClusterRange);
                }
            }
            expectedClusterIndexOffset += cluster.IndexCount;
        }

        if (expectedClusterIndexOffset != fallbackIndexOffset)
        {
            return Fail(CookedMeshParseStatus::InvalidClusterRange);
        }

        CookedMeshParseResult result;
        result.Status = CookedMeshParseStatus::Success;
        result.Mesh.SourceBlob = std::move(sourceBlob);
        result.Mesh.FormatMajor = VersionMajor;
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

        // 文字列は albedo・normal・arm の順に詰める。空の参照は (0, 0)
        size_t stringCursor = 0;
        const Container::AnsiStringView textures[] = {input.AlbedoTexture, input.NormalTexture, input.ArmTexture};
        const size_t textureRecordOffsets[] = {MaterialRecordOffset::AlbedoTexture, MaterialRecordOffset::NormalTexture,
                                               MaterialRecordOffset::ArmTexture};
        for (size_t textureIndex = 0; textureIndex < 3; ++textureIndex)
        {
            const Container::AnsiStringView texture = textures[textureIndex];
            if (texture.empty())
            {
                continue;
            }

            const size_t recordOffset = materialTableOffset + textureRecordOffsets[textureIndex];
            WriteLe64(bytes, recordOffset + StringRefRecordOffset::StringOffset, static_cast<uint64_t>(stringCursor));
            WriteLe32(bytes, recordOffset + StringRefRecordOffset::StringLength, static_cast<uint32_t>(texture.size()));
            std::memcpy(bytes.data() + stringTableOffset + stringCursor, texture.data(), texture.size());
            stringCursor += texture.size();
        }

        for (size_t clusterIndex = 0; clusterIndex < clusterCount; ++clusterIndex)
        {
            const CookedMeshCluster& cluster = input.Clusters[clusterIndex];
            const size_t recordOffset = clusterTableOffset + clusterIndex * ClusterRecordSize;
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
        }

        for (size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex)
        {
            const CookedMeshClusterGroup& group = input.Groups[groupIndex];
            const size_t recordOffset = groupTableOffset + groupIndex * GroupRecordSize;
            WriteFloat3(bytes, recordOffset + GroupRecordOffset::BoundsCenterX, group.BoundsCenter);
            WriteLeFloat(bytes, recordOffset + GroupRecordOffset::BoundsRadius, group.BoundsRadius);
            WriteLeFloat(bytes, recordOffset + GroupRecordOffset::Error, group.Error);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::ClusterOffset, group.ClusterOffset);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::ClusterCount, group.ClusterCount);
            WriteLe32(bytes, recordOffset + GroupRecordOffset::LODLevel, group.LODLevel);
        }

        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            const CookedMeshVertex& vertex = input.Vertices[vertexIndex];
            const size_t recordOffset = vertexPayloadOffset + vertexIndex * VertexRecordSize;
            WriteFloat3(bytes, recordOffset + VertexRecordOffset::PositionX, vertex.Position);
            WriteFloat3(bytes, recordOffset + VertexRecordOffset::NormalX, vertex.Normal);
            WriteLeFloat(bytes, recordOffset + VertexRecordOffset::TexCoordU, vertex.TexCoord.U);
            WriteLeFloat(bytes, recordOffset + VertexRecordOffset::TexCoordV, vertex.TexCoord.V);
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
