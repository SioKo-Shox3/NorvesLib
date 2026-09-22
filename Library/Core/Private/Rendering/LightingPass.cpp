#include "Rendering/LightingPass.h"
#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/LightingPassLightPacking.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/SceneView.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FramePacket.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

// HDR環境マップロード用
#include "stb_image.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // float32 → float16 (round-to-nearest-even) 変換ヘルパー
    // ========================================
    static uint16_t FloatToHalfRne(float value)
    {
        union
        {
            float f;
            uint32_t u;
        } conv;
        conv.f = value;
        uint32_t f32 = conv.u;

        const uint16_t sign = static_cast<uint16_t>((f32 >> 16u) & 0x8000u);
        const uint32_t exponentBits = (f32 >> 23u) & 0xFFu;
        const uint32_t mantissa = f32 & 0x007FFFFFu;
        if (exponentBits == 0xFFu)
        {
            return mantissa == 0u ? sign | 0x7C00u : 0x7E00u;
        }

        const int32_t exponent = static_cast<int32_t>(exponentBits) - 127;
        if (exponent > 15)
        {
            return sign | 0x7C00u;
        }
        if (exponent >= -14)
        {
            uint32_t roundedMantissa = mantissa;
            const uint32_t truncated = roundedMantissa >> 13u;
            const uint32_t remainder = roundedMantissa & 0x1FFFu;
            const bool bRoundUp = remainder > 0x1000u ||
                                   (remainder == 0x1000u && (truncated & 1u) != 0u);
            roundedMantissa = truncated + (bRoundUp ? 1u : 0u);
            int32_t roundedExponent = exponent;
            if (roundedMantissa >= 0x400u)
            {
                roundedMantissa = 0u;
                ++roundedExponent;
            }
            if (roundedExponent > 15)
            {
                return sign | 0x7C00u;
            }
            return sign |
                   static_cast<uint16_t>((roundedExponent + 15) << 10u) |
                   static_cast<uint16_t>(roundedMantissa);
        }
        if (exponent >= -25)
        {
            const uint32_t normalizedMantissa = mantissa | 0x00800000u;
            const uint32_t shift = static_cast<uint32_t>(-exponent - 1);
            const uint32_t truncated = normalizedMantissa >> shift;
            const uint32_t remainderMask = (1u << shift) - 1u;
            const uint32_t remainder = normalizedMantissa & remainderMask;
            const uint32_t halfway = 1u << (shift - 1u);
            const bool bRoundUp = remainder > halfway ||
                                   (remainder == halfway && (truncated & 1u) != 0u);
            return sign | static_cast<uint16_t>(truncated + (bRoundUp ? 1u : 0u));
        }
        return sign;
    }

    static uint32_t ReverseBits32(uint32_t value)
    {
        value = (value << 16u) | (value >> 16u);
        value = ((value & 0x55555555u) << 1u) | ((value & 0xAAAAAAAAu) >> 1u);
        value = ((value & 0x33333333u) << 2u) | ((value & 0xCCCCCCCCu) >> 2u);
        value = ((value & 0x0F0F0F0Fu) << 4u) | ((value & 0xF0F0F0F0u) >> 4u);
        value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
        return value;
    }

    static float HalfToFloat(uint16_t value)
    {
        const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16u;
        const uint32_t exponent = (static_cast<uint32_t>(value) >> 10u) & 0x1Fu;
        const uint32_t mantissa = static_cast<uint32_t>(value) & 0x03FFu;
        uint32_t bits = sign;
        if (exponent == 0u)
        {
            if (mantissa != 0u)
            {
                const float significand = static_cast<float>(mantissa) / 1024.0f;
                return std::ldexp(significand, -14) * (sign != 0u ? -1.0f : 1.0f);
            }
            union
            {
                uint32_t u;
                float f;
            } zero = {sign};
            return zero.f;
        }
        if (exponent == 0x1Fu)
        {
            bits |= 0x7F800000u | (mantissa << 13u);
        }
        else
        {
            bits |= (exponent + (127u - 15u)) << 23u;
            bits |= mantissa << 13u;
        }
        union
        {
            uint32_t u;
            float f;
        } result = {bits};
        return result.f;
    }

    static bool TryScaleSourceValue(float fileValue,
                                   double luminanceScale,
                                   float& outScaledValue)
    {
        if (!std::isfinite(luminanceScale) || luminanceScale < 0.0)
        {
            return false;
        }
        if (!std::isfinite(fileValue) || fileValue < 0.0f)
        {
            return false;
        }

        const double scaledValue = static_cast<double>(fileValue) * luminanceScale;
        if (!std::isfinite(scaledValue) || scaledValue < 0.0 ||
            scaledValue >= 65504.0)
        {
            return false;
        }

        outScaledValue = static_cast<float>(scaledValue);
        return true;
    }

    static bool ValidateCanonicalSource(const Container::VariableArray<float>& sourceData,
                                        uint32_t width,
                                        uint32_t height)
    {
        if (width == 0u || height == 0u ||
            sourceData.size() < static_cast<size_t>(width) * height * 4u)
        {
            return false;
        }
        const size_t pixelCount = static_cast<size_t>(width) * height;
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float value = sourceData[pixel * 4u + channel];
                float validatedValue = 0.0f;
                if (!TryScaleSourceValue(value, 1.0, validatedValue))
                {
                    return false;
                }
                const float canonical = HalfToFloat(FloatToHalfRne(validatedValue));
                if (!std::isfinite(canonical) || canonical < 0.0f ||
                    static_cast<double>(canonical) >= 65504.0)
                {
                    return false;
                }
            }
            if (!std::isfinite(sourceData[pixel * 4u + 3u]))
            {
                return false;
            }
        }
        return true;
    }

    static bool ValidateProductionInitializationInvariants(float scale)
    {
        if (!std::isfinite(scale) || scale < 0.0f)
        {
            return false;
        }

        union
        {
            uint32_t u;
            float f;
        } negativeNaN = {0xFFC00001u};
        const bool bRneTable =
            FloatToHalfRne(0.0f) == 0x0000u &&
            FloatToHalfRne(-0.0f) == 0x8000u &&
            FloatToHalfRne(1.0f) == 0x3C00u &&
            FloatToHalfRne(-2.0f) == 0xC000u &&
            FloatToHalfRne(65504.0f) == 0x7BFFu &&
            FloatToHalfRne(std::ldexp(1.0f, 16)) == 0x7C00u &&
            FloatToHalfRne(-std::ldexp(1.0f, 16)) == 0xFC00u &&
            FloatToHalfRne(std::ldexp(1.0f, -24)) == 0x0001u &&
            FloatToHalfRne(std::ldexp(1.0f, -25)) == 0x0000u &&
            FloatToHalfRne(1.0f + std::ldexp(1.0f, -11)) == 0x3C00u &&
            FloatToHalfRne(1.0f + 3.0f * std::ldexp(1.0f, -11)) == 0x3C02u &&
            FloatToHalfRne(std::numeric_limits<float>::infinity()) == 0x7C00u &&
            FloatToHalfRne(-std::numeric_limits<float>::infinity()) == 0xFC00u &&
            FloatToHalfRne(negativeNaN.f) == 0x7E00u;
        if (!bRneTable)
        {
            return false;
        }

        struct SourcePredicateCase
        {
            float fileValue;
            double luminanceScale;
            bool bExpectedValid;
        };
        const SourcePredicateCase sourcePredicateCases[] =
        {
            {-0.0f, 0.0, true},
            {0.0f, 0.0, true},
            {1.0f, 0.0, true},
            {65503.0f, 0.0, true},
            {1.0f, 1.0, true},
            {65503.0f, 1.0, true},
            {-1.0f, 1.0, false},
            {65504.0f, 1.0, false},
            {std::numeric_limits<float>::quiet_NaN(), 1.0, false},
            {std::numeric_limits<float>::infinity(), 0.0, false},
            {-std::numeric_limits<float>::infinity(), 1.0, false},
            {32768.0f, 2.0, false},
            {1.0f, -1.0, false},
            {1.0f, std::numeric_limits<double>::quiet_NaN(), false},
            {1.0f, std::numeric_limits<double>::infinity(), false},
            {1.0f, -std::numeric_limits<double>::infinity(), false},
        };
        for (const SourcePredicateCase& sourceCase : sourcePredicateCases)
        {
            float scaledValue = 0.0f;
            const bool bActualValid =
                TryScaleSourceValue(sourceCase.fileValue,
                                    sourceCase.luminanceScale,
                                    scaledValue);
            if (bActualValid != sourceCase.bExpectedValid)
            {
                return false;
            }
        }
        return true;
    }

    static bool CreateIblResources(RHI::IDevice* device,
                                   const Container::VariableArray<float>& sourceData,
                                   uint32_t sourceWidth,
                                   uint32_t sourceHeight,
                                   const char* environmentName,
                                   const char* diffuseName,
                                   const char* prefilterName,
                                   RHI::TexturePtr& outEnvironment,
                                   RHI::TexturePtr& outDiffuse,
                                   RHI::TexturePtr& outPrefilter)
    {
        constexpr uint32_t workingWidth = 256u;
        constexpr uint32_t workingHeight = 128u;
        constexpr uint32_t diffuseWidth = 64u;
        constexpr uint32_t diffuseHeight = 32u;
        constexpr uint32_t prefilterMipCount = 9u;
        constexpr uint32_t prefilterSamples = 1024u;
        constexpr double pi = 3.14159265358979323846;

        outEnvironment.reset();
        outDiffuse.reset();
        outPrefilter.reset();
        if (device == nullptr || !ValidateCanonicalSource(sourceData, sourceWidth, sourceHeight))
        {
            return false;
        }

        auto createRgbaTexture = [device](uint32_t width,
                                          uint32_t height,
                                          uint32_t mipLevels,
                                          const char* debugName) -> RHI::TexturePtr
        {
            RHI::TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = mipLevels;
            desc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
            desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            desc.DebugName = debugName;
            return device->CreateTexture(desc);
        };

        const size_t sourcePixelCount = static_cast<size_t>(sourceWidth) * sourceHeight;
        Container::VariableArray<uint16_t> sourceHalf(sourcePixelCount * 4u);
        Container::VariableArray<float> canonicalSource(sourcePixelCount * 4u);
        for (size_t index = 0u; index < sourceHalf.size(); ++index)
        {
            sourceHalf[index] = FloatToHalfRne(sourceData[index]);
            canonicalSource[index] = HalfToFloat(sourceHalf[index]);
        }

        double constantSourceRgb[3] = {};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            constantSourceRgb[channel] = static_cast<double>(canonicalSource[channel]);
        }
        bool bConstantSource = true;
        for (size_t pixel = 0u; pixel < sourcePixelCount && bConstantSource; ++pixel)
        {
            const size_t offset = pixel * 4u;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                if (static_cast<double>(canonicalSource[offset + channel]) !=
                    constantSourceRgb[channel])
                {
                    bConstantSource = false;
                    break;
                }
            }
        }

        auto validateGeneratedRgb =
            [&](const Container::VariableArray<uint16_t>& halfData,
                size_t pixelCount,
                const double expectedRgb[3],
                bool bCheckExpected,
                const char* stageName) -> bool
        {
            if (halfData.size() < pixelCount * 4u)
            {
                NORVES_LOG_ERROR("LightingPass",
                                 "IBL %s post-RNE buffer size is invalid",
                                 stageName);
                return false;
            }
            for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
            {
                const size_t offset = pixel * 4u;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    const double decoded = static_cast<double>(HalfToFloat(halfData[offset + channel]));
                    if (!std::isfinite(decoded) || decoded < 0.0 || decoded >= 65504.0)
                    {
                        NORVES_LOG_ERROR("LightingPass",
                                         "IBL %s post-RNE range check failed: pixel=%zu channel=%u value=%g",
                                         stageName,
                                         pixel,
                                         channel,
                                         decoded);
                        return false;
                    }
                    if (bCheckExpected && channel < 3u)
                    {
                        const double expected = expectedRgb[channel];
                        if (expected == 0.0)
                        {
                            if (decoded != 0.0)
                            {
                                NORVES_LOG_ERROR("LightingPass",
                                                 "IBL %s constant zero check failed: pixel=%zu channel=%u value=%g",
                                                 stageName,
                                                 pixel,
                                                 channel,
                                                 decoded);
                                return false;
                            }
                        }
                        else
                        {
                            const double relative = std::abs(decoded - expected) /
                                                     std::abs(expected);
                            if (!std::isfinite(relative) || relative > 1.0e-3)
                            {
                                NORVES_LOG_ERROR("LightingPass",
                                                 "IBL %s constant check failed: pixel=%zu channel=%u value=%g expected=%g relative=%g",
                                                 stageName,
                                                 pixel,
                                                 channel,
                                                 decoded,
                                                 expected,
                                                 relative);
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };

        const double sourceExpectedRgb[3] = {
            constantSourceRgb[0], constantSourceRgb[1], constantSourceRgb[2]};
        const double diffuseExpectedRgb[3] = {
            constantSourceRgb[0] * pi,
            constantSourceRgb[1] * pi,
            constantSourceRgb[2] * pi};
        if (!validateGeneratedRgb(sourceHalf,
                                  sourcePixelCount,
                                  sourceExpectedRgb,
                                  false,
                                  "source"))
        {
            return false;
        }

        RHI::TexturePtr environmentTexture =
            createRgbaTexture(sourceWidth, sourceHeight, 1u, environmentName);
        if (!environmentTexture)
        {
            return false;
        }
        const uint32_t sourceRowPitch = sourceWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
        environmentTexture->Update(sourceHalf.data(), sourceRowPitch,
                                    sourceRowPitch * sourceHeight);

        const size_t workingPixelCount = static_cast<size_t>(workingWidth) * workingHeight;
        Container::VariableArray<uint16_t> workingHalf(workingPixelCount * 4u, 0u);
        Container::VariableArray<float> workingData(workingPixelCount * 4u, 0.0f);
        if (sourceWidth == workingWidth && sourceHeight == workingHeight)
        {
            for (size_t index = 0u; index < workingHalf.size(); ++index)
            {
                workingHalf[index] = sourceHalf[index];
                workingData[index] = HalfToFloat(workingHalf[index]);
            }
        }
        else
        {
            for (uint32_t y = 0u; y < workingHeight; ++y)
            {
                const uint32_t sourceY0 = (y * sourceHeight) / workingHeight;
                const uint32_t sourceY1 = (std::max)(sourceY0 + 1u,
                                                     ((y + 1u) * sourceHeight) / workingHeight);
                for (uint32_t x = 0u; x < workingWidth; ++x)
                {
                    const uint32_t sourceX0 = (x * sourceWidth) / workingWidth;
                    const uint32_t sourceX1 = (std::max)(sourceX0 + 1u,
                                                         ((x + 1u) * sourceWidth) / workingWidth);
                    double weighted[3] = {};
                    double weightTotal = 0.0;
                    for (uint32_t sourceY = sourceY0;
                         sourceY < sourceY1 && sourceY < sourceHeight;
                         ++sourceY)
                    {
                        const double theta = pi *
                            (static_cast<double>(sourceY) + 0.5) / sourceHeight;
                        const double weight = std::sin(theta);
                        for (uint32_t sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                        {
                            const uint32_t wrappedX = sourceX % sourceWidth;
                            const size_t sourceOffset =
                                (static_cast<size_t>(sourceY) * sourceWidth + wrappedX) * 4u;
                            for (uint32_t channel = 0u; channel < 3u; ++channel)
                            {
                                weighted[channel] +=
                                    static_cast<double>(canonicalSource[sourceOffset + channel]) * weight;
                            }
                            weightTotal += weight;
                        }
                    }
                    if (!std::isfinite(weightTotal) || weightTotal <= 0.0)
                    {
                        return false;
                    }
                    const size_t workingOffset =
                        (static_cast<size_t>(y) * workingWidth + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double value = weighted[channel] / weightTotal;
                        if (!std::isfinite(value) || value < 0.0 || value >= 65504.0)
                        {
                            return false;
                        }
                        workingHalf[workingOffset + channel] =
                            FloatToHalfRne(static_cast<float>(value));
                        workingData[workingOffset + channel] =
                            HalfToFloat(workingHalf[workingOffset + channel]);
                    }
                    workingHalf[workingOffset + 3u] = FloatToHalfRne(1.0f);
                    workingData[workingOffset + 3u] = 1.0f;
                }
            }
        }
        if (!validateGeneratedRgb(workingHalf,
                                  workingPixelCount,
                                  sourceExpectedRgb,
                                  false,
                                  "working"))
        {
            return false;
        }

        Container::VariableArray<float> workingDirections(workingPixelCount * 3u, 0.0f);
        Container::VariableArray<double> workingSolidAngles(workingPixelCount, 0.0);
        const double deltaU = 2.0 * pi / static_cast<double>(workingWidth);
        const double deltaV = pi / static_cast<double>(workingHeight);
        for (uint32_t y = 0u; y < workingHeight; ++y)
        {
            const double theta = pi * (static_cast<double>(y) + 0.5) / workingHeight;
            const double sinTheta = std::sin(theta);
            const double cosTheta = std::cos(theta);
            for (uint32_t x = 0u; x < workingWidth; ++x)
            {
                const double phi = 2.0 * pi *
                    ((static_cast<double>(x) + 0.5) / workingWidth - 0.5);
                const size_t index = static_cast<size_t>(y) * workingWidth + x;
                workingDirections[index * 3u + 0u] =
                    static_cast<float>(sinTheta * std::cos(phi));
                workingDirections[index * 3u + 1u] = static_cast<float>(cosTheta);
                workingDirections[index * 3u + 2u] =
                    static_cast<float>(sinTheta * std::sin(phi));
                workingSolidAngles[index] = deltaU * deltaV * sinTheta;
            }
        }

        RHI::TexturePtr diffuseTexture =
            createRgbaTexture(diffuseWidth, diffuseHeight, 1u, diffuseName);
        RHI::TexturePtr prefilterTexture =
            createRgbaTexture(workingWidth, workingHeight, prefilterMipCount, prefilterName);
        if (!diffuseTexture || !prefilterTexture)
        {
            return false;
        }

        Container::VariableArray<uint16_t> diffuseHalf(
            static_cast<size_t>(diffuseWidth) * diffuseHeight * 4u, 0u);
        for (uint32_t y = 0u; y < diffuseHeight; ++y)
        {
            const double theta = pi * (static_cast<double>(y) + 0.5) / diffuseHeight;
            const double sinTheta = std::sin(theta);
            const double cosTheta = std::cos(theta);
            for (uint32_t x = 0u; x < diffuseWidth; ++x)
            {
                const double phi = 2.0 * pi *
                    ((static_cast<double>(x) + 0.5) / diffuseWidth - 0.5);
                const double normal[3] = {
                    sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
                double sum[3] = {};
                for (size_t workingIndex = 0u;
                     workingIndex < workingPixelCount;
                     ++workingIndex)
                {
                    const float* direction = &workingDirections[workingIndex * 3u];
                    const double nDotL = (std::max)(
                        normal[0] * direction[0] +
                        normal[1] * direction[1] +
                        normal[2] * direction[2], 0.0);
                    const double weight = nDotL * workingSolidAngles[workingIndex];
                    const size_t workingOffset = workingIndex * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        sum[channel] +=
                            static_cast<double>(workingData[workingOffset + channel]) * weight;
                    }
                }
                const size_t offset =
                    (static_cast<size_t>(y) * diffuseWidth + x) * 4u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double value = sum[channel];
                    if (!std::isfinite(value) || value < 0.0 || value >= 65504.0)
                    {
                        return false;
                    }
                    diffuseHalf[offset + channel] =
                        FloatToHalfRne(static_cast<float>(value));
                }
                diffuseHalf[offset + 3u] = FloatToHalfRne(1.0f);
            }
        }
        const uint32_t diffuseRowPitch =
            diffuseWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
        if (!validateGeneratedRgb(diffuseHalf,
                                  static_cast<size_t>(diffuseWidth) * diffuseHeight,
                                  diffuseExpectedRgb,
                                  bConstantSource,
                                  "diffuse"))
        {
            return false;
        }
        diffuseTexture->Update(diffuseHalf.data(), diffuseRowPitch,
                                diffuseRowPitch * diffuseHeight);

        auto sampleWorking = [&](float u, float v, float outColor[3])
        {
            const float wrappedU = u - std::floor(u);
            const float clampedV = (std::max)(0.0f, (std::min)(1.0f, v));
            const float xPosition = wrappedU * static_cast<float>(workingWidth) - 0.5f;
            const float yPosition = clampedV * static_cast<float>(workingHeight) - 0.5f;
            const int32_t x0 = static_cast<int32_t>(std::floor(xPosition));
            const int32_t y0 = static_cast<int32_t>(std::floor(yPosition));
            const int32_t x1 = x0 + 1;
            const int32_t y1 = y0 + 1;
            const float wx = xPosition - std::floor(xPosition);
            const float wy = yPosition - std::floor(yPosition);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                auto readWorking = [&](int32_t sampleX, int32_t sampleY) -> float
                {
                    const int32_t wrappedX = ((sampleX % static_cast<int32_t>(workingWidth)) +
                                              static_cast<int32_t>(workingWidth)) %
                                             static_cast<int32_t>(workingWidth);
                    const uint32_t clampedY = static_cast<uint32_t>((std::max)(
                        0, (std::min)(sampleY, static_cast<int32_t>(workingHeight - 1u))));
                    return workingData[(static_cast<size_t>(clampedY) * workingWidth +
                                        static_cast<uint32_t>(wrappedX)) * 4u + channel];
                };
                const float c00 = readWorking(x0, y0);
                const float c10 = readWorking(x1, y0);
                const float c01 = readWorking(x0, y1);
                const float c11 = readWorking(x1, y1);
                const float top = c00 + (c10 - c00) * wx;
                const float bottom = c01 + (c11 - c01) * wx;
                outColor[channel] = top + (bottom - top) * wy;
            }
        };

        Container::VariableArray<uint16_t> mip0Half(
            static_cast<size_t>(workingWidth) * workingHeight * 4u, 0u);
        for (size_t index = 0u; index < mip0Half.size(); ++index)
        {
            mip0Half[index] = workingHalf[index];
        }
        const uint32_t mip0RowPitch = workingWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
        if (!validateGeneratedRgb(mip0Half,
                                  workingPixelCount,
                                  sourceExpectedRgb,
                                  bConstantSource,
                                  "prefilter_mip0"))
        {
            return false;
        }
        prefilterTexture->Update(mip0Half.data(), mip0RowPitch,
                                 mip0RowPitch * workingHeight, 0u);

        for (uint32_t mip = 1u; mip < prefilterMipCount; ++mip)
        {
            const uint32_t mipWidth = (std::max)(1u, workingWidth >> mip);
            const uint32_t mipHeight = (std::max)(1u, workingHeight >> mip);
            Container::VariableArray<uint16_t> prefilteredHalf(
                static_cast<size_t>(mipWidth) * mipHeight * 4u, 0u);
            const float roughness = static_cast<float>(mip) / 8.0f;
            const float alpha = roughness * roughness;
            const float alphaSquared = alpha * alpha;
            for (uint32_t y = 0u; y < mipHeight; ++y)
            {
                const float v = (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(mipHeight);
                for (uint32_t x = 0u; x < mipWidth; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) /
                                    static_cast<float>(mipWidth);
                    const float theta = 3.14159265358979323846f * v;
                    const float phi = 2.0f * 3.14159265358979323846f * (u - 0.5f);
                    const float direction[3] = {
                        std::sin(theta) * std::cos(phi), std::cos(theta),
                        std::sin(theta) * std::sin(phi)};
                    float up[3] = {0.0f, 1.0f, 0.0f};
                    if (std::abs(direction[1]) >= 0.999f)
                    {
                        up[0] = 1.0f;
                        up[1] = 0.0f;
                        up[2] = 0.0f;
                    }
                    float tangent[3] = {
                        up[1] * direction[2] - up[2] * direction[1],
                        up[2] * direction[0] - up[0] * direction[2],
                        up[0] * direction[1] - up[1] * direction[0]};
                    const float tangentLength = std::sqrt(
                        tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                        tangent[2] * tangent[2]);
                    if (!std::isfinite(tangentLength) || tangentLength <= 0.0f)
                    {
                        return false;
                    }
                    tangent[0] /= tangentLength;
                    tangent[1] /= tangentLength;
                    tangent[2] /= tangentLength;
                    const float bitangent[3] = {
                        direction[1] * tangent[2] - direction[2] * tangent[1],
                        direction[2] * tangent[0] - direction[0] * tangent[2],
                        direction[0] * tangent[1] - direction[1] * tangent[0]};

                    double result[3] = {};
                    double weight = 0.0;
                    for (uint32_t sample = 0u; sample < prefilterSamples; ++sample)
                    {
                        const float xi1 = static_cast<float>(sample) /
                                          static_cast<float>(prefilterSamples);
                        const float xi2 = static_cast<float>(ReverseBits32(sample)) *
                                          2.3283064365386963e-10f;
                        const float samplePhi = 2.0f * 3.14159265358979323846f * xi1;
                        const float cosTheta = std::sqrt((std::max)(0.0f,
                            (1.0f - xi2) / (1.0f + (alphaSquared - 1.0f) * xi2)));
                        const float sinTheta = std::sqrt((std::max)(
                            0.0f, 1.0f - cosTheta * cosTheta));
                        const float half[3] = {
                            sinTheta * std::cos(samplePhi),
                            sinTheta * std::sin(samplePhi), cosTheta};
                        const float worldHalf[3] = {
                            tangent[0] * half[0] + bitangent[0] * half[1] + direction[0] * half[2],
                            tangent[1] * half[0] + bitangent[1] * half[1] + direction[1] * half[2],
                            tangent[2] * half[0] + bitangent[2] * half[1] + direction[2] * half[2]};
                        const float viewDotHalf = direction[0] * worldHalf[0] +
                                                  direction[1] * worldHalf[1] +
                                                  direction[2] * worldHalf[2];
                        const float light[3] = {
                            2.0f * viewDotHalf * worldHalf[0] - direction[0],
                            2.0f * viewDotHalf * worldHalf[1] - direction[1],
                            2.0f * viewDotHalf * worldHalf[2] - direction[2]};
                        const float nDotL = light[0] * direction[0] +
                                            light[1] * direction[1] +
                                            light[2] * direction[2];
                        if (!(nDotL > 0.0f) || !std::isfinite(nDotL))
                        {
                            continue;
                        }
                        const float lightPhi = std::atan2(light[2], light[0]);
                        const float lightV = std::asin((std::max)(-1.0f,
                            (std::min)(1.0f, -light[1])));
                        float radiance[3] = {};
                        sampleWorking(lightPhi * 0.15915494309189535f + 0.5f,
                                      lightV * 0.3183098861837907f + 0.5f,
                                      radiance);
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            result[channel] += static_cast<double>(radiance[channel]) *
                                               static_cast<double>(nDotL);
                        }
                        weight += static_cast<double>(nDotL);
                    }
                    if (!std::isfinite(weight) || weight <= 1.0e-8)
                    {
                        return false;
                    }
                    const size_t offset =
                        (static_cast<size_t>(y) * mipWidth + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double value = result[channel] / weight;
                        if (!std::isfinite(value) || value < 0.0 || value >= 65504.0)
                        {
                            return false;
                        }
                        prefilteredHalf[offset + channel] =
                            FloatToHalfRne(static_cast<float>(value));
                    }
                    prefilteredHalf[offset + 3u] = FloatToHalfRne(1.0f);
                }
            }
            const uint32_t rowPitch =
                mipWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
            if (!validateGeneratedRgb(prefilteredHalf,
                                      static_cast<size_t>(mipWidth) * mipHeight,
                                      sourceExpectedRgb,
                                      bConstantSource,
                                      "prefilter_mip"))
            {
                return false;
            }
            prefilterTexture->Update(prefilteredHalf.data(), rowPitch,
                                     rowPitch * mipHeight, mip);
        }

        outEnvironment = environmentTexture;
        outDiffuse = diffuseTexture;
        outPrefilter = prefilterTexture;
        return true;
    }

    static bool AreSkyAtmosphereParametersEqual(const SkyAtmosphereParameters& lhs,
                                                const SkyAtmosphereParameters& rhs)
    {
        return lhs.bEnabled == rhs.bEnabled &&
               lhs.SunAltitudeDegrees == rhs.SunAltitudeDegrees &&
               lhs.SunAzimuthDegrees == rhs.SunAzimuthDegrees &&
               lhs.SunLuminanceNits == rhs.SunLuminanceNits &&
               lhs.PlanetRadiusMeters == rhs.PlanetRadiusMeters &&
               lhs.AtmosphereHeightMeters == rhs.AtmosphereHeightMeters &&
               lhs.RayleighScaleHeightMeters == rhs.RayleighScaleHeightMeters &&
               lhs.MieScaleHeightMeters == rhs.MieScaleHeightMeters &&
               lhs.MieAnisotropy == rhs.MieAnisotropy &&
               lhs.GroundAlbedo.x == rhs.GroundAlbedo.x &&
               lhs.GroundAlbedo.y == rhs.GroundAlbedo.y &&
               lhs.GroundAlbedo.z == rhs.GroundAlbedo.z;
    }

    static float ClampSkyRadianceForFp16(float value)
    {
        constexpr float kFp16SafeMax = 65504.0f * 0.9f;
        return std::isfinite(value) ? std::clamp(value, 0.0f, kFp16SafeMax) : 0.0f;
    }

    static Math::Vector3 SkyDirectionFromEquirectangular(uint32_t x,
                                                         uint32_t y,
                                                         uint32_t width,
                                                         uint32_t height)
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        const float longitude = (u - 0.5f) * 2.0f * kPi;
        const float latitude = (v - 0.5f) * kPi;
        const float horizontal = std::cos(latitude);
        return Math::Vector3(horizontal * std::cos(longitude),
                             -std::sin(latitude),
                             horizontal * std::sin(longitude));
    }

    static bool BuildSkyAtmosphereRadianceSource(
        const SkyAtmosphereParameters& parameters,
        uint32_t width,
        uint32_t height,
        Container::VariableArray<float>& outSource)
    {
        outSource.clear();
        if (width == 0u || height == 0u ||
            static_cast<size_t>(width) > std::numeric_limits<size_t>::max() /
                                      static_cast<size_t>(height))
        {
            return false;
        }

        const size_t pixelCount = static_cast<size_t>(width) * height;
        if (pixelCount > std::numeric_limits<size_t>::max() / 4u)
        {
            return false;
        }

        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!sanitized.bEnabled)
        {
            return false;
        }

        outSource.resize(pixelCount * 4u, 0.0f);
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const SkyRadianceSample sample = EvaluateHillaireSkyReference(
                    sanitized, SkyDirectionFromEquirectangular(x, y, width, height));
                if (!sample.bValid || !std::isfinite(sample.Radiance.x) ||
                    !std::isfinite(sample.Radiance.y) ||
                    !std::isfinite(sample.Radiance.z))
                {
                    outSource.clear();
                    return false;
                }

                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                outSource[offset + 0u] = ClampSkyRadianceForFp16(sample.Radiance.x);
                outSource[offset + 1u] = ClampSkyRadianceForFp16(sample.Radiance.y);
                outSource[offset + 2u] = ClampSkyRadianceForFp16(sample.Radiance.z);
                outSource[offset + 3u] = 1.0f;
            }
        }
        return true;
    }

    static constexpr uint32_t LIGHTING_PARAMS_SIZE = sizeof(GPULightingParams);
    static constexpr uint32_t DDGI_ATLAS_TEXEL_COUNT = 8u;
    static constexpr uint32_t DDGI_ATLAS_MINIMUM_ARRAY_LAYER_COUNT = 2u;
    static constexpr uint32_t RTGI_COMPUTE_WORKGROUP_SIZE = 8u;
    static constexpr float RTGI_RAY_MINIMUM_DISTANCE = 0.001f;
    static constexpr float RTGI_RAY_MAXIMUM_DISTANCE = 10000.0f;
    static constexpr uint32_t RTGI_MAX_INSTANCE_CUSTOM_INDEX = 0x00FFFFFFu;

    struct RTGIComputeParameters
    {
        float invViewProjection[16] = {};
        float cameraPosition[4] = {};
        uint32_t imageAndSceneCounts[4] = {};
        float rayLimits[4] = {};
        uint32_t temporalState[4] = {};
    };

    struct RTGIInstanceData
    {
        uint64_t VertexAddress = 0u;
        uint64_t IndexAddress = 0u;
        float BaseColor[4] = {};
        float EmissiveChromaticityAndLuminance[4] = {};
        uint32_t VertexStride = 0u;
        uint32_t VertexCount = 0u;
        uint32_t IndexCount = 0u;
        uint32_t CustomIndex = UINT32_MAX;
        float Transform[12] = {};
    };

    static_assert(sizeof(RTGIComputeParameters) == 128u);
    static_assert(sizeof(RTGIInstanceData) == 112u);

    static bool IsFiniteNonNegativeRTGI(float value)
    {
        return std::isfinite(value) && value >= 0.0f;
    }

    static bool TryBuildRTGIInstanceData(
        const RayTracingSceneSnapshot& scene,
        Container::VariableArray<RTGIInstanceData>& outInstances,
        Container::VariableArray<RHI::BufferPtr>& outGeometryBuffers)
    {
        outInstances.clear();
        outGeometryBuffers.clear();
        if (scene.Instances.empty() ||
            scene.Instances.size() > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        for (const RayTracingSceneInstanceSnapshot& snapshot : scene.Instances)
        {
            if (!snapshot.AccelerationStructureVertexBuffer ||
                !snapshot.AccelerationStructureIndexBuffer ||
                snapshot.VertexStride < sizeof(float) * 3u ||
                snapshot.VertexCount < 3u || snapshot.IndexCount < 3u ||
                snapshot.IndexCount % 3u != 0u ||
                snapshot.Instance.customIndex > RTGI_MAX_INSTANCE_CUSTOM_INDEX)
            {
                return false;
            }

            const uint64_t vertexOffsetBytes =
                static_cast<uint64_t>(snapshot.VertexOffset) * snapshot.VertexStride;
            const uint64_t vertexRangeBytes =
                static_cast<uint64_t>(snapshot.VertexCount) * snapshot.VertexStride;
            const uint64_t indexOffsetBytes =
                static_cast<uint64_t>(snapshot.IndexOffset) * sizeof(uint32_t);
            const uint64_t indexRangeBytes =
                static_cast<uint64_t>(snapshot.IndexCount) * sizeof(uint32_t);
            const RHI::BufferPtr& vertexBuffer = snapshot.AccelerationStructureVertexBuffer;
            const RHI::BufferPtr& indexBuffer = snapshot.AccelerationStructureIndexBuffer;
            if (vertexRangeBytes > std::numeric_limits<uint32_t>::max() ||
                indexRangeBytes > std::numeric_limits<uint32_t>::max() ||
                vertexOffsetBytes > vertexBuffer->GetSize() ||
                vertexRangeBytes > vertexBuffer->GetSize() - vertexOffsetBytes ||
                indexOffsetBytes > indexBuffer->GetSize() ||
                indexRangeBytes > indexBuffer->GetSize() - indexOffsetBytes ||
                vertexBuffer->GetDeviceAddress() == 0u ||
                indexBuffer->GetDeviceAddress() == 0u ||
                vertexBuffer->GetDeviceAddress() >
                    std::numeric_limits<uint64_t>::max() - vertexOffsetBytes ||
                indexBuffer->GetDeviceAddress() >
                    std::numeric_limits<uint64_t>::max() - indexOffsetBytes)
            {
                return false;
            }

            RTGIInstanceData instanceData;
            instanceData.VertexAddress = vertexBuffer->GetDeviceAddress() + vertexOffsetBytes;
            instanceData.IndexAddress = indexBuffer->GetDeviceAddress() + indexOffsetBytes;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                if (!IsFiniteNonNegativeRTGI(snapshot.Material.BaseColor[channel]))
                {
                    return false;
                }
                instanceData.BaseColor[channel] = snapshot.Material.BaseColor[channel];
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                if (!IsFiniteNonNegativeRTGI(snapshot.Material.EmissiveColor[channel]))
                {
                    return false;
                }
                instanceData.EmissiveChromaticityAndLuminance[channel] =
                    snapshot.Material.EmissiveColor[channel];
            }
            if (!IsFiniteNonNegativeRTGI(snapshot.Material.EmissiveLuminanceNits))
            {
                return false;
            }
            instanceData.EmissiveChromaticityAndLuminance[3] =
                snapshot.Material.EmissiveLuminanceNits;
            instanceData.VertexStride = snapshot.VertexStride;
            instanceData.VertexCount = snapshot.VertexCount;
            instanceData.IndexCount = snapshot.IndexCount;
            instanceData.CustomIndex = snapshot.Instance.customIndex;
            for (uint32_t transformIndex = 0u; transformIndex < 12u; ++transformIndex)
            {
                if (!std::isfinite(snapshot.Instance.transform[transformIndex]))
                {
                    return false;
                }
                instanceData.Transform[transformIndex] =
                    snapshot.Instance.transform[transformIndex];
            }
            outInstances.push_back(instanceData);
            outGeometryBuffers.push_back(vertexBuffer);
            outGeometryBuffers.push_back(indexBuffer);
        }

        std::sort(outInstances.begin(), outInstances.end(),
                  [](const RTGIInstanceData& lhs, const RTGIInstanceData& rhs)
                  {
                      return lhs.CustomIndex < rhs.CustomIndex;
                  });
        for (size_t index = 1u; index < outInstances.size(); ++index)
        {
            if (outInstances[index - 1u].CustomIndex == outInstances[index].CustomIndex)
            {
                return false;
            }
        }
        return true;
    }

    static RHI::DescriptorSetDesc CreateRTGIComputeDescriptorSetDesc()
    {
        RHI::DescriptorSetDesc descriptorSetDesc;
        const RHI::ResourceBindType types[] = {
            RHI::ResourceBindType::AccelerationStructure,
            RHI::ResourceBindType::ConstantBuffer,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::StructuredBuffer,
            RHI::ResourceBindType::StructuredBuffer,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::RWTexture,
            RHI::ResourceBindType::RWTexture};
        for (uint32_t bindingIndex = 0u; bindingIndex < 23u; ++bindingIndex)
        {
            RHI::DescriptorBinding binding;
            binding.binding = bindingIndex;
            binding.type = types[bindingIndex];
            binding.stages = RHI::ShaderStage::Compute;
            descriptorSetDesc.bindings.push_back(binding);
        }
        return descriptorSetDesc;
    }

    static void InitializeSafeCascadedShadowParams(GPULightingParams& params)
    {
        for (uint32_t cascadeIndex = 0u;
             cascadeIndex < PhysicalLightingShadowCascadeCount;
             ++cascadeIndex)
        {
            for (uint32_t matrixIndex = 0u; matrixIndex < 16u; ++matrixIndex)
            {
                params.lightView[cascadeIndex][matrixIndex] = 0.0f;
                params.lightProjection[cascadeIndex][matrixIndex] = 0.0f;
            }
            params.lightView[cascadeIndex][0] = 1.0f;
            params.lightView[cascadeIndex][5] = 1.0f;
            params.lightView[cascadeIndex][10] = 1.0f;
            params.lightView[cascadeIndex][15] = 1.0f;
            params.lightProjection[cascadeIndex][0] = 1.0f;
            params.lightProjection[cascadeIndex][5] = 1.0f;
            params.lightProjection[cascadeIndex][10] = 1.0f;
            params.lightProjection[cascadeIndex][15] = 1.0f;
        }

        params.shadowSplitDistances[0] = 0.0f;
        params.shadowSplitDistances[1] = 1.0f;
        params.shadowSplitDistances[2] = 2.0f;
        params.shadowSplitDistances[3] = 3.0f;
        params.shadowSplitDistances[4] = 4.0f;
        params.shadowSplitDistances[5] = 0.0f;
        params.shadowSplitDistances[6] = 0.0f;
        params.shadowSplitDistances[7] = 0.0f;
        params.cascadeCount = 0u;
        params.bShadowEnabled = 0u;
    }

    static bool HasValidCascadedShadowPublication(const ViewRenderContext& context,
                                                   bool bShadowResourceAvailable)
    {
        const PhysicalLightingResources& lighting = context.PhysicalLighting;
        const CascadedDirectionalShadowShaderValues& cascaded = lighting.CascadedShadow;
        if (!bShadowResourceAvailable || !lighting.bShadowPublished ||
            !lighting.ShadowMapTexture || !lighting.ShadowSampler ||
            lighting.ShadowMapTexture->GetArraySize() != PhysicalLightingShadowCascadeCount ||
            !cascaded.bEnabled ||
            cascaded.CascadeCount != PhysicalLightingShadowCascadeCount)
        {
            return false;
        }

        for (uint32_t cascadeIndex = 0u;
             cascadeIndex < PhysicalLightingShadowCascadeCount;
             ++cascadeIndex)
        {
            for (uint32_t matrixIndex = 0u; matrixIndex < 16u; ++matrixIndex)
            {
                if (!std::isfinite(cascaded.View[cascadeIndex][matrixIndex]) ||
                    !std::isfinite(cascaded.Projection[cascadeIndex][matrixIndex]))
                {
                    return false;
                }
            }
        }

        for (uint32_t splitIndex = 0u;
             splitIndex < PhysicalLightingShadowSplitCount;
             ++splitIndex)
        {
            if (!std::isfinite(cascaded.SplitDistances[splitIndex]) ||
                (splitIndex > 0u &&
                 cascaded.SplitDistances[splitIndex] <=
                     cascaded.SplitDistances[splitIndex - 1u]))
            {
                return false;
            }
        }
        return true;
    }

    static bool AreDDGIAtlasResourcesComplete(const RHI::TexturePtr& irradianceAtlas,
                                              const RHI::TexturePtr& distanceAtlas,
                                              uint32_t probeCount)
    {
        if (!irradianceAtlas || !distanceAtlas || probeCount == 0u)
        {
            return false;
        }

        const uint32_t requiredArraySize =
            std::max(probeCount, DDGI_ATLAS_MINIMUM_ARRAY_LAYER_COUNT);
        return irradianceAtlas->GetWidth() == DDGI_ATLAS_TEXEL_COUNT &&
               irradianceAtlas->GetHeight() == DDGI_ATLAS_TEXEL_COUNT &&
               irradianceAtlas->GetArraySize() >= requiredArraySize &&
               irradianceAtlas->GetFormat() == RHI::Format::R16G16B16A16_FLOAT &&
               (irradianceAtlas->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                   RHI::ResourceUsage::None &&
               distanceAtlas->GetWidth() == DDGI_ATLAS_TEXEL_COUNT &&
               distanceAtlas->GetHeight() == DDGI_ATLAS_TEXEL_COUNT &&
               distanceAtlas->GetArraySize() >= requiredArraySize &&
               distanceAtlas->GetFormat() == RHI::Format::R16G16_FLOAT &&
               (distanceAtlas->GetUsage() & RHI::ResourceUsage::ShaderRead) !=
                   RHI::ResourceUsage::None;
    }

    static bool SupportsDDGILighting(const ViewRenderContext& context)
    {
        if (context.Device == nullptr)
        {
            return false;
        }

        const RHI::DeviceCapabilities& capabilities = context.Capabilities != nullptr
            ? *context.Capabilities
            : context.Device->GetCapabilities();
        return capabilities.RayTracing.bAccelerationStructure &&
               capabilities.RayTracing.bRayQuery &&
               capabilities.bBufferDeviceAddress &&
               capabilities.bShaderInt64;
    }

    static bool IsCompleteDDGILightingPublication(
        const ViewRenderContext& context,
        const DDGIVolumeParameters& volume,
        const RHI::TexturePtr& irradianceAtlas,
        const RHI::TexturePtr& distanceAtlas,
        uint32_t atlasProbeCount,
        const RHI::SamplerPtr& atlasSampler)
    {
        const uint32_t expectedProbeCount = GetDDGIProbeCount(volume);
        const PhysicalLightingResources& lighting = context.PhysicalLighting;
        return IsDDGIVolumeValid(volume) && expectedProbeCount > 0u &&
               expectedProbeCount <= DDGIMaxProbeCount &&
               atlasProbeCount == expectedProbeCount &&
               AreDDGIAtlasResourcesComplete(irradianceAtlas,
                                             distanceAtlas,
                                             atlasProbeCount) &&
               atlasSampler && SupportsDDGILighting(context) &&
               lighting.bActive && lighting.bLightingPublished &&
               lighting.FrameNumber == context.FrameNumber;
    }

    static RHI::DescriptorSetDesc CreateLightingDescriptorSetDesc(bool bNeuralBRDFAvailable)
    {
        (void)bNeuralBRDFAvailable;
        RHI::DescriptorSetDesc dsDesc;

        RHI::DescriptorBinding albedoBinding;
        albedoBinding.binding = 0;
        albedoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        albedoBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(albedoBinding);

        RHI::DescriptorBinding normalBinding;
        normalBinding.binding = 1;
        normalBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        normalBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(normalBinding);

        RHI::DescriptorBinding materialBinding;
        materialBinding.binding = 2;
        materialBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        materialBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(materialBinding);

        RHI::DescriptorBinding depthBinding;
        depthBinding.binding = 3;
        depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        depthBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(depthBinding);

        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 4;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(paramsBinding);

        RHI::DescriptorBinding lightBinding;
        lightBinding.binding = 5;
        lightBinding.type = RHI::ResourceBindType::StructuredBuffer;
        lightBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(lightBinding);

        RHI::DescriptorBinding shadowBinding;
        shadowBinding.binding = 6;
        shadowBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        shadowBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(shadowBinding);

        RHI::DescriptorBinding emissiveBinding;
        emissiveBinding.binding = 7;
        emissiveBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        emissiveBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(emissiveBinding);

        RHI::DescriptorBinding envMapBinding;
        envMapBinding.binding = 8;
        envMapBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        envMapBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(envMapBinding);

        RHI::DescriptorBinding brdfLutBinding;
        brdfLutBinding.binding = 9;
        brdfLutBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        brdfLutBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(brdfLutBinding);

        RHI::DescriptorBinding ssaoBinding;
        ssaoBinding.binding = 10;
        ssaoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        ssaoBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(ssaoBinding);

        RHI::DescriptorBinding neuralWeightBinding;
        neuralWeightBinding.binding = 11;
        neuralWeightBinding.type = RHI::ResourceBindType::StructuredBuffer;
        neuralWeightBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(neuralWeightBinding);

        RHI::DescriptorBinding diffuseIrradianceBinding;
        diffuseIrradianceBinding.binding = 12;
        diffuseIrradianceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        diffuseIrradianceBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(diffuseIrradianceBinding);

        RHI::DescriptorBinding prefilteredSpecularBinding;
        prefilteredSpecularBinding.binding = 13;
        prefilteredSpecularBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        prefilteredSpecularBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(prefilteredSpecularBinding);

        RHI::DescriptorBinding skySunDiskBinding;
        skySunDiskBinding.binding = 14;
        skySunDiskBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        skySunDiskBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(skySunDiskBinding);

        RHI::DescriptorBinding skyTransmittanceBinding;
        skyTransmittanceBinding.binding = 15;
        skyTransmittanceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        skyTransmittanceBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(skyTransmittanceBinding);

        RHI::DescriptorBinding rayTracingShadowBinding;
        rayTracingShadowBinding.binding = 16;
        rayTracingShadowBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        rayTracingShadowBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(rayTracingShadowBinding);

        RHI::DescriptorBinding ddgiIrradianceBinding;
        ddgiIrradianceBinding.binding = 17;
        ddgiIrradianceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        ddgiIrradianceBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(ddgiIrradianceBinding);

        RHI::DescriptorBinding ddgiDistanceBinding;
        ddgiDistanceBinding.binding = 18;
        ddgiDistanceBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        ddgiDistanceBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(ddgiDistanceBinding);

        RHI::DescriptorBinding rtgiDiffuseIndirectBinding;
        rtgiDiffuseIndirectBinding.binding = 19;
        rtgiDiffuseIndirectBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        rtgiDiffuseIndirectBinding.stages = RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(rtgiDiffuseIndirectBinding);

        return dsDesc;
    }

    LightingPass::LightingPass(const LightingPassSettings& settings)
        : m_Settings(settings)
    {
    }

    LightingPass::~LightingPass()
    {
        Shutdown();
    }

    namespace
    {
        struct LightingPassInitializationRollback
        {
            LightingPass& pass;
            SceneView* sceneView;
            const GBufferPass* gbufferPass;
            const SSAOPass* ssaoPass;
            bool bCommitted = false;

            ~LightingPassInitializationRollback()
            {
                if (bCommitted)
                {
                    return;
                }

                pass.Shutdown();
                pass.SetSceneView(sceneView);
                pass.SetGBufferPass(gbufferPass);
                pass.SetSSAOPass(ssaoPass);
            }

            void Commit()
            {
                bCommitted = true;
            }
        };
    }

    bool LightingPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (m_Device != nullptr || m_DefaultBlackTexture || m_DefaultShadowMapArrayTexture ||
            m_BrdfLutTexture ||
            m_DefaultNeuralBRDFWeightBuffer)
        {
            SceneView* sceneView = m_SceneView;
            const GBufferPass* gbufferPass = m_GBufferPass;
            const SSAOPass* ssaoPass = m_SSAOPass;
            Shutdown();
            m_SceneView = sceneView;
            m_GBufferPass = gbufferPass;
            m_SSAOPass = ssaoPass;
        }

        if (!std::isfinite(m_Settings.EnvironmentLuminanceScaleNits) ||
            m_Settings.EnvironmentLuminanceScaleNits < 0.0f)
        {
            NORVES_LOG_ERROR("LightingPass",
                             "EnvironmentLuminanceScaleNits must be finite and non-negative");
            return false;
        }

        if (!ValidateProductionInitializationInvariants(
                m_Settings.EnvironmentLuminanceScaleNits))
        {
            NORVES_LOG_ERROR("LightingPass", "Production binary16/source invariant self-test failed");
            return false;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("LightingPass", "Device is null");
            return false;
        }

        const uint32_t activeDebugMode = static_cast<uint32_t>(context.GetActiveDebugMode());
        const bool bValidationSnapshotMode =
            activeDebugMode == 250u || activeDebugMode == 251u || activeDebugMode == 252u;

        LightingPassInitializationRollback initializationRollback
        {
            *this, m_SceneView, m_GBufferPass, m_SSAOPass
        };
        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("LightingPass", "ShaderManager is null");
            return false;
        }

        m_LightingVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_LightingVertexShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // ライティングフラグメントシェーダー作成
        // ========================================
        m_LightingFragmentShader = context.ShaderMgr->LoadShader("lighting.frag", RHI::ShaderStage::Pixel);
        if (!m_LightingFragmentShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting fragment shader");
            return false;
        }

        // ========================================
        // GBufferサンプラー作成
        // ========================================
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_GBufferSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_GBufferSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create GBuffer sampler");
            return false;
        }

        // ========================================
        // ライティングパラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            LIGHTING_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "LightingParamsUBO");
        m_LightDataBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_LightDataBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting params buffer");
            return false;
        }

        // ========================================
        // ライト配列SSBOバッファ作成（binding=5用）
        // ========================================
        if (!EnsureLightArrayBufferCapacity(1))
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create light array buffer");
            return false;
        }

        // D0/D1共通の有効なfallbackを先に確保する。未bind descriptorは許可しない。
        RHI::TextureDesc blackTextureDesc;
        blackTextureDesc.Width = 1u;
        blackTextureDesc.Height = 1u;
        blackTextureDesc.MipLevels = 1u;
        blackTextureDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        blackTextureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        blackTextureDesc.DebugName = "LightingBlackEnvironmentFallback";
        m_DefaultBlackTexture = m_Device->CreateTexture(blackTextureDesc);
        if (!m_DefaultBlackTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create black environment fallback");
            return false;
        }
        const uint16_t blackPixel[4] = {0x0000u, 0x0000u, 0x0000u, 0x0000u};
        m_DefaultBlackTexture->Update(blackPixel, sizeof(blackPixel), sizeof(blackPixel));

        RHI::TextureDesc ddgiIrradianceFallbackDesc;
        ddgiIrradianceFallbackDesc.Width = DDGI_ATLAS_TEXEL_COUNT;
        ddgiIrradianceFallbackDesc.Height = DDGI_ATLAS_TEXEL_COUNT;
        ddgiIrradianceFallbackDesc.ArraySize = DDGI_ATLAS_MINIMUM_ARRAY_LAYER_COUNT;
        ddgiIrradianceFallbackDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        ddgiIrradianceFallbackDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                           RHI::ResourceUsage::TransferDst;
        ddgiIrradianceFallbackDesc.DebugName = "LightingDDGIIrradianceArrayFallback";
        m_DefaultDDGIIrradianceAtlas = m_Device->CreateTexture(ddgiIrradianceFallbackDesc);
        if (!m_DefaultDDGIIrradianceAtlas)
        {
            NORVES_LOG_ERROR("LightingPass", "DDGI irradianceのfallback配列を作成できません");
            return false;
        }
        uint16_t ddgiIrradianceFallbackPixels[
            DDGI_ATLAS_TEXEL_COUNT * DDGI_ATLAS_TEXEL_COUNT * 4u] = {};
        const uint32_t ddgiIrradianceRowPitch =
            DDGI_ATLAS_TEXEL_COUNT * 4u * sizeof(uint16_t);
        const uint32_t ddgiIrradianceSlicePitch =
            ddgiIrradianceRowPitch * DDGI_ATLAS_TEXEL_COUNT;
        for (uint32_t layer = 0u;
             layer < DDGI_ATLAS_MINIMUM_ARRAY_LAYER_COUNT;
             ++layer)
        {
            m_DefaultDDGIIrradianceAtlas->Update(ddgiIrradianceFallbackPixels,
                                                 ddgiIrradianceRowPitch,
                                                 ddgiIrradianceSlicePitch,
                                                 0u,
                                                 layer);
        }

        m_DefaultDDGIDistanceAtlas = m_DefaultDDGIIrradianceAtlas;

        RHI::TextureDesc shadowMapFallbackDesc;
        shadowMapFallbackDesc.Width = 1u;
        shadowMapFallbackDesc.Height = 1u;
        shadowMapFallbackDesc.ArraySize = PhysicalLightingShadowCascadeCount;
        shadowMapFallbackDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
        shadowMapFallbackDesc.Usage = RHI::ResourceUsage::ShaderRead |
                                      RHI::ResourceUsage::TransferDst;
        shadowMapFallbackDesc.DebugName = "LightingShadowMapArrayFallback";
        m_DefaultShadowMapArrayTexture = m_Device->CreateTexture(shadowMapFallbackDesc);
        if (!m_DefaultShadowMapArrayTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create shadow map array fallback");
            return false;
        }
        const uint8_t shadowMapFallbackPixel[4] = {255u, 255u, 255u, 255u};
        for (uint32_t layer = 0u;
             layer < PhysicalLightingShadowCascadeCount;
             ++layer)
        {
            m_DefaultShadowMapArrayTexture->Update(shadowMapFallbackPixel,
                                                    sizeof(shadowMapFallbackPixel),
                                                    sizeof(shadowMapFallbackPixel),
                                                    0u,
                                                    layer);
        }

        RHI::TextureDesc dfgFallbackDesc;
        dfgFallbackDesc.Width = 1u;
        dfgFallbackDesc.Height = 1u;
        dfgFallbackDesc.MipLevels = 1u;
        dfgFallbackDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
        dfgFallbackDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        dfgFallbackDesc.DebugName = "LightingDfgFallback";
        m_BrdfLutTexture = m_Device->CreateTexture(dfgFallbackDesc);
        if (!m_BrdfLutTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create DFG fallback");
            return false;
        }
        const uint16_t dfgFallbackPixel[2] = {0x0000u, 0x0000u};
        m_BrdfLutTexture->Update(dfgFallbackPixel, sizeof(dfgFallbackPixel), sizeof(dfgFallbackPixel));

        RHI::BufferDesc neuralFallbackDesc(
            4u,
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead,
            true,
            "NeuralBRDF_ZeroFallback");
        m_DefaultNeuralBRDFWeightBuffer = m_Device->CreateBuffer(neuralFallbackDesc);
        if (!m_DefaultNeuralBRDFWeightBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create Neural BRDF fallback");
            return false;
        }
        const uint32_t neuralFallbackValue = 0u;
        m_DefaultNeuralBRDFWeightBuffer->Update(&neuralFallbackValue, sizeof(neuralFallbackValue));

        RHI::SamplerDesc sourceSamplerDesc;
        sourceSamplerDesc.filterMin = RHI::FilterMode::Linear;
        sourceSamplerDesc.filterMag = RHI::FilterMode::Linear;
        sourceSamplerDesc.filterMip = RHI::FilterMode::Point;
        sourceSamplerDesc.addressU = RHI::TextureAddressMode::Wrap;
        sourceSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        sourceSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_IBLSampler = m_Device->CreateSampler(sourceSamplerDesc);
        m_DiffuseIrradianceSampler = m_Device->CreateSampler(sourceSamplerDesc);
        if (!m_IBLSampler || !m_DiffuseIrradianceSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create source/diffuse fallback sampler");
            return false;
        }

        RHI::SamplerDesc prefilterSamplerDesc = sourceSamplerDesc;
        prefilterSamplerDesc.filterMip = RHI::FilterMode::Linear;
        m_PrefilteredSpecularSampler = m_Device->CreateSampler(prefilterSamplerDesc);
        if (!m_PrefilteredSpecularSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create prefiltered fallback sampler");
            return false;
        }

        RHI::SamplerDesc dfgSamplerDesc;
        dfgSamplerDesc.filterMin = RHI::FilterMode::Linear;
        dfgSamplerDesc.filterMag = RHI::FilterMode::Linear;
        dfgSamplerDesc.filterMip = RHI::FilterMode::Point;
        dfgSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        dfgSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        dfgSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_DfgSampler = m_Device->CreateSampler(dfgSamplerDesc);
        if (!m_DfgSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create DFG fallback sampler");
            return false;
        }

        RHI::SamplerDesc ddgiSamplerDesc;
        ddgiSamplerDesc.filterMin = RHI::FilterMode::Linear;
        ddgiSamplerDesc.filterMag = RHI::FilterMode::Linear;
        ddgiSamplerDesc.filterMip = RHI::FilterMode::Point;
        ddgiSamplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        ddgiSamplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        ddgiSamplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_DDGISampler = m_Device->CreateSampler(ddgiSamplerDesc);
        if (!m_DDGISampler)
        {
            NORVES_LOG_ERROR("LightingPass", "DDGI atlas用サンプラーを作成できません");
            return false;
        }

        // ========================================
        // Neural BRDF ウェイトデータ読み込み
        // ========================================
        if (!m_Settings.NeuralBRDFWeightPath.empty())
        {
            Container::String resolvedPath = m_Settings.NeuralBRDFWeightPath;
#ifdef NORVES_ASSET_DIR
            if (resolvedPath.size() > 0 && resolvedPath[0] != '/' && resolvedPath[0] != '\\' &&
                (resolvedPath.size() < 2 || resolvedPath[1] != ':'))
            {
                resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + resolvedPath;
            }
#endif

            if (m_NeuralBRDFData.LoadFromFile(resolvedPath))
            {
                const auto& weightData = m_NeuralBRDFData.GetWeightDataFP32();
                size_t bufferSize = m_NeuralBRDFData.GetWeightDataSizeFP32();

                RHI::BufferDesc weightBufDesc(
                    static_cast<uint64_t>(bufferSize),
                    RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst,
                    true, "NeuralBRDF_Weights");

                m_NeuralBRDFWeightBuffer = m_Device->CreateBuffer(weightBufDesc);
                if (m_NeuralBRDFWeightBuffer)
                {
                    m_NeuralBRDFWeightBuffer->Update(weightData.data(), bufferSize);
                    m_bNeuralBRDFAvailable = true;
                    NORVES_LOG_INFO("LightingPass", "Neural BRDF loaded: %zu parameters, %zu bytes",
                                    weightData.size(), bufferSize);
                }
                else
                {
                    NORVES_LOG_WARNING("LightingPass", "Failed to create Neural BRDF weight buffer");
                }
            }
            else
            {
                NORVES_LOG_WARNING("LightingPass", "Failed to load Neural BRDF weights, using analytical BRDF");
            }
        }

        if (!GenerateBRDFLut())
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to generate DFG LUT");
            return false;
        }

        if (!bValidationSnapshotMode && !m_Settings.EnvironmentMapPath.empty() &&
            LoadEnvironmentMap(m_Settings.EnvironmentMapPath))
        {
            m_bIBLAvailable = true;
        }

        if (bValidationSnapshotMode && !GenerateValidationSnapshots())
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to generate validation environment snapshots");
            return false;
        }

        // Populate the initial light buffer before the mandatory descriptor bindings are made.
        // Render-graph initialization therefore has a complete binding, while the first
        // per-frame update remains ordered before the descriptor update.
        if (!UpdateLightBuffer(context, false, false))
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to populate the initial light buffer");
            return false;
        }

        RHI::DescriptorSetPtr initialDescriptorSet;
        if (!CreateLightingDescriptorSet(initialDescriptorSet))
        {
            return false;
        }
        m_LightingDescriptorSet = std::move(initialDescriptorSet);

        initializationRollback.Commit();
        m_bInitialized = true;
        NORVES_LOG_INFO("LightingPass", "LightingPass initialized");
        return true;
    }

    void LightingPass::Shutdown()
    {
        m_DDGIProbePass.Shutdown();
        m_RayTracingShadowPass.Shutdown();
        if (!m_bInitialized && m_Device == nullptr && !m_DefaultBlackTexture &&
            !m_DefaultShadowMapArrayTexture &&
            !m_DefaultDDGIIrradianceAtlas && !m_DefaultDDGIDistanceAtlas &&
            !m_BrdfLutTexture && !m_DefaultNeuralBRDFWeightBuffer &&
            !m_RTGIComputePipeline && !m_RTGIComputeParametersBuffer &&
            !m_RTGIComputeInstanceDataBuffer)
        {
            return;
        }

        // Descriptor bindings own references to buffers, textures, and samplers.
        m_LightingDescriptorSet.reset();

        // Release dependents before the resources they reference.
        m_LightingPipeline.reset();
        m_LightingFramebuffer.reset();
        m_LightingRenderPass.reset();
        m_SceneColorTexture.reset();

        // RTGIのcompute資源はdescriptor、pipeline、shaderの順で解放する。
        m_RTGIComputeDescriptorSet.reset();
        m_RTGIComputePipeline.reset();
        m_RTGIComputeParametersBuffer.reset();
        m_RTGIComputeInstanceDataBuffer.reset();
        m_RTGIGeometryBuffers.clear();
        for (RTGIHistoryTextureSet& history : m_RTGIHistoryTextures)
        {
            history.Clear();
        }
        m_RTGIHistorySlotState[0] = RHI::ResourceState::Undefined;
        m_RTGIHistorySlotState[1] = RHI::ResourceState::Undefined;
        m_RTGIComputeInstanceDataCapacity = 0u;
        m_RTGIHistoryWidth = 0u;
        m_RTGIHistoryHeight = 0u;
        m_RTGIHistoryWriteIndex = 0u;
        m_RTGIHistoryAgeFrames = 0u;
        m_RTGIHistorySceneRevision = 0u;
        m_RTGIHistoryLightRevision = 0u;
        m_RTGIHistoryLightWeightLimitedFrames = 0u;
        m_bRTGIHistoryValid = false;
        m_bRTGIHistoryLightRevisionValid = false;
        m_bRTGIComputeUnavailable = false;

        m_LightArrayBuffer.reset();
        m_RetiredLightArrayBuffers.clear();
        m_LightArrayCapacity = 0;
        m_LightDataBuffer.reset();

        // IBL resources
        m_EnvironmentTexture.reset();
        m_DiffuseIrradianceTexture.reset();
        m_PrefilteredSpecularTexture.reset();
        m_SkyAtmosphereDiffuseIrradianceTexture.reset();
        m_SkyAtmospherePrefilteredSpecularTexture.reset();
        m_ValidationRaw250EnvironmentTexture.reset();
        m_ValidationRaw250DiffuseIrradianceTexture.reset();
        m_ValidationRaw250Texture.reset();
        m_ValidationRaw252EnvironmentTexture.reset();
        m_ValidationRaw252DiffuseIrradianceTexture.reset();
        m_ValidationRaw252PrefilteredSpecularTexture.reset();
        m_BrdfLutTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultShadowMapArrayTexture.reset();
        m_DefaultDDGIIrradianceAtlas.reset();
        m_DefaultDDGIDistanceAtlas.reset();
        m_bIBLAvailable = false;
        m_SkyAtmosphereIblParameters = SkyAtmosphereParameters{};
        m_SkyAtmosphereIblRadianceWidth = 0u;
        m_SkyAtmosphereIblRadianceHeight = 0u;
        m_bSkyAtmosphereIblCacheValid = false;
        m_bSkyAtmosphereIblAvailable = false;

        // Neural BRDF resources
        m_NeuralBRDFWeightBuffer.reset();
        m_DefaultNeuralBRDFWeightBuffer.reset();
        m_bNeuralBRDFAvailable = false;

        // Samplers are released after descriptor and texture ownership is gone.
        m_GBufferSampler.reset();
        m_IBLSampler.reset();
        m_DiffuseIrradianceSampler.reset();
        m_PrefilteredSpecularSampler.reset();
        m_DfgSampler.reset();
        m_DDGISampler.reset();

        // Shaders are released after the pipeline.
        m_RTGIComputeShader.reset();
        m_LightingFragmentShader.reset();
        m_LightingVertexShader.reset();

        m_Device = nullptr;
        m_SceneView = nullptr;
        m_GBufferPass = nullptr;
        m_SSAOPass = nullptr;
        m_SceneColorHandle = {};
        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferVelocityHandle = {};
        m_GBufferEmissiveHandle = {};
        m_SSAOBlurredHandle = {};
        m_ShadowMapHandle = {};
        m_RTGIDiffuseIndirectHandle = {};
        m_bLegacyInputFallbackActive = false;
        m_bUsingRenderGraphResources = false;
        m_bRenderPassUsesRenderGraphInitialState = false;
        m_FramebufferSceneColorTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;

        m_bInitialized = false;
        NORVES_LOG_INFO("LightingPass", "LightingPass shutdown");
    }

    void LightingPass::Setup(ViewRenderContext& context)
    {
        uint32_t width = context.GetActiveRenderWidth();
        uint32_t height = context.GetActiveRenderHeight();

        if (width == 0 || height == 0)
        {
            return;
        }

        m_bUsingRenderGraphResources = false;

        const bool bUseRenderGraphInitialState = m_bUsingRenderGraphResources;
        const bool bResourcesChanged =
            width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            !m_SceneColorTexture ||
            m_FramebufferSceneColorTexture != m_SceneColorTexture.get() ||
            m_bRenderPassUsesRenderGraphInitialState != bUseRenderGraphInitialState ||
            !m_LightingRenderPass ||
            !m_LightingFramebuffer ||
            !m_LightingPipeline ||
            !m_LightingDescriptorSet;

        if (bResourcesChanged)
        {
            RHI::TextureDesc sceneColorDesc =
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor");
            sceneColorDesc.Usage = sceneColorDesc.Usage | RHI::ResourceUsage::TransferSrc;
            RHI::TexturePtr sceneColorTexture = m_Device->CreateTexture(
                sceneColorDesc);
            if (!sceneColorTexture)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create SceneColor texture");
                return;
            }

            if (!PrepareLightingOutput(width,
                                       height,
                                       sceneColorTexture,
                                       bUseRenderGraphInitialState,
                                       context))
            {
                return;
            }

            NORVES_LOG_INFO("LightingPass", "Lighting resources resized (%ux%u)", width, height);
        }
    }

    void LightingPass::Declare(RenderGraphBuilder &builder)
    {
        m_bLegacyInputFallbackActive = false;

        const ViewRenderContext *context = builder.GetContext();

        uint32_t width = 0;
        uint32_t height = 0;
        if (context)
        {
            width = ResolveLightingWidth(*context);
            height = ResolveLightingHeight(*context);
        }

        if (width == 0)
        {
            width = m_CurrentWidth > 0 ? m_CurrentWidth : 1;
        }

        if (height == 0)
        {
            height = m_CurrentHeight > 0 ? m_CurrentHeight : 1;
        }

        m_GBufferAlbedoHandle = {};
        m_GBufferNormalHandle = {};
        m_GBufferMaterialHandle = {};
        m_GBufferDepthHandle = {};
        m_GBufferEmissiveHandle = {};
        m_SSAOBlurredHandle = {};
        m_ShadowMapHandle = {};
        m_RTGIDiffuseIndirectHandle = {};

        RGTextureHandle albedoHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferAlbedo,
                                   albedoHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferAlbedoHandle = albedoHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackAlbedoHandle = m_GBufferPass->GetAlbedoHandle();
            if (fallbackAlbedoHandle.IsValid())
            {
                builder.Read(fallbackAlbedoHandle, RHI::ResourceState::ShaderResource);
                m_GBufferAlbedoHandle = fallbackAlbedoHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle normalHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferNormal,
                                   normalHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferNormalHandle = normalHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackNormalHandle = m_GBufferPass->GetNormalHandle();
            if (fallbackNormalHandle.IsValid())
            {
                builder.Read(fallbackNormalHandle, RHI::ResourceState::ShaderResource);
                m_GBufferNormalHandle = fallbackNormalHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle materialHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferMaterial,
                                   materialHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferMaterialHandle = materialHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackMaterialHandle = m_GBufferPass->GetMaterialHandle();
            if (fallbackMaterialHandle.IsValid())
            {
                builder.Read(fallbackMaterialHandle, RHI::ResourceState::ShaderResource);
                m_GBufferMaterialHandle = fallbackMaterialHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle depthHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferDepth,
                                   depthHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferDepthHandle = depthHandle.ToResourceHandle();
            builder.PublishTexture(RenderGraphResourceNames::SceneDepth, depthHandle);
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackDepthHandle = m_GBufferPass->GetDepthHandle();
            if (fallbackDepthHandle.IsValid())
            {
                builder.Read(fallbackDepthHandle, RHI::ResourceState::ShaderResource);
                m_GBufferDepthHandle = fallbackDepthHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle velocityHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferVelocity,
                                   velocityHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferVelocityHandle = velocityHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackVelocityHandle = m_GBufferPass->GetVelocityHandle();
            if (fallbackVelocityHandle.IsValid())
            {
                builder.Read(fallbackVelocityHandle, RHI::ResourceState::ShaderResource);
                m_GBufferVelocityHandle = fallbackVelocityHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle emissiveHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::GBufferEmissive,
                                   emissiveHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_GBufferEmissiveHandle = emissiveHandle.ToResourceHandle();
        }
        else if (m_GBufferPass)
        {
            const RGResourceHandle fallbackEmissiveHandle = m_GBufferPass->GetEmissiveHandle();
            if (fallbackEmissiveHandle.IsValid())
            {
                builder.Read(fallbackEmissiveHandle, RHI::ResourceState::ShaderResource);
                m_GBufferEmissiveHandle = fallbackEmissiveHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle ssaoHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::SSAOBlurred,
                                   ssaoHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_SSAOBlurredHandle = ssaoHandle.ToResourceHandle();
        }
        else if (m_SSAOPass)
        {
            const RGResourceHandle fallbackSSAOHandle = m_SSAOPass->GetSSAOBlurredHandle();
            if (fallbackSSAOHandle.IsValid())
            {
                builder.Read(fallbackSSAOHandle, RHI::ResourceState::ShaderResource);
                m_SSAOBlurredHandle = fallbackSSAOHandle;
                m_bLegacyInputFallbackActive = true;
            }
        }

        RGTextureHandle shadowMapHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::ShadowMap,
                                   shadowMapHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_ShadowMapHandle = shadowMapHandle.ToResourceHandle();
        }

        // R6 RTGIはこのパス内のcomputeが生成し、後段のLightingへ渡す。
        RGTextureHandle rtgiDiffuseIndirectHandle;
        if (builder.TryReadTexture(RenderGraphResourceNames::RTGIDiffuseIndirect,
                                   rtgiDiffuseIndirectHandle,
                                   RHI::ResourceState::ShaderResource))
        {
            m_RTGIDiffuseIndirectHandle = rtgiDiffuseIndirectHandle.ToResourceHandle();
        }
        else
        {
            RGTextureDesc rtgiDesc;
            rtgiDesc.Width = width;
            rtgiDesc.Height = height;
            rtgiDesc.Format = RTGIDiffuseIndirectRadianceFormat;
            rtgiDesc.Usage = RHI::ResourceUsage::ShaderRead |
                             RHI::ResourceUsage::ShaderWrite;
            rtgiDesc.DebugName = "RTGI.DiffuseIndirect";
            const RGTextureHandle rtgiOutput = builder.WriteTexture(
                RenderGraphResourceNames::RTGIDiffuseIndirect,
                rtgiDesc,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (rtgiOutput.IsValid())
            {
                m_RTGIDiffuseIndirectHandle = rtgiOutput.ToResourceHandle();
            }
        }

        // SkyAtmospherePassの同一スナップショット由来リソースを依存として読む。
        // 実体はViewRenderContextへ公開されたテクスチャを使い、named resourceの
        // 読み取り宣言は空LUTがLighting前段にあることをRenderGraphへ伝える。
        RGTextureHandle skyTransmittanceHandle;
        RGTextureHandle skyRadianceHandle;
        RGTextureHandle skySunDiskHandle;
        builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereTransmittance,
                               skyTransmittanceHandle,
                               RHI::ResourceState::ShaderResource);
        builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereRadiance,
                               skyRadianceHandle,
                               RHI::ResourceState::ShaderResource);
        builder.TryReadTexture(RenderGraphResourceNames::SkyAtmosphereSunDisk,
                               skySunDiskHandle,
                               RHI::ResourceState::ShaderResource);

        RGTextureDesc sceneColorDesc =
            RGTextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor");
        sceneColorDesc.Usage = sceneColorDesc.Usage | RHI::ResourceUsage::TransferSrc;
        m_SceneColorHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::SceneColor,
            sceneColorDesc,
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);
        builder.ExportTexture(RenderGraphResourceNames::SceneColor, m_SceneColorHandle);

        builder.PreserveInsertionOrder();
    }

    void LightingPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr sceneColorTexture = resources.GetTexture(m_SceneColorHandle);
        if (!sceneColorTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to resolve native SceneColor texture");
            return;
        }

        RHI::TexturePtr albedoTexture;
        RHI::TexturePtr normalTexture;
        RHI::TexturePtr materialTexture;
        RHI::TexturePtr depthTexture;
        RHI::TexturePtr velocityTexture;
        RHI::TexturePtr emissiveTexture;
        bool bUsedSharedResourceFallback = false;
        if (m_GBufferAlbedoHandle.IsValid())
        {
            albedoTexture = resources.GetTexture(m_GBufferAlbedoHandle);
        }
        if (m_GBufferNormalHandle.IsValid())
        {
            normalTexture = resources.GetTexture(m_GBufferNormalHandle);
        }
        if (m_GBufferMaterialHandle.IsValid())
        {
            materialTexture = resources.GetTexture(m_GBufferMaterialHandle);
        }
        if (m_GBufferDepthHandle.IsValid())
        {
            depthTexture = resources.GetTexture(m_GBufferDepthHandle);
        }
        if (m_GBufferVelocityHandle.IsValid())
        {
            velocityTexture = resources.GetTexture(m_GBufferVelocityHandle);
        }
        if (m_GBufferEmissiveHandle.IsValid())
        {
            emissiveTexture = resources.GetTexture(m_GBufferEmissiveHandle);
        }

        RHI::TexturePtr ssaoTexture;
        if (m_SSAOBlurredHandle.IsValid())
        {
            ssaoTexture = resources.GetTexture(m_SSAOBlurredHandle);
        }

        RHI::TexturePtr rtgiDiffuseIndirectTexture;
        if (m_RTGIDiffuseIndirectHandle.IsValid())
        {
            rtgiDiffuseIndirectTexture = resources.GetTexture(m_RTGIDiffuseIndirectHandle);
        }

        if ((!albedoTexture || !normalTexture || !materialTexture || !depthTexture ||
             !velocityTexture || !emissiveTexture) &&
            m_GBufferPass)
        {
            albedoTexture = albedoTexture ? albedoTexture : resources.GetTexture(m_GBufferPass->GetAlbedoHandle());
            normalTexture = normalTexture ? normalTexture : resources.GetTexture(m_GBufferPass->GetNormalHandle());
            materialTexture = materialTexture ? materialTexture : resources.GetTexture(m_GBufferPass->GetMaterialHandle());
            depthTexture = depthTexture ? depthTexture : resources.GetTexture(m_GBufferPass->GetDepthHandle());
            velocityTexture = velocityTexture ? velocityTexture : resources.GetTexture(m_GBufferPass->GetVelocityHandle());
            emissiveTexture = emissiveTexture ? emissiveTexture : resources.GetTexture(m_GBufferPass->GetEmissiveHandle());
        }

        if (!ssaoTexture && m_SSAOPass)
        {
            ssaoTexture = resources.GetTexture(m_SSAOPass->GetSSAOBlurredHandle());
        }

        if (context.SharedResources)
        {
            if (!albedoTexture)
            {
                albedoTexture = context.SharedResources->GetTexturePtr("GBuffer_Albedo");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || albedoTexture != nullptr;
            }
            if (!normalTexture)
            {
                normalTexture = context.SharedResources->GetTexturePtr("GBuffer_Normal");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || normalTexture != nullptr;
            }
            if (!materialTexture)
            {
                materialTexture = context.SharedResources->GetTexturePtr("GBuffer_Material");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || materialTexture != nullptr;
            }
            if (!depthTexture)
            {
                depthTexture = context.SharedResources->GetTexturePtr("GBuffer_Depth");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || depthTexture != nullptr;
            }
            if (!velocityTexture)
            {
                velocityTexture = context.SharedResources->GetTexturePtr("GBuffer_Velocity");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || velocityTexture != nullptr;
            }
            if (!emissiveTexture)
            {
                emissiveTexture = context.SharedResources->GetTexturePtr("GBuffer_Emissive");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || emissiveTexture != nullptr;
            }
            if (!ssaoTexture)
            {
                ssaoTexture = context.SharedResources->GetTexturePtr("SSAO");
                bUsedSharedResourceFallback = bUsedSharedResourceFallback || ssaoTexture != nullptr;
            }
        }

        RHI::TexturePtr shadowMapTexture;
        if (m_ShadowMapHandle.IsValid())
        {
            shadowMapTexture = resources.GetTexture(m_ShadowMapHandle);
        }
        if (!shadowMapTexture && context.SharedResources)
        {
            shadowMapTexture = context.SharedResources->GetTexturePtr("ShadowMap");
        }

        if (!PrepareLightingOutput(sceneColorTexture->GetWidth(),
                                   sceneColorTexture->GetHeight(),
                                   sceneColorTexture,
                                   true,
                                   context))
        {
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        ExecuteWithInputs(context,
                          albedoTexture,
                          normalTexture,
                          materialTexture,
                          depthTexture,
                          velocityTexture,
                          emissiveTexture,
                          ssaoTexture,
                          shadowMapTexture,
                          rtgiDiffuseIndirectTexture,
                          m_bLegacyInputFallbackActive || bUsedSharedResourceFallback);
    }

    void LightingPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_LightingRenderPass || !m_LightingFramebuffer || !m_LightingPipeline)
        {
            NORVES_LOG_WARNING("LightingPass", "Lighting resources not ready, skipping");
            return;
        }

        RHI::TexturePtr albedoPtr;
        RHI::TexturePtr normalPtr;
        RHI::TexturePtr materialPtr;
        RHI::TexturePtr depthPtr;
        RHI::TexturePtr velocityPtr;
        RHI::TexturePtr emissivePtr;
        RHI::TexturePtr ssaoPtr;
        RHI::TexturePtr shadowMapPtr;

        if (context.SharedResources)
        {
            albedoPtr = context.SharedResources->GetTexturePtr("GBuffer_Albedo");
            normalPtr = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            materialPtr = context.SharedResources->GetTexturePtr("GBuffer_Material");
            depthPtr = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            velocityPtr = context.SharedResources->GetTexturePtr("GBuffer_Velocity");
            emissivePtr = context.SharedResources->GetTexturePtr("GBuffer_Emissive");
            ssaoPtr = context.SharedResources->GetTexturePtr("SSAO");
            shadowMapPtr = context.SharedResources->GetTexturePtr("ShadowMap");
        }

        if (!albedoPtr || !normalPtr || !materialPtr || !depthPtr)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer textures not available, skipping lighting");
            return;
        }

        ExecuteWithInputs(context,
                          albedoPtr,
                          normalPtr,
                          materialPtr,
                          depthPtr,
                          velocityPtr,
                          emissivePtr,
                          ssaoPtr,
                          shadowMapPtr,
                          RHI::TexturePtr{},
                          true);
    }

    uint32_t LightingPass::ResolveLightingWidth(const ViewRenderContext& context) const
    {
        return context.GetActiveRenderWidth();
    }

    uint32_t LightingPass::ResolveLightingHeight(const ViewRenderContext& context) const
    {
        return context.GetActiveRenderHeight();
    }

    bool LightingPass::CreateLightingResources(uint32_t width, uint32_t height, ViewRenderContext& context)
    {
        if (!m_Device)
        {
            return false;
        }

        RHI::TextureDesc sceneColorDesc =
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor");
        sceneColorDesc.Usage = sceneColorDesc.Usage | RHI::ResourceUsage::TransferSrc;
        RHI::TexturePtr sceneColorTexture = m_Device->CreateTexture(sceneColorDesc);
        if (!sceneColorTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create SceneColor texture");
            return false;
        }

        return PrepareLightingOutput(width, height, sceneColorTexture, false, context);
    }

    bool LightingPass::PrepareLightingOutput(uint32_t width,
                                             uint32_t height,
                                             const RHI::TexturePtr& sceneColorTexture,
                                             bool bUseRenderGraphInitialState,
                                             ViewRenderContext& context)
    {
        (void)context;

        if (!m_Device || !sceneColorTexture || width == 0 || height == 0)
        {
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_SceneColorTexture = sceneColorTexture;
        m_bUsingRenderGraphResources = bUseRenderGraphInitialState;

        const RenderPassSignature signature = CreateLightingRenderPassSignature(width,
                                                                                height,
                                                                                sceneColorTexture,
                                                                                bUseRenderGraphInitialState);

        if (!EnsureLightingRenderPass(signature))
        {
            return false;
        }

        if (!EnsureLightingFramebuffer(width, height, sceneColorTexture))
        {
            return false;
        }

        if (!EnsureLightingDescriptorSet())
        {
            return false;
        }

        if (!EnsureLightingPipeline())
        {
            return false;
        }

        return true;
    }

    bool LightingPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                                 const AttachmentSignature& rhs) const
    {
        return lhs.Kind == rhs.Kind &&
               lhs.Format == rhs.Format &&
               lhs.LoadOp == rhs.LoadOp &&
               lhs.StoreOp == rhs.StoreOp &&
               lhs.InitialState == rhs.InitialState &&
               lhs.FinalState == rhs.FinalState &&
               lhs.Target == rhs.Target &&
               lhs.Width == rhs.Width &&
               lhs.Height == rhs.Height &&
               lhs.bDepthReadOnly == rhs.bDepthReadOnly;
    }

    bool LightingPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                 const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.SceneColor, rhs.SceneColor);
    }

    LightingPass::RenderPassSignature LightingPass::CreateLightingRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& sceneColorTexture,
        bool bUseRenderGraphInitialState) const
    {
        RenderPassSignature signature;
        signature.bValid = true;
        signature.SceneColor = {RGAttachmentKind::Color,
                                sceneColorTexture ? sceneColorTexture->GetFormat() : m_Settings.OutputFormat,
                                RHI::AttachmentLoadOp::Clear,
                                RHI::AttachmentStoreOp::Store,
                                bUseRenderGraphInitialState ? RHI::ResourceState::RenderTarget : RHI::ResourceState::Undefined,
                                RHI::ResourceState::ShaderResource,
                                sceneColorTexture.get(),
                                width,
                                height,
                                false};
        return signature;
    }

    bool LightingPass::EnsureLightingRenderPass(const RenderPassSignature& signature)
    {
        if (m_LightingRenderPass &&
            RenderPassSignatureEquals(m_RenderPassSignature, signature))
        {
            return true;
        }

        m_LightingRenderPass.reset();
        m_LightingFramebuffer.reset();
        m_LightingPipeline.reset();
        m_FramebufferSceneColorTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;
        m_RenderPassSignature = {};

        RHI::RenderPassDesc rpDesc;

        RHI::AttachmentDesc colorAttach;
        colorAttach.format = signature.SceneColor.Format;
        colorAttach.isDepthStencil = false;
        colorAttach.clear = true;
        colorAttach.clearColor[0] = 0.0f;
        colorAttach.clearColor[1] = 0.0f;
        colorAttach.clearColor[2] = 0.0f;
        colorAttach.clearColor[3] = 1.0f;
        colorAttach.loadOp = signature.SceneColor.LoadOp;
        colorAttach.storeOp = signature.SceneColor.StoreOp;
        colorAttach.initialState = signature.SceneColor.InitialState;
        colorAttach.finalState = signature.SceneColor.FinalState;
        rpDesc.colorAttachments.push_back(colorAttach);
        rpDesc.hasDepthStencil = false;

        m_LightingRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_LightingRenderPass)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting render pass");
            return false;
        }

        m_bRenderPassUsesRenderGraphInitialState =
            signature.SceneColor.InitialState == RHI::ResourceState::RenderTarget;
        m_RenderPassSignature = signature;
        return true;
    }

    bool LightingPass::EnsureLightingFramebuffer(uint32_t width,
                                                 uint32_t height,
                                                 const RHI::TexturePtr& sceneColorTexture)
    {
        if (m_LightingFramebuffer &&
            m_FramebufferSceneColorTexture == sceneColorTexture.get() &&
            m_FramebufferWidth == width &&
            m_FramebufferHeight == height)
        {
            return true;
        }

        if (!m_LightingRenderPass || !sceneColorTexture)
        {
            return false;
        }

        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_LightingRenderPass;
        fbDesc.colorTargets.push_back(sceneColorTexture);
        fbDesc.width = width;
        fbDesc.height = height;

        m_LightingFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_LightingFramebuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting framebuffer");
            return false;
        }

        m_FramebufferSceneColorTexture = sceneColorTexture.get();
        m_FramebufferWidth = width;
        m_FramebufferHeight = height;
        return true;
    }

    bool LightingPass::CreateLightingDescriptorSet(RHI::DescriptorSetPtr& outDescriptorSet)
    {
        outDescriptorSet.reset();
        if (!m_Device || !m_LightDataBuffer || !m_LightArrayBuffer || !m_BrdfLutTexture ||
            !m_DefaultBlackTexture || !m_DefaultShadowMapArrayTexture ||
            !m_DefaultDDGIIrradianceAtlas || !m_DefaultDDGIDistanceAtlas ||
            !m_DefaultNeuralBRDFWeightBuffer ||
            !m_GBufferSampler || !m_IBLSampler || !m_DiffuseIrradianceSampler ||
            !m_PrefilteredSpecularSampler || !m_DfgSampler || !m_DDGISampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Mandatory lighting descriptor resources are unavailable");
            return false;
        }

        RHI::DescriptorSetDesc dsDesc = CreateLightingDescriptorSetDesc(m_bNeuralBRDFAvailable);
        RHI::DescriptorSetPtr descriptorSet = m_Device->CreateDescriptorSet(dsDesc);
        if (!descriptorSet)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting descriptor set");
            return false;
        }

        descriptorSet->BindConstantBuffer(4, m_LightDataBuffer, 0u, LIGHTING_PARAMS_SIZE);
        descriptorSet->BindStorageBuffer(5,
                                         m_LightArrayBuffer,
                                         0u,
                                         GetLightArrayBufferSizeBytes());
        descriptorSet->BindTexture(6, m_DefaultShadowMapArrayTexture);
        descriptorSet->BindSampler(6, m_GBufferSampler);
        descriptorSet->BindTexture(8, m_DefaultBlackTexture);
        descriptorSet->BindSampler(8, m_IBLSampler);
        descriptorSet->BindTexture(9, m_BrdfLutTexture);
        descriptorSet->BindSampler(9, m_DfgSampler);
        if (m_bNeuralBRDFAvailable && m_NeuralBRDFWeightBuffer)
        {
            descriptorSet->BindStorageBuffer(
                11,
                m_NeuralBRDFWeightBuffer,
                0u,
                static_cast<uint32_t>(m_NeuralBRDFData.GetWeightDataSizeFP32()));
        }
        else
        {
            descriptorSet->BindStorageBuffer(11, m_DefaultNeuralBRDFWeightBuffer, 0u, 4u);
        }
        descriptorSet->BindTexture(12, m_DefaultBlackTexture);
        descriptorSet->BindSampler(12, m_DiffuseIrradianceSampler);
        descriptorSet->BindTexture(13, m_DefaultBlackTexture);
        descriptorSet->BindSampler(13, m_PrefilteredSpecularSampler);
        descriptorSet->BindTexture(14, m_DefaultBlackTexture);
        descriptorSet->BindSampler(14, m_IBLSampler);
        descriptorSet->BindTexture(15, m_DefaultBlackTexture);
        descriptorSet->BindSampler(15, m_IBLSampler);
        descriptorSet->BindTexture(16, m_DefaultBlackTexture);
        descriptorSet->BindSampler(16, m_GBufferSampler);
        descriptorSet->BindTexture(17, m_DefaultDDGIIrradianceAtlas);
        descriptorSet->BindSampler(17, m_DDGISampler);
        descriptorSet->BindTexture(18, m_DefaultDDGIDistanceAtlas);
        descriptorSet->BindSampler(18, m_DDGISampler);
        descriptorSet->BindTexture(19, m_DefaultBlackTexture);
        descriptorSet->BindSampler(19, m_GBufferSampler);

        outDescriptorSet = std::move(descriptorSet);
        return true;
    }

    bool LightingPass::EnsureLightingDescriptorSet()
    {
        if (m_LightingDescriptorSet)
        {
            return true;
        }

        RHI::DescriptorSetPtr descriptorSet;
        if (!CreateLightingDescriptorSet(descriptorSet))
        {
            return false;
        }
        m_LightingDescriptorSet = std::move(descriptorSet);
        return true;
    }

    bool LightingPass::EnsureLightingPipeline()
    {
        if (m_LightingPipeline)
        {
            return true;
        }

        if (!m_LightingRenderPass || !m_LightingVertexShader || !m_LightingFragmentShader)
        {
            return false;
        }

        RHI::DescriptorSetDesc dsDesc = CreateLightingDescriptorSetDesc(m_bNeuralBRDFAvailable);

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_LightingVertexShader;
        pipelineDesc.pixelShader = m_LightingFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;

        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);

        pipelineDesc.renderPass = m_LightingRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_LightingPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_LightingPipeline)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting pipeline");
            return false;
        }

        return true;
    }

    bool LightingPass::EnsureRTGIComputePipeline(ViewRenderContext& context)
    {
        if (m_bRTGIComputeUnavailable)
        {
            return false;
        }
        if (m_RTGIComputePipeline && m_RTGIComputeParametersBuffer &&
            m_RTGIComputeInstanceDataBuffer)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr || !context.CommandList ||
            !context.RTGICapability.IsUsable())
        {
            return false;
        }

        m_Device = context.Device;
        m_RTGIComputeShader = context.ShaderMgr->LoadShader(
            "RTGI/DiffuseIndirect.comp", RHI::ShaderStage::Compute);
        if (!m_RTGIComputeShader)
        {
            m_bRTGIComputeUnavailable = true;
            NORVES_LOG_WARNING("LightingPass", "RTGI ray-query compute shaderを読み込めません");
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_RTGIComputeShader;
        pipelineDesc.descriptorSetLayouts.push_back(CreateRTGIComputeDescriptorSetDesc());
        m_RTGIComputePipeline = context.Device->CreateComputePipeline(pipelineDesc);
        if (!m_RTGIComputePipeline)
        {
            m_bRTGIComputeUnavailable = true;
            NORVES_LOG_WARNING("LightingPass", "RTGI ray-query compute pipelineを作成できません");
            return false;
        }

        RHI::BufferDesc parametersDesc(
            sizeof(RTGIComputeParameters),
            RHI::ResourceUsage::ConstantBuffer,
            true,
            "RTGI.DiffuseIndirect.Parameters");
        m_RTGIComputeParametersBuffer = context.Device->CreateBuffer(parametersDesc);
        if (!m_RTGIComputeParametersBuffer)
        {
            m_bRTGIComputeUnavailable = true;
            NORVES_LOG_WARNING("LightingPass", "RTGI parameter bufferを作成できません");
            return false;
        }

        RHI::BufferDesc instanceDataDesc(
            sizeof(RTGIInstanceData),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
            true,
            "RTGI.DiffuseIndirect.Instances");
        m_RTGIComputeInstanceDataBuffer = context.Device->CreateBuffer(instanceDataDesc);
        if (!m_RTGIComputeInstanceDataBuffer)
        {
            m_bRTGIComputeUnavailable = true;
            NORVES_LOG_WARNING("LightingPass", "RTGI instance bufferを作成できません");
            return false;
        }
        m_RTGIComputeInstanceDataCapacity = m_RTGIComputeInstanceDataBuffer->GetSize();
        return true;
    }

    bool LightingPass::EnsureRTGIHistoryTextures(uint32_t width, uint32_t height)
    {
        if (!m_Device || width == 0u || height == 0u)
        {
            return false;
        }
        bool bHistoryTexturesComplete = m_RTGIHistoryWidth == width &&
            m_RTGIHistoryHeight == height;
        for (const RTGIHistoryTextureSet& history : m_RTGIHistoryTextures)
        {
            bHistoryTexturesComplete = bHistoryTexturesComplete &&
                history.Radiance && history.Age && history.Confidence && history.Depth &&
                history.Normal && history.Material;
        }
        if (bHistoryTexturesComplete)
        {
            return true;
        }

        const RHI::ResourceUsage historyUsage = RHI::ResourceUsage::ShaderRead |
            RHI::ResourceUsage::ShaderWrite;
        RTGIHistoryTextureSet newHistory[2];
        for (uint32_t slotIndex = 0u; slotIndex < 2u; ++slotIndex)
        {
            RHI::TextureDesc radianceDesc;
            radianceDesc.Width = width;
            radianceDesc.Height = height;
            radianceDesc.TextureFormat = RTGIDiffuseIndirectRadianceFormat;
            radianceDesc.Usage = historyUsage;
            radianceDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentRadiance"
                : "RTGI.History.HistoryRadiance";
            newHistory[slotIndex].Radiance = m_Device->CreateTexture(radianceDesc);

            RHI::TextureDesc ageDesc = radianceDesc;
            ageDesc.TextureFormat = RTGIHistoryAgeFormat;
            ageDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentAge"
                : "RTGI.History.HistoryAge";
            newHistory[slotIndex].Age = m_Device->CreateTexture(ageDesc);

            RHI::TextureDesc confidenceDesc = ageDesc;
            confidenceDesc.TextureFormat = RTGIHistoryConfidenceFormat;
            confidenceDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentConfidence"
                : "RTGI.History.HistoryConfidence";
            newHistory[slotIndex].Confidence = m_Device->CreateTexture(confidenceDesc);

            RHI::TextureDesc gbufferHistoryDesc = radianceDesc;
            gbufferHistoryDesc.TextureFormat = RTGIHistoryGBufferFormat;
            gbufferHistoryDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentDepth"
                : "RTGI.History.HistoryDepth";
            newHistory[slotIndex].Depth = m_Device->CreateTexture(gbufferHistoryDesc);
            gbufferHistoryDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentNormal"
                : "RTGI.History.HistoryNormal";
            newHistory[slotIndex].Normal = m_Device->CreateTexture(gbufferHistoryDesc);
            gbufferHistoryDesc.DebugName = slotIndex == 0u
                ? "RTGI.History.CurrentMaterial"
                : "RTGI.History.HistoryMaterial";
            newHistory[slotIndex].Material = m_Device->CreateTexture(gbufferHistoryDesc);

            if (!newHistory[slotIndex].Radiance || !newHistory[slotIndex].Age ||
                !newHistory[slotIndex].Confidence || !newHistory[slotIndex].Depth ||
                !newHistory[slotIndex].Normal || !newHistory[slotIndex].Material)
            {
                return false;
            }
        }

        for (uint32_t slotIndex = 0u; slotIndex < 2u; ++slotIndex)
        {
            m_RTGIHistoryTextures[slotIndex] = std::move(newHistory[slotIndex]);
            m_RTGIHistorySlotState[slotIndex] = RHI::ResourceState::Undefined;
        }
        m_RTGIHistoryWidth = width;
        m_RTGIHistoryHeight = height;
        m_RTGIHistoryWriteIndex = 0u;
        InvalidateRTGIHistory();
        return true;
    }

    void LightingPass::InvalidateRTGIHistory()
    {
        m_bRTGIHistoryValid = false;
        m_bRTGIHistoryLightRevisionValid = false;
        m_RTGIHistoryAgeFrames = 0u;
        m_RTGIHistorySceneRevision = 0u;
        m_RTGIHistoryLightRevision = 0u;
        m_RTGIHistoryLightWeightLimitedFrames = 0u;
    }

    bool LightingPass::ExecuteRTGI(ViewRenderContext& context,
                                   const RHI::TexturePtr& albedoTexture,
                                   const RHI::TexturePtr& normalTexture,
                                   const RHI::TexturePtr& materialTexture,
                                   const RHI::TexturePtr& depthTexture,
                                   const RHI::TexturePtr& velocityTexture,
                                   const RHI::TexturePtr& rtgiDiffuseIndirectTexture,
                                   const GPULightingParams& lightingParams)
    try
    {
        const auto fail = [this]() -> bool
        {
            InvalidateRTGIHistory();
            return false;
        };
        if (!context.CommandList || !context.Device || !context.bRTGIEnabled ||
            !context.bRTGITLASAvailable || !context.RTGICapability.IsUsable() ||
            !context.SnapshotRayTracingScene ||
            !context.SnapshotRayTracingScene->IsComplete() ||
            !albedoTexture || !normalTexture || !materialTexture || !depthTexture ||
            !velocityTexture ||
            !rtgiDiffuseIndirectTexture ||
            rtgiDiffuseIndirectTexture->GetFormat() != RTGIDiffuseIndirectRadianceFormat ||
            (rtgiDiffuseIndirectTexture->GetUsage() & RHI::ResourceUsage::ShaderRead) ==
                RHI::ResourceUsage::None ||
            (rtgiDiffuseIndirectTexture->GetUsage() & RHI::ResourceUsage::ShaderWrite) ==
                RHI::ResourceUsage::None)
        {
            return fail();
        }

        const uint32_t debugViewMode = static_cast<uint32_t>(context.GetActiveDebugMode());
        if (debugViewMode >= 246u && debugViewMode <= 255u)
        {
            return fail();
        }

        const uint32_t width = rtgiDiffuseIndirectTexture->GetWidth();
        const uint32_t height = rtgiDiffuseIndirectTexture->GetHeight();
        if (width == 0u || height == 0u ||
            width > std::numeric_limits<uint32_t>::max() -
                        (RTGI_COMPUTE_WORKGROUP_SIZE - 1u) ||
            height > std::numeric_limits<uint32_t>::max() -
                         (RTGI_COMPUTE_WORKGROUP_SIZE - 1u))
        {
            return fail();
        }

        Container::VariableArray<RTGIInstanceData> instanceData;
        Container::VariableArray<RHI::BufferPtr> geometryBuffers;
        if (!TryBuildRTGIInstanceData(*context.SnapshotRayTracingScene,
                                      instanceData,
                                      geometryBuffers))
        {
            return fail();
        }
        if (!EnsureRTGIComputePipeline(context) ||
            !EnsureRTGIHistoryTextures(width, height))
        {
            return fail();
        }

        const uint64_t requiredInstanceDataSize =
            static_cast<uint64_t>(instanceData.size()) * sizeof(RTGIInstanceData);
        if (requiredInstanceDataSize == 0u ||
            requiredInstanceDataSize > std::numeric_limits<uint32_t>::max())
        {
            return fail();
        }
        if (!m_RTGIComputeInstanceDataBuffer ||
            m_RTGIComputeInstanceDataCapacity < requiredInstanceDataSize)
        {
            RHI::BufferDesc instanceDataDesc(
                requiredInstanceDataSize,
                RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                true,
                "RTGI.DiffuseIndirect.Instances");
            RHI::BufferPtr instanceDataBuffer = context.Device->CreateBuffer(instanceDataDesc);
            if (!instanceDataBuffer)
            {
                return fail();
            }
            m_RTGIComputeInstanceDataBuffer = std::move(instanceDataBuffer);
            m_RTGIComputeInstanceDataCapacity = requiredInstanceDataSize;
            m_RTGIComputeDescriptorSet.reset();
        }

        const RHI::TexturePtr& environmentTexture =
            context.PhysicalLighting.EnvironmentRadianceTexture;
        const RHI::SamplerPtr& environmentSampler =
            context.PhysicalLighting.EnvironmentRadianceSampler;
        const RHI::BufferPtr& lightBuffer = context.PhysicalLighting.LightBuffer;
        const uint64_t requiredLightBufferSize =
            static_cast<uint64_t>(context.PhysicalLighting.LogicalLightCount > 0u
                                     ? context.PhysicalLighting.LogicalLightCount
                                     : 1u) * sizeof(GPULightData);
        if (!lightBuffer || !environmentTexture || !environmentSampler ||
            context.PhysicalLighting.LightBufferSizeBytes < requiredLightBufferSize ||
            lightBuffer->GetSize() < context.PhysicalLighting.LightBufferSizeBytes)
        {
            return fail();
        }

        const bool bHadHistory = m_bRTGIHistoryValid;
        const uint32_t writeHistoryIndex = bHadHistory
            ? (m_RTGIHistoryWriteIndex ^ 1u)
            : m_RTGIHistoryWriteIndex;
        const uint32_t readHistoryIndex = writeHistoryIndex ^ 1u;
        RTGIHistoryTextureSet& writeHistory = m_RTGIHistoryTextures[writeHistoryIndex];
        RTGIHistoryTextureSet& readHistory = m_RTGIHistoryTextures[readHistoryIndex];
        const bool bSceneRevisionMatches =
            m_bRTGIHistoryValid && m_RTGIHistorySceneRevision == context.SceneRevision;
        const bool bHistoryReprojectionValid = bSceneRevisionMatches;
        const bool bLightRevisionMismatch =
            m_bRTGIHistoryLightRevisionValid &&
            m_RTGIHistoryLightRevision != context.LightRevision;
        const uint32_t lightWeightLimitedFrames = bLightRevisionMismatch
            ? 2u
            : m_RTGIHistoryLightWeightLimitedFrames;

        const auto transitionHistorySlot = [&](RTGIHistoryTextureSet& slot,
                                                RHI::ResourceState beforeState,
                                                RHI::ResourceState afterState)
        {
            context.CommandList->TextureBarrier(slot.Radiance, beforeState, afterState);
            context.CommandList->TextureBarrier(slot.Age, beforeState, afterState);
            context.CommandList->TextureBarrier(slot.Confidence, beforeState, afterState);
            context.CommandList->TextureBarrier(slot.Depth, beforeState, afterState);
            context.CommandList->TextureBarrier(slot.Normal, beforeState, afterState);
            context.CommandList->TextureBarrier(slot.Material, beforeState, afterState);
        };

        if (m_RTGIHistorySlotState[readHistoryIndex] == RHI::ResourceState::Undefined)
        {
            transitionHistorySlot(readHistory,
                                  RHI::ResourceState::Undefined,
                                  RHI::ResourceState::ShaderResource);
            m_RTGIHistorySlotState[readHistoryIndex] = RHI::ResourceState::ShaderResource;
        }
        transitionHistorySlot(
            writeHistory,
            m_RTGIHistorySlotState[writeHistoryIndex],
            RHI::ResourceState::UnorderedAccess);

        RTGIComputeParameters parameters;
        std::memcpy(parameters.invViewProjection,
                    lightingParams.invViewProjection,
                    sizeof(parameters.invViewProjection));
        std::memcpy(parameters.cameraPosition,
                    lightingParams.cameraPosition,
                    sizeof(parameters.cameraPosition));
        parameters.imageAndSceneCounts[0] = width;
        parameters.imageAndSceneCounts[1] = height;
        parameters.imageAndSceneCounts[2] = static_cast<uint32_t>(instanceData.size());
        parameters.imageAndSceneCounts[3] = context.PhysicalLighting.LogicalLightCount;
        parameters.rayLimits[0] = RTGI_RAY_MINIMUM_DISTANCE;
        parameters.rayLimits[1] = RTGI_RAY_MAXIMUM_DISTANCE;
        parameters.rayLimits[2] = std::isfinite(lightingParams.preExposure) &&
                                           lightingParams.preExposure > 0.0f
                                       ? std::clamp(lightingParams.preExposure, 1.0e-6f, 1.0e6f)
                                       : 1.0f;
        parameters.rayLimits[3] = context.PhysicalLighting.bIBLEnabled &&
                                          std::isfinite(context.PhysicalLighting.IBLIntensity) &&
                                          context.PhysicalLighting.IBLIntensity > 0.0f
                                      ? context.PhysicalLighting.IBLIntensity
                                      : 0.0f;
        parameters.temporalState[0] = bHistoryReprojectionValid ? 1u : 0u;
        parameters.temporalState[1] = bLightRevisionMismatch ? 1u : 0u;
        parameters.temporalState[2] = lightWeightLimitedFrames > 0u ? 1u : 0u;
        parameters.temporalState[3] = RTGIHistoryMaximumAge;
        m_RTGIComputeParametersBuffer->Update(&parameters, sizeof(parameters));
        m_RTGIComputeInstanceDataBuffer->Update(
            instanceData.data(), requiredInstanceDataSize);
        m_RTGIGeometryBuffers = std::move(geometryBuffers);

        if (!m_RTGIComputeDescriptorSet)
        {
            m_RTGIComputeDescriptorSet = context.Device->CreateDescriptorSet(
                CreateRTGIComputeDescriptorSetDesc());
            if (!m_RTGIComputeDescriptorSet)
            {
                return fail();
            }
        }

        if (!m_RTGIComputeDescriptorSet->BindAccelerationStructure(
                0u, context.SnapshotRayTracingScene->TopLevel))
        {
            return fail();
        }
        m_RTGIComputeDescriptorSet->BindConstantBuffer(
            1u,
            m_RTGIComputeParametersBuffer,
            0u,
            static_cast<uint32_t>(sizeof(RTGIComputeParameters)));
        m_RTGIComputeDescriptorSet->BindTexture(2u, normalTexture);
        m_RTGIComputeDescriptorSet->BindSampler(2u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(3u, depthTexture);
        m_RTGIComputeDescriptorSet->BindSampler(3u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(4u, albedoTexture);
        m_RTGIComputeDescriptorSet->BindSampler(4u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(5u, materialTexture);
        m_RTGIComputeDescriptorSet->BindSampler(5u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindStorageTexture(6u, rtgiDiffuseIndirectTexture);
        m_RTGIComputeDescriptorSet->BindStorageBuffer(
            7u,
            m_RTGIComputeInstanceDataBuffer,
            0u,
            static_cast<uint32_t>(requiredInstanceDataSize));
        m_RTGIComputeDescriptorSet->BindStorageBuffer(
            8u,
            lightBuffer,
            0u,
            context.PhysicalLighting.LightBufferSizeBytes);
        m_RTGIComputeDescriptorSet->BindTexture(9u, environmentTexture);
        m_RTGIComputeDescriptorSet->BindSampler(9u, environmentSampler);
        m_RTGIComputeDescriptorSet->BindTexture(10u, velocityTexture);
        m_RTGIComputeDescriptorSet->BindSampler(10u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(11u, readHistory.Radiance);
        m_RTGIComputeDescriptorSet->BindSampler(11u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(12u, readHistory.Age);
        m_RTGIComputeDescriptorSet->BindSampler(12u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(13u, readHistory.Confidence);
        m_RTGIComputeDescriptorSet->BindSampler(13u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(14u, readHistory.Depth);
        m_RTGIComputeDescriptorSet->BindSampler(14u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(15u, readHistory.Normal);
        m_RTGIComputeDescriptorSet->BindSampler(15u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindTexture(16u, readHistory.Material);
        m_RTGIComputeDescriptorSet->BindSampler(16u, m_GBufferSampler);
        m_RTGIComputeDescriptorSet->BindStorageTexture(17u, writeHistory.Radiance);
        m_RTGIComputeDescriptorSet->BindStorageTexture(18u, writeHistory.Age);
        m_RTGIComputeDescriptorSet->BindStorageTexture(19u, writeHistory.Confidence);
        m_RTGIComputeDescriptorSet->BindStorageTexture(20u, writeHistory.Depth);
        m_RTGIComputeDescriptorSet->BindStorageTexture(21u, writeHistory.Normal);
        m_RTGIComputeDescriptorSet->BindStorageTexture(22u, writeHistory.Material);
        m_RTGIComputeDescriptorSet->Update();

        const uint32_t groupCountX =
            (width + RTGI_COMPUTE_WORKGROUP_SIZE - 1u) / RTGI_COMPUTE_WORKGROUP_SIZE;
        const uint32_t groupCountY =
            (height + RTGI_COMPUTE_WORKGROUP_SIZE - 1u) / RTGI_COMPUTE_WORKGROUP_SIZE;
        context.CommandList->SetPipeline(m_RTGIComputePipeline);
        context.CommandList->SetDescriptorSet(m_RTGIComputeDescriptorSet);
        context.CommandList->Dispatch(groupCountX, groupCountY, 1u);
        context.CommandList->TextureBarrier(
            rtgiDiffuseIndirectTexture,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource);
        transitionHistorySlot(writeHistory,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
        // Vulkanのstorage imageはShaderResource遷移後もgeneral layoutを保持するため、
        // 次のframeでは論理状態をUnorderedAccessとして再利用します。
        m_RTGIHistorySlotState[writeHistoryIndex] = RHI::ResourceState::UnorderedAccess;

        RTGIResult result;
        result.DiffuseIndirectRadiance = rtgiDiffuseIndirectTexture;
        result.State = RHI::ResourceState::ShaderResource;
        result.Format = RTGIDiffuseIndirectRadianceFormat;
        result.BounceCount = RTGIDiffuseBounceCount;
        result.Width = width;
        result.Height = height;
        result.FrameNumber = context.FrameNumber;
        result.SceneRevision = context.SceneRevision;
        result.LightRevision = context.LightRevision;
        result.bPreExposed = true;
        result.bDiffuse = true;
        result.bValid = true;

        const uint32_t previousAgeFrames = m_RTGIHistoryAgeFrames;
        const uint32_t currentAgeFrames = bHistoryReprojectionValid
            ? std::min(previousAgeFrames + 1u, RTGIHistoryMaximumAge)
            : 0u;
        const uint32_t nextLightWeightLimitedFrames = lightWeightLimitedFrames > 0u
            ? lightWeightLimitedFrames - 1u
            : 0u;
        const uint64_t previousSceneRevision = m_RTGIHistorySceneRevision;
        const uint64_t previousLightRevision = m_RTGIHistoryLightRevision;

        m_RTGIHistoryWriteIndex = writeHistoryIndex;
        m_bRTGIHistoryValid = true;
        m_RTGIHistoryAgeFrames = currentAgeFrames;
        m_RTGIHistorySceneRevision = context.SceneRevision;
        m_RTGIHistoryLightRevision = context.LightRevision;
        m_RTGIHistoryLightWeightLimitedFrames = nextLightWeightLimitedFrames;
        m_bRTGIHistoryLightRevisionValid = true;

        RTGIHistoryResources history;
        history.FrameNumber = context.FrameNumber;
        history.bValid = true;
        const auto populateHistorySet = [&](RTGIHistoryResourceSet& resourceSet,
                                            const RTGIHistoryTextureSet& textures,
                                            uint64_t sceneRevision,
                                            uint64_t lightRevision,
                                            uint32_t ageFrames)
        {
            resourceSet.Radiance = textures.Radiance;
            resourceSet.Age = textures.Age;
            resourceSet.Confidence = textures.Confidence;
            resourceSet.State = RHI::ResourceState::ShaderResource;
            resourceSet.Width = width;
            resourceSet.Height = height;
            resourceSet.SceneRevision = sceneRevision;
            resourceSet.LightRevision = lightRevision;
            resourceSet.AgeFrames = std::min(ageFrames, RTGIHistoryMaximumAge);
            resourceSet.bValid = true;
        };
        populateHistorySet(history.Current,
                           writeHistory,
                           context.SceneRevision,
                           context.LightRevision,
                           currentAgeFrames);
        if (bHadHistory)
        {
            populateHistorySet(history.History,
                               readHistory,
                               previousSceneRevision,
                               previousLightRevision,
                               previousAgeFrames);
        }
        else
        {
            history.History = history.Current;
        }
        context.PhysicalLighting.PublishRTGI(result, history);
        return context.PhysicalLighting.RTGI.bPublished;
    }
    catch (const std::exception& exception)
    {
        NORVES_LOG_WARNING("LightingPass",
                           "RTGI ray-queryの実行に失敗したため既存間接光へ戻ります: %s",
                           exception.what());
        InvalidateRTGIHistory();
        return false;
    }
    catch (...)
    {
        NORVES_LOG_WARNING("LightingPass",
                           "RTGI ray-queryの実行に失敗したため既存間接光へ戻ります");
        InvalidateRTGIHistory();
        return false;
    }

    void LightingPass::ExecuteWithInputs(ViewRenderContext& context,
                                         const RHI::TexturePtr& albedoTexture,
                                         const RHI::TexturePtr& normalTexture,
                                         const RHI::TexturePtr& materialTexture,
                                         const RHI::TexturePtr& depthTexture,
                                         const RHI::TexturePtr& velocityTexture,
                                         const RHI::TexturePtr& emissiveTexture,
                                         const RHI::TexturePtr& ssaoTexture,
                                         const RHI::TexturePtr& shadowMapTexture,
                                         const RHI::TexturePtr& rtgiDiffuseIndirectTexture,
                                         bool bRegisterLegacyOutputs)
    {
        if (!context.CommandList)
        {
            return;
        }

        context.PhysicalLighting.PublishShadowMapFallback(m_DefaultShadowMapArrayTexture,
                                                          m_GBufferSampler);

        if (!m_LightingRenderPass || !m_LightingFramebuffer || !m_LightingPipeline || !m_LightingDescriptorSet)
        {
            NORVES_LOG_WARNING("LightingPass", "Lighting resources not ready, skipping");
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        if (!albedoTexture || !normalTexture || !materialTexture || !depthTexture)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer textures not available, skipping lighting");
            TryEnqueueNativeTransitionPass(context);
            return;
        }

        const uint32_t debugViewMode = static_cast<uint32_t>(context.GetActiveDebugMode());
        const bool bEnableRayTracingShadow = debugViewMode == 247u || debugViewMode == 248u;
        RHI::TexturePtr rayTracingShadowVisibility;
        const bool bRayTracingShadowAvailable = m_RayTracingShadowPass.Execute(
            context,
            depthTexture,
            normalTexture,
            bEnableRayTracingShadow,
            rayTracingShadowVisibility);
        context.PhysicalLighting.PublishRayTracingShadow(
            rayTracingShadowVisibility, bRayTracingShadowAvailable);

        const bool bShadowResourceAvailable =
            shadowMapTexture &&
            shadowMapTexture->GetArraySize() == PhysicalLightingShadowCascadeCount;
        GPULightingParams lightingParams = {};
        if (!UpdateLightBuffer(context,
                               bShadowResourceAvailable,
                               ssaoTexture != nullptr,
                               &lightingParams))
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to update lighting light buffer, skipping lighting draw");
            return;
        }
        const bool bDDGIProbeUpdateSucceeded = m_DDGIProbePass.Execute(context);
        RHI::TexturePtr ddgiIrradianceAtlas = m_DDGIProbePass.GetIrradianceAtlas(
            context.FrameIndex,
            context.PhysicalLighting.ViewId,
            context.PhysicalLighting.ViewportId);
        RHI::TexturePtr ddgiDistanceAtlas = m_DDGIProbePass.GetDistanceAtlas(
            context.FrameIndex,
            context.PhysicalLighting.ViewId,
            context.PhysicalLighting.ViewportId);
        const uint32_t ddgiAtlasProbeCount = m_DDGIProbePass.GetAtlasProbeCount(
            context.FrameIndex,
            context.PhysicalLighting.ViewId,
            context.PhysicalLighting.ViewportId);
        const DDGIVolumeParameters* ddgiVolume = context.SnapshotScene != nullptr
            ? &context.SnapshotScene->DDGIVolume
            : nullptr;
        const bool bDDGILightingAvailable = bDDGIProbeUpdateSucceeded &&
            ddgiVolume != nullptr &&
            IsCompleteDDGILightingPublication(context,
                                               *ddgiVolume,
                                               ddgiIrradianceAtlas,
                                               ddgiDistanceAtlas,
                                               ddgiAtlasProbeCount,
                                               m_DDGISampler);
        context.PhysicalLighting.PublishDDGIAtlas(ddgiIrradianceAtlas,
                                                  ddgiDistanceAtlas,
                                                  ddgiAtlasProbeCount,
                                                  bDDGILightingAvailable);
        ExecuteRTGI(context,
                    albedoTexture,
                    normalTexture,
                    materialTexture,
                    depthTexture,
                    velocityTexture,
                    rtgiDiffuseIndirectTexture,
                    lightingParams);
        const RTGIFallbackDecision indirectLighting =
            context.PhysicalLighting.ResolveIndirectLighting();
        const bool bUseDDGILighting =
            indirectLighting.Source == RTGIIndirectLightingSource::DDGI;
        const bool bUseRTGILighting =
            indirectLighting.Source == RTGIIndirectLightingSource::RTGI;

        GPUDDGILightingParams ddgiParameters = {};
        if (bUseDDGILighting)
        {
            ddgiParameters.volumeOrigin[0] = ddgiVolume->Origin.x;
            ddgiParameters.volumeOrigin[1] = ddgiVolume->Origin.y;
            ddgiParameters.volumeOrigin[2] = ddgiVolume->Origin.z;
            ddgiParameters.probeSpacing[0] = ddgiVolume->ProbeSpacing.x;
            ddgiParameters.probeSpacing[1] = ddgiVolume->ProbeSpacing.y;
            ddgiParameters.probeSpacing[2] = ddgiVolume->ProbeSpacing.z;
            ddgiParameters.probeCounts[0] = ddgiVolume->ProbeCountX;
            ddgiParameters.probeCounts[1] = ddgiVolume->ProbeCountY;
            ddgiParameters.probeCounts[2] = ddgiVolume->ProbeCountZ;
            ddgiParameters.probeCounts[3] = ddgiAtlasProbeCount;
            ddgiParameters.info[0] = 1u;
        }
        if (bUseRTGILighting)
        {
            ddgiParameters.info[1] = 1u;
        }
        lightingParams.ddgi = ddgiParameters;
        m_LightDataBuffer->Update(&lightingParams, sizeof(lightingParams));

        if (m_bRegisterLegacyBridge && bRegisterLegacyOutputs)
        {
            RegisterOutputs(context, m_SceneColorTexture, depthTexture);
        }

        m_LightingDescriptorSet->BindTexture(0, albedoTexture);
        m_LightingDescriptorSet->BindTexture(1, normalTexture);
        m_LightingDescriptorSet->BindTexture(2, materialTexture);
        m_LightingDescriptorSet->BindTexture(3, depthTexture);
        m_LightingDescriptorSet->BindSampler(0, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(1, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(2, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(3, m_GBufferSampler);

        const RHI::TexturePtr& boundShadowMapTexture =
            shadowMapTexture ? shadowMapTexture : context.PhysicalLighting.ShadowMapTexture;
        const bool bShadowMapIsArray =
            boundShadowMapTexture &&
            boundShadowMapTexture->GetArraySize() == PhysicalLightingShadowCascadeCount;
        m_LightingDescriptorSet->BindTexture(
            6,
            bShadowMapIsArray ? boundShadowMapTexture : m_DefaultShadowMapArrayTexture);
        m_LightingDescriptorSet->BindSampler(
            6,
            bShadowMapIsArray && context.PhysicalLighting.ShadowSampler ?
                context.PhysicalLighting.ShadowSampler : m_GBufferSampler);

        if (emissiveTexture)
        {
            m_LightingDescriptorSet->BindTexture(7, emissiveTexture);
        }
        else
        {
            m_LightingDescriptorSet->BindTexture(7, albedoTexture);
        }
        m_LightingDescriptorSet->BindSampler(7, m_GBufferSampler);

        const uint32_t activeDebugMode = static_cast<uint32_t>(context.GetActiveDebugMode());
        const bool bValidationRaw250 = activeDebugMode == 250u;
        const bool bValidationRaw251 = activeDebugMode == 251u;
        const bool bValidationRaw252 = activeDebugMode == 252u;

        const bool bSkyAtmosphereRequested = context.SkyAtmosphere.bSnapshotEnabled;
        const bool bSkyAtmosphereAvailable =
            bSkyAtmosphereRequested && context.SkyAtmosphere.bValid &&
            context.SkyAtmosphere.RadianceTexture &&
            context.SkyAtmosphere.TransmittanceTexture &&
            context.SkyAtmosphere.SunDiskTexture &&
            context.SkyAtmosphere.Sampler && m_bSkyAtmosphereIblAvailable;

        const RHI::TexturePtr& environmentTexture =
            bValidationRaw251 ? m_DefaultBlackTexture :
            bValidationRaw252 && m_ValidationRaw252EnvironmentTexture ?
                m_ValidationRaw252EnvironmentTexture :
            bValidationRaw250 && m_ValidationRaw250EnvironmentTexture ?
                m_ValidationRaw250EnvironmentTexture :
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.RadianceTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_EnvironmentTexture ? m_EnvironmentTexture : m_DefaultBlackTexture;
        m_LightingDescriptorSet->BindTexture(8, environmentTexture);
        m_LightingDescriptorSet->BindSampler(8,
                                             bSkyAtmosphereAvailable ?
                                                 context.SkyAtmosphere.Sampler : m_IBLSampler);
        m_LightingDescriptorSet->BindTexture(9, m_BrdfLutTexture);
        m_LightingDescriptorSet->BindSampler(9, m_DfgSampler);

        const RHI::TexturePtr& diffuseIrradianceTexture =
            bValidationRaw251 ? m_DefaultBlackTexture :
            bValidationRaw252 && m_ValidationRaw252DiffuseIrradianceTexture ?
                m_ValidationRaw252DiffuseIrradianceTexture :
            bValidationRaw250 && m_ValidationRaw250DiffuseIrradianceTexture ?
                m_ValidationRaw250DiffuseIrradianceTexture :
            bSkyAtmosphereAvailable && m_SkyAtmosphereDiffuseIrradianceTexture ?
                m_SkyAtmosphereDiffuseIrradianceTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_DiffuseIrradianceTexture ?
                m_DiffuseIrradianceTexture : m_DefaultBlackTexture;
        m_LightingDescriptorSet->BindTexture(12, diffuseIrradianceTexture);
        m_LightingDescriptorSet->BindSampler(12, m_DiffuseIrradianceSampler);

        const RHI::TexturePtr& prefilteredSpecularTexture =
            bValidationRaw251 ? m_DefaultBlackTexture :
            bValidationRaw252 && m_ValidationRaw252PrefilteredSpecularTexture ?
                m_ValidationRaw252PrefilteredSpecularTexture :
            bValidationRaw250 && m_ValidationRaw250Texture ? m_ValidationRaw250Texture :
            bSkyAtmosphereAvailable && m_SkyAtmospherePrefilteredSpecularTexture ?
                m_SkyAtmospherePrefilteredSpecularTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_PrefilteredSpecularTexture ? m_PrefilteredSpecularTexture :
            m_DefaultBlackTexture;
        m_LightingDescriptorSet->BindTexture(13, prefilteredSpecularTexture);
        m_LightingDescriptorSet->BindSampler(13, m_PrefilteredSpecularSampler);

        m_LightingDescriptorSet->BindTexture(
            14,
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.SunDiskTexture : m_DefaultBlackTexture);
        m_LightingDescriptorSet->BindSampler(
            14,
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.Sampler : m_IBLSampler);
        m_LightingDescriptorSet->BindTexture(
            15,
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.TransmittanceTexture : m_DefaultBlackTexture);
        m_LightingDescriptorSet->BindSampler(
            15,
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.Sampler : m_IBLSampler);

        m_LightingDescriptorSet->BindTexture(
            16,
            context.PhysicalLighting.bRayTracingShadowPublished
                ? context.PhysicalLighting.RayTracingShadowVisibilityTexture
                : m_DefaultBlackTexture);
        m_LightingDescriptorSet->BindSampler(16, m_GBufferSampler);

        m_LightingDescriptorSet->BindTexture(
            17,
            bUseDDGILighting
                ? context.PhysicalLighting.DDGIIrradianceAtlas
                : m_DefaultDDGIIrradianceAtlas);
        m_LightingDescriptorSet->BindSampler(17, m_DDGISampler);
        m_LightingDescriptorSet->BindTexture(
            18,
            bUseDDGILighting
                ? context.PhysicalLighting.DDGIDistanceAtlas
                : m_DefaultDDGIDistanceAtlas);
        m_LightingDescriptorSet->BindSampler(18, m_DDGISampler);
        m_LightingDescriptorSet->BindTexture(
            19,
            bUseRTGILighting && context.PhysicalLighting.RTGI.Result.DiffuseIndirectRadiance
                ? context.PhysicalLighting.RTGI.Result.DiffuseIndirectRadiance
                : m_DefaultBlackTexture);
        m_LightingDescriptorSet->BindSampler(19, m_GBufferSampler);

        if (ssaoTexture)
        {
            m_LightingDescriptorSet->BindTexture(10, ssaoTexture);
        }
        else
        {
            m_LightingDescriptorSet->BindTexture(10, albedoTexture);
        }
        m_LightingDescriptorSet->BindSampler(10, m_GBufferSampler);

        m_LightingDescriptorSet->BindStorageBuffer(5,
                                                   m_LightArrayBuffer,
                                                   0,
                                                   GetLightArrayBufferSizeBytes());

        if (m_bNeuralBRDFAvailable && m_NeuralBRDFWeightBuffer)
        {
            m_LightingDescriptorSet->BindStorageBuffer(
                11, m_NeuralBRDFWeightBuffer, 0,
                static_cast<uint32_t>(m_NeuralBRDFData.GetWeightDataSizeFP32()));
        }
        else
        {
            m_LightingDescriptorSet->BindStorageBuffer(11, m_DefaultNeuralBRDFWeightBuffer, 0, 4u);
        }

        m_LightingDescriptorSet->Update();

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        context.EnqueueFullscreenPass(m_LightingRenderPass,
                                      m_LightingFramebuffer,
                                      viewport,
                                      scissor,
                                      m_LightingPipeline,
                                      m_LightingDescriptorSet);
    }

    void LightingPass::RegisterOutputs(ViewRenderContext& context,
                                       const RHI::TexturePtr& sceneColorTexture,
                                       const RHI::TexturePtr& depthTexture) const
    {
        if (!context.SharedResources)
        {
            return;
        }

        if (sceneColorTexture)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", sceneColorTexture);
        }

        if (depthTexture)
        {
            context.SharedResources->RegisterTexturePtr("SceneDepth", depthTexture);
        }
    }

    bool LightingPass::TryEnqueueNativeTransitionPass(ViewRenderContext& context) const
    {
        if (!m_bUsingRenderGraphResources || !m_LightingRenderPass || !m_LightingFramebuffer)
        {
            return false;
        }

        context.EnqueueFullscreenPass(m_LightingRenderPass,
                                      m_LightingFramebuffer,
                                      context.GetActiveLocalViewport(),
                                      context.GetActiveLocalScissor(),
                                      RHI::PipelinePtr{},
                                      RHI::DescriptorSetPtr{},
                                      0,
                                      0);
        return true;
    }

    bool LightingPass::EnsureLightArrayBufferCapacity(uint32_t requiredLightCount)
    {
        if (requiredLightCount == 0)
        {
            requiredLightCount = 1;
        }

        if (requiredLightCount <= m_LightArrayCapacity && m_LightArrayBuffer)
        {
            return true;
        }

        if (!m_Device)
        {
            return false;
        }

        uint32_t newCapacity = m_LightArrayCapacity > 0 ? m_LightArrayCapacity : 1;
        while (newCapacity < requiredLightCount)
        {
            newCapacity *= 2;
        }

        const uint64_t newSizeBytes = static_cast<uint64_t>(newCapacity) * sizeof(GPULightData);
        RHI::BufferDesc lightArraySsboDesc(newSizeBytes,
                                           RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead,
                                           true,
                                           "LightArraySSBO");
        RHI::BufferPtr newLightArrayBuffer = m_Device->CreateBuffer(lightArraySsboDesc);
        if (!newLightArrayBuffer)
        {
            return false;
        }

        if (m_LightArrayBuffer)
        {
            m_RetiredLightArrayBuffers.push_back(m_LightArrayBuffer);
        }

        m_LightArrayBuffer = newLightArrayBuffer;
        m_LightArrayCapacity = newCapacity;
        return true;
    }

    uint32_t LightingPass::GetLightArrayBufferSizeBytes() const
    {
        return m_LightArrayCapacity * static_cast<uint32_t>(sizeof(GPULightData));
    }

    bool LightingPass::EnsureSkyAtmosphereIbl(const SkyAtmosphereParameters& parameters,
                                              uint32_t radianceWidth,
                                              uint32_t radianceHeight)
    {
        const SkyAtmosphereParameters sanitized =
            SanitizeSkyAtmosphereParameters(parameters);
        if (!m_Device || !sanitized.bEnabled || radianceWidth == 0u || radianceHeight == 0u)
        {
            m_SkyAtmosphereDiffuseIrradianceTexture.reset();
            m_SkyAtmospherePrefilteredSpecularTexture.reset();
            m_bSkyAtmosphereIblCacheValid = false;
            m_bSkyAtmosphereIblAvailable = false;
            return false;
        }

        if (m_bSkyAtmosphereIblCacheValid &&
            m_SkyAtmosphereIblRadianceWidth == radianceWidth &&
            m_SkyAtmosphereIblRadianceHeight == radianceHeight &&
            AreSkyAtmosphereParametersEqual(m_SkyAtmosphereIblParameters, sanitized))
        {
            return m_bSkyAtmosphereIblAvailable;
        }

        m_SkyAtmosphereIblParameters = sanitized;
        m_SkyAtmosphereIblRadianceWidth = radianceWidth;
        m_SkyAtmosphereIblRadianceHeight = radianceHeight;
        m_bSkyAtmosphereIblCacheValid = true;
        m_bSkyAtmosphereIblAvailable = false;
        m_SkyAtmosphereDiffuseIrradianceTexture.reset();
        m_SkyAtmospherePrefilteredSpecularTexture.reset();

        Container::VariableArray<float> sourceData;
        if (!BuildSkyAtmosphereRadianceSource(sanitized,
                                              radianceWidth,
                                              radianceHeight,
                                              sourceData))
        {
            NORVES_LOG_WARNING("LightingPass",
                               "Sky atmosphere radiance source generation failed; using black IBL");
            return false;
        }

        RHI::TexturePtr generatedEnvironment;
        RHI::TexturePtr generatedDiffuse;
        RHI::TexturePtr generatedPrefilter;
        if (!CreateIblResources(m_Device,
                                sourceData,
                                radianceWidth,
                                radianceHeight,
                                "SkyAtmosphere.Environment",
                                "SkyAtmosphere.DiffuseIrradiance",
                                "SkyAtmosphere.PrefilteredSpecular",
                                generatedEnvironment,
                                generatedDiffuse,
                                generatedPrefilter))
        {
            NORVES_LOG_WARNING("LightingPass",
                               "Sky atmosphere IBL generation failed; using black IBL");
            return false;
        }

        m_SkyAtmosphereDiffuseIrradianceTexture = generatedDiffuse;
        m_SkyAtmospherePrefilteredSpecularTexture = generatedPrefilter;
        m_bSkyAtmosphereIblAvailable =
            m_SkyAtmosphereDiffuseIrradianceTexture &&
            m_SkyAtmospherePrefilteredSpecularTexture;
        return m_bSkyAtmosphereIblAvailable;
    }

    bool LightingPass::UpdateLightBuffer(ViewRenderContext& context,
                                         bool bShadowAvailable,
                                         bool bSSAOAvailable,
                                         GPULightingParams* outParams)
    {
        // ライティングパラメータを構築
        GPULightingParams params = {};
        InitializeSafeCascadedShadowParams(params);

        using namespace NorvesLib::Math;

        params.cameraForward[0] = 0.0f;
        params.cameraForward[1] = 0.0f;
        params.cameraForward[2] = -1.0f;
        params.cameraForward[3] = 0.0f;
        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            params.preExposure = std::isfinite(activeCamera->PreExposure) &&
                                         activeCamera->PreExposure > 0.0f
                                     ? std::clamp(activeCamera->PreExposure, 1.0e-6f, 1.0e6f)
                                     : 1.0f;
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyCameraPosition(params.cameraPosition);
            cameraConstants.CopyShaderInverseViewProjection(params.invViewProjection);
            const float forwardLengthSquared =
                activeCamera->ForwardX * activeCamera->ForwardX +
                activeCamera->ForwardY * activeCamera->ForwardY +
                activeCamera->ForwardZ * activeCamera->ForwardZ;
            if (std::isfinite(forwardLengthSquared) && forwardLengthSquared > 1.0e-10f)
            {
                const float inverseForwardLength = 1.0f / std::sqrt(forwardLengthSquared);
                if (std::isfinite(inverseForwardLength))
                {
                    params.cameraForward[0] = activeCamera->ForwardX * inverseForwardLength;
                    params.cameraForward[1] = activeCamera->ForwardY * inverseForwardLength;
                    params.cameraForward[2] = activeCamera->ForwardZ * inverseForwardLength;
                }
            }
        }
        else
        {
            params.preExposure = 1.0f;
            params.cameraPosition[0] = 0.0f;
            params.cameraPosition[1] = 2.0f;
            params.cameraPosition[2] = 5.0f;
            params.cameraPosition[3] = 1.0f;
            MatrixUtils::TransposeToShaderData(Matrix4x4::Identity, params.invViewProjection);
        }
        params.skySunDirectionAndCosRadius[0] = 0.0f;
        params.skySunDirectionAndCosRadius[1] = 1.0f;
        params.skySunDirectionAndCosRadius[2] = 0.0f;
        params.skySunDirectionAndCosRadius[3] = 1.0f;

        // アンビエントカラー
        params.ambientColor[0] = m_Settings.AmbientColor[0];
        params.ambientColor[1] = m_Settings.AmbientColor[1];
        params.ambientColor[2] = m_Settings.AmbientColor[2];
        params.ambientColor[3] = m_Settings.AmbientIntensity;

        // ========================================
        // ShadowMapPassが公開した4カスケードの行列・分割距離
        // ========================================
        const bool bCascadedShadowEnabled =
            HasValidCascadedShadowPublication(context, bShadowAvailable);
        const bool bRayTracingShadowEnabled =
            context.PhysicalLighting.bRayTracingShadowPublished &&
            context.PhysicalLighting.RayTracingShadowVisibilityTexture;
        // shadowPadding0はRT可視性テクスチャが有効なフレームを示す。
        params.shadowPadding0 = bRayTracingShadowEnabled ? 1u : 0u;
        if (bCascadedShadowEnabled)
        {
            std::memcpy(params.lightView,
                        context.PhysicalLighting.CascadedShadow.View,
                        sizeof(params.lightView));
            std::memcpy(params.lightProjection,
                        context.PhysicalLighting.CascadedShadow.Projection,
                        sizeof(params.lightProjection));
            std::memcpy(params.shadowSplitDistances,
                        context.PhysicalLighting.CascadedShadow.SplitDistances,
                        sizeof(float) * PhysicalLightingShadowSplitCount);
            params.cascadeCount = PhysicalLightingShadowCascadeCount;
            params.bShadowEnabled = 1u;
        }
        else if (bRayTracingShadowEnabled)
        {
            params.bShadowEnabled = 1u;
        }

        // ========================================
        // SceneViewのLightProxyからライト配列を構築
        // ========================================
        Container::VariableArray<GPULightData> lightArray;
        Container::Span<const LightProxy> lightProxies;
        if (context.SnapshotLightProxies)
        {
            lightProxies = Container::Span<const LightProxy>(*context.SnapshotLightProxies);
        }

        uint32_t lightCount = 0;
        lightCount = PackLightingPassLights(lightProxies, lightArray);
        if (!EnsureLightArrayBufferCapacity(lightCount))
        {
            return false;
        }

        // SSAOパラメータ設定
        params.debugViewMode = static_cast<uint32_t>(context.GetActiveDebugMode());
        const bool bValidationMode = params.debugViewMode >= 246u &&
                                     params.debugViewMode <= 255u;
        params.bSSAOEnabled = bValidationMode ? 0u : (bSSAOAvailable ? 1u : 0u);
        params.bNeuralBRDFEnabled = bValidationMode ? 0u :
                                    (m_bNeuralBRDFAvailable ? 1u : 0u);
        params.lightCount = lightCount;

        // IBLパラメータ設定
        params.prefilteredSpecularMipLevels = 9u;
        const bool bValidationRaw250 = params.debugViewMode == 250u;
        const bool bValidationRaw251 = params.debugViewMode == 251u;
        const bool bValidationRaw252 = params.debugViewMode == 252u;
        if (bValidationRaw252 &&
            (!m_ValidationRaw252EnvironmentTexture ||
             !m_ValidationRaw252DiffuseIrradianceTexture ||
             !m_ValidationRaw252PrefilteredSpecularTexture) &&
            !GenerateValidationSnapshots())
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lazy raw252 validation snapshots");
            return false;
        }
        const bool bValidationConstantIblAvailable =
            bValidationRaw252 && m_ValidationRaw252EnvironmentTexture &&
            m_ValidationRaw252DiffuseIrradianceTexture &&
            m_ValidationRaw252PrefilteredSpecularTexture;
        const bool bSkyAtmosphereRequested = context.SkyAtmosphere.bSnapshotEnabled;
        const bool bSkyAtmosphereTexturesAvailable =
            bSkyAtmosphereRequested && context.SkyAtmosphere.bValid &&
            context.SkyAtmosphere.RadianceTexture &&
            context.SkyAtmosphere.TransmittanceTexture &&
            context.SkyAtmosphere.SunDiskTexture &&
            context.SkyAtmosphere.Sampler;
        bool bSkyAtmosphereIblAvailable = false;
        if (bSkyAtmosphereTexturesAvailable)
        {
            bSkyAtmosphereIblAvailable = EnsureSkyAtmosphereIbl(
                context.SkyAtmosphere.Parameters,
                context.SkyAtmosphere.RadianceTexture->GetWidth(),
                context.SkyAtmosphere.RadianceTexture->GetHeight());
        }
        const bool bSkyAtmosphereAvailable =
            bSkyAtmosphereTexturesAvailable && bSkyAtmosphereIblAvailable;
        if (bSkyAtmosphereRequested)
        {
            const Math::Vector3 sunDirection =
                MakeSunDirectionFromAltitudeAzimuth(
                    context.SkyAtmosphere.Parameters.SunAltitudeDegrees,
                    context.SkyAtmosphere.Parameters.SunAzimuthDegrees);
            constexpr float kPi = 3.14159265358979323846f;
            const float solarDiskAngularRadius =
                std::sqrt(SolarDiskSolidAngleSteradians / kPi);
            params.skySunDirectionAndCosRadius[0] = sunDirection.x;
            params.skySunDirectionAndCosRadius[1] = sunDirection.y;
            params.skySunDirectionAndCosRadius[2] = sunDirection.z;
            params.skySunDirectionAndCosRadius[3] =
                std::cos(solarDiskAngularRadius);
        }
        const bool bValidationPbr = params.debugViewMode == 254u;
        params.bIBLEnabled = (!bValidationRaw251 &&
                             (m_bIBLAvailable || bValidationConstantIblAvailable)) ? 1u : 0u;
        if (bSkyAtmosphereRequested && !bValidationRaw251)
        {
            // 空が有効なフレームでは静的HDRへ暗黙に戻さない。生成失敗時は
            // bIBLEnabled=0としてシェーダー側を黒へ固定する。
            params.bIBLEnabled = bSkyAtmosphereAvailable ? 1u : 0u;
        }
        if (bValidationPbr)
        {
            params.bIBLEnabled = 0u;
        }

        // IBL有効時はambientColor.wにIBL強度を設定
        if (bValidationConstantIblAvailable)
        {
            params.ambientColor[3] = 1.0f;
        }
        else if (m_bIBLAvailable)
        {
            params.ambientColor[3] = m_Settings.IBLIntensity;
        }
        if (bSkyAtmosphereAvailable && !bValidationRaw251)
        {
            params.ambientColor[3] = m_Settings.IBLIntensity;
        }

        if (!m_LightDataBuffer || !m_LightArrayBuffer)
        {
            return false;
        }

        if (outParams != nullptr)
        {
            *outParams = params;
        }
        else
        {
            m_LightDataBuffer->Update(&params, sizeof(GPULightingParams));
        }
        if (lightCount > 0)
        {
            m_LightArrayBuffer->Update(lightArray.data(), sizeof(GPULightData) * lightCount);
        }

        const RHI::TexturePtr& environmentRadiance =
            bValidationRaw252 && m_ValidationRaw252EnvironmentTexture ?
                m_ValidationRaw252EnvironmentTexture :
            bValidationRaw250 && m_ValidationRaw250EnvironmentTexture ?
                m_ValidationRaw250EnvironmentTexture :
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.RadianceTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_EnvironmentTexture ? m_EnvironmentTexture : m_DefaultBlackTexture;
        const RHI::SamplerPtr& environmentRadianceSampler =
            bSkyAtmosphereAvailable ? context.SkyAtmosphere.Sampler : m_IBLSampler;
        const RHI::TexturePtr& diffuseIrradiance =
            bValidationRaw252 && m_ValidationRaw252DiffuseIrradianceTexture ?
                m_ValidationRaw252DiffuseIrradianceTexture :
            bValidationRaw250 && m_ValidationRaw250DiffuseIrradianceTexture ?
                m_ValidationRaw250DiffuseIrradianceTexture :
            bSkyAtmosphereAvailable && m_SkyAtmosphereDiffuseIrradianceTexture ?
                m_SkyAtmosphereDiffuseIrradianceTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_DiffuseIrradianceTexture ?
                m_DiffuseIrradianceTexture : m_DefaultBlackTexture;
        const RHI::TexturePtr& prefilteredSpecular =
            bValidationRaw252 && m_ValidationRaw252PrefilteredSpecularTexture ?
                m_ValidationRaw252PrefilteredSpecularTexture :
            bValidationRaw250 && m_ValidationRaw250Texture ? m_ValidationRaw250Texture :
            bSkyAtmosphereAvailable && m_SkyAtmospherePrefilteredSpecularTexture ?
                m_SkyAtmospherePrefilteredSpecularTexture :
            bSkyAtmosphereRequested ? m_DefaultBlackTexture :
            m_bIBLAvailable && m_PrefilteredSpecularTexture ? m_PrefilteredSpecularTexture :
            m_DefaultBlackTexture;
        if (m_bInitialized && context.PhysicalLighting.bActive)
        {
            context.PhysicalLighting.PublishLighting(
                m_LightArrayBuffer,
                lightCount,
                GetLightArrayBufferSizeBytes(),
                environmentRadiance,
                environmentRadianceSampler,
                diffuseIrradiance,
                m_DiffuseIrradianceSampler,
                prefilteredSpecular,
                m_PrefilteredSpecularSampler,
                m_BrdfLutTexture,
                m_DfgSampler,
                9u,
                bValidationConstantIblAvailable ? 1.0f : m_Settings.IBLIntensity,
                params.bIBLEnabled != 0u);
        }
        return true;
    }

    // ========================================
    // HDR環境マップロード（ミップマップ付きRGBA16_FLOAT）
    // ========================================
    bool LightingPass::LoadEnvironmentMap(const Container::String &path)
    {
        if (path.empty())
        {
            NORVES_LOG_WARNING("LightingPass", "No environment map path specified");
            return false;
        }

        Container::String resolvedPath = path;
#ifdef NORVES_ASSET_DIR
        if (path.size() > 0 && path[0] != '/' && path[0] != '\\' &&
            (path.size() < 2 || path[1] != ':'))
        {
            Container::String relativePath = path;
            if (relativePath.size() > 7)
            {
                Container::String prefix = relativePath.substr(0, 7);
                if (prefix == "Assets/" || prefix == "Assets\\")
                {
                    relativePath = relativePath.substr(7);
                }
            }
            resolvedPath = Container::String(NORVES_ASSET_DIR) + "/" + relativePath;
        }
#endif

        NORVES_LOG_INFO("LightingPass", "Loading HDR environment map...");
        NORVES_LOG_INFO("LightingPass", resolvedPath.c_str());

        int width = 0;
        int height = 0;
        int channels = 0;
        float* hdrData = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, 4);
        if (hdrData == nullptr)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to load HDR environment map");
            return false;
        }
        if (width <= 0 || height <= 0)
        {
            stbi_image_free(hdrData);
            NORVES_LOG_ERROR("LightingPass", "HDR environment map has invalid dimensions");
            return false;
        }

        const double luminanceScale =
            static_cast<double>(m_Settings.EnvironmentLuminanceScaleNits);
        if (!std::isfinite(luminanceScale) || luminanceScale < 0.0)
        {
            stbi_image_free(hdrData);
            NORVES_LOG_ERROR("LightingPass", "HDR environment map scale is invalid");
            return false;
        }

        const uint32_t sourceWidth = static_cast<uint32_t>(width);
        const uint32_t sourceHeight = static_cast<uint32_t>(height);
        const size_t sourcePixelCount = static_cast<size_t>(sourceWidth) * sourceHeight;
        Container::VariableArray<float> sourceData(sourcePixelCount * 4u);
        bool bSourceValid = true;
        for (size_t pixel = 0u; pixel < sourcePixelCount && bSourceValid; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float fileValue = hdrData[pixel * 4u + channel];
                float scaledValue = 0.0f;
                if (!TryScaleSourceValue(fileValue, luminanceScale, scaledValue))
                {
                    bSourceValid = false;
                    break;
                }
                sourceData[pixel * 4u + channel] = scaledValue;
            }
            sourceData[pixel * 4u + 3u] = 1.0f;
        }
        stbi_image_free(hdrData);

        if (!bSourceValid)
        {
            NORVES_LOG_ERROR("LightingPass",
                             "HDR environment source contains a negative, non-finite, or out-of-range RGB value");
            return false;
        }

        RHI::TexturePtr environmentTexture;
        RHI::TexturePtr diffuseTexture;
        RHI::TexturePtr prefilterTexture;
        if (!CreateIblResources(m_Device,
                                sourceData,
                                sourceWidth,
                                sourceHeight,
                                "EnvironmentMap",
                                "DiffuseIrradiance",
                                "PrefilteredSpecular",
                                environmentTexture,
                                diffuseTexture,
                                prefilterTexture))
        {
            NORVES_LOG_ERROR("LightingPass",
                             "Failed to create environment source or derived IBL resources");
            return false;
        }

        m_EnvironmentTexture = environmentTexture;
        m_DiffuseIrradianceTexture = diffuseTexture;
        m_PrefilteredSpecularTexture = prefilterTexture;
        m_EnvironmentMipLevels = 1u;
        NORVES_LOG_INFO("LightingPass", "Environment source and derived IBL resources created");
        return true;
    }

    bool LightingPass::GenerateValidationSnapshots()
    {
        constexpr uint32_t width = 256u;
        constexpr uint32_t height = 128u;
        constexpr double pi = 3.14159265358979323846;
        const size_t pixelCount = static_cast<size_t>(width) * height;

        Container::VariableArray<float> nonconstantSource(pixelCount * 4u);
        Container::VariableArray<float> constantSource(pixelCount * 4u);
        for (uint32_t y = 0u; y < height; ++y)
        {
            const double theta = pi * (static_cast<double>(y) + 0.5) / height;
            const double sinTheta = std::sin(theta);
            const double cosTheta = std::cos(theta);
            for (uint32_t x = 0u; x < width; ++x)
            {
                const double phi = 2.0 * pi *
                    ((static_cast<double>(x) + 0.5) / width - 0.5);
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4u;
                nonconstantSource[offset + 0u] =
                    static_cast<float>(64.0 + 16.0 * sinTheta * std::cos(phi));
                nonconstantSource[offset + 1u] =
                    static_cast<float>(64.0 + 16.0 * cosTheta);
                nonconstantSource[offset + 2u] =
                    static_cast<float>(64.0 + 16.0 * sinTheta * std::sin(phi));
                nonconstantSource[offset + 3u] = 1.0f;
                constantSource[offset + 0u] = 100.0f;
                constantSource[offset + 1u] = 100.0f;
                constantSource[offset + 2u] = 100.0f;
                constantSource[offset + 3u] = 1.0f;
            }
        }

        RHI::TexturePtr nonconstantEnvironment;
        RHI::TexturePtr nonconstantDiffuse;
        RHI::TexturePtr nonconstantPrefilter;
        if (!CreateIblResources(m_Device,
                                nonconstantSource,
                                width,
                                height,
                                "ValidationRaw250Environment",
                                "ValidationRaw250Diffuse",
                                "ValidationRaw250Prefilter",
                                nonconstantEnvironment,
                                nonconstantDiffuse,
                                nonconstantPrefilter))
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create raw250 validation snapshots");
            return false;
        }

        RHI::TexturePtr constantEnvironment;
        RHI::TexturePtr constantDiffuse;
        RHI::TexturePtr constantPrefilter;
        if (!CreateIblResources(m_Device,
                                constantSource,
                                width,
                                height,
                                "ValidationRaw252Environment",
                                "ValidationRaw252Diffuse",
                                "ValidationRaw252Prefilter",
                                constantEnvironment,
                                constantDiffuse,
                                constantPrefilter))
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create raw252 validation snapshots");
            return false;
        }

        m_ValidationRaw250EnvironmentTexture = nonconstantEnvironment;
        m_ValidationRaw250DiffuseIrradianceTexture = nonconstantDiffuse;
        m_ValidationRaw250Texture = nonconstantPrefilter;
        m_ValidationRaw252EnvironmentTexture = constantEnvironment;
        m_ValidationRaw252DiffuseIrradianceTexture = constantDiffuse;
        m_ValidationRaw252PrefilteredSpecularTexture = constantPrefilter;
        return true;
    }

    // BRDF LUT CPU生成（split-sum近似）
    // ========================================
    bool LightingPass::GenerateBRDFLut()
    {
        constexpr uint32_t LUT_SIZE = 256;
        constexpr uint32_t SAMPLE_COUNT = 4096;
        constexpr float PI = 3.14159265359f;

        NORVES_LOG_INFO("LightingPass", "Generating BRDF LUT...");

        // RG16_FLOAT LUT
        Container::VariableArray<uint16_t> lutData(static_cast<size_t>(LUT_SIZE) * LUT_SIZE * 2, 0);
        Container::VariableArray<float> halfVectors(SAMPLE_COUNT * 3, 0.0f);

        for (uint32_t y = 0; y < LUT_SIZE; ++y)
        {
            const float roughness = (static_cast<float>(y) + 0.5f) /
                                    static_cast<float>(LUT_SIZE);

            for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
            {
                // Hammersley sequence
                float u = static_cast<float>(i) / static_cast<float>(SAMPLE_COUNT);
                uint32_t bits = i;
                bits = (bits << 16u) | (bits >> 16u);
                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                float v = static_cast<float>(bits) * 2.3283064365386963e-10f;

                // ImportanceSample GGX
                float a = roughness * roughness;
                float phi = 2.0f * PI * u;
                float cosTheta = std::sqrt((1.0f - v) / (1.0f + (a * a - 1.0f) * v));
                float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

                halfVectors[i * 3] = sinTheta * std::cos(phi);
                halfVectors[i * 3 + 1] = sinTheta * std::sin(phi);
                halfVectors[i * 3 + 2] = cosTheta;
            }

            for (uint32_t x = 0; x < LUT_SIZE; ++x)
            {
                const float NdotV = (static_cast<float>(x) + 0.5f) /
                                    static_cast<float>(LUT_SIZE);

                // V vector in tangent space (N = (0,0,1))
                float Vx = std::sqrt(1.0f - NdotV * NdotV);
                float Vy = 0.0f;
                float Vz = NdotV;

                float A = 0.0f;
                float B = 0.0f;

                for (uint32_t i = 0; i < SAMPLE_COUNT; ++i)
                {
                    const float Hx = halfVectors[i * 3];
                    const float Hy = halfVectors[i * 3 + 1];
                    const float Hz = halfVectors[i * 3 + 2];

                    // Reflect V around H to get L
                    float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
                    float Lx = 2.0f * VdotH * Hx - Vx;
                    float Ly = 2.0f * VdotH * Hy - Vy;
                    float Lz = 2.0f * VdotH * Hz - Vz;

                    float NdotL = (std::max)(Lz, 0.0f);
                    float NdotH = (std::max)(Hz, 0.0f);
                    VdotH = (std::max)(VdotH, 0.0f);

                    if (NdotL > 0.0f)
                    {
                        // Smith GGX for IBL: k = roughness^2 / 2
                        float k = (roughness * roughness) / 2.0f;
                        float G_V = NdotV / (NdotV * (1.0f - k) + k);
                        float G_L = NdotL / (NdotL * (1.0f - k) + k);
                        float G = G_V * G_L;

                        float G_Vis = (G * VdotH) / (NdotH * NdotV + 0.0001f);
                        float Fc = std::pow(1.0f - VdotH, 5.0f);

                        A += (1.0f - Fc) * G_Vis;
                        B += Fc * G_Vis;
                    }
                }

                A /= static_cast<float>(SAMPLE_COUNT);
                B /= static_cast<float>(SAMPLE_COUNT);

                // Clamp to valid range
                A = (std::max)(0.0f, (std::min)(1.0f, A));
                B = (std::max)(0.0f, (std::min)(1.0f, B));

                size_t idx = (static_cast<size_t>(y) * LUT_SIZE + x) * 2;
                lutData[idx + 0] = FloatToHalfRne(A);
                lutData[idx + 1] = FloatToHalfRne(B);
            }
        }

        // テクスチャ作成（R16G16_FLOAT）
        RHI::TextureDesc lutDesc;
        lutDesc.Width = LUT_SIZE;
        lutDesc.Height = LUT_SIZE;
        lutDesc.MipLevels = 1;
        lutDesc.TextureFormat = RHI::Format::R16G16_FLOAT;
        lutDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        lutDesc.DebugName = "BRDF_LUT";

        m_BrdfLutTexture = m_Device->CreateTexture(lutDesc);
        if (!m_BrdfLutTexture)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create BRDF LUT texture");
            return false;
        }

        uint32_t rowPitch = LUT_SIZE * 2 * static_cast<uint32_t>(sizeof(uint16_t));
        uint32_t slicePitch = rowPitch * LUT_SIZE;
        m_BrdfLutTexture->Update(lutData.data(), rowPitch, slicePitch);

        NORVES_LOG_INFO("LightingPass", "BRDF LUT generated");
        return true;
    }

} // namespace NorvesLib::Core::Rendering
