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
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC1) == PixelFormatBC1);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC4) == PixelFormatBC4);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC5) == PixelFormatBC5);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::BC7) == PixelFormatBC7);
    static_assert(static_cast<uint32_t>(CookedTexturePixelFormat::R16UNorm) == PixelFormatR16UNorm);
    static_assert(VersionMinor == 0 && VersionMinorBlockCompressed == 1);
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
        WriteLe16(bytes, HeaderOffset::VersionMinor, 2);
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

    std::cout << "CookedTextureTest passed\n";
    return 0;
}
