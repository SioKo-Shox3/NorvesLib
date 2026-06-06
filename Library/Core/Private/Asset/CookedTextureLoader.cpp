#include "Asset/CookedTextureFormat.h"

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

        bool IsKnownPixelFormat(uint32_t rawPixelFormat)
        {
            return rawPixelFormat == CookedTextureFormatV0::PixelFormatR8UNorm ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatRG8UNorm ||
                   rawPixelFormat == CookedTextureFormatV0::PixelFormatRGBA8UNorm;
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
                   pixelFormat == CookedTexturePixelFormat::RGBA8UNorm;
        }

        bool ComputeExpectedMipPayloadSize(uint32_t width,
                                           uint32_t height,
                                           uint32_t layerCount,
                                           size_t bytesPerPixel,
                                           uint64_t &outSize)
        {
            uint64_t value = width;
            if (!MultiplyChecked64(value, height, value) ||
                !MultiplyChecked64(value, layerCount, value) ||
                !MultiplyChecked64(value, bytesPerPixel, value))
            {
                return false;
            }

            outSize = value;
            return true;
        }
    }

    Container::Span<const uint8_t> CookedTextureData::GetMipBytes(size_t index) const noexcept
    {
        if (index >= Mips.size() || !SourceBlob.IsValid())
        {
            return {};
        }

        const CookedTextureMip &mip = Mips[index];
        const Container::Span<const uint8_t> bytes = SourceBlob.GetSpan();
        if (mip.DataOffset > bytes.size() || mip.DataSize > bytes.size() - mip.DataOffset)
        {
            return {};
        }

        return Container::Span<const uint8_t>(bytes.data() + mip.DataOffset, mip.DataSize);
    }

    CookedTextureParseResult ParseCookedTexture(AssetBlob sourceBlob)
    {
        using namespace CookedTextureFormatV0;

        if (!sourceBlob.IsValid())
        {
            return Fail(CookedTextureParseStatus::InvalidBlob);
        }

        const Container::Span<const uint8_t> bytes = sourceBlob.GetSpan();
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

        if (versionMajor != VersionMajor || versionMinor != VersionMinor)
        {
            return Fail(CookedTextureParseStatus::UnsupportedVersion);
        }

        if (endianMarker != EndianMarker)
        {
            return Fail(CookedTextureParseStatus::EndianMismatch);
        }

        if (headerSize != HeaderSize)
        {
            return Fail(CookedTextureParseStatus::HeaderSizeMismatch);
        }

        if (mipRecordSize != MipRecordSize)
        {
            return Fail(CookedTextureParseStatus::MipRecordSizeMismatch);
        }

        if (declaredFileSize != static_cast<uint64_t>(bytes.size()))
        {
            return Fail(CookedTextureParseStatus::FileSizeMismatch);
        }

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

        if (!IsKnownPixelFormat(rawPixelFormat))
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
        if (!ConvertRange(mipTableOffset64, mipTableSize64, bytes.size(), mipTableOffset, mipTableSize) ||
            mipTableOffset < HeaderSize)
        {
            return Fail(CookedTextureParseStatus::MipTableOutOfRange);
        }

        size_t mipTableEnd = 0;
        if (!AddChecked(mipTableOffset, mipTableSize, mipTableEnd))
        {
            return Fail(CookedTextureParseStatus::IntegerOverflow);
        }

        size_t payloadOffset = 0;
        size_t payloadSize = 0;
        if (!ConvertRange(payloadOffset64, payloadSize64, bytes.size(), payloadOffset, payloadSize))
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

        const size_t bytesPerPixel = GetCookedTextureBytesPerPixel(pixelFormat);
        size_t expectedPayloadCursor = payloadOffset;
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
            if (!ComputeExpectedMipPayloadSize(mipWidth, mipHeight, layerCount, bytesPerPixel, expectedDataSize64))
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
            if (!ConvertRange(dataOffset64, dataSize64, bytes.size(), dataOffset, dataSize))
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
            mips.push_back(mip);

            expectedPayloadCursor += dataSize;
        }

        if (expectedPayloadCursor != payloadEnd)
        {
            return Fail(CookedTextureParseStatus::MipPackingMismatch);
        }

        const uint64_t computedPayloadHash = ComputeCookedTexturePayloadHash(data + payloadOffset, payloadSize);
        if (computedPayloadHash != payloadHash)
        {
            return Fail(CookedTextureParseStatus::PayloadHashMismatch);
        }

        CookedTextureParseResult result;
        result.Status = CookedTextureParseStatus::Success;
        result.Texture.SourceBlob = std::move(sourceBlob);
        result.Texture.Width = width;
        result.Texture.Height = height;
        result.Texture.LayerCount = layerCount;
        result.Texture.MipCount = mipCount;
        result.Texture.PixelFormat = pixelFormat;
        result.Texture.ColorSpace = colorSpace;
        result.Texture.PayloadHash = payloadHash;
        result.Texture.Mips = std::move(mips);
        return result;
    }
}
