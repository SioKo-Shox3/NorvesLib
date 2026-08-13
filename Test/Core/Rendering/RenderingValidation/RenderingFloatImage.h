#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameCaptureTypes.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    enum class FloatImageStatus : uint8_t
    {
        Success,
        CaptureNotSuccessful,
        UnsupportedFormat,
        InvalidDimensions,
        InvalidPixelData
    };

    struct RgbaFloatImage
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::VariableArray<float> Values;
    };

    enum class NonFiniteKind : uint8_t
    {
        None,
        NaN,
        PositiveInfinity,
        NegativeInfinity
    };

    struct NonFiniteLocation
    {
        NonFiniteKind Kind = NonFiniteKind::None;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Channel = 0;
    };

    enum class RgbaFloatViolationKind : uint8_t
    {
        None,
        NonFinite,
        PositiveSaturation,
        NegativeSaturation
    };

    struct RgbaFloatViolation
    {
        RgbaFloatViolationKind Kind = RgbaFloatViolationKind::None;
        uint32_t X = 0;
        uint32_t Y = 0;
        uint32_t Channel = 0;
        float Value = 0.0f;
    };

    float DecodeIeee754Binary16(uint16_t bits);
    FloatImageStatus DecodeCapturedRgba16Float(
        const Core::Rendering::CapturedFrame& frame,
        RgbaFloatImage& outImage);
    NonFiniteLocation FindFirstNonFinite(const RgbaFloatImage& image);
    bool IsFiniteImage(const RgbaFloatImage& image);
    RgbaFloatViolation FindFirstRgba16FloatViolation(const RgbaFloatImage& image);
    bool IsFiniteAndWithinRgba16Range(const RgbaFloatImage& image);
}
