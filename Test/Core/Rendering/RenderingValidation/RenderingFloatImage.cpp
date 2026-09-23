#include "RenderingValidation/RenderingFloatImage.h"

#include "FileStream/FileStream.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        constexpr uint32_t RgbaChannelCount = 4u;
        constexpr uint32_t Rgba16BytesPerPixel = 8u;
        constexpr float Rgba16MaxFinite = 65504.0f;
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

    uint16_t EncodeIeee754Binary16Rne(float value)
    {
        const uint32_t floatBits = std::bit_cast<uint32_t>(value);
        const uint16_t sign = static_cast<uint16_t>((floatBits >> 16u) & 0x8000u);
        const uint32_t exponent = (floatBits >> 23u) & 0xFFu;
        const uint32_t mantissa = floatBits & 0x007FFFFFu;
        if (exponent == 0xFFu)
        {
            if (mantissa != 0u)
            {
                return 0x7E00u;
            }
            return static_cast<uint16_t>(sign | 0x7C00u);
        }

        const int32_t halfExponent = static_cast<int32_t>(exponent) - 127 + 15;
        if (halfExponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7C00u);
        }

        if (halfExponent <= 0)
        {
            if (halfExponent < -10)
            {
                return sign;
            }

            const uint32_t significand = mantissa | 0x00800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - halfExponent);
            uint32_t rounded = significand >> shift;
            const uint32_t remainderMask = (1u << shift) - 1u;
            const uint32_t remainder = significand & remainderMask;
            const uint32_t halfway = 1u << (shift - 1u);
            if (remainder > halfway ||
                (remainder == halfway && (rounded & 1u) != 0u))
            {
                ++rounded;
            }
            if (rounded >= 0x0400u)
            {
                return static_cast<uint16_t>(sign | 0x0400u);
            }
            return static_cast<uint16_t>(sign | rounded);
        }

        uint32_t roundedMantissa = mantissa >> 13u;
        const uint32_t remainder = mantissa & 0x1FFFu;
        if (remainder > 0x1000u ||
            (remainder == 0x1000u && (roundedMantissa & 1u) != 0u))
        {
            ++roundedMantissa;
        }

        int32_t roundedExponent = halfExponent;
        if (roundedMantissa >= 0x0400u)
        {
            roundedMantissa = 0u;
            ++roundedExponent;
        }
        if (roundedExponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7C00u);
        }
        return static_cast<uint16_t>(sign |
                                     (static_cast<uint32_t>(roundedExponent) << 10u) |
                                     roundedMantissa);
    }

    bool ValidateIeee754Binary16RneTable()
    {
        struct RneCase
        {
            float Value;
            uint16_t ExpectedBits;
        };

        const RneCase cases[] = {
            {0.0f, 0x0000u},
            {-0.0f, 0x8000u},
            {1.0f, 0x3C00u},
            {-2.0f, 0xC000u},
            {65504.0f, 0x7BFFu},
            {std::ldexp(1.0f, -24), 0x0001u},
            {std::ldexp(1.0f, -25), 0x0000u},
            {std::ldexp(2047.0f, -25), 0x0400u},
            {1.0f + std::ldexp(1.0f, -11), 0x3C00u},
            {1.0f + 3.0f * std::ldexp(1.0f, -11), 0x3C02u},
            {65520.0f, 0x7C00u},
            {std::numeric_limits<float>::infinity(), 0x7C00u},
            {-std::numeric_limits<float>::infinity(), 0xFC00u}};
        for (const RneCase& testCase : cases)
        {
            if (EncodeIeee754Binary16Rne(testCase.Value) != testCase.ExpectedBits)
            {
                return false;
            }
        }
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float negativeNan = std::bit_cast<float>(0xFFC00001u);
        return EncodeIeee754Binary16Rne(nan) == 0x7E00u &&
               EncodeIeee754Binary16Rne(negativeNan) == 0x7E00u;
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

    FloatImageStatus DecodeCapturedRgbaFloat(
        const Core::Rendering::CapturedFrame& frame,
        RgbaFloatImage& outImage)
    {
        if (frame.Format != RHI::Format::R32G32B32A32_FLOAT)
        {
            return DecodeCapturedRgba16Float(frame, outImage);
        }
        outImage = RgbaFloatImage{};
        if (!frame.IsSuccess())
        {
            return FloatImageStatus::CaptureNotSuccessful;
        }
        constexpr uint32_t Rgba32BytesPerPixel = 16u;
        if (frame.Width == 0u || frame.Height == 0u ||
            frame.Width > std::numeric_limits<uint32_t>::max() / Rgba32BytesPerPixel)
        {
            return FloatImageStatus::InvalidDimensions;
        }
        if (frame.BytesPerPixel != Rgba32BytesPerPixel ||
            frame.RowPitchBytes < frame.Width * Rgba32BytesPerPixel ||
            frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
        {
            return FloatImageStatus::InvalidPixelData;
        }
        outImage.Width = frame.Width;
        outImage.Height = frame.Height;
        outImage.Values.resize(static_cast<size_t>(frame.Width) * frame.Height * RgbaChannelCount);
        for (uint32_t y = 0; y < frame.Height; ++y)
        {
            const uint8_t* row = frame.Pixels.data() + static_cast<size_t>(y) * frame.RowPitchBytes;
            std::memcpy(outImage.Values.data() +
                            static_cast<size_t>(y) * frame.Width * RgbaChannelCount,
                        row, static_cast<size_t>(frame.Width) * Rgba32BytesPerPixel);
        }
        return FloatImageStatus::Success;
    }

    bool WriteRgbaFloatDump(const Core::Container::String& path, const RgbaFloatImage& image,
                            uint32_t sampleCount)
    {
        if (image.Width == 0u || image.Height == 0u ||
            image.Values.size() != static_cast<size_t>(image.Width) * image.Height * RgbaChannelCount)
        {
            return false;
        }
        char header[96] = {};
        const int headerLength = std::snprintf(header, sizeof(header), "NLRGBA32F %u %u %u\n",
                                               image.Width, image.Height, sampleCount);
        if (headerLength <= 0 || static_cast<size_t>(headerLength) >= sizeof(header))
        {
            return false;
        }
        FileStream::FileStreamUniquePtr stream = FileStream::FileStream::CreateUnique(
            path, FileStream::FileMode::Write, FileStream::FileAccess::Write,
            FileStream::FileShare::None);
        if (!stream)
        {
            return false;
        }
        const size_t bodyBytes = image.Values.size() * sizeof(float);
        if (stream->Write(header, static_cast<size_t>(headerLength)) !=
                static_cast<size_t>(headerLength) ||
            stream->Write(image.Values.data(), bodyBytes) != bodyBytes)
        {
            return false;
        }
        stream->Flush();
        return true;
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

    RgbaFloatViolation FindFirstRgba16FloatViolation(const RgbaFloatImage& image)
    {
        if (image.Width == 0u)
        {
            return {};
        }

        for (size_t index = 0; index < image.Values.size(); ++index)
        {
            const float value = image.Values[index];
            RgbaFloatViolationKind kind = RgbaFloatViolationKind::None;
            if (!std::isfinite(value))
            {
                kind = RgbaFloatViolationKind::NonFinite;
            }
            else if (!(std::abs(value) < Rgba16MaxFinite))
            {
                kind = value < 0.0f
                    ? RgbaFloatViolationKind::NegativeSaturation
                    : RgbaFloatViolationKind::PositiveSaturation;
            }

            if (kind != RgbaFloatViolationKind::None)
            {
                const size_t pixelIndex = index / RgbaChannelCount;
                RgbaFloatViolation violation;
                violation.Kind = kind;
                violation.X = static_cast<uint32_t>(pixelIndex % image.Width);
                violation.Y = static_cast<uint32_t>(pixelIndex / image.Width);
                violation.Channel = static_cast<uint32_t>(index % RgbaChannelCount);
                violation.Value = value;
                return violation;
            }
        }
        return {};
    }

    bool IsFiniteAndWithinRgba16Range(const RgbaFloatImage& image)
    {
        return FindFirstRgba16FloatViolation(image).Kind == RgbaFloatViolationKind::None;
    }
}
