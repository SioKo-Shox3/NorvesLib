#pragma once

#include "Asset/AssetBlob.h"
#include "Asset/AssetFileReader.h"
#include "Asset/AssetReadRequest.h"
#include "Container/PointerTypes.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Asset
{
    namespace CookedTextureFormatV0
    {
        inline constexpr uint8_t Magic[] = {'N', 'V', 'T', 'E', 'X', 'v', '0', '\0'};
        inline constexpr size_t MagicSize = sizeof(Magic);

        inline constexpr uint16_t VersionMajor = 0;
        // v0.0: 非圧縮の R8/RG8/RGBA8 のみ。クッカーはこの版で書く。
        inline constexpr uint16_t VersionMinor = 0;
        // v0.1: v0.0 の上位互換で、ブロック圧縮（BC1/BC4/BC5/BC7）と R16 の形式が加わる。
        inline constexpr uint16_t VersionMinorBlockCompressed = 1;
        // v0.2: v0.1 の上位互換で、標準ブロック形状（64 KiB）のタイル単位に並べたデータ、ミップテイルの塊、
        // タイルの表を持つ。ヘッダは 160 バイト（v0.0・v0.1 は 112 バイト）。
        inline constexpr uint16_t VersionMinorTiled = 2;
        inline constexpr uint32_t EndianMarker = 0x01020304u;

        inline constexpr uint32_t PixelFormatR8UNorm = 1;
        inline constexpr uint32_t PixelFormatRG8UNorm = 2;
        inline constexpr uint32_t PixelFormatRGBA8UNorm = 3;
        // 以下は v0.1 以降だけで使える。
        inline constexpr uint32_t PixelFormatBC1 = 4;
        inline constexpr uint32_t PixelFormatBC4 = 5;
        inline constexpr uint32_t PixelFormatBC5 = 6;
        inline constexpr uint32_t PixelFormatBC7 = 7;
        inline constexpr uint32_t PixelFormatR16UNorm = 8;

        inline constexpr uint32_t ColorSpaceLinear = 1;
        inline constexpr uint32_t ColorSpaceSRGB = 2;

        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr uint64_t ZeroSizePayloadHash = Fnv1a64OffsetBasis;

        inline constexpr size_t HeaderSize = 112;
        inline constexpr size_t HeaderSizeTiled = 160;
        inline constexpr size_t MipRecordSize = 32;
        inline constexpr size_t TileRecordSize = 32;
        // sparse の標準ブロック形状のタイル 1 枚のバイト数。端のタイルは切り詰めるので、これより小さいことがある。
        inline constexpr uint32_t StandardTileBytes = 65536;

        namespace HeaderOffset
        {
            inline constexpr size_t Magic = 0;            // uint8[8]
            inline constexpr size_t HeaderSize = 8;       // uint32
            inline constexpr size_t VersionMajor = 12;    // uint16
            inline constexpr size_t VersionMinor = 14;    // uint16
            inline constexpr size_t EndianMarker = 16;    // uint32
            inline constexpr size_t MipRecordSize = 20;   // uint32
            inline constexpr size_t FileSize = 24;        // uint64
            inline constexpr size_t MipTableOffset = 32;  // uint64, absolute file offset
            inline constexpr size_t MipTableSize = 40;    // uint64
            inline constexpr size_t PayloadOffset = 48;   // uint64, absolute file offset
            inline constexpr size_t PayloadSize = 56;     // uint64
            inline constexpr size_t PayloadHash = 64;     // uint64, FNV-1a64 over mip payload bytes only
            inline constexpr size_t Width = 72;           // uint32
            inline constexpr size_t Height = 76;          // uint32
            inline constexpr size_t LayerCount = 80;      // uint32, 2D array layers only
            inline constexpr size_t MipCount = 84;        // uint32, full chain required
            inline constexpr size_t PixelFormat = 88;     // uint32
            inline constexpr size_t ColorSpace = 92;      // uint32
            inline constexpr size_t Flags = 96;           // uint32, reserved zero
            inline constexpr size_t Reserved0 = 100;      // uint32, reserved zero
            inline constexpr size_t Reserved1 = 104;      // uint64, reserved zero
        }

        // v0.2 のヘッダの続き（112 バイト目から）。v0.0・v0.1 には無い。
        namespace TiledHeaderOffset
        {
            inline constexpr size_t TileWidth = 112;       // uint32, texel。形式の標準ブロック形状の幅
            inline constexpr size_t TileHeight = 116;      // uint32, texel
            inline constexpr size_t FirstTailMip = 120;    // uint32, この段以降がミップテイル（タイルより小さい段）
            inline constexpr size_t TileDataBytes = 124;   // uint32, StandardTileBytes と一致
            inline constexpr size_t TileTableOffset = 128; // uint64, absolute file offset（ミップ表の直後）
            inline constexpr size_t TileTableSize = 136;   // uint64
            inline constexpr size_t TailOffset = 144;      // uint64, absolute file offset（ミップテイルの塊の先頭）
            inline constexpr size_t TailSize = 152;        // uint64
        }

        // タイルの表の 1 件。表の並びは「ミップ昇順 → レイヤー昇順 → タイルの行 → タイルの列」で、件数も位置も形式から決まる。
        namespace TileRecordOffset
        {
            inline constexpr size_t DataOffset = 0;   // uint64, absolute file offset
            inline constexpr size_t DataSize = 8;     // uint64
            inline constexpr size_t MipIndex = 16;    // uint32
            inline constexpr size_t LayerIndex = 20;  // uint32
            inline constexpr size_t TileX = 24;       // uint32
            inline constexpr size_t TileY = 28;       // uint32
        }

        namespace MipRecordOffset
        {
            inline constexpr size_t DataOffset = 0;   // uint64, absolute file offset
            inline constexpr size_t DataSize = 8;     // uint64
            inline constexpr size_t Width = 16;       // uint32
            inline constexpr size_t Height = 20;      // uint32
            inline constexpr size_t Reserved0 = 24;   // uint32, reserved zero
            inline constexpr size_t Reserved1 = 28;   // uint32, reserved zero
        }
    }

    enum class CookedTexturePixelFormat : uint32_t
    {
        R8UNorm = CookedTextureFormatV0::PixelFormatR8UNorm,
        RG8UNorm = CookedTextureFormatV0::PixelFormatRG8UNorm,
        RGBA8UNorm = CookedTextureFormatV0::PixelFormatRGBA8UNorm,
        // v0.1 以降。BC1/BC7 は ColorSpace で sRGB の有無を表し、BC4/BC5/R16 は Linear のみ。
        BC1 = CookedTextureFormatV0::PixelFormatBC1,
        BC4 = CookedTextureFormatV0::PixelFormatBC4,
        BC5 = CookedTextureFormatV0::PixelFormatBC5,
        BC7 = CookedTextureFormatV0::PixelFormatBC7,
        R16UNorm = CookedTextureFormatV0::PixelFormatR16UNorm
    };

    enum class CookedTextureColorSpace : uint32_t
    {
        Linear = CookedTextureFormatV0::ColorSpaceLinear,
        SRGB = CookedTextureFormatV0::ColorSpaceSRGB
    };

    enum class CookedTextureParseStatus : uint8_t
    {
        Success,
        InvalidBlob,
        EmptyBlob,
        HeaderTooSmall,
        BadMagic,
        UnsupportedVersion,
        EndianMismatch,
        HeaderSizeMismatch,
        MipRecordSizeMismatch,
        FileSizeMismatch,
        ReservedFieldNonZero,
        InvalidDimensions,
        InvalidMipCount,
        InvalidPayloadSize,
        UnknownPixelFormat,
        UnknownColorSpace,
        InvalidColorSpaceForFormat,
        IntegerOverflow,
        MipTableSizeMismatch,
        MipTableOutOfRange,
        MipOffsetBeforePayload,
        MipPackingMismatch,
        MipDimensionsMismatch,
        MipDataSizeMismatch,
        PayloadHashMismatch,
        TruncatedPayload,
        // 以下は v0.2（タイル配置）と範囲読みで使う。
        InvalidTileShape,
        InvalidFirstTailMip,
        TileTableSizeMismatch,
        TileTableOutOfRange,
        TileRecordMismatch,
        TailRangeMismatch,
        MetadataTooSmall,
        ReadFailed
    };

    struct CookedTextureMip
    {
        // .nvtex の先頭からのオフセット。v0.2 のタイル配置の段では、その段のタイルを表の順に並べた区間の先頭になる。
        size_t DataOffset = 0;
        // 段の全レイヤーのバイト数。タイル配置でも行優先でも同じ値（タイルは段を隙間なく分割する）。
        size_t DataSize = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        // 行優先へ展開したバイト列（CookedTextureData::RowMajorStorage）の中の先頭。v0.2 の GetMipBytes が使う。
        size_t RowMajorOffset = 0;
    };

    /**
     * @brief v0.2 のタイル 1 枚の位置。DataOffset は .nvtex の先頭からのオフセット。
     *
     * 中身は、そのタイルの範囲（端は切り詰め）を 1 行ずつ行優先で詰めたバイト列で、行の余白は無い。
     * 非圧縮の形式は 1 行が（タイルの幅）* bytes_per_pixel バイト、ブロック圧縮の形式は 1 行がタイルの幅のブロック分。
     */
    struct CookedTextureTile
    {
        uint32_t MipIndex = 0;
        uint32_t LayerIndex = 0;
        uint32_t TileX = 0;
        uint32_t TileY = 0;
        uint64_t DataOffset = 0;
        uint32_t DataSize = 0;
    };

    /**
     * @brief v0.2 のタイル配置。FirstTailMip より前の段はタイル単位、それ以降の段（タイルより小さい段）は
     * ミップテイルの 1 つの塊に、v0.0 と同じ行優先（ミップ昇順、レイヤー昇順）で並ぶ。
     */
    struct CookedTextureTiling
    {
        uint32_t TileWidth = 0;   // texel
        uint32_t TileHeight = 0;  // texel
        uint32_t FirstTailMip = 0;
        uint64_t TailOffset = 0;  // .nvtex の先頭からのオフセット
        uint64_t TailSize = 0;
        // 表の並び（ミップ昇順 → レイヤー昇順 → タイルの行 → タイルの列）。
        Container::VariableArray<CookedTextureTile> Tiles;
        // 段ごとの Tiles の先頭の番号（FirstTailMip + 1 個。最後は総数）。
        Container::VariableArray<uint32_t> MipFirstTile;
    };

    /**
     * @brief 読み込んだ .nvtex v0 のメタデータと、保持しているソースのバイト列。
     *
     * v0 は 2D テクスチャ配列だけを持つ。ミップのバイト列はミップ番号の昇順、その中でレイヤー番号の昇順に並び、
     * 各レイヤーは行優先で詰める（行の余白なし）。非圧縮の形式は 1 行を width * bytes_per_pixel バイト、
     * ブロック圧縮の形式は 1 行を ceil(width / 4) ブロック分のバイトとし、行数は ceil(height / 4)（最小 1 ブロック）。
     * キューブ・深度・ボリューム・余白付きの行は扱わない。
     */
    struct CookedTextureData
    {
        AssetBlob SourceBlob;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t LayerCount = 0;
        uint32_t MipCount = 0;
        CookedTexturePixelFormat PixelFormat = CookedTexturePixelFormat::R8UNorm;
        CookedTextureColorSpace ColorSpace = CookedTextureColorSpace::Linear;
        uint64_t PayloadHash = 0;
        Container::VariableArray<CookedTextureMip> Mips;
        uint16_t VersionMinor = 0;
        // v0.2 のとき true。Tiling が有効になる。
        bool bTiled = false;
        CookedTextureTiling Tiling;
        // v0.2 を ParseCookedTexture で全体読みしたとき、各段を行優先へ展開したバイト列（ミップ昇順、レイヤー昇順）。
        // 範囲読みの解析（ParseCookedTextureLayout）と bMaterializeRowMajor = false では空。
        Container::TSharedPtr<const AssetBlob::ByteArray> RowMajorStorage;

        /**
         * @brief 段の行優先のバイト列。v0.0・v0.1 はソースの中の範囲、v0.2 は展開済みのバイト列の中の範囲。
         * v0.2 で展開していない（範囲読みの解析・bMaterializeRowMajor = false）ときは空。
         */
        [[nodiscard]] Container::Span<const uint8_t> GetMipBytes(size_t index) const noexcept;

        /**
         * @brief v0.2 のタイルの位置を表から引く。v0.0・v0.1、ミップテイルの段、範囲外は false。
         */
        [[nodiscard]] bool FindTile(uint32_t mipIndex,
                                    uint32_t layerIndex,
                                    uint32_t tileX,
                                    uint32_t tileY,
                                    CookedTextureTile &outTile) const noexcept;
    };

    struct CookedTextureParseResult
    {
        CookedTextureParseStatus Status = CookedTextureParseStatus::InvalidBlob;
        CookedTextureData Texture;

        [[nodiscard]] bool Succeeded() const noexcept { return Status == CookedTextureParseStatus::Success; }
    };

    /**
     * @brief 非圧縮の形式の 1 画素のバイト数。ブロック圧縮の形式と未知の値は 0 を返す。
     */
    [[nodiscard]] constexpr size_t GetCookedTextureBytesPerPixel(CookedTexturePixelFormat pixelFormat) noexcept
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::R8UNorm:
            return 1;
        case CookedTexturePixelFormat::RG8UNorm:
        case CookedTexturePixelFormat::R16UNorm:
            return 2;
        case CookedTexturePixelFormat::RGBA8UNorm:
            return 4;
        case CookedTexturePixelFormat::BC1:
        case CookedTexturePixelFormat::BC4:
        case CookedTexturePixelFormat::BC5:
        case CookedTexturePixelFormat::BC7:
            return 0;
        }
        return 0;
    }

    /**
     * @brief 形式の 1 ブロックの大きさ。非圧縮の形式は 1x1 画素を 1 ブロックとして扱う。未知の値は全て 0。
     */
    struct CookedTextureBlockInfo
    {
        uint32_t BlockWidth = 0;   // ブロックの幅（画素）
        uint32_t BlockHeight = 0;  // ブロックの高さ（画素）
        uint32_t BlockBytes = 0;   // 1 ブロックのバイト数
    };

    [[nodiscard]] constexpr CookedTextureBlockInfo GetCookedTextureBlockInfo(CookedTexturePixelFormat pixelFormat) noexcept
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::BC1:
        case CookedTexturePixelFormat::BC4:
            return {4, 4, 8};
        case CookedTexturePixelFormat::BC5:
        case CookedTexturePixelFormat::BC7:
            return {4, 4, 16};
        case CookedTexturePixelFormat::R8UNorm:
        case CookedTexturePixelFormat::RG8UNorm:
        case CookedTexturePixelFormat::RGBA8UNorm:
        case CookedTexturePixelFormat::R16UNorm:
            return {1, 1, static_cast<uint32_t>(GetCookedTextureBytesPerPixel(pixelFormat))};
        }
        return {};
    }

    [[nodiscard]] constexpr bool IsCookedTextureBlockCompressed(CookedTexturePixelFormat pixelFormat) noexcept
    {
        return GetCookedTextureBlockInfo(pixelFormat).BlockWidth > 1;
    }

    /**
     * @brief 1 レイヤー分・1 ミップの 1 行のバイト数と行数を、ブロック単位（端は切り上げ、最小 1 ブロック）で求める。
     * @return 形式が未知、または幅・高さが 0 のとき false。
     */
    [[nodiscard]] constexpr bool ComputeCookedTextureMipLayout(
        CookedTexturePixelFormat pixelFormat,
        uint32_t width,
        uint32_t height,
        uint64_t &outRowBytes,
        uint64_t &outRowCount) noexcept
    {
        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(pixelFormat);
        if (block.BlockWidth == 0 || block.BlockHeight == 0 || block.BlockBytes == 0 || width == 0 || height == 0)
        {
            return false;
        }

        const uint64_t blocksX = (static_cast<uint64_t>(width) + block.BlockWidth - 1) / block.BlockWidth;
        outRowBytes = blocksX * block.BlockBytes;
        outRowCount = (static_cast<uint64_t>(height) + block.BlockHeight - 1) / block.BlockHeight;
        return true;
    }

    /**
     * @brief 形式の標準ブロック形状（64 KiB のタイル）の大きさ（texel）。Vulkan の標準 sparse イメージブロックと同じ。
     *
     * 1 ブロックのバイト数で決まる: 1B は 256x256、2B は 256x128、4B は 128x128、8B は 128x64、16B は 64x64（いずれも
     * ブロック数）。BC1/BC4（4x4 画素・8B）は 512x256 texel、BC5/BC7（16B）は 256x256 texel になる。未知の形式は 0。
     */
    struct CookedTextureTileShape
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    [[nodiscard]] constexpr CookedTextureTileShape GetCookedTextureStandardTileShape(CookedTexturePixelFormat pixelFormat) noexcept
    {
        const CookedTextureBlockInfo block = GetCookedTextureBlockInfo(pixelFormat);
        switch (block.BlockBytes)
        {
        case 1:
            return {256 * block.BlockWidth, 256 * block.BlockHeight};
        case 2:
            return {256 * block.BlockWidth, 128 * block.BlockHeight};
        case 4:
            return {128 * block.BlockWidth, 128 * block.BlockHeight};
        case 8:
            return {128 * block.BlockWidth, 64 * block.BlockHeight};
        case 16:
            return {64 * block.BlockWidth, 64 * block.BlockHeight};
        default:
            return {};
        }
    }

    [[nodiscard]] constexpr uint32_t ComputeCookedTextureFullMipCount(uint32_t width, uint32_t height) noexcept
    {
        uint32_t maxDimension = width > height ? width : height;
        uint32_t mipCount = 0;
        while (maxDimension > 0)
        {
            ++mipCount;
            maxDimension >>= 1;
        }
        return mipCount;
    }

    /**
     * @brief Computes the .nvtex internal payload_hash over mip payload bytes only.
     *
     * This uses the same FNV-1a64 constants as package payload hashes. Manifest/package cooked
     * hashes cover the complete .nvtex package entry bytes, while this internal hash covers only
     * the contiguous mip payload region declared by the .nvtex header.
     */
    [[nodiscard]] constexpr uint64_t ComputeCookedTexturePayloadHash(const uint8_t *data, size_t size) noexcept
    {
        uint64_t hash = CookedTextureFormatV0::Fnv1a64OffsetBasis;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<uint64_t>(data[index]);
            hash *= CookedTextureFormatV0::Fnv1a64Prime;
        }
        return hash;
    }

    [[nodiscard]] constexpr uint64_t ComputeCookedTexturePayloadHash(Container::Span<const uint8_t> bytes) noexcept
    {
        return ComputeCookedTexturePayloadHash(bytes.data(), bytes.size());
    }

    /**
     * @brief 最初のミップテイルの段。段の幅か高さがタイルより小さい最初の段で、それ以降はテイルに入る。
     *
     * 最後の段（1x1）は必ずタイルより小さいので、テイルは空にならない。v0.2 のクッカーとローダーの共通の規則で、
     * デバイスが返す imageMipTailFirstLod との照合は結び付けの側が行う。
     */
    [[nodiscard]] constexpr uint32_t ComputeCookedTextureFirstTailMip(CookedTexturePixelFormat pixelFormat,
                                                                      uint32_t width,
                                                                      uint32_t height) noexcept
    {
        const CookedTextureTileShape tile = GetCookedTextureStandardTileShape(pixelFormat);
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        for (uint32_t mipIndex = 0; mipIndex < mipCount; ++mipIndex)
        {
            const uint32_t mipWidth = (width >> mipIndex) == 0 ? 1 : (width >> mipIndex);
            const uint32_t mipHeight = (height >> mipIndex) == 0 ? 1 : (height >> mipIndex);
            if (mipWidth < tile.Width || mipHeight < tile.Height)
            {
                return mipIndex;
            }
        }
        return mipCount;
    }

    /**
     * @brief 1 枚のタイルが段の中で占める範囲（ブロック単位）。端のタイルは段の大きさで切り詰める。
     */
    struct CookedTextureTileRect
    {
        uint32_t BlockX = 0;       // 段の中の左端のブロック
        uint32_t BlockY = 0;       // 段の中の上端のブロック
        uint32_t BlocksX = 0;      // 幅（ブロック）
        uint32_t BlocksY = 0;      // 高さ（ブロック）
        uint64_t RowBytes = 0;     // タイルの 1 行のバイト数（余白なし）
        uint64_t DataBytes = 0;    // タイル全体のバイト数
    };

    /**
     * @brief 段の幅・高さからタイルの格子の大きさ（列数・行数）を求める。
     * @return 形式・大きさが不正のとき false。
     */
    [[nodiscard]] bool ComputeCookedTextureTileGrid(CookedTexturePixelFormat pixelFormat,
                                                    uint32_t mipWidth,
                                                    uint32_t mipHeight,
                                                    uint32_t &outTilesX,
                                                    uint32_t &outTilesY) noexcept;

    /**
     * @brief タイル (tileX, tileY) の範囲を求める。格子の外・形式が不正のとき false。
     */
    [[nodiscard]] bool ComputeCookedTextureTileRect(CookedTexturePixelFormat pixelFormat,
                                                    uint32_t mipWidth,
                                                    uint32_t mipHeight,
                                                    uint32_t tileX,
                                                    uint32_t tileY,
                                                    CookedTextureTileRect &outRect) noexcept;

    /**
     * @brief 行優先の 1 レイヤー・1 段（mipLayerBytes。行の余白なし）から、タイルの範囲を行優先で詰めて取り出す。
     * @param outTile 少なくとも rect.DataBytes バイト。
     */
    void GatherCookedTextureTile(CookedTexturePixelFormat pixelFormat,
                                 uint32_t mipWidth,
                                 const CookedTextureTileRect &rect,
                                 const uint8_t *mipLayerBytes,
                                 uint8_t *outTile) noexcept;

    /**
     * @brief GatherCookedTextureTile の逆。タイルのバイト列を、行優先の 1 レイヤー・1 段の範囲へ書き戻す。
     */
    void ScatterCookedTextureTile(CookedTexturePixelFormat pixelFormat,
                                  uint32_t mipWidth,
                                  const CookedTextureTileRect &rect,
                                  const uint8_t *tile,
                                  uint8_t *mipLayerBytes) noexcept;

    /**
     * @brief .nvtex の全体を解析する。v0.0・v0.1・v0.2 を読む。
     *
     * v0.2 は bMaterializeRowMajor が true のとき各段を行優先へ展開し、GetMipBytes が v0.0 と同じに使える。
     * 1 タイルずつ読むストリーマは false にして、FindTile と範囲読みだけを使う。
     */
    [[nodiscard]] CookedTextureParseResult ParseCookedTexture(AssetBlob sourceBlob, bool bMaterializeRowMajor = true);

    /**
     * @brief .nvtex の先頭のバイト列（少なくとも 112 バイト）から、メタデータ（ヘッダ・ミップ表・タイルの表）の
     * バイト数（= ペイロードの先頭）を求める。範囲読みの 1 回目の読みで使う。
     */
    [[nodiscard]] CookedTextureParseStatus GetCookedTextureMetadataSize(Container::Span<const uint8_t> headBytes,
                                                                         uint64_t fileSize,
                                                                         uint64_t &outMetadataSize) noexcept;

    /**
     * @brief メタデータだけ（ペイロードを含まない先頭 PayloadOffset バイト）を解析する。v0.2 ではタイルの表を検証し、
     * FindTile とミップテイルの範囲が使える。ペイロードのハッシュは検証しない（本体を読まないため）。
     * 結果の SourceBlob は無効で、GetMipBytes は空を返す。
     * @param fileSize .nvtex 全体のバイト数（ヘッダの FileSize と一致しなければならない）。
     */
    [[nodiscard]] CookedTextureParseResult ParseCookedTextureLayout(Container::Span<const uint8_t> metadataBytes,
                                                                    uint64_t fileSize);

    /**
     * @brief ファイルを範囲読みして、.nvtex のメタデータを取り出す。本体は読まない。
     * @param baseOffset .nvtex の先頭のファイル内の位置（パッケージのエントリならペイロードの位置、単体なら 0）。
     * @param nvtexSize .nvtex のバイト数。0 ならファイルの大きさから baseOffset を引いた値。
     */
    [[nodiscard]] CookedTextureParseResult ReadCookedTextureLayout(const AssetFileReader &reader,
                                                                   const AssetReadRequest &request,
                                                                   uint64_t baseOffset = 0,
                                                                   uint64_t nvtexSize = 0);

    /**
     * @brief 1 タイルをファイルの範囲読みで取り出す（全体を読まない）。layout は ReadCookedTextureLayout の結果。
     * 結果の Blob は表が示す範囲のバイト列そのもの（CookedTextureTile の説明の並び）。範囲が無い（テイルの段・格子の外・
     * v0.2 でない）ときは Status が InvalidRequest。
     */
    [[nodiscard]] AssetReadResult ReadCookedTextureTile(const AssetFileReader &reader,
                                                        const AssetReadRequest &request,
                                                        uint64_t baseOffset,
                                                        const CookedTextureData &layout,
                                                        uint32_t mipIndex,
                                                        uint32_t layerIndex,
                                                        uint32_t tileX,
                                                        uint32_t tileY);

    /**
     * @brief ミップテイルの塊（FirstTailMip 以降の段を行優先で詰めたもの）をファイルの範囲読みで取り出す。
     */
    [[nodiscard]] AssetReadResult ReadCookedTextureMipTail(const AssetFileReader &reader,
                                                           const AssetReadRequest &request,
                                                           uint64_t baseOffset,
                                                           const CookedTextureData &layout);
}
