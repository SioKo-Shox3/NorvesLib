#include "Asset/CookedTerrainFormat.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Asset
{
    namespace TerrainOuterRingDetail
    {
        bool IsHole(const CookedTerrainOuterRing& ring, uint32_t x, uint32_t z) noexcept;
        uint32_t TileFlags(const CookedTerrainOuterRing& ring, uint32_t x, uint32_t z) noexcept;
        bool IsValid(const CookedTerrainOuterRing& ring, const CookedTerrainData& nearTerrain) noexcept;
    }

    namespace
    {
        using namespace CookedTerrainFormatV0;

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

        void WriteLe(uint8_t* bytes, size_t offset, uint64_t value, size_t width) noexcept
        {
            for (size_t i = 0; i < width; ++i)
            {
                bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
            }
        }

        void WriteLe16(uint8_t* bytes, size_t offset, uint16_t value) noexcept
        {
            WriteLe(bytes, offset, value, 2);
        }

        void WriteLe32(uint8_t* bytes, size_t offset, uint32_t value) noexcept
        {
            WriteLe(bytes, offset, value, 4);
        }

        void WriteLe64(uint8_t* bytes, size_t offset, uint64_t value) noexcept
        {
            WriteLe(bytes, offset, value, 8);
        }

        void WriteFloat(uint8_t* bytes, size_t offset, float value) noexcept
        {
            WriteLe32(bytes, offset, std::bit_cast<uint32_t>(value));
        }

        void WriteDouble(uint8_t* bytes, size_t offset, double value) noexcept
        {
            WriteLe64(bytes, offset, std::bit_cast<uint64_t>(value));
        }

        bool IsValidLayer(const CookedTerrainLayerDesc& layer, size_t index) noexcept
        {
            if (GetCookedTextureBlockInfo(layer.PixelFormat).BlockBytes == 0 ||
                (layer.ColorSpace != CookedTextureColorSpace::Linear && layer.ColorSpace != CookedTextureColorSpace::SRGB) ||
                layer.Semantic.size() == 0 || layer.Semantic.size() > MaxSemanticBytes)
            {
                return false;
            }
            for (size_t i = 0; i < layer.Semantic.size(); ++i)
            {
                const auto ch = static_cast<uint8_t>(layer.Semantic[i]);
                if (ch < 0x20 || ch > 0x7e)
                {
                    return false;
                }
            }
            if (index == 0)
            {
                return layer.FourCC == HeightLayerFourCC && layer.Encoding == LayerEncodingRawHeight16 &&
                       layer.PixelFormat == CookedTexturePixelFormat::R16UNorm && layer.ColorSpace == CookedTextureColorSpace::Linear;
            }
            return layer.FourCC != HeightLayerFourCC && layer.Encoding == LayerEncodingEmbeddedNvtex;
        }

        template <typename Tile>
        bool IsValidTileBounds(const Tile& tile, uint32_t lodCount) noexcept
        {
            if (!std::isfinite(tile.HeightMinM) || !std::isfinite(tile.HeightMaxM) || tile.HeightMinM > tile.HeightMaxM)
            {
                return false;
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(tile.AabbMin[axis]) || !std::isfinite(tile.AabbMax[axis]) || tile.AabbMin[axis] > tile.AabbMax[axis])
                {
                    return false;
                }
            }
            for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
            {
                if (lod < lodCount)
                {
                    if (!std::isfinite(tile.LodMaxErrorM[lod]) || tile.LodMaxErrorM[lod] < 0.0f)
                    {
                        return false;
                    }
                }
                else if (tile.LodMaxErrorM[lod] != 0.0f)
                {
                    return false;
                }
            }
            return true;
        }
    }

    CookedTerrainWriteStatus WriteCookedTerrain(const CookedTerrainWriteDesc& desc,
                                               Container::VariableArray<uint8_t>& outBytes)
    {
        using namespace CookedTerrainFormatV0;
        const auto fail = [&outBytes](CookedTerrainWriteStatus status)
        {
            outBytes.clear();
            return status;
        };

        if (desc.CellCountX == 0 || desc.CellCountZ == 0 || desc.TileCells == 0 ||
            desc.CellCountX % desc.TileCells != 0 || desc.CellCountZ % desc.TileCells != 0 ||
            !std::isfinite(desc.CellSizeM) || desc.CellSizeM <= 0.0f ||
            !std::isfinite(desc.OriginX) || !std::isfinite(desc.OriginY) || !std::isfinite(desc.OriginZ) ||
            !std::isfinite(desc.HeightMinM) || !std::isfinite(desc.HeightMaxM) || desc.HeightMinM > desc.HeightMaxM)
        {
            return fail(CookedTerrainWriteStatus::InvalidDimensions);
        }
        if (desc.LodCount == 0 || desc.LodCount > MaxLodCount)
        {
            return fail(CookedTerrainWriteStatus::InvalidLodCount);
        }
        if (desc.Layers.empty() || desc.Layers.size() > MaxLayerCount)
        {
            return fail(CookedTerrainWriteStatus::InvalidLayerCount);
        }

        const uint32_t tileCountX = desc.CellCountX / desc.TileCells;
        const uint32_t tileCountZ = desc.CellCountZ / desc.TileCells;
        const uint32_t layerCount = static_cast<uint32_t>(desc.Layers.size());
        uint64_t tileCount = 0;
        uint64_t samplesPerSide = 0;
        uint64_t heightSize = 0;
        uint64_t tileRecordSize = 0;
        uint64_t layerTableSize = 0;
        const uint64_t layerTableOffset = HeaderSize + (desc.bHasOuterRing ? OuterRingBlockSize : 0);
        uint64_t tileTableOffset = 0;
        uint64_t tileTableSize = 0;
        uint64_t stringTableOffset = 0;
        uint64_t rangeCount = 0;
        if (!CheckedMultiply(tileCountX, tileCountZ, tileCount) ||
            !CheckedAdd(desc.TileCells, 1, samplesPerSide) ||
            !CheckedMultiply(samplesPerSide, samplesPerSide, heightSize) ||
            !CheckedMultiply(heightSize, 2, heightSize) ||
            !CheckedMultiply(layerCount, TileLayerRangeSize, tileRecordSize) ||
            !CheckedAdd(TileRecordHeadSize, tileRecordSize, tileRecordSize) ||
            tileRecordSize > std::numeric_limits<uint32_t>::max() ||
            !CheckedMultiply(layerCount, LayerRecordSize, layerTableSize) ||
            !CheckedAdd(layerTableOffset, layerTableSize, tileTableOffset) ||
            !CheckedMultiply(tileCount, tileRecordSize, tileTableSize) ||
            !CheckedAdd(tileTableOffset, tileTableSize, stringTableOffset) ||
            !CheckedMultiply(tileCount, layerCount, rangeCount))
        {
            return fail(CookedTerrainWriteStatus::IntegerOverflow);
        }

        uint64_t stringTableSize = 0;
        for (size_t layerIndex = 0; layerIndex < desc.Layers.size(); ++layerIndex)
        {
            const auto& layer = desc.Layers[layerIndex];
            if (!IsValidLayer(layer, layerIndex))
            {
                return fail(CookedTerrainWriteStatus::InvalidLayer);
            }
            for (size_t previous = 0; previous < layerIndex; ++previous)
            {
                if (desc.Layers[previous].FourCC == layer.FourCC)
                {
                    return fail(CookedTerrainWriteStatus::DuplicateLayerFourCC);
                }
            }
            if (!CheckedAdd(stringTableSize, layer.Semantic.size(), stringTableSize) ||
                stringTableSize > std::numeric_limits<uint32_t>::max())
            {
                return fail(CookedTerrainWriteStatus::IntegerOverflow);
            }
        }

        uint64_t outerGridCount = 0;
        uint64_t outerTileCount = 0;
        uint64_t outerTableOffset = 0;
        uint64_t outerTableSize = 0;
        uint64_t outerHeightSize = 0;
        if (desc.bHasOuterRing)
        {
            CookedTerrainData nearTerrain;
            nearTerrain.CellCountX = desc.CellCountX;
            nearTerrain.CellCountZ = desc.CellCountZ;
            nearTerrain.CellSizeM = desc.CellSizeM;
            nearTerrain.OriginX = desc.OriginX;
            nearTerrain.OriginZ = desc.OriginZ;
            const auto& ring = desc.OuterRing;
            if (!TerrainOuterRingDetail::IsValid(ring, nearTerrain))
            {
                return fail(CookedTerrainWriteStatus::InvalidOuterRing);
            }
            outerGridCount = static_cast<uint64_t>(ring.TileCountX) * ring.TileCountZ;
            outerTileCount = outerGridCount - static_cast<uint64_t>(ring.HoleTileCountX) * ring.HoleTileCountZ;
            outerHeightSize = static_cast<uint64_t>(ring.TileCells + 1) * (ring.TileCells + 1) * 2;
            outerTableOffset = stringTableOffset;
            if (outerTileCount == 0 || !CheckedMultiply(outerTileCount, OuterTileRecordSize, outerTableSize) ||
                !CheckedAdd(outerTableOffset, outerTableSize, stringTableOffset))
            {
                return fail(CookedTerrainWriteStatus::InvalidOuterRing);
            }
        }
        uint64_t payloadOffset = 0;
        if (!CheckedAdd(stringTableOffset, stringTableSize, payloadOffset) || !AlignPayload(payloadOffset, payloadOffset))
        {
            return fail(CookedTerrainWriteStatus::IntegerOverflow);
        }
        if (tileCount != desc.Tiles.size())
        {
            return fail(CookedTerrainWriteStatus::TileCountMismatch);
        }

        Container::VariableArray<const CookedTerrainTileDesc*> orderedTiles;
        Container::VariableArray<CookedTerrainLayerRange> ranges;
        Container::VariableArray<const CookedTerrainOuterTileDesc*> orderedOuterTiles;
        Container::VariableArray<CookedTerrainLayerRange> outerRanges;
        Container::VariableArray<uint8_t> bytes;
        if (tileCount > orderedTiles.max_size() || rangeCount > ranges.max_size() || payloadOffset > bytes.max_size() ||
            outerGridCount > orderedOuterTiles.max_size() || outerTileCount > outerRanges.max_size() ||
            outerTileCount > std::numeric_limits<uint32_t>::max())
        {
            return fail(CookedTerrainWriteStatus::IntegerOverflow);
        }
        orderedTiles.resize(static_cast<size_t>(tileCount), nullptr);
        for (const auto& tile : desc.Tiles)
        {
            if (tile.TileX >= tileCountX || tile.TileZ >= tileCountZ)
            {
                return fail(CookedTerrainWriteStatus::TileIndexOutOfRange);
            }
            // 座標を制限済みなので、この添字は検査済みの tileCount より小さい。
            const size_t coordinate = static_cast<size_t>(tile.TileZ) * tileCountX + tile.TileX;
            if (orderedTiles[coordinate] != nullptr)
            {
                return fail(CookedTerrainWriteStatus::DuplicateTile);
            }
            orderedTiles[coordinate] = &tile;
            if (!IsValidTileBounds(tile, desc.LodCount))
            {
                return fail(CookedTerrainWriteStatus::InvalidTileBounds);
            }
            if (tile.LayerBytes.size() != layerCount)
            {
                return fail(CookedTerrainWriteStatus::LayerBytesCountMismatch);
            }
            for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
            {
                const auto span = tile.LayerBytes[layerIndex];
                if (span.size() > bytes.max_size())
                {
                    return fail(CookedTerrainWriteStatus::IntegerOverflow);
                }
                if (layerIndex == 0)
                {
                    if (span.data() == nullptr || span.size() != heightSize)
                    {
                        return fail(CookedTerrainWriteStatus::HeightSizeMismatch);
                    }
                }
                else
                {
                    if (span.data() == nullptr || span.empty())
                    {
                        return fail(CookedTerrainWriteStatus::EmbeddedTextureInvalid);
                    }
                    const auto parsed = ParseCookedTexture(AssetBlob::CopyBytes(span), false);
                    if (!parsed.Succeeded() || parsed.Texture.PixelFormat != desc.Layers[layerIndex].PixelFormat ||
                        parsed.Texture.ColorSpace != desc.Layers[layerIndex].ColorSpace)
                    {
                        return fail(CookedTerrainWriteStatus::EmbeddedTextureInvalid);
                    }
                }
            }
        }

        orderedOuterTiles.resize(static_cast<size_t>(outerGridCount), nullptr);
        for (const auto& tile : desc.OuterTiles)
        {
            if (!desc.bHasOuterRing)
            {
                break;
            }
            if (tile.TileX >= desc.OuterRing.TileCountX || tile.TileZ >= desc.OuterRing.TileCountZ ||
                TerrainOuterRingDetail::IsHole(desc.OuterRing, tile.TileX, tile.TileZ))
            {
                return fail(CookedTerrainWriteStatus::TileIndexOutOfRange);
            }
            const size_t coordinate = static_cast<size_t>(static_cast<uint64_t>(tile.TileZ) * desc.OuterRing.TileCountX + tile.TileX);
            if (orderedOuterTiles[coordinate] != nullptr)
            {
                return fail(CookedTerrainWriteStatus::DuplicateTile);
            }
            orderedOuterTiles[coordinate] = &tile;
            if (!IsValidTileBounds(tile, desc.LodCount))
            {
                return fail(CookedTerrainWriteStatus::InvalidTileBounds);
            }
            if (tile.HeightBytes.data() == nullptr || tile.HeightBytes.size() != outerHeightSize)
            {
                return fail(CookedTerrainWriteStatus::HeightSizeMismatch);
            }
        }
        if (desc.bHasOuterRing && desc.OuterTiles.size() != outerTileCount)
        {
            return fail(CookedTerrainWriteStatus::InvalidOuterRing);
        }

        // 全タイルが一度ずつ現れるため、座標の添字順が z 昇順、次に x 昇順になる。
        ranges.resize(static_cast<size_t>(rangeCount));
        uint64_t fileSize = payloadOffset;
        size_t rangeIndex = 0;
        for (const auto* tile : orderedTiles)
        {
            for (const auto span : tile->LayerBytes)
            {
                auto& range = ranges[rangeIndex++];
                if (!AlignPayload(fileSize, range.DataOffset) || !CheckedAdd(range.DataOffset, span.size(), fileSize) ||
                    fileSize > bytes.max_size())
                {
                    return fail(CookedTerrainWriteStatus::IntegerOverflow);
                }
                range.DataSize = span.size();
            }
        }

        outerRanges.resize(static_cast<size_t>(outerTileCount));
        size_t outerRangeIndex = 0;
        for (const auto* tile : orderedOuterTiles)
        {
            if (tile == nullptr)
            {
                continue;
            }
            auto& range = outerRanges[outerRangeIndex++];
            if (!AlignPayload(fileSize, range.DataOffset) || !CheckedAdd(range.DataOffset, tile->HeightBytes.size(), fileSize) ||
                fileSize > bytes.max_size())
            {
                return fail(CookedTerrainWriteStatus::IntegerOverflow);
            }
            range.DataSize = tile->HeightBytes.size();
        }

        // 予約欄とアラインメントの隙間は 0 のまま保つ。
        // 別の配列へ書くことで、入力ビューが outBytes を参照していても読み終わるまで有効にする。
        bytes.resize(static_cast<size_t>(fileSize), 0);
        uint8_t* data = bytes.data();
        std::memcpy(data, Magic, MagicSize);
        WriteLe32(data, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(data, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(data, HeaderOffset::VersionMinor, desc.bHasOuterRing ? VersionMinorOuterRing : VersionMinor);
        WriteLe32(data, HeaderOffset::EndianMarker, EndianMarker);
        if (desc.bHasOuterRing)
        {
            WriteLe32(data, HeaderOffset::Flags, HeaderFlagOuterRing);
        }
        WriteLe64(data, HeaderOffset::FileSize, fileSize);
        WriteLe32(data, HeaderOffset::CellCountX, desc.CellCountX);
        WriteLe32(data, HeaderOffset::CellCountZ, desc.CellCountZ);
        WriteLe32(data, HeaderOffset::TileCells, desc.TileCells);
        WriteLe32(data, HeaderOffset::TileCountX, tileCountX);
        WriteLe32(data, HeaderOffset::TileCountZ, tileCountZ);
        WriteLe32(data, HeaderOffset::LodCount, desc.LodCount);
        WriteFloat(data, HeaderOffset::CellSizeM, desc.CellSizeM);
        WriteDouble(data, HeaderOffset::OriginX, desc.OriginX);
        WriteDouble(data, HeaderOffset::OriginY, desc.OriginY);
        WriteDouble(data, HeaderOffset::OriginZ, desc.OriginZ);
        WriteFloat(data, HeaderOffset::HeightMinM, desc.HeightMinM);
        WriteFloat(data, HeaderOffset::HeightMaxM, desc.HeightMaxM);
        WriteLe32(data, HeaderOffset::LayerCount, layerCount);
        WriteLe32(data, HeaderOffset::LayerRecordSize, static_cast<uint32_t>(LayerRecordSize));
        WriteLe32(data, HeaderOffset::TileRecordSize, static_cast<uint32_t>(tileRecordSize));
        WriteLe64(data, HeaderOffset::LayerTableOffset, layerTableOffset);
        WriteLe64(data, HeaderOffset::LayerTableSize, layerTableSize);
        WriteLe64(data, HeaderOffset::TileTableOffset, tileTableOffset);
        WriteLe64(data, HeaderOffset::TileTableSize, tileTableSize);
        WriteLe64(data, HeaderOffset::StringTableOffset, stringTableOffset);
        WriteLe64(data, HeaderOffset::StringTableSize, stringTableSize);
        WriteLe64(data, HeaderOffset::PayloadOffset, payloadOffset);
        WriteLe64(data, HeaderOffset::PayloadSize, fileSize - payloadOffset);

        if (desc.bHasOuterRing)
        {
            const size_t block = HeaderSize;
            const auto& ring = desc.OuterRing;
            WriteLe32(data, block + OuterRingOffset::CellCountX, ring.CellCountX);
            WriteLe32(data, block + OuterRingOffset::CellCountZ, ring.CellCountZ);
            WriteLe32(data, block + OuterRingOffset::TileCells, ring.TileCells);
            WriteLe32(data, block + OuterRingOffset::TileCountX, ring.TileCountX);
            WriteLe32(data, block + OuterRingOffset::TileCountZ, ring.TileCountZ);
            WriteLe32(data, block + OuterRingOffset::HoleTileMinX, ring.HoleTileMinX);
            WriteLe32(data, block + OuterRingOffset::HoleTileMinZ, ring.HoleTileMinZ);
            WriteLe32(data, block + OuterRingOffset::HoleTileCountX, ring.HoleTileCountX);
            WriteLe32(data, block + OuterRingOffset::HoleTileCountZ, ring.HoleTileCountZ);
            WriteFloat(data, block + OuterRingOffset::CellSizeM, ring.CellSizeM);
            WriteDouble(data, block + OuterRingOffset::OriginX, ring.OriginX);
            WriteDouble(data, block + OuterRingOffset::OriginZ, ring.OriginZ);
            WriteLe64(data, block + OuterRingOffset::TileTableOffset, outerTableOffset);
            WriteLe64(data, block + OuterRingOffset::TileTableSize, outerTableSize);
        }
        uint32_t semanticOffset = 0;
        for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
        {
            const auto& layer = desc.Layers[layerIndex];
            const size_t recordOffset = static_cast<size_t>(layerTableOffset + layerIndex * LayerRecordSize);
            WriteLe32(data, recordOffset + LayerRecordOffset::FourCC, layer.FourCC);
            WriteLe32(data, recordOffset + LayerRecordOffset::Encoding, layer.Encoding);
            WriteLe32(data, recordOffset + LayerRecordOffset::PixelFormat, static_cast<uint32_t>(layer.PixelFormat));
            WriteLe32(data, recordOffset + LayerRecordOffset::ColorSpace, static_cast<uint32_t>(layer.ColorSpace));
            WriteLe32(data, recordOffset + LayerRecordOffset::SemanticOffset, semanticOffset);
            WriteLe32(data, recordOffset + LayerRecordOffset::SemanticSize, static_cast<uint32_t>(layer.Semantic.size()));
            std::memcpy(data + static_cast<size_t>(stringTableOffset) + semanticOffset, layer.Semantic.data(), layer.Semantic.size());
            semanticOffset += static_cast<uint32_t>(layer.Semantic.size());
        }

        rangeIndex = 0;
        for (size_t tileIndex = 0; tileIndex < orderedTiles.size(); ++tileIndex)
        {
            const auto& tile = *orderedTiles[tileIndex];
            // 表全体の積と終端を検査済みなので、各レコードと欄の添字も出力の範囲内に収まる。
            const size_t recordOffset = static_cast<size_t>(tileTableOffset + tileIndex * tileRecordSize);
            WriteLe32(data, recordOffset + TileRecordOffset::TileX, tile.TileX);
            WriteLe32(data, recordOffset + TileRecordOffset::TileZ, tile.TileZ);
            WriteFloat(data, recordOffset + TileRecordOffset::HeightMinM, tile.HeightMinM);
            WriteFloat(data, recordOffset + TileRecordOffset::HeightMaxM, tile.HeightMaxM);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                WriteFloat(data, recordOffset + TileRecordOffset::AabbMin + axis * 4, tile.AabbMin[axis]);
                WriteFloat(data, recordOffset + TileRecordOffset::AabbMax + axis * 4, tile.AabbMax[axis]);
            }
            for (uint32_t lod = 0; lod < desc.LodCount; ++lod)
            {
                WriteFloat(data, recordOffset + TileRecordOffset::LodMaxErrorM + lod * 4, tile.LodMaxErrorM[lod]);
            }
            for (size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
            {
                const auto span = tile.LayerBytes[layerIndex];
                auto& range = ranges[rangeIndex++];
                std::memcpy(data + static_cast<size_t>(range.DataOffset), span.data(), span.size());
                range.DataHash = ComputeCookedTexturePayloadHash(span);
                const size_t offset = recordOffset + TileRecordHeadSize + layerIndex * TileLayerRangeSize;
                WriteLe64(data, offset + TileLayerRangeOffset::DataOffset, range.DataOffset);
                WriteLe64(data, offset + TileLayerRangeOffset::DataSize, range.DataSize);
                WriteLe64(data, offset + TileLayerRangeOffset::DataHash, range.DataHash);
            }
        }
        outerRangeIndex = 0;
        for (const auto* tile : orderedOuterTiles)
        {
            if (tile == nullptr)
            {
                continue;
            }
            auto& range = outerRanges[outerRangeIndex];
            const size_t record = static_cast<size_t>(outerTableOffset + outerRangeIndex * OuterTileRecordSize);
            ++outerRangeIndex;
            WriteLe32(data, record + TileRecordOffset::TileX, tile->TileX);
            WriteLe32(data, record + TileRecordOffset::TileZ, tile->TileZ);
            WriteFloat(data, record + TileRecordOffset::HeightMinM, tile->HeightMinM);
            WriteFloat(data, record + TileRecordOffset::HeightMaxM, tile->HeightMaxM);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                WriteFloat(data, record + TileRecordOffset::AabbMin + axis * 4, tile->AabbMin[axis]);
                WriteFloat(data, record + TileRecordOffset::AabbMax + axis * 4, tile->AabbMax[axis]);
            }
            WriteLe32(data, record + TileRecordOffset::Reserved0,
                      TerrainOuterRingDetail::TileFlags(desc.OuterRing, tile->TileX, tile->TileZ));
            for (uint32_t lod = 0; lod < desc.LodCount; ++lod)
            {
                WriteFloat(data, record + TileRecordOffset::LodMaxErrorM + lod * 4, tile->LodMaxErrorM[lod]);
            }
            std::memcpy(data + static_cast<size_t>(range.DataOffset), tile->HeightBytes.data(), tile->HeightBytes.size());
            range.DataHash = ComputeCookedTexturePayloadHash(tile->HeightBytes);
            const size_t height = record + TileRecordOffset::LayerRanges;
            WriteLe64(data, height + TileLayerRangeOffset::DataOffset, range.DataOffset);
            WriteLe64(data, height + TileLayerRangeOffset::DataSize, range.DataSize);
            WriteLe64(data, height + TileLayerRangeOffset::DataHash, range.DataHash);
        }
        WriteLe64(data, HeaderOffset::PayloadHash, ComputeCookedTerrainFileHash({data, bytes.size()}));
        if (desc.bHasOuterRing)
        {
            // 同じ読み手の規則で縁を照合し、入力のバイト列を余分に複製しない。
            auto owner = Container::MakeShared<Container::VariableArray<uint8_t>>();
            owner->swap(bytes);
            const auto parsed = ParseCookedTerrain(AssetBlob::FromOwnedBytes(owner));
            if (!parsed.Succeeded())
            {
                if (parsed.Status == CookedTerrainParseStatus::OuterEdgeMismatch)
                {
                    return fail(CookedTerrainWriteStatus::OuterEdgeMismatch);
                }
                if (parsed.Status == CookedTerrainParseStatus::IntegerOverflow)
                {
                    return fail(CookedTerrainWriteStatus::IntegerOverflow);
                }
                return fail(CookedTerrainWriteStatus::InvalidOuterRing);
            }
            outBytes.swap(*owner);
            return CookedTerrainWriteStatus::Success;
        }
        outBytes.swap(bytes);
        return CookedTerrainWriteStatus::Success;
    }
} // namespace NorvesLib::Core::Asset
