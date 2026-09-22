// Cornell RGBEの参照比較とDDGIの動的更新を実GPU captureで検証する。
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/FrameCaptureTypes.h"
#include "Rendering/RenderWorld.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace NorvesLib::RHI::Vulkan
{
    void BeginVulkanValidationErrorCaptureForTesting() noexcept;
    void EndVulkanValidationErrorCaptureForTesting() noexcept;
    uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RenderingDDGIVulkanTest";
    constexpr uint32_t CornellImageWidth = 512u;
    constexpr uint32_t CornellImageHeight = 512u;
    constexpr uint32_t CornellDynamicSampleCount = 10u;
    constexpr uint32_t CornellDynamicInitialWarmupSampleCount = 4u;
    constexpr uint32_t CornellDynamicDeadlineFrames = 8u;
    constexpr float CornellProbeOrigin = -0.1f;
    constexpr float CornellProbeSpacing = 0.82f;
    constexpr uint32_t CornellProbeCountAxis = 8u;

    struct Region
    {
        uint32_t Left = 0u;
        uint32_t Top = 0u;
        uint32_t Right = 0u;
        uint32_t Bottom = 0u;
    };

    struct RegionMean
    {
        double Red = 0.0;
        double Green = 0.0;
        double Blue = 0.0;
        double Y = 0.0;
    };

    struct R4Thresholds
    {
        Region DirectWhite;
        Region ShadowFloor;
        Region RedBounce;
        Region GreenBounce;
        double MaximumRelativeYError = 0.25;
        double MaximumChromaDifference = 0.10;
        double DisabledMeanDelta = 0.002;
        double DisabledMaximumDelta = 0.01;
        double MinimumEnabledMeanDelta = 0.001;
        double MinimumDynamicChange = 0.01;
        double MinimumDynamicProgress = 0.80;
        double MaximumDynamicProgressStep = 1.02;
        double LightOffsetX = 0.55;
        uint32_t DisabledWarmupFrames = 12u;
        uint32_t DynamicFrameLimit = 10u;
        bool bValid = false;
    };

    enum class Scenario : uint8_t
    {
        CornellReference,
        DynamicUpdate
    };

    enum class CaptureStage : uint8_t
    {
        CornellDisabledBaseline,
        CornellEnabled,
        CornellDisabledVerify,
        DynamicInitial,
        DynamicMoved,
        Complete
    };

    struct LinearRgb
    {
        double Red = 0.0;
        double Green = 0.0;
        double Blue = 0.0;
    };

    struct ImageDifference
    {
        double MeanAbsoluteDelta = 0.0;
        double MaximumAbsoluteDelta = 0.0;
    };

    bool ParseRegionValue(const char* key, double value, R4Thresholds& thresholds)
    {
        Region* region = nullptr;
        if (std::strncmp(key, "direct_white_", 13u) == 0)
        {
            region = &thresholds.DirectWhite;
            key += 13;
        }
        else if (std::strncmp(key, "shadow_floor_", 13u) == 0)
        {
            region = &thresholds.ShadowFloor;
            key += 13;
        }
        else if (std::strncmp(key, "red_bounce_", 11u) == 0)
        {
            region = &thresholds.RedBounce;
            key += 11;
        }
        else if (std::strncmp(key, "green_bounce_", 13u) == 0)
        {
            region = &thresholds.GreenBounce;
            key += 13;
        }
        if (region == nullptr || value < 0.0 || value > CornellImageWidth)
        {
            return false;
        }

        const uint32_t coordinate = static_cast<uint32_t>(value);
        if (std::strcmp(key, "left") == 0)
        {
            region->Left = coordinate;
        }
        else if (std::strcmp(key, "top") == 0)
        {
            region->Top = coordinate;
        }
        else if (std::strcmp(key, "right") == 0)
        {
            region->Right = coordinate;
        }
        else if (std::strcmp(key, "bottom") == 0)
        {
            region->Bottom = coordinate;
        }
        else
        {
            return false;
        }
        return true;
    }

    bool LoadThresholds(R4Thresholds& outThresholds)
    {
        std::ifstream input(
            NORVES_SOURCE_ROOT "/Test/Core/Rendering/Thresholds/RenderingValidation/R4CornellAcceptance.tsv");
        if (!input)
        {
            std::cerr << "R4_THRESHOLD_LOAD_FAILURE path=R4CornellAcceptance.tsv\n";
            return false;
        }

        bool bWidth = false;
        bool bHeight = false;
        bool bMaximumY = false;
        bool bMaximumChroma = false;
        bool bDisabledMean = false;
        bool bDisabledMaximum = false;
        bool bMinimumEnabledMean = false;
        bool bMinimumChange = false;
        bool bMinimumProgress = false;
        bool bMaximumProgressStep = false;
        bool bLightOffset = false;
        bool bWarmupFrames = false;
        bool bDynamicFrameLimit = false;
        bool bDirectWhiteLeft = false;
        bool bDirectWhiteTop = false;
        bool bDirectWhiteRight = false;
        bool bDirectWhiteBottom = false;
        bool bShadowLeft = false;
        bool bShadowTop = false;
        bool bShadowRight = false;
        bool bShadowBottom = false;
        bool bRedLeft = false;
        bool bRedTop = false;
        bool bRedRight = false;
        bool bRedBottom = false;
        bool bGreenLeft = false;
        bool bGreenTop = false;
        bool bGreenRight = false;
        bool bGreenBottom = false;
        char line[160] = {};
        while (input.getline(line, sizeof(line)))
        {
            if (line[0] == '\0' || line[0] == '#')
            {
                continue;
            }
            char* separator = std::strchr(line, '\t');
            if (separator == nullptr)
            {
                return false;
            }
            *separator = '\0';
            char* valueEnd = nullptr;
            const double value = std::strtod(separator + 1, &valueEnd);
            if (valueEnd == separator + 1 || !std::isfinite(value))
            {
                return false;
            }

            if (ParseRegionValue(line, value, outThresholds))
            {
                if (std::strcmp(line, "direct_white_left") == 0) bDirectWhiteLeft = true;
                else if (std::strcmp(line, "direct_white_top") == 0) bDirectWhiteTop = true;
                else if (std::strcmp(line, "direct_white_right") == 0) bDirectWhiteRight = true;
                else if (std::strcmp(line, "direct_white_bottom") == 0) bDirectWhiteBottom = true;
                else if (std::strcmp(line, "shadow_floor_left") == 0) bShadowLeft = true;
                else if (std::strcmp(line, "shadow_floor_top") == 0) bShadowTop = true;
                else if (std::strcmp(line, "shadow_floor_right") == 0) bShadowRight = true;
                else if (std::strcmp(line, "shadow_floor_bottom") == 0) bShadowBottom = true;
                else if (std::strcmp(line, "red_bounce_left") == 0) bRedLeft = true;
                else if (std::strcmp(line, "red_bounce_top") == 0) bRedTop = true;
                else if (std::strcmp(line, "red_bounce_right") == 0) bRedRight = true;
                else if (std::strcmp(line, "red_bounce_bottom") == 0) bRedBottom = true;
                else if (std::strcmp(line, "green_bounce_left") == 0) bGreenLeft = true;
                else if (std::strcmp(line, "green_bounce_top") == 0) bGreenTop = true;
                else if (std::strcmp(line, "green_bounce_right") == 0) bGreenRight = true;
                else if (std::strcmp(line, "green_bounce_bottom") == 0) bGreenBottom = true;
                continue;
            }

            if (std::strcmp(line, "width") == 0 && value == CornellImageWidth) bWidth = true;
            else if (std::strcmp(line, "height") == 0 && value == CornellImageHeight) bHeight = true;
            else if (std::strcmp(line, "max_relative_y_error") == 0)
            {
                outThresholds.MaximumRelativeYError = value;
                bMaximumY = true;
            }
            else if (std::strcmp(line, "max_chroma_difference") == 0)
            {
                outThresholds.MaximumChromaDifference = value;
                bMaximumChroma = true;
            }
            else if (std::strcmp(line, "disabled_mean_delta_max") == 0)
            {
                outThresholds.DisabledMeanDelta = value;
                bDisabledMean = true;
            }
            else if (std::strcmp(line, "disabled_max_delta_max") == 0)
            {
                outThresholds.DisabledMaximumDelta = value;
                bDisabledMaximum = true;
            }
            else if (std::strcmp(line, "enabled_mean_delta_min") == 0)
            {
                outThresholds.MinimumEnabledMeanDelta = value;
                bMinimumEnabledMean = true;
            }
            else if (std::strcmp(line, "minimum_dynamic_change") == 0)
            {
                outThresholds.MinimumDynamicChange = value;
                bMinimumChange = true;
            }
            else if (std::strcmp(line, "minimum_dynamic_progress") == 0)
            {
                outThresholds.MinimumDynamicProgress = value;
                bMinimumProgress = true;
            }
            else if (std::strcmp(line, "maximum_dynamic_progress_step") == 0)
            {
                outThresholds.MaximumDynamicProgressStep = value;
                bMaximumProgressStep = true;
            }
            else if (std::strcmp(line, "light_offset_x") == 0)
            {
                outThresholds.LightOffsetX = value;
                bLightOffset = true;
            }
            else if (std::strcmp(line, "disabled_warmup_frames") == 0)
            {
                outThresholds.DisabledWarmupFrames = static_cast<uint32_t>(value);
                bWarmupFrames = true;
            }
            else if (std::strcmp(line, "dynamic_frame_limit") == 0)
            {
                outThresholds.DynamicFrameLimit = static_cast<uint32_t>(value);
                bDynamicFrameLimit = true;
            }
            else
            {
                std::cerr << "R4_THRESHOLD_UNKNOWN_KEY=" << line << '\n';
                return false;
            }
        }

        const bool bAllValues =
            bWidth && bHeight && bMaximumY && bMaximumChroma && bDisabledMean &&
            bDisabledMaximum && bMinimumEnabledMean && bMinimumChange && bMinimumProgress &&
            bMaximumProgressStep &&
            bLightOffset && bWarmupFrames && bDynamicFrameLimit &&
            bDirectWhiteLeft && bDirectWhiteTop && bDirectWhiteRight && bDirectWhiteBottom &&
            bShadowLeft && bShadowTop && bShadowRight && bShadowBottom &&
            bRedLeft && bRedTop && bRedRight && bRedBottom &&
            bGreenLeft && bGreenTop && bGreenRight && bGreenBottom;
        const Region regions[] = {
            outThresholds.DirectWhite,
            outThresholds.ShadowFloor,
            outThresholds.RedBounce,
            outThresholds.GreenBounce};
        for (const Region& region : regions)
        {
            if (region.Left >= region.Right || region.Top >= region.Bottom ||
                region.Right > CornellImageWidth || region.Bottom > CornellImageHeight)
            {
                return false;
            }
        }
        outThresholds.bValid = bAllValues;
        return outThresholds.bValid;
    }

    bool ReadExact(std::ifstream& input, uint8_t* destination, size_t byteCount)
    {
        input.read(reinterpret_cast<char*>(destination), static_cast<std::streamsize>(byteCount));
        return static_cast<size_t>(input.gcount()) == byteCount;
    }

    bool LoadCornellRgbe(VariableArray<uint8_t>& outPixels)
    {
        std::ifstream input(
            NORVES_SOURCE_ROOT "/Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe",
            std::ios::binary);
        if (!input)
        {
            std::cerr << "R4_CORNELL_RGBE_LOAD_FAILURE path=R4CornellReference.rgbe\n";
            return false;
        }

        char line[128] = {};
        uint32_t width = 0u;
        uint32_t height = 0u;
        bool bResolutionFound = false;
        while (input.getline(line, sizeof(line)))
        {
            unsigned parsedHeight = 0u;
            unsigned parsedWidth = 0u;
            if (sscanf_s(line, "-Y %u +X %u", &parsedHeight, &parsedWidth) == 2)
            {
                height = parsedHeight;
                width = parsedWidth;
                bResolutionFound = true;
                break;
            }
        }
        if (!bResolutionFound || width != CornellImageWidth || height != CornellImageHeight)
        {
            std::cerr << "R4_CORNELL_RGBE_RESOLUTION_FAILURE width=" << width
                      << " height=" << height << '\n';
            return false;
        }

        constexpr size_t channelStride = CornellImageWidth;
        VariableArray<uint8_t> planarChannels;
        planarChannels.resize(channelStride * 4u);
        outPixels.resize(static_cast<size_t>(width) * height * 4u);
        for (uint32_t y = 0u; y < height; ++y)
        {
            uint8_t scanlineHeader[4] = {};
            if (!ReadExact(input, scanlineHeader, sizeof(scanlineHeader)) ||
                scanlineHeader[0] != 2u || scanlineHeader[1] != 2u ||
                (scanlineHeader[2] & 0x80u) != 0u ||
                ((static_cast<uint32_t>(scanlineHeader[2]) << 8u) | scanlineHeader[3]) != width)
            {
                std::cerr << "R4_CORNELL_RGBE_SCANLINE_HEADER_FAILURE row=" << y << '\n';
                return false;
            }

            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                uint32_t x = 0u;
                while (x < width)
                {
                    const int code = input.get();
                    if (code == std::char_traits<char>::eof())
                    {
                        return false;
                    }
                    if (code > 128)
                    {
                        const uint32_t runLength = static_cast<uint32_t>(code - 128);
                        const int value = input.get();
                        if (value == std::char_traits<char>::eof() ||
                            runLength == 0u || x + runLength > width)
                        {
                            return false;
                        }
                        for (uint32_t run = 0u; run < runLength; ++run)
                        {
                            planarChannels[channel * channelStride + x + run] =
                                static_cast<uint8_t>(value);
                        }
                        x += runLength;
                    }
                    else
                    {
                        const uint32_t runLength = static_cast<uint32_t>(code);
                        if (runLength == 0u || x + runLength > width ||
                            !ReadExact(input,
                                       planarChannels.data() + channel * channelStride + x,
                                       runLength))
                        {
                            return false;
                        }
                        x += runLength;
                    }
                }
            }

            for (uint32_t x = 0u; x < width; ++x)
            {
                const size_t pixel = (static_cast<size_t>(y) * width + x) * 4u;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    outPixels[pixel + channel] = planarChannels[channel * channelStride + x];
                }
            }
        }
        return true;
    }

    double RgbeChannelToLinear(uint8_t mantissa, uint8_t exponent)
    {
        if (exponent == 0u)
        {
            return 0.0;
        }
        return (static_cast<double>(mantissa) + 0.5) * std::ldexp(1.0, static_cast<int>(exponent) - 136);
    }

    LinearRgb GetRgbePixel(const VariableArray<uint8_t>& pixels, uint32_t x, uint32_t y)
    {
        const size_t offset = (static_cast<size_t>(y) * CornellImageWidth + x) * 4u;
        const uint8_t exponent = pixels[offset + 3u];
        return {
            RgbeChannelToLinear(pixels[offset], exponent),
            RgbeChannelToLinear(pixels[offset + 1u], exponent),
            RgbeChannelToLinear(pixels[offset + 2u], exponent)};
    }

    float HalfToFloat(uint16_t value)
    {
        const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16u;
        uint32_t exponent = (value >> 10u) & 0x1fu;
        uint32_t mantissa = value & 0x03ffu;
        uint32_t bits = 0u;
        if (exponent == 0u)
        {
            if (mantissa == 0u)
            {
                bits = sign;
            }
            else
            {
                int32_t adjustedExponent = -14;
                while ((mantissa & 0x0400u) == 0u)
                {
                    mantissa <<= 1u;
                    --adjustedExponent;
                }
                mantissa &= 0x03ffu;
                bits = sign |
                       (static_cast<uint32_t>(adjustedExponent + 127) << 23u) |
                       (mantissa << 13u);
            }
        }
        else if (exponent == 0x1fu)
        {
            bits = sign | 0x7f800000u | (mantissa << 13u);
        }
        else
        {
            bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
        }
        float result = 0.0f;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    double SrgbToLinear(double value)
    {
        return value <= 0.04045
                   ? value / 12.92
                   : std::pow((value + 0.055) / 1.055, 2.4);
    }

    bool IsByteBackBufferFormat(RHI::Format format)
    {
        return format == RHI::Format::R8G8B8A8_UNORM ||
               format == RHI::Format::R8G8B8A8_SRGB ||
               format == RHI::Format::B8G8R8A8_UNORM ||
               format == RHI::Format::B8G8R8A8_SRGB;
    }

    bool IsSupportedCapture(const CapturedFrame& frame)
    {
        return frame.IsSuccess() && frame.Width == CornellImageWidth &&
               frame.Height == CornellImageHeight &&
               frame.Format == RHI::Format::R16G16B16A16_FLOAT &&
               frame.BytesPerPixel == 8u &&
               frame.RowPitchBytes >= frame.Width * frame.BytesPerPixel &&
               frame.Pixels.size() >= static_cast<size_t>(frame.RowPitchBytes) * frame.Height;
    }

    bool GetCapturePixel(const CapturedFrame& frame,
                         uint32_t x,
                         uint32_t y,
                         LinearRgb& outColor);

    bool GetCapturePixel(const CapturedFrame& frame,
                         uint32_t x,
                         uint32_t y,
                         LinearRgb& outColor)
    {
        if (x >= frame.Width || y >= frame.Height || !IsSupportedCapture(frame))
        {
            return false;
        }
        const size_t offset = static_cast<size_t>(y) * frame.RowPitchBytes +
                              static_cast<size_t>(x) * frame.BytesPerPixel;
        if (frame.Format == RHI::Format::R16G16B16A16_FLOAT)
        {
            uint16_t channels[4] = {};
            std::memcpy(channels, frame.Pixels.data() + offset, sizeof(channels));
            outColor = {HalfToFloat(channels[0]), HalfToFloat(channels[1]), HalfToFloat(channels[2])};
            return std::isfinite(outColor.Red) && std::isfinite(outColor.Green) &&
                   std::isfinite(outColor.Blue);
        }

        const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM ||
                           frame.Format == RHI::Format::B8G8R8A8_SRGB;
        const uint32_t redChannel = bBgra ? 2u : 0u;
        const uint32_t blueChannel = bBgra ? 0u : 2u;
        double red = static_cast<double>(frame.Pixels[offset + redChannel]) / 255.0;
        double green = static_cast<double>(frame.Pixels[offset + 1u]) / 255.0;
        double blue = static_cast<double>(frame.Pixels[offset + blueChannel]) / 255.0;
        const bool bSrgbEncoded = frame.Transfer == RHI::PresentationTransfer::SRGB ||
                                  frame.bHardwareSrgbEncode || frame.bShaderSrgbEncode ||
                                  frame.Format == RHI::Format::R8G8B8A8_SRGB ||
                                  frame.Format == RHI::Format::B8G8R8A8_SRGB;
        if (bSrgbEncoded)
        {
            red = SrgbToLinear(red);
            green = SrgbToLinear(green);
            blue = SrgbToLinear(blue);
        }
        outColor = {red, green, blue};
        return true;
    }

    double Luma(const LinearRgb& color)
    {
        return 0.2126 * color.Red + 0.7152 * color.Green + 0.0722 * color.Blue;
    }

    bool GetReferenceMean(const VariableArray<uint8_t>& pixels,
                          const Region& region,
                          RegionMean& outMean)
    {
        uint64_t count = 0u;
        for (uint32_t y = region.Top; y < region.Bottom; ++y)
        {
            for (uint32_t x = region.Left; x < region.Right; ++x)
            {
                const LinearRgb color = GetRgbePixel(pixels, x, y);
                outMean.Red += color.Red;
                outMean.Green += color.Green;
                outMean.Blue += color.Blue;
                outMean.Y += Luma(color);
                ++count;
            }
        }
        if (count == 0u)
        {
            return false;
        }
        const double inverseCount = 1.0 / static_cast<double>(count);
        outMean.Red *= inverseCount;
        outMean.Green *= inverseCount;
        outMean.Blue *= inverseCount;
        outMean.Y *= inverseCount;
        return true;
    }

    bool GetCaptureMean(const CapturedFrame& frame,
                        const Region& region,
                        RegionMean& outMean)
    {
        uint64_t count = 0u;
        for (uint32_t y = region.Top; y < region.Bottom; ++y)
        {
            for (uint32_t x = region.Left; x < region.Right; ++x)
            {
                LinearRgb color;
                if (!GetCapturePixel(frame, x, y, color))
                {
                    return false;
                }
                outMean.Red += color.Red;
                outMean.Green += color.Green;
                outMean.Blue += color.Blue;
                outMean.Y += Luma(color);
                ++count;
            }
        }
        if (count == 0u)
        {
            return false;
        }
        const double inverseCount = 1.0 / static_cast<double>(count);
        outMean.Red *= inverseCount;
        outMean.Green *= inverseCount;
        outMean.Blue *= inverseCount;
        outMean.Y *= inverseCount;
        return true;
    }

    bool GetIndirectBounceMean(const CapturedFrame& frame,
                               const R4Thresholds& thresholds,
                               double& outRedY,
                               double& outGreenY)
    {
        RegionMean redMean;
        RegionMean greenMean;
        if (!GetCaptureMean(frame, thresholds.RedBounce, redMean) ||
            !GetCaptureMean(frame, thresholds.GreenBounce, greenMean))
        {
            return false;
        }
        outRedY = redMean.Y;
        outGreenY = greenMean.Y;
        return std::isfinite(outRedY) && std::isfinite(outGreenY);
    }

    double GetRedChroma(const RegionMean& mean)
    {
        const double sum = mean.Red + mean.Green + mean.Blue;
        return sum > 1.0e-12 ? mean.Red / sum : 0.0;
    }

    double GetGreenChroma(const RegionMean& mean)
    {
        const double sum = mean.Red + mean.Green + mean.Blue;
        return sum > 1.0e-12 ? mean.Green / sum : 0.0;
    }

    bool EvaluateCornellMetrics(const CapturedFrame& frame,
                                const VariableArray<uint8_t>& referencePixels,
                                const R4Thresholds& thresholds,
                                Core::Container::String& outFailure)
    {
        RegionMean directReference;
        RegionMean directCapture;
        RegionMean shadowReference;
        RegionMean shadowCapture;
        RegionMean redReference;
        RegionMean redCapture;
        RegionMean greenReference;
        RegionMean greenCapture;
        if (!GetReferenceMean(referencePixels, thresholds.DirectWhite, directReference) ||
            !GetCaptureMean(frame, thresholds.DirectWhite, directCapture) ||
            !GetReferenceMean(referencePixels, thresholds.ShadowFloor, shadowReference) ||
            !GetCaptureMean(frame, thresholds.ShadowFloor, shadowCapture) ||
            !GetReferenceMean(referencePixels, thresholds.RedBounce, redReference) ||
            !GetCaptureMean(frame, thresholds.RedBounce, redCapture) ||
            !GetReferenceMean(referencePixels, thresholds.GreenBounce, greenReference) ||
            !GetCaptureMean(frame, thresholds.GreenBounce, greenCapture) ||
            directReference.Y <= 1.0e-8 || directCapture.Y <= 1.0e-8)
        {
            LinearRgb diagnosticPixel;
            if (GetCapturePixel(frame, 250u, 98u, diagnosticPixel))
            {
                std::cout << "R4_CORNELL_DIAGNOSTIC_PIXEL x=250 y=98"
                          << " r=" << diagnosticPixel.Red
                          << " g=" << diagnosticPixel.Green
                          << " b=" << diagnosticPixel.Blue << '\n';
            }
            std::cout << "R4_CORNELL_ROI_INPUT_FAILURE"
                      << " direct_reference_y=" << directReference.Y
                      << " direct_capture_y=" << directCapture.Y
                      << " shadow_reference_y=" << shadowReference.Y
                      << " shadow_capture_y=" << shadowCapture.Y
                      << " red_reference_y=" << redReference.Y
                      << " red_capture_y=" << redCapture.Y
                      << " green_reference_y=" << greenReference.Y
                      << " green_capture_y=" << greenCapture.Y << '\n';
            double fullFrameMeanY = 0.0;
            double fullFrameMaximumY = 0.0;
            uint64_t nonBlackPixelCount = 0u;
            uint32_t maximumPixelX = 0u;
            uint32_t maximumPixelY = 0u;
            for (uint32_t pixelY = 0u; pixelY < frame.Height; ++pixelY)
            {
                for (uint32_t pixelX = 0u; pixelX < frame.Width; ++pixelX)
                {
                    LinearRgb color;
                    if (!GetCapturePixel(frame, pixelX, pixelY, color))
                    {
                        continue;
                    }
                    const double pixelLuma = Luma(color);
                    fullFrameMeanY += pixelLuma;
                    if (pixelLuma > 1.0e-6)
                    {
                        ++nonBlackPixelCount;
                    }
                    if (pixelLuma > fullFrameMaximumY)
                    {
                        fullFrameMaximumY = pixelLuma;
                        maximumPixelX = pixelX;
                        maximumPixelY = pixelY;
                    }
                }
            }
            fullFrameMeanY /= static_cast<double>(frame.Width * frame.Height);
            std::cout << "R4_CORNELL_FRAME_LUMA mean_y=" << fullFrameMeanY
                      << " max_y=" << fullFrameMaximumY
                      << " max_x=" << maximumPixelX
                      << " max_y_pixel=" << maximumPixelY
                      << " nonblack_pixels=" << nonBlackPixelCount << '\n';
            outFailure = TEXT("Cornell受入れROIを有限な値で計算できません");
            return false;
        }

        std::cout << "R4_CORNELL_CAPTURE_MEANS"
                  << " direct_y=" << directCapture.Y
                  << " shadow_y=" << shadowCapture.Y
                  << " red_y=" << redCapture.Y
                  << " green_y=" << greenCapture.Y << '\n';
        const double exposureScale = directReference.Y / directCapture.Y;
        const double shadowError = std::abs(shadowCapture.Y * exposureScale - shadowReference.Y) /
                                   shadowReference.Y;
        const double redError = std::abs(redCapture.Y * exposureScale - redReference.Y) /
                                redReference.Y;
        const double greenError = std::abs(greenCapture.Y * exposureScale - greenReference.Y) /
                                  greenReference.Y;
        const double redChromaDifference =
            std::abs(GetRedChroma(redCapture) - GetRedChroma(redReference));
        const double greenChromaDifference =
            std::abs(GetGreenChroma(greenCapture) - GetGreenChroma(greenReference));

        std::cout << "R4_CORNELL_EXPOSURE_SCALE=" << exposureScale << '\n';
        std::cout << "R4_CORNELL_SHADOW_FLOOR reference_y=" << shadowReference.Y
                  << " measured_y=" << shadowCapture.Y * exposureScale
                  << " relative_error=" << shadowError << '\n';
        std::cout << "R4_CORNELL_RED_BOUNCE reference_y=" << redReference.Y
                  << " measured_y=" << redCapture.Y * exposureScale
                  << " relative_error=" << redError
                  << " reference_chroma=" << GetRedChroma(redReference)
                  << " measured_chroma=" << GetRedChroma(redCapture)
                  << " chroma_difference=" << redChromaDifference << '\n';
        std::cout << "R4_CORNELL_GREEN_BOUNCE reference_y=" << greenReference.Y
                  << " measured_y=" << greenCapture.Y * exposureScale
                  << " relative_error=" << greenError
                  << " reference_chroma=" << GetGreenChroma(greenReference)
                  << " measured_chroma=" << GetGreenChroma(greenCapture)
                  << " chroma_difference=" << greenChromaDifference << '\n';

        if (shadowError > thresholds.MaximumRelativeYError ||
            redError > thresholds.MaximumRelativeYError ||
            greenError > thresholds.MaximumRelativeYError ||
            redChromaDifference > thresholds.MaximumChromaDifference ||
            greenChromaDifference > thresholds.MaximumChromaDifference)
        {
            outFailure = TEXT("Cornell間接ROIまたはred/green chromaがR4閾値を超えています");
            return false;
        }
        std::cout << "R4_CORNELL_REFERENCE=PASS\n";
        return true;
    }

    ImageDifference CompareDisabledFrames(const CapturedFrame& frame,
                                         const VariableArray<uint8_t>& baseline,
                                         uint32_t baselineRowPitch)
    {
        ImageDifference difference;
        uint64_t sampleCount = 0u;
        for (uint32_t y = 0u; y < frame.Height; ++y)
        {
            for (uint32_t x = 0u; x < frame.Width; ++x)
            {
                const size_t currentOffset = static_cast<size_t>(y) * frame.RowPitchBytes +
                                             static_cast<size_t>(x) * frame.BytesPerPixel;
                const size_t baselineOffset = static_cast<size_t>(y) * baselineRowPitch +
                                              static_cast<size_t>(x) * frame.BytesPerPixel;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    double delta = 0.0;
                    if (IsByteBackBufferFormat(frame.Format))
                    {
                        const int current = frame.Pixels[currentOffset + channel];
                        const int reference = baseline[baselineOffset + channel];
                        delta = std::abs(current - reference) / 255.0;
                    }
                    else
                    {
                        LinearRgb current;
                        LinearRgb reference;
                        if (!GetCapturePixel(frame, x, y, current))
                        {
                            return difference;
                        }
                        const size_t halfOffset = static_cast<size_t>(y) * baselineRowPitch +
                                                  static_cast<size_t>(x) * frame.BytesPerPixel;
                        uint16_t channels[4] = {};
                        std::memcpy(channels, baseline.data() + halfOffset, sizeof(channels));
                        reference = {
                            HalfToFloat(channels[0]),
                            HalfToFloat(channels[1]),
                            HalfToFloat(channels[2])};
                        const double currentValues[3] = {current.Red, current.Green, current.Blue};
                        const double referenceValues[3] = {
                            reference.Red, reference.Green, reference.Blue};
                        delta = std::abs(currentValues[channel] - referenceValues[channel]);
                    }
                    difference.MeanAbsoluteDelta += delta;
                    difference.MaximumAbsoluteDelta =
                        std::max(difference.MaximumAbsoluteDelta, delta);
                    ++sampleCount;
                }
            }
        }
        if (sampleCount > 0u)
        {
            difference.MeanAbsoluteDelta /= static_cast<double>(sampleCount);
        }
        return difference;
    }

    class RenderingDDGIHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(const VariableArray<String>& args) override
        {
            m_bScenarioParsed = false;
            m_bStateReady = true;
            m_Scenario = Scenario::CornellReference;
            m_Stage = CaptureStage::CornellDisabledBaseline;
            m_EnabledCaptureCount = 0u;
            m_DynamicInitialCaptureCount = 0u;
            m_DynamicCaptureCount = 0u;
            m_DynamicFrameNumbers.fill(0u);
            m_DynamicLuma.fill(0.0);
            m_DynamicRedY.fill(0.0);
            m_DynamicGreenY.fill(0.0);
            m_OffBaselinePixels.clear();
            m_OffBaselineRowPitch = 0u;
            m_OffBaselineFormat = RHI::Format::UNKNOWN;
            m_LastLightOffset = 0.0f;
            if (!LoadThresholds(m_Thresholds) || !LoadCornellRgbe(m_ReferencePixels))
            {
                return false;
            }
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            if (!m_bScenarioParsed ||
                GetRunConfig().CaptureSource != FrameCaptureSourceKind::SceneColor)
            {
                return false;
            }
            m_Stage = m_Scenario == Scenario::CornellReference
                          ? CaptureStage::CornellDisabledBaseline
                          : CaptureStage::DynamicInitial;
            return true;
        }

        bool OnInitialize() override
        {
            return RenderingValidationApplicationHandler::OnInitialize() &&
                   GetFixture().ApplyR4CornellFixture();
        }

    protected:
        bool ParseAdditionalArgument(const String& argument, String& outFailureReason) override
        {
            if (argument == TEXT("--scenario=cornell-reference") ||
                argument == TEXT("--scenario=dynamic-update"))
            {
                if (m_bScenarioParsed)
                {
                    outFailureReason = TEXT("R4 DDGI scenarioが重複しています");
                    return false;
                }
                m_bScenarioParsed = true;
                m_Scenario = argument == TEXT("--scenario=cornell-reference")
                                 ? Scenario::CornellReference
                                 : Scenario::DynamicUpdate;
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(
                argument, outFailureReason);
        }

        void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld) override
        {
            renderWorld.SetMainCamera(GetFixture().GetR4CornellCamera());
            bool bEnabled = true;
            if (m_Scenario == Scenario::CornellReference)
            {
                bEnabled = m_Stage == CaptureStage::CornellEnabled;
            }
            renderWorld.SetDDGIVolumeParameters(MakeCornellVolume(bEnabled));

            const float requestedOffset =
                m_Scenario == Scenario::DynamicUpdate && m_Stage == CaptureStage::DynamicMoved
                    ? static_cast<float>(m_Thresholds.LightOffsetX)
                    : 0.0f;
            if (requestedOffset != m_LastLightOffset)
            {
                m_bStateReady = GetFixture().SetR4CornellLightOffsetX(requestedOffset);
                m_LastLightOffset = requestedOffset;
            }
        }

        bool EvaluateCapturedFrame(const CapturedFrame& frame,
                                   String& outFailureReason) override
        {
            if (!m_bStateReady)
            {
                outFailureReason = TEXT("R4 Cornell fixtureのライト状態を更新できません");
                return false;
            }
            if (!IsSupportedCapture(frame))
            {
                std::cerr << "R4_CAPTURE_FORMAT_FAILURE format=" << static_cast<uint32_t>(frame.Format)
                          << " bpp=" << frame.BytesPerPixel
                          << " width=" << frame.Width << " height=" << frame.Height << '\n';
                outFailureReason = TEXT("バックバッファcaptureが512x512の対応HDR/表示形式ではありません");
                return false;
            }

            switch (m_Stage)
            {
            case CaptureStage::CornellDisabledBaseline:
                m_OffBaselinePixels.resize(frame.Pixels.size());
                std::memcpy(m_OffBaselinePixels.data(), frame.Pixels.data(), frame.Pixels.size());
                m_OffBaselineRowPitch = frame.RowPitchBytes;
                m_OffBaselineFormat = frame.Format;
                std::cout << "R4_DDGI_DISABLED_BASELINE=PASS frame=" << frame.FrameNumber
                          << " format=" << static_cast<uint32_t>(frame.Format) << '\n';
                return true;

            case CaptureStage::CornellEnabled:
                ++m_EnabledCaptureCount;
                std::cout << "R4_CORNELL_DDGI_CAPTURE frame=" << frame.FrameNumber
                          << " sample=" << m_EnabledCaptureCount << '\n';
                if (m_EnabledCaptureCount == m_Thresholds.DisabledWarmupFrames)
                {
                    if (!EvaluateCornellMetrics(
                            frame, m_ReferencePixels, m_Thresholds, outFailureReason))
                    {
                        return false;
                    }
                    const ImageDifference enabledDifference = CompareDisabledFrames(
                        frame, m_OffBaselinePixels, m_OffBaselineRowPitch);
                    std::cout << "R4_DDGI_ENABLED_AB mean_delta="
                              << enabledDifference.MeanAbsoluteDelta
                              << " max_delta=" << enabledDifference.MaximumAbsoluteDelta << '\n';
                    if (enabledDifference.MeanAbsoluteDelta < m_Thresholds.MinimumEnabledMeanDelta)
                    {
                        outFailureReason = TEXT("DDGI有効時の間接照明変化が正の対照閾値に達しません");
                        return false;
                    }
                }
                return true;

            case CaptureStage::CornellDisabledVerify:
            {
                if (m_OffBaselinePixels.empty() || m_OffBaselineFormat != frame.Format ||
                    m_OffBaselineRowPitch < frame.Width * frame.BytesPerPixel)
                {
                    outFailureReason = TEXT("DDGI無効baselineと最終captureの形式が一致しません");
                    return false;
                }
                const ImageDifference difference = CompareDisabledFrames(
                    frame, m_OffBaselinePixels, m_OffBaselineRowPitch);
                std::cout << "R4_DDGI_DISABLED_AB mean_delta=" << difference.MeanAbsoluteDelta
                          << " max_delta=" << difference.MaximumAbsoluteDelta << '\n';
                if (difference.MeanAbsoluteDelta > m_Thresholds.DisabledMeanDelta ||
                    difference.MaximumAbsoluteDelta > m_Thresholds.DisabledMaximumDelta)
                {
                    outFailureReason = TEXT("DDGI無効時の描画が開始時の既存描画閾値を超えています");
                    return false;
                }
                std::cout << "R4_DDGI_DISABLED_AB=PASS\n";
                return true;
            }

            case CaptureStage::DynamicInitial:
            {
                double redY = 0.0;
                double greenY = 0.0;
                if (!GetIndirectBounceMean(frame, m_Thresholds, redY, greenY))
                {
                    outFailureReason = TEXT("動的更新の初期間接ROIを計算できません");
                    return false;
                }
                ++m_DynamicInitialCaptureCount;
                const double roiY = (redY + greenY) * 0.5;
                if (m_DynamicInitialCaptureCount >= CornellDynamicInitialWarmupSampleCount)
                {
                    m_DynamicLuma[0] = roiY;
                    m_DynamicRedY[0] = redY;
                    m_DynamicGreenY[0] = greenY;
                    m_DynamicFrameNumbers[0] = frame.FrameNumber;
                    std::cout << "R4_DYNAMIC_INITIAL frame=" << frame.FrameNumber
                              << " warmup_samples=" << m_DynamicInitialCaptureCount
                              << " red_y=" << redY
                              << " green_y=" << greenY
                              << " roi_y=" << m_DynamicLuma[0] << '\n';
                }
                else
                {
                    std::cout << "R4_DYNAMIC_INITIAL_WARMUP frame=" << frame.FrameNumber
                              << " sample=" << m_DynamicInitialCaptureCount
                              << " red_y=" << redY
                              << " green_y=" << greenY
                              << " roi_y=" << roiY << '\n';
                }
                return true;
            }

            case CaptureStage::DynamicMoved:
            {
                double redY = 0.0;
                double greenY = 0.0;
                if (m_DynamicCaptureCount >= CornellDynamicSampleCount ||
                    !GetIndirectBounceMean(frame, m_Thresholds, redY, greenY))
                {
                    outFailureReason = TEXT("動的更新の間接ROI sampleを計算できません");
                    return false;
                }
                ++m_DynamicCaptureCount;
                m_DynamicLuma[m_DynamicCaptureCount] = (redY + greenY) * 0.5;
                m_DynamicRedY[m_DynamicCaptureCount] = redY;
                m_DynamicGreenY[m_DynamicCaptureCount] = greenY;
                m_DynamicFrameNumbers[m_DynamicCaptureCount] = frame.FrameNumber;
                std::cout << "R4_DYNAMIC_SAMPLE frame=" << frame.FrameNumber
                          << " update_frame=" << m_DynamicCaptureCount
                          << " red_y=" << redY
                          << " green_y=" << greenY
                          << " roi_y=" << m_DynamicLuma[m_DynamicCaptureCount] << '\n';
                if (m_DynamicCaptureCount == m_Thresholds.DynamicFrameLimit)
                {
                    return EvaluateDynamicConvergence(outFailureReason);
                }
                return true;
            }

            case CaptureStage::Complete:
                break;
            }
            outFailureReason = TEXT("R4 DDGI captureが想定外の段階です");
            return false;
        }

        bool RequestFollowupCapture(const CapturedFrame&,
                                    FrameCaptureRequest& outRequest) override
        {
            if (m_Stage == CaptureStage::Complete)
            {
                return false;
            }
            outRequest.SourceKind = FrameCaptureSourceKind::SceneColor;
            return true;
        }

        void AdvanceCaptureStage() override
        {
            switch (m_Stage)
            {
            case CaptureStage::CornellDisabledBaseline:
                m_Stage = CaptureStage::CornellEnabled;
                break;
            case CaptureStage::CornellEnabled:
                if (m_EnabledCaptureCount >= m_Thresholds.DisabledWarmupFrames)
                {
                    m_Stage = CaptureStage::CornellDisabledVerify;
                }
                break;
            case CaptureStage::CornellDisabledVerify:
                m_Stage = CaptureStage::Complete;
                break;
            case CaptureStage::DynamicInitial:
                if (m_DynamicInitialCaptureCount >= CornellDynamicInitialWarmupSampleCount)
                {
                    m_Stage = CaptureStage::DynamicMoved;
                }
                break;
            case CaptureStage::DynamicMoved:
                if (m_DynamicCaptureCount >= m_Thresholds.DynamicFrameLimit)
                {
                    m_Stage = CaptureStage::Complete;
                }
                break;
            case CaptureStage::Complete:
                break;
            }
        }

    private:
        static DDGIVolumeParameters MakeCornellVolume(bool bEnabled)
        {
            DDGIVolumeParameters parameters;
            parameters.bEnabled = bEnabled;
            parameters.Origin = Math::Vector3(
                CornellProbeOrigin, CornellProbeOrigin, CornellProbeOrigin);
            parameters.ProbeSpacing = Math::Vector3(
                CornellProbeSpacing, CornellProbeSpacing, CornellProbeSpacing);
            parameters.ProbeCountX = CornellProbeCountAxis;
            parameters.ProbeCountY = CornellProbeCountAxis;
            parameters.ProbeCountZ = CornellProbeCountAxis;
            return parameters;
        }

        bool EvaluateDynamicConvergence(String& outFailureReason)
        {
            bool bObservedBeforeDeadline = false;
            double redDeadlineProgress = 0.0;
            double greenDeadlineProgress = 0.0;
            uint64_t deadlineSampleElapsedFrames = 0u;
            const auto evaluateChannel = [&](const char* channelName,
                                             const FixedArray<double, CornellDynamicSampleCount + 1u>& values,
                                             double& outDeadlineProgress) -> bool
            {
                const double initial = values[0];
                const double final = values[m_DynamicCaptureCount];
                const double finalChange = final - initial;
                if (std::abs(finalChange) < m_Thresholds.MinimumDynamicChange)
                {
                    outFailureReason = TEXT("ライト移動による赤緑間接ROIの変化が小さすぎます");
                    return false;
                }

                double previousProgress = 0.0;
                for (uint32_t updateFrame = 1u;
                     updateFrame <= m_DynamicCaptureCount;
                     ++updateFrame)
                {
                    const uint64_t frameNumber = m_DynamicFrameNumbers[updateFrame];
                    const uint64_t previousFrameNumber = m_DynamicFrameNumbers[updateFrame - 1u];
                    if (frameNumber <= previousFrameNumber)
                    {
                        outFailureReason = TEXT("動的DDGI captureのFrameNumberが単調増加していません");
                        return false;
                    }
                    const uint64_t elapsedFrames = frameNumber - m_DynamicFrameNumbers[0];
                    const double progress = (values[updateFrame] - initial) / finalChange;
                    if (!std::isfinite(progress) ||
                        (previousProgress < 1.0 && progress + 1.0e-4 < previousProgress) ||
                        progress > m_Thresholds.MaximumDynamicProgressStep)
                    {
                        std::cerr << "R4_DYNAMIC_MONOTONIC_FAILURE channel=" << channelName
                                  << " update_frame=" << updateFrame
                                  << " progress=" << progress
                                  << " previous=" << previousProgress << '\n';
                        outFailureReason = TEXT("赤緑間接ROIが最終値へ単調に収束していません");
                        return false;
                    }
                    // 最終値をわずかに超えた後の半精度由来の settling は、到達済みとして飽和させる。
                    const double settledProgress = std::min(progress, 1.0);
                    if (elapsedFrames <= CornellDynamicDeadlineFrames)
                    {
                        bObservedBeforeDeadline = true;
                        deadlineSampleElapsedFrames = elapsedFrames;
                        outDeadlineProgress = settledProgress;
                    }
                    previousProgress = std::max(previousProgress, settledProgress);
                }
                return true;
            };

            if (!evaluateChannel("red", m_DynamicRedY, redDeadlineProgress) ||
                !evaluateChannel("green", m_DynamicGreenY, greenDeadlineProgress))
            {
                return false;
            }

            std::cout << "R4_DYNAMIC_FRAME_8 red_progress=" << redDeadlineProgress
                      << " green_progress=" << greenDeadlineProgress
                      << " elapsed_frames=" << deadlineSampleElapsedFrames
                      << " observed=" << (bObservedBeforeDeadline ? 1 : 0) << '\n';
            if (!bObservedBeforeDeadline ||
                redDeadlineProgress < m_Thresholds.MinimumDynamicProgress ||
                greenDeadlineProgress < m_Thresholds.MinimumDynamicProgress)
            {
                outFailureReason = TEXT("8 frame以内に赤緑間接ROIが必要な割合まで収束しません");
                return false;
            }

            const double initial = m_DynamicLuma[0];
            const double final = m_DynamicLuma[m_DynamicCaptureCount];
            std::cout << "R4_DYNAMIC_CONVERGENCE=PASS initial_y=" << initial
                      << " final_y=" << final
                      << " final_change=" << (final - initial)
                      << " red_frame8_progress=" << redDeadlineProgress
                      << " green_frame8_progress=" << greenDeadlineProgress
                      << " samples=" << m_DynamicCaptureCount << '\n';
            return true;
        }

        bool m_bScenarioParsed = false;
        bool m_bStateReady = true;
        Scenario m_Scenario = Scenario::CornellReference;
        CaptureStage m_Stage = CaptureStage::CornellDisabledBaseline;
        R4Thresholds m_Thresholds;
        VariableArray<uint8_t> m_ReferencePixels;
        VariableArray<uint8_t> m_OffBaselinePixels;
        uint32_t m_OffBaselineRowPitch = 0u;
        RHI::Format m_OffBaselineFormat = RHI::Format::UNKNOWN;
        uint32_t m_EnabledCaptureCount = 0u;
        uint32_t m_DynamicInitialCaptureCount = 0u;
        uint32_t m_DynamicCaptureCount = 0u;
        FixedArray<double, CornellDynamicSampleCount + 1u> m_DynamicLuma{};
        FixedArray<double, CornellDynamicSampleCount + 1u> m_DynamicRedY{};
        FixedArray<double, CornellDynamicSampleCount + 1u> m_DynamicGreenY{};
        FixedArray<uint64_t, CornellDynamicSampleCount + 1u> m_DynamicFrameNumbers{};
        float m_LastLightOffset = 0.0f;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return MakeShared<RenderingDDGIHandler>();
    }
}

int main(int argc, char** argv)
{
    if (NorvesLib::Test::RenderingValidation::IsForcedGpuTestSkipRequested())
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "環境変数によりGPU検証をスキップします");
    }

    NorvesLib::Core::Container::String reason;
    if (!NorvesLib::Test::RenderingValidation::CanCreateVulkanDeviceForGpuTest(reason))
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "Vulkanデバイスを利用できません");
    }

    NorvesLib::RHI::RHIDeviceDesc deviceDesc;
    deviceDesc.Api = NorvesLib::RHI::GraphicsAPI::Vulkan;
    NorvesLib::RHI::DevicePtr device = NorvesLib::RHI::CreateRHIDevice(deviceDesc);
    if (!device)
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "Vulkanデバイスを作成できません");
    }
    const NorvesLib::RHI::RayTracingCapabilities capabilities =
        device->GetCapabilities().RayTracing;
    device->WaitIdle();
    device.reset();
    if (!capabilities.bAccelerationStructure || !capabilities.bRayQuery)
    {
        return NorvesLib::Test::RenderingValidation::ReportGpuTestSkip(
            TestName, "DDGIに必要なVulkan acceleration structure/ray queryが利用できません");
    }

    bool bSceneSpecified = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], "--scene=", 8u) == 0)
        {
            bSceneSpecified = true;
            break;
        }
    }

    NorvesLib::Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("R4 Cornell DDGI GPU受入れ");
    config.WindowWidth = CornellImageWidth;
    config.WindowHeight = CornellImageHeight;
    config.bResizable = false;
    config.bVSync = false;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = true;
    config.Api = NorvesLib::RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingDDGIVulkan.log");
    config.CreateHandler = &CreateHandler;
    if (!bSceneSpecified)
    {
        config.Arguments.push_back(TEXT("--scene=indoor"));
    }
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(NorvesLib::Core::Container::String(argv[index]));
    }

    NorvesLib::RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting();
    const int result = NorvesLib::Core::Boot::LaunchApplication(config);
    NorvesLib::RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting();
    const uint32_t validationErrorCount =
        NorvesLib::RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
    std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
    return validationErrorCount == 0u ? result : 1;
}
