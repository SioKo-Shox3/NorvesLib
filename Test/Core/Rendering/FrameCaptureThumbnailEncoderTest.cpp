#include "CoreTypes.h"
#include "Rendering/FrameCaptureThumbnailEncoder.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

#include "stb_image.h"

namespace NorvesLib::Core::Rendering
{
namespace
{
    using Container::VariableArray;

    constexpr uint32_t BytesPerPixel = 4;

    struct DecodedImage
    {
        int Width = 0;
        int Height = 0;
        VariableArray<uint8_t> Pixels;
    };

    uint32_t ReadBigEndianU32(const VariableArray<uint8_t>& bytes, size_t offset)
    {
        assert(offset + 4u <= bytes.size());
        return (static_cast<uint32_t>(bytes[offset]) << 24u)
             | (static_cast<uint32_t>(bytes[offset + 1u]) << 16u)
             | (static_cast<uint32_t>(bytes[offset + 2u]) << 8u)
             | static_cast<uint32_t>(bytes[offset + 3u]);
    }

    CapturedFrame MakeFrame(
        uint32_t width,
        uint32_t height,
        RHI::Format format,
        uint32_t rowPitchBytes,
        const VariableArray<uint8_t>& pixels)
    {
        CapturedFrame frame;
        frame.Status = FrameCaptureResultStatus::Success;
        frame.RequestId = 17;
        frame.FrameNumber = 29;
        frame.Width = width;
        frame.Height = height;
        frame.Format = format;
        frame.BytesPerPixel = BytesPerPixel;
        frame.RowPitchBytes = rowPitchBytes;
        frame.Pixels = pixels;
        return frame;
    }

    void AssertPngSignatureAndIhdr(const FrameCaptureThumbnailResult& result)
    {
        const uint8_t expectedSignature[] = { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
        assert(result.PngBytes.size() > 24u);
        assert(std::memcmp(result.PngBytes.data(), expectedSignature, sizeof(expectedSignature)) == 0);
        assert(ReadBigEndianU32(result.PngBytes, 16u) == result.Width);
        assert(ReadBigEndianU32(result.PngBytes, 20u) == result.Height);
    }

    DecodedImage DecodePng(const VariableArray<uint8_t>& bytes)
    {
        assert(bytes.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));

        DecodedImage decoded;
        int sourceChannels = 0;
        unsigned char* pixels = stbi_load_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &decoded.Width,
            &decoded.Height,
            &sourceChannels,
            4);
        assert(pixels != nullptr);
        assert(decoded.Width > 0);
        assert(decoded.Height > 0);

        const size_t pixelByteCount = static_cast<size_t>(decoded.Width) * static_cast<size_t>(decoded.Height) * BytesPerPixel;
        decoded.Pixels.resize(pixelByteCount);
        std::memcpy(decoded.Pixels.data(), pixels, pixelByteCount);
        stbi_image_free(pixels);
        return decoded;
    }

    void AssertPixel(const DecodedImage& image, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        assert(x < static_cast<uint32_t>(image.Width));
        assert(y < static_cast<uint32_t>(image.Height));
        const size_t pixelOffset = (static_cast<size_t>(y) * static_cast<size_t>(image.Width) + static_cast<size_t>(x)) * BytesPerPixel;
        assert(image.Pixels[pixelOffset + 0u] == r);
        assert(image.Pixels[pixelOffset + 1u] == g);
        assert(image.Pixels[pixelOffset + 2u] == b);
        assert(image.Pixels[pixelOffset + 3u] == a);
    }

    void TestRgbaSuccessTightRowPitch()
    {
        const VariableArray<uint8_t> pixels =
        {
            255, 0, 0, 255,      0, 255, 0, 255,
            0, 0, 255, 255,      255, 255, 0, 128
        };
        const CapturedFrame frame = MakeFrame(2, 2, RHI::Format::R8G8B8A8_UNORM, 2u * BytesPerPixel, pixels);

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame);

        assert(result.IsSuccess());
        assert(result.Status == FrameCaptureThumbnailStatus::Success);
        assert(result.RequestId == frame.RequestId);
        assert(result.FrameNumber == frame.FrameNumber);
        assert(result.Width == 2);
        assert(result.Height == 2);
        assert(result.SourceFormat == frame.Format);
        assert(std::strcmp(GetFrameCaptureThumbnailMimeType(), "image/png") == 0);
        AssertPngSignatureAndIhdr(result);

        const DecodedImage decoded = DecodePng(result.PngBytes);
        assert(decoded.Width == 2);
        assert(decoded.Height == 2);
        assert(decoded.Pixels == pixels);
    }

    void TestBgraConversionWithPaddedRowPitch()
    {
        const VariableArray<uint8_t> pixels =
        {
            30, 20, 10, 255,     70, 60, 50, 128,     99, 98, 97, 96
        };
        const CapturedFrame frame = MakeFrame(2, 1, RHI::Format::B8G8R8A8_SRGB, 12, pixels);

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame);

        assert(result.IsSuccess());
        AssertPngSignatureAndIhdr(result);
        const DecodedImage decoded = DecodePng(result.PngBytes);
        assert(decoded.Width == 2);
        assert(decoded.Height == 1);
        AssertPixel(decoded, 0, 0, 10, 20, 30, 255);
        AssertPixel(decoded, 1, 0, 50, 60, 70, 128);
    }

    void TestBoxAverageDownscaleIsDeterministic()
    {
        VariableArray<uint8_t> pixels;
        pixels.resize(4u * 4u * BytesPerPixel);
        for (uint32_t y = 0; y < 4u; ++y)
        {
            for (uint32_t x = 0; x < 4u; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * 4u + x) * BytesPerPixel;
                pixels[offset + 0u] = static_cast<uint8_t>(x * 10u);
                pixels[offset + 1u] = static_cast<uint8_t>(y * 20u);
                pixels[offset + 2u] = static_cast<uint8_t>((x + y) * 5u);
                pixels[offset + 3u] = 255u;
            }
        }
        const CapturedFrame frame = MakeFrame(4, 4, RHI::Format::R8G8B8A8_SRGB, 4u * BytesPerPixel, pixels);
        FrameCaptureThumbnailOptions options;
        options.MaxWidth = 2;
        options.MaxHeight = 2;

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame, options);

        assert(result.IsSuccess());
        assert(result.Width == 2);
        assert(result.Height == 2);
        const DecodedImage decoded = DecodePng(result.PngBytes);
        AssertPixel(decoded, 0, 0, 5, 10, 5, 255);
        AssertPixel(decoded, 1, 0, 25, 10, 15, 255);
        AssertPixel(decoded, 0, 1, 5, 50, 15, 255);
        AssertPixel(decoded, 1, 1, 25, 50, 25, 255);
    }

    void TestHardCapDefaultsTo640By360()
    {
        VariableArray<uint8_t> pixels;
        pixels.resize(1280u * 720u * BytesPerPixel, 64u);
        for (size_t alphaOffset = 3u; alphaOffset < pixels.size(); alphaOffset += BytesPerPixel)
        {
            pixels[alphaOffset] = 255u;
        }
        const CapturedFrame frame = MakeFrame(1280, 720, RHI::Format::R8G8B8A8_UNORM, 1280u * BytesPerPixel, pixels);

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame);

        assert(result.IsSuccess());
        assert(result.Width == FrameCaptureThumbnailHardMaxWidth);
        assert(result.Height == FrameCaptureThumbnailHardMaxHeight);
        AssertPngSignatureAndIhdr(result);
    }

    void TestCallerCapsPreserveAspectAndNeverUpscale()
    {
        VariableArray<uint8_t> pixels;
        pixels.resize(800u * 600u * BytesPerPixel, 120u);
        const CapturedFrame frame = MakeFrame(800, 600, RHI::Format::R8G8B8A8_UNORM, 800u * BytesPerPixel, pixels);
        FrameCaptureThumbnailOptions options;
        options.MaxWidth = 320;
        options.MaxHeight = 100;

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame, options);

        assert(result.IsSuccess());
        assert(result.Width <= 320);
        assert(result.Height <= 100);
        assert(result.Width == 133);
        assert(result.Height == 100);

        VariableArray<uint8_t> smallPixels;
        smallPixels.resize(3u * 2u * BytesPerPixel, 200u);
        const CapturedFrame smallFrame = MakeFrame(3, 2, RHI::Format::R8G8B8A8_UNORM, 3u * BytesPerPixel, smallPixels);
        FrameCaptureThumbnailOptions largeOptions;
        largeOptions.MaxWidth = 400;
        largeOptions.MaxHeight = 300;

        const FrameCaptureThumbnailResult smallResult = EncodeCapturedFrameThumbnail(smallFrame, largeOptions);

        assert(smallResult.IsSuccess());
        assert(smallResult.Width == 3);
        assert(smallResult.Height == 2);
    }

    void TestEncodedByteFallbackShrinksAndSucceeds()
    {
        VariableArray<uint8_t> pixels;
        pixels.resize(256u * 256u * BytesPerPixel);
        uint32_t state = 0x12345678u;
        for (size_t index = 0; index < pixels.size(); ++index)
        {
            state = state * 1664525u + 1013904223u;
            pixels[index] = static_cast<uint8_t>((state >> 24u) & 0xFFu);
        }
        const CapturedFrame frame = MakeFrame(256, 256, RHI::Format::R8G8B8A8_UNORM, 256u * BytesPerPixel, pixels);
        FrameCaptureThumbnailOptions options;
        options.MaxWidth = 256;
        options.MaxHeight = 256;
        options.MaxEncodedBytes = 64u * 1024u;

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame, options);

        assert(result.IsSuccess());
        assert(result.PngBytes.size() <= options.MaxEncodedBytes);
        assert(result.Width < frame.Width || result.Height < frame.Height);
        AssertPngSignatureAndIhdr(result);
    }

    void TestEncodedBytesTooLargeFailure()
    {
        const VariableArray<uint8_t> pixels =
        {
            1, 2, 3, 255
        };
        const CapturedFrame frame = MakeFrame(1, 1, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        FrameCaptureThumbnailOptions options;
        options.MaxEncodedBytes = 1;

        const FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame, options);

        assert(!result.IsSuccess());
        assert(result.Status == FrameCaptureThumbnailStatus::EncodedBytesTooLarge);
        assert(result.PngBytes.empty());
    }

    void TestInvalidInputs()
    {
        const VariableArray<uint8_t> pixels =
        {
            1, 2, 3, 4
        };

        CapturedFrame frame = MakeFrame(1, 1, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        frame.Status = FrameCaptureResultStatus::SourceUnavailable;
        FrameCaptureThumbnailResult result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::CapturedFrameNotReady);
        assert(result.RequestId == frame.RequestId);
        assert(result.FrameNumber == frame.FrameNumber);
        assert(result.SourceFormat == frame.Format);
        assert(result.PngBytes.empty());

        frame = MakeFrame(1, 1, RHI::Format::R16_FLOAT, BytesPerPixel, pixels);
        result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::UnsupportedFormat);

        frame = MakeFrame(0, 1, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidDimensions);

        frame = MakeFrame(1, 1, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        frame.BytesPerPixel = 3;
        result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::UnsupportedFormat);

        frame = MakeFrame(2, 1, RHI::Format::R8G8B8A8_UNORM, 4, pixels);
        result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidPixelData);

        frame = MakeFrame(1, 2, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        result = EncodeCapturedFrameThumbnail(frame);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidPixelData);

        frame = MakeFrame(1, 1, RHI::Format::R8G8B8A8_UNORM, BytesPerPixel, pixels);
        FrameCaptureThumbnailOptions options;
        options.MaxWidth = 0;
        result = EncodeCapturedFrameThumbnail(frame, options);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidOptions);

        options = {};
        options.MaxHeight = 0;
        result = EncodeCapturedFrameThumbnail(frame, options);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidOptions);

        options = {};
        options.MaxEncodedBytes = 0;
        result = EncodeCapturedFrameThumbnail(frame, options);
        assert(result.Status == FrameCaptureThumbnailStatus::InvalidOptions);
    }
} // namespace

} // namespace NorvesLib::Core::Rendering

int main()
{
    using namespace NorvesLib::Core::Rendering;

    TestRgbaSuccessTightRowPitch();
    TestBgraConversionWithPaddedRowPitch();
    TestBoxAverageDownscaleIsDeterministic();
    TestHardCapDefaultsTo640By360();
    TestCallerCapsPreserveAspectAndNeverUpscale();
    TestEncodedByteFallbackShrinksAndSucceeds();
    TestEncodedBytesTooLargeFailure();
    TestInvalidInputs();

    std::cout << "FrameCaptureThumbnailEncoderTest passed\n";
    return 0;
}
