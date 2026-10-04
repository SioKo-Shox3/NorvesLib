#include "Asset/CookedTextureFormat.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Asset;
using namespace NorvesLib::Core::Asset::CookedTextureFormatV0;
using NorvesLib::Core::Container::Span;

namespace
{
    static_assert(HeaderOffset::Reserved1 + sizeof(uint64_t) == HeaderSize);
    static_assert(MipRecordOffset::Reserved1 + sizeof(uint32_t) == MipRecordSize);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::R8UNorm) == PixelFormatR8UNorm);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::RG8UNorm) == PixelFormatRG8UNorm);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::RGBA8UNorm) == PixelFormatRGBA8UNorm);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC1) == PixelFormatBC1);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC4) == PixelFormatBC4);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC5) == PixelFormatBC5);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC7) == PixelFormatBC7);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::R16UNorm) == PixelFormatR16UNorm);
    static_assert(VersionMinor == 0 && VersionMinorBlockCompressed == 1 && VersionMinorTiled == 2);
    static_assert(TiledHeaderOffset::TailSize + sizeof(uint64_t) == HeaderSizeTiled);
    static_assert(TiledHeaderOffset::TileWidth == HeaderSize);
    static_assert(TileRecordOffset::TileY + sizeof(uint32_t) == TileRecordSize);
    static_assert(StandardTileBytes == 65536);

    // 標準ブロック形状は、どの形式でも 64 KiB（タイルの幅x高さをブロックで数えてブロックのバイト数を掛ける）になり、
    // Vulkan の標準 sparse イメージブロックの表と一致する。
    constexpr bool TileShapeIs(CookedTexturePixelFormat format, uint32_t width, uint32_t height)
    {
        const CookedTextureTileShape shape = GetCookedTextureStandardTileShape(format);
        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(format);
        return shape.Width == width && shape.Height == height &&
               (shape.Width / block.BlockWidth) * (shape.Height / block.BlockHeight) * block.BlockBytes == StandardTileBytes;
    }
    static_assert(TileShapeIs(CookedTexturePixelFormat::R8UNorm, 256, 256));
    static_assert(TileShapeIs(CookedTexturePixelFormat::RG8UNorm, 256, 128));
    static_assert(TileShapeIs(CookedTexturePixelFormat::R16UNorm, 256, 128));
    static_assert(TileShapeIs(CookedTexturePixelFormat::RGBA8UNorm, 128, 128));
    static_assert(TileShapeIs(CookedTexturePixelFormat::BC1, 512, 256));
    static_assert(TileShapeIs(CookedTexturePixelFormat::BC4, 512, 256));
    static_assert(TileShapeIs(CookedTexturePixelFormat::BC5, 256, 256));
    static_assert(TileShapeIs(CookedTexturePixelFormat::BC7, 256, 256));

    // ミップテイルは、段の幅か高さがタイルより小さい最初の段から。
    static_assert(ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat::BC7, 1024, 512) == 2);
    static_assert(ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat::BC7, 256, 256) == 1);
    static_assert(ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat::BC7, 64, 64) == 0);
    static_assert(ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat::R8UNorm, 1024, 100) == 0);
    static_assert(ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat::BC4, 1100, 600) == 2);
    static_assert(static_cast<uint32_t>(CookedTextureColorSpace::Linear) == ColorSpaceLinear);
    static_assert(static_cast<uint32_t>(CookedTextureColorSpace::SRGB) == ColorSpaceSRGB);

    void WriteLe16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    uint16_t ReadLe16(const std::vector<uint8_t> &bytes, size_t offset)
    {
        return static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset]) |
                                     static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8));
    }

    uint32_t ReadLe32(const std::vector<uint8_t> &bytes, size_t offset)
    {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    uint64_t ReadLe64(const std::vector<uint8_t> &bytes, size_t offset)
    {
        return static_cast<uint64_t>(ReadLe32(bytes, offset)) |
               (static_cast<uint64_t>(ReadLe32(bytes, offset + 4)) << 32);
    }

    AssetBlob MakeBlob(const std::vector<uint8_t> &bytes)
    {
        return AssetBlob::CopyBytes(Span<const uint8_t>(bytes.data(), bytes.size()), "memory.nvtex");
    }

    size_t MipRecordOffsetFor(size_t mipIndex)
    {
        return HeaderSize + mipIndex * MipRecordSize;
    }

    uint32_t ExpectedMipDimension(uint32_t baseDimension, uint32_t mipIndex)
    {
        const uint32_t shifted = baseDimension >> mipIndex;
        return shifted == 0 ? 1 : shifted;
    }

    // 形式ごとの 1 ブロックの大きさ。テスト側で独立に持ち、実装の計算を写さない。
    struct TestBlock
    {
        uint32_t Width;
        uint32_t Height;
        uint32_t Bytes;
    };

    TestBlock TestBlockOf(CookedTexturePixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::R8UNorm:
            return {1, 1, 1};
        case CookedTexturePixelFormat::RG8UNorm:
        case CookedTexturePixelFormat::R16UNorm:
            return {1, 1, 2};
        case CookedTexturePixelFormat::RGBA8UNorm:
            return {1, 1, 4};
        case CookedTexturePixelFormat::BC1:
        case CookedTexturePixelFormat::BC4:
            return {4, 4, 8};
        case CookedTexturePixelFormat::BC5:
        case CookedTexturePixelFormat::BC7:
            return {4, 4, 16};
        }
        return {1, 1, 0};
    }

    // versionMinor を省略すると v0.0 で書く。BC と R16 は v0.1 を明示して書く。
    std::vector<uint8_t> BuildTexture(uint32_t width,
                                      uint32_t height,
                                      uint32_t layerCount,
                                      CookedTexturePixelFormat pixelFormat,
                                      CookedTextureColorSpace colorSpace,
                                      uint16_t versionMinor = VersionMinor)
    {
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const TestBlock block = TestBlockOf(pixelFormat);
        const size_t mipTableOffset = HeaderSize;
        const size_t mipTableSize = static_cast<size_t>(mipCount) * MipRecordSize;
        const size_t payloadOffset = mipTableOffset + mipTableSize;

        std::vector<std::vector<uint8_t>> mipPayloads;
        mipPayloads.reserve(mipCount);

        size_t payloadSize = 0;
        uint8_t nextValue = 0;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);
            const size_t blocksX = (mipWidth + block.Width - 1) / block.Width;
            const size_t blocksY = (mipHeight + block.Height - 1) / block.Height;
            const size_t dataSize = blocksX * blocksY * static_cast<size_t>(layerCount) * block.Bytes;
            std::vector<uint8_t> mipBytes(dataSize);
            for (uint8_t &value : mipBytes)
            {
                value = nextValue++;
            }
            payloadSize += dataSize;
            mipPayloads.push_back(std::move(mipBytes));
        }

        const size_t fileSize = payloadOffset + payloadSize;
        std::vector<uint8_t> bytes(fileSize, 0);
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, versionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(MipRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(mipTableOffset));
        WriteLe64(bytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
        WriteLe64(bytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
        WriteLe64(bytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
        WriteLe32(bytes, HeaderOffset::Width, width);
        WriteLe32(bytes, HeaderOffset::Height, height);
        WriteLe32(bytes, HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes, HeaderOffset::PixelFormat, static_cast<uint32_t>(pixelFormat));
        WriteLe32(bytes, HeaderOffset::ColorSpace, static_cast<uint32_t>(colorSpace));
        WriteLe32(bytes, HeaderOffset::Flags, 0);
        WriteLe32(bytes, HeaderOffset::Reserved0, 0);
        WriteLe64(bytes, HeaderOffset::Reserved1, 0);

        size_t payloadCursor = payloadOffset;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const std::vector<uint8_t> &mipBytes = mipPayloads[mipIndex];
            const size_t recordOffset = MipRecordOffsetFor(mipIndex);
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);

            WriteLe64(bytes, recordOffset + MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadCursor));
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataSize, static_cast<uint64_t>(mipBytes.size()));
            WriteLe32(bytes, recordOffset + MipRecordOffset::Width, mipWidth);
            WriteLe32(bytes, recordOffset + MipRecordOffset::Height, mipHeight);
            WriteLe32(bytes, recordOffset + MipRecordOffset::Reserved0, 0);
            WriteLe32(bytes, recordOffset + MipRecordOffset::Reserved1, 0);

            std::memcpy(bytes.data() + payloadCursor, mipBytes.data(), mipBytes.size());
            payloadCursor += mipBytes.size();
        }

        WriteLe64(bytes,
                  HeaderOffset::PayloadHash,
                  ComputeCookedTexturePayloadHash(bytes.data() + payloadOffset, payloadSize));
        return bytes;
    }

    std::vector<uint8_t> BuildOverflowTexture()
    {
        const uint32_t width = std::numeric_limits<uint32_t>::max();
        const uint32_t height = std::numeric_limits<uint32_t>::max();
        const uint32_t layerCount = std::numeric_limits<uint32_t>::max();
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const size_t mipTableOffset = HeaderSize;
        const size_t mipTableSize = static_cast<size_t>(mipCount) * MipRecordSize;
        const size_t payloadOffset = mipTableOffset + mipTableSize;
        const size_t payloadSize = 1;
        const size_t fileSize = payloadOffset + payloadSize;

        std::vector<uint8_t> bytes(fileSize, 0);
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(MipRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(mipTableOffset));
        WriteLe64(bytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
        WriteLe64(bytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(payloadOffset));
        WriteLe64(bytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payloadSize));
        WriteLe64(bytes, HeaderOffset::PayloadHash, ComputeCookedTexturePayloadHash(bytes.data() + payloadOffset, payloadSize));
        WriteLe32(bytes, HeaderOffset::Width, width);
        WriteLe32(bytes, HeaderOffset::Height, height);
        WriteLe32(bytes, HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes, HeaderOffset::PixelFormat, PixelFormatRGBA8UNorm);
        WriteLe32(bytes, HeaderOffset::ColorSpace, ColorSpaceLinear);

        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const size_t recordOffset = MipRecordOffsetFor(mipIndex);
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataOffset, static_cast<uint64_t>(payloadOffset));
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataSize, payloadSize);
            WriteLe32(bytes, recordOffset + MipRecordOffset::Width, ExpectedMipDimension(width, mipIndex));
            WriteLe32(bytes, recordOffset + MipRecordOffset::Height, ExpectedMipDimension(height, mipIndex));
        }

        return bytes;
    }

    void ExpectStatus(std::vector<uint8_t> bytes, CookedTextureParseStatus expectedStatus)
    {
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(bytes));
        assert(result.Status == expectedStatus);
        assert(!result.Succeeded());
    }

    void AssertSpanBytes(Span<const uint8_t> bytes, size_t startValue)
    {
        for (size_t index = 0; index < bytes.size(); ++index)
        {
            assert(bytes[index] == static_cast<uint8_t>((startValue + index) & 0xffu));
        }
    }

    // ---- v0.2（タイル配置）----
    // ビルダーは仕様の表（Vulkan の標準 sparse イメージブロック）から独立に書き、実装の計算を写さない。

    struct TestTile
    {
        uint32_t Width;
        uint32_t Height;
    };

    TestTile TestTileOf(CookedTexturePixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::R8UNorm:
            return {256, 256};
        case CookedTexturePixelFormat::RG8UNorm:
        case CookedTexturePixelFormat::R16UNorm:
            return {256, 128};
        case CookedTexturePixelFormat::RGBA8UNorm:
            return {128, 128};
        case CookedTexturePixelFormat::BC1:
        case CookedTexturePixelFormat::BC4:
            return {512, 256};
        case CookedTexturePixelFormat::BC5:
        case CookedTexturePixelFormat::BC7:
            return {256, 256};
        }
        return {0, 0};
    }

    struct TileExpect
    {
        uint32_t Mip = 0;
        uint32_t Layer = 0;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint64_t Offset = 0;
        std::vector<uint8_t> Data;
    };

    struct BuiltTiled
    {
        std::vector<uint8_t> Bytes;
        std::vector<std::vector<uint8_t>> RowMajorMips;  // 段ごと（全レイヤー）の行優先
        std::vector<TileExpect> Tiles;
        uint32_t FirstTailMip = 0;
        uint64_t TailOffset = 0;
        uint64_t TailSize = 0;
        size_t TileTableOffset = 0;
        size_t MetadataSize = 0;
    };

    BuiltTiled BuildTiledTexture(uint32_t width,
                                 uint32_t height,
                                 uint32_t layerCount,
                                 CookedTexturePixelFormat pixelFormat,
                                 CookedTextureColorSpace colorSpace)
    {
        BuiltTiled built;
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const TestBlock block = TestBlockOf(pixelFormat);
        const TestTile tile = TestTileOf(pixelFormat);

        // 行優先の段。値はバイトごとに違うハッシュで、タイルの取り違えを検出できる。
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);
            const size_t blocksX = (mipWidth + block.Width - 1) / block.Width;
            const size_t blocksY = (mipHeight + block.Height - 1) / block.Height;
            std::vector<uint8_t> mipBytes(blocksX * blocksY * layerCount * block.Bytes);
            for (size_t index = 0; index < mipBytes.size(); ++index)
            {
                const uint32_t hash = static_cast<uint32_t>(index) * 2654435761u + mipIndex * 40503u;
                mipBytes[index] = static_cast<uint8_t>((hash >> 13) & 0xffu);
            }
            built.RowMajorMips.push_back(std::move(mipBytes));
        }

        built.FirstTailMip = mipCount;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            if (ExpectedMipDimension(width, mipIndex) < tile.Width || ExpectedMipDimension(height, mipIndex) < tile.Height)
            {
                built.FirstTailMip = mipIndex;
                break;
            }
        }

        size_t tileCount = 0;
        for (uint32_t mipIndex = 0; mipIndex < built.FirstTailMip; ++mipIndex)
        {
            const size_t tilesX = (ExpectedMipDimension(width, mipIndex) + tile.Width - 1) / tile.Width;
            const size_t tilesY = (ExpectedMipDimension(height, mipIndex) + tile.Height - 1) / tile.Height;
            tileCount += tilesX * tilesY * layerCount;
        }

        const size_t mipTableSize = static_cast<size_t>(mipCount) * MipRecordSize;
        built.TileTableOffset = HeaderSizeTiled + mipTableSize;
        built.MetadataSize = built.TileTableOffset + tileCount * TileRecordSize;

        // ペイロード: タイルの段はタイルを表の順に、テイルの段は行優先のまま。
        std::vector<uint8_t> payload;
        std::vector<uint64_t> mipOffsets;
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            mipOffsets.push_back(built.MetadataSize + payload.size());
            const std::vector<uint8_t> &mipBytes = built.RowMajorMips[mipIndex];
            if (mipIndex >= built.FirstTailMip)
            {
                if (mipIndex == built.FirstTailMip)
                {
                    built.TailOffset = built.MetadataSize + payload.size();
                }
                payload.insert(payload.end(), mipBytes.begin(), mipBytes.end());
                continue;
            }

            const uint32_t mipWidth = ExpectedMipDimension(width, mipIndex);
            const uint32_t mipHeight = ExpectedMipDimension(height, mipIndex);
            const size_t blocksX = (mipWidth + block.Width - 1) / block.Width;
            const size_t blocksY = (mipHeight + block.Height - 1) / block.Height;
            const size_t tilesX = (mipWidth + tile.Width - 1) / tile.Width;
            const size_t tilesY = (mipHeight + tile.Height - 1) / tile.Height;
            const size_t tileBlocksX = tile.Width / block.Width;
            const size_t tileBlocksY = tile.Height / block.Height;
            const size_t layerBytes = blocksX * blocksY * block.Bytes;
            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                for (size_t tileY = 0; tileY < tilesY; ++tileY)
                {
                    for (size_t tileX = 0; tileX < tilesX; ++tileX)
                    {
                        const size_t x0 = tileX * tileBlocksX;
                        const size_t y0 = tileY * tileBlocksY;
                        const size_t w = std::min(tileBlocksX, blocksX - x0);
                        const size_t h = std::min(tileBlocksY, blocksY - y0);

                        TileExpect expect;
                        expect.Mip = mipIndex;
                        expect.Layer = layer;
                        expect.X = static_cast<uint32_t>(tileX);
                        expect.Y = static_cast<uint32_t>(tileY);
                        expect.Offset = built.MetadataSize + payload.size();
                        for (size_t row = 0; row < h; ++row)
                        {
                            const size_t source = layer * layerBytes + ((y0 + row) * blocksX + x0) * block.Bytes;
                            expect.Data.insert(expect.Data.end(),
                                               mipBytes.begin() + source,
                                               mipBytes.begin() + source + w * block.Bytes);
                        }
                        payload.insert(payload.end(), expect.Data.begin(), expect.Data.end());
                        built.Tiles.push_back(std::move(expect));
                    }
                }
            }
        }
        built.TailSize = built.MetadataSize + payload.size() - built.TailOffset;

        const size_t fileSize = built.MetadataSize + payload.size();
        built.Bytes.assign(fileSize, 0);
        std::vector<uint8_t> &bytes = built.Bytes;
        std::memcpy(bytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSizeTiled));
        WriteLe16(bytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinorTiled);
        WriteLe32(bytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(bytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(MipRecordSize));
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(fileSize));
        WriteLe64(bytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(HeaderSizeTiled));
        WriteLe64(bytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(mipTableSize));
        WriteLe64(bytes, HeaderOffset::PayloadOffset, static_cast<uint64_t>(built.MetadataSize));
        WriteLe64(bytes, HeaderOffset::PayloadSize, static_cast<uint64_t>(payload.size()));
        WriteLe32(bytes, HeaderOffset::Width, width);
        WriteLe32(bytes, HeaderOffset::Height, height);
        WriteLe32(bytes, HeaderOffset::LayerCount, layerCount);
        WriteLe32(bytes, HeaderOffset::MipCount, mipCount);
        WriteLe32(bytes, HeaderOffset::PixelFormat, static_cast<uint32_t>(pixelFormat));
        WriteLe32(bytes, HeaderOffset::ColorSpace, static_cast<uint32_t>(colorSpace));
        WriteLe32(bytes, TiledHeaderOffset::TileWidth, tile.Width);
        WriteLe32(bytes, TiledHeaderOffset::TileHeight, tile.Height);
        WriteLe32(bytes, TiledHeaderOffset::FirstTailMip, built.FirstTailMip);
        WriteLe32(bytes, TiledHeaderOffset::TileDataBytes, 65536);
        WriteLe64(bytes, TiledHeaderOffset::TileTableOffset, static_cast<uint64_t>(built.TileTableOffset));
        WriteLe64(bytes, TiledHeaderOffset::TileTableSize, static_cast<uint64_t>(tileCount * TileRecordSize));
        WriteLe64(bytes, TiledHeaderOffset::TailOffset, built.TailOffset);
        WriteLe64(bytes, TiledHeaderOffset::TailSize, built.TailSize);

        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const size_t recordOffset = HeaderSizeTiled + static_cast<size_t>(mipIndex) * MipRecordSize;
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataOffset, mipOffsets[mipIndex]);
            WriteLe64(bytes, recordOffset + MipRecordOffset::DataSize, built.RowMajorMips[mipIndex].size());
            WriteLe32(bytes, recordOffset + MipRecordOffset::Width, ExpectedMipDimension(width, mipIndex));
            WriteLe32(bytes, recordOffset + MipRecordOffset::Height, ExpectedMipDimension(height, mipIndex));
        }

        for (size_t tileIndex = 0; tileIndex < built.Tiles.size(); ++tileIndex)
        {
            const TileExpect &expect = built.Tiles[tileIndex];
            const size_t recordOffset = built.TileTableOffset + tileIndex * TileRecordSize;
            WriteLe64(bytes, recordOffset + TileRecordOffset::DataOffset, expect.Offset);
            WriteLe64(bytes, recordOffset + TileRecordOffset::DataSize, expect.Data.size());
            WriteLe32(bytes, recordOffset + TileRecordOffset::MipIndex, expect.Mip);
            WriteLe32(bytes, recordOffset + TileRecordOffset::LayerIndex, expect.Layer);
            WriteLe32(bytes, recordOffset + TileRecordOffset::TileX, expect.X);
            WriteLe32(bytes, recordOffset + TileRecordOffset::TileY, expect.Y);
        }

        std::memcpy(bytes.data() + built.MetadataSize, payload.data(), payload.size());
        WriteLe64(bytes, HeaderOffset::PayloadHash, ComputeCookedTexturePayloadHash(payload.data(), payload.size()));
        return built;
    }

    bool SameBytes(Span<const uint8_t> actual, const std::vector<uint8_t> &expected)
    {
        return actual.size() == expected.size() &&
               (expected.empty() || std::memcmp(actual.data(), expected.data(), expected.size()) == 0);
    }

    // 全タイル・全段について、表の位置・タイルの中身・行優先への展開が独立に作った期待と一致することを確かめる。
    void ExpectTiledTexture(const BuiltTiled &built, const CookedTextureData &texture)
    {
        assert(texture.VersionMinor == VersionMinorTiled);
        assert(texture.bTiled);
        assert(texture.Tiling.FirstTailMip == built.FirstTailMip);
        assert(texture.Tiling.TailOffset == built.TailOffset);
        assert(texture.Tiling.TailSize == built.TailSize);
        assert(texture.Tiling.Tiles.size() == built.Tiles.size());

        for (const TileExpect &expect : built.Tiles)
        {
            CookedTextureTile tile;
            assert(texture.FindTile(expect.Mip, expect.Layer, expect.X, expect.Y, tile));
            assert(tile.MipIndex == expect.Mip && tile.LayerIndex == expect.Layer);
            assert(tile.TileX == expect.X && tile.TileY == expect.Y);
            assert(tile.DataOffset == expect.Offset);
            assert(tile.DataSize == expect.Data.size());
            assert(tile.DataSize <= StandardTileBytes);
            if (texture.SourceBlob.IsValid())
            {
                const Span<const uint8_t> all = texture.SourceBlob.GetSpan();
                assert(SameBytes(Span<const uint8_t>(all.data() + tile.DataOffset, tile.DataSize), expect.Data));
            }
        }
    }

    std::filesystem::path CreateTileTestRoot()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("NorvesLibCookedTextureTileTest_" + std::to_string(now));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    void WriteFileBytes(const std::filesystem::path &path, const std::vector<uint8_t> &bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assert(stream.good());
    }

    AssetReadRequest MakeRequest(const std::filesystem::path &path)
    {
        AssetReadRequest request;
        request.InputPath = NorvesLib::Core::Container::AnsiString(path.generic_string());
        request.bAllowAbsolutePath = true;
        return request;
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "CookedTextureTest start\n";

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(bytes));
        assert(result.Succeeded());
        assert(result.Status == CookedTextureParseStatus::Success);
        assert(result.Texture.Width == 4);
        assert(result.Texture.Height == 4);
        assert(result.Texture.LayerCount == 1);
        assert(result.Texture.MipCount == 3);
        assert(result.Texture.PixelFormat == CookedTexturePixelFormat::RGBA8UNorm);
        assert(result.Texture.ColorSpace == CookedTextureColorSpace::SRGB);
        assert(result.Texture.Mips.size() == 3);
        assert(result.Texture.Mips[0].DataSize == 64);
        assert(result.Texture.Mips[1].DataSize == 16);
        assert(result.Texture.Mips[2].DataSize == 4);
        assert(result.Texture.GetMipBytes(99).empty());
        AssertSpanBytes(result.Texture.GetMipBytes(0), 0);
        AssertSpanBytes(result.Texture.GetMipBytes(1), 64);
        AssertSpanBytes(result.Texture.GetMipBytes(2), 80);
    }

    {
        const CookedTextureParseResult result =
            ParseCookedTexture(MakeBlob(BuildTexture(5, 3, 1, CookedTexturePixelFormat::R8UNorm, CookedTextureColorSpace::Linear)));
        assert(result.Succeeded());
        assert(result.Texture.MipCount == 3);
        assert(result.Texture.Mips[0].Width == 5);
        assert(result.Texture.Mips[0].Height == 3);
        assert(result.Texture.Mips[0].DataSize == 15);
        assert(result.Texture.Mips[1].Width == 2);
        assert(result.Texture.Mips[1].Height == 1);
        assert(result.Texture.Mips[1].DataSize == 2);
        assert(result.Texture.Mips[2].Width == 1);
        assert(result.Texture.Mips[2].Height == 1);
        assert(result.Texture.Mips[2].DataSize == 1);
    }

    {
        const CookedTextureParseResult result =
            ParseCookedTexture(MakeBlob(BuildTexture(2, 2, 2, CookedTexturePixelFormat::RG8UNorm, CookedTextureColorSpace::Linear)));
        assert(result.Succeeded());
        assert(result.Texture.LayerCount == 2);
        assert(result.Texture.MipCount == 2);
        const Span<const uint8_t> mip0 = result.Texture.GetMipBytes(0);
        const Span<const uint8_t> mip1 = result.Texture.GetMipBytes(1);
        assert(mip0.size() == 16);
        assert(mip1.size() == 4);
        AssertSpanBytes(mip0, 0);
        AssertSpanBytes(mip1, 16);
        assert(mip0[0] == 0);
        assert(mip0[7] == 7);
        assert(mip0[8] == 8);
        assert(mip0[15] == 15);
    }

    {
        CookedTextureParseResult retainedResult;
        {
            AssetBlob sourceBlob = MakeBlob(BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB));
            retainedResult = ParseCookedTexture(sourceBlob);
            sourceBlob = AssetBlob::Invalid();
        }
        assert(retainedResult.Succeeded());
        assert(retainedResult.Texture.GetMipBytes(0).size() == 64);
        assert(retainedResult.Texture.GetMipBytes(0)[0] == 0);
    }

    {
        assert(ParseCookedTexture(AssetBlob::Invalid()).Status == CookedTextureParseStatus::InvalidBlob);

        const std::vector<uint8_t> empty;
        assert(ParseCookedTexture(MakeBlob(empty)).Status == CookedTextureParseStatus::EmptyBlob);

        std::vector<uint8_t> shorterThanHeader(HeaderSize - 1, 0);
        assert(ParseCookedTexture(MakeBlob(shorterThanHeader)).Status == CookedTextureParseStatus::HeaderTooSmall);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        bytes[HeaderOffset::Magic] = 'X';
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::BadMagic);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe16(bytes, HeaderOffset::VersionMajor, 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::UnsupportedVersion);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::EndianMarker, 0x04030201u);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::EndianMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize - 1));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::HeaderSizeMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::MipRecordSize, static_cast<uint32_t>(MipRecordSize - 1));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipRecordSizeMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(bytes.size() + 1));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::FileSizeMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::Reserved0, 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::ReservedFieldNonZero);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, MipRecordOffsetFor(0) + MipRecordOffset::Reserved0, 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::ReservedFieldNonZero);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::Width, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidDimensions);

        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::Height, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidDimensions);

        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::LayerCount, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidDimensions);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::MipCount, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidMipCount);

        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::MipCount, 2);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidMipCount);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::PayloadSize, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidPayloadSize);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::PixelFormat, 999);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::UnknownPixelFormat);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, HeaderOffset::ColorSpace, 999);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::UnknownColorSpace);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::R8UNorm, CookedTextureColorSpace::Linear);
        WriteLe32(bytes, HeaderOffset::ColorSpace, ColorSpaceSRGB);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidColorSpaceForFormat);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::MipTableSize, static_cast<uint64_t>(MipRecordSize * 2));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipTableSizeMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::MipTableOffset, static_cast<uint64_t>(bytes.size() + 8));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipTableOutOfRange);

        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::PayloadOffset, HeaderSize);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipTableOutOfRange);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        const uint64_t payloadOffset = ReadLe64(bytes, HeaderOffset::PayloadOffset);
        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataOffset, payloadOffset - 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipOffsetBeforePayload);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        const uint64_t payloadOffset = ReadLe64(bytes, HeaderOffset::PayloadOffset);
        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataOffset, payloadOffset + 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipPackingMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe32(bytes, MipRecordOffsetFor(1) + MipRecordOffset::Width, 3);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipDimensionsMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        const uint64_t dataSize = ReadLe64(bytes, MipRecordOffsetFor(1) + MipRecordOffset::DataSize);
        WriteLe64(bytes, MipRecordOffsetFor(1) + MipRecordOffset::DataSize, dataSize + 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipDataSizeMismatch);
    }

    {
        ExpectStatus(BuildOverflowTexture(), CookedTextureParseStatus::IntegerOverflow);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        WriteLe64(bytes, HeaderOffset::PayloadHash, 0);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::PayloadHashMismatch);
    }

    {
        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        bytes.pop_back();
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(bytes.size()));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::TruncatedPayload);
    }

    // v0.1: ブロック圧縮の形式。ミップは 4x4 画素のブロック単位（端は切り上げ、最小 1 ブロック）で数える。
    {
        const std::vector<uint8_t> bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7,
                                                        CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed);
        assert(ReadLe16(bytes, HeaderOffset::VersionMinor) == 1);
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(bytes));
        assert(result.Succeeded());
        assert(result.Texture.PixelFormat == CookedTexturePixelFormat::BC7);
        assert(result.Texture.ColorSpace == CookedTextureColorSpace::SRGB);
        assert(result.Texture.MipCount == 4);
        assert(result.Texture.Mips.size() == 4);
        assert(result.Texture.Mips[0].DataSize == 64);  // 2x2 ブロック
        assert(result.Texture.Mips[1].DataSize == 16);  // 4x4 画素 = 1 ブロック
        assert(result.Texture.Mips[2].DataSize == 16);  // 2x2 画素でも最小 1 ブロック
        assert(result.Texture.Mips[3].DataSize == 16);  // 1x1 画素でも最小 1 ブロック
        assert(result.Texture.Mips[3].Width == 1 && result.Texture.Mips[3].Height == 1);
        AssertSpanBytes(result.Texture.GetMipBytes(0), 0);
        AssertSpanBytes(result.Texture.GetMipBytes(1), 64);
        AssertSpanBytes(result.Texture.GetMipBytes(3), 96);
    }

    {
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(
            BuildTexture(6, 5, 1, CookedTexturePixelFormat::BC1, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed)));
        assert(result.Succeeded());
        assert(result.Texture.PixelFormat == CookedTexturePixelFormat::BC1);
        assert(result.Texture.MipCount == 3);
        assert(result.Texture.Mips[0].DataSize == 32);  // 6x5 画素 = 2x2 ブロック
        assert(result.Texture.Mips[1].Width == 3 && result.Texture.Mips[1].Height == 2);
        assert(result.Texture.Mips[1].DataSize == 8);
        assert(result.Texture.Mips[2].DataSize == 8);
    }

    {
        const CookedTextureParseResult bc1Srgb = ParseCookedTexture(MakeBlob(
            BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC1, CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed)));
        assert(bc1Srgb.Succeeded());
        assert(bc1Srgb.Texture.ColorSpace == CookedTextureColorSpace::SRGB);

        const CookedTextureParseResult bc4 = ParseCookedTexture(MakeBlob(
            BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC4, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed)));
        assert(bc4.Succeeded());
        assert(bc4.Texture.Mips[0].DataSize == 8);

        const CookedTextureParseResult bc5 = ParseCookedTexture(MakeBlob(
            BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC5, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed)));
        assert(bc5.Succeeded());
        assert(bc5.Texture.Mips[0].DataSize == 16);
    }

    // v0.1: R16 は 1 画素 2 バイトの非圧縮として数える。配列も通る。
    {
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(
            BuildTexture(3, 2, 2, CookedTexturePixelFormat::R16UNorm, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed)));
        assert(result.Succeeded());
        assert(result.Texture.PixelFormat == CookedTexturePixelFormat::R16UNorm);
        assert(result.Texture.MipCount == 2);
        assert(result.Texture.Mips[0].DataSize == 24);  // 3x2 画素 x 2 バイト x 2 レイヤー
        assert(result.Texture.Mips[1].DataSize == 4);   // 1x1 画素 x 2 バイト x 2 レイヤー
    }

    // v0.1 は v0.0 の形式も読める。BC の配列はブロック単位でレイヤーを数える。
    {
        const CookedTextureParseResult rgba = ParseCookedTexture(MakeBlob(
            BuildTexture(2, 2, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed)));
        assert(rgba.Succeeded());
        assert(rgba.Texture.Mips[0].DataSize == 16);

        const CookedTextureParseResult bcArray = ParseCookedTexture(MakeBlob(
            BuildTexture(4, 4, 3, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed)));
        assert(bcArray.Succeeded());
        assert(bcArray.Texture.LayerCount == 3);
        assert(bcArray.Texture.Mips[0].DataSize == 48);
    }

    // v0.0 は BC・R16 を表せない。v0.1 でも未知の形式・知らない版は拒否する。
    {
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear, VersionMinor),
                     CookedTextureParseStatus::UnknownPixelFormat);
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC1, CookedTextureColorSpace::Linear, VersionMinor),
                     CookedTextureParseStatus::UnknownPixelFormat);
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::R16UNorm, CookedTextureColorSpace::Linear, VersionMinor),
                     CookedTextureParseStatus::UnknownPixelFormat);

        std::vector<uint8_t> bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                  VersionMinorBlockCompressed);
        WriteLe32(bytes, HeaderOffset::PixelFormat, 9);
        ExpectStatus(bytes, CookedTextureParseStatus::UnknownPixelFormat);
        WriteLe32(bytes, HeaderOffset::PixelFormat, 0);
        ExpectStatus(bytes, CookedTextureParseStatus::UnknownPixelFormat);

        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        WriteLe16(bytes, HeaderOffset::VersionMinor, 3);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::UnsupportedVersion);
    }

    // sRGB を持てない形式（BC4・BC5・R16）は拒否する。
    {
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC4, CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed),
                     CookedTextureParseStatus::InvalidColorSpaceForFormat);
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC5, CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed),
                     CookedTextureParseStatus::InvalidColorSpaceForFormat);
        ExpectStatus(BuildTexture(4, 4, 1, CookedTexturePixelFormat::R16UNorm, CookedTextureColorSpace::SRGB, VersionMinorBlockCompressed),
                     CookedTextureParseStatus::InvalidColorSpaceForFormat);
    }

    // ミップのバイト数はブロック単位で厳密に照合する（ブロック数の不足・過剰、画素単位の数え方を拒否）。
    {
        std::vector<uint8_t> bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                  VersionMinorBlockCompressed);
        const uint64_t mip0Size = ReadLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataSize);
        assert(mip0Size == 64);
        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataSize, mip0Size - 16);  // 1 ブロック不足
        ExpectStatus(bytes, CookedTextureParseStatus::MipDataSizeMismatch);

        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataSize, mip0Size + 16);  // 1 ブロック過剰
        ExpectStatus(bytes, CookedTextureParseStatus::MipDataSizeMismatch);

        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataSize, mip0Size);
        WriteLe64(bytes, MipRecordOffsetFor(3) + MipRecordOffset::DataSize, 1);  // 末尾の 1x1 は最小 1 ブロックが要る
        ExpectStatus(bytes, CookedTextureParseStatus::MipDataSizeMismatch);

        // 非圧縮と同じ画素数 x ブロックのバイト数で数えたサイズ（8x8 なら 1024）も通さない
        bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                             VersionMinorBlockCompressed);
        WriteLe64(bytes, MipRecordOffsetFor(0) + MipRecordOffset::DataSize, 8ull * 8ull * 16ull);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::MipDataSizeMismatch);
    }

    // 全段必須: 圧縮形式でもミップ数の省略は拒否する。
    {
        std::vector<uint8_t> bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                  VersionMinorBlockCompressed);
        WriteLe32(bytes, HeaderOffset::MipCount, 1);
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::InvalidMipCount);
    }

    // ペイロードが切れた BC は拒否する。
    {
        std::vector<uint8_t> bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                  VersionMinorBlockCompressed);
        bytes.pop_back();
        WriteLe64(bytes, HeaderOffset::FileSize, static_cast<uint64_t>(bytes.size()));
        ExpectStatus(std::move(bytes), CookedTextureParseStatus::TruncatedPayload);
    }

    // ---- v0.2: タイル配置 ----

    // BC7 1024x512: 段 0 は 256x256 のタイル 4x2、段 1 は 2x1、段 2（256x128）以降はミップテイル。
    {
        const BuiltTiled built = BuildTiledTexture(1024, 512, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear);
        assert(built.FirstTailMip == 2 && built.Tiles.size() == 10);
        assert(built.Tiles[0].Data.size() == 65536);

        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(built.Bytes));
        assert(result.Succeeded());
        assert(result.Texture.Tiling.TileWidth == 256 && result.Texture.Tiling.TileHeight == 256);
        ExpectTiledTexture(built, result.Texture);

        // 段ごとの行優先への展開が、元の行優先と一致する（v0.0 と同じ GetMipBytes で読める）。
        for (uint32_t mipIndex = 0; mipIndex < result.Texture.MipCount; ++mipIndex)
        {
            assert(SameBytes(result.Texture.GetMipBytes(mipIndex), built.RowMajorMips[mipIndex]));
        }

        // 格子の外・テイルの段・存在しないレイヤーは引けない。
        CookedTextureTile tile;
        assert(!result.Texture.FindTile(0, 0, 4, 0, tile));
        assert(!result.Texture.FindTile(0, 0, 0, 2, tile));
        assert(!result.Texture.FindTile(1, 0, 2, 0, tile));
        assert(!result.Texture.FindTile(2, 0, 0, 0, tile));
        assert(!result.Texture.FindTile(99, 0, 0, 0, tile));
        assert(!result.Texture.FindTile(0, 1, 0, 0, tile));

        // 展開しない解析でも表は引ける。GetMipBytes は空になる。
        const CookedTextureParseResult lazy = ParseCookedTexture(MakeBlob(built.Bytes), false);
        assert(lazy.Succeeded());
        ExpectTiledTexture(built, lazy.Texture);
        assert(lazy.Texture.GetMipBytes(0).empty());
        assert(lazy.Texture.GetMipBytes(3).empty());
    }

    // 端のタイルは切り詰める。BC4 1100x600（タイル 512x256）: 段 0 は 3x3、右端と下端のタイルは小さい。
    {
        const BuiltTiled built = BuildTiledTexture(1100, 600, 1, CookedTexturePixelFormat::BC4, CookedTextureColorSpace::Linear);
        assert(built.FirstTailMip == 2);
        assert(built.Tiles.size() == 9 + 4);

        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(built.Bytes));
        assert(result.Succeeded());
        ExpectTiledTexture(built, result.Texture);
        CookedTextureTile tile;
        assert(result.Texture.FindTile(0, 0, 1, 1, tile) && tile.DataSize == 65536);
        // 右端: 1100/4 = 275 ブロックのうち 256 を除いた 19 ブロック、下端: 150 ブロックのうち 128 を除いた 22 ブロック
        assert(result.Texture.FindTile(0, 0, 2, 0, tile) && tile.DataSize == 19u * 64u * 8u);
        assert(result.Texture.FindTile(0, 0, 0, 2, tile) && tile.DataSize == 128u * 22u * 8u);
        assert(result.Texture.FindTile(0, 0, 2, 2, tile) && tile.DataSize == 19u * 22u * 8u);
        for (uint32_t mipIndex = 0; mipIndex < result.Texture.MipCount; ++mipIndex)
        {
            assert(SameBytes(result.Texture.GetMipBytes(mipIndex), built.RowMajorMips[mipIndex]));
        }
    }

    // 配列: タイルはレイヤー順に並び、展開でレイヤーごとの位置へ戻る。
    {
        const BuiltTiled built = BuildTiledTexture(256, 256, 2, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::SRGB);
        assert(built.FirstTailMip == 2 && built.Tiles.size() == 2 * (4 + 1));
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(built.Bytes));
        assert(result.Succeeded());
        ExpectTiledTexture(built, result.Texture);
        CookedTextureTile tile;
        assert(result.Texture.FindTile(0, 1, 1, 0, tile) && tile.LayerIndex == 1 && tile.DataSize == 65536);
        assert(result.Texture.FindTile(1, 1, 0, 0, tile) && tile.DataSize == 65536);
        assert(!result.Texture.FindTile(1, 2, 0, 0, tile));
        for (uint32_t mipIndex = 0; mipIndex < result.Texture.MipCount; ++mipIndex)
        {
            assert(SameBytes(result.Texture.GetMipBytes(mipIndex), built.RowMajorMips[mipIndex]));
        }
    }

    // 形式ごとの標準ブロック形状: 1 バイト 256x256、2 バイト 256x128。
    {
        const BuiltTiled r8 = BuildTiledTexture(512, 256, 1, CookedTexturePixelFormat::R8UNorm, CookedTextureColorSpace::Linear);
        assert(r8.FirstTailMip == 1 && r8.Tiles.size() == 2);
        const CookedTextureParseResult r8Result = ParseCookedTexture(MakeBlob(r8.Bytes));
        assert(r8Result.Succeeded());
        ExpectTiledTexture(r8, r8Result.Texture);

        const BuiltTiled rg8 = BuildTiledTexture(256, 128, 1, CookedTexturePixelFormat::RG8UNorm, CookedTextureColorSpace::Linear);
        assert(rg8.FirstTailMip == 1 && rg8.Tiles.size() == 1);
        const CookedTextureParseResult rg8Result = ParseCookedTexture(MakeBlob(rg8.Bytes));
        assert(rg8Result.Succeeded());
        ExpectTiledTexture(rg8, rg8Result.Texture);

        const BuiltTiled r16 = BuildTiledTexture(512, 256, 1, CookedTexturePixelFormat::R16UNorm, CookedTextureColorSpace::Linear);
        assert(r16.FirstTailMip == 2 && r16.Tiles.size() == 4 + 1);
        const CookedTextureParseResult r16Result = ParseCookedTexture(MakeBlob(r16.Bytes));
        assert(r16Result.Succeeded());
        ExpectTiledTexture(r16, r16Result.Texture);
        for (uint32_t mipIndex = 0; mipIndex < r16Result.Texture.MipCount; ++mipIndex)
        {
            assert(SameBytes(r16Result.Texture.GetMipBytes(mipIndex), r16.RowMajorMips[mipIndex]));
        }
    }

    // タイルより小さいテクスチャは全段がミップテイル（表は空）。
    {
        const BuiltTiled built = BuildTiledTexture(64, 64, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::SRGB);
        assert(built.FirstTailMip == 0 && built.Tiles.empty());
        const CookedTextureParseResult result = ParseCookedTexture(MakeBlob(built.Bytes));
        assert(result.Succeeded());
        ExpectTiledTexture(built, result.Texture);
        assert(result.Texture.Tiling.TailOffset == built.MetadataSize);
        CookedTextureTile tile;
        assert(!result.Texture.FindTile(0, 0, 0, 0, tile));
        for (uint32_t mipIndex = 0; mipIndex < result.Texture.MipCount; ++mipIndex)
        {
            assert(SameBytes(result.Texture.GetMipBytes(mipIndex), built.RowMajorMips[mipIndex]));
        }
    }

    // 壊れた表は拒否する。タイルの表はペイロードのハッシュに入らないので、表の食い違いは表の検査だけが止める。
    {
        const BuiltTiled built = BuildTiledTexture(1024, 512, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear);
        const size_t record0 = built.TileTableOffset;
        const size_t record3 = built.TileTableOffset + 3 * TileRecordSize;

        std::vector<uint8_t> bytes = built.Bytes;
        WriteLe64(bytes, record0 + TileRecordOffset::DataOffset, built.Tiles[0].Offset + 1);
        ExpectStatus(bytes, CookedTextureParseStatus::TileRecordMismatch);

        bytes = built.Bytes;
        WriteLe32(bytes, record0 + TileRecordOffset::TileX, 1);  // 先頭の件のタイルの位置を取り違える
        ExpectStatus(bytes, CookedTextureParseStatus::TileRecordMismatch);

        bytes = built.Bytes;
        WriteLe64(bytes, record3 + TileRecordOffset::DataSize, 65535);
        ExpectStatus(bytes, CookedTextureParseStatus::TileRecordMismatch);

        bytes = built.Bytes;
        WriteLe32(bytes, record3 + TileRecordOffset::MipIndex, 1);
        ExpectStatus(bytes, CookedTextureParseStatus::TileRecordMismatch);

        bytes = built.Bytes;
        WriteLe32(bytes, record3 + TileRecordOffset::LayerIndex, 1);
        ExpectStatus(bytes, CookedTextureParseStatus::TileRecordMismatch);

        bytes = built.Bytes;
        WriteLe64(bytes, TiledHeaderOffset::TileTableSize, 10 * TileRecordSize + TileRecordSize);
        ExpectStatus(bytes, CookedTextureParseStatus::TileTableSizeMismatch);
        WriteLe64(bytes, TiledHeaderOffset::TileTableSize, 9 * TileRecordSize);
        ExpectStatus(bytes, CookedTextureParseStatus::TileTableSizeMismatch);

        bytes = built.Bytes;
        WriteLe64(bytes, TiledHeaderOffset::TileTableOffset, built.TileTableOffset + TileRecordSize);
        ExpectStatus(bytes, CookedTextureParseStatus::TileTableOutOfRange);

        bytes = built.Bytes;
        WriteLe32(bytes, TiledHeaderOffset::TileWidth, 128);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidTileShape);
        bytes = built.Bytes;
        WriteLe32(bytes, TiledHeaderOffset::TileHeight, 128);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidTileShape);
        bytes = built.Bytes;
        WriteLe32(bytes, TiledHeaderOffset::TileDataBytes, 32768);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidTileShape);

        bytes = built.Bytes;
        WriteLe32(bytes, TiledHeaderOffset::FirstTailMip, 1);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidFirstTailMip);
        WriteLe32(bytes, TiledHeaderOffset::FirstTailMip, 3);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidFirstTailMip);
        WriteLe32(bytes, TiledHeaderOffset::FirstTailMip, built.FirstTailMip + 100);
        ExpectStatus(bytes, CookedTextureParseStatus::InvalidFirstTailMip);

        bytes = built.Bytes;
        WriteLe64(bytes, TiledHeaderOffset::TailOffset, built.TailOffset - 1);
        ExpectStatus(bytes, CookedTextureParseStatus::TailRangeMismatch);
        bytes = built.Bytes;
        WriteLe64(bytes, TiledHeaderOffset::TailSize, built.TailSize + 1);
        ExpectStatus(bytes, CookedTextureParseStatus::TailRangeMismatch);
        bytes = built.Bytes;
        WriteLe64(bytes, TiledHeaderOffset::TailSize, 0);
        ExpectStatus(bytes, CookedTextureParseStatus::TailRangeMismatch);

        // ミップ表はヘッダの直後（v0.2 は 160 バイト目）に限る。
        bytes = built.Bytes;
        WriteLe64(bytes, HeaderOffset::MipTableOffset, HeaderSizeTiled + 1);
        ExpectStatus(bytes, CookedTextureParseStatus::MipTableOutOfRange);

        // ヘッダの大きさは版で決まる。
        bytes = built.Bytes;
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        ExpectStatus(bytes, CookedTextureParseStatus::HeaderSizeMismatch);
        bytes = BuildTexture(4, 4, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear, VersionMinorBlockCompressed);
        WriteLe32(bytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSizeTiled));
        ExpectStatus(bytes, CookedTextureParseStatus::HeaderSizeMismatch);

        // 160 バイトに満たない v0.2。
        bytes = built.Bytes;
        bytes.resize(150);
        ExpectStatus(bytes, CookedTextureParseStatus::HeaderTooSmall);

        // v0.2 は BC・R16 も持てる。v0.0 の形式は v0.2 でも読める（クッカーは書かないが形式として許す）。
        const BuiltTiled rgba = BuildTiledTexture(8, 8, 1, CookedTexturePixelFormat::RGBA8UNorm, CookedTextureColorSpace::Linear);
        assert(ParseCookedTexture(MakeBlob(rgba.Bytes)).Succeeded());
    }

    // ペイロードの破損は全体読みのハッシュが止める。範囲読みの解析（メタデータだけ）は本体を読まないので通る。
    {
        const BuiltTiled built = BuildTiledTexture(1024, 512, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear);
        std::vector<uint8_t> bytes = built.Bytes;
        bytes[built.Tiles[2].Offset + 100] ^= 0xffu;
        ExpectStatus(bytes, CookedTextureParseStatus::PayloadHashMismatch);

        const Span<const uint8_t> metadata(bytes.data(), built.MetadataSize);
        const CookedTextureParseResult layout = ParseCookedTextureLayout(metadata, bytes.size());
        assert(layout.Succeeded());
        assert(!layout.Texture.SourceBlob.IsValid());
        assert(layout.Texture.GetMipBytes(0).empty());
        ExpectTiledTexture(built, layout.Texture);

        // メタデータが 1 バイトでも足りない、ヘッダにも満たない、全体の大きさが合わないものは拒否する。
        assert(ParseCookedTextureLayout(Span<const uint8_t>(bytes.data(), built.MetadataSize - 1), bytes.size()).Status ==
               CookedTextureParseStatus::MetadataTooSmall);
        assert(ParseCookedTextureLayout(Span<const uint8_t>(bytes.data(), 100), bytes.size()).Status ==
               CookedTextureParseStatus::HeaderTooSmall);
        assert(ParseCookedTextureLayout(metadata, bytes.size() + 1).Status == CookedTextureParseStatus::FileSizeMismatch);
        assert(ParseCookedTextureLayout(Span<const uint8_t>(), bytes.size()).Status == CookedTextureParseStatus::EmptyBlob);

        // 1 回目の読み（先頭 112 バイト）からメタデータの大きさが分かる。
        uint64_t metadataSize = 0;
        const Span<const uint8_t> head(bytes.data(), HeaderSize);
        assert(GetCookedTextureMetadataSize(head, bytes.size(), metadataSize) == CookedTextureParseStatus::Success);
        assert(metadataSize == built.MetadataSize);
        assert(GetCookedTextureMetadataSize(head, bytes.size() + 1, metadataSize) == CookedTextureParseStatus::FileSizeMismatch);
        assert(GetCookedTextureMetadataSize(Span<const uint8_t>(bytes.data(), HeaderSize - 1), bytes.size(), metadataSize) ==
               CookedTextureParseStatus::HeaderTooSmall);
    }

    // v0.0・v0.1 の範囲読みの解析は通り、タイルは無い。
    {
        const std::vector<uint8_t> bytes = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                        VersionMinorBlockCompressed);
        const size_t metadataSize = HeaderSize + 4 * MipRecordSize;
        const CookedTextureParseResult layout =
            ParseCookedTextureLayout(Span<const uint8_t>(bytes.data(), metadataSize), bytes.size());
        assert(layout.Succeeded());
        assert(!layout.Texture.bTiled && layout.Texture.VersionMinor == VersionMinorBlockCompressed);
        CookedTextureTile tile;
        assert(!layout.Texture.FindTile(0, 0, 0, 0, tile));
        const CookedTextureParseResult full = ParseCookedTexture(MakeBlob(bytes));
        assert(full.Succeeded() && !full.Texture.bTiled);
        AssertSpanBytes(full.Texture.GetMipBytes(0), 0);
    }

    // ファイルの範囲読み: メタデータだけを読み、1 タイル・ミップテイルをその範囲だけ読む（全体は読まない）。
    {
        const BuiltTiled built = BuildTiledTexture(1024, 512, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear);
        const std::filesystem::path root = CreateTileTestRoot();

        // パッケージのエントリのように、.nvtex の前に別のバイト列がある場合（baseOffset）も読む。
        const uint64_t baseOffset = 37;
        std::vector<uint8_t> fileBytes(baseOffset, 0xabu);
        fileBytes.insert(fileBytes.end(), built.Bytes.begin(), built.Bytes.end());
        const std::filesystem::path path = root / "tiled.bin";
        WriteFileBytes(path, fileBytes);

        const AssetFileReader reader;
        const AssetReadRequest request = MakeRequest(path);

        const CookedTextureParseResult layout = ReadCookedTextureLayout(reader, request, baseOffset);
        assert(layout.Succeeded());
        assert(layout.Texture.bTiled);
        assert(!layout.Texture.SourceBlob.IsValid());
        ExpectTiledTexture(built, layout.Texture);

        // 全タイルを 1 件ずつ範囲読みして、中身が元のタイルと一致する。読んだ量はタイルの大きさだけ。
        for (const TileExpect &expect : built.Tiles)
        {
            const AssetReadResult tile =
                ReadCookedTextureTile(reader, request, baseOffset, layout.Texture, expect.Mip, expect.Layer, expect.X, expect.Y);
            assert(tile.Succeeded());
            assert(tile.BytesRead == expect.Data.size());
            assert(tile.Blob.GetSize() == expect.Data.size());
            assert(tile.FileSize == static_cast<int64_t>(fileBytes.size()));
            assert(tile.BytesRead < static_cast<size_t>(tile.FileSize));
            assert(SameBytes(tile.Blob.GetSpan(), expect.Data));
        }

        const AssetReadResult tail = ReadCookedTextureMipTail(reader, request, baseOffset, layout.Texture);
        assert(tail.Succeeded());
        assert(tail.BytesRead == built.TailSize);
        assert(std::memcmp(tail.Blob.GetData(), built.Bytes.data() + built.TailOffset, built.TailSize) == 0);

        // 表に無いタイル（テイルの段・格子の外）は読まない。
        assert(ReadCookedTextureTile(reader, request, baseOffset, layout.Texture, 2, 0, 0, 0).Status == AssetReadStatus::InvalidRequest);
        assert(ReadCookedTextureTile(reader, request, baseOffset, layout.Texture, 0, 0, 4, 0).Status == AssetReadStatus::InvalidRequest);

        // baseOffset が違う（先頭がマジックでない）、.nvtex の大きさがファイルを超える。
        assert(ReadCookedTextureLayout(reader, request, 0).Status == CookedTextureParseStatus::BadMagic);
        assert(ReadCookedTextureLayout(reader, request, baseOffset, built.Bytes.size()).Succeeded());
        assert(ReadCookedTextureLayout(reader, request, baseOffset, built.Bytes.size() + 1).Status ==
               CookedTextureParseStatus::ReadFailed);

        // v0.0・v0.1 のファイルのタイルは読めない。
        const std::vector<uint8_t> legacy = BuildTexture(8, 8, 1, CookedTexturePixelFormat::BC7, CookedTextureColorSpace::Linear,
                                                         VersionMinorBlockCompressed);
        const std::filesystem::path legacyPath = root / "legacy.nvtex";
        WriteFileBytes(legacyPath, legacy);
        const AssetReadRequest legacyRequest = MakeRequest(legacyPath);
        const CookedTextureParseResult legacyLayout = ReadCookedTextureLayout(reader, legacyRequest);
        assert(legacyLayout.Succeeded() && !legacyLayout.Texture.bTiled);
        assert(ReadCookedTextureTile(reader, legacyRequest, 0, legacyLayout.Texture, 0, 0, 0, 0).Status == AssetReadStatus::InvalidRequest);
        assert(ReadCookedTextureMipTail(reader, legacyRequest, 0, legacyLayout.Texture).Status == AssetReadStatus::InvalidRequest);

        // ヘッダに満たないファイル・無いファイル。
        const std::filesystem::path shortPath = root / "short.nvtex";
        WriteFileBytes(shortPath, std::vector<uint8_t>(50, 0));
        assert(ReadCookedTextureLayout(reader, MakeRequest(shortPath)).Status == CookedTextureParseStatus::HeaderTooSmall);
        assert(ReadCookedTextureLayout(reader, MakeRequest(root / "missing.nvtex")).Status == CookedTextureParseStatus::ReadFailed);

        // AssetFileReader::ReadRange 自体: 範囲の中身・範囲外・サイズ 0。
        const AssetReadResult middle = reader.ReadRange(request, baseOffset + 200, 64);
        assert(middle.Succeeded() && middle.BytesRead == 64);
        assert(SameBytes(middle.Blob.GetSpan(), std::vector<uint8_t>(built.Bytes.begin() + 200, built.Bytes.begin() + 264)));
        assert(reader.ReadRange(request, fileBytes.size() - 8, 8).Succeeded());
        assert(reader.ReadRange(request, fileBytes.size() - 8, 9).Status == AssetReadStatus::ReadFailed);
        assert(reader.ReadRange(request, fileBytes.size() + 1, 1).Status == AssetReadStatus::ReadFailed);
        assert(reader.ReadRange(request, 0, 0).Status == AssetReadStatus::InvalidRequest);

        std::filesystem::remove_all(root);
    }

    std::cout << "CookedTextureTest passed\n";
    return 0;
}
