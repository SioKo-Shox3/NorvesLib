// 正距円筒の放射輝度から IBL 用の資源を生成する。
#include "Rendering/IblGeneration.h"

#include "Rendering/DfgLut.h"
#include "Rendering/EnvironmentMapSource.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/PaleFlatShading.h"
#include "Rendering/ShaderManager.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
namespace
{
    struct alignas(16) SkyCloudBlendConstants
    {
        float ClearWeights[4] = {};
        float OvercastWeight[4] = {};
        uint32_t Target[4] = {};
    };
    static_assert(sizeof(SkyCloudBlendConstants) == 48u);

    RHI::DescriptorSetDesc MakeSkyCloudBlendLayout()
    {
        const RHI::ResourceBindType types[] = {
            RHI::ResourceBindType::ConstantBuffer,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::CombinedImageSampler,
            RHI::ResourceBindType::RWTexture};
        RHI::DescriptorSetDesc desc;
        for (uint32_t binding = 0u; binding < 5u; ++binding)
        {
            RHI::DescriptorBinding entry;
            entry.binding = binding;
            entry.type = types[binding];
            entry.stages = RHI::ShaderStage::Compute;
            desc.bindings.push_back(entry);
        }
        return desc;
    }

    bool IsSkyCloudBlendSampledTexture(const RHI::TexturePtr& texture)
    {
        return texture && texture->GetWidth() != 0u && texture->GetHeight() != 0u &&
            texture->GetDepth() == 1u && texture->GetArraySize() == 1u &&
            !texture->IsCubemap() && texture->GetMipLevels() != 0u &&
            (texture->GetUsage() & RHI::ResourceUsage::ShaderRead) != RHI::ResourceUsage::None;
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
                if (!TryScaleEnvironmentSourceValue(value, 1.0, validatedValue))
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

}

    RHI::TexturePtr CreateSkyCloudBlendOutputTexture(RHI::IDevice* device,
                                                     uint32_t width,
                                                     uint32_t height,
                                                     uint32_t mipLevels,
                                                     const char* name)
    {
        constexpr uint32_t bytesPerPixel = 4u * sizeof(uint16_t);
        constexpr uint32_t maxPitch = (std::numeric_limits<uint32_t>::max)();
        if (!device || width == 0u || height == 0u || mipLevels == 0u ||
            width > maxPitch / bytesPerPixel || height > maxPitch / (width * bytesPerPixel))
        {
            return nullptr;
        }

        uint32_t maxMipLevels = 0u;
        for (uint32_t longest = (std::max)(width, height); longest != 0u; longest >>= 1u)
        {
            ++maxMipLevels;
        }
        if (mipLevels > maxMipLevels)
        {
            return nullptr;
        }

        try
        {
            RHI::TextureDesc desc;
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = mipLevels;
            desc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
            desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::UnorderedAccess |
                RHI::ResourceUsage::TransferDst;
            desc.DebugName = name;
            RHI::TexturePtr texture = device->CreateTexture(desc);
            if (!texture)
            {
                return nullptr;
            }

            const uint32_t baseSlicePitch = width * bytesPerPixel * height;
            const Container::VariableArray<uint16_t> zeroData(baseSlicePitch / sizeof(uint16_t), 0u);
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            for (uint32_t mip = 0u; mip < mipLevels; ++mip)
            {
                const uint32_t rowPitch = mipWidth * bytesPerPixel;
                texture->Update(zeroData.data(), rowPitch, rowPitch * mipHeight, mip);
                mipWidth = (std::max)(1u, mipWidth >> 1u);
                mipHeight = (std::max)(1u, mipHeight >> 1u);
            }
            return texture;
        }
        catch (const std::exception&)
        {
            return nullptr;
        }
    }

    bool SkyCloudBlendRecorder::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (device && shaderManager)
        {
            try
            {
                const RHI::DescriptorSetDesc layout = MakeSkyCloudBlendLayout();
                m_Shader = shaderManager->LoadShader("Sky/SkyCloudBlend.comp", RHI::ShaderStage::Compute);
                m_DescriptorSetLayout = device->GetOrCreateDescriptorSetLayout(layout);
                if (m_Shader && m_DescriptorSetLayout)
                {
                    RHI::ComputePipelineDesc pipelineDesc;
                    pipelineDesc.computeShader = m_Shader;
                    pipelineDesc.descriptorSetLayouts.push_back(layout);
                    m_Pipeline = device->CreateComputePipeline(pipelineDesc);

                    RHI::SamplerDesc samplerDesc;
                    samplerDesc.filterMin = RHI::FilterMode::Point;
                    samplerDesc.filterMag = RHI::FilterMode::Point;
                    samplerDesc.filterMip = RHI::FilterMode::Point;
                    samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
                    samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
                    samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
                    m_Sampler = device->CreateSampler(samplerDesc);
                }
            }
            catch (const std::exception&)
            {
                Shutdown();
            }
        }
        if (!IsReady())
        {
            NORVES_LOG_WARNING("SkyCloudBlend", "空と IBL の混ぜの初期化に失敗しました");
            Shutdown();
            return false;
        }
        return true;
    }

    void SkyCloudBlendRecorder::Shutdown()
    {
        m_Dispatches.clear();
        m_Outputs.clear();
        m_Sampler.reset();
        m_Pipeline.reset();
        m_DescriptorSetLayout.reset();
        m_Shader.reset();
    }

    bool SkyCloudBlendRecorder::IsReady() const
    {
        return m_Shader && m_DescriptorSetLayout && m_Pipeline && m_Sampler;
    }

    bool SkyCloudBlendRecorder::Prepare(FrameResourceLane& lane,
                                        const SkyCloudBlendTarget* targets,
                                        size_t targetCount,
                                        const SkyCloudBlendInputWeights& weights)
    {
        m_Dispatches.clear();
        m_Outputs.clear();
        if (!IsReady() || (targetCount != 0u && !targets) ||
            !std::isfinite(weights.ClearWeightA) || !std::isfinite(weights.ClearWeightB) ||
            !std::isfinite(weights.OvercastWeight.x) || !std::isfinite(weights.OvercastWeight.y) ||
            !std::isfinite(weights.OvercastWeight.z) || !std::isfinite(weights.ShiftA) ||
            !std::isfinite(weights.ShiftB))
        {
            return false;
        }

        const auto normalizeShift = [](float shift)
        {
            const float normalized = shift - std::floor(shift);
            // 負の微小値で差が 1 に丸められた場合も、周期の始点へ戻す。
            return normalized < 1.0f ? normalized : 0.0f;
        };
        const float shiftA = normalizeShift(weights.ShiftA);
        const float shiftB = normalizeShift(weights.ShiftB);
        const auto fail = [this]()
        {
            m_Dispatches.clear();
            m_Outputs.clear();
            return false;
        };
        try
        {
            for (size_t index = 0u; index < targetCount; ++index)
            {
                const SkyCloudBlendTarget& target = targets[index];
                if (!IsSkyCloudBlendSampledTexture(target.ClearA) ||
                    !IsSkyCloudBlendSampledTexture(target.ClearB) ||
                    !IsSkyCloudBlendSampledTexture(target.UnitOvercast) ||
                    !IsSkyCloudBlendSampledTexture(target.Output) ||
                    target.Output->GetFormat() != RHI::Format::R16G16B16A16_FLOAT ||
                    (target.Output->GetUsage() & RHI::ResourceUsage::UnorderedAccess) == RHI::ResourceUsage::None ||
                    target.ClearA->GetWidth() != target.ClearB->GetWidth() ||
                    target.ClearA->GetHeight() != target.ClearB->GetHeight() ||
                    target.ClearA->GetMipLevels() != target.ClearB->GetMipLevels() ||
                    target.ClearA->GetWidth() != target.UnitOvercast->GetWidth() ||
                    target.ClearA->GetHeight() != target.UnitOvercast->GetHeight() ||
                    target.ClearA->GetWidth() != target.Output->GetWidth() ||
                    target.ClearA->GetHeight() != target.Output->GetHeight() ||
                    target.UnitOvercast->GetMipLevels() < target.ClearA->GetMipLevels() ||
                    target.Output->GetMipLevels() != target.ClearA->GetMipLevels())
                {
                    return fail();
                }
                for (size_t other = 0u; other < targetCount; ++other)
                {
                    if (target.Output == targets[other].ClearA ||
                        target.Output == targets[other].ClearB ||
                        target.Output == targets[other].UnitOvercast ||
                        (other < index && target.Output == targets[other].Output))
                    {
                        return fail();
                    }
                }
            }

            const bool bCopyClear = weights.ClearWeightA == 1.0f && weights.ClearWeightB == 0.0f &&
                weights.OvercastWeight.x == 0.0f && weights.OvercastWeight.y == 0.0f &&
                weights.OvercastWeight.z == 0.0f && shiftA == 0.0f;
            for (size_t index = 0u; index < targetCount; ++index)
            {
                const SkyCloudBlendTarget& target = targets[index];
                uint32_t width = target.Output->GetWidth();
                uint32_t height = target.Output->GetHeight();
                for (uint32_t mip = 0u; mip < target.Output->GetMipLevels(); ++mip)
                {
                    SkyCloudBlendConstants constants;
                    constants.ClearWeights[0] = weights.ClearWeightA;
                    constants.ClearWeights[1] = weights.ClearWeightB;
                    constants.ClearWeights[2] = shiftA;
                    constants.ClearWeights[3] = shiftB;
                    constants.OvercastWeight[0] = weights.OvercastWeight.x;
                    constants.OvercastWeight[1] = weights.OvercastWeight.y;
                    constants.OvercastWeight[2] = weights.OvercastWeight.z;
                    constants.Target[0] = width;
                    constants.Target[1] = height;
                    constants.Target[2] = mip;
                    constants.Target[3] = bCopyClear ? 1u : 0u;

                    const FrameUploadAllocation uniform = lane.AllocateConstants(constants);
                    const FrameDescriptorAllocation descriptor = lane.AllocateDescriptorSet(m_DescriptorSetLayout);
                    if (!uniform.IsValid() || !descriptor.IsValid())
                    {
                        return fail();
                    }
                    const RHI::DescriptorSetPtr& set = descriptor.Set;
                    set->BindConstantBuffer(0u, uniform.Buffer, uniform.GetOffset32(), uniform.GetSize32());
                    set->BindTexture(1u, target.ClearA);
                    set->BindSampler(1u, m_Sampler);
                    set->BindTexture(2u, target.ClearB);
                    set->BindSampler(2u, m_Sampler);
                    set->BindTexture(3u, target.UnitOvercast);
                    set->BindSampler(3u, m_Sampler);
                    set->BindStorageTexture(4u, target.Output, mip);
                    set->Update();
                    m_Dispatches.push_back({set, width, height});
                    width = (std::max)(1u, width >> 1u);
                    height = (std::max)(1u, height >> 1u);
                }
                m_Outputs.push_back(target.Output);
            }
        }
        catch (const std::exception&)
        {
            return fail();
        }
        return true;
    }

    bool SkyCloudBlendRecorder::HasPrepared() const
    {
        return !m_Dispatches.empty();
    }

    void SkyCloudBlendRecorder::Record(RHI::ICommandList& commands)
    {
        if (m_Dispatches.empty())
        {
            return;
        }
        commands.SetPipeline(m_Pipeline);
        for (const PreparedDispatch& dispatch : m_Dispatches)
        {
            commands.SetDescriptorSet(dispatch.DescriptorSet, 0u);
            const uint32_t groupsX = dispatch.Width / 8u + (dispatch.Width % 8u != 0u ? 1u : 0u);
            const uint32_t groupsY = dispatch.Height / 8u + (dispatch.Height % 8u != 0u ? 1u : 0u);
            commands.Dispatch(groupsX, groupsY, 1u);
        }
        for (const RHI::TexturePtr& output : m_Outputs)
        {
            commands.TextureBarrier(output, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
        }
        m_Dispatches.clear();
        m_Outputs.clear();
    }

    bool ComputeIblCpuData(const Container::VariableArray<float>& sourceData,
                           uint32_t sourceWidth,
                           uint32_t sourceHeight,
                           IblCpuData& outData)
    {
        outData = {};
        try
        {
            constexpr uint32_t workingWidth = IblWorkingWidth;
            constexpr uint32_t workingHeight = IblWorkingHeight;
            constexpr uint32_t diffuseWidth = IblDiffuseWidth;
            constexpr uint32_t diffuseHeight = IblDiffuseHeight;
            constexpr uint32_t prefilterMipCount = IblPrefilterMipCount;
            constexpr uint32_t prefilterSamples = 1024u;
            constexpr double pi = 3.14159265358979323846;

            if (!ValidateCanonicalSource(sourceData, sourceWidth, sourceHeight))
            {
                return false;
            }

            IblCpuData data;
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
                                     "IBL %s の半精度化後の配列サイズが不正です",
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

            double averageIrradiance[3] = {};
            if (!ComputeEquirectangularAverageIrradiance(
                    workingData.data(), workingWidth, workingHeight, averageIrradiance))
            {
                return false;
            }
            if (bConstantSource)
            {
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double value = averageIrradiance[channel];
                    const double expected = diffuseExpectedRgb[channel];
                    const double relative = expected == 0.0 ? 0.0 :
                        std::abs(value - expected) / std::abs(expected);
                    if ((expected == 0.0 && value != 0.0) ||
                        !std::isfinite(value) || !std::isfinite(relative) || relative > 1.0e-3)
                    {
                        NORVES_LOG_ERROR("LightingPass",
                                         "IBL の全球の平均照度が一定の入力の πL と一致しません: channel=%u value=%g expected=%g",
                                         channel, value, expected);
                        return false;
                    }
                }
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
            if (!validateGeneratedRgb(diffuseHalf,
                                      static_cast<size_t>(diffuseWidth) * diffuseHeight,
                                      diffuseExpectedRgb,
                                      bConstantSource,
                                      "diffuse"))
            {
                return false;
            }

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
            if (!validateGeneratedRgb(mip0Half,
                                      workingPixelCount,
                                      sourceExpectedRgb,
                                      bConstantSource,
                                      "prefilter_mip0"))
            {
                return false;
            }

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
                if (!validateGeneratedRgb(prefilteredHalf,
                                          static_cast<size_t>(mipWidth) * mipHeight,
                                          sourceExpectedRgb,
                                          bConstantSource,
                                          "prefilter_mip"))
                {
                    return false;
                }
                data.PrefilterMips[mip] = std::move(prefilteredHalf);
            }

            data.Environment = std::move(sourceHalf);
            data.EnvironmentWidth = sourceWidth;
            data.EnvironmentHeight = sourceHeight;
            data.Diffuse = std::move(diffuseHalf);
            data.PrefilterMips[0] = std::move(mip0Half);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                data.AverageIrradiance[channel] = static_cast<float>(averageIrradiance[channel]);
            }
            data.bValid = true;
            outData = std::move(data);
            return true;
        }
        catch (const std::exception&)
        {
            outData = {};
            return false;
        }
    }

    bool UploadIblResources(RHI::IDevice* device,
                            const IblCpuData& data,
                            const char* environmentName,
                            const char* diffuseName,
                            const char* prefilterName,
                            RHI::TexturePtr& outEnvironment,
                            RHI::TexturePtr& outDiffuse,
                            RHI::TexturePtr& outPrefilter,
                            float outAverageIrradiance[3])
    {
        std::fill_n(outAverageIrradiance, 3u, 0.0f);
        outEnvironment.reset();
        outDiffuse.reset();
        outPrefilter.reset();

        constexpr uint32_t bytesPerPixel = 4u * static_cast<uint32_t>(sizeof(uint16_t));
        constexpr uint32_t maxPitch = (std::numeric_limits<uint32_t>::max)();
        if (!device || !data.bValid || data.EnvironmentWidth == 0u ||
            data.EnvironmentHeight == 0u || data.EnvironmentWidth > maxPitch / bytesPerPixel ||
            data.EnvironmentHeight > maxPitch / (data.EnvironmentWidth * bytesPerPixel) ||
            data.Environment.size() != static_cast<size_t>(data.EnvironmentWidth) *
                data.EnvironmentHeight * 4u ||
            data.Diffuse.size() != static_cast<size_t>(IblDiffuseWidth) * IblDiffuseHeight * 4u)
        {
            return false;
        }
        for (uint32_t mip = 0u; mip < IblPrefilterMipCount; ++mip)
        {
            const uint32_t mipWidth = (std::max)(1u, IblWorkingWidth >> mip);
            const uint32_t mipHeight = (std::max)(1u, IblWorkingHeight >> mip);
            if (data.PrefilterMips[mip].size() != static_cast<size_t>(mipWidth) * mipHeight * 4u)
            {
                return false;
            }
        }

        try
        {
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

            RHI::TexturePtr environmentTexture =
                createRgbaTexture(data.EnvironmentWidth, data.EnvironmentHeight, 1u, environmentName);
            if (!environmentTexture)
            {
                return false;
            }
            const uint32_t sourceRowPitch = data.EnvironmentWidth * 4u *
                static_cast<uint32_t>(sizeof(uint16_t));
            environmentTexture->Update(data.Environment.data(), sourceRowPitch,
                                        sourceRowPitch * data.EnvironmentHeight);

            RHI::TexturePtr diffuseTexture =
                createRgbaTexture(IblDiffuseWidth, IblDiffuseHeight, 1u, diffuseName);
            RHI::TexturePtr prefilterTexture =
                createRgbaTexture(IblWorkingWidth, IblWorkingHeight, IblPrefilterMipCount, prefilterName);
            if (!diffuseTexture || !prefilterTexture)
            {
                return false;
            }
            const uint32_t diffuseRowPitch =
                IblDiffuseWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
            diffuseTexture->Update(data.Diffuse.data(), diffuseRowPitch,
                                    diffuseRowPitch * IblDiffuseHeight);
            for (uint32_t mip = 0u; mip < IblPrefilterMipCount; ++mip)
            {
                const uint32_t mipWidth = (std::max)(1u, IblWorkingWidth >> mip);
                const uint32_t mipHeight = (std::max)(1u, IblWorkingHeight >> mip);
                const uint32_t rowPitch =
                    mipWidth * 4u * static_cast<uint32_t>(sizeof(uint16_t));
                prefilterTexture->Update(data.PrefilterMips[mip].data(), rowPitch,
                                         rowPitch * mipHeight, mip);
            }

            outEnvironment = environmentTexture;
            outDiffuse = diffuseTexture;
            outPrefilter = prefilterTexture;
            std::copy_n(data.AverageIrradiance, 3u, outAverageIrradiance);
            return true;
        }
        catch (const std::exception&)
        {
            outEnvironment.reset();
            outDiffuse.reset();
            outPrefilter.reset();
            std::fill_n(outAverageIrradiance, 3u, 0.0f);
            return false;
        }
    }

    bool CreateIblResources(RHI::IDevice* device,
                                   const Container::VariableArray<float>& sourceData,
                                   uint32_t sourceWidth,
                                   uint32_t sourceHeight,
                                   const char* environmentName,
                                   const char* diffuseName,
                                   const char* prefilterName,
                                   RHI::TexturePtr& outEnvironment,
                                   RHI::TexturePtr& outDiffuse,
                                   RHI::TexturePtr& outPrefilter,
                                   float outAverageIrradiance[3])
    {
        IblCpuData data;
        ComputeIblCpuData(sourceData, sourceWidth, sourceHeight, data);
        return UploadIblResources(device, data, environmentName, diffuseName, prefilterName,
                                  outEnvironment, outDiffuse, outPrefilter, outAverageIrradiance);
    }

}
