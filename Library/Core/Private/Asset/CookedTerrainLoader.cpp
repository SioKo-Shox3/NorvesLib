#include "Asset/CookedTerrainFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace TerrainOuterRingDetail
    {
        bool IsHole(const CookedTerrainOuterRing& ring, uint32_t x, uint32_t z) noexcept
        {
            return x >= ring.HoleTileMinX && x - ring.HoleTileMinX < ring.HoleTileCountX &&
                   z >= ring.HoleTileMinZ && z - ring.HoleTileMinZ < ring.HoleTileCountZ;
        }

        uint32_t TileFlags(const CookedTerrainOuterRing& ring, uint32_t x, uint32_t z) noexcept
        {
            using namespace CookedTerrainFormatV0;
            uint32_t flags = OuterTileFlagOuter;
            const bool bInX = x >= ring.HoleTileMinX && x - ring.HoleTileMinX < ring.HoleTileCountX;
            const bool bInZ = z >= ring.HoleTileMinZ && z - ring.HoleTileMinZ < ring.HoleTileCountZ;
            if (bInX && z == ring.HoleTileMinZ + ring.HoleTileCountZ)
            {
                flags |= OuterTileFlagSharedNegZ;
            }
            if (bInZ && x + 1 == ring.HoleTileMinX)
            {
                flags |= OuterTileFlagSharedPosX;
            }
            if (bInX && z + 1 == ring.HoleTileMinZ)
            {
                flags |= OuterTileFlagSharedPosZ;
            }
            if (bInZ && x == ring.HoleTileMinX + ring.HoleTileCountX)
            {
                flags |= OuterTileFlagSharedNegX;
            }
            return flags;
        }
    }

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

        bool NearEqual(double value, double expected) noexcept
        {
            return std::isfinite(value) && std::isfinite(expected) &&
                   std::abs(value - expected) <= 1e-6 * std::max(1.0, std::abs(expected));
        }

        bool IsValidOuterRing(const CookedTerrainOuterRing& ring, const CookedTerrainData& nearTerrain) noexcept
        {
            if (ring.TileCells < 2 || ring.TileCells > 1024 || (ring.TileCells & (ring.TileCells - 1)) != 0 ||
                ring.TileCountX == 0 || ring.TileCountZ == 0 ||
                static_cast<uint64_t>(ring.TileCountX) * ring.TileCells != ring.CellCountX ||
                static_cast<uint64_t>(ring.TileCountZ) * ring.TileCells != ring.CellCountZ ||
                ring.HoleTileCountX == 0 || ring.HoleTileCountZ == 0 ||
                ring.HoleTileMinX >= ring.TileCountX || ring.HoleTileMinZ >= ring.TileCountZ ||
                ring.HoleTileCountX > ring.TileCountX - ring.HoleTileMinX ||
                ring.HoleTileCountZ > ring.TileCountZ - ring.HoleTileMinZ ||
                !std::isfinite(ring.CellSizeM) || ring.CellSizeM <= 0.0f ||
                !std::isfinite(ring.OriginX) || !std::isfinite(ring.OriginZ) || ring.Reserved != 0)
            {
                return false;
            }
            const double ratio = static_cast<double>(ring.CellSizeM) / nearTerrain.CellSizeM;
            const double rounded = std::round(ratio);
            if (!std::isfinite(ratio) || rounded < 1.0 ||
                std::abs(ratio - rounded) > 1e-6 * rounded)
            {
                return false;
            }
            const double tileWidth = static_cast<double>(ring.TileCells) * ring.CellSizeM;
            return NearEqual(ring.OriginX + ring.HoleTileMinX * tileWidth, nearTerrain.OriginX) &&
                   NearEqual(ring.OriginZ + ring.HoleTileMinZ * tileWidth, nearTerrain.OriginZ) &&
                   NearEqual(ring.HoleTileCountX * tileWidth, static_cast<double>(nearTerrain.CellCountX) * nearTerrain.CellSizeM) &&
                   NearEqual(ring.HoleTileCountZ * tileWidth, static_cast<double>(nearTerrain.CellCountZ) * nearTerrain.CellSizeM);
        }

        // 穴の角に点だけで接するタイルも含め、同じ位置を持つ近景タイルの標本をすべて照合する。
        bool OuterEdgesMatch(const CookedTerrainData& terrain, const uint8_t* data) noexcept
        {
            const auto& ring = terrain.OuterRing;
            const uint64_t minX = static_cast<uint64_t>(ring.HoleTileMinX) * ring.TileCells;
            const uint64_t minZ = static_cast<uint64_t>(ring.HoleTileMinZ) * ring.TileCells;
            const uint64_t maxX = minX + static_cast<uint64_t>(ring.HoleTileCountX) * ring.TileCells;
            const uint64_t maxZ = minZ + static_cast<uint64_t>(ring.HoleTileCountZ) * ring.TileCells;
            const double ratio = std::round(static_cast<double>(ring.CellSizeM) / terrain.CellSizeM);
            const uint64_t outerSide = static_cast<uint64_t>(ring.TileCells) + 1;
            const uint64_t nearSide = static_cast<uint64_t>(terrain.TileCells) + 1;
            for (const auto& tile : terrain.OuterTiles)
            {
                const uint64_t baseX = static_cast<uint64_t>(tile.TileX) * ring.TileCells;
                const uint64_t baseZ = static_cast<uint64_t>(tile.TileZ) * ring.TileCells;
                if (baseX > maxX || baseX + ring.TileCells < minX || baseZ > maxZ || baseZ + ring.TileCells < minZ)
                {
                    continue;
                }
                const auto matches = [&](uint32_t sx, uint32_t sz)
                {
                    const uint64_t x = baseX + sx;
                    const uint64_t z = baseZ + sz;
                    if (!(((z == minZ || z == maxZ) && x >= minX && x <= maxX) ||
                          ((x == minX || x == maxX) && z >= minZ && z <= maxZ)))
                    {
                        return true;
                    }
                    const double nearX = x == maxX ? terrain.CellCountX : static_cast<double>(x - minX) * ratio;
                    const double nearZ = z == maxZ ? terrain.CellCountZ : static_cast<double>(z - minZ) * ratio;
                    if (nearX > terrain.CellCountX || nearZ > terrain.CellCountZ)
                    {
                        return true;
                    }
                    const uint64_t nx = static_cast<uint64_t>(nearX);
                    const uint64_t nz = static_cast<uint64_t>(nearZ);
                    const uint16_t outer = ReadLe16(data, static_cast<size_t>(tile.Height.DataOffset + 2 * (sz * outerSide + sx)));
                    const uint32_t tx1 = static_cast<uint32_t>(std::min<uint64_t>(nx / terrain.TileCells, terrain.TileCountX - 1));
                    const uint32_t tz1 = static_cast<uint32_t>(std::min<uint64_t>(nz / terrain.TileCells, terrain.TileCountZ - 1));
                    const uint32_t tx0 = nx != 0 && nx % terrain.TileCells == 0 ? static_cast<uint32_t>(nx / terrain.TileCells - 1) : tx1;
                    const uint32_t tz0 = nz != 0 && nz % terrain.TileCells == 0 ? static_cast<uint32_t>(nz / terrain.TileCells - 1) : tz1;
                    for (uint32_t tz = tz0; tz <= tz1; ++tz)
                    {
                        for (uint32_t tx = tx0; tx <= tx1; ++tx)
                        {
                            const size_t coord = static_cast<size_t>(static_cast<uint64_t>(tz) * terrain.TileCountX + tx);
                            const size_t index = terrain.TileIndexByCoord[coord];
                            const auto& height = terrain.LayerRanges[index * terrain.Layers.size()];
                            const uint64_t localX = nx - static_cast<uint64_t>(tx) * terrain.TileCells;
                            const uint64_t localZ = nz - static_cast<uint64_t>(tz) * terrain.TileCells;
                            if (ReadLe16(data, static_cast<size_t>(height.DataOffset + 2 * (localZ * nearSide + localX))) != outer)
                            {
                                return false;
                            }
                        }
                    }
                    return true;
                };
                for (uint32_t i = 0; i <= ring.TileCells; ++i)
                {
                    if (!matches(i, 0) || !matches(i, ring.TileCells) ||
                        !matches(0, i) || !matches(ring.TileCells, i))
                    {
                        return false;
                    }
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

    namespace TerrainOuterRingDetail
    {
        bool IsValid(const CookedTerrainOuterRing& ring, const CookedTerrainData& nearTerrain) noexcept
        {
            return IsValidOuterRing(ring, nearTerrain);
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

    uint32_t CookedTerrainData::GetOuterSamplesPerSide() const noexcept
    {
        return bHasOuterRing ? OuterRing.TileCells + 1 : 0;
    }

    const CookedTerrainOuterTile* CookedTerrainData::FindOuterTile(uint32_t tileX, uint32_t tileZ, uint32_t* outIndex) const noexcept
    {
        if (!bHasOuterRing || tileX >= OuterRing.TileCountX || tileZ >= OuterRing.TileCountZ)
        {
            return nullptr;
        }
        const uint64_t coordinate = static_cast<uint64_t>(tileZ) * OuterRing.TileCountX + tileX;
        if (coordinate >= OuterTileIndexByCoord.size())
        {
            return nullptr;
        }
        const uint32_t index = OuterTileIndexByCoord[static_cast<size_t>(coordinate)];
        if (index >= OuterTiles.size() || OuterTiles[index].TileX != tileX || OuterTiles[index].TileZ != tileZ)
        {
            return nullptr;
        }
        if (outIndex != nullptr)
        {
            *outIndex = index;
        }
        return &OuterTiles[index];
    }

    uint16_t CookedTerrainData::GetOuterHeightSample(uint32_t outerTileIndex, uint32_t sampleX, uint32_t sampleZ) const noexcept
    {
        const uint32_t side = GetOuterSamplesPerSide();
        if (outerTileIndex >= OuterTiles.size() || sampleX >= side || sampleZ >= side || !SourceBlob.IsValid())
        {
            return 0;
        }
        const auto& range = OuterTiles[outerTileIndex].Height;
        uint64_t sampleOffset = 0;
        uint64_t absoluteOffset = 0;
        if (!IsRangeWithin(range.DataOffset, range.DataSize, SourceBlob.GetSize()) ||
            !CheckedMultiply(sampleZ, side, sampleOffset) || !CheckedAdd(sampleOffset, sampleX, sampleOffset) ||
            !CheckedMultiply(sampleOffset, 2, sampleOffset) || !IsRangeWithin(sampleOffset, 2, range.DataSize) ||
            !CheckedAdd(range.DataOffset, sampleOffset, absoluteOffset) || !IsRangeWithin(absoluteOffset, 2, SourceBlob.GetSize()))
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
        const uint16_t minor = ReadLe16(data, HeaderOffset::VersionMinor);
        const uint32_t flags = ReadLe32(data, HeaderOffset::Flags);
        if (ReadLe16(data, HeaderOffset::VersionMajor) != VersionMajor || minor > VersionMinorOuterRing ||
            (minor == VersionMinorOuterRing && flags != HeaderFlagOuterRing))
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
        if ((minor == VersionMinor && flags != 0) || ReadLe32(data, HeaderOffset::Reserved0) != 0 ||
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

        terrain.bHasOuterRing = minor == VersionMinorOuterRing;
        uint64_t outerGridCount = 0;
        uint64_t outerTileCount = 0;
        uint64_t outerHeightSize = 0;
        uint64_t expectedOuterTableSize = 0;
        uint64_t outerTableEnd = 0;
        if (terrain.bHasOuterRing)
        {
            if (fileSize < HeaderSize + OuterRingBlockSize)
            {
                return Fail(CookedTerrainParseStatus::InvalidOuterRing);
            }
            const size_t block = HeaderSize;
            auto& ring = terrain.OuterRing;
            ring.CellCountX = ReadLe32(data, block + OuterRingOffset::CellCountX);
            ring.CellCountZ = ReadLe32(data, block + OuterRingOffset::CellCountZ);
            ring.TileCells = ReadLe32(data, block + OuterRingOffset::TileCells);
            ring.TileCountX = ReadLe32(data, block + OuterRingOffset::TileCountX);
            ring.TileCountZ = ReadLe32(data, block + OuterRingOffset::TileCountZ);
            ring.HoleTileMinX = ReadLe32(data, block + OuterRingOffset::HoleTileMinX);
            ring.HoleTileMinZ = ReadLe32(data, block + OuterRingOffset::HoleTileMinZ);
            ring.HoleTileCountX = ReadLe32(data, block + OuterRingOffset::HoleTileCountX);
            ring.HoleTileCountZ = ReadLe32(data, block + OuterRingOffset::HoleTileCountZ);
            ring.CellSizeM = ReadFloat(data, block + OuterRingOffset::CellSizeM);
            ring.OriginX = ReadDouble(data, block + OuterRingOffset::OriginX);
            ring.OriginZ = ReadDouble(data, block + OuterRingOffset::OriginZ);
            ring.TileTableOffset = ReadLe64(data, block + OuterRingOffset::TileTableOffset);
            ring.TileTableSize = ReadLe64(data, block + OuterRingOffset::TileTableSize);
            ring.Reserved = ReadLe64(data, block + OuterRingOffset::Reserved);
            if (!IsValidOuterRing(ring, terrain))
            {
                return Fail(CookedTerrainParseStatus::InvalidOuterRing);
            }
            outerGridCount = static_cast<uint64_t>(ring.TileCountX) * ring.TileCountZ;
            outerTileCount = outerGridCount - static_cast<uint64_t>(ring.HoleTileCountX) * ring.HoleTileCountZ;
            outerHeightSize = static_cast<uint64_t>(ring.TileCells + 1) * (ring.TileCells + 1) * 2;
            if (outerTileCount == 0 || !CheckedMultiply(outerTileCount, OuterTileRecordSize, expectedOuterTableSize) ||
                !CheckedAdd(ring.TileTableOffset, ring.TileTableSize, outerTableEnd))
            {
                return Fail(CookedTerrainParseStatus::InvalidOuterRing);
            }
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
        if (terrain.bHasOuterRing &&
            (terrain.OuterRing.TileTableOffset != tileEnd || terrain.OuterRing.TileTableSize != expectedOuterTableSize ||
             outerTableEnd != stringTableOffset || !IsRangeWithin(terrain.OuterRing.TileTableOffset, terrain.OuterRing.TileTableSize, fileSize)))
        {
            return Fail(CookedTerrainParseStatus::InvalidOuterRing);
        }
        if (layerTableOffset != HeaderSize + (terrain.bHasOuterRing ? OuterRingBlockSize : 0) || layerTableSize != expectedLayerSize ||
            tileTableOffset != layerEnd || tileTableSize != expectedTileSize ||
            (!terrain.bHasOuterRing && stringTableOffset != tileEnd) ||
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
            layerCount > terrain.Layers.max_size() || outerTileCount > std::numeric_limits<uint32_t>::max() ||
            outerTileCount > terrain.OuterTiles.max_size() || outerGridCount > terrain.OuterTileIndexByCoord.max_size())
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
        for (uint64_t tileIndex = 0; tileIndex < outerTileCount; ++tileIndex)
        {
            const size_t record = static_cast<size_t>(terrain.OuterRing.TileTableOffset + tileIndex * OuterTileRecordSize);
            if (ReadLe32(data, record + TileRecordOffset::Reserved1) != 0)
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
        terrain.OuterTiles.resize(static_cast<size_t>(outerTileCount));
        terrain.OuterTileIndexByCoord.resize(static_cast<size_t>(outerGridCount), std::numeric_limits<uint32_t>::max());
        for (uint64_t tileIndex = 0; tileIndex < outerTileCount; ++tileIndex)
        {
            const size_t record = static_cast<size_t>(terrain.OuterRing.TileTableOffset + tileIndex * OuterTileRecordSize);
            auto& tile = terrain.OuterTiles[static_cast<size_t>(tileIndex)];
            tile.TileX = ReadLe32(data, record + TileRecordOffset::TileX);
            tile.TileZ = ReadLe32(data, record + TileRecordOffset::TileZ);
            if (tile.TileX >= terrain.OuterRing.TileCountX || tile.TileZ >= terrain.OuterRing.TileCountZ ||
                TerrainOuterRingDetail::IsHole(terrain.OuterRing, tile.TileX, tile.TileZ))
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
            tile.Flags = ReadLe32(data, record + TileRecordOffset::Reserved0);
            for (uint32_t lod = 0; lod < MaxLodCount; ++lod)
            {
                tile.LodMaxErrorM[lod] = ReadFloat(data, record + TileRecordOffset::LodMaxErrorM + lod * 4);
            }
            const size_t range = record + TileRecordOffset::LayerRanges;
            tile.Height.DataOffset = ReadLe64(data, range + TileLayerRangeOffset::DataOffset);
            tile.Height.DataSize = ReadLe64(data, range + TileLayerRangeOffset::DataSize);
            tile.Height.DataHash = ReadLe64(data, range + TileLayerRangeOffset::DataHash);
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
        for (uint64_t tileIndex = 0; tileIndex < outerTileCount; ++tileIndex)
        {
            const auto& tile = terrain.OuterTiles[static_cast<size_t>(tileIndex)];
            const size_t coordinate = static_cast<size_t>(static_cast<uint64_t>(tile.TileZ) * terrain.OuterRing.TileCountX + tile.TileX);
            if (terrain.OuterTileIndexByCoord[coordinate] != std::numeric_limits<uint32_t>::max())
            {
                return Fail(CookedTerrainParseStatus::DuplicateTile);
            }
            terrain.OuterTileIndexByCoord[coordinate] = static_cast<uint32_t>(tileIndex);
        }
        for (const auto& tile : terrain.OuterTiles)
        {
            if (tile.Flags != TerrainOuterRingDetail::TileFlags(terrain.OuterRing, tile.TileX, tile.TileZ))
            {
                return Fail(CookedTerrainParseStatus::InvalidOuterRing);
            }
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
        for (const auto& tile : terrain.OuterTiles)
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
        for (const auto& tile : terrain.OuterTiles)
        {
            const auto& range = tile.Height;
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
        for (const auto& tile : terrain.OuterTiles)
        {
            if (tile.Height.DataSize != outerHeightSize)
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
        for (const auto& tile : terrain.OuterTiles)
        {
            const auto& range = tile.Height;
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
        for (const auto& tile : terrain.OuterTiles)
        {
            const auto& range = tile.Height;
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
        if (terrain.bHasOuterRing && !OuterEdgesMatch(terrain, data))
        {
            return Fail(CookedTerrainParseStatus::OuterEdgeMismatch);
        }
        terrain.SourceBlob = std::move(sourceBlob);
        result.Status = CookedTerrainParseStatus::Success;
        return result;
    }
} // namespace NorvesLib::Core::Asset
