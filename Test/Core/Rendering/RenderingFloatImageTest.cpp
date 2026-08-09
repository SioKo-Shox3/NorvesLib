#include "RenderingValidation/RenderingFloatImage.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr uint32_t Rgba16BytesPerPixel = 8u;
    constexpr uint32_t ChannelCount = 4u;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "RenderingFloatImageTest failed: " << message << '\n';
            std::abort();
        }
    }

    Core::Rendering::CapturedFrame MakeRgba16Frame(
        uint32_t width,
        uint32_t height,
        uint32_t rowPitchBytes,
        const uint16_t* values)
    {
        Core::Rendering::CapturedFrame frame;
        frame.Status = Core::Rendering::FrameCaptureResultStatus::Success;
        frame.Width = width;
        frame.Height = height;
        frame.Format = RHI::Format::R16G16B16A16_FLOAT;
        frame.BytesPerPixel = Rgba16BytesPerPixel;
        frame.RowPitchBytes = rowPitchBytes;
        frame.Pixels.resize(static_cast<size_t>(rowPitchBytes) * height, 0xCDu);

        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                for (uint32_t channel = 0; channel < ChannelCount; ++channel)
                {
                    const size_t valueIndex =
                        (static_cast<size_t>(y) * width + x) * ChannelCount + channel;
                    const size_t byteOffset =
                        static_cast<size_t>(y) * rowPitchBytes + x * Rgba16BytesPerPixel + channel * 2u;
                    const uint16_t bits = values[valueIndex];
                    frame.Pixels[byteOffset] = static_cast<uint8_t>(bits & 0xFFu);
                    frame.Pixels[byteOffset + 1u] = static_cast<uint8_t>(bits >> 8u);
                }
            }
        }
        return frame;
    }

    Core::Rendering::CapturedFrame MakeOnePixelFrame(uint16_t firstChannel)
    {
        const uint16_t values[] = {firstChannel, 0x3C00u, 0x3C00u, 0x3C00u};
        return MakeRgba16Frame(1u, 1u, Rgba16BytesPerPixel, values);
    }

    void TestBinary16KnownValues()
    {
        Require(DecodeIeee754Binary16(0x0000u) == 0.0f, "positive zero must decode exactly");
        Require(DecodeIeee754Binary16(0x0001u) == std::ldexp(1.0f, -24),
                "smallest subnormal must decode exactly");
        Require(DecodeIeee754Binary16(0x3C00u) == 1.0f, "one must decode exactly");
        Require(DecodeIeee754Binary16(0xC000u) == -2.0f, "negative normal must decode exactly");
        Require(DecodeIeee754Binary16(0x7BFFu) == 65504.0f, "maximum finite value must decode exactly");

        const float negativeZero = DecodeIeee754Binary16(0x8000u);
        Require(negativeZero == 0.0f && std::signbit(negativeZero),
                "negative zero must preserve its sign");
        Require(std::isnan(DecodeIeee754Binary16(0x7E00u)), "NaN must remain NaN");

        const float positiveInfinity = DecodeIeee754Binary16(0x7C00u);
        Require(std::isinf(positiveInfinity) && !std::signbit(positiveInfinity),
                "positive infinity must preserve its sign");
        const float negativeInfinity = DecodeIeee754Binary16(0xFC00u);
        Require(std::isinf(negativeInfinity) && std::signbit(negativeInfinity),
                "negative infinity must preserve its sign");
    }

    void TestDecodeHonorsRowPitch()
    {
        constexpr uint16_t Values[] = {
            0x0000u, 0x3C00u, 0xC000u, 0x7BFFu,
            0x3800u, 0xB800u, 0x4000u, 0x4400u,
            0x3C00u, 0x4000u, 0x4200u, 0x4400u,
            0xBC00u, 0xC000u, 0xC200u, 0xC400u};
        const Core::Rendering::CapturedFrame frame = MakeRgba16Frame(2u, 2u, 20u, Values);

        RgbaFloatImage image;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::Success,
                "padded rows must decode");
        Require(image.Width == 2u && image.Height == 2u && image.Values.size() == 16u,
                "decoded dimensions must match the capture");
        constexpr float Expected[] = {
            0.0f, 1.0f, -2.0f, 65504.0f,
            0.5f, -0.5f, 2.0f, 4.0f,
            1.0f, 2.0f, 3.0f, 4.0f,
            -1.0f, -2.0f, -3.0f, -4.0f};
        for (size_t index = 0; index < image.Values.size(); ++index)
        {
            Require(image.Values[index] == Expected[index], "decoded channel value must match");
        }
        Require(IsFiniteImage(image), "known finite image must be accepted");
    }

    void TestDecodeRejectsMalformedCaptures()
    {
        const uint16_t values[] = {0x0000u, 0x3C00u, 0x4000u, 0x4200u};
        RgbaFloatImage image;

        Core::Rendering::CapturedFrame frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.Status = Core::Rendering::FrameCaptureResultStatus::MapFailed;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::CaptureNotSuccessful,
                "failed captures must be rejected");

        frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.Format = RHI::Format::R16_FLOAT;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::UnsupportedFormat,
                "R16_FLOAT must not be accepted");
        frame.Format = RHI::Format::R32G32B32A32_FLOAT;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::UnsupportedFormat,
                "R32G32B32A32_FLOAT must not be accepted");

        frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.Width = 0u;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidDimensions,
                "zero width must be rejected");
        frame.Width = std::numeric_limits<uint32_t>::max();
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidDimensions,
                "row-size overflow must be rejected");

        frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.BytesPerPixel = 4u;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidPixelData,
                "wrong bytes-per-pixel must be rejected");
        frame.BytesPerPixel = 8u;
        frame.RowPitchBytes = 7u;
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidPixelData,
                "short row pitch must be rejected");

        frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.Pixels.resize(7u);
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidPixelData,
                "short pixel storage must be rejected");

        frame = MakeRgba16Frame(1u, 1u, 8u, values);
        frame.Height = std::numeric_limits<uint32_t>::max();
        frame.RowPitchBytes = std::numeric_limits<uint32_t>::max();
        Require(DecodeCapturedRgba16Float(frame, image) == FloatImageStatus::InvalidPixelData,
                "overflow-safe required storage validation must reject impossible storage");
        Require(image.Width == 0u && image.Height == 0u && image.Values.empty(),
                "failed decode must clear the output image");
    }

    void TestNonFiniteClassification()
    {
        RgbaFloatImage image;
        Require(DecodeCapturedRgba16Float(MakeOnePixelFrame(0x7E00u), image) == FloatImageStatus::Success,
                "NaN fixture must decode");
        Require(!IsFiniteImage(image), "NaN image must be rejected");
        NonFiniteLocation location = FindFirstNonFinite(image);
        Require(location.Kind == NonFiniteKind::NaN && location.X == 0u && location.Y == 0u &&
                    location.Channel == 0u,
                "NaN location must identify the R channel");

        Require(DecodeCapturedRgba16Float(MakeOnePixelFrame(0x7C00u), image) == FloatImageStatus::Success,
                "positive infinity fixture must decode");
        Require(!IsFiniteImage(image), "positive infinity image must be rejected");
        location = FindFirstNonFinite(image);
        Require(location.Kind == NonFiniteKind::PositiveInfinity,
                "positive infinity must be classified separately");

        Require(DecodeCapturedRgba16Float(MakeOnePixelFrame(0xFC00u), image) == FloatImageStatus::Success,
                "negative infinity fixture must decode");
        Require(!IsFiniteImage(image), "negative infinity image must be rejected");
        location = FindFirstNonFinite(image);
        Require(location.Kind == NonFiniteKind::NegativeInfinity,
                "negative infinity must be classified separately");
    }

    void TestNonFiniteScanOrder()
    {
        RgbaFloatImage image;
        image.Width = 2u;
        image.Height = 1u;
        image.Values.resize(8u, 1.0f);
        image.Values[3u] = DecodeIeee754Binary16(0x7C00u);
        image.Values[4u] = DecodeIeee754Binary16(0x7E00u);

        NonFiniteLocation location = FindFirstNonFinite(image);
        Require(location.Kind == NonFiniteKind::PositiveInfinity && location.X == 0u &&
                    location.Y == 0u && location.Channel == 3u,
                "scan must finish RGBA channels before advancing to the next pixel");

        image.Values[3u] = 1.0f;
        location = FindFirstNonFinite(image);
        Require(location.Kind == NonFiniteKind::NaN && location.X == 1u &&
                    location.Y == 0u && location.Channel == 0u,
                "scan must advance in row-major pixel order");
    }
}

int main()
{
    TestBinary16KnownValues();
    TestDecodeHonorsRowPitch();
    TestDecodeRejectsMalformedCaptures();
    TestNonFiniteClassification();
    TestNonFiniteScanOrder();
    std::cout << "known_float=green nan_negative=rejected positive_inf_negative=rejected "
                 "negative_inf_negative=rejected\n";
    return 0;
}
