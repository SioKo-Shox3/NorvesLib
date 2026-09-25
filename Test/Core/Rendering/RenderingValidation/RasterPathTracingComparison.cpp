#include "RenderingValidation/RasterPathTracingComparison.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace NorvesLib::Test::RenderingValidation
{
    using Core::Container::VariableArray;

    namespace
    {
        uint8_t EncodeSrgbByte(double linear)
        {
            const double clamped = std::clamp(linear, 0.0, 1.0);
            const double encoded = clamped <= 0.0031308 ? 12.92 * clamped
                                                        : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
            return static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
        }
    } // namespace

    Rgba8Image ToneMapToLdr(const RgbaFloatImage& image)
    {
        Rgba8Image result;
        result.Width = image.Width;
        result.Height = image.Height;
        result.RowPitchBytes = image.Width * 4u;
        result.Pixels.resize(static_cast<size_t>(image.Width) * image.Height * 4u);
        for (size_t pixel = 0u; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double value = std::max(0.0, static_cast<double>(image.Values[pixel * 4u + channel]));
                result.Pixels[pixel * 4u + channel] = EncodeSrgbByte(value / (1.0 + value));
            }
            result.Pixels[pixel * 4u + 3u] = 255u;
        }
        return result;
    }

    RgbaFloatImage DownsampleBlocks(const RgbaFloatImage& image, uint32_t blockSize)
    {
        RgbaFloatImage result;
        result.Width = image.Width / blockSize;
        result.Height = image.Height / blockSize;
        result.Values.resize(static_cast<size_t>(result.Width) * result.Height * 4u);
        for (uint32_t by = 0u; by < result.Height; ++by)
        {
            for (uint32_t bx = 0u; bx < result.Width; ++bx)
            {
                double sum[4] = {};
                for (uint32_t y = by * blockSize; y < (by + 1u) * blockSize; ++y)
                {
                    for (uint32_t x = bx * blockSize; x < (bx + 1u) * blockSize; ++x)
                    {
                        for (uint32_t channel = 0u; channel < 4u; ++channel)
                        {
                            sum[channel] += image.Values[(static_cast<size_t>(y) * image.Width + x) * 4u + channel];
                        }
                    }
                }
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    result.Values[(static_cast<size_t>(by) * result.Width + bx) * 4u + channel] =
                        static_cast<float>(sum[channel] / (blockSize * blockSize));
                }
            }
        }
        return result;
    }

    bool MeasureFlip(const RgbaFloatImage& reference,
                     const RgbaFloatImage& candidate,
                     const VariableArray<uint8_t>& agreement,
                     uint32_t blockSize,
                     FlipMeasurement& outMeasurement)
    {
        outMeasurement = FlipMeasurement{};
        PerceptualDifferenceMetrics full;
        PerceptualDifferenceMetrics blocks;
        VariableArray<float> errorMap;
        if (CompareLdrFlip(ToneMapToLdr(reference), ToneMapToLdr(candidate), full, &errorMap) !=
                PerceptualDiffStatus::Success ||
            CompareLdrFlip(ToneMapToLdr(DownsampleBlocks(reference, blockSize)),
                           ToneMapToLdr(DownsampleBlocks(candidate, blockSize)), blocks) !=
                PerceptualDiffStatus::Success)
        {
            return false;
        }
        outMeasurement.Mean = full.MeanFlipError;
        outMeasurement.PixelMax = full.MaxFlipError;
        outMeasurement.PixelX = full.MaxFlipX;
        outMeasurement.PixelY = full.MaxFlipY;
        outMeasurement.BlockMax = blocks.MaxFlipError;
        outMeasurement.BlockX = blocks.MaxFlipX;
        outMeasurement.BlockY = blocks.MaxFlipY;
        // 一致しない画素の誤差（記録用、置き換えない画像のFLIP）。
        for (size_t index = 0u; index < errorMap.size(); ++index)
        {
            if (!agreement.empty() && agreement[index] == 0u &&
                errorMap[index] > outMeasurement.DisagreeingPixelMax)
            {
                outMeasurement.DisagreeingPixelMax = errorMap[index];
                outMeasurement.DisagreeingPixelX = static_cast<uint32_t>(index % reference.Width);
                outMeasurement.DisagreeingPixelY = static_cast<uint32_t>(index / reference.Width);
            }
        }
        RgbaFloatImage neutralized = candidate;
        bool bAnyDisagreeing = false;
        for (size_t index = 0u; index < agreement.size(); ++index)
        {
            if (agreement[index] == 0u)
            {
                bAnyDisagreeing = true;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    neutralized.Values[index * 4u + channel] = reference.Values[index * 4u + channel];
                }
            }
        }
        PerceptualDifferenceMetrics agreeing = full;
        outMeasurement.AgreeingErrorMap = errorMap;
        if (bAnyDisagreeing &&
            CompareLdrFlip(ToneMapToLdr(reference), ToneMapToLdr(neutralized), agreeing,
                           &outMeasurement.AgreeingErrorMap) != PerceptualDiffStatus::Success)
        {
            return false;
        }
        // 置き換えた画素にも近傍から誤差が広がるため、最大は一致する画素だけで求める。一致しない
        // 画素の誤差は0にして、以後の診断の集計からも外す。
        outMeasurement.AgreeingPixelMax = 0.0f;
        for (size_t index = 0u; index < outMeasurement.AgreeingErrorMap.size(); ++index)
        {
            if (!agreement.empty() && agreement[index] == 0u)
            {
                outMeasurement.AgreeingErrorMap[index] = 0.0f;
                continue;
            }
            if (outMeasurement.AgreeingErrorMap[index] > outMeasurement.AgreeingPixelMax)
            {
                outMeasurement.AgreeingPixelMax = outMeasurement.AgreeingErrorMap[index];
                outMeasurement.AgreeingPixelX = static_cast<uint32_t>(index % reference.Width);
                outMeasurement.AgreeingPixelY = static_cast<uint32_t>(index / reference.Width);
            }
        }
        return true;
    }

    VariableArray<uint8_t> BuildGeometryAgreement(const RgbaFloatImage& rasterNormal,
                                                  const RgbaFloatImage& rasterDepth,
                                                  const RgbaFloatImage& pathNormal,
                                                  const RgbaFloatImage& pathDistance,
                                                  uint32_t& outDisagreeingPixels)
    {
        const size_t pixelCount = static_cast<size_t>(rasterNormal.Width) * rasterNormal.Height;
        VariableArray<uint8_t> agreement(pixelCount, 0u);
        outDisagreeingPixels = 0u;
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const float* encodedNormal = rasterNormal.Values.data() + pixel * 4u;
            const float encodedDepth = rasterDepth.Values[pixel * 4u];
            const float* traceNormal = pathNormal.Values.data() + pixel * 4u;
            const double traceDistance = pathDistance.Values[pixel * 4u];
            const bool bRasterHit = encodedDepth > 0.0f;
            const bool bTraceHit = traceDistance > 0.0;
            bool bAgree = !bRasterHit && !bTraceHit;
            if (bRasterHit && bTraceHit && encodedDepth < 1.0f)
            {
                const double rasterDistance = 25.0 * encodedDepth / (1.0 - encodedDepth);
                double rasterN[3] = {};
                double rasterLength = 0.0;
                double traceLength = 0.0;
                double dot = 0.0;
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    rasterN[axis] = 2.0 * encodedNormal[axis] - 1.0;
                    rasterLength += rasterN[axis] * rasterN[axis];
                    traceLength += static_cast<double>(traceNormal[axis]) * traceNormal[axis];
                    dot += rasterN[axis] * traceNormal[axis];
                }
                const double cosine = rasterLength > 0.0 && traceLength > 0.0
                    ? dot / std::sqrt(rasterLength * traceLength)
                    : -1.0;
                bAgree = std::abs(rasterDistance - traceDistance) <= 0.01 * traceDistance &&
                         cosine >= 0.99;
            }
            agreement[pixel] = bAgree ? 1u : 0u;
            outDisagreeingPixels += bAgree ? 0u : 1u;
        }
        return agreement;
    }

    uint32_t ExcludeSunVisibilityDisagreement(const RgbaFloatImage& rasterVisibility,
                                              const RgbaFloatImage& pathVisibility,
                                              float tolerance,
                                              VariableArray<uint8_t>& inOutAgreement)
    {
        const size_t pixelCount = static_cast<size_t>(rasterVisibility.Width) * rasterVisibility.Height;
        uint32_t excluded = 0u;
        for (size_t pixel = 0u; pixel < pixelCount && pixel < inOutAgreement.size(); ++pixel)
        {
            const float difference = std::abs(rasterVisibility.Values[pixel * 4u] -
                                              pathVisibility.Values[pixel * 4u]);
            if (inOutAgreement[pixel] != 0u && difference > tolerance)
            {
                inOutAgreement[pixel] = 0u;
                ++excluded;
            }
        }
        return excluded;
    }

    RgbaFloatImage ScaleComponent(const RgbaFloatImage& base, const RgbaFloatImage& total, double scale)
    {
        RgbaFloatImage result = total;
        for (size_t index = 0u; index < result.Values.size(); ++index)
        {
            if (index % 4u == 3u)
            {
                continue;
            }
            const double component = static_cast<double>(total.Values[index]) - base.Values[index];
            result.Values[index] = static_cast<float>(base.Values[index] + scale * component);
        }
        return result;
    }

    RgbaFloatImage AddLocalLeak(const RgbaFloatImage& image,
                                double meanLuminance,
                                const VariableArray<uint8_t>& agreement,
                                uint32_t blockSize,
                                uint32_t patchSize,
                                double leakScale)
    {
        uint32_t bestX = blockSize;
        uint32_t bestY = blockSize;
        double bestLuminance = 1.0e30;
        for (uint32_t y = blockSize; y + blockSize + patchSize <= image.Height; ++y)
        {
            for (uint32_t x = blockSize; x + blockSize + patchSize <= image.Width; ++x)
            {
                double sum = 0.0;
                bool bAllAgree = true;
                for (uint32_t dy = 0u; dy < patchSize; ++dy)
                {
                    for (uint32_t dx = 0u; dx < patchSize; ++dx)
                    {
                        const size_t pixelIndex = static_cast<size_t>(y + dy) * image.Width + x + dx;
                        bAllAgree = bAllAgree && (agreement.empty() || agreement[pixelIndex] != 0u);
                        const size_t offset = pixelIndex * 4u;
                        sum += 0.2126 * image.Values[offset] + 0.7152 * image.Values[offset + 1u] +
                               0.0722 * image.Values[offset + 2u];
                    }
                }
                if (bAllAgree && sum < bestLuminance)
                {
                    bestLuminance = sum;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        RgbaFloatImage result = image;
        for (uint32_t dy = 0u; dy < patchSize; ++dy)
        {
            for (uint32_t dx = 0u; dx < patchSize; ++dx)
            {
                const size_t offset = (static_cast<size_t>(bestY + dy) * image.Width + bestX + dx) * 4u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    result.Values[offset + channel] += static_cast<float>(leakScale * meanLuminance);
                }
            }
        }
        std::cout << "local_leak_patch x=" << bestX << " y=" << bestY
                  << " size=" << patchSize << " added=" << leakScale * meanLuminance << '\n';
        return result;
    }

    double MeanLuminance(const RgbaFloatImage& image)
    {
        double sum = 0.0;
        for (size_t pixel = 0u; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
        {
            sum += 0.2126 * image.Values[pixel * 4u] + 0.7152 * image.Values[pixel * 4u + 1u] +
                   0.0722 * image.Values[pixel * 4u + 2u];
        }
        return sum / (static_cast<double>(image.Width) * image.Height);
    }

    void PrintFlipMeasurement(const char* label, const FlipMeasurement& measurement)
    {
        std::cout << label << " mean_flip=" << measurement.Mean
                  << " pixel_max_flip=" << measurement.PixelMax
                  << " pixel=(" << measurement.PixelX << "," << measurement.PixelY << ")"
                  << " agreeing_pixel_max_flip=" << measurement.AgreeingPixelMax
                  << " agreeing_pixel=(" << measurement.AgreeingPixelX << ","
                  << measurement.AgreeingPixelY << ")"
                  << " disagreeing_pixel_max_flip=" << measurement.DisagreeingPixelMax
                  << " disagreeing_pixel=(" << measurement.DisagreeingPixelX << ","
                  << measurement.DisagreeingPixelY << ")"
                  << " block8_max_flip=" << measurement.BlockMax
                  << " block=(" << measurement.BlockX << "," << measurement.BlockY << ")\n";
    }

    void PrintAgreeingPixelsOverLimit(const FlipMeasurement& measurement, float pixelLimit, uint32_t width)
    {
        uint32_t overLimit = 0u;
        for (float error : measurement.AgreeingErrorMap)
        {
            overLimit += error > pixelLimit ? 1u : 0u;
        }
        std::cout << "diagnostic_agreeing_pixels_over_limit count=" << overLimit << " fraction="
                  << static_cast<double>(overLimit) / measurement.AgreeingErrorMap.size();
        VariableArray<uint32_t> peaks;
        for (uint32_t rank = 0u; rank < 8u; ++rank)
        {
            float best = 0.0f;
            uint32_t bestIndex = UINT32_MAX;
            for (uint32_t index = 0u; index < measurement.AgreeingErrorMap.size(); ++index)
            {
                const float error = measurement.AgreeingErrorMap[index];
                if (error <= pixelLimit || error <= best)
                {
                    continue;
                }
                bool bNearPeak = false;
                for (uint32_t peak : peaks)
                {
                    const int32_t dx = static_cast<int32_t>(index % width) - static_cast<int32_t>(peak % width);
                    const int32_t dy = static_cast<int32_t>(index / width) - static_cast<int32_t>(peak / width);
                    bNearPeak = bNearPeak || (std::abs(dx) < 16 && std::abs(dy) < 16);
                }
                if (!bNearPeak)
                {
                    best = error;
                    bestIndex = index;
                }
            }
            if (bestIndex == UINT32_MAX)
            {
                break;
            }
            peaks.push_back(bestIndex);
            std::cout << " peak=(" << bestIndex % width << "," << bestIndex / width << "):" << best;
        }
        std::cout << '\n';
    }

} // namespace NorvesLib::Test::RenderingValidation
