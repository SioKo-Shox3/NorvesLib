#include "Asset/CookedTextureFormat.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Asset
{
    namespace
    {
        uint16_t ReadLe16(const uint8_t *data, size_t offset)
        {
            return static_cast<uint16_t>(data[offset]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadLe32(const uint8_t *data, size_t offset)
        {
            return static_cast<uint32_t>(data[offset]) |
                   (static_cast<uint32_t>(data[offset + 1]) << 8) |
                   (static_cast<uint32_t>(data[offset + 2]) << 16) |
                   (static_cast<uint32_t>(data[offset + 3]) << 24);
        }

        uint64_t ReadLe64(const uint8_t *data, size_t offset)
        {
            return static_cast<uint64_t>(ReadLe32(data, offset)) |
                   (static_cast<uint64_t>(ReadLe32(data, offset + 4)) << 32);
        }

        CookedTextureParseResult Fail(CookedTextureParseStatus status)
        {
            CookedTextureParseResult result;
            result.Status = status;
            return result;
        }

        bool ConvertRange(uint64_t offset64, uint64_t size64, size_t fileSize, size_t &outOffset, size_t &outSize)
        {
            if (offset64 > static_cast<uint64_t>(fileSize))
            {
                return false;
            }

            const uint64_t remaining = static_cast<uint64_t>(fileSize) - offset64;
            if (size64 > remaining)
            {
                return false;
            }

            if (offset64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
                size64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return false;
            }

            outOffset = static_cast<size_t>(offset64);
            outSize = static_cast<size_t>(size64);
            return true;
        }

        bool AddChecked(size_t left, size_t right, size_t &outValue)
        {
            if (right > std::numeric_limits<size_t>::max() - left)
            {
                return false;
            }

            outValue = left + right;
            return true;
        }

        bool AddChecked64(uint64_t left, uint64_t right, uint64_t &outValue)
        {
            if (right > std::numeric_limits<uint64_t>::max() - left)
            {
                return false;
            }

            outValue = left + right;
            return true;
        }

        bool MultiplyChecked64(uint64_t left, uint64_t right, uint64_t &outValue)
        {
            if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
            {
                return false;
            }

            outValue = left * right;
            return true;
        }

        bool HasExactMagic(Container::Span<const uint8_t> bytes)
        {
            return bytes.size() >= CookedTextureFormatV0::MagicSize &&
                   std::memcmp(bytes.data(), CookedTextureFormatV0::Magic, CookedTextureFormatV0::MagicSize) == 0;
        }

        // 版ごとに使える形式が違う。v0.0 は非圧縮の 3 形式だけで、v0.1 はそれに BC と R16 が加わる。
        bool IsKnownPixelFormat(uint16_t versionMinor, uint32_t rawPixelFormat)
        {
            if (rawPixelFormat == CookedTextureFormatV0::PixelFormatR8UNorm ||
                rawPixelFormat == CookedTextureFormatV0::PixelFormatRG8UNorm ||
                rawPixelFormat == CookedTextureFormatV0::PixelFormatRGBA8UNorm)
            {
                return true;
            }

            if (versionMinor < CookedTextureFormatV0::VersionMinorBlockCompressed)
            {
                return false;
            }

            return rawPixelFormat == CookedTextureFormatV0::PixelFormatBC1 ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatBC4 ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatBC5 ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatBC7 ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatR16UNorm;
        }

        bool IsKnownColorSpace(uint32_t rawColorSpace)
        {
            return rawColorSpace == CookedTextureFormatV0::ColorSpaceLinear ||
                   rawColorSpace == CookedTextureFormatV0::ColorSpaceSRGB;
        }

        bool IsValidColorSpaceForFormat(CookedTexturePixelFormat pixelFormat, CookedTextureColorSpace colorSpace)
        {
            if (colorSpace == CookedTextureColorSpace::Linear)
            {
                return true;
            }

            return colorSpace == CookedTextureColorSpace::SRGB &&
                   (pixelFormat == CookedTexturePixelFormat::RGBA8UNorm ||
                    pixelFormat == CookedTexturePixelFormat::BC1 ||
                    pixelFormat == CookedTexturePixelFormat::BC7);
        }

        // 1 ミップ（全レイヤー）のバイト数。ブロック圧縮の形式はブロック単位で、端は切り上げ、最小 1 ブロック。
        bool ComputeExpectedMipPayloadSize(CookedTexturePixelFormat pixelFormat,
                                           uint32_t width,
                                           uint32_t height,
                                           uint32_t layerCount,
                                           uint64_t &outSize)
        {
            uint64_t rowBytes = 0;
            uint64_t rowCount = 0;
            if (!ComputeCookedTextureMipLayout(pixelFormat, width, height, rowBytes, rowCount))
            {
                return false;
            }

            uint64_t value = rowBytes;
            if (!MultiplyChecked64(value, rowCount, value) ||
                !MultiplyChecked64(value, layerCount, value))
            {
                return false;
            }

            outSize = value;
            return true;
        }
    }

    bool ComputeCookedTextureTileGrid(CookedTexturePixelFormat pixelFormat,
                                      uint32_t mipWidth,
                                      uint32_t mipHeight,
                                      uint32_t &outTilesX,
                                      uint32_t &outTilesY) noexcept
    {
        const CookedTextureTileShape shape = GetCookedTextureStandardTileShape(pixelFormat);
        if (shape.Width == 0 || shape.Height == 0 || mipWidth == 0 || mipHeight == 0)
        {
            return false;
        }

        outTilesX = static_cast<uint32_t>((static_cast<uint64_t>(mipWidth) + shape.Width - 1) / shape.Width);
        outTilesY = static_cast<uint32_t>((static_cast<uint64_t>(mipHeight) + shape.Height - 1) / shape.Height);
        return true;
    }

    bool ComputeCookedTextureTileRect(CookedTexturePixelFormat pixelFormat,
                                      uint32_t mipWidth,
                                      uint32_t mipHeight,
                                      uint32_t tileX,
                                      uint32_t tileY,
                                      CookedTextureTileRect &outRect) noexcept
    {
        uint32_t tilesX = 0;
        uint32_t tilesY = 0;
        if (!ComputeCookedTextureTileGrid(pixelFormat, mipWidth, mipHeight, tilesX, tilesY) ||
            tileX >= tilesX || tileY >= tilesY)
        {
            return false;
        }

        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(pixelFormat);
        const CookedTextureTileShape shape = GetCookedTextureStandardTileShape(pixelFormat);
        const uint64_t tileBlocksX = shape.Width / block.BlockWidth;
        const uint64_t tileBlocksY = shape.Height / block.BlockHeight;
        const uint64_t totalBlocksX = (static_cast<uint64_t>(mipWidth) + block.BlockWidth - 1) / block.BlockWidth;
        const uint64_t totalBlocksY = (static_cast<uint64_t>(mipHeight) + block.BlockHeight - 1) / block.BlockHeight;
        const uint64_t blockX = static_cast<uint64_t>(tileX) * tileBlocksX;
        const uint64_t blockY = static_cast<uint64_t>(tileY) * tileBlocksY;
        const uint64_t blocksX = std::min(tileBlocksX, totalBlocksX - blockX);
        const uint64_t blocksY = std::min(tileBlocksY, totalBlocksY - blockY);

        outRect.BlockX = static_cast<uint32_t>(blockX);
        outRect.BlockY = static_cast<uint32_t>(blockY);
        outRect.BlocksX = static_cast<uint32_t>(blocksX);
        outRect.BlocksY = static_cast<uint32_t>(blocksY);
        outRect.RowBytes = blocksX * block.BlockBytes;
        outRect.DataBytes = outRect.RowBytes * blocksY;
        return true;
    }

    void GatherCookedTextureTile(CookedTexturePixelFormat pixelFormat,
                                 uint32_t mipWidth,
                                 const CookedTextureTileRect &rect,
                                 const uint8_t *mipLayerBytes,
                                 uint8_t *outTile) noexcept
    {
        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(pixelFormat);
        const uint64_t totalBlocksX = (static_cast<uint64_t>(mipWidth) + block.BlockWidth - 1) / block.BlockWidth;
        const uint64_t mipRowBytes = totalBlocksX * block.BlockBytes;
        for (uint32_t row = 0; row < rect.BlocksY; ++row)
        {
            const uint64_t mipOffset = (static_cast<uint64_t>(rect.BlockY) + row) * mipRowBytes +
                                       static_cast<uint64_t>(rect.BlockX) * block.BlockBytes;
            std::memcpy(outTile + static_cast<uint64_t>(row) * rect.RowBytes,
                        mipLayerBytes + mipOffset,
                        static_cast<size_t>(rect.RowBytes));
        }
    }

    void ScatterCookedTextureTile(CookedTexturePixelFormat pixelFormat,
                                  uint32_t mipWidth,
                                  const CookedTextureTileRect &rect,
                                  const uint8_t *tile,
                                  uint8_t *mipLayerBytes) noexcept
    {
        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(pixelFormat);
        const uint64_t totalBlocksX = (static_cast<uint64_t>(mipWidth) + block.BlockWidth - 1) / block.BlockWidth;
        const uint64_t mipRowBytes = totalBlocksX * block.BlockBytes;
        for (uint32_t row = 0; row < rect.BlocksY; ++row)
        {
            const uint64_t mipOffset = (static_cast<uint64_t>(rect.BlockY) + row) * mipRowBytes +
                                       static_cast<uint64_t>(rect.BlockX) * block.BlockBytes;
            std::memcpy(mipLayerBytes + mipOffset,
                        tile + static_cast<uint64_t>(row) * rect.RowBytes,
                        static_cast<size_t>(rect.RowBytes));
        }
    }

    Container::Span<const uint8_t> CookedTextureData::GetMipBytes(size_t index) const noexcept
    {
        if (index >= Mips.size())
        {
            return {};
        }

        const CookedTextureMip &mip = Mips[index];
        if (bTiled)
        {
            // v0.2 は展開済みのバイト列から返す。展開していない解析では空。
            if (!RowMajorStorage || mip.RowMajorOffset > RowMajorStorage->size() ||
                mip.DataSize > RowMajorStorage->size() - mip.RowMajorOffset)
            {
                return {};
            }

            return Container::Span<const uint8_t>(RowMajorStorage->data() + mip.RowMajorOffset, mip.DataSize);
        }

        if (!SourceBlob.IsValid())
        {
            return {};
        }

        const Container::Span<const uint8_t> bytes = SourceBlob.GetSpan();
        if (mip.DataOffset > bytes.size() || mip.DataSize > bytes.size() - mip.DataOffset)
        {
            return {};
        }

        return Container::Span<const uint8_t>(bytes.data() + mip.DataOffset, mip.DataSize);
    }

    bool CookedTextureData::FindTile(uint32_t mipIndex,
                                     uint32_t layerIndex,
                                     uint32_t tileX,
                                     uint32_t tileY,
                                     CookedTextureTile &outTile) const noexcept
    {
        if (!bTiled || mipIndex >= Tiling.FirstTailMip || mipIndex >= Mips.size() ||
            static_cast<size_t>(mipIndex) + 1 >= Tiling.MipFirstTile.size() || layerIndex >= LayerCount)
        {
            return false;
        }

        uint32_t tilesX = 0;
        uint32_t tilesY = 0;
        if (!ComputeCookedTextureTileGrid(PixelFormat, Mips[mipIndex].Width, Mips[mipIndex].Height, tilesX, tilesY) ||
            tileX >= tilesX || tileY >= tilesY)
        {
            return false;
        }

        const uint64_t index = static_cast<uint64_t>(Tiling.MipFirstTile[mipIndex]) +
                               (static_cast<uint64_t>(layerIndex) * tilesY + tileY) * tilesX + tileX;
        if (index >= Tiling.MipFirstTile[static_cast<size_t>(mipIndex) + 1] || index >= Tiling.Tiles.size())
        {
            return false;
        }

        outTile = Tiling.Tiles[static_cast<size_t>(index)];
        return true;
    }

    namespace
    {
        size_t GetHeaderSizeForVersion(uint16_t versionMinor)
        {
            return versionMinor == CookedTextureFormatV0::VersionMinorTiled ? CookedTextureFormatV0::HeaderSizeTiled
                                                                           : CookedTextureFormatV0::HeaderSize;
        }

        bool IsSupportedVersion(uint16_t versionMajor, uint16_t versionMinor)
        {
            return versionMajor == CookedTextureFormatV0::VersionMajor &&
                   (versionMinor == CookedTextureFormatV0::VersionMinor ||
                    versionMinor == CookedTextureFormatV0::VersionMinorBlockCompressed ||
                    versionMinor == CookedTextureFormatV0::VersionMinorTiled);
        }

        // v0.2 のタイルの表を検証して tiling へ読む。mips は検証済みの段の表、payloadOffset64・payloadSize64 はヘッダの値。
        // bytes はメタデータ（少なくとも表の終わりまで）を含む。
        CookedTextureParseStatus ParseTiling(const uint8_t *data,
                                             size_t availableBytes,
                                             size_t fileSize,
                                             CookedTexturePixelFormat pixelFormat,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t layerCount,
                                             uint32_t mipCount,
                                             size_t mipTableEnd,
                                             uint64_t payloadOffset64,
                                             uint64_t payloadSize64,
                                             const Container::VariableArray<CookedTextureMip> &mips,
                                             CookedTextureTiling &outTiling)
        {
            using namespace CookedTextureFormatV0;

            const uint32_t tileWidth = ReadLe32(data, TiledHeaderOffset::TileWidth);
            const uint32_t tileHeight = ReadLe32(data, TiledHeaderOffset::TileHeight);
            const uint32_t firstTailMip = ReadLe32(data, TiledHeaderOffset::FirstTailMip);
            const uint32_t tileDataBytes = ReadLe32(data, TiledHeaderOffset::TileDataBytes);
            const uint64_t tileTableOffset64 = ReadLe64(data, TiledHeaderOffset::TileTableOffset);
            const uint64_t tileTableSize64 = ReadLe64(data, TiledHeaderOffset::TileTableSize);
            const uint64_t tailOffset64 = ReadLe64(data, TiledHeaderOffset::TailOffset);
            const uint64_t tailSize64 = ReadLe64(data, TiledHeaderOffset::TailSize);

            const CookedTextureTileShape shape = GetCookedTextureStandardTileShape(pixelFormat);
            if (shape.Width == 0 || tileWidth != shape.Width || tileHeight != shape.Height ||
                tileDataBytes != StandardTileBytes)
            {
                return CookedTextureParseStatus::InvalidTileShape;
            }

            if (firstTailMip >= mipCount ||
                firstTailMip != ComputeCookedTextureFirstTailMip(pixelFormat, width, height))
            {
                return CookedTextureParseStatus::InvalidFirstTailMip;
            }

            // 表の件数は形式から決まる。表の大きさが一致してから確保する。
            uint64_t expectedCount = 0;
            for (uint32_t mipIndex = 0; mipIndex < firstTailMip; ++mipIndex)
            {
                uint32_t tilesX = 0;
                uint32_t tilesY = 0;
                if (!ComputeCookedTextureTileGrid(pixelFormat, mips[mipIndex].Width, mips[mipIndex].Height, tilesX, tilesY))
                {
                    return CookedTextureParseStatus::IntegerOverflow;
                }

                uint64_t mipTiles = 0;
                if (!MultiplyChecked64(static_cast<uint64_t>(tilesX), static_cast<uint64_t>(tilesY), mipTiles) ||
                    !MultiplyChecked64(mipTiles, static_cast<uint64_t>(layerCount), mipTiles) ||
                    !AddChecked64(expectedCount, mipTiles, expectedCount))
                {
                    return CookedTextureParseStatus::IntegerOverflow;
                }
            }

            uint64_t expectedTableSize = 0;
            if (!MultiplyChecked64(expectedCount, static_cast<uint64_t>(TileRecordSize), expectedTableSize))
            {
                return CookedTextureParseStatus::IntegerOverflow;
            }

            if (tileTableSize64 != expectedTableSize)
            {
                return CookedTextureParseStatus::TileTableSizeMismatch;
            }

            size_t tileTableOffset = 0;
            size_t tileTableSize = 0;
            if (tileTableOffset64 != static_cast<uint64_t>(mipTableEnd) ||
                !ConvertRange(tileTableOffset64, tileTableSize64, fileSize, tileTableOffset, tileTableSize))
            {
                return CookedTextureParseStatus::TileTableOutOfRange;
            }

            size_t tileTableEnd = 0;
            if (!AddChecked(tileTableOffset, tileTableSize, tileTableEnd) ||
                static_cast<uint64_t>(tileTableEnd) != payloadOffset64)
            {
                return CookedTextureParseStatus::TileTableOutOfRange;
            }

            if (tileTableEnd > availableBytes)
            {
                return CookedTextureParseStatus::MetadataTooSmall;
            }

            outTiling.TileWidth = tileWidth;
            outTiling.TileHeight = tileHeight;
            outTiling.FirstTailMip = firstTailMip;
            outTiling.Tiles.reserve(static_cast<size_t>(expectedCount));
            outTiling.MipFirstTile.reserve(static_cast<size_t>(firstTailMip) + 1);

            uint64_t cursor = payloadOffset64;
            size_t recordIndex = 0;
            for (uint32_t mipIndex = 0; mipIndex < firstTailMip; ++mipIndex)
            {
                if (static_cast<uint64_t>(mips[mipIndex].DataOffset) != cursor)
                {
                    return CookedTextureParseStatus::TileRecordMismatch;
                }

                outTiling.MipFirstTile.push_back(static_cast<uint32_t>(recordIndex));

                uint32_t tilesX = 0;
                uint32_t tilesY = 0;
                (void)ComputeCookedTextureTileGrid(pixelFormat, mips[mipIndex].Width, mips[mipIndex].Height, tilesX, tilesY);
                for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
                {
                    for (uint32_t tileY = 0; tileY < tilesY; ++tileY)
                    {
                        for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
                        {
                            CookedTextureTileRect rect;
                            if (!ComputeCookedTextureTileRect(
                                    pixelFormat, mips[mipIndex].Width, mips[mipIndex].Height, tileX, tileY, rect))
                            {
                                return CookedTextureParseStatus::TileRecordMismatch;
                            }

                            const size_t recordOffset = tileTableOffset + recordIndex * TileRecordSize;
                            const uint64_t dataOffset64 = ReadLe64(data, recordOffset + TileRecordOffset::DataOffset);
                            const uint64_t dataSize64 = ReadLe64(data, recordOffset + TileRecordOffset::DataSize);
                            if (dataOffset64 != cursor || dataSize64 != rect.DataBytes ||
                                rect.DataBytes == 0 || rect.DataBytes > StandardTileBytes ||
                                ReadLe32(data, recordOffset + TileRecordOffset::MipIndex) != mipIndex ||
                                ReadLe32(data, recordOffset + TileRecordOffset::LayerIndex) != layerIndex ||
                                ReadLe32(data, recordOffset + TileRecordOffset::TileX) != tileX ||
                                ReadLe32(data, recordOffset + TileRecordOffset::TileY) != tileY)
                            {
                                return CookedTextureParseStatus::TileRecordMismatch;
                            }

                            CookedTextureTile tile;
                            tile.MipIndex = mipIndex;
                            tile.LayerIndex = layerIndex;
                            tile.TileX = tileX;
                            tile.TileY = tileY;
                            tile.DataOffset = dataOffset64;
                            tile.DataSize = static_cast<uint32_t>(dataSize64);
                            outTiling.Tiles.push_back(tile);

                            cursor += dataSize64;
                            ++recordIndex;
                        }
                    }
                }
            }
            outTiling.MipFirstTile.push_back(static_cast<uint32_t>(recordIndex));

            // タイルの区間の終わりがミップテイルの先頭で、テイルはペイロードの終わりまで続く。
            uint64_t payloadEnd64 = 0;
            if (!AddChecked64(payloadOffset64, payloadSize64, payloadEnd64))
            {
                return CookedTextureParseStatus::IntegerOverflow;
            }

            if (tailOffset64 != cursor || static_cast<uint64_t>(mips[firstTailMip].DataOffset) != cursor ||
                cursor > payloadEnd64 || tailSize64 != payloadEnd64 - cursor || tailSize64 == 0)
            {
                return CookedTextureParseStatus::TailRangeMismatch;
            }

            outTiling.TailOffset = tailOffset64;
            outTiling.TailSize = tailSize64;
            return CookedTextureParseStatus::Success;
        }

        // 解析の本体。bytes は全体（bHavePayload）、またはメタデータだけ。fileSize64 は .nvtex 全体のバイト数。
        CookedTextureParseResult ParseInternal(Container::Span<const uint8_t> bytes,
                                               uint64_t fileSize64,
                                               bool bHavePayload,
                                               bool bMaterializeRowMajor)
        {
            using namespace CookedTextureFormatV0;

            if (bytes.empty())
            {
                return Fail(CookedTextureParseStatus::EmptyBlob);
            }

            if (bytes.size() < HeaderSize)
            {
                return Fail(CookedTextureParseStatus::HeaderTooSmall);
            }

            if (!HasExactMagic(bytes))
            {
                return Fail(CookedTextureParseStatus::BadMagic);
            }

            const uint8_t *data = bytes.data();
            const uint32_t headerSize = ReadLe32(data, HeaderOffset::HeaderSize);
            const uint16_t versionMajor = ReadLe16(data, HeaderOffset::VersionMajor);
            const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
            const uint32_t endianMarker = ReadLe32(data, HeaderOffset::EndianMarker);
            const uint32_t mipRecordSize = ReadLe32(data, HeaderOffset::MipRecordSize);
            const uint64_t declaredFileSize = ReadLe64(data, HeaderOffset::FileSize);
            const uint64_t mipTableOffset64 = ReadLe64(data, HeaderOffset::MipTableOffset);
            const uint64_t mipTableSize64 = ReadLe64(data, HeaderOffset::MipTableSize);
            const uint64_t payloadOffset64 = ReadLe64(data, HeaderOffset::PayloadOffset);
            const uint64_t payloadSize64 = ReadLe64(data, HeaderOffset::PayloadSize);
            const uint64_t payloadHash = ReadLe64(data, HeaderOffset::PayloadHash);
            const uint32_t width = ReadLe32(data, HeaderOffset::Width);
            const uint32_t height = ReadLe32(data, HeaderOffset::Height);
            const uint32_t layerCount = ReadLe32(data, HeaderOffset::LayerCount);
            const uint32_t mipCount = ReadLe32(data, HeaderOffset::MipCount);
            const uint32_t rawPixelFormat = ReadLe32(data, HeaderOffset::PixelFormat);
            const uint32_t rawColorSpace = ReadLe32(data, HeaderOffset::ColorSpace);
            const uint32_t flags = ReadLe32(data, HeaderOffset::Flags);
            const uint32_t reserved0 = ReadLe32(data, HeaderOffset::Reserved0);
            const uint64_t reserved1 = ReadLe64(data, HeaderOffset::Reserved1);

            if (!IsSupportedVersion(versionMajor, versionMinor))
            {
                return Fail(CookedTextureParseStatus::UnsupportedVersion);
            }

            if (endianMarker != EndianMarker)
            {
                return Fail(CookedTextureParseStatus::EndianMismatch);
            }

            const size_t expectedHeaderSize = GetHeaderSizeForVersion(versionMinor);
            if (headerSize != expectedHeaderSize)
            {
                return Fail(CookedTextureParseStatus::HeaderSizeMismatch);
            }

            if (bytes.size() < expectedHeaderSize)
            {
                return Fail(CookedTextureParseStatus::HeaderTooSmall);
            }

            if (mipRecordSize != MipRecordSize)
            {
                return Fail(CookedTextureParseStatus::MipRecordSizeMismatch);
            }

            if (declaredFileSize != fileSize64 ||
                (bHavePayload && declaredFileSize != static_cast<uint64_t>(bytes.size())))
            {
                return Fail(CookedTextureParseStatus::FileSizeMismatch);
            }

            if (fileSize64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return Fail(CookedTextureParseStatus::IntegerOverflow);
            }
            const size_t fileSize = static_cast<size_t>(fileSize64);

            if (flags != 0 || reserved0 != 0 || reserved1 != 0)
            {
                return Fail(CookedTextureParseStatus::ReservedFieldNonZero);
            }

            if (width == 0 || height == 0 || layerCount == 0)
            {
                return Fail(CookedTextureParseStatus::InvalidDimensions);
            }

            if (mipCount == 0 || mipCount != ComputeCookedTextureFullMipCount(width, height))
            {
                return Fail(CookedTextureParseStatus::InvalidMipCount);
            }

            if (payloadSize64 == 0)
            {
                return Fail(CookedTextureParseStatus::InvalidPayloadSize);
            }

            if (!IsKnownPixelFormat(versionMinor, rawPixelFormat))
            {
                return Fail(CookedTextureParseStatus::UnknownPixelFormat);
            }

            if (!IsKnownColorSpace(rawColorSpace))
            {
                return Fail(CookedTextureParseStatus::UnknownColorSpace);
            }

            const CookedTexturePixelFormat pixelFormat = static_cast<CookedTexturePixelFormat>(rawPixelFormat);
            const CookedTextureColorSpace colorSpace = static_cast<CookedTextureColorSpace>(rawColorSpace);
            if (!IsValidColorSpaceForFormat(pixelFormat, colorSpace))
            {
                return Fail(CookedTextureParseStatus::InvalidColorSpaceForFormat);
            }

            uint64_t expectedMipTableSize64 = 0;
            if (!MultiplyChecked64(static_cast<uint64_t>(mipCount), static_cast<uint64_t>(MipRecordSize), expectedMipTableSize64))
            {
                return Fail(CookedTextureParseStatus::IntegerOverflow);
            }

            if (mipTableSize64 != expectedMipTableSize64)
            {
                return Fail(CookedTextureParseStatus::MipTableSizeMismatch);
            }

            size_t mipTableOffset = 0;
            size_t mipTableSize = 0;
            if (!ConvertRange(mipTableOffset64, mipTableSize64, fileSize, mipTableOffset, mipTableSize) ||
                mipTableOffset < expectedHeaderSize ||
                (versionMinor == VersionMinorTiled && mipTableOffset != expectedHeaderSize))
            {
                return Fail(CookedTextureParseStatus::MipTableOutOfRange);
            }

            size_t mipTableEnd = 0;
            if (!AddChecked(mipTableOffset, mipTableSize, mipTableEnd))
            {
                return Fail(CookedTextureParseStatus::IntegerOverflow);
            }

            if (mipTableEnd > bytes.size())
            {
                return Fail(CookedTextureParseStatus::MetadataTooSmall);
            }

            size_t payloadOffset = 0;
            size_t payloadSize = 0;
            if (!ConvertRange(payloadOffset64, payloadSize64, fileSize, payloadOffset, payloadSize))
            {
                return Fail(CookedTextureParseStatus::TruncatedPayload);
            }

            if (payloadOffset < mipTableEnd)
            {
                return Fail(CookedTextureParseStatus::MipTableOutOfRange);
            }

            size_t payloadEnd = 0;
            if (!AddChecked(payloadOffset, payloadSize, payloadEnd))
            {
                return Fail(CookedTextureParseStatus::IntegerOverflow);
            }

            Container::VariableArray<CookedTextureMip> mips;
            mips.reserve(mipCount);

            size_t expectedPayloadCursor = payloadOffset;
            size_t rowMajorCursor = 0;
            for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
            {
                const size_t recordOffset = mipTableOffset + static_cast<size_t>(mipIndex) * MipRecordSize;
                const uint64_t dataOffset64 = ReadLe64(data, recordOffset + MipRecordOffset::DataOffset);
                const uint64_t dataSize64 = ReadLe64(data, recordOffset + MipRecordOffset::DataSize);
                const uint32_t mipWidth = ReadLe32(data, recordOffset + MipRecordOffset::Width);
                const uint32_t mipHeight = ReadLe32(data, recordOffset + MipRecordOffset::Height);
                const uint32_t mipReserved0 = ReadLe32(data, recordOffset + MipRecordOffset::Reserved0);
                const uint32_t mipReserved1 = ReadLe32(data, recordOffset + MipRecordOffset::Reserved1);

                if (mipReserved0 != 0 || mipReserved1 != 0)
                {
                    return Fail(CookedTextureParseStatus::ReservedFieldNonZero);
                }

                const uint32_t expectedMipWidth = width >> mipIndex;
                const uint32_t expectedMipHeight = height >> mipIndex;
                const uint32_t clampedExpectedWidth = expectedMipWidth == 0 ? 1 : expectedMipWidth;
                const uint32_t clampedExpectedHeight = expectedMipHeight == 0 ? 1 : expectedMipHeight;
                if (mipWidth != clampedExpectedWidth || mipHeight != clampedExpectedHeight)
                {
                    return Fail(CookedTextureParseStatus::MipDimensionsMismatch);
                }

                uint64_t expectedDataSize64 = 0;
                if (!ComputeExpectedMipPayloadSize(pixelFormat, mipWidth, mipHeight, layerCount, expectedDataSize64))
                {
                    return Fail(CookedTextureParseStatus::IntegerOverflow);
                }

                if (dataSize64 != expectedDataSize64)
                {
                    return Fail(CookedTextureParseStatus::MipDataSizeMismatch);
                }

                if (dataOffset64 < payloadOffset64)
                {
                    return Fail(CookedTextureParseStatus::MipOffsetBeforePayload);
                }

                size_t dataOffset = 0;
                size_t dataSize = 0;
                if (!ConvertRange(dataOffset64, dataSize64, fileSize, dataOffset, dataSize))
                {
                    return Fail(CookedTextureParseStatus::TruncatedPayload);
                }

                if (dataOffset != expectedPayloadCursor)
                {
                    return Fail(CookedTextureParseStatus::MipPackingMismatch);
                }

                if (dataSize > payloadEnd - expectedPayloadCursor)
                {
                    return Fail(CookedTextureParseStatus::TruncatedPayload);
                }

                CookedTextureMip mip;
                mip.DataOffset = dataOffset;
                mip.DataSize = dataSize;
                mip.Width = mipWidth;
                mip.Height = mipHeight;
                mip.RowMajorOffset = rowMajorCursor;
                mips.push_back(mip);

                expectedPayloadCursor += dataSize;
                rowMajorCursor += dataSize;
            }

            if (expectedPayloadCursor != payloadEnd)
            {
                return Fail(CookedTextureParseStatus::MipPackingMismatch);
            }

            CookedTextureTiling tiling;
            const bool bTiled = versionMinor == VersionMinorTiled;
            if (bTiled)
            {
                const CookedTextureParseStatus tilingStatus = ParseTiling(data,
                                                                          bytes.size(),
                                                                          fileSize,
                                                                          pixelFormat,
                                                                          width,
                                                                          height,
                                                                          layerCount,
                                                                          mipCount,
                                                                          mipTableEnd,
                                                                          payloadOffset64,
                                                                          payloadSize64,
                                                                          mips,
                                                                          tiling);
                if (tilingStatus != CookedTextureParseStatus::Success)
                {
                    return Fail(tilingStatus);
                }
            }

            if (bHavePayload)
            {
                const uint64_t computedPayloadHash = ComputeCookedTexturePayloadHash(data + payloadOffset, payloadSize);
                if (computedPayloadHash != payloadHash)
                {
                    return Fail(CookedTextureParseStatus::PayloadHashMismatch);
                }
            }

            // v0.2 は各段をタイルから行優先へ戻して、v0.0 と同じ GetMipBytes で読めるようにする。
            Container::TSharedPtr<AssetBlob::ByteArray> rowMajorStorage;
            if (bTiled && bHavePayload && bMaterializeRowMajor)
            {
                rowMajorStorage = Container::MakeShared<AssetBlob::ByteArray>();
                rowMajorStorage->resize(rowMajorCursor);
                for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
                {
                    const CookedTextureMip &mip = mips[mipIndex];
                    uint8_t *destination = rowMajorStorage->data() + mip.RowMajorOffset;
                    if (mipIndex >= tiling.FirstTailMip)
                    {
                        std::memcpy(destination, data + mip.DataOffset, mip.DataSize);
                        continue;
                    }

                    uint64_t layerRowBytes = 0;
                    uint64_t layerRowCount = 0;
                    if (!ComputeCookedTextureMipLayout(pixelFormat, mip.Width, mip.Height, layerRowBytes, layerRowCount))
                    {
                        return Fail(CookedTextureParseStatus::IntegerOverflow);
                    }
                    const uint64_t layerBytes = layerRowBytes * layerRowCount;

                    for (uint32_t tileIndex = tiling.MipFirstTile[mipIndex]; tileIndex < tiling.MipFirstTile[mipIndex + 1]; ++tileIndex)
                    {
                        const CookedTextureTile &tile = tiling.Tiles[tileIndex];
                        CookedTextureTileRect rect;
                        if (!ComputeCookedTextureTileRect(pixelFormat, mip.Width, mip.Height, tile.TileX, tile.TileY, rect))
                        {
                            return Fail(CookedTextureParseStatus::TileRecordMismatch);
                        }

                        ScatterCookedTextureTile(pixelFormat,
                                                 mip.Width,
                                                 rect,
                                                 data + tile.DataOffset,
                                                 destination + static_cast<uint64_t>(tile.LayerIndex) * layerBytes);
                    }
                }
            }

            CookedTextureParseResult result;
            result.Status = CookedTextureParseStatus::Success;
            result.Texture.Width = width;
            result.Texture.Height = height;
            result.Texture.LayerCount = layerCount;
            result.Texture.MipCount = mipCount;
            result.Texture.PixelFormat = pixelFormat;
            result.Texture.ColorSpace = colorSpace;
            result.Texture.PayloadHash = payloadHash;
            result.Texture.Mips = std::move(mips);
            result.Texture.VersionMinor = versionMinor;
            result.Texture.bTiled = bTiled;
            result.Texture.Tiling = std::move(tiling);
            result.Texture.RowMajorStorage = std::move(rowMajorStorage);
            return result;
        }
    }

    CookedTextureParseResult ParseCookedTexture(AssetBlob sourceBlob, bool bMaterializeRowMajor)
    {
        if (!sourceBlob.IsValid())
        {
            return Fail(CookedTextureParseStatus::InvalidBlob);
        }

        const Container::Span<const uint8_t> bytes = sourceBlob.GetSpan();
        CookedTextureParseResult result = ParseInternal(bytes, static_cast<uint64_t>(bytes.size()), true, bMaterializeRowMajor);
        if (result.Succeeded())
        {
            result.Texture.SourceBlob = std::move(sourceBlob);
        }
        return result;
    }

    CookedTextureParseStatus GetCookedTextureMetadataSize(Container::Span<const uint8_t> headBytes,
                                                          uint64_t fileSize,
                                                          uint64_t &outMetadataSize) noexcept
    {
        using namespace CookedTextureFormatV0;

        if (headBytes.size() < HeaderSize)
        {
            return CookedTextureParseStatus::HeaderTooSmall;
        }

        if (!HasExactMagic(headBytes))
        {
            return CookedTextureParseStatus::BadMagic;
        }

        const uint8_t *data = headBytes.data();
        const uint16_t versionMajor = ReadLe16(data, HeaderOffset::VersionMajor);
        const uint16_t versionMinor = ReadLe16(data, HeaderOffset::VersionMinor);
        if (!IsSupportedVersion(versionMajor, versionMinor))
        {
            return CookedTextureParseStatus::UnsupportedVersion;
        }

        if (ReadLe32(data, HeaderOffset::EndianMarker) != EndianMarker)
        {
            return CookedTextureParseStatus::EndianMismatch;
        }

        if (ReadLe32(data, HeaderOffset::HeaderSize) != GetHeaderSizeForVersion(versionMinor))
        {
            return CookedTextureParseStatus::HeaderSizeMismatch;
        }

        if (ReadLe64(data, HeaderOffset::FileSize) != fileSize)
        {
            return CookedTextureParseStatus::FileSizeMismatch;
        }

        const uint64_t payloadOffset = ReadLe64(data, HeaderOffset::PayloadOffset);
        if (payloadOffset < GetHeaderSizeForVersion(versionMinor) || payloadOffset > fileSize ||
            payloadOffset > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        {
            return CookedTextureParseStatus::MipTableOutOfRange;
        }

        outMetadataSize = payloadOffset;
        return CookedTextureParseStatus::Success;
    }

    CookedTextureParseResult ParseCookedTextureLayout(Container::Span<const uint8_t> metadataBytes, uint64_t fileSize)
    {
        return ParseInternal(metadataBytes, fileSize, false, false);
    }

    CookedTextureParseResult ReadCookedTextureLayout(const AssetFileReader &reader,
                                                     const AssetReadRequest &request,
                                                     uint64_t baseOffset,
                                                     uint64_t nvtexSize)
    {
        const AssetReadResult head = reader.ReadRange(request, baseOffset, CookedTextureFormatV0::HeaderSize);
        if (!head.Succeeded())
        {
            // ファイルの外を読もうとした = .nvtex がヘッダより短い。
            if (head.FileSize >= 0 && static_cast<uint64_t>(head.FileSize) < baseOffset + CookedTextureFormatV0::HeaderSize)
            {
                return Fail(CookedTextureParseStatus::HeaderTooSmall);
            }
            return Fail(CookedTextureParseStatus::ReadFailed);
        }

        const uint64_t available = static_cast<uint64_t>(head.FileSize) - baseOffset;
        if (nvtexSize == 0)
        {
            nvtexSize = available;
        }
        else if (nvtexSize > available)
        {
            return Fail(CookedTextureParseStatus::ReadFailed);
        }

        uint64_t metadataSize = 0;
        const CookedTextureParseStatus sizeStatus =
            GetCookedTextureMetadataSize(head.Blob.GetSpan(), nvtexSize, metadataSize);
        if (sizeStatus != CookedTextureParseStatus::Success)
        {
            return Fail(sizeStatus);
        }

        const AssetReadResult metadata = reader.ReadRange(request, baseOffset, static_cast<size_t>(metadataSize));
        if (!metadata.Succeeded())
        {
            return Fail(CookedTextureParseStatus::ReadFailed);
        }

        return ParseCookedTextureLayout(metadata.Blob.GetSpan(), nvtexSize);
    }

    namespace
    {
        AssetReadResult MakeInvalidRange(const char *reason)
        {
            AssetReadResult result;
            result.Status = AssetReadStatus::InvalidRequest;
            result.Reason = reason;
            return result;
        }

        AssetReadResult ReadNvtexRange(const AssetFileReader &reader,
                                       const AssetReadRequest &request,
                                       uint64_t baseOffset,
                                       uint64_t offset,
                                       uint64_t size)
        {
            uint64_t fileOffset = 0;
            if (!AddChecked64(baseOffset, offset, fileOffset) || size == 0 ||
                size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
            {
                return MakeInvalidRange("範囲が不正である");
            }

            return reader.ReadRange(request, fileOffset, static_cast<size_t>(size));
        }
    }

    AssetReadResult ReadCookedTextureTile(const AssetFileReader &reader,
                                          const AssetReadRequest &request,
                                          uint64_t baseOffset,
                                          const CookedTextureData &layout,
                                          uint32_t mipIndex,
                                          uint32_t layerIndex,
                                          uint32_t tileX,
                                          uint32_t tileY)
    {
        CookedTextureTile tile;
        if (!layout.FindTile(mipIndex, layerIndex, tileX, tileY, tile))
        {
            return MakeInvalidRange("タイルがタイルの表に無い");
        }

        return ReadNvtexRange(reader, request, baseOffset, tile.DataOffset, tile.DataSize);
    }

    AssetReadResult ReadCookedTextureMipTail(const AssetFileReader &reader,
                                             const AssetReadRequest &request,
                                             uint64_t baseOffset,
                                             const CookedTextureData &layout)
    {
        if (!layout.bTiled)
        {
            return MakeInvalidRange("テクスチャにミップテイルの塊が無い");
        }

        return ReadNvtexRange(reader, request, baseOffset, layout.Tiling.TailOffset, layout.Tiling.TailSize);
    }
}
