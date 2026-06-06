#pragma once

#include "Asset/AssetBlob.h"
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
        inline constexpr uint16_t VersionMinor = 0;
        inline constexpr uint32_t EndianMarker = 0x01020304u;

        inline constexpr uint32_t PixelFormatR8UNorm = 1;
        inline constexpr uint32_t PixelFormatRG8UNorm = 2;
        inline constexpr uint32_t PixelFormatRGBA8UNorm = 3;

        inline constexpr uint32_t ColorSpaceLinear = 1;
        inline constexpr uint32_t ColorSpaceSRGB = 2;

        inline constexpr uint64_t Fnv1a64OffsetBasis = 14695981039346656037ull;
        inline constexpr uint64_t Fnv1a64Prime = 1099511628211ull;
        inline constexpr uint64_t ZeroSizePayloadHash = Fnv1a64OffsetBasis;

        inline constexpr size_t HeaderSize = 112;
        inline constexpr size_t MipRecordSize = 32;

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
        RGBA8UNorm = CookedTextureFormatV0::PixelFormatRGBA8UNorm
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
        TruncatedPayload
    };

    struct CookedTextureMip
    {
        size_t DataOffset = 0;
        size_t DataSize = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    /**
     * @brief Parsed .nvtex v0 metadata and retained source bytes.
     *
     * v0 stores 2D texture arrays only. Mip payload bytes are ordered by mip index ascending,
     * then layer index ascending, with each layer row-major and tightly packed as
     * width * bytes_per_pixel bytes per row. Cube, depth, volume, and padded rows are unsupported.
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

        [[nodiscard]] Container::Span<const uint8_t> GetMipBytes(size_t index) const noexcept;
    };

    struct CookedTextureParseResult
    {
        CookedTextureParseStatus Status = CookedTextureParseStatus::InvalidBlob;
        CookedTextureData Texture;

        [[nodiscard]] bool Succeeded() const noexcept { return Status == CookedTextureParseStatus::Success; }
    };

    [[nodiscard]] constexpr size_t GetCookedTextureBytesPerPixel(CookedTexturePixelFormat pixelFormat) noexcept
    {
        switch (pixelFormat)
        {
        case CookedTexturePixelFormat::R8UNorm:
            return 1;
        case CookedTexturePixelFormat::RG8UNorm:
            return 2;
        case CookedTexturePixelFormat::RGBA8UNorm:
            return 4;
        }
        return 0;
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

    [[nodiscard]] CookedTextureParseResult ParseCookedTexture(AssetBlob sourceBlob);
}
