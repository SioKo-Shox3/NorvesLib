#pragma once

#include "RenderingValidation/RenderingGoldenImage.h"
#include "RenderingValidation/RenderingValidationScene.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    inline constexpr float RenderingValidationPixelsPerDegree = 67.0f;
    inline constexpr uint8_t RenderingValidationHardMaxChannelDelta = 8u;

    float DecodeSrgbToLinear(float encodedSrgb);

    enum class PerceptualDiffStatus : uint8_t
    {
        Success,
        InvalidDimensions,
        InvalidPixelData,
        VendorEvaluationFailed
    };

    struct PerceptualDifferenceMetrics
    {
        double MeanFlipError = 0.0;
        float MaxFlipError = 0.0f;
        uint32_t MaxFlipX = 0;
        uint32_t MaxFlipY = 0;
        RawImageDifferenceMetrics Raw;
    };

    struct VisualGoldenThresholds
    {
        double MaximumMeanFlipError = 0.0;
        uint8_t MaximumChannelDelta = RenderingValidationHardMaxChannelDelta;
    };

    struct ArtificialDifferenceSpec
    {
        uint32_t PatchSize = 0;
        uint8_t ChannelDelta = 0;
    };

    PerceptualDiffStatus CompareLdrFlip(
        const Rgba8Image& reference,
        const Rgba8Image& candidate,
        PerceptualDifferenceMetrics& outMetrics);
    bool MeetsVisualGoldenThresholds(
        const PerceptualDifferenceMetrics& metrics,
        const VisualGoldenThresholds& thresholds);
    bool LoadVisualGoldenThresholds(
        const Core::Container::String& tsvPath,
        SceneKind scene,
        VisualGoldenThresholds& outThresholds,
        ArtificialDifferenceSpec& outArtificialDifference);
    void ApplyArtificialDifference(
        Rgba8Image& image,
        const ArtificialDifferenceSpec& difference);
} // namespace NorvesLib::Test::RenderingValidation
