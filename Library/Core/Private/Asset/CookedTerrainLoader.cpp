#include "Asset/CookedTerrainFormat.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        using namespace CookedTerrainFormatV0;

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

        float ReadFloat(const uint8_t* data, size_t offset) noexcept
        {
            return std::bit_cast<float>(ReadLe32(data, offset));
        }

        double ReadDouble(const uint8_t* data, size_t offset) noexcept
        {
            return std::bit_cast<double>(ReadLe64(data, offset));
        }

        bool CheckedAdd(uint64_t a, uint64_t b, uint64_t& result) noexcept
        {
            if (b > std::numeric_limits<uint64_t>::max() - a)
            {
                return false;
            }
            result = a + b;
            return true;
        }

        bool CheckedMultiply(uint64_t a, uint64_t b, uint64_t& result) noexcept
        {
            if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
            {
                return false;
            }
            result = a * b;
            return true;
        }

        bool AlignPayload(uint64_t value, uint64_t& result) noexcept
        {
            uint64_t rounded = 0;
            if (!CheckedAdd(value, PayloadAlignment - 1, rounded))
            {
                return false;
            }
            result = rounded & ~uint64_t(PayloadAlignment - 1);
            return true;
        }

        bool IsRangeWithin(uint64_t offset, uint64_t size, uint64_t total) noexcept
        {
            return offset <= total && size <= total - offset;
        }

        bool IsZeroFilled(const uint8_t* data, size_t begin, size_t end) noexcept
        {
            for (size_t offset = begin; offset < end; ++offset)
            {
                if (data[offset] != 0)
                {
                    return false;
                }
            }
            return true;
        }

        CookedTerrainParseResult Fail(CookedTerrainParseStatus status)
        {
            CookedTerrainParseResult result;
            result.Status = status;
            return result;
        }
    }

    uint32_t CookedTerrainData::GetSamplesPerSide() const noexcept
    {
        return TileCells + 1;
    }

    const CookedTerrainTile* CookedTerrainData::FindTile(uint32_t tileX, uint32_t tileZ, uint32_t* outTileIndex) const noexcept
    {
        if (tileX >= TileCountX || tileZ >= TileCountZ)
        {
            return nullptr;
        }
        const uint64_t coordinate = static_cast<uint64_t>(tileZ) * TileCountX + tileX;
        if (coordinate >= TileIndexByCoord.size())
        {
            return nullptr;
        }
        const uint32_t index = TileIndexByCoord[static_cast<size_t>(coordinate)];
        if (index >= Tiles.size() || Tiles[index].TileX != tileX || Tiles[index].TileZ != tileZ)
        {
            return nullptr;
        }
        if (outTileIndex != nullptr)
        {
            *outTileIndex = index;
        }
        return &Tiles[index];
    }

    int32_t CookedTerrainData::FindLayer(AssetPackageFourCC fourCC) const noexcept
    {
        for (size_t index = 0; index < Layers.size(); ++index)
        {
            if (Layers[index].FourCC == fourCC && index <= static_cast<size_t>(std::numeric_limits<int32_t>::max()))
            {
                return static_cast<int32_t>(index);
            }
        }
        return -1;
    }

    const CookedTerrainLayerRange* CookedTerrainData::GetLayerRange(uint32_t tileIndex, uint32_t layerIndex) const noexcept
    {
        if (tileIndex >= Tiles.size() || layerIndex >= Layers.size())
        {
            return nullptr;
        }
        uint64_t index = 0;
        if (!CheckedMultiply(tileIndex, Layers.size(), index) || !CheckedAdd(index, layerIndex, index) ||
            index >= LayerRanges.size())
        {
            return nullptr;
        }
        return &LayerRanges[static_cast<size_t>(index)];
    }

    AssetBlob CookedTerrainData::GetLayerBlob(uint32_t tileIndex, uint32_t layerIndex) const
    {
        const auto* range = GetLayerRange(tileIndex, layerIndex);
        if (range == nullptr || !SourceBlob.IsValid() ||
            !IsRangeWithin(range->DataOffset, range->DataSize, SourceBlob.GetSize()))
        {
            return AssetBlob::Invalid();
        }
        return SourceBlob.TryCreateSubBlob(static_cast<size_t>(range->DataOffset), static_cast<size_t>(range->DataSize));
    }

    uint16_t CookedTerrainData::GetHeightSample(uint32_t tileIndex, uint32_t sampleX, uint32_t sampleZ) const noexcept
    {
        const uint32_t side = GetSamplesPerSide();
        if (sampleX >= side || sampleZ >= side || !SourceBlob.IsValid())
        {
            return 0;
        }
        const auto* range = GetLayerRange(tileIndex, 0);
        uint64_t sampleOffset = 0;
        uint64_t absoluteOffset = 0;
        if (range == nullptr || !IsRangeWithin(range->DataOffset, range->DataSize, SourceBlob.GetSize()) ||
            !CheckedMultiply(sampleZ, side, sampleOffset) ||
            !CheckedAdd(sampleOffset, sampleX, sampleOffset) || !CheckedMultiply(sampleOffset, 2, sampleOffset) ||
            !IsRangeWithin(sampleOffset, 2, range->DataSize) ||
            !CheckedAdd(range->DataOffset, sampleOffset, absoluteOffset) ||
            !IsRangeWithin(absoluteOffset, 2, SourceBlob.GetSize()))
        {
            return 0;
        }
        return ReadLe16(SourceBlob.GetData(), static_cast<size_t>(absoluteOffset));
    }

    float CookedTerrainData::DecodeHeightM(uint16_t value) const noexcept
    {
        const double minimum = HeightMinM;
        const double maximum = HeightMaxM;
        const double fraction = static_cast<double>(value) / 65535.0;
        return static_cast<float>(minimum * (1.0 - fraction) + maximum * fraction);
    }

    CookedTerrainParseResult ParseCookedTerrain(AssetBlob sourceBlob)
    {
        using namespace CookedTerrainFormatV0;
        if (!sourceBlob.IsValid() || sourceBlob.IsEmpty())
        {
            return Fail(CookedTerrainParseStatus::InvalidBlob);
        }
        const auto bytes = sourceBlob.GetSpan();
        if (bytes.size() < HeaderSize)
        {
            return Fail(CookedTerrainParseStatus::HeaderTooSmall);
        }
        const uint8_t* data = bytes.data();
        if (std::memcmp(data + HeaderOffset::Magic, Magic, MagicSize) != 0)
        {
            return Fail(CookedTerrainParseStatus::BadMagic);
        }
        if (ReadLe16(data, HeaderOffset::VersionMajor) != VersionMajor ||
            ReadLe16(data, HeaderOffset::VersionMinor) != VersionMinor)
        {
            return Fail(CookedTerrainParseStatus::UnsupportedVersion);
        }
        if (ReadLe32(data, HeaderOffset::EndianMarker) != EndianMarker)
        {
            return Fail(CookedTerrainParseStatus::EndianMismatch);
        }
        if (ReadLe32(data, HeaderOffset::HeaderSize) != HeaderSize)
        {
            return Fail(CookedTerrainParseStatus::HeaderSizeMismatch);
        }
        const uint64_t fileSize = ReadLe64(data, HeaderOffset::FileSize);
        if (fileSize != bytes.size())
        {
            return Fail(CookedTerrainParseStatus::FileSizeMismatch);
        }
        if (ReadLe32(data, HeaderOffset::Flags) != 0 || ReadLe32(data, HeaderOffset::Reserved0) != 0 ||
            ReadLe32(data, HeaderOffset::Reserved1) != 0 || ReadLe64(data, HeaderOffset::Reserved2) != 0)
        {
            return Fail(CookedTerrainParseStatus::ReservedFieldNonZero);
        }
        const uint64_t fileHash = ReadLe64(data, HeaderOffset::PayloadHash);
        if (ComputeCookedTerrainFileHash(bytes) != fileHash)
        {
            return Fail(CookedTerrainParseStatus::PayloadHashMismatch);
        }

        CookedTerrainParseResult result;
        auto& terrain = result.Terrain;
        terrain.CellCountX = ReadLe32(data, HeaderOffset::CellCountX);
        terrain.CellCountZ = ReadLe32(data, HeaderOffset::CellCountZ);
        terrain.TileCells = ReadLe32(data, HeaderOffset::TileCells);
        terrain.TileCountX = ReadLe32(data, HeaderOffset::TileCountX);
        terrain.TileCountZ = ReadLe32(data, HeaderOffset::TileCountZ);
        terrain.LodCount = ReadLe32(data, HeaderOffset::LodCount);
        terrain.CellSizeM = ReadFloat(data, HeaderOffset::CellSizeM);
        terrain.OriginX = ReadDouble(data, HeaderOffset::OriginX);
        terrain.OriginY = ReadDouble(data, HeaderOffset::OriginY);
        terrain.OriginZ = ReadDouble(data, HeaderOffset::OriginZ);
        terrain.HeightMinM = ReadFloat(data, HeaderOffset::HeightMinM);
        terrain.HeightMaxM = ReadFloat(data, HeaderOffset::HeightMaxM);
        terrain.PayloadHash = fileHash;
        if (terrain.CellCountX == 0 || terrain.CellCountZ == 0 || terrain.TileCells == 0 ||
            terrain.CellCountX % terrain.TileCells != 0 || terrain.CellCountZ % terrain.TileCells != 0 ||
            terrain.TileCountX != terrain.CellCountX / terrain.TileCells ||
            terrain.TileCountZ != terrain.CellCountZ / terrain.TileCells ||
            !std::isfinite(terrain.CellSizeM) || terrain.CellSizeM <= 0.0f ||
            !std::isfinite(terrain.OriginX) || !std::isfinite(terrain.OriginY) || !std::isfinite(terrain.OriginZ) ||
            !std::isfinite(terrain.HeightMinM) || !std::isfinite(terrain.HeightMaxM) || terrain.HeightMinM > terrain.HeightMaxM)
        {
            return Fail(CookedTerrainParseStatus::InvalidDimensions);
        }
        if (terrain.LodCount == 0 || terrain.LodCount > MaxLodCount)
        {
            return Fail(CookedTerrainParseStatus::InvalidLodCount);
        }
        const uint32_t layerCount = ReadLe32(data, HeaderOffset::LayerCount);
        if (layerCount == 0 || layerCount > MaxLayerCount)
        {
            return Fail(CookedTerrainParseStatus::InvalidLayerCount);
        }
        const uint32_t tileRecordSize = ReadLe32(data, HeaderOffset::TileRecordSize);
        if (ReadLe32(data, HeaderOffset::LayerRecordSize) != LayerRecordSize ||
            tileRecordSize != TileRecordHeadSize + layerCount * TileLayerRangeSize)
        {
            return Fail(CookedTerrainParseStatus::RecordSizeMismatch);
        }

        const uint64_t layerTableOffset = ReadLe64(data, HeaderOffset::LayerTableOffset);
        const uint64_t layerTableSize = ReadLe64(data, HeaderOffset::LayerTableSize);
        const uint64_t tileTableOffset = ReadLe64(data, HeaderOffset::TileTableOffset);
        const uint64_t tileTableSize = ReadLe64(data, HeaderOffset::TileTableSize);
        const uint64_t stringTableOffset = ReadLe64(data, HeaderOffset::StringTableOffset);
        const uint64_t stringTableSize = ReadLe64(data, HeaderOffset::StringTableSize);
        const uint64_t payloadOffset = ReadLe64(data, HeaderOffset::PayloadOffset);
        const uint64_t payloadSize = ReadLe64(data, HeaderOffset::PayloadSize);
        uint64_t tileCount = 0;
        uint64_t rangeCount = 0;
        uint64_t expectedLayerSize = 0;
        uint64_t expectedTileSize = 0;
        uint64_t heightSize = 0;
        uint64_t layerEnd = 0;
        uint64_t tileEnd = 0;
        uint64_t stringEnd = 0;
        uint64_t expectedPayloadOffset = 0;
        uint64_t payloadEnd = 0;
        const uint64_t samplesPerSide = static_cast<uint64_t>(terrain.TileCells) + 1;
        if (!CheckedMultiply(terrain.TileCountX, terrain.TileCountZ, tileCount) ||
            !CheckedMultiply(tileCount, layerCount, rangeCount) ||
            !CheckedMultiply(layerCount, LayerRecordSize, expectedLayerSize) ||
            !CheckedMultiply(tileCount, tileRecordSize, expectedTileSize) ||
            !CheckedMultiply(samplesPerSide, samplesPerSide, heightSize) ||
            !CheckedMultiply(heightSize, 2, heightSize) ||
            !CheckedAdd(layerTableOffset, layerTableSize, layerEnd) ||
            !CheckedAdd(tileTableOffset, tileTableSize, tileEnd) ||
            !CheckedAdd(stringTableOffset, stringTableSize, stringEnd) ||
            !AlignPayload(stringEnd, expectedPayloadOffset) ||
            !CheckedAdd(payloadOffset, payloadSize, payloadEnd))
        {
            return Fail(CookedTerrainParseStatus::IntegerOverflow);
        }
        if (layerTableOffset != HeaderSize || layerTableSize != expectedLayerSize ||
            tileTableOffset != layerEnd || tileTableSize != expectedTileSize || stringTableOffset != tileEnd ||
            payloadOffset != expectedPayloadOffset || payloadEnd != fileSize ||
            !IsRangeWithin(layerTableOffset, layerTableSize, fileSize) ||
            !IsRangeWithin(tileTableOffset, tileTableSize, fileSize) ||
            !IsRangeWithin(stringTableOffset, stringTableSize, fileSize) ||
            !IsRangeWithin(payloadOffset, payloadSize, fileSize) ||
            !IsZeroFilled(data, static_cast<size_t>(stringEnd), static_cast<size_t>(payloadOffset)))
        {
            return Fail(CookedTerrainParseStatus::TableLayoutMismatch);
        }
        if (tileCount > std::numeric_limits<uint32_t>::max() || tileCount > terrain.Tiles.max_size() ||
            tileCount > terrain.TileIndexByCoord.max_size() || rangeCount > terrain.LayerRanges.max_size() ||
            layerCount > terrain.Layers.max_size())
        {
            return Fail(CookedTerrainParseStatus::IntegerOverflow);
        }

        // 表全体の範囲を確認済みのため、各レコードの固定欄を安全に読める。
        for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            const size_t record = static_cast<size_t>(layerTableOffset + layerIndex * LayerRecordSize);
            if (ReadLe64(data, record + LayerRecordOffset::Reserved) != 0)
            {
                return Fail(CookedTerrainParseStatus::ReservedFieldNonZero);
            }
        }
        terrain.Layers.resize(layerCount);
        uint64_t nextSemanticOffset = 0;
        for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            const size_t record = static_cast<size_t>(layerTableOffset + layerIndex * LayerRecordSize);
            auto& layer = terrain.Layers[layerIndex];
            layer.FourCC = ReadLe32(data, record + LayerRecordOffset::FourCC);
            layer.Encoding = ReadLe32(data, record + LayerRecordOffset::Encoding);
            layer.PixelFormat = static_cast<CookedTexturePixelFormat>(ReadLe32(data, record + LayerRecordOffset::PixelFormat));
            layer.ColorSpace = static_cast<CookedTextureColorSpace>(ReadLe32(data, record + LayerRecordOffset::ColorSpace));
            const uint32_t semanticOffset = ReadLe32(data, record + LayerRecordOffset::SemanticOffset);
            const uint32_t semanticSize = ReadLe32(data, record + LayerRecordOffset::SemanticSize);
            if (GetCookedTextureBlockInfo(layer.PixelFormat).BlockBytes == 0 ||
                (layer.ColorSpace != CookedTextureColorSpace::Linear && layer.ColorSpace != CookedTextureColorSpace::SRGB) ||
                semanticSize == 0 || semanticSize > MaxSemanticBytes || semanticOffset != nextSemanticOffset ||
                !IsRangeWithin(semanticOffset, semanticSize, stringTableSize) ||
                (layerIndex == 0 && (layer.FourCC != HeightLayerFourCC || layer.Encoding != LayerEncodingRawHeight16 ||
                                     layer.PixelFormat != CookedTexturePixelFormat::R16UNorm ||
                                     layer.ColorSpace != CookedTextureColorSpace::Linear)) ||
                (layerIndex != 0 && (layer.FourCC == HeightLayerFourCC || layer.Encoding != LayerEncodingEmbeddedNvtex)))
            {
                return Fail(CookedTerrainParseStatus::InvalidLayerRecord);
            }
            const size_t semanticStart = static_cast<size_t>(stringTableOffset + semanticOffset);
            for (size_t i = 0; i < semanticSize; ++i)
            {
                if (data[semanticStart + i] < 0x20 || data[semanticStart + i] > 0x7e)
                {
                    return Fail(CookedTerrainParseStatus::InvalidLayerRecord);
                }
            }
            layer.Semantic.append(reinterpret_cast<const char*>(data + semanticStart), semanticSize);
            nextSemanticOffset += semanticSize;
        }
        if (nextSemanticOffset != stringTableSize)
        {
            return Fail(CookedTerrainParseStatus::InvalidLayerRecord);
        }
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            for (uint32_t previous = 0; previous < i; ++previous)
            {
                if (terrain.Layers[previous].FourCC == terrain.Layers[i].FourCC)
                {
                    return Fail(CookedTerrainParseStatus::DuplicateLayerFourCC);
                }
            }
        }

        for (uint64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            const size_t record = static_cast<size_t>(tileTableOffset + tileIndex * tileRecordSize);
            if (ReadLe32(data, record + TileRecordOffset::Reserved0) != 0 ||
                ReadLe32(data, record + TileRecordOffset::Reserved1) != 0)
            {
                return Fail(CookedTerrainParseStatus::ReservedFieldNonZero);
            }
            for (uint32_t lod = terrain.LodCount; lod < MaxLodCount; ++lod)
            {
                if (ReadFloat(data, record + TileRecordOffset::LodMaxErrorM + lod * 4) != 0.0f)
                {
                    return Fail(CookedTerrainParseStatus::ReservedFieldNonZero);
                }
            }
        }
        terrain.Tiles.resize(static_cast<size_t>(tileCount));
        terrain.LayerRanges.resize(static_cast<size_t>(rangeCount));
        terrain.TileIndexByCoord.resize(static_cast<size_t>(tileCount), std::numeric_limits<uint32_t>::max());
        for (uint64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            const size_t record = static_cast<size_t>(tileTableOffset + tileIndex * tileRecordSize);
            auto& tile = terrain.Tiles[static_cast<size_t>(tileIndex)];
            tile.TileX = ReadLe32(data, record + TileRecordOffset::TileX);
            tile.TileZ = ReadLe32(data, record + TileRecordOffset::TileZ);
            if (tile.TileX >= terrain.TileCountX || tile.TileZ >= terrain.TileCountZ)
            {
                return Fail(CookedTerrainParseStatus::TileIndexOutOfRange);
            }
            tile.HeightMinM = ReadFloat(data, record + TileRecordOffset::HeightMinM);
            tile.HeightMaxM = ReadFloat(data, record + TileRecordOffset::HeightMaxM);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                tile.AabbMin[axis] = ReadFloat(data, record + TileRecordOffset::AabbMin + axis * 4);
                tile.AabbMax[axis] = ReadFloat(data, record + TileRecordOffset::AabbMax + axis * 4);
            }
            for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
            {
                tile.LodMaxErrorM[lod] = ReadFloat(data, record + TileRecordOffset::LodMaxErrorM + lod * 4);
            }
            for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
            {
                const size_t rangeOffset = record + TileRecordHeadSize + layerIndex * TileLayerRangeSize;
                auto& range = terrain.LayerRanges[static_cast<size_t>(tileIndex * layerCount + layerIndex)];
                range.DataOffset = ReadLe64(data, rangeOffset + TileLayerRangeOffset::DataOffset);
                range.DataSize = ReadLe64(data, rangeOffset + TileLayerRangeOffset::DataSize);
                range.DataHash = ReadLe64(data, rangeOffset + TileLayerRangeOffset::DataHash);
            }
        }
        for (uint64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            const auto& tile = terrain.Tiles[static_cast<size_t>(tileIndex)];
            const size_t coordinate = static_cast<size_t>(static_cast<uint64_t>(tile.TileZ) * terrain.TileCountX + tile.TileX);
            if (terrain.TileIndexByCoord[coordinate] != std::numeric_limits<uint32_t>::max())
            {
                return Fail(CookedTerrainParseStatus::DuplicateTile);
            }
            terrain.TileIndexByCoord[coordinate] = static_cast<uint32_t>(tileIndex);
        }
        for (const auto& tile : terrain.Tiles)
        {
            if (!std::isfinite(tile.HeightMinM) || !std::isfinite(tile.HeightMaxM) || tile.HeightMinM > tile.HeightMaxM)
            {
                return Fail(CookedTerrainParseStatus::InvalidTileBounds);
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(tile.AabbMin[axis]) || !std::isfinite(tile.AabbMax[axis]) || tile.AabbMin[axis] > tile.AabbMax[axis])
                {
                    return Fail(CookedTerrainParseStatus::InvalidTileBounds);
                }
            }
            for (uint32_t lod = 0; lod < terrain.LodCount; ++lod)
            {
                if (!std::isfinite(tile.LodMaxErrorM[lod]) || tile.LodMaxErrorM[lod] < 0.0f)
                {
                    return Fail(CookedTerrainParseStatus::InvalidTileBounds);
                }
            }
        }
        for (const auto& range : terrain.LayerRanges)
        {
            if (range.DataSize == 0 || range.DataOffset < payloadOffset || range.DataOffset % PayloadAlignment != 0 ||
                !IsRangeWithin(range.DataOffset, range.DataSize, fileSize))
            {
                return Fail(CookedTerrainParseStatus::LayerRangeOutOfRange);
            }
        }
        for (uint64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            if (terrain.LayerRanges[static_cast<size_t>(tileIndex * layerCount)].DataSize != heightSize)
            {
                return Fail(CookedTerrainParseStatus::HeightSizeMismatch);
            }
        }
        for (const auto& range : terrain.LayerRanges)
        {
            if (ComputeCookedTexturePayloadHash(data + static_cast<size_t>(range.DataOffset), static_cast<size_t>(range.DataSize)) != range.DataHash)
            {
                return Fail(CookedTerrainParseStatus::LayerHashMismatch);
            }
        }
        for (uint64_t tileIndex = 0; tileIndex < tileCount; ++tileIndex)
        {
            for (uint32_t layerIndex = 1; layerIndex < layerCount; ++layerIndex)
            {
                const auto& range = terrain.LayerRanges[static_cast<size_t>(tileIndex * layerCount + layerIndex)];
                const AssetBlob sub = sourceBlob.TryCreateSubBlob(static_cast<size_t>(range.DataOffset), static_cast<size_t>(range.DataSize));
                const auto parsed = ParseCookedTexture(sub, false);
                const auto& layer = terrain.Layers[layerIndex];
                if (!parsed.Succeeded() || parsed.Texture.PixelFormat != layer.PixelFormat || parsed.Texture.ColorSpace != layer.ColorSpace)
                {
                    return Fail(CookedTerrainParseStatus::EmbeddedTextureInvalid);
                }
            }
        }

        // 大きさやハッシュの不正を先に分類し、正常な区間同士の詰め方とゼロ埋めを最後に確認する。
        uint64_t previousEnd = payloadOffset;
        for (const auto& range : terrain.LayerRanges)
        {
            uint64_t expectedOffset = 0;
            if (!AlignPayload(previousEnd, expectedOffset))
            {
                return Fail(CookedTerrainParseStatus::IntegerOverflow);
            }
            if (range.DataOffset != expectedOffset ||
                !IsZeroFilled(data, static_cast<size_t>(previousEnd), static_cast<size_t>(range.DataOffset)))
            {
                return Fail(CookedTerrainParseStatus::TableLayoutMismatch);
            }
            previousEnd = range.DataOffset + range.DataSize;
        }
        if (previousEnd != fileSize)
        {
            return Fail(CookedTerrainParseStatus::TableLayoutMismatch);
        }
        terrain.SourceBlob = std::move(sourceBlob);
        result.Status = CookedTerrainParseStatus::Success;
        return result;
    }
} // namespace NorvesLib::Core::Asset
