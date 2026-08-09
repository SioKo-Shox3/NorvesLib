#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameCaptureTypes.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    struct Rgba8Image
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t RowPitchBytes = 0;
        Core::Container::VariableArray<uint8_t> Pixels;
    };

    struct RawImageDifferenceMetrics
    {
        uint64_t DifferingPixelCount = 0;
        uint8_t MaxChannelDelta = 0;
        uint32_t MaxDifferenceX = 0;
        uint32_t MaxDifferenceY = 0;
        double MeanAbsoluteChannelDelta = 0.0;
    };

    struct RawGoldenThresholds
    {
        uint64_t MaximumDifferingPixelCount = 0;
        uint8_t MaximumChannelDelta = 0;
    };

    enum class GoldenImageStatus : uint8_t
    {
        Success,
        CaptureNotSuccessful,
        UnsupportedFormat,
        InvalidDimensions,
        InvalidPixelData,
        EncodeFailed,
        DecodeFailed,
        FileOpenFailed,
        FileWriteFailed
    };

    GoldenImageStatus EncodeCapturedFramePng(
        const Core::Rendering::CapturedFrame& frame,
        Core::Container::VariableArray<uint8_t>& outPng);
    GoldenImageStatus EncodeRgba8Png(
        const Rgba8Image& image,
        Core::Container::VariableArray<uint8_t>& outPng);
    GoldenImageStatus DecodePng(Core::Container::Span<const uint8_t> png, Rgba8Image& outImage);
    GoldenImageStatus LoadPng(const Core::Container::String& path, Rgba8Image& outImage);
    GoldenImageStatus SavePng(
        const Core::Container::String& path,
        Core::Container::Span<const uint8_t> png);
    GoldenImageStatus CompareRgba8(
        const Rgba8Image& reference,
        const Rgba8Image& candidate,
        RawImageDifferenceMetrics& outMetrics);
    bool MeetsRawGoldenThresholds(
        const RawImageDifferenceMetrics& metrics,
        const RawGoldenThresholds& thresholds);
} // namespace NorvesLib::Test::RenderingValidation
