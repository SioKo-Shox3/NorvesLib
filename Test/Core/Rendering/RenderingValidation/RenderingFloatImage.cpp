#include "RenderingValidation/RenderingFloatImage.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        constexpr uint32_t RgbaChannelCount = 4u;
        constexpr uint32_t Rgba16BytesPerPixel = 8u;
    }

    float DecodeIeee754Binary16(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16u;
        uint32_t exponent = (bits >> 10u) & 0x1Fu;
        uint32_t mantissa = bits & 0x03FFu;
        uint32_t floatBits = sign;

        if (exponent == 0u)
        {
            if (mantissa != 0u)
            {
                int32_t unbiasedExponent = -14;
                while ((mantissa & 0x0400u) == 0u)
                {
                    mantissa <<= 1u;
                    --unbiasedExponent;
                }
                mantissa &= 0x03FFu;
                floatBits |= static_cast<uint32_t>(unbiasedExponent + 127) << 23u;
                floatBits |= mantissa << 13u;
            }
        }
        else if (exponent == 0x1Fu)
        {
            floatBits |= 0x7F800000u;
            if (mantissa != 0u)
            {
                floatBits |= mantissa << 13u;
                floatBits |= 0x00400000u;
            }
        }
        else
        {
            exponent += 127u - 15u;
            floatBits |= exponent << 23u;
            floatBits |= mantissa << 13u;
        }

        return std::bit_cast<float>(floatBits);
    }

    FloatImageStatus DecodeCapturedRgba16Float(
        const Core::Rendering::CapturedFrame& frame,
        RgbaFloatImage& outImage)
    {
        outImage = RgbaFloatImage{};

        if (!frame.IsSuccess())
        {
            return FloatImageStatus::CaptureNotSuccessful;
        }
        if (frame.Format != RHI::Format::R16G16B16A16_FLOAT)
        {
            return FloatImageStatus::UnsupportedFormat;
        }
        if (frame.Width == 0u || frame.Height == 0u ||
            frame.Width > std::numeric_limits<uint32_t>::max() / Rgba16BytesPerPixel)
        {
            return FloatImageStatus::InvalidDimensions;
        }

        const size_t width = static_cast<size_t>(frame.Width);
        const size_t height = static_cast<size_t>(frame.Height);
        if (width > std::numeric_limits<size_t>::max() / RgbaChannelCount ||
            height > std::numeric_limits<size_t>::max() / (width * RgbaChannelCount))
        {
            return FloatImageStatus::InvalidDimensions;
        }
        if (frame.BytesPerPixel != Rgba16BytesPerPixel)
        {
            return FloatImageStatus::InvalidPixelData;
        }

        const uint32_t minimumRowPitch = frame.Width * Rgba16BytesPerPixel;
        if (frame.RowPitchBytes < minimumRowPitch ||
            height > std::numeric_limits<size_t>::max() / frame.RowPitchBytes)
        {
            return FloatImageStatus::InvalidPixelData;
        }
        const size_t requiredPixelBytes = static_cast<size_t>(frame.RowPitchBytes) * height;
        if (frame.Pixels.size() < requiredPixelBytes)
        {
            return FloatImageStatus::InvalidPixelData;
        }

        outImage.Width = frame.Width;
        outImage.Height = frame.Height;
        outImage.Values.reserve(width * height * RgbaChannelCount);
        for (uint32_t y = 0; y < frame.Height; ++y)
        {
            const uint8_t* row = frame.Pixels.data() + static_cast<size_t>(y) * frame.RowPitchBytes;
            for (uint32_t x = 0; x < frame.Width; ++x)
            {
                const size_t pixelOffset = static_cast<size_t>(x) * Rgba16BytesPerPixel;
                for (uint32_t channel = 0; channel < RgbaChannelCount; ++channel)
                {
                    const size_t channelOffset = pixelOffset + static_cast<size_t>(channel) * 2u;
                    const uint16_t channelBits = static_cast<uint16_t>(row[channelOffset]) |
                        (static_cast<uint16_t>(row[channelOffset + 1u]) << 8u);
                    outImage.Values.push_back(DecodeIeee754Binary16(channelBits));
                }
            }
        }
        return FloatImageStatus::Success;
    }

    NonFiniteLocation FindFirstNonFinite(const RgbaFloatImage& image)
    {
        if (image.Width == 0u)
        {
            return {};
        }

        for (size_t index = 0; index < image.Values.size(); ++index)
        {
            const float value = image.Values[index];
            NonFiniteKind kind = NonFiniteKind::None;
            if (std::isnan(value))
            {
                kind = NonFiniteKind::NaN;
            }
            else if (std::isinf(value))
            {
                kind = std::signbit(value)
                    ? NonFiniteKind::NegativeInfinity
                    : NonFiniteKind::PositiveInfinity;
            }

            if (kind != NonFiniteKind::None)
            {
                const size_t pixelIndex = index / RgbaChannelCount;
                NonFiniteLocation location;
                location.Kind = kind;
                location.X = static_cast<uint32_t>(pixelIndex % image.Width);
                location.Y = static_cast<uint32_t>(pixelIndex / image.Width);
                location.Channel = static_cast<uint32_t>(index % RgbaChannelCount);
                return location;
            }
        }
        return {};
    }

    bool IsFiniteImage(const RgbaFloatImage& image)
    {
        return FindFirstNonFinite(image).Kind == NonFiniteKind::None;
    }
}
