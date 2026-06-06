#include "Asset/CookedTextureFormat.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
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

    std::vector<uint8_t> BuildTexture(uint32_t width,
                                      uint32_t height,
                                      uint32_t layerCount,
                                      CookedTexturePixelFormat pixelFormat,
                                      CookedTextureColorSpace colorSpace)
    {
        const uint32_t mipCount = ComputeCookedTextureFullMipCount(width, height);
        const size_t bytesPerPixel = GetCookedTextureBytesPerPixel(pixelFormat);
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
            const size_t dataSize = static_cast<size_t>(mipWidth) *
                                    static_cast<size_t>(mipHeight) *
                                    static_cast<size_t>(layerCount) *
                                    bytesPerPixel;
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
        WriteLe16(bytes, HeaderOffset::VersionMinor, VersionMinor);
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

    std::cout << "CookedTextureTest passed\n";
    return 0;
}
