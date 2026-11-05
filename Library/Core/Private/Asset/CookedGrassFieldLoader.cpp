#include "Asset/CookedGrassFieldFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        using namespace CookedGrassFieldFormatV0;

        uint16_t ReadLe16(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) |
                                         (static_cast<uint16_t>(data[offset + 1]) << 8));
        }

        uint32_t ReadLe32(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t ReadLe64(const uint8_t* data, size_t offset) noexcept
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        bool IsRangeWithin(uint64_t offset, uint64_t size, uint64_t total) noexcept
        {
            return offset <= total && size <= total - offset;
        }

        CookedGrassFieldParseResult Fail(CookedGrassFieldParseStatus status)
        {
            CookedGrassFieldParseResult result;
            result.Status = status;
            return result;
        }

        Container::Span<const uint8_t> GetLayer(const CookedGrassFieldData& field, uint32_t chunkIndex,
                                                bool bLayerA) noexcept
        {
            if (!field.Source.IsValid() || chunkIndex >= field.Chunks.size())
            {
                return {};
            }
            const auto bytes = field.Source.GetSpan();
            const auto& chunk = field.Chunks[chunkIndex];
            const uint64_t offset = bLayerA ? chunk.LayerAOffset : chunk.LayerBOffset;
            const uint64_t size = bLayerA ? chunk.LayerASize : chunk.LayerBSize;
            if (bytes.data() == nullptr || field.PayloadOffset > bytes.size() ||
                !IsRangeWithin(offset, size, static_cast<uint64_t>(bytes.size()) - field.PayloadOffset))
            {
                return {};
            }
            return {bytes.data() + static_cast<size_t>(field.PayloadOffset + offset), static_cast<size_t>(size)};
        }
    } // namespace

    Container::Span<const uint8_t> CookedGrassFieldData::GetLayerA(uint32_t chunkIndex) const noexcept
    {
        return GetLayer(*this, chunkIndex, true);
    }

    Container::Span<const uint8_t> CookedGrassFieldData::GetLayerB(uint32_t chunkIndex) const noexcept
    {
        return GetLayer(*this, chunkIndex, false);
    }

    CookedGrassFieldParseResult ParseCookedGrassField(AssetBlob sourceBlob)
    {
        using namespace CookedGrassFieldFormatV0;
        using Status = CookedGrassFieldParseStatus;

        if (!sourceBlob.IsValid())
        {
            return Fail(Status::InvalidBlob);
        }
        const auto bytes = sourceBlob.GetSpan();
        if (bytes.size() < HeaderSize)
        {
            return Fail(Status::TruncatedHeader);
        }
        const uint8_t* data = bytes.data();
        if (std::memcmp(data, Magic, MagicSize) != 0)
        {
            return Fail(Status::BadMagic);
        }
        if (ReadLe32(data, HeaderOffset::Version) != Version)
        {
            return Fail(Status::UnsupportedVersion);
        }
        if (ReadLe32(data, HeaderOffset::EndianMarker) != EndianMarker)
        {
            return Fail(Status::EndianMismatch);
        }
        if (ReadLe32(data, HeaderOffset::HeaderSize) != HeaderSize)
        {
            return Fail(Status::HeaderSizeMismatch);
        }

        const uint64_t fileSize = ReadLe64(data, HeaderOffset::FileSize);
        if (fileSize != bytes.size())
        {
            return Fail(Status::FileSizeMismatch);
        }
        if (ReadLe32(data, HeaderOffset::Reserved0) != 0 ||
            ReadLe32(data, HeaderOffset::Reserved1) != 0 ||
            ReadLe64(data, HeaderOffset::Reserved2) != 0 ||
            ReadLe64(data, HeaderOffset::Reserved3) != 0)
        {
            return Fail(Status::ReservedFieldNonZero);
        }

        const float chunkSizeMeters = std::bit_cast<float>(ReadLe32(data, HeaderOffset::ChunkSizeMeters));
        const uint32_t layerAResolution = ReadLe32(data, HeaderOffset::LayerAResolution);
        const uint32_t layerBResolution = ReadLe32(data, HeaderOffset::LayerBResolution);
        const uint32_t chunkCount = ReadLe32(data, HeaderOffset::ChunkCount);
        const uint32_t speciesCount = ReadLe32(data, HeaderOffset::SpeciesCount);
        if (!std::isfinite(chunkSizeMeters) || chunkSizeMeters <= 0.0f ||
            layerAResolution == 0 || layerAResolution > MaxLayerResolution ||
            layerBResolution == 0 || layerBResolution > MaxLayerResolution ||
            speciesCount == 0 || speciesCount > MaxSpeciesCount)
        {
            return Fail(Status::InvalidFieldDesc);
        }

        const uint64_t chunkTableOffset = ReadLe64(data, HeaderOffset::ChunkTableOffset);
        const uint64_t chunkTableSize = ReadLe64(data, HeaderOffset::ChunkTableSize);
        const uint64_t speciesTableOffset = ReadLe64(data, HeaderOffset::SpeciesTableOffset);
        const uint64_t speciesTableSize = ReadLe64(data, HeaderOffset::SpeciesTableSize);
        const uint64_t payloadOffset = ReadLe64(data, HeaderOffset::PayloadOffset);
        const uint64_t payloadSize = ReadLe64(data, HeaderOffset::PayloadSize);
        // 件数は uint32 と 1..64 に制限済みなので、これらの積と和は uint64 に収まる。
        const uint64_t expectedChunkTableSize = static_cast<uint64_t>(chunkCount) * ChunkRecordSize;
        const uint64_t expectedSpeciesOffset = HeaderSize + expectedChunkTableSize;
        const uint64_t expectedSpeciesSize = static_cast<uint64_t>(speciesCount) * SpeciesNameRecordSize;
        const uint64_t speciesEnd = expectedSpeciesOffset + expectedSpeciesSize;
        const uint64_t expectedPayloadOffset = (speciesEnd + PayloadAlignment - 1) & ~uint64_t(PayloadAlignment - 1);
        if (chunkTableOffset != HeaderSize || chunkTableSize != expectedChunkTableSize ||
            speciesTableOffset != expectedSpeciesOffset || speciesTableSize != expectedSpeciesSize ||
            payloadOffset != expectedPayloadOffset || speciesEnd > fileSize)
        {
            return Fail(Status::SectionOutOfRange);
        }
        // 範囲外までの隙間は読まず、欠落は次のペイロード検査で報告する。
        for (uint64_t offset = speciesEnd; offset < payloadOffset && offset < fileSize; ++offset)
        {
            if (data[static_cast<size_t>(offset)] != 0)
            {
                return Fail(Status::ReservedFieldNonZero);
            }
        }
        if (!IsRangeWithin(payloadOffset, payloadSize, fileSize))
        {
            return Fail(Status::TruncatedPayload);
        }
        if (payloadSize != fileSize - payloadOffset)
        {
            return Fail(Status::FileSizeMismatch);
        }
        const uint64_t payloadHash = ReadLe64(data, HeaderOffset::PayloadHash);
        const uint8_t* payload = data + static_cast<size_t>(payloadOffset);
        if (ComputeAssetPackagePayloadHash(payload, static_cast<size_t>(payloadSize)) != payloadHash)
        {
            return Fail(Status::PayloadHashMismatch);
        }

        // 長さと詰めをすべて検査してから、名前の重複を調べる。
        for (uint32_t i = 0; i < speciesCount; ++i)
        {
            const size_t record = static_cast<size_t>(speciesTableOffset + static_cast<uint64_t>(i) * SpeciesNameRecordSize);
            const uint8_t length = data[record];
            if (length == 0 || length > MaxSpeciesNameLength)
            {
                return Fail(Status::InvalidSpeciesName);
            }
            for (size_t byte = static_cast<size_t>(length) + 1; byte < SpeciesNameRecordSize; ++byte)
            {
                if (data[record + byte] != 0)
                {
                    return Fail(Status::InvalidSpeciesName);
                }
            }
        }
        for (uint32_t i = 0; i < speciesCount; ++i)
        {
            const size_t record = static_cast<size_t>(speciesTableOffset + static_cast<uint64_t>(i) * SpeciesNameRecordSize);
            for (uint32_t previous = 0; previous < i; ++previous)
            {
                const size_t other = static_cast<size_t>(speciesTableOffset + static_cast<uint64_t>(previous) * SpeciesNameRecordSize);
                if (data[record] == data[other] && std::memcmp(data + record + 1, data + other + 1, data[record]) == 0)
                {
                    return Fail(Status::DuplicateSpeciesName);
                }
            }
        }

        CookedGrassFieldParseResult result;
        auto& field = result.Field;
        field.ChunkSizeMeters = chunkSizeMeters;
        field.LayerAResolution = layerAResolution;
        field.LayerBResolution = layerBResolution;
        field.PayloadHash = payloadHash;
        field.PayloadOffset = payloadOffset;
        field.SpeciesNames.resize(speciesCount);
        for (uint32_t i = 0; i < speciesCount; ++i)
        {
            const size_t record = static_cast<size_t>(speciesTableOffset + static_cast<uint64_t>(i) * SpeciesNameRecordSize);
            auto& name = field.SpeciesNames[i];
            name.reserve(data[record]);
            for (size_t byte = 0; byte < data[record]; ++byte)
            {
                name.push_back(static_cast<char>(data[record + 1 + byte]));
            }
        }

        const uint64_t expectedLayerASize = static_cast<uint64_t>(layerAResolution) * layerAResolution * 4;
        const uint64_t expectedLayerBSize = static_cast<uint64_t>(layerBResolution) * layerBResolution * 4;
        field.Chunks.resize(chunkCount);
        for (uint32_t i = 0; i < chunkCount; ++i)
        {
            const size_t record = static_cast<size_t>(chunkTableOffset + static_cast<uint64_t>(i) * ChunkRecordSize);
            auto& chunk = field.Chunks[i];
            chunk.X = std::bit_cast<int32_t>(ReadLe32(data, record + ChunkRecordOffset::ChunkX));
            chunk.Z = std::bit_cast<int32_t>(ReadLe32(data, record + ChunkRecordOffset::ChunkZ));
            chunk.Flags = ReadLe32(data, record + ChunkRecordOffset::Flags);
            chunk.MaxDensity = data[record + ChunkRecordOffset::MaxDensity];
            chunk.MaxHeightFactor = data[record + ChunkRecordOffset::MaxHeightFactor];
            chunk.LayerAOffset = ReadLe64(data, record + ChunkRecordOffset::LayerAOffset);
            chunk.LayerASize = ReadLe64(data, record + ChunkRecordOffset::LayerASize);
            chunk.LayerBOffset = ReadLe64(data, record + ChunkRecordOffset::LayerBOffset);
            chunk.LayerBSize = ReadLe64(data, record + ChunkRecordOffset::LayerBSize);
            if (chunk.Flags != 0 || ReadLe16(data, record + ChunkRecordOffset::Reserved) != 0 ||
                chunk.MaxDensity == 0 || chunk.LayerASize != expectedLayerASize ||
                chunk.LayerBSize != expectedLayerBSize || chunk.LayerAOffset % 4 != 0 || chunk.LayerBOffset % 4 != 0)
            {
                return Fail(Status::InvalidChunkRecord);
            }
        }
        for (const auto& chunk : field.Chunks)
        {
            if (!IsRangeWithin(chunk.LayerAOffset, chunk.LayerASize, payloadSize) ||
                !IsRangeWithin(chunk.LayerBOffset, chunk.LayerBSize, payloadSize))
            {
                return Fail(Status::ChunkRangeOutOfRange);
            }
        }

        Container::VariableArray<const CookedGrassFieldChunk*> ordered;
        ordered.reserve(field.Chunks.size());
        for (const auto& chunk : field.Chunks)
        {
            ordered.push_back(&chunk);
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right)
        {
            return left->Z != right->Z ? left->Z < right->Z : left->X < right->X;
        });
        for (size_t i = 1; i < ordered.size(); ++i)
        {
            if (ordered[i - 1]->X == ordered[i]->X && ordered[i - 1]->Z == ordered[i]->Z)
            {
                return Fail(Status::DuplicateChunk);
            }
        }

        bool bInvalidSpeciesId = false;
        for (const auto& chunk : field.Chunks)
        {
            const uint8_t* layerA = payload + static_cast<size_t>(chunk.LayerAOffset);
            uint8_t maxDensity = 0;
            uint8_t maxHeightFactor = 0;
            for (size_t offset = 0; offset < chunk.LayerASize; offset += 4)
            {
                maxDensity = (std::max)(maxDensity, layerA[offset]);
                maxHeightFactor = (std::max)(maxHeightFactor, layerA[offset + 1]);
                bInvalidSpeciesId = bInvalidSpeciesId || layerA[offset + 3] >= speciesCount;
            }
            if (maxDensity != chunk.MaxDensity || maxHeightFactor != chunk.MaxHeightFactor)
            {
                return Fail(Status::ChunkStatsMismatch);
            }
        }
        if (bInvalidSpeciesId)
        {
            return Fail(Status::InvalidSpeciesId);
        }
        field.Source = std::move(sourceBlob);
        result.Status = Status::Success;
        return result;
    }

    const char* GetCookedGrassFieldParseStatusName(CookedGrassFieldParseStatus status) noexcept
    {
        switch (status)
        {
        case CookedGrassFieldParseStatus::Success: return "Success";
        case CookedGrassFieldParseStatus::InvalidBlob: return "InvalidBlob";
        case CookedGrassFieldParseStatus::TruncatedHeader: return "TruncatedHeader";
        case CookedGrassFieldParseStatus::BadMagic: return "BadMagic";
        case CookedGrassFieldParseStatus::UnsupportedVersion: return "UnsupportedVersion";
        case CookedGrassFieldParseStatus::EndianMismatch: return "EndianMismatch";
        case CookedGrassFieldParseStatus::HeaderSizeMismatch: return "HeaderSizeMismatch";
        case CookedGrassFieldParseStatus::FileSizeMismatch: return "FileSizeMismatch";
        case CookedGrassFieldParseStatus::ReservedFieldNonZero: return "ReservedFieldNonZero";
        case CookedGrassFieldParseStatus::InvalidFieldDesc: return "InvalidFieldDesc";
        case CookedGrassFieldParseStatus::SectionOutOfRange: return "SectionOutOfRange";
        case CookedGrassFieldParseStatus::TruncatedPayload: return "TruncatedPayload";
        case CookedGrassFieldParseStatus::PayloadHashMismatch: return "PayloadHashMismatch";
        case CookedGrassFieldParseStatus::InvalidSpeciesName: return "InvalidSpeciesName";
        case CookedGrassFieldParseStatus::DuplicateSpeciesName: return "DuplicateSpeciesName";
        case CookedGrassFieldParseStatus::InvalidChunkRecord: return "InvalidChunkRecord";
        case CookedGrassFieldParseStatus::ChunkRangeOutOfRange: return "ChunkRangeOutOfRange";
        case CookedGrassFieldParseStatus::DuplicateChunk: return "DuplicateChunk";
        case CookedGrassFieldParseStatus::ChunkStatsMismatch: return "ChunkStatsMismatch";
        case CookedGrassFieldParseStatus::InvalidSpeciesId: return "InvalidSpeciesId";
        }
        return "Unknown";
    }
} // namespace NorvesLib::Core::Asset
