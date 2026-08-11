#include "RenderingValidation/RenderingPerceptualDiff.h"

#if defined(FLIP_ENABLE_CUDA)
#error "NorvesLib R0 rendering validation requires the CPU-only FLIP path"
#endif
#include <FLIP.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace NorvesLib::Test::RenderingValidation
{
    namespace
    {
        constexpr uint32_t RgbaChannelCount = 4u;
        constexpr uint32_t FlipChannelCount = 3u;

        PerceptualDiffStatus ConvertRawStatus(GoldenImageStatus status)
        {
            switch (status)
            {
            case GoldenImageStatus::Success:
                return PerceptualDiffStatus::Success;
            case GoldenImageStatus::InvalidDimensions:
                return PerceptualDiffStatus::InvalidDimensions;
            case GoldenImageStatus::InvalidPixelData:
                return PerceptualDiffStatus::InvalidPixelData;
            default:
                return PerceptualDiffStatus::VendorEvaluationFailed;
            }
        }

        bool TryCalculateFlipValueCount(
            const Rgba8Image& image,
            size_t& outPixelCount,
            size_t& outValueCount)
        {
            const uint64_t pixelCount =
                static_cast<uint64_t>(image.Width) * static_cast<uint64_t>(image.Height);
            if (pixelCount > std::numeric_limits<size_t>::max() ||
                pixelCount > std::numeric_limits<size_t>::max() / FlipChannelCount)
            {
                return false;
            }
            outPixelCount = static_cast<size_t>(pixelCount);
            outValueCount = outPixelCount * FlipChannelCount;
            return true;
        }

        void CopyLinearRgb(const Rgba8Image& image, Core::Container::VariableArray<float>& outValues)
        {
            size_t outputIndex = 0;
            for (uint32_t y = 0; y < image.Height; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * image.RowPitchBytes;
                for (uint32_t x = 0; x < image.Width; ++x)
                {
                    const size_t pixelOffset = rowOffset + static_cast<size_t>(x) * RgbaChannelCount;
                    for (uint32_t channel = 0; channel < FlipChannelCount; ++channel)
                    {
                        const float encoded =
                            static_cast<float>(image.Pixels[pixelOffset + channel]) / 255.0f;
                        outValues[outputIndex++] = DecodeSrgbToLinear(encoded);
                    }
                }
            }
        }

        bool TryReadThresholdFile(
            const Core::Container::String& path,
            Core::Container::VariableArray<char>& outText)
        {
            std::FILE* file = nullptr;
            if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr)
            {
                return false;
            }
            if (std::fseek(file, 0, SEEK_END) != 0)
            {
                std::fclose(file);
                return false;
            }
            const long length = std::ftell(file);
            if (length <= 0 || length > 4096 || std::fseek(file, 0, SEEK_SET) != 0)
            {
                std::fclose(file);
                return false;
            }
            try
            {
                outText.resize(static_cast<size_t>(length) + 1u);
            }
            catch (...)
            {
                std::fclose(file);
                return false;
            }
            const size_t readCount = std::fread(outText.data(), 1u, static_cast<size_t>(length), file);
            const bool bCloseSucceeded = std::fclose(file) == 0;
            if (readCount != static_cast<size_t>(length) || !bCloseSucceeded)
            {
                outText.clear();
                return false;
            }
            outText[static_cast<size_t>(length)] = '\0';
            return true;
        }

        char* TakeLine(char*& cursor)
        {
            if (cursor == nullptr || *cursor == '\0')
            {
                return nullptr;
            }
            char* line = cursor;
            while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n')
            {
                ++cursor;
            }
            if (*cursor == '\r')
            {
                *cursor++ = '\0';
                if (*cursor != '\n')
                {
                    return nullptr;
                }
                *cursor++ = '\0';
            }
            else if (*cursor == '\n')
            {
                *cursor++ = '\0';
            }
            return line;
        }

        bool SplitThresholdRow(char* line, char* (&fields)[7])
        {
            fields[0] = line;
            size_t fieldIndex = 1u;
            for (char* cursor = line; *cursor != '\0'; ++cursor)
            {
                if (*cursor == '\t')
                {
                    if (fieldIndex >= 7u)
                    {
                        return false;
                    }
                    *cursor = '\0';
                    fields[fieldIndex++] = cursor + 1;
                }
            }
            if (fieldIndex != 7u)
            {
                return false;
            }
            for (char* field : fields)
            {
                if (*field == '\0')
                {
                    return false;
                }
            }
            return true;
        }

        bool TryParseUnsigned(const char* text, unsigned long maximum, unsigned long& outValue)
        {
            char* end = nullptr;
            const unsigned long value = std::strtoul(text, &end, 10);
            if (end == text || *end != '\0' || value > maximum)
            {
                return false;
            }
            outValue = value;
            return true;
        }

        bool TryParseThresholdRow(
            char* line,
            const char* expectedScene,
            VisualGoldenThresholds& outThresholds,
            ArtificialDifferenceSpec& outDifference)
        {
            char* fields[7]{};
            if (!SplitThresholdRow(line, fields) || std::strcmp(fields[0], expectedScene) != 0 ||
                std::strcmp(fields[3], "67.0") != 0 ||
                std::strcmp(fields[6], "b475eb4bf394ab877c42166c9eb0a84a02cc5b14") != 0)
            {
                return false;
            }
            char* meanEnd = nullptr;
            const double meanLimit = std::strtod(fields[1], &meanEnd);
            unsigned long maximumDelta = 0;
            unsigned long patchSize = 0;
            unsigned long channelDelta = 0;
            if (meanEnd == fields[1] || *meanEnd != '\0' || !std::isfinite(meanLimit) ||
                meanLimit < 0.0 || meanLimit > 1.0 ||
                !TryParseUnsigned(fields[2], RenderingValidationHardMaxChannelDelta, maximumDelta) ||
                !TryParseUnsigned(fields[4], 16u, patchSize) ||
                !TryParseUnsigned(fields[5], RenderingValidationHardMaxChannelDelta, channelDelta) ||
                (patchSize != 1u && patchSize != 2u && patchSize != 4u &&
                 patchSize != 8u && patchSize != 16u) ||
                (channelDelta != 1u && channelDelta != 2u &&
                 channelDelta != 4u && channelDelta != 8u))
            {
                return false;
            }
            outThresholds.MaximumMeanFlipError = meanLimit;
            outThresholds.MaximumChannelDelta = static_cast<uint8_t>(maximumDelta);
            outDifference.PatchSize = static_cast<uint32_t>(patchSize);
            outDifference.ChannelDelta = static_cast<uint8_t>(channelDelta);
            return true;
        }
    } // namespace

    float DecodeSrgbToLinear(float encodedSrgb)
    {
        if (encodedSrgb <= 0.04045f)
        {
            return encodedSrgb / 12.92f;
        }
        return std::pow((encodedSrgb + 0.055f) / 1.055f, 2.4f);
    }

    PerceptualDiffStatus CompareLdrFlip(
        const Rgba8Image& reference,
        const Rgba8Image& candidate,
        PerceptualDifferenceMetrics& outMetrics)
    {
        outMetrics = {};
        const GoldenImageStatus rawStatus = CompareRgba8(reference, candidate, outMetrics.Raw);
        if (rawStatus != GoldenImageStatus::Success)
        {
            return ConvertRawStatus(rawStatus);
        }

        if (reference.Width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            reference.Height > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        {
            outMetrics = {};
            return PerceptualDiffStatus::InvalidDimensions;
        }

        size_t pixelCount = 0;
        size_t valueCount = 0;
        if (!TryCalculateFlipValueCount(reference, pixelCount, valueCount))
        {
            outMetrics = {};
            return PerceptualDiffStatus::InvalidDimensions;
        }

        Core::Container::VariableArray<float> referenceLinear;
        Core::Container::VariableArray<float> candidateLinear;
        float* errorMap = nullptr;
        try
        {
            referenceLinear.resize(valueCount);
            candidateLinear.resize(valueCount);
            CopyLinearRgb(reference, referenceLinear);
            CopyLinearRgb(candidate, candidateLinear);

            FLIP::Parameters parameters;
            parameters.PPD = RenderingValidationPixelsPerDegree;
            float vendorMean = 0.0f;
            // Pinned FLIP.h lines 2428-2431 require interleaved [0,1] linear RGB input.
            FLIP::evaluate(
                referenceLinear.data(),
                candidateLinear.data(),
                static_cast<int>(reference.Width),
                static_cast<int>(reference.Height),
                false,
                parameters,
                false,
                true,
                vendorMean,
                &errorMap);
        }
        catch (...)
        {
            delete[] errorMap;
            outMetrics = {};
            return PerceptualDiffStatus::VendorEvaluationFailed;
        }

        if (errorMap == nullptr)
        {
            outMetrics = {};
            return PerceptualDiffStatus::VendorEvaluationFailed;
        }

        double errorSum = 0.0;
        for (size_t index = 0; index < pixelCount; ++index)
        {
            const float error = errorMap[index];
            if (!std::isfinite(error) || error < 0.0f || error > 1.0f)
            {
                delete[] errorMap;
                outMetrics = {};
                return PerceptualDiffStatus::VendorEvaluationFailed;
            }
            errorSum += static_cast<double>(error);
            if (error > outMetrics.MaxFlipError)
            {
                outMetrics.MaxFlipError = error;
                outMetrics.MaxFlipX = static_cast<uint32_t>(index % reference.Width);
                outMetrics.MaxFlipY = static_cast<uint32_t>(index / reference.Width);
            }
        }
        delete[] errorMap;

        outMetrics.MeanFlipError = errorSum / static_cast<double>(pixelCount);
        if (!std::isfinite(outMetrics.MeanFlipError))
        {
            outMetrics = {};
            return PerceptualDiffStatus::VendorEvaluationFailed;
        }
        return PerceptualDiffStatus::Success;
    }

    bool MeetsVisualGoldenThresholds(
        const PerceptualDifferenceMetrics& metrics,
        const VisualGoldenThresholds& thresholds)
    {
        return std::isfinite(metrics.MeanFlipError) &&
               metrics.MeanFlipError <= thresholds.MaximumMeanFlipError &&
               thresholds.MaximumChannelDelta <= RenderingValidationHardMaxChannelDelta &&
               metrics.Raw.MaxChannelDelta <= thresholds.MaximumChannelDelta;
    }

    bool LoadVisualGoldenThresholds(
        const Core::Container::String& tsvPath,
        SceneKind scene,
        VisualGoldenThresholds& outThresholds,
        ArtificialDifferenceSpec& outArtificialDifference)
    {
        outThresholds = {};
        outArtificialDifference = {};
        Core::Container::VariableArray<char> text;
        if (!TryReadThresholdFile(tsvPath, text))
        {
            return false;
        }
        char* cursor = text.data();
        if (static_cast<unsigned char>(cursor[0]) == 0xEFu &&
            static_cast<unsigned char>(cursor[1]) == 0xBBu &&
            static_cast<unsigned char>(cursor[2]) == 0xBFu)
        {
            cursor += 3;
        }
        constexpr const char* Header =
            "scene\tmean_flip_limit\tmax_channel_delta\tpixels_per_degree\tpatch_size\tchannel_delta\tflip_commit";
        char* header = TakeLine(cursor);
        char* indoorLine = TakeLine(cursor);
        char* outdoorLine = TakeLine(cursor);
        if (header == nullptr || std::strcmp(header, Header) != 0 ||
            indoorLine == nullptr || outdoorLine == nullptr || TakeLine(cursor) != nullptr)
        {
            return false;
        }
        VisualGoldenThresholds indoorThresholds;
        ArtificialDifferenceSpec indoorDifference;
        VisualGoldenThresholds outdoorThresholds;
        ArtificialDifferenceSpec outdoorDifference;
        if (!TryParseThresholdRow(
                indoorLine, "indoor", indoorThresholds, indoorDifference) ||
            !TryParseThresholdRow(
                outdoorLine, "outdoor", outdoorThresholds, outdoorDifference))
        {
            return false;
        }
        if (scene == SceneKind::Indoor)
        {
            outThresholds = indoorThresholds;
            outArtificialDifference = indoorDifference;
        }
        else
        {
            outThresholds = outdoorThresholds;
            outArtificialDifference = outdoorDifference;
        }
        return true;
    }

    void ApplyArtificialDifference(
        Rgba8Image& image,
        const ArtificialDifferenceSpec& difference)
    {
        RawImageDifferenceMetrics validation;
        if (difference.PatchSize == 0u || difference.ChannelDelta == 0u ||
            CompareRgba8(image, image, validation) != GoldenImageStatus::Success)
        {
            return;
        }

        const uint32_t patchWidth = std::min(difference.PatchSize, image.Width);
        const uint32_t patchHeight = std::min(difference.PatchSize, image.Height);
        const uint32_t beginX = (image.Width - patchWidth) / 2u;
        const uint32_t beginY = (image.Height - patchHeight) / 2u;
        const uint8_t addLimit = static_cast<uint8_t>(255u - difference.ChannelDelta);
        for (uint32_t y = beginY; y < beginY + patchHeight; ++y)
        {
            for (uint32_t x = beginX; x < beginX + patchWidth; ++x)
            {
                const size_t pixelOffset = static_cast<size_t>(y) * image.RowPitchBytes +
                                           static_cast<size_t>(x) * RgbaChannelCount;
                for (uint32_t channel = 0; channel < FlipChannelCount; ++channel)
                {
                    uint8_t& value = image.Pixels[pixelOffset + channel];
                    value = value <= addLimit
                                ? static_cast<uint8_t>(value + difference.ChannelDelta)
                                : static_cast<uint8_t>(value - difference.ChannelDelta);
                }
            }
        }
    }
} // namespace NorvesLib::Test::RenderingValidation
