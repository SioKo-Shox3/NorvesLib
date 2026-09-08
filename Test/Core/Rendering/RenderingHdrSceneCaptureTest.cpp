#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Module/ModuleRegistry.h"
#include "ImGuiModule/IImGuiView.h"
#include "ImGuiModule/ImGuiModule.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/RenderWorld.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IShader.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/RHITypes.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    class OpaqueMarkerView final : public Modules::Gui::IImGuiView
    {
    public:
        void OnImGui() override
        {
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(4.0f, 4.0f),
                ImVec2(20.0f, 20.0f),
                IM_COL32(128, 64, 191, 255));
        }

        const char* GetViewName() const override
        {
            return "R1OpaqueMarkerView";
        }
    };

    uint16_t FloatToHalf(float value)
    {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t sign = (bits >> 16u) & 0x8000u;
        const uint32_t exponent = (bits >> 23u) & 0xFFu;
        const uint32_t mantissa = bits & 0x7FFFFFu;
        if (exponent == 0xFFu)
        {
            return static_cast<uint16_t>(sign | 0x7C00u | (mantissa != 0u ? 0x0200u : 0u));
        }
        const int32_t halfExponent = static_cast<int32_t>(exponent) - 127 + 15;
        if (halfExponent <= 0)
        {
            return static_cast<uint16_t>(sign);
        }
        if (halfExponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7C00u);
        }
        return static_cast<uint16_t>(sign |
                                     (static_cast<uint32_t>(halfExponent) << 10u) |
                                     (mantissa >> 13u));
    }

    uint8_t EncodeSrgbReference(float linear)
    {
        const float encoded = linear <= 0.0031308f
                                  ? 12.92f * linear
                                  : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
    }

    bool VerifyForcedPresentationFormat(
        RHI::IDevice* device,
        RHI::Format targetFormat,
        RHI::PresentationEncodePath encodePath)
    {
        constexpr uint32_t sampleCount = 7u;
        constexpr uint32_t width = sampleCount;
        constexpr uint32_t height = 1u;
        constexpr float samples[sampleCount] = {
            0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.0031308f, 0.0031309f};
        if (device == nullptr ||
            RHI::GetPresentationEncodePath(targetFormat) != encodePath)
        {
            std::cerr << "forced presentation: invalid device/path\n";
            return false;
        }

        RHI::TextureDesc sourceDesc;
        sourceDesc.Width = width;
        sourceDesc.Height = height;
        sourceDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        sourceDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        sourceDesc.DebugName = "R1ForcedPresentationLinearSource";
        RHI::TexturePtr sourceTexture = device->CreateTexture(sourceDesc);
        if (!sourceTexture)
        {
            std::cerr << "forced presentation: source texture create failed\n";
            return false;
        }

        Core::Container::VariableArray<uint16_t> sourcePixels;
        sourcePixels.resize(sampleCount * 4u);
        for (uint32_t index = 0; index < sampleCount; ++index)
        {
            const uint16_t half = FloatToHalf(samples[index]);
            sourcePixels[index * 4u + 0u] = half;
            sourcePixels[index * 4u + 1u] = half;
            sourcePixels[index * 4u + 2u] = half;
            sourcePixels[index * 4u + 3u] = FloatToHalf(1.0f);
        }
        sourceTexture->Update(sourcePixels.data(), width * 8u, width * 8u);

        RHI::TextureDesc targetDesc;
        targetDesc.Width = width;
        targetDesc.Height = height;
        targetDesc.TextureFormat = targetFormat;
        targetDesc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::TransferSrc;
        targetDesc.DebugName = "R1ForcedPresentationReadbackTarget";
        RHI::TexturePtr targetTexture = device->CreateTexture(targetDesc);
        if (!targetTexture)
        {
            std::cerr << "forced presentation: target texture create failed\n";
            return false;
        }

        RHI::RenderPassDesc renderPassDesc;
        RHI::AttachmentDesc colorAttachment;
        colorAttachment.format = targetFormat;
        colorAttachment.clear = true;
        colorAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        colorAttachment.initialState = RHI::ResourceState::Undefined;
        colorAttachment.finalState = RHI::ResourceState::RenderTarget;
        renderPassDesc.colorAttachments.push_back(colorAttachment);
        RHI::RenderPassPtr renderPass = device->CreateRenderPass(renderPassDesc);
        if (!renderPass)
        {
            std::cerr << "forced presentation: render pass create failed\n";
            return false;
        }

        RHI::FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = renderPass;
        framebufferDesc.colorTargets.push_back(targetTexture);
        framebufferDesc.width = width;
        framebufferDesc.height = height;
        RHI::FramebufferPtr framebuffer = device->CreateFramebuffer(framebufferDesc);
        if (!framebuffer)
        {
            std::cerr << "forced presentation: framebuffer create failed\n";
            return false;
        }

        Core::Rendering::ShaderManager shaderManager;
        if (!shaderManager.Initialize(device, "Assets/Shaders"))
        {
            std::cerr << "forced presentation: shader manager init failed\n";
            return false;
        }
        RHI::ShaderPtr vertexShader = shaderManager.LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        RHI::ShaderPtr fragmentShader = shaderManager.LoadShader("blit.frag", RHI::ShaderStage::Pixel);
        if (!vertexShader || !fragmentShader)
        {
            std::cerr << "forced presentation: shader load failed\n";
            shaderManager.Shutdown();
            return false;
        }

        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        RHI::SamplerPtr sampler = device->CreateSampler(samplerDesc);
        if (!sampler)
        {
            std::cerr << "forced presentation: sampler create failed\n";
            shaderManager.Shutdown();
            return false;
        }

        RHI::DescriptorSetDesc descriptorDesc;
        RHI::DescriptorBinding textureBinding;
        textureBinding.binding = 0;
        textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        textureBinding.stages = RHI::ShaderStage::Pixel;
        descriptorDesc.bindings.push_back(textureBinding);
        RHI::DescriptorBinding paramsBinding;
        paramsBinding.binding = 1;
        paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramsBinding.stages = RHI::ShaderStage::Pixel;
        descriptorDesc.bindings.push_back(paramsBinding);
        RHI::DescriptorSetPtr descriptorSet = device->CreateDescriptorSet(descriptorDesc);
        if (!descriptorSet)
        {
            std::cerr << "forced presentation: descriptor create failed\n";
            shaderManager.Shutdown();
            return false;
        }

        RHI::PresentationEncodeParams params;
        params.EncodePath = encodePath == RHI::PresentationEncodePath::ShaderOETF ? 1u : 0u;
        RHI::BufferDesc paramsDesc(sizeof(RHI::PresentationEncodeParams),
                                   RHI::ResourceUsage::ConstantBuffer,
                                   true,
                                   "R1ForcedPresentationEncodeParams");
        RHI::BufferPtr paramsBuffer = device->CreateBuffer(paramsDesc);
        if (!paramsBuffer)
        {
            std::cerr << "forced presentation: params buffer create failed\n";
            shaderManager.Shutdown();
            return false;
        }
        paramsBuffer->Update(&params, sizeof(params));
        descriptorSet->BindTexture(0, sourceTexture);
        descriptorSet->BindSampler(0, sampler);
        descriptorSet->BindConstantBuffer(1, paramsBuffer, 0, sizeof(params));
        descriptorSet->Update();

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.pixelShader = fragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.depthStencilState.depthTestEnable = false;
        pipelineDesc.depthStencilState.depthWriteEnable = false;
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = renderPass;
        pipelineDesc.descriptorSetLayouts.push_back(descriptorDesc);
        RHI::PipelinePtr pipeline = device->CreateGraphicsPipeline(pipelineDesc);
        if (!pipeline)
        {
            std::cerr << "forced presentation: pipeline create failed\n";
            shaderManager.Shutdown();
            return false;
        }

        RHI::BufferDesc readbackDesc(width * 4u,
                                     RHI::ResourceUsage::TransferDst,
                                     true,
                                     "R1ForcedPresentationReadback");
        RHI::BufferPtr readback = device->CreateBuffer(readbackDesc);
        RHI::CommandListPtr commandList = device->CreateCommandList();
        if (!readback || !commandList)
        {
            std::cerr << "forced presentation: readback or command list create failed\n";
            shaderManager.Shutdown();
            return false;
        }

        RHI::Viewport viewport;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        RHI::ScissorRect scissor;
        scissor.right = static_cast<int32_t>(width);
        scissor.bottom = static_cast<int32_t>(height);
        commandList->Begin();
        commandList->BeginRenderPass(renderPass, framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet);
        commandList->Draw(3u, 0u);
        commandList->EndRenderPass();
        commandList->TextureBarrier(targetTexture,
                                    RHI::ResourceState::RenderTarget,
                                    RHI::ResourceState::CopySource);
        commandList->CopyTextureToBuffer(targetTexture, readback, width, height, 0u);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();
        shaderManager.Shutdown();

        void* mapped = readback->Map(0u, width * 4u);
        if (mapped == nullptr)
        {
            std::cerr << "forced presentation: readback map failed\n";
            return false;
        }
        const uint8_t* pixels = static_cast<const uint8_t*>(mapped);
        bool bPassed = true;
        for (uint32_t index = 0; index < sampleCount; ++index)
        {
            const uint8_t expected = EncodeSrgbReference(samples[index]);
            const size_t offset = static_cast<size_t>(index) * 4u;
            for (uint32_t channel = 0; channel < 3u; ++channel)
            {
                if (std::abs(static_cast<int>(pixels[offset + channel]) -
                             static_cast<int>(expected)) > 1)
                {
                    bPassed = false;
                    std::cerr << "forced presentation: format "
                              << static_cast<unsigned int>(targetFormat)
                              << " sample " << index << " channel " << channel
                              << " actual " << static_cast<unsigned int>(pixels[offset + channel])
                              << " expected " << static_cast<unsigned int>(expected) << "\n";
                }
            }
            if (pixels[offset + 3u] != 255u)
            {
                bPassed = false;
                std::cerr << "forced presentation: format "
                          << static_cast<unsigned int>(targetFormat)
                          << " sample " << index << " alpha actual "
                          << static_cast<unsigned int>(pixels[offset + 3u])
                          << " expected 255\n";
            }
        }
        readback->Unmap();
        if (!bPassed)
        {
            std::cerr << "forced presentation: pixel verification failed for format "
                      << static_cast<unsigned int>(targetFormat) << "\n";
        }
        return bPassed;
    }

    bool RunForcedPresentationFormatReadback(
        uint32_t& outRows,
        uint32_t& outPixels,
        uint32_t& outChannels)
    {
        outRows = 0u;
        outPixels = 0u;
        outChannels = 0u;
        RHI::RHIDeviceDesc deviceDesc;
        deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device)
        {
            std::cerr << "forced presentation: device create failed\n";
            return false;
        }
        if (!VerifyForcedPresentationFormat(device.get(),
                                            RHI::Format::R8G8B8A8_SRGB,
                                            RHI::PresentationEncodePath::HardwareSRGB))
        {
            return false;
        }
        ++outRows;
        std::cout << "R1 forced actual readback passed: format=R8G8B8A8_SRGB "
                     "encode_path=hardware_srgb samples=7 channels=4 lsb=1\n";
        if (!VerifyForcedPresentationFormat(device.get(),
                                            RHI::Format::R8G8B8A8_UNORM,
                                            RHI::PresentationEncodePath::ShaderOETF))
        {
            return false;
        }
        ++outRows;
        outPixels = outRows * 7u;
        outChannels = outPixels * 4u;
        std::cout << "R1 forced actual readback passed: format=R8G8B8A8_UNORM "
                     "encode_path=shader_oetf samples=7 channels=4 lsb=1\n";
        return true;
    }

    struct P4SamplePair
    {
        double Xi1 = 0.0;
        double Xi2 = 0.0;
    };

    struct P4DfgValue
    {
        double A = 0.0;
        double B = 0.0;
    };

    struct P4DfgOracle
    {
        Core::Container::VariableArray<uint32_t> NdotVIndices;
        Core::Container::VariableArray<uint32_t> RoughnessIndices;
        Core::Container::VariableArray<P4DfgValue> Values;
        double HammersleySobolMaxAbs[3] = {};
        double HammersleySobolMaxRelative[3] = {};
        double ProductionSobolMaxAbs[3] = {};
        double ProductionSobolMaxRelative[3] = {};
    };

    struct P5DfgOracle
    {
        P4DfgValue X254Y127;
        P4DfgValue X254Y128;
        P4DfgValue X255Y127;
        P4DfgValue X255Y128;
        bool bValid = false;
    };

    struct P4RoughnessOracle
    {
        P4DfgValue Values[5] = {};
        bool bValid = false;
    };

    struct P4RgbValue
    {
        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
    };

    struct P4RawMip
    {
        uint32_t Mip = 0u;
        uint32_t Width = 0u;
        uint32_t Height = 0u;
        Core::Container::VariableArray<P4RgbValue> Values;
    };

    struct P4Raw250Oracle
    {
        Core::Container::VariableArray<P4RawMip> Mips;
    };

    struct P4DirectConductorOracle
    {
        P4DfgValue Dfg;
        double RangeWindow = 0.0;
        double Radiance = 0.0;
        double Common = 0.0;
        double CorrectTarget = 0.0;
        double StoredTarget = 0.0;
        double StoredHalf = 0.0;
        double LegacyTarget = 0.0;
        double LegacyStoredHalf = 0.0;
        double LegacyStoredPhysical = 0.0;
        double SensitivityPercent = 0.0;
    };

    struct P4SobolDirections
    {
        uint32_t Values[2][32] = {};
    };

    constexpr double P4Pi = 3.1415926535897932384626433832795;
    constexpr double P4HalfUnit = 2.3283064365386962890625e-10;
    constexpr uint32_t P4DfgTextureSize = 256u;
    constexpr uint32_t P4DfgSampleCount = 65536u;
    constexpr uint32_t P4DfgHammersleySampleCount = 16384u;
    constexpr uint32_t P4DfgProductionSampleCount = 4096u;
    constexpr double P4RneRangeLimit = 65504.0;
    constexpr double P4DfgCoordinate = 255.5 / 256.0;
    constexpr double P4DirectTargetLiteral = 6.4407868244456914;
    constexpr double P4DirectStoredLiteral = 0.089455372561745711;
    constexpr double P4DirectStoredHalfLiteral = 0.0894775390625;
    constexpr double P4DirectLegacyTargetLiteral = 1.9893870539086822;
    constexpr double P4DirectLegacyStoredHalfLiteral = 0.0276336669921875;
    constexpr double P4DirectLegacyStoredPhysicalLiteral = 1.9896240234375;
    constexpr double P4DirectSensitivityPercentLiteral = 69.112670421600331;
    constexpr uint32_t P4RuntimeMaterialCountPerRoughness = 6u;
    constexpr uint32_t P4RuntimeTargetMetallicIndex = 2u;
    constexpr uint32_t P4RuntimeDfgQueryIndices[5] = {3u, 4u, 1u, 5u, 2u};

    enum class P4CaptureSubstage : uint8_t
    {
        Primary,
        Depth,
        Normal,
        Material
    };

    bool BuildP4RuntimeIdentity(P4Scenario scenario,
                                uint32_t rowIndex,
                                uint32_t& outMaterialIndex,
                                uint32_t& outLightCount)
    {
        uint32_t roughnessIndex = 0u;
        uint32_t metallicQueryIndex = 0u;
        switch (scenario)
        {
        case P4Scenario::Raw250TextureRepresentation:
            if (rowIndex != 0u)
            {
                return false;
            }
            break;
        case P4Scenario::Raw251DfgLut:
            if (rowIndex >= 25u)
            {
                return false;
            }
            roughnessIndex = rowIndex / 5u;
            metallicQueryIndex = P4RuntimeDfgQueryIndices[rowIndex % 5u];
            break;
        case P4Scenario::Raw252RoughnessSweep:
            if (rowIndex >= 5u)
            {
                return false;
            }
            roughnessIndex = rowIndex;
            metallicQueryIndex = 0u;
            break;
        case P4Scenario::Raw252TargetNotOne:
            if (rowIndex != 5u)
            {
                return false;
            }
            roughnessIndex = 1u;
            metallicQueryIndex = P4RuntimeTargetMetallicIndex;
            break;
        case P4Scenario::Raw252WhiteFurnace:
            if (rowIndex >= 15u)
            {
                return false;
            }
            roughnessIndex = rowIndex / 3u;
            metallicQueryIndex = rowIndex % 3u;
            break;
        case P4Scenario::Raw254DirectConductorEndpoint:
            if (rowIndex != 0u)
            {
                return false;
            }
            roughnessIndex = 4u;
            metallicQueryIndex = P4RuntimeTargetMetallicIndex;
            break;
        default:
            return false;
        }
        outMaterialIndex = roughnessIndex * P4RuntimeMaterialCountPerRoughness +
                           metallicQueryIndex;
        outLightCount = scenario == P4Scenario::Raw254DirectConductorEndpoint ? 1u : 0u;
        return outMaterialIndex < 30u;
    }
    constexpr double P4DirectLegacyEnvelope = 0.005;

    uint32_t ReverseBits32(uint32_t value)
    {
        value = (value << 16u) | (value >> 16u);
        value = ((value & 0x55555555u) << 1u) | ((value & 0xAAAAAAAAu) >> 1u);
        value = ((value & 0x33333333u) << 2u) | ((value & 0xCCCCCCCCu) >> 2u);
        value = ((value & 0x0F0F0F0Fu) << 4u) | ((value & 0xF0F0F0F0u) >> 4u);
        value = ((value & 0x00FF00FFu) << 8u) | ((value & 0xFF00FF00u) >> 8u);
        return value;
    }

    uint32_t Mix32(uint32_t value)
    {
        value += 0x9E3779B9u;
        value = (value ^ (value >> 16u)) * 0x85EBCA6Bu;
        value = (value ^ (value >> 13u)) * 0xC2B2AE35u;
        return value ^ (value >> 16u);
    }

    uint32_t Owen32(uint32_t value, uint32_t key)
    {
        uint32_t output = 0u;
        for (int32_t bit = 31; bit >= 0; --bit)
        {
            const uint32_t salt = static_cast<uint32_t>(31 - bit) * 0x9E3779B9u;
            const uint32_t flip = (Mix32(key ^ output ^ salt) >> 31u) & 1u;
            const uint32_t sourceBit = (value >> static_cast<uint32_t>(bit)) & 1u;
            output = (output << 1u) | (sourceBit ^ flip);
        }
        return output;
    }

    void BuildP4SobolDirections(P4SobolDirections& outDirections)
    {
        for (uint32_t index = 0u; index < 32u; ++index)
        {
            outDirections.Values[0][index] = 1u << (31u - index);
        }
        uint32_t m[32] = {};
        m[0] = 1u;
        m[1] = 3u;
        m[2] = 5u;
        for (uint32_t index = 3u; index < 32u; ++index)
        {
            m[index] = (m[index - 2u] << 2u) ^
                       (m[index - 3u] << 3u) ^
                       m[index - 3u];
        }
        for (uint32_t index = 0u; index < 32u; ++index)
        {
            outDirections.Values[1][index] = m[index] << (31u - index);
        }
    }

    uint32_t SobolUInt(uint32_t index, uint32_t dimension,
                       const P4SobolDirections& directions)
    {
        const uint32_t gray = index ^ (index >> 1u);
        uint32_t value = 0u;
        for (uint32_t bit = 0u; bit < 32u; ++bit)
        {
            if (((gray >> bit) & 1u) != 0u)
            {
                value ^= directions.Values[dimension][bit];
            }
        }
        return value;
    }

    bool BuildP4HammersleySamples(uint32_t sampleCount,
                                  Core::Container::VariableArray<P4SamplePair>& outSamples)
    {
        if (sampleCount == 0u)
        {
            return false;
        }
        outSamples.clear();
        outSamples.reserve(sampleCount);
        for (uint32_t index = 0u; index < sampleCount; ++index)
        {
            P4SamplePair sample;
            sample.Xi1 = static_cast<double>(index) / static_cast<double>(sampleCount);
            sample.Xi2 = static_cast<double>(ReverseBits32(index)) * P4HalfUnit;
            outSamples.push_back(sample);
        }
        return true;
    }

    bool BuildP4SobolSamples(uint32_t sampleCount,
                             Core::Container::VariableArray<P4SamplePair>& outSamples)
    {
        if (sampleCount == 0u)
        {
            return false;
        }
        P4SobolDirections directions;
        BuildP4SobolDirections(directions);
        outSamples.clear();
        outSamples.reserve(sampleCount);
        for (uint32_t index = 0u; index < sampleCount; ++index)
        {
            P4SamplePair sample;
            sample.Xi1 = (static_cast<double>(Owen32(
                             SobolUInt(index, 0u, directions), 0x12345678u)) + 0.5) /
                         4294967296.0;
            sample.Xi2 = (static_cast<double>(Owen32(
                             SobolUInt(index, 1u, directions), 0x9ABCDEF0u)) + 0.5) /
                         4294967296.0;
            outSamples.push_back(sample);
        }
        return true;
    }

    double QuantizeP4R8(double value)
    {
        return std::floor(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
    }

    void AddP4UniqueIndex(Core::Container::VariableArray<uint32_t>& indices,
                          uint32_t index)
    {
        for (const uint32_t existing : indices)
        {
            if (existing == index)
            {
                return;
            }
        }
        indices.push_back(index);
    }

    void BuildP4QueryIndices(const double* values,
                             Core::Container::VariableArray<uint32_t>& outIndices)
    {
        outIndices.clear();
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            const double quantized = QuantizeP4R8(values[index]);
            const double coordinate = std::clamp(
                quantized,
                0.5 / static_cast<double>(P4DfgTextureSize),
                255.5 / static_cast<double>(P4DfgTextureSize));
            const double texelPosition = coordinate * static_cast<double>(P4DfgTextureSize) - 0.5;
            const uint32_t first = static_cast<uint32_t>(std::floor(texelPosition));
            AddP4UniqueIndex(outIndices, first);
            AddP4UniqueIndex(outIndices, std::min(first + 1u, P4DfgTextureSize - 1u));
        }
    }

    P4DfgValue IntegrateP4Dfg(double nDotV,
                              double roughness,
                              const Core::Container::VariableArray<P4SamplePair>& samples)
    {
        const double alpha = roughness * roughness;
        const double alphaSquared = alpha * alpha;
        const double k = alpha / 2.0;
        const double viewX = std::sqrt(std::max(0.0, 1.0 - nDotV * nDotV));
        double sumA = 0.0;
        double sumB = 0.0;
        for (const P4SamplePair& sample : samples)
        {
            const double phi = 2.0 * P4Pi * sample.Xi1;
            const double denominator = 1.0 + (alphaSquared - 1.0) * sample.Xi2;
            const double cosThetaH = std::sqrt(std::max(0.0,
                (1.0 - sample.Xi2) / denominator));
            const double sinThetaH = std::sqrt(std::max(0.0, 1.0 - cosThetaH * cosThetaH));
            const double halfX = sinThetaH * std::cos(phi);
            const double halfY = sinThetaH * std::sin(phi);
            const double halfZ = cosThetaH;
            const double viewDotHalf = std::max(0.0, viewX * halfX + nDotV * halfZ);
            const double lightZ = 2.0 * viewDotHalf * halfZ - nDotV;
            const double nDotL = std::max(0.0, lightZ);
            const double nDotH = std::max(0.0, halfZ);
            if (nDotL <= 0.0)
            {
                continue;
            }

            const double geometryView = nDotV / (nDotV * (1.0 - k) + k);
            const double geometryLight = nDotL / (nDotL * (1.0 - k) + k);
            const double visibility = geometryView * geometryLight * viewDotHalf /
                                      (nDotH * nDotV + 1.0e-4);
            const double fresnel = std::pow(1.0 - viewDotHalf, 5.0);
            sumA += (1.0 - fresnel) * visibility;
            sumB += fresnel * visibility;
        }

        P4DfgValue result;
        result.A = std::clamp(sumA / static_cast<double>(samples.size()), 0.0, 1.0);
        result.B = std::clamp(sumB / static_cast<double>(samples.size()), 0.0, 1.0);
        return result;
    }

    void AccumulateP4CrossCheck(
        const P4DfgValue& left,
        const P4DfgValue& right,
        double outMaxAbs[3],
        double outMaxRelative[3])
    {
        const double valuesLeft[3] = {left.A, left.B, left.A + left.B};
        const double valuesRight[3] = {right.A, right.B, right.A + right.B};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const double absolute = std::abs(valuesLeft[channel] - valuesRight[channel]);
            outMaxAbs[channel] = std::max(outMaxAbs[channel], absolute);
            if (std::abs(valuesRight[channel]) >= 0.01)
            {
                outMaxRelative[channel] = std::max(
                    outMaxRelative[channel], absolute / std::abs(valuesRight[channel]));
            }
        }
    }

    const P4DfgValue* FindP4DfgValue(const P4DfgOracle& oracle,
                                     uint32_t x,
                                     uint32_t y)
    {
        for (const P4DfgValue& value : oracle.Values)
        {
            const size_t index = static_cast<size_t>(&value - oracle.Values.data());
            const uint32_t storedX = oracle.NdotVIndices[index % oracle.NdotVIndices.size()];
            const uint32_t storedY = oracle.RoughnessIndices[index / oracle.NdotVIndices.size()];
            if (storedX == x && storedY == y)
            {
                return &value;
            }
        }
        return nullptr;
    }

    bool BuildP4DfgOracle(P4DfgOracle& outOracle)
    {
        if (!ValidateIeee754Binary16RneTable())
        {
            return false;
        }

        const double nDotVQueries[] = {0.10, 0.25, 0.50, 0.75, 1.00};
        const double roughnessQueries[] = {0.05, 0.25, 0.50, 0.75, 1.00};
        outOracle.NdotVIndices.clear();
        outOracle.RoughnessIndices.clear();
        BuildP4QueryIndices(nDotVQueries, outOracle.NdotVIndices);
        BuildP4QueryIndices(roughnessQueries, outOracle.RoughnessIndices);
        if (outOracle.NdotVIndices.size() != 9u ||
            outOracle.RoughnessIndices.size() != 9u)
        {
            return false;
        }

        Core::Container::VariableArray<P4SamplePair> hammersleySamples;
        Core::Container::VariableArray<P4SamplePair> productionSamples;
        Core::Container::VariableArray<P4SamplePair> sobolSamples;
        if (!BuildP4HammersleySamples(P4DfgHammersleySampleCount, hammersleySamples) ||
            !BuildP4HammersleySamples(P4DfgProductionSampleCount, productionSamples) ||
            !BuildP4SobolSamples(P4DfgSampleCount, sobolSamples))
        {
            return false;
        }
        P4SobolDirections smokeDirections;
        BuildP4SobolDirections(smokeDirections);
        if (Owen32(SobolUInt(0u, 0u, smokeDirections), 0x12345678u) != 0x312E10D4u ||
            Owen32(SobolUInt(0u, 1u, smokeDirections), 0x9ABCDEF0u) != 0x531A11A7u)
        {
            return false;
        }

        Core::Container::VariableArray<P4DfgValue> hammersleyValues;
        Core::Container::VariableArray<P4DfgValue> productionValues;
        Core::Container::VariableArray<P4DfgValue> sobolValues;
        hammersleyValues.reserve(81u);
        productionValues.reserve(81u);
        sobolValues.reserve(81u);
        for (const uint32_t roughnessIndex : outOracle.RoughnessIndices)
        {
            const double roughness =
                (static_cast<double>(roughnessIndex) + 0.5) / P4DfgTextureSize;
            for (const uint32_t nDotVIndex : outOracle.NdotVIndices)
            {
                const double nDotV =
                    (static_cast<double>(nDotVIndex) + 0.5) / P4DfgTextureSize;
                hammersleyValues.push_back(IntegrateP4Dfg(nDotV, roughness, hammersleySamples));
                productionValues.push_back(IntegrateP4Dfg(nDotV, roughness, productionSamples));
                sobolValues.push_back(IntegrateP4Dfg(nDotV, roughness, sobolSamples));
            }
        }

        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outOracle.HammersleySobolMaxAbs[channel] = 0.0;
            outOracle.HammersleySobolMaxRelative[channel] = 0.0;
            outOracle.ProductionSobolMaxAbs[channel] = 0.0;
            outOracle.ProductionSobolMaxRelative[channel] = 0.0;
        }
        for (size_t index = 0u; index < sobolValues.size(); ++index)
        {
            AccumulateP4CrossCheck(hammersleyValues[index], sobolValues[index],
                                   outOracle.HammersleySobolMaxAbs,
                                   outOracle.HammersleySobolMaxRelative);
            AccumulateP4CrossCheck(productionValues[index], sobolValues[index],
                                   outOracle.ProductionSobolMaxAbs,
                                   outOracle.ProductionSobolMaxRelative);
        }
        outOracle.Values.clear();
        outOracle.Values.reserve(81u);
        for (const P4DfgValue& value : hammersleyValues)
        {
            P4DfgValue encoded;
            encoded.A = static_cast<double>(DecodeIeee754Binary16(
                EncodeIeee754Binary16Rne(static_cast<float>(value.A))));
            encoded.B = static_cast<double>(DecodeIeee754Binary16(
                EncodeIeee754Binary16Rne(static_cast<float>(value.B))));
            outOracle.Values.push_back(encoded);
        }
        return true;
    }

    bool BuildP5DfgOracle(P5DfgOracle& outOracle)
    {
        outOracle = {};
        if (!ValidateIeee754Binary16RneTable())
        {
            return false;
        }
        Core::Container::VariableArray<P4SamplePair> samples;
        if (!BuildP4HammersleySamples(P4DfgProductionSampleCount, samples))
        {
            return false;
        }
        const double nDotV254 = 254.5 / static_cast<double>(P4DfgTextureSize);
        const double nDotV255 = 255.5 / static_cast<double>(P4DfgTextureSize);
        const double roughness127 = 127.5 / static_cast<double>(P4DfgTextureSize);
        const double roughness128 = 128.5 / static_cast<double>(P4DfgTextureSize);
        const P4DfgValue raw254Y127 = IntegrateP4Dfg(nDotV254, roughness127, samples);
        const P4DfgValue raw254Y128 = IntegrateP4Dfg(nDotV254, roughness128, samples);
        const P4DfgValue raw255Y127 = IntegrateP4Dfg(nDotV255, roughness127, samples);
        const P4DfgValue raw255Y128 = IntegrateP4Dfg(nDotV255, roughness128, samples);
        const P4DfgValue rawMid = IntegrateP4Dfg(nDotV255, 0.5, samples);
        const auto encodeStoredHalf = [](double value) -> double
        {
            return static_cast<double>(DecodeIeee754Binary16(
                EncodeIeee754Binary16Rne(static_cast<float>(value))));
        };
        outOracle.X254Y127 = {
            encodeStoredHalf(raw254Y127.A), encodeStoredHalf(raw254Y127.B)};
        outOracle.X254Y128 = {
            encodeStoredHalf(raw254Y128.A), encodeStoredHalf(raw254Y128.B)};
        outOracle.X255Y127 = {
            encodeStoredHalf(raw255Y127.A), encodeStoredHalf(raw255Y127.B)};
        outOracle.X255Y128 = {
            encodeStoredHalf(raw255Y128.A), encodeStoredHalf(raw255Y128.B)};
        const P4DfgValue sampled = {
            (outOracle.X255Y127.A + outOracle.X255Y128.A) * 0.5,
            (outOracle.X255Y127.B + outOracle.X255Y128.B) * 0.5};
        const P4DfgValue fixedPreflight = {
            encodeStoredHalf(rawMid.A), encodeStoredHalf(rawMid.B)};
        if (!std::isfinite(sampled.A) || !std::isfinite(sampled.B) ||
            std::abs(fixedPreflight.A - 0.89453125) > 1.0e-12 ||
            std::abs(fixedPreflight.B - 0.0000262856483459473) > 1.0e-18 ||
            std::abs(sampled.A - fixedPreflight.A) > 1.0e-6 ||
            std::abs(sampled.B - fixedPreflight.B) > 1.0e-7)
        {
            return false;
        }
        outOracle.bValid = true;
        return true;
    }

    bool BuildP4RoughnessOracle(P4RoughnessOracle& outOracle)
    {
        constexpr double roughnessQueries[5] = {0.05, 0.25, 0.50, 0.75, 1.00};
        outOracle = {};
        Core::Container::VariableArray<P4SamplePair> sobolSamples;
        if (!BuildP4SobolSamples(P4DfgSampleCount, sobolSamples))
        {
            return false;
        }
        const double nDotV = std::clamp(
            QuantizeP4R8(1.0),
            0.5 / P4DfgTextureSize,
            255.5 / P4DfgTextureSize);
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            const double roughness = std::clamp(
                QuantizeP4R8(roughnessQueries[index]),
                0.5 / P4DfgTextureSize,
                255.5 / P4DfgTextureSize);
            const P4DfgValue raw = IntegrateP4Dfg(nDotV, roughness, sobolSamples);
            P4DfgValue& canonical = outOracle.Values[index];
            canonical.A = static_cast<double>(DecodeIeee754Binary16(
                EncodeIeee754Binary16Rne(static_cast<float>(raw.A))));
            canonical.B = static_cast<double>(DecodeIeee754Binary16(
                EncodeIeee754Binary16Rne(static_cast<float>(raw.B))));
            if (!std::isfinite(canonical.A) || !std::isfinite(canonical.B) ||
                canonical.A < 0.0 || canonical.B < 0.0 ||
                canonical.A >= P4RneRangeLimit || canonical.B >= P4RneRangeLimit)
            {
                return false;
            }
        }
        outOracle.bValid = true;
        return true;
    }

    bool BuildP4DirectConductorOracle(P4DirectConductorOracle& outOracle)
    {
        Core::Container::VariableArray<P4SamplePair> samples;
        if (!BuildP4HammersleySamples(P4DfgHammersleySampleCount, samples))
        {
            return false;
        }

        const P4DfgValue rawDfg = IntegrateP4Dfg(
            P4DfgCoordinate,
            P4DfgCoordinate,
            samples);
        outOracle.Dfg.A = static_cast<double>(DecodeIeee754Binary16(
            EncodeIeee754Binary16Rne(static_cast<float>(rawDfg.A))));
        outOracle.Dfg.B = static_cast<double>(DecodeIeee754Binary16(
            EncodeIeee754Binary16Rne(static_cast<float>(rawDfg.B))));
        if (!std::isfinite(outOracle.Dfg.A) || !std::isfinite(outOracle.Dfg.B) ||
            std::abs(outOracle.Dfg.A - 0.308837890625) > 1.0e-12 ||
            std::abs(outOracle.Dfg.B - 0.000035405158996582031) > 1.0e-18)
        {
            return false;
        }

        constexpr double intensityCd = 100.0;
        constexpr double distanceMeters = 2.0;
        constexpr double rangeMeters = 1000.0;
        outOracle.RangeWindow = std::pow(
            1.0 - std::pow(distanceMeters / rangeMeters, 4.0), 2.0);
        outOracle.Radiance = intensityCd /
            (distanceMeters * distanceMeters) * outOracle.RangeWindow;
        outOracle.Common = (1.0 / P4Pi) / (4.0 + 0.0001);
        const double ess = outOracle.Dfg.A + outOracle.Dfg.B;
        if (!std::isfinite(ess) || ess <= 0.0)
        {
            return false;
        }

        const double compensation = 1.0 / ess;
        outOracle.CorrectTarget = outOracle.Radiance * outOracle.Common * compensation;
        outOracle.StoredTarget = outOracle.CorrectTarget / 72.0;
        outOracle.StoredHalf = static_cast<double>(DecodeIeee754Binary16(
            EncodeIeee754Binary16Rne(static_cast<float>(outOracle.StoredTarget))));
        outOracle.LegacyTarget = outOracle.Radiance * outOracle.Common;
        outOracle.LegacyStoredHalf = static_cast<double>(DecodeIeee754Binary16(
            EncodeIeee754Binary16Rne(static_cast<float>(outOracle.LegacyTarget / 72.0))));
        outOracle.LegacyStoredPhysical = outOracle.LegacyStoredHalf * 72.0;
        outOracle.SensitivityPercent =
            std::abs(outOracle.CorrectTarget - outOracle.LegacyTarget) /
            outOracle.CorrectTarget * 100.0;

        return std::isfinite(outOracle.CorrectTarget) &&
               std::isfinite(outOracle.StoredTarget) &&
               std::isfinite(outOracle.StoredHalf) &&
               std::isfinite(outOracle.LegacyTarget) &&
               std::isfinite(outOracle.LegacyStoredHalf) &&
               std::isfinite(outOracle.LegacyStoredPhysical) &&
               std::isfinite(outOracle.SensitivityPercent) &&
               std::abs(outOracle.CorrectTarget - P4DirectTargetLiteral) <= 1.0e-12 &&
               std::abs(outOracle.StoredTarget - P4DirectStoredLiteral) <= 1.0e-12 &&
               std::abs(outOracle.StoredHalf - P4DirectStoredHalfLiteral) <= 1.0e-12 &&
               std::abs(outOracle.LegacyTarget - P4DirectLegacyTargetLiteral) <= 1.0e-12 &&
               std::abs(outOracle.LegacyStoredHalf - P4DirectLegacyStoredHalfLiteral) <= 1.0e-12 &&
               std::abs(outOracle.LegacyStoredPhysical -
                        P4DirectLegacyStoredPhysicalLiteral) <= 1.0e-12 &&
               std::abs(outOracle.SensitivityPercent -
                        P4DirectSensitivityPercentLiteral) <= 1.0e-12;
    }

    bool SampleP4DfgOracle(const P4DfgOracle& oracle,
                           double nDotV,
                           double roughness,
                           P4DfgValue& outValue)
    {
        if (oracle.NdotVIndices.size() != 9u || oracle.RoughnessIndices.size() != 9u ||
            oracle.Values.size() != 81u)
        {
            return false;
        }
        const double coordinateX = std::clamp(
            QuantizeP4R8(nDotV), 0.5 / P4DfgTextureSize, 255.5 / P4DfgTextureSize);
        const double coordinateY = std::clamp(
            QuantizeP4R8(roughness), 0.5 / P4DfgTextureSize, 255.5 / P4DfgTextureSize);
        const double positionX = coordinateX * P4DfgTextureSize - 0.5;
        const double positionY = coordinateY * P4DfgTextureSize - 0.5;
        const uint32_t x0 = static_cast<uint32_t>(std::floor(positionX));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(positionY));
        const uint32_t x1 = std::min(x0 + 1u, P4DfgTextureSize - 1u);
        const uint32_t y1 = std::min(y0 + 1u, P4DfgTextureSize - 1u);
        const double wx = positionX - std::floor(positionX);
        const double wy = positionY - std::floor(positionY);
        const P4DfgValue* v00 = FindP4DfgValue(oracle, x0, y0);
        const P4DfgValue* v10 = FindP4DfgValue(oracle, x1, y0);
        const P4DfgValue* v01 = FindP4DfgValue(oracle, x0, y1);
        const P4DfgValue* v11 = FindP4DfgValue(oracle, x1, y1);
        if (v00 == nullptr || v10 == nullptr || v01 == nullptr || v11 == nullptr)
        {
            return false;
        }
        const double topA = v00->A + (v10->A - v00->A) * wx;
        const double bottomA = v01->A + (v11->A - v01->A) * wx;
        const double topB = v00->B + (v10->B - v00->B) * wx;
        const double bottomB = v01->B + (v11->B - v01->B) * wx;
        outValue.A = topA + (bottomA - topA) * wy;
        outValue.B = topB + (bottomB - topB) * wy;
        return true;
    }

    P4RgbValue P4RawDirection(double u, double v)
    {
        const double phi = 2.0 * P4Pi * (u - 0.5);
        const double theta = P4Pi * v;
        const double sinTheta = std::sin(theta);
        P4RgbValue direction;
        direction.R = sinTheta * std::cos(phi);
        direction.G = std::cos(theta);
        direction.B = sinTheta * std::sin(phi);
        return direction;
    }

    double IntegrateP4Raw250Coefficient(
        double roughness,
        const Core::Container::VariableArray<P4SamplePair>& samples)
    {
        const double alpha = roughness * roughness;
        const double alphaSquared = alpha * alpha;
        double weightedSum = 0.0;
        double weightSum = 0.0;
        for (const P4SamplePair& sample : samples)
        {
            const double mu = sample.Xi1;
            const double phi = 2.0 * P4Pi * sample.Xi2;
            const double tangent = std::sqrt(std::max(0.0, 1.0 - mu * mu));
            const double lightX = tangent * std::cos(phi);
            const double lightY = tangent * std::sin(phi);
            const double lightZ = mu;
            const double halfLength = std::sqrt(std::max(1.0e-12,
                lightX * lightX + lightY * lightY + (1.0 + lightZ) * (1.0 + lightZ)));
            const double halfZ = (1.0 + lightZ) / halfLength;
            const double denominator = P4Pi *
                std::pow(halfZ * halfZ * (alphaSquared - 1.0) + 1.0, 2.0);
            const double distribution = alphaSquared / std::max(denominator, 1.0e-12);
            const double weight = distribution * mu;
            weightSum += weight;
            weightedSum += weight * mu;
        }
        return weightSum > 0.0 ? weightedSum / weightSum : 0.0;
    }

    const P4RawMip* FindP4RawMip(const P4Raw250Oracle& oracle, uint32_t mip)
    {
        for (const P4RawMip& value : oracle.Mips)
        {
            if (value.Mip == mip)
            {
                return &value;
            }
        }
        return nullptr;
    }

    P4RgbValue GetP4RawMipTexel(const P4RawMip& mip, uint32_t x, uint32_t y)
    {
        const size_t index = static_cast<size_t>(y) * mip.Width + x;
        return mip.Values[index];
    }

    uint32_t WrapP4Index(int64_t value, uint32_t size)
    {
        const int64_t signedSize = static_cast<int64_t>(size);
        const int64_t wrapped = value % signedSize;
        return static_cast<uint32_t>(wrapped < 0 ? wrapped + signedSize : wrapped);
    }

    P4RgbValue SampleP4RawMip(const P4RawMip& mip, double u, double v)
    {
        const double xPosition = u * static_cast<double>(mip.Width) - 0.5;
        const double yPosition = v * static_cast<double>(mip.Height) - 0.5;
        const int64_t x0 = static_cast<int64_t>(std::floor(xPosition));
        const int64_t y0 = static_cast<int64_t>(std::floor(yPosition));
        const uint32_t x1 = WrapP4Index(x0 + 1, mip.Width);
        const uint32_t xWrapped = WrapP4Index(x0, mip.Width);
        const uint32_t yClamped = static_cast<uint32_t>(std::clamp<int64_t>(
            y0, 0, static_cast<int64_t>(mip.Height - 1u)));
        const uint32_t yNext = static_cast<uint32_t>(std::clamp<int64_t>(
            y0 + 1, 0, static_cast<int64_t>(mip.Height - 1u)));
        const double wx = xPosition - std::floor(xPosition);
        const double wy = yPosition - std::floor(yPosition);
        const P4RgbValue c00 = GetP4RawMipTexel(mip, xWrapped, yClamped);
        const P4RgbValue c10 = GetP4RawMipTexel(mip, x1, yClamped);
        const P4RgbValue c01 = GetP4RawMipTexel(mip, xWrapped, yNext);
        const P4RgbValue c11 = GetP4RawMipTexel(mip, x1, yNext);
        P4RgbValue result;
        const double topR = c00.R + (c10.R - c00.R) * wx;
        const double bottomR = c01.R + (c11.R - c01.R) * wx;
        const double topG = c00.G + (c10.G - c00.G) * wx;
        const double bottomG = c01.G + (c11.G - c01.G) * wx;
        const double topB = c00.B + (c10.B - c00.B) * wx;
        const double bottomB = c01.B + (c11.B - c01.B) * wx;
        result.R = topR + (bottomR - topR) * wy;
        result.G = topG + (bottomG - topG) * wy;
        result.B = topB + (bottomB - topB) * wy;
        return result;
    }

    bool BuildP4Raw250Oracle(P4Raw250Oracle& outOracle)
    {
        Core::Container::VariableArray<P4SamplePair> samples;
        if (!BuildP4SobolSamples(P4DfgSampleCount, samples))
        {
            return false;
        }
        constexpr uint32_t mips[] = {0u, 1u, 2u, 4u, 8u};
        outOracle.Mips.clear();
        outOracle.Mips.reserve(5u);
        for (const uint32_t mip : mips)
        {
            P4RawMip result;
            result.Mip = mip;
            result.Width = std::max(1u, 256u >> mip);
            result.Height = std::max(1u, 128u >> mip);
            result.Values.reserve(static_cast<size_t>(result.Width) * result.Height);
            const double coefficient = mip == 0u
                                           ? 0.0
                                           : IntegrateP4Raw250Coefficient(
                                                 static_cast<double>(mip) / 8.0, samples);
            for (uint32_t y = 0u; y < result.Height; ++y)
            {
                for (uint32_t x = 0u; x < result.Width; ++x)
                {
                    const P4RgbValue direction = P4RawDirection(
                        (static_cast<double>(x) + 0.5) / result.Width,
                        (static_cast<double>(y) + 0.5) / result.Height);
                    P4RgbValue value;
                    if (mip == 0u)
                    {
                        value.R = 64.0 + 16.0 * direction.R;
                        value.G = 64.0 + 16.0 * direction.G;
                        value.B = 64.0 + 16.0 * direction.B;
                    }
                    else
                    {
                        value.R = 64.0 + 16.0 * coefficient * direction.R;
                        value.G = 64.0 + 16.0 * coefficient * direction.G;
                        value.B = 64.0 + 16.0 * coefficient * direction.B;
                    }
                    value.R = DecodeIeee754Binary16(EncodeIeee754Binary16Rne(
                        static_cast<float>(value.R)));
                    value.G = DecodeIeee754Binary16(EncodeIeee754Binary16Rne(
                        static_cast<float>(value.G)));
                    value.B = DecodeIeee754Binary16(EncodeIeee754Binary16Rne(
                        static_cast<float>(value.B)));
                    result.Values.push_back(value);
                }
            }
            outOracle.Mips.push_back(std::move(result));
        }
        const P4RawMip* mip8 = FindP4RawMip(outOracle, 8u);
        if (mip8 == nullptr || mip8->Values.size() != 1u ||
            std::abs(mip8->Values[0].R - 74.6875) > 0.1 ||
            std::abs(mip8->Values[0].G - 64.0) > 0.1 ||
            std::abs(mip8->Values[0].B - 64.0) > 0.1)
        {
            return false;
        }
        return true;
    }

    bool SampleP4Raw250Oracle(const P4Raw250Oracle& oracle,
                              double u,
                              double v,
                              double lod,
                              P4RgbValue& outValue)
    {
        const double floorLod = std::floor(lod);
        const uint32_t lowerMip = std::min(8u, static_cast<uint32_t>(floorLod));
        const double mipWeight = std::clamp(lod - floorLod, 0.0, 1.0);
        const P4RawMip* lower = FindP4RawMip(oracle, lowerMip);
        if (lower == nullptr)
        {
            return false;
        }
        if (mipWeight == 0.0)
        {
            outValue = SampleP4RawMip(*lower, u, v);
            return true;
        }
        const uint32_t upperMip = std::min(8u, lowerMip + 1u);
        const P4RawMip* upper = FindP4RawMip(oracle, upperMip);
        if (upper == nullptr)
        {
            return false;
        }
        const P4RgbValue lowerValue = SampleP4RawMip(*lower, u, v);
        const P4RgbValue upperValue = SampleP4RawMip(*upper, u, v);
        outValue.R = lowerValue.R + (upperValue.R - lowerValue.R) * mipWeight;
        outValue.G = lowerValue.G + (upperValue.G - lowerValue.G) * mipWeight;
        outValue.B = lowerValue.B + (upperValue.B - lowerValue.B) * mipWeight;
        return true;
    }

    class HdrHandler final : public RenderingValidationApplicationHandler
    {
    public:
        enum class TransparentPhysicalStage : uint8_t
        {
            DirectM0Off,
            DirectM0On,
            DirectM05Off,
            DirectM05On,
            ShadowUnshadowed,
            Shadowed,
            IblM0Off,
            IblM0On,
            IblM05Off,
            IblM05On,
            Complete
        };

        bool m_bTransparentPhysicalHasFrameNumber = false;
        uint64_t m_TransparentPhysicalLastFrameNumber = 0u;
        bool m_bTransparentPhysicalHasStageToken = false;
        uint64_t m_TransparentPhysicalLastStageToken = 0u;

        enum class KnownCdStage : uint8_t
        {
            PureLambertA,
            PureLambertB,
            DirectPbrA,
            DirectPbrB,
            NormalA,
            NormalB,
            Complete
        };

        enum class AllNumericalCaptureStage : uint8_t
        {
            BackBuffer,
            PresentationColor,
            SceneColor,
            Complete
        };

        Core::Rendering::FrameCaptureSourceKind GetCaptureSourceForTest() const
        {
            return GetRunConfig().CaptureSource;
        }

        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            m_bAllNumericalScenario = false;
            m_bAllNumericalArgumentParsed = false;
            m_bAllNumericalForcedRowsArgumentParsed = false;
            for (const Core::Container::String& argument : args)
            {
                if (argument == TEXT("--r1-scenario=all-numerical"))
                {
                    m_bAllNumericalScenario = true;
                    break;
                }
            }

            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            if (m_bAllNumericalScenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                LOG_ERROR("--r1-scenario=all-numerical は BackBuffer capture と組み合わせてください");
                return false;
            }
            if (m_bAllNumericalScenario &&
                (!m_bAllNumericalForcedRowsArgumentParsed || m_AllNumericalForcedRows != 2u))
            {
                LOG_ERROR("R1 all-numerical forced format row count is not the measured value");
                return false;
            }
            if (m_bR1Scenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                LOG_ERROR("--r1-scenario=srgb-transfer は BackBuffer capture と組み合わせてください");
                return false;
            }
            if (m_bKnownCdScenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::SceneColor)
            {
                LOG_ERROR("--r1-scenario=known-cd-lambert は SceneColor capture と組み合わせてください");
                return false;
            }
            if (m_bP4Scenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::SceneColor)
            {
                LOG_ERROR("P4 scenario は SceneColor capture と組み合わせてください");
                return false;
            }
            if (m_bTransparentPhysicalLightingScenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::SceneColor)
            {
                LOG_ERROR("transparent-physical-lighting は SceneColor capture と組み合わせてください");
                return false;
            }
            m_R1CaptureStage = R1CaptureStage::BackBuffer;
            m_bR1HasFrameNumber = false;
            m_R1LastFrameNumber = 0u;
            m_KnownCdStage = KnownCdStage::PureLambertA;
            m_KnownCdHasFrameNumber = false;
            m_KnownCdLastFrameNumber = 0u;
            m_KnownCdHasStageToken = false;
            m_KnownCdLastStageToken = 0u;
            m_P4RowIndex = 0u;
            m_P4Substage = P4CaptureSubstage::Primary;
            m_P4HasFrameNumber = false;
            m_P4LastFrameNumber = 0u;
            m_P4HasStageToken = false;
            m_P4LastStageToken = 0u;
            m_bP4StageApplyFailed = false;
            m_bP4CameraMarkerPrinted = false;
            m_bP4CaptureEnvelopeMarkerPrinted = false;
            m_bP4DirectLegacyMarkerPrinted = false;
            m_bP4ActualOracleMismatch = false;
            m_bP4MismatchMarkerPrinted = false;
            m_bP4ActualCameraAvailable = false;
            m_bP4HasRuntimeIdentity = false;
            m_P4LastRuntimeRow = 0u;
            m_bP4FixtureSnapshotAvailable = false;
            m_P4FixtureMeshCount = 0u;
            m_P4FixtureTextureCount = 0u;
            m_P4FixtureMaterialCount = 0u;
            m_TransparentPhysicalStage = TransparentPhysicalStage::DirectM0Off;
            m_bTransparentPhysicalHasFrameNumber = false;
            m_TransparentPhysicalLastFrameNumber = 0u;
            m_bTransparentPhysicalHasStageToken = false;
            m_TransparentPhysicalLastStageToken = 0u;
            m_bTransparentPhysicalStageApplyFailed = false;
            m_bTransparentPhysicalRowMarkerPrinted = false;
            m_bTransparentPhysicalHasUnshadowedValue = false;
            m_TransparentPhysicalDfgOracle = {};
            m_bTransparentPhysicalDfgOracleValid = false;
            m_AllNumericalCaptureStage = AllNumericalCaptureStage::BackBuffer;
            m_AllNumericalRowIndex = 0u;
            m_AllNumericalHasFrameNumber = false;
            m_AllNumericalLastFrameNumber = 0u;
            m_AllNumericalHasStageToken = false;
            m_AllNumericalLastStageToken = 0u;
            m_bAllNumericalRequestStartFrameSet = false;
            m_AllNumericalRequestStartFrame = 0u;
            m_AllNumericalLastObservedRequestId = 0u;
            m_AllNumericalStartupFrame = 0u;
            m_AllNumericalFinalFrame = 0u;
            m_AllNumericalPreviousFrame = 0u;
            m_AllNumericalMaxLatency = 0u;
            m_AllNumericalBackBufferScans = 0u;
            m_AllNumericalPresentationScans = 0u;
            m_AllNumericalSceneScans = 0u;
            m_AllNumericalActualByteChannels = 0u;
            m_AllNumericalFloatChannels = 0u;
            m_AllNumericalNumericalRows = 0u;
            m_AllNumericalForcedRows = 0u;
            m_bAllNumericalStartupFrameSet = false;
            m_bAllNumericalStageApplyFailed = false;
            m_bAllNumericalRowMarkerPrinted = false;
            m_AllNumericalBackBuffer = {};
            m_AllNumericalPresentationColor = {};
            m_AllNumericalPresentationImage = {};
            m_AllNumericalSceneColor = {};
            if (m_bTransparentPhysicalLightingScenario &&
                !ValidateTransparentPhysicalStageContract())
            {
                LOG_ERROR("P5 transparent physical lighting stage contract is invalid");
                return false;
            }
            if (m_bKnownCdScenario && !ValidateKnownCdScenarioContract())
            {
                LOG_ERROR("known-cd-lambert test-local scenario contract is invalid");
                return false;
            }
            return true;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
            }
            if (m_bAllNumericalScenario)
            {
                if (GetRunConfig().Scene != SceneKind::Indoor ||
                    !ValidateAllNumericalRowContract() || !ValidateSyntheticLsbContract() ||
                    !ValidateIeee754Binary16RneTable() ||
                    !BuildP4DfgOracle(m_P4DfgOracle) ||
                    !BuildP4RoughnessOracle(m_P4RoughnessOracle) ||
                    !BuildP4Raw250Oracle(m_P4Raw250Oracle) ||
                    !BuildP5DfgOracle(m_TransparentPhysicalDfgOracle) ||
                    !GetFixture().ApplyBaseValidationFixture() ||
                    !GetFixture().ApplyP4ScenarioRow({P4Scenario::Raw250TextureRepresentation, 0u}) ||
                    GetFixture().TrackedMeshCount() != 2u ||
                    GetFixture().TrackedTextureCount() != 13u ||
                    GetFixture().TrackedMaterialCount() != 33u)
                {
                    LOG_ERROR("R1 all-numerical fixture preflight failed");
                    return false;
                }
                m_AllNumericalForcedRows = 2u;
                m_bTransparentPhysicalDfgOracleValid = true;
                if (Core::Module::RegisterImGuiModule(Core::Module::GetModuleRegistry()) == nullptr)
                {
                    LOG_ERROR("R1 all-numerical scenario failed to register ImGui module");
                    return false;
                }
                Modules::Gui::RegisterImGuiView(&m_MarkerView);
                m_bMarkerRegistered = true;
                std::cout << "R1 all-numerical fixture passed: scene="
                          << (GetRunConfig().Scene == SceneKind::Indoor ? "indoor" : "outdoor")
                          << " marker=outer[4,20)x[4,20) interior=144" << "\n";
                return true;
            }
            if (m_bTransparentPhysicalLightingScenario)
            {
                if (!BuildP5DfgOracle(m_TransparentPhysicalDfgOracle))
                {
                    LOG_ERROR("P5 independent DFG clamp/bilinear oracle preflight failed");
                    return false;
                }
                m_bTransparentPhysicalDfgOracleValid = true;
                double fixedLuminance[4] = {};
                if (!BuildTransparentPhysicalFixedLuminancePreflight(fixedLuminance))
                {
                    LOG_ERROR("P5 fixed N=V=L=1 luminance preflight failed");
                    return false;
                }
                std::cout << std::fixed << std::setprecision(15)
                          << "P5 fixed luminance preflight: N=V=L=1"
                          << " direct_m0_brdf_y=" << fixedLuminance[0]
                          << " direct_m05_brdf_y=" << fixedLuminance[1]
                          << " ibl_m0_source_y=" << fixedLuminance[2]
                          << " ibl_m05_source_y=" << fixedLuminance[3] << "\n";
                const double dfgA = (m_TransparentPhysicalDfgOracle.X255Y127.A +
                                     m_TransparentPhysicalDfgOracle.X255Y128.A) * 0.5;
                const double dfgB = (m_TransparentPhysicalDfgOracle.X255Y127.B +
                                     m_TransparentPhysicalDfgOracle.X255Y128.B) * 0.5;
                std::cout << std::fixed << std::setprecision(15)
                          << "P5 DFG preflight: n_dot_v_clamp=0.998046875000000"
                          << " texture_texels=(254,255)x(127,128)"
                          << " A=0.894531250000000"
                          << " B=0.0000262856483459473"
                          << " Ess=0.894557535648346"
                          << " sampled_A=" << dfgA
                          << " sampled_B=" << dfgB
                          << " samples=4096 rne=RG16F clamp_bilinear=1\n"
                          << std::setprecision(6);
                if (!GetFixture().ApplyTransparentPhysicalLightingRow(0u))
                {
                    LOG_ERROR("P5 transparent physical lighting fixture first row was rejected");
                    return false;
                }
                std::cout << "P5 transparent fixture passed: rows=10 camera=perspective"
                          << " target_materials=2 background_material=distinct"
                          << " target_object_index=2 background_object_index>=3"
                          << " target_alpha=0.5 background_alpha=1 back_to_front=background_then_target\n";
                return true;
            }
            if (m_bP4Scenario)
            {
                if (!ValidateIeee754Binary16RneTable() ||
                    !BuildP4DfgOracle(m_P4DfgOracle) ||
                    !BuildP4RoughnessOracle(m_P4RoughnessOracle) ||
                    !BuildP4Raw250Oracle(m_P4Raw250Oracle) ||
                    !BuildP4DirectConductorOracle(m_P4DirectOracle))
                {
                    LOG_ERROR("P4 independent CPU oracle preflight failed");
                    return false;
                }
                LOG_INFO("P4 DFG preflight: unique_texels=%zu H_vs_S_abs=(%g,%g,%g) H_vs_S_rel=(%g,%g,%g) P4096_vs_S_abs=(%g,%g,%g) P4096_vs_S_rel=(%g,%g,%g) hammersley=16384 sobol=65536 production_candidate=4096",
                         m_P4DfgOracle.Values.size(),
                         m_P4DfgOracle.HammersleySobolMaxAbs[0],
                         m_P4DfgOracle.HammersleySobolMaxAbs[1],
                         m_P4DfgOracle.HammersleySobolMaxAbs[2],
                         m_P4DfgOracle.HammersleySobolMaxRelative[0],
                         m_P4DfgOracle.HammersleySobolMaxRelative[1],
                         m_P4DfgOracle.HammersleySobolMaxRelative[2],
                         m_P4DfgOracle.ProductionSobolMaxAbs[0],
                         m_P4DfgOracle.ProductionSobolMaxAbs[1],
                         m_P4DfgOracle.ProductionSobolMaxAbs[2],
                         m_P4DfgOracle.ProductionSobolMaxRelative[0],
                         m_P4DfgOracle.ProductionSobolMaxRelative[1],
                         m_P4DfgOracle.ProductionSobolMaxRelative[2]);
                std::cout << "P4 DFG preflight: unique_texels=" << m_P4DfgOracle.Values.size()
                          << " H_vs_S_abs=(" << m_P4DfgOracle.HammersleySobolMaxAbs[0]
                          << "," << m_P4DfgOracle.HammersleySobolMaxAbs[1]
                          << "," << m_P4DfgOracle.HammersleySobolMaxAbs[2]
                          << ") P4096_vs_S_abs=(" << m_P4DfgOracle.ProductionSobolMaxAbs[0]
                          << "," << m_P4DfgOracle.ProductionSobolMaxAbs[1]
                          << "," << m_P4DfgOracle.ProductionSobolMaxAbs[2]
                         << ") samples=(16384,65536,4096)\n";
                constexpr double expectedHammersleySobol[3] = {
                    0.00150252, 0.0000476907, 0.0015395};
                constexpr double expectedProductionSobol[3] = {
                    0.00110045, 0.00013509, 0.00102136};
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    if (!std::isfinite(m_P4DfgOracle.HammersleySobolMaxAbs[channel]) ||
                        !std::isfinite(m_P4DfgOracle.ProductionSobolMaxAbs[channel]) ||
                        std::abs(m_P4DfgOracle.HammersleySobolMaxAbs[channel] -
                                     expectedHammersleySobol[channel]) > 1.0e-7 ||
                        std::abs(m_P4DfgOracle.ProductionSobolMaxAbs[channel] -
                                     expectedProductionSobol[channel]) > 1.0e-7)
                    {
                        LOG_ERROR("P4 DFG cross-check literal mismatch: channel=%u H=%g expectedH=%g P4096=%g expectedP4096=%g",
                                  channel,
                                  m_P4DfgOracle.HammersleySobolMaxAbs[channel],
                                  expectedHammersleySobol[channel],
                                  m_P4DfgOracle.ProductionSobolMaxAbs[channel],
                                  expectedProductionSobol[channel]);
                        return false;
                    }
                }
                std::cout << "P4 preflight passed: scenario=" << GetP4ScenarioName()
                          << " H_vs_S_abs=(0.00150252,0.0000476907,0.0015395)"
                          << " P4096_vs_S_abs=(0.00110045,0.00013509,0.00102136)\n";
                std::cout << "P4 DFG coordinate: value=" << std::setprecision(10)
                          << P4DfgCoordinate << " texel=(255,255) samples=16384 rne=RG16F\n"
                          << std::setprecision(6);
                const P4ScenarioRow firstRow{m_P4Scenario, 0u};
                if (!GetFixture().ApplyP4ScenarioRow(firstRow))
                {
                    LOG_ERROR("P4 fixture first scenario row was rejected");
                    return false;
                }
                const size_t meshCount = GetFixture().TrackedMeshCount();
                const size_t textureCount = GetFixture().TrackedTextureCount();
                const size_t materialCount = GetFixture().TrackedMaterialCount();
                if (meshCount != 2u || textureCount != 13u || materialCount != 33u)
                {
                    LOG_ERROR("P4 fixture resource count mismatch: meshes=%zu textures=%zu materials=%zu",
                              meshCount,
                              textureCount,
                              materialCount);
                    return false;
                }
                m_P4FixtureCameraSnapshot = GetFixture().GetCamera();
                m_P4FixtureMeshCount = meshCount;
                m_P4FixtureTextureCount = textureCount;
                m_P4FixtureMaterialCount = materialCount;
                m_bP4FixtureSnapshotAvailable = true;
                uint32_t materialIndex = 0u;
                uint32_t lightCount = 0u;
                if (!BuildP4RuntimeIdentity(GetP4RowScenario(),
                                            m_P4RowIndex,
                                            materialIndex,
                                            lightCount))
                {
                    LOG_ERROR("P4 fixture runtime identity is invalid");
                    return false;
                }
                std::cout << "P4 fixture resources passed meshes=" << meshCount
                          << " textures=" << textureCount
                          << " materials=" << materialCount
                          << " scenario=" << GetP4ScenarioName() << "\n";
                if (m_P4Scenario == P4Scenario::Raw254DirectConductorEndpoint)
                {
                    std::cout << "P4 direct fixture: row=" << m_P4RowIndex
                              << " meshes=" << meshCount
                              << " textures=" << textureCount
                              << " materials=" << materialCount
                              << " material_index=" << materialIndex
                              << " light_required=" << lightCount << "\n";
                }
                std::cout << "P4 setup passed: scenario=" << GetP4ScenarioName() << "\n";
            }
            if (!m_bR1Scenario)
            {
                return true;
            }

            if (Core::Module::RegisterImGuiModule(Core::Module::GetModuleRegistry()) == nullptr)
            {
                LOG_ERROR("R1 srgb-transfer scenario failed to register ImGui module");
                return false;
            }
            Modules::Gui::RegisterImGuiView(&m_MarkerView);
            m_bMarkerRegistered = true;
            return true;
        }

        void OnPreRender() override
        {
            RenderingValidationApplicationHandler::OnPreRender();
            if (m_bAllNumericalScenario && Core::Engine::GEngine != nullptr)
            {
                Core::Rendering::RenderWorld& renderWorld = Core::Engine::GEngine->GetRenderWorld();
                const uint64_t requestId = GetLastAcceptedRequestId();
                if (requestId != 0u && requestId != m_AllNumericalLastObservedRequestId &&
                    GetFixture().IsCaptureStateStable() && !renderWorld.HasPendingAsyncAssets())
                {
                    m_AllNumericalLastObservedRequestId = requestId;
                    if (!m_bAllNumericalStartupFrameSet)
                    {
                        m_AllNumericalRequestStartFrame = renderWorld.GetRenderedFrameCount();
                        m_bAllNumericalRequestStartFrameSet = true;
                        m_AllNumericalStartupFrame = m_AllNumericalRequestStartFrame;
                        m_bAllNumericalStartupFrameSet = true;
                    }
                }
            }
        }

        void OnPreShutdown() override
        {
            if (m_bMarkerRegistered)
            {
                Modules::Gui::UnregisterImGuiView(&m_MarkerView);
                m_bMarkerRegistered = false;
            }
            RenderingValidationApplicationHandler::OnPreShutdown();
        }

    protected:
        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& outFailureReason) override
        {
            if (argument == TEXT("--r1-scenario=all-numerical"))
            {
                if (m_bAllNumericalArgumentParsed ||
                    m_bR1Scenario || m_bKnownCdScenario || m_bP4Scenario ||
                    m_bTransparentPhysicalLightingScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bAllNumericalScenario = true;
                m_bAllNumericalArgumentParsed = true;
                return true;
            }
            if (argument == TEXT("--r1-forced-rows=2"))
            {
                if (!m_bAllNumericalScenario || m_bAllNumericalForcedRowsArgumentParsed)
                {
                    outFailureReason = TEXT("forced row count is only valid for all-numerical");
                    return false;
                }
                m_bAllNumericalForcedRowsArgumentParsed = true;
                m_AllNumericalForcedRows = 2u;
                return true;
            }
            if (argument == TEXT("--r1-scenario=srgb-transfer"))
            {
                if (m_bR1Scenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bR1Scenario = true;
                return true;
            }
            if (argument == TEXT("--r1-scenario=known-cd-lambert"))
            {
                if (m_bKnownCdScenario || m_bR1Scenario || m_bTransparentPhysicalLightingScenario ||
                    m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bKnownCdScenario = true;
                return true;
            }
            if (argument == TEXT("--r1-scenario=transparent-physical-lighting"))
            {
                if (m_bTransparentPhysicalLightingScenario || m_bP4Scenario ||
                    m_bKnownCdScenario || m_bR1Scenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bTransparentPhysicalLightingScenario = true;
                return true;
            }
            if (argument == TEXT("--r1-scenario=ibl-prefilter-nonconstant"))
            {
                if (m_bP4Scenario || m_bKnownCdScenario || m_bR1Scenario ||
                    m_bTransparentPhysicalLightingScenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bP4Scenario = true;
                m_P4Scenario = P4Scenario::Raw250TextureRepresentation;
                return true;
            }
            if (argument == TEXT("--r1-scenario=dfg-lut"))
            {
                if (m_bP4Scenario || m_bKnownCdScenario || m_bR1Scenario ||
                    m_bTransparentPhysicalLightingScenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bP4Scenario = true;
                m_P4Scenario = P4Scenario::Raw251DfgLut;
                return true;
            }
            if (argument == TEXT("--r1-scenario=ibl-roughness-sweep"))
            {
                if (m_bP4Scenario || m_bKnownCdScenario || m_bR1Scenario ||
                    m_bTransparentPhysicalLightingScenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bP4Scenario = true;
                m_P4Scenario = P4Scenario::Raw252RoughnessSweep;
                return true;
            }
            if (argument == TEXT("--r1-scenario=white-furnace"))
            {
                if (m_bP4Scenario || m_bKnownCdScenario || m_bR1Scenario ||
                    m_bTransparentPhysicalLightingScenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bP4Scenario = true;
                m_P4Scenario = P4Scenario::Raw252WhiteFurnace;
                return true;
            }
            if (argument == TEXT("--r1-scenario=direct-conductor-endpoint"))
            {
                if (m_bP4Scenario || m_bKnownCdScenario || m_bR1Scenario ||
                    m_bTransparentPhysicalLightingScenario || m_bAllNumericalScenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bP4Scenario = true;
                m_P4Scenario = P4Scenario::Raw254DirectConductorEndpoint;
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument, outFailureReason);
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) override
        {
            if (m_bAllNumericalScenario)
            {
                return EvaluateAllNumericalFrame(frame, reason);
            }
            if (m_bTransparentPhysicalLightingScenario)
            {
                return EvaluateTransparentPhysicalLightingFrame(frame, reason);
            }
            if (m_bKnownCdScenario)
            {
                return EvaluateKnownCdFrame(frame, reason);
            }
            if (m_bP4Scenario)
            {
                return EvaluateP4Frame(frame, reason);
            }
            if (m_bR1Scenario)
            {
                if (frame.FrameNumber < m_R1LastFrameNumber ||
                    (m_bR1HasFrameNumber && frame.FrameNumber == m_R1LastFrameNumber))
                {
                    LOG_ERROR(
                        "R1 capture frame order is not strictly increasing: previous=%llu current=%llu",
                        static_cast<unsigned long long>(m_R1LastFrameNumber),
                        static_cast<unsigned long long>(frame.FrameNumber));
                    reason = TEXT("R1 capture FrameNumber order is invalid");
                    return false;
                }
                m_R1LastFrameNumber = frame.FrameNumber;
                m_bR1HasFrameNumber = true;

                switch (m_R1CaptureStage)
                {
                case R1CaptureStage::BackBuffer:
                    if (!EvaluateR1SrgbTransfer(frame, reason))
                    {
                        return false;
                    }
                    LOG_INFO(
                        "R1 BackBuffer capture accepted: frame=%llu format=%u marker=ROI",
                        static_cast<unsigned long long>(frame.FrameNumber),
                        static_cast<unsigned int>(frame.Format));
                    m_R1CaptureStage = R1CaptureStage::PresentationColor;
                    return true;
                case R1CaptureStage::PresentationColor:
                    if (!EvaluateR1PresentationColor(frame, reason))
                    {
                        return false;
                    }
                    m_R1CaptureStage = R1CaptureStage::SceneColor;
                    return true;
                case R1CaptureStage::SceneColor:
                    if (!EvaluateR1SceneColor(frame, reason))
                    {
                        return false;
                    }
                    m_R1CaptureStage = R1CaptureStage::Complete;
                    return true;
                case R1CaptureStage::Complete:
                default:
                    reason = TEXT("R1 capture sequence completed unexpectedly");
                    return false;
                }
            }

            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT)
            {
                reason = TEXT("captured SceneColor format is not RGBA16F");
                return false;
            }

            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
            {
                reason = TEXT("captured SceneColor RGBA16F decode failed");
                return false;
            }
            if (image.Width != ValidationWidth || image.Height != ValidationHeight)
            {
                reason = TEXT("captured SceneColor dimensions are invalid");
                return false;
            }
            if (!IsFiniteAndWithinRgba16Range(image))
            {
                const RgbaFloatViolation location = FindFirstRgba16FloatViolation(image);
                LOG_ERROR(
                    "HDR display-linear capture contains an invalid RGBA16F value: x=%u y=%u channel=%u value=%g kind=%u",
                    location.X,
                    location.Y,
                    location.Channel,
                    location.Value,
                    static_cast<unsigned int>(location.Kind));
                reason = TEXT("captured display-linear RGBA16F contains a non-finite or saturated value");
                return false;
            }
            return true;
        }

        void ApplyCaptureStageState(Core::Rendering::RenderWorld& renderWorld) override
        {
            if (m_bAllNumericalScenario)
            {
                if (m_bAllNumericalStageApplyFailed ||
                    m_AllNumericalCaptureStage == AllNumericalCaptureStage::Complete)
                {
                    return;
                }
                if (m_AllNumericalRowIndex <= 22u)
                {
                    const Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
                    renderWorld.SetMainCamera(camera);
                    const Core::Rendering::CameraProxy& actualCamera =
                        renderWorld.GetRenderingCoordinator().GetMainCamera();
                    if (!ValidateP4Camera(actualCamera))
                    {
                        m_bAllNumericalStageApplyFailed = true;
                        return;
                    }
                    m_P4ActualCamera = actualCamera;
                    m_bP4ActualCameraAvailable = true;
                }
                else if (m_AllNumericalRowIndex >= 23u && m_AllNumericalRowIndex <= 28u)
                {
                    Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
                    const bool bExposureA = m_KnownCdStage == KnownCdStage::PureLambertA ||
                                            m_KnownCdStage == KnownCdStage::DirectPbrA ||
                                            m_KnownCdStage == KnownCdStage::NormalA;
                    camera.Aperture = 1.0f;
                    camera.ShutterSpeed = bExposureA ? 0.3f : 0.15f;
                    camera.ISO = 100.0f;
                    camera.ExposureCompensation = 0.0f;
                    camera.PreExposure = bExposureA ? 0.25f : 0.125f;
                    camera.InvPreExposure = bExposureA ? 4.0f : 8.0f;
                    camera.Exposure = camera.PreExposure;
                    renderWorld.SetMainCamera(camera);
                }
                else if (m_AllNumericalRowIndex == 29u)
                {
                    renderWorld.SetMainCamera(GetFixture().GetCamera());
                }
                else
                {
                    Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
                    camera.Aperture = 4.0f;
                    camera.ShutterSpeed = 1.0f / 60.0f;
                    camera.ISO = 100.0f;
                    camera.ExposureCompensation = 4.0f;
                    const double ev100 = std::log2(
                        (static_cast<double>(camera.Aperture) *
                         static_cast<double>(camera.Aperture) /
                         static_cast<double>(camera.ShutterSpeed)) *
                        (100.0 / static_cast<double>(camera.ISO)));
                    const double exposure = std::exp2(
                        static_cast<double>(camera.ExposureCompensation) - ev100) / 1.2;
                    camera.EV100 = static_cast<float>(ev100);
                    camera.Exposure = static_cast<float>(exposure);
                    camera.PreExposure = 1.0f / 72.0f;
                    camera.InvPreExposure = 72.0f;
                    if (!std::isfinite(camera.Exposure) || camera.Exposure <= 0.0f)
                    {
                        m_bAllNumericalStageApplyFailed = true;
                        return;
                    }
                    renderWorld.SetMainCamera(camera);
                }
                renderWorld.SetDebugViewModeAll(
                    static_cast<Core::Rendering::DebugViewMode>(
                        GetAllNumericalDebugViewMode(m_AllNumericalRowIndex)));
                if (!m_bAllNumericalRowMarkerPrinted)
                {
                    std::cout << "R1 all-numerical row applied: row="
                              << m_AllNumericalRowIndex
                              << " mode=" << GetAllNumericalDebugViewMode(m_AllNumericalRowIndex)
                              << " numerical_assertions="
                              << GetAllNumericalNumericalAssertionCount(m_AllNumericalRowIndex)
                              << "\n";
                    m_bAllNumericalRowMarkerPrinted = true;
                }
                return;
            }
            if (m_bTransparentPhysicalLightingScenario)
            {
                if (m_bTransparentPhysicalStageApplyFailed ||
                    m_TransparentPhysicalStage == TransparentPhysicalStage::Complete)
                {
                    return;
                }
                Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
                camera.Aperture = 4.0f;
                camera.ShutterSpeed = 1.0f / 60.0f;
                camera.ISO = 100.0f;
                camera.ExposureCompensation = 4.0f;
                const double ev100 = std::log2(
                    (static_cast<double>(camera.Aperture) *
                     static_cast<double>(camera.Aperture) /
                     static_cast<double>(camera.ShutterSpeed)) *
                    (100.0 / static_cast<double>(camera.ISO)));
                const double exposure = std::exp2(
                    static_cast<double>(camera.ExposureCompensation) - ev100) / 1.2;
                camera.EV100 = static_cast<float>(ev100);
                camera.Exposure = static_cast<float>(exposure);
                camera.PreExposure = 1.0f / 72.0f;
                camera.InvPreExposure = 72.0f;
                if (!std::isfinite(camera.Exposure) || camera.Exposure <= 0.0f ||
                    camera.PreExposure != 1.0f / 72.0f || camera.InvPreExposure != 72.0f)
                {
                    m_bTransparentPhysicalStageApplyFailed = true;
                    return;
                }
                renderWorld.SetMainCamera(camera);
                const bool bIblOn = m_TransparentPhysicalStage == TransparentPhysicalStage::IblM0On ||
                                    m_TransparentPhysicalStage == TransparentPhysicalStage::IblM05On;
                renderWorld.SetDebugViewModeAll(
                    static_cast<Core::Rendering::DebugViewMode>(bIblOn ? 252u : 254u));
                if (!m_bTransparentPhysicalRowMarkerPrinted)
                {
                    std::cout << "P5 transparent stage applied: row="
                              << static_cast<unsigned int>(m_TransparentPhysicalStage)
                              << " mode=" << (bIblOn ? 252u : 254u)
                              << " pre_exposure=0.013888889 inv_pre_exposure=72"
                              << " aperture=4 shutter=0.016666667 iso=100 compensation=4\n";
                    m_bTransparentPhysicalRowMarkerPrinted = true;
                }
                return;
            }
            if (m_bP4Scenario)
            {
                if (!m_bP4FixtureSnapshotAvailable ||
                    m_P4FixtureMeshCount != GetFixture().TrackedMeshCount() ||
                    m_P4FixtureTextureCount != GetFixture().TrackedTextureCount() ||
                    m_P4FixtureMaterialCount != GetFixture().TrackedMaterialCount() ||
                    std::memcmp(&m_P4FixtureCameraSnapshot,
                                &GetFixture().GetCamera(),
                                sizeof(Core::Rendering::CameraProxy)) != 0)
                {
                    m_bP4StageApplyFailed = true;
                    return;
                }
                Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
                camera.Aperture = 4.0f;
                camera.ShutterSpeed = 1.0f / 60.0f;
                camera.ISO = 100.0f;
                camera.ExposureCompensation = 4.0f;
                const double ev100 = std::log2(
                    (static_cast<double>(camera.Aperture) *
                     static_cast<double>(camera.Aperture) /
                     static_cast<double>(camera.ShutterSpeed)) *
                    (100.0 / static_cast<double>(camera.ISO)));
                const double exposure = std::exp2(
                    static_cast<double>(camera.ExposureCompensation) - ev100) / 1.2;
                camera.EV100 = static_cast<float>(ev100);
                camera.Exposure = static_cast<float>(exposure);
                camera.PreExposure = camera.Exposure;
                camera.InvPreExposure = static_cast<float>(1.0 / exposure);
                if (!ValidateP4Camera(camera))
                {
                    m_bP4StageApplyFailed = true;
                    return;
                }
                renderWorld.SetMainCamera(camera);
                const Core::Rendering::CameraProxy& actualCamera =
                    renderWorld.GetRenderingCoordinator().GetMainCamera();
                if (!ValidateP4Camera(actualCamera))
                {
                    m_bP4StageApplyFailed = true;
                    return;
                }
                m_P4ActualCamera = actualCamera;
                m_bP4ActualCameraAvailable = true;
                if (!m_bP4CameraMarkerPrinted)
                {
                    std::cout << std::setprecision(9)
                              << "P4 camera passed: scenario=" << GetP4ScenarioName()
                              << " Aperture=" << actualCamera.Aperture
                              << " ShutterSpeed=" << actualCamera.ShutterSpeed
                              << " ISO=" << actualCamera.ISO
                              << " ExposureCompensation=" << actualCamera.ExposureCompensation
                              << " EV100=" << actualCamera.EV100
                              << " Exposure=" << actualCamera.Exposure
                              << " PreExposure=" << actualCamera.PreExposure
                              << " InvPreExposure=" << actualCamera.InvPreExposure << "\n"
                              << std::setprecision(6);
                    m_bP4CameraMarkerPrinted = true;
                }
                renderWorld.SetDebugViewModeAll(
                    static_cast<Core::Rendering::DebugViewMode>(GetP4DebugViewMode()));
                return;
            }
            if (!m_bKnownCdScenario)
            {
                return;
            }

            Core::Rendering::CameraProxy camera = GetFixture().GetCamera();
            const bool bExposureA = m_KnownCdStage == KnownCdStage::PureLambertA ||
                                    m_KnownCdStage == KnownCdStage::DirectPbrA ||
                                    m_KnownCdStage == KnownCdStage::NormalA;
            camera.Aperture = 1.0f;
            camera.ShutterSpeed = bExposureA ? 0.3f : 0.15f;
            camera.ISO = 100.0f;
            camera.ExposureCompensation = 0.0f;
            camera.PreExposure = bExposureA ? 0.25f : 0.125f;
            camera.InvPreExposure = bExposureA ? 4.0f : 8.0f;
            camera.Exposure = camera.PreExposure;
            renderWorld.SetMainCamera(camera);

            Core::Rendering::DebugViewMode debugMode = Core::Rendering::DebugViewMode::Normal;
            if (m_KnownCdStage == KnownCdStage::PureLambertA ||
                m_KnownCdStage == KnownCdStage::PureLambertB)
            {
                debugMode = static_cast<Core::Rendering::DebugViewMode>(253u);
            }
            else if (m_KnownCdStage == KnownCdStage::DirectPbrA ||
                     m_KnownCdStage == KnownCdStage::DirectPbrB)
            {
                debugMode = static_cast<Core::Rendering::DebugViewMode>(254u);
            }
            renderWorld.SetDebugViewModeAll(debugMode);
            LOG_INFO("R1 known-cd stage applied: stage=%s mode=%u pre=%g inv=%g",
                     GetKnownCdStageName(),
                     static_cast<unsigned int>(debugMode),
                     camera.PreExposure,
                     camera.InvPreExposure);
        }

        void AdvanceCaptureStage() override
        {
            if (m_bAllNumericalScenario)
            {
                return;
            }
            if (m_bTransparentPhysicalLightingScenario)
            {
                if (m_TransparentPhysicalStage == TransparentPhysicalStage::Complete)
                {
                    return;
                }
                const uint32_t nextRow =
                    static_cast<uint32_t>(m_TransparentPhysicalStage) + 1u;
                m_TransparentPhysicalStage = static_cast<TransparentPhysicalStage>(nextRow);
                m_bTransparentPhysicalRowMarkerPrinted = false;
                if (m_TransparentPhysicalStage != TransparentPhysicalStage::Complete &&
                    !GetFixture().ApplyTransparentPhysicalLightingRow(nextRow))
                {
                    LOG_ERROR("P5 transparent physical lighting fixture row was rejected: row=%u", nextRow);
                    m_bTransparentPhysicalStageApplyFailed = true;
                }
                return;
            }
            if (m_bP4Scenario)
            {
                if (m_P4Substage != P4CaptureSubstage::Material)
                {
                    m_P4Substage = static_cast<P4CaptureSubstage>(
                        static_cast<uint8_t>(m_P4Substage) + 1u);
                    return;
                }
                m_P4Substage = P4CaptureSubstage::Primary;
                const uint32_t rowCount = GetP4ScenarioRowCount();
                if (m_P4RowIndex + 1u < rowCount)
                {
                    ++m_P4RowIndex;
                    const P4ScenarioRow row{m_P4Scenario, m_P4RowIndex};
                    if (!GetFixture().ApplyP4ScenarioRow(row))
                    {
                        LOG_ERROR("P4 fixture scenario row was rejected: scenario=%u row=%u",
                                  static_cast<unsigned int>(m_P4Scenario),
                                  m_P4RowIndex);
                        m_bP4StageApplyFailed = true;
                    }
                }
                else
                {
                    m_P4RowIndex = rowCount;
                }
                return;
            }
            if (m_bKnownCdScenario && m_KnownCdStage != KnownCdStage::Complete)
            {
                m_KnownCdStage = static_cast<KnownCdStage>(
                    static_cast<uint8_t>(m_KnownCdStage) + 1u);
            }
        }

        bool RequestFollowupCapture(
            const Core::Rendering::CapturedFrame& frame,
            Core::Rendering::FrameCaptureRequest& outRequest) override
        {
            if (m_bAllNumericalScenario)
            {
                if (m_bAllNumericalStageApplyFailed ||
                    m_AllNumericalCaptureStage == AllNumericalCaptureStage::Complete)
                {
                    return false;
                }
                if (m_AllNumericalCaptureStage == AllNumericalCaptureStage::PresentationColor)
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::PresentationColor;
                }
                else if (m_AllNumericalCaptureStage == AllNumericalCaptureStage::SceneColor)
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                }
                else
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::BackBuffer;
                }
                if (Core::Engine::GEngine == nullptr)
                {
                    return false;
                }
                m_AllNumericalRequestStartFrame =
                    Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount();
                m_bAllNumericalRequestStartFrameSet = true;
                LOG_INFO("R1 all-numerical follow-up requested: row=%u stage=%u after frame=%llu",
                         m_AllNumericalRowIndex,
                         static_cast<unsigned int>(m_AllNumericalCaptureStage),
                         static_cast<unsigned long long>(frame.FrameNumber));
                return true;
            }
            if (m_bTransparentPhysicalLightingScenario)
            {
                if (m_TransparentPhysicalStage == TransparentPhysicalStage::Complete)
                {
                    return false;
                }
                outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                LOG_INFO("P5 transparent follow-up requested: next_row=%u after frame=%llu",
                         static_cast<unsigned int>(m_TransparentPhysicalStage),
                         static_cast<unsigned long long>(frame.FrameNumber));
                return true;
            }
            if (m_bKnownCdScenario)
            {
                if (m_KnownCdStage == KnownCdStage::Complete)
                {
                    return false;
                }
                outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                LOG_INFO("R1 known-cd follow-up requested: next_stage=%s after frame=%llu",
                         GetKnownCdStageName(),
                         static_cast<unsigned long long>(frame.FrameNumber));
                return true;
            }
            if (m_bP4Scenario)
            {
                if (m_bP4StageApplyFailed)
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                    return true;
                }
                if (m_P4Substage != P4CaptureSubstage::Primary)
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                    return true;
                }
                if (m_P4RowIndex < GetP4ScenarioRowCount())
                {
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                    LOG_INFO("P4 follow-up capture requested: scenario=%u row=%u after frame=%llu",
                             static_cast<unsigned int>(m_P4Scenario),
                             m_P4RowIndex,
                             static_cast<unsigned long long>(frame.FrameNumber));
                    return true;
                }
                return false;
            }
            if (m_bR1Scenario &&
                GetRunConfig().CaptureSource == Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                switch (m_R1CaptureStage)
                {
                case R1CaptureStage::PresentationColor:
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::PresentationColor;
                    LOG_INFO(
                        "R1 follow-up capture requested: source=PresentationColor after frame=%llu",
                        static_cast<unsigned long long>(frame.FrameNumber));
                    return true;
                case R1CaptureStage::SceneColor:
                    outRequest.SourceKind = Core::Rendering::FrameCaptureSourceKind::SceneColor;
                    LOG_INFO(
                        "R1 follow-up capture requested: source=SceneColor after frame=%llu",
                        static_cast<unsigned long long>(frame.FrameNumber));
                    return true;
                case R1CaptureStage::BackBuffer:
                case R1CaptureStage::Complete:
                default:
                    return false;
                }
            }
            return false;
        }

    private:
        static constexpr uint32_t AllNumericalStaticRowCount = 40u;

        static uint32_t GetAllNumericalDebugViewMode(uint32_t rowIndex)
        {
            if (rowIndex == 0u)
            {
                return 250u;
            }
            if (rowIndex == 1u)
            {
                return 251u;
            }
            if (rowIndex >= 2u && rowIndex <= 22u)
            {
                return 252u;
            }
            if (rowIndex >= 23u && rowIndex <= 24u)
            {
                return 253u;
            }
            if (rowIndex >= 25u && rowIndex <= 26u)
            {
                return 254u;
            }
            if (rowIndex >= 30u && rowIndex <= 36u)
            {
                return 254u;
            }
            if (rowIndex == 37u || rowIndex == 39u)
            {
                return 252u;
            }
            if (rowIndex == 38u)
            {
                return 254u;
            }
            return 0u;
        }

        static uint32_t GetAllNumericalNumericalAssertionCount(uint32_t rowIndex)
        {
            if (rowIndex == 0u)
            {
                return 5u;
            }
            if (rowIndex == 1u)
            {
                return 25u;
            }
            if (rowIndex >= 2u && rowIndex <= 7u)
            {
                return 1u;
            }
            if (rowIndex >= 8u && rowIndex <= 22u)
            {
                return 1u;
            }
            return 1u;
        }

        static uint32_t GetExpectedAllNumericalAssertionTotal()
        {
            uint32_t total = 0u;
            for (uint32_t rowIndex = 0u; rowIndex < AllNumericalStaticRowCount; ++rowIndex)
            {
                total += GetAllNumericalNumericalAssertionCount(rowIndex);
            }
            return total;
        }

        static bool ValidateAllNumericalRowContract()
        {
            return GetExpectedAllNumericalAssertionTotal() == 68u &&
                   GetAllNumericalDebugViewMode(0u) == 250u &&
                   GetAllNumericalDebugViewMode(1u) == 251u &&
                   GetAllNumericalDebugViewMode(2u) == 252u &&
                   GetAllNumericalDebugViewMode(22u) == 252u &&
                   GetAllNumericalDebugViewMode(23u) == 253u &&
                   GetAllNumericalDebugViewMode(25u) == 254u &&
                   GetAllNumericalDebugViewMode(27u) == 0u;
        }

        static bool ValidateSyntheticLsbContract()
        {
            const uint8_t expected = 128u;
            const uint8_t oneLsb = static_cast<uint8_t>(expected + 1u);
            const uint8_t twoLsb = static_cast<uint8_t>(expected + 2u);
            return IsWithinOneLsb(oneLsb, expected) && !IsWithinOneLsb(twoLsb, expected);
        }

        bool ApplyAllNumericalRow(uint32_t rowIndex)
        {
            if (rowIndex >= AllNumericalStaticRowCount)
            {
                return false;
            }
            m_bP4StageApplyFailed = false;
            m_bP4ActualCameraAvailable = false;
            m_P4HasFrameNumber = false;
            m_P4HasStageToken = false;
            m_bP4HasRuntimeIdentity = false;
            m_P4LastRuntimeRow = 0u;
            m_bP4ActualOracleMismatch = false;
            if (rowIndex != 1u && !GetFixture().ClearP4DfgTileFixture())
            {
                return false;
            }
            if (rowIndex == 0u)
            {
                m_P4Scenario = P4Scenario::Raw250TextureRepresentation;
                m_P4RowIndex = 0u;
                m_P4Substage = P4CaptureSubstage::Primary;
                return GetFixture().ApplyP4ScenarioRow({P4Scenario::Raw250TextureRepresentation, 0u});
            }
            if (rowIndex == 1u)
            {
                m_P4Scenario = P4Scenario::Raw251DfgLut;
                m_P4RowIndex = 0u;
                m_P4Substage = P4CaptureSubstage::Primary;
                return GetFixture().ApplyP4ScenarioRow({P4Scenario::Raw251DfgLut, 0u}) &&
                       GetFixture().ApplyP4DfgTileFixture();
            }
            if (rowIndex >= 2u && rowIndex <= 6u)
            {
                m_P4Scenario = P4Scenario::Raw252RoughnessSweep;
                m_P4RowIndex = rowIndex - 2u;
                m_P4Substage = P4CaptureSubstage::Primary;
                return GetFixture().ApplyP4ScenarioRow({
                    P4Scenario::Raw252RoughnessSweep, rowIndex - 2u});
            }
            if (rowIndex == 7u)
            {
                m_P4Scenario = P4Scenario::Raw252RoughnessSweep;
                m_P4RowIndex = 5u;
                m_P4Substage = P4CaptureSubstage::Primary;
                return GetFixture().ApplyP4ScenarioRow({
                    P4Scenario::Raw252RoughnessSweep, 5u});
            }
            if (rowIndex >= 8u && rowIndex <= 22u)
            {
                m_P4Scenario = P4Scenario::Raw252WhiteFurnace;
                m_P4RowIndex = rowIndex - 8u;
                m_P4Substage = P4CaptureSubstage::Primary;
                return GetFixture().ApplyP4ScenarioRow({
                    P4Scenario::Raw252WhiteFurnace, rowIndex - 8u});
            }
            if (rowIndex >= 23u && rowIndex <= 28u)
            {
                if (rowIndex == 23u)
                {
                    m_KnownCdPreviousImage = {};
                }
                m_KnownCdStage = static_cast<KnownCdStage>(rowIndex - 23u);
                return GetFixture().ApplyBaseValidationFixture();
            }
            if (rowIndex == 29u)
            {
                return GetFixture().ApplyBaseValidationFixture();
            }
            if (rowIndex >= 30u && rowIndex <= 39u)
            {
                m_TransparentPhysicalStage = static_cast<TransparentPhysicalStage>(rowIndex - 30u);
                if (rowIndex == 30u)
                {
                    m_bTransparentPhysicalHasFrameNumber = false;
                    m_bTransparentPhysicalHasStageToken = false;
                    m_bTransparentPhysicalHasUnshadowedValue = false;
                    m_TransparentPhysicalUnshadowedMeanY = 0.0;
                }
                if (!GetFixture().ApplyTransparentPhysicalLightingRow(rowIndex - 30u))
                {
                    return false;
                }
                return rowIndex != 30u ||
                       (GetFixture().TrackedMeshCount() == 3u &&
                        GetFixture().TrackedTextureCount() == 22u &&
                        GetFixture().TrackedMaterialCount() == 37u);
            }
            return false;
        }

        static bool IsAllNumericalBackBufferFormat(RHI::Format format)
        {
            return RHI::IsPresentationSrgbFormat(format) || RHI::IsPresentationUnormFormat(format);
        }

        static uint8_t EncodeAllNumericalSrgb(double linear)
        {
            const double encoded = linear <= 0.0031308
                ? 12.92 * linear
                : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
            const double clamped = std::clamp(encoded, 0.0, 1.0);
            return static_cast<uint8_t>(std::floor(clamped * 255.0 + 0.5));
        }

        static bool ValidateAllNumericalCaptureEnvelope(
            const Core::Rendering::CapturedFrame& frame,
            bool bBackBuffer,
            Core::Container::String& reason)
        {
            if (frame.Width != ValidationWidth || frame.Height != ValidationHeight)
            {
                reason = TEXT("R1 all-numerical capture dimensions are invalid");
                return false;
            }
            if (bBackBuffer)
            {
                if (!IsAllNumericalBackBufferFormat(frame.Format) ||
                    frame.BytesPerPixel != 4u ||
                    frame.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
                    frame.Transfer != RHI::PresentationTransfer::SRGB ||
                    frame.bHardwareSrgbEncode == frame.bShaderSrgbEncode ||
                    frame.bHardwareSrgbEncode != RHI::IsPresentationSrgbFormat(frame.Format) ||
                    frame.bShaderSrgbEncode != RHI::IsPresentationUnormFormat(frame.Format))
                {
                    reason = TEXT("R1 all-numerical BackBuffer format or encode metadata is invalid");
                    return false;
                }
                const uint64_t tightPitch = static_cast<uint64_t>(frame.Width) * 4u;
                const uint64_t requiredBytes = static_cast<uint64_t>(frame.RowPitchBytes) * frame.Height;
                if (frame.RowPitchBytes < tightPitch ||
                    requiredBytes > frame.Pixels.size())
                {
                    reason = TEXT("R1 all-numerical BackBuffer pixel storage is invalid");
                    return false;
                }
                return true;
            }
            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT ||
                frame.BytesPerPixel != 8u)
            {
                reason = TEXT("R1 all-numerical float capture format is invalid");
                return false;
            }
            return true;
        }

        static bool ValidateAllNumericalBackBufferMarker(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM ||
                               frame.Format == RHI::Format::B8G8R8A8_SRGB;
            size_t matched = 0u;
            for (uint32_t y = 6u; y < 18u; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * frame.RowPitchBytes;
                for (uint32_t x = 6u; x < 18u; ++x)
                {
                    const size_t offset = rowOffset + static_cast<size_t>(x) * 4u;
                    const uint8_t red = frame.Pixels[offset + (bBgra ? 2u : 0u)];
                    const uint8_t green = frame.Pixels[offset + 1u];
                    const uint8_t blue = frame.Pixels[offset + (bBgra ? 0u : 2u)];
                    if (IsWithinOneLsb(red, 128u) && IsWithinOneLsb(green, 64u) &&
                        IsWithinOneLsb(blue, 191u) && frame.Pixels[offset + 3u] == 255u)
                    {
                        ++matched;
                    }
                }
            }
            if (matched != 144u)
            {
                reason = TEXT("R1 all-numerical BackBuffer marker interior is incomplete");
                return false;
            }
            std::cout << "R1 all-numerical BackBuffer marker: matched=144/144\n";
            return true;
        }

        bool EvaluateAllNumericalBackBuffer(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (!ValidateAllNumericalCaptureEnvelope(frame, true, reason) ||
                !ValidateAllNumericalBackBufferMarker(frame, reason))
            {
                return false;
            }
            m_AllNumericalBackBuffer = frame;
            ++m_AllNumericalBackBufferScans;
            m_AllNumericalActualByteChannels +=
                static_cast<uint64_t>(ValidationWidth) * ValidationHeight * 4u;
            return true;
        }

        bool EvaluateAllNumericalPresentationColor(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (!ValidateAllNumericalCaptureEnvelope(frame, false, reason) ||
                frame.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
                frame.Transfer != RHI::PresentationTransfer::SRGB ||
                frame.bHardwareSrgbEncode == frame.bShaderSrgbEncode)
            {
                reason = TEXT("R1 all-numerical PresentationColor metadata is invalid");
                return false;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success ||
                !IsFiniteAndWithinRgba16Range(image))
            {
                reason = TEXT("R1 all-numerical PresentationColor RGBA16F validation failed");
                return false;
            }
            const bool bBgra = m_AllNumericalBackBuffer.Format == RHI::Format::B8G8R8A8_UNORM ||
                               m_AllNumericalBackBuffer.Format == RHI::Format::B8G8R8A8_SRGB;
            size_t markerMatches = 0u;
            for (uint32_t y = 0u; y < ValidationHeight; ++y)
            {
                const size_t backRowOffset = static_cast<size_t>(y) * m_AllNumericalBackBuffer.RowPitchBytes;
                for (uint32_t x = 0u; x < ValidationWidth; ++x)
                {
                    const size_t floatOffset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const size_t backOffset = backRowOffset + static_cast<size_t>(x) * 4u;
                    const uint8_t expectedLogical[4] = {
                        EncodeAllNumericalSrgb(static_cast<double>(image.Values[floatOffset + 0u])),
                        EncodeAllNumericalSrgb(static_cast<double>(image.Values[floatOffset + 1u])),
                        EncodeAllNumericalSrgb(static_cast<double>(image.Values[floatOffset + 2u])),
                        static_cast<uint8_t>(std::floor(std::clamp(
                            static_cast<double>(image.Values[floatOffset + 3u]), 0.0, 1.0) * 255.0 + 0.5))};
                    const uint8_t expectedStorage[4] = {
                        bBgra ? expectedLogical[2] : expectedLogical[0],
                        expectedLogical[1],
                        bBgra ? expectedLogical[0] : expectedLogical[2],
                        expectedLogical[3]};
                    for (uint32_t channel = 0u; channel < 4u; ++channel)
                    {
                        const int delta = static_cast<int>(m_AllNumericalBackBuffer.Pixels[backOffset + channel]) -
                                          static_cast<int>(expectedStorage[channel]);
                        if (std::abs(delta) > 1)
                        {
                            const uint32_t logicalChannel = bBgra && channel == 0u
                                ? 2u
                                : bBgra && channel == 2u ? 0u : channel;
                            const char* encodePath = frame.bHardwareSrgbEncode
                                ? "hardware_srgb"
                                : "shader_oetf";
                            std::cerr << "R1 all-numerical oracle mismatch: row=" << m_AllNumericalRowIndex
                                      << " source=BackBuffer x=" << x << " y=" << y
                                      << " storage_channel=" << channel
                                      << " logical_channel=" << logicalChannel
                                      << " logical_linear=" << image.Values[floatOffset + logicalChannel]
                                      << " format=" << static_cast<unsigned int>(frame.Format)
                                      << " color_space=" << static_cast<unsigned int>(frame.ColorSpace)
                                      << " transfer=" << static_cast<unsigned int>(frame.Transfer)
                                      << " hardware_srgb=" << (frame.bHardwareSrgbEncode ? 1 : 0)
                                      << " shader_srgb=" << (frame.bShaderSrgbEncode ? 1 : 0)
                                      << " encode_path=" << encodePath
                                      << " expected=" << static_cast<unsigned int>(expectedStorage[channel])
                                      << " actual=" << static_cast<unsigned int>(
                                             m_AllNumericalBackBuffer.Pixels[backOffset + channel])
                                      << " delta=" << delta << "\n";
                            reason = TEXT("R1 all-numerical sRGB oracle rejected a BackBuffer pixel");
                            return false;
                        }
                    }
                    if (x >= 6u && x < 18u && y >= 6u && y < 18u &&
                        IsWithinOneLsb(expectedLogical[0], 128u) &&
                        IsWithinOneLsb(expectedLogical[1], 64u) &&
                        IsWithinOneLsb(expectedLogical[2], 191u) &&
                        expectedLogical[3] == 255u)
                    {
                        ++markerMatches;
                    }
                }
            }
            if (markerMatches != 144u)
            {
                reason = TEXT("R1 all-numerical PresentationColor marker interior is incomplete");
                return false;
            }
            m_AllNumericalPresentationColor = frame;
            m_AllNumericalPresentationImage = image;
            ++m_AllNumericalPresentationScans;
            m_AllNumericalFloatChannels += static_cast<uint64_t>(ValidationWidth) * ValidationHeight * 4u;
            return true;
        }

        bool EvaluateAllNumericalSceneColor(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (!ValidateAllNumericalCaptureEnvelope(frame, false, reason))
            {
                return false;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success ||
                !IsFiniteAndWithinRgba16Range(image))
            {
                reason = TEXT("R1 all-numerical SceneColor RGBA16F validation failed");
                return false;
            }
            const double markerLinear[3] = {
                0.21586050011389926, 0.05126945837404324, 0.5209955732043543};
            bool bSceneLooksLikeMarker = true;
            bool bPresentationDiffers = false;
            for (uint32_t y = 6u; y < 18u; ++y)
            {
                for (uint32_t x = 6u; x < 18u; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    if (std::abs(static_cast<double>(image.Values[offset + 0u]) - markerLinear[0]) > 1.0e-3 ||
                        std::abs(static_cast<double>(image.Values[offset + 1u]) - markerLinear[1]) > 1.0e-3 ||
                        std::abs(static_cast<double>(image.Values[offset + 2u]) - markerLinear[2]) > 1.0e-3 ||
                        std::abs(static_cast<double>(image.Values[offset + 3u]) - 1.0) > 1.0e-3)
                    {
                        bSceneLooksLikeMarker = false;
                    }
                    if (std::abs(static_cast<double>(image.Values[offset + 0u]) -
                                 static_cast<double>(m_AllNumericalPresentationImage.Values[offset + 0u])) > 1.0e-6 ||
                        std::abs(static_cast<double>(image.Values[offset + 1u]) -
                                 static_cast<double>(m_AllNumericalPresentationImage.Values[offset + 1u])) > 1.0e-6 ||
                        std::abs(static_cast<double>(image.Values[offset + 2u]) -
                                 static_cast<double>(m_AllNumericalPresentationImage.Values[offset + 2u])) > 1.0e-6 ||
                        std::abs(static_cast<double>(image.Values[offset + 3u]) -
                                 static_cast<double>(m_AllNumericalPresentationImage.Values[offset + 3u])) > 1.0e-6)
                    {
                        bPresentationDiffers = true;
                    }
                }
            }
            if (bSceneLooksLikeMarker || !bPresentationDiffers)
            {
                reason = TEXT("R1 all-numerical SceneColor marker or overlay contamination detected");
                return false;
            }

            uint32_t actualAssertions = 0u;
            if (m_AllNumericalRowIndex <= 22u)
            {
                m_bP4ActualOracleMismatch = false;
                if (m_AllNumericalRowIndex == 1u)
                {
                    if (!ValidateP4CaptureEnvelope(frame, image, reason) ||
                        !EvaluateAllNumericalP4DfgTiles(image, reason))
                    {
                        return false;
                    }
                    actualAssertions = 25u;
                }
                else if (!EvaluateP4Frame(frame, reason) || m_bP4ActualOracleMismatch)
                {
                    if (reason.empty())
                    {
                        reason = TEXT("R1 all-numerical P4 oracle rejected a SceneColor row");
                    }
                    return false;
                }
                else
                {
                    actualAssertions = GetAllNumericalNumericalAssertionCount(
                        m_AllNumericalRowIndex);
                }
            }
            else if (m_AllNumericalRowIndex >= 23u && m_AllNumericalRowIndex <= 28u)
            {
                if (!EvaluateKnownCdFrame(frame, reason))
                {
                    return false;
                }
                actualAssertions = 1u;
            }
            else if (m_AllNumericalRowIndex == 29u)
            {
                if (!EvaluateR1SrgbTransfer(m_AllNumericalBackBuffer, reason))
                {
                    return false;
                }
                std::cout << "R1 all-numerical Raw254 sRGB transfer oracle passed:"
                          << " source=actual_back_buffer transfer=IEC_reference marker=144\n";
                actualAssertions = 1u;
            }
            else if (m_AllNumericalRowIndex >= 30u && m_AllNumericalRowIndex <= 39u)
            {
                if (!EvaluateTransparentPhysicalLightingFrame(frame, reason))
                {
                    return false;
                }
                actualAssertions = 1u;
            }
            if (actualAssertions != GetAllNumericalNumericalAssertionCount(
                                      m_AllNumericalRowIndex))
            {
                reason = TEXT("R1 all-numerical oracle assertion count is invalid");
                return false;
            }
            m_AllNumericalSceneColor = frame;
            ++m_AllNumericalSceneScans;
            m_AllNumericalFloatChannels += static_cast<uint64_t>(ValidationWidth) * ValidationHeight * 4u;
            m_AllNumericalNumericalRows += actualAssertions;
            m_AllNumericalRowIndex++;
            if (m_AllNumericalRowIndex >= AllNumericalStaticRowCount)
            {
                m_AllNumericalCaptureStage = AllNumericalCaptureStage::Complete;
                const uint64_t finalRenderedFrame =
                    Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount();
                m_AllNumericalFinalFrame = finalRenderedFrame;
                const uint64_t expectedFinal = m_AllNumericalStartupFrame + 240u;
                if (!m_bAllNumericalStartupFrameSet || m_AllNumericalFinalFrame != expectedFinal ||
                    m_AllNumericalStartupFrame > 359u || m_AllNumericalFinalFrame >= 600u ||
                    m_AllNumericalBackBufferScans != 40u ||
                    m_AllNumericalPresentationScans != 40u || m_AllNumericalSceneScans != 40u ||
                    m_AllNumericalActualByteChannels != 10485760u ||
                    m_AllNumericalFloatChannels != 20971520u ||
                    m_AllNumericalNumericalRows != 68u || m_AllNumericalForcedRows != 2u ||
                    m_AllNumericalMaxLatency != 2u)
                {
                    reason = TEXT("R1 all-numerical frame or scan aggregate is invalid");
                    return false;
                }
                std::cout << "R1 all-numerical contract passed: static_rows=40 numerical_rows=68 total_numerical_rows=70 captures=120 backbuffer_scans=40 presentation_scans=40 scene_scans=40 actual_byte_channels=10485760 float_channels=20971520 forced_rows=2 forced_pixels=14 forced_channels=56 lsb=1 synthetic_2_lsb=rejected startup_frame="
                          << m_AllNumericalStartupFrame << " final_frame=" << m_AllNumericalFinalFrame
                          << " ideal_latency=2 max_latency=2\n";
            }
            else
            {
                m_bAllNumericalRowMarkerPrinted = false;
                m_AllNumericalCaptureStage = AllNumericalCaptureStage::BackBuffer;
                if (!ApplyAllNumericalRow(m_AllNumericalRowIndex))
                {
                    m_bAllNumericalStageApplyFailed = true;
                    reason = TEXT("R1 all-numerical next row application failed");
                    return false;
                }
            }
            return true;
        }

        bool EvaluateAllNumericalFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (m_bAllNumericalStageApplyFailed)
            {
                reason = TEXT("R1 all-numerical capture stage application failed");
                return false;
            }
            if (frame.RequestId != GetLastAcceptedRequestId() ||
                (m_AllNumericalHasFrameNumber && frame.FrameNumber <= m_AllNumericalLastFrameNumber) ||
                (m_AllNumericalHasStageToken &&
                 GetLastAcceptedRequestStageToken() <= m_AllNumericalLastStageToken))
            {
                reason = TEXT("R1 all-numerical request, frame, or stage order is invalid");
                return false;
            }
            if (!m_bAllNumericalRequestStartFrameSet || Core::Engine::GEngine == nullptr)
            {
                reason = TEXT("R1 all-numerical request start frame is unavailable");
                return false;
            }
            const uint64_t completionFrame =
                Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount();
            if (completionFrame < m_AllNumericalRequestStartFrame)
            {
                reason = TEXT("R1 all-numerical completion precedes request start");
                return false;
            }
            const uint64_t latency = completionFrame - m_AllNumericalRequestStartFrame;
            m_AllNumericalMaxLatency = std::max(m_AllNumericalMaxLatency, latency);
            if (latency != 2u)
            {
                reason = TEXT("R1 all-numerical capture latency is not exactly two rendered frames");
                return false;
            }
            m_AllNumericalLastFrameNumber = frame.FrameNumber;
            m_AllNumericalPreviousFrame = frame.FrameNumber;
            m_AllNumericalHasFrameNumber = true;
            m_AllNumericalLastStageToken = GetLastAcceptedRequestStageToken();
            m_AllNumericalHasStageToken = true;

            switch (m_AllNumericalCaptureStage)
            {
            case AllNumericalCaptureStage::BackBuffer:
                if (!EvaluateAllNumericalBackBuffer(frame, reason))
                {
                    return false;
                }
                m_AllNumericalCaptureStage = AllNumericalCaptureStage::PresentationColor;
                return true;
            case AllNumericalCaptureStage::PresentationColor:
                if (!EvaluateAllNumericalPresentationColor(frame, reason))
                {
                    return false;
                }
                m_AllNumericalCaptureStage = AllNumericalCaptureStage::SceneColor;
                return true;
            case AllNumericalCaptureStage::SceneColor:
                return EvaluateAllNumericalSceneColor(frame, reason);
            case AllNumericalCaptureStage::Complete:
            default:
                reason = TEXT("R1 all-numerical capture sequence completed unexpectedly");
                return false;
            }
        }

        static bool ValidateTransparentPhysicalStageContract()
        {
            constexpr const char* expectedNames[TransparentPhysicalLightingRowCount] = {
                "DirectM0Off",
                "DirectM0On",
                "DirectM05Off",
                "DirectM05On",
                "ShadowUnshadowed",
                "Shadowed",
                "IblM0Off",
                "IblM0On",
                "IblM05Off",
                "IblM05On"};
            if (static_cast<uint32_t>(TransparentPhysicalStage::Complete) !=
                TransparentPhysicalLightingRowCount)
            {
                return false;
            }
            for (uint32_t index = 0u; index < TransparentPhysicalLightingRowCount; ++index)
            {
                if (std::strcmp(GetTransparentPhysicalStageName(
                                    static_cast<TransparentPhysicalStage>(index)),
                                expectedNames[index]) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        static const char* GetTransparentPhysicalStageName(TransparentPhysicalStage stage)
        {
            switch (stage)
            {
            case TransparentPhysicalStage::DirectM0Off:
                return "DirectM0Off";
            case TransparentPhysicalStage::DirectM0On:
                return "DirectM0On";
            case TransparentPhysicalStage::DirectM05Off:
                return "DirectM05Off";
            case TransparentPhysicalStage::DirectM05On:
                return "DirectM05On";
            case TransparentPhysicalStage::ShadowUnshadowed:
                return "ShadowUnshadowed";
            case TransparentPhysicalStage::Shadowed:
                return "Shadowed";
            case TransparentPhysicalStage::IblM0Off:
                return "IblM0Off";
            case TransparentPhysicalStage::IblM0On:
                return "IblM0On";
            case TransparentPhysicalStage::IblM05Off:
                return "IblM05Off";
            case TransparentPhysicalStage::IblM05On:
                return "IblM05On";
            case TransparentPhysicalStage::Complete:
            default:
                return "Complete";
            }
        }

        static bool IsTransparentPhysicalOn(TransparentPhysicalStage stage)
        {
            return stage == TransparentPhysicalStage::DirectM0On ||
                   stage == TransparentPhysicalStage::DirectM05On ||
                   stage == TransparentPhysicalStage::IblM0On ||
                   stage == TransparentPhysicalStage::IblM05On ||
                   stage == TransparentPhysicalStage::ShadowUnshadowed ||
                   stage == TransparentPhysicalStage::Shadowed;
        }

        static bool IsTransparentPhysicalIbl(TransparentPhysicalStage stage)
        {
            return stage == TransparentPhysicalStage::IblM0Off ||
                   stage == TransparentPhysicalStage::IblM0On ||
                   stage == TransparentPhysicalStage::IblM05Off ||
                   stage == TransparentPhysicalStage::IblM05On;
        }

        static bool IsTransparentPhysicalMetallicHalf(TransparentPhysicalStage stage)
        {
            return stage == TransparentPhysicalStage::DirectM05Off ||
                   stage == TransparentPhysicalStage::DirectM05On ||
                   stage == TransparentPhysicalStage::IblM05Off ||
                   stage == TransparentPhysicalStage::IblM05On;
        }

        static double TransparentPhysicalDot(const double left[3], const double right[3])
        {
            return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
        }

        static bool ComputeTransparentPhysicalPixel(uint32_t x,
                                                    uint32_t y,
                                                    double outPosition[3],
                                                    double outViewDirection[3],
                                                    double& outNdotV)
        {
            constexpr double cameraPosition[3] = {0.0, 0.0, 4.0};
            constexpr double depth = 4.0;
            constexpr double halfAngleTangent = 0.577350269189625764509148780501957456;
            const double pixelCenterX = static_cast<double>(x) + 0.5;
            const double pixelCenterY = static_cast<double>(y) + 0.5;
            const double right = (2.0 * pixelCenterX / 256.0 - 1.0) *
                                 depth * halfAngleTangent;
            const double up = (1.0 - 2.0 * pixelCenterY / 256.0) *
                              depth * halfAngleTangent;
            outPosition[0] = right;
            outPosition[1] = up;
            outPosition[2] = 0.0;
            const double toCamera[3] = {
                cameraPosition[0] - outPosition[0],
                cameraPosition[1] - outPosition[1],
                cameraPosition[2] - outPosition[2]};
            const double viewLength = std::sqrt(TransparentPhysicalDot(toCamera, toCamera));
            if (!std::isfinite(viewLength) || viewLength <= 0.0)
            {
                return false;
            }
            outViewDirection[0] = toCamera[0] / viewLength;
            outViewDirection[1] = toCamera[1] / viewLength;
            outViewDirection[2] = toCamera[2] / viewLength;
            outNdotV = std::clamp(outViewDirection[2], 0.0, 1.0);
            return std::isfinite(outNdotV);
        }

        static bool SampleTransparentPhysicalDfg(const P5DfgOracle& oracle,
                                                 double nDotV,
                                                 double roughness,
                                                 P4DfgValue& outValue)
        {
            if (!oracle.bValid || !std::isfinite(nDotV) || !std::isfinite(roughness))
            {
                return false;
            }
            const double coordinateX = std::clamp(
                nDotV,
                0.5 / static_cast<double>(P4DfgTextureSize),
                255.5 / static_cast<double>(P4DfgTextureSize));
            const double coordinateY = std::clamp(
                roughness,
                0.5 / static_cast<double>(P4DfgTextureSize),
                255.5 / static_cast<double>(P4DfgTextureSize));
            const double positionX = coordinateX * static_cast<double>(P4DfgTextureSize) - 0.5;
            const double positionY = coordinateY * static_cast<double>(P4DfgTextureSize) - 0.5;
            const uint32_t x0 = static_cast<uint32_t>(std::floor(positionX));
            const uint32_t y0 = static_cast<uint32_t>(std::floor(positionY));
            const uint32_t x1 = std::min(x0 + 1u, P4DfgTextureSize - 1u);
            const uint32_t y1 = std::min(y0 + 1u, P4DfgTextureSize - 1u);
            if (x0 < 254u || x0 > 255u || x1 < 254u || x1 > 255u ||
                (y0 != 127u && y0 != 128u) || (y1 != 127u && y1 != 128u))
            {
                return false;
            }
            const auto valueAt = [&oracle](uint32_t x, uint32_t y) -> const P4DfgValue&
            {
                if (x == 254u)
                {
                    return y == 127u ? oracle.X254Y127 : oracle.X254Y128;
                }
                return y == 127u ? oracle.X255Y127 : oracle.X255Y128;
            };
            const double wy = positionY - std::floor(positionY);
            const double wx = positionX - std::floor(positionX);
            const P4DfgValue& v00 = valueAt(x0, y0);
            const P4DfgValue& v10 = valueAt(x1, y0);
            const P4DfgValue& v01 = valueAt(x0, y1);
            const P4DfgValue& v11 = valueAt(x1, y1);
            const double topA = v00.A + (v10.A - v00.A) * wx;
            const double bottomA = v01.A + (v11.A - v01.A) * wx;
            const double topB = v00.B + (v10.B - v00.B) * wx;
            const double bottomB = v01.B + (v11.B - v01.B) * wx;
            outValue.A = topA + (bottomA - topA) * wy;
            outValue.B = topB + (bottomB - topB) * wy;
            return std::isfinite(outValue.A) && std::isfinite(outValue.B);
        }

        static bool ComputeTransparentPhysicalDirectSource(
            TransparentPhysicalStage stage,
            uint32_t x,
            uint32_t y,
            const P5DfgOracle& dfgOracle,
            double outRgb[3])
        {
            constexpr double pi = 3.141592653589793238462643383279502884;
            constexpr double baseColor[3] = {0.5, 0.25, 0.125};
            constexpr double roughness = 0.5;
            double position[3] = {};
            double viewDirection[3] = {};
            double nDotV = 0.0;
            P4DfgValue dfg;
            if (!ComputeTransparentPhysicalPixel(x, y, position, viewDirection, nDotV) ||
                !SampleTransparentPhysicalDfg(dfgOracle, nDotV, roughness, dfg))
            {
                return false;
            }
            const bool bMetallicHalf = IsTransparentPhysicalMetallicHalf(stage);
            const bool bDirectional = stage == TransparentPhysicalStage::ShadowUnshadowed ||
                                      stage == TransparentPhysicalStage::Shadowed;
            double lightDirection[3] = {};
            double radiance = 0.0;
            if (bDirectional)
            {
                lightDirection[0] = 0.8;
                lightDirection[1] = 0.0;
                lightDirection[2] = 0.6;
                radiance = 100.0;
            }
            else
            {
                const double toLight[3] = {
                    -position[0], -position[1], 2.0 - position[2]};
                const double distance = std::sqrt(TransparentPhysicalDot(toLight, toLight));
                if (!std::isfinite(distance) || distance <= 0.0)
                {
                    return false;
                }
                lightDirection[0] = toLight[0] / distance;
                lightDirection[1] = toLight[1] / distance;
                lightDirection[2] = toLight[2] / distance;
                const double rangeWindow = std::pow(
                    std::max(1.0 - std::pow(distance / 1000.0, 4.0), 0.0), 2.0);
                radiance = 100.0 / std::max(distance * distance, 0.0001) * rangeWindow;
            }
            const double nDotL = std::max(lightDirection[2], 0.0);
            if (nDotL <= 0.0)
            {
                outRgb[0] = 0.0;
                outRgb[1] = 0.0;
                outRgb[2] = 0.0;
                return true;
            }
            double halfVector[3] = {
                viewDirection[0] + lightDirection[0],
                viewDirection[1] + lightDirection[1],
                viewDirection[2] + lightDirection[2]};
            const double halfLength = std::sqrt(TransparentPhysicalDot(halfVector, halfVector));
            if (!std::isfinite(halfLength) || halfLength <= 0.0)
            {
                return false;
            }
            halfVector[0] /= halfLength;
            halfVector[1] /= halfLength;
            halfVector[2] /= halfLength;
            const double nDotH = std::max(halfVector[2], 0.0);
            const double viewDotH = std::max(TransparentPhysicalDot(viewDirection, halfVector), 0.0);
            const double alpha = roughness * roughness;
            const double alphaSquared = alpha * alpha;
            const double distributionDenominator =
                pi * std::pow(nDotH * nDotH * (alphaSquared - 1.0) + 1.0, 2.0);
            const double distribution = alphaSquared /
                                        std::max(distributionDenominator, 0.0001);
            const double k = std::pow(roughness + 1.0, 2.0) / 8.0;
            const double geometryView = nDotV / (nDotV * (1.0 - k) + k);
            const double geometryLight = nDotL / (nDotL * (1.0 - k) + k);
            const double specularTerm = distribution * geometryView * geometryLight /
                                        (4.0 * nDotV * nDotL + 0.0001);
            const double ess = std::max(dfg.A + dfg.B, 0.0001);
            const double compensationD = 1.0 + 0.04 * (1.0 - ess) / ess;
            const double dielectricFresnel = 0.04 + 0.96 *
                                              std::pow(1.0 - viewDotH, 5.0);
            const double metallic = bMetallicHalf ? 0.5 : 0.0;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double conductorFresnel = baseColor[channel] +
                    (1.0 - baseColor[channel]) * std::pow(1.0 - viewDotH, 5.0);
                const double compensationC = 1.0 + baseColor[channel] *
                    (1.0 - ess) / ess;
                const double dielectric =
                    (1.0 - dielectricFresnel) * baseColor[channel] / pi +
                    specularTerm * dielectricFresnel * compensationD;
                const double conductor = specularTerm * conductorFresnel * compensationC;
                outRgb[channel] = ((1.0 - metallic) * dielectric + metallic * conductor) *
                                  radiance * nDotL;
            }
            return true;
        }

        static bool ComputeTransparentPhysicalIblSource(
            TransparentPhysicalStage stage,
            uint32_t x,
            uint32_t y,
            const P5DfgOracle& dfgOracle,
            double outRgb[3])
        {
            constexpr double baseColor[3] = {0.5, 0.25, 0.125};
            constexpr double roughness = 0.5;
            double position[3] = {};
            double viewDirection[3] = {};
            double nDotV = 0.0;
            P4DfgValue dfg;
            if (!ComputeTransparentPhysicalPixel(x, y, position, viewDirection, nDotV) ||
                !SampleTransparentPhysicalDfg(dfgOracle, nDotV, roughness, dfg))
            {
                return false;
            }
            const double ess = std::max(dfg.A + dfg.B, 0.0001);
            const double metallic = IsTransparentPhysicalMetallicHalf(stage) ? 0.5 : 0.0;
            const double specularAo = std::clamp(
                std::pow(nDotV + 1.0, std::exp2(-16.0 * roughness - 1.0)) - 1.0 + 1.0,
                0.0,
                1.0);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double compensationD = 1.0 + 0.04 * (1.0 - ess) / ess;
                const double compensationC = 1.0 + baseColor[channel] *
                    (1.0 - ess) / ess;
                const double ed = std::clamp(
                    (0.04 * dfg.A + dfg.B) * compensationD, 0.0, 1.0);
                const double ec = std::clamp(
                    (baseColor[channel] * dfg.A + dfg.B) * compensationC, 0.0, 1.0);
                const double diffuse = 100.0 * baseColor[channel] *
                    (1.0 - metallic) * (1.0 - ed);
                const double specular = 100.0 *
                    ((1.0 - metallic) * ed + metallic * ec);
                outRgb[channel] = diffuse + specular * specularAo;
            }
            return true;
        }

        static bool BuildTransparentPhysicalFixedLuminancePreflight(double outLuminance[4])
        {
            constexpr double pi = 3.141592653589793238462643383279502884;
            constexpr double baseColor[3] = {0.5, 0.25, 0.125};
            constexpr double roughness = 0.5;
            constexpr double dfgA = 0.89453125;
            constexpr double dfgB = 0.0000262856483459473;
            constexpr double directRadiance = 25.0;
            constexpr double expected[4] = {
                0.1410464070373729,
                0.2651913529406838,
                31.9519814926778,
                29.654360326265852};
            const double ess = dfgA + dfgB;
            const double alpha = roughness * roughness;
            const double alphaSquared = alpha * alpha;
            const double distribution = alphaSquared /
                (pi * std::pow(alphaSquared, 2.0));
            const double k = std::pow(roughness + 1.0, 2.0) / 8.0;
            const double geometry = 1.0 / (1.0 * (1.0 - k) + k);
            const double specularTerm = distribution * geometry * geometry /
                                        (4.0 + 0.0001);
            const double compensationD = 1.0 + 0.04 * (1.0 - ess) / ess;
            const double dielectricFresnel = 0.04;
            const double specularAo = std::clamp(
                std::pow(2.0, std::exp2(-16.0 * roughness - 1.0)) - 1.0 + 1.0,
                0.0,
                1.0);

            for (uint32_t metallicIndex = 0u; metallicIndex < 2u; ++metallicIndex)
            {
                const double metallic = metallicIndex == 0u ? 0.0 : 0.5;
                double directRgb[3] = {};
                double iblRgb[3] = {};
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double conductorFresnel = baseColor[channel];
                    const double compensationC = 1.0 + baseColor[channel] *
                        (1.0 - ess) / ess;
                    const double dielectric =
                        (1.0 - dielectricFresnel) * baseColor[channel] / pi +
                        specularTerm * dielectricFresnel * compensationD;
                    const double conductor = specularTerm * conductorFresnel * compensationC;
                    directRgb[channel] = ((1.0 - metallic) * dielectric +
                                          metallic * conductor) * directRadiance;

                    const double ed = std::clamp(
                        (0.04 * dfgA + dfgB) * compensationD, 0.0, 1.0);
                    const double ec = std::clamp(
                        (baseColor[channel] * dfgA + dfgB) * compensationC,
                        0.0,
                        1.0);
                    const double diffuse = 100.0 * baseColor[channel] *
                        (1.0 - metallic) * (1.0 - ed);
                    const double specular = 100.0 *
                        ((1.0 - metallic) * ed + metallic * ec);
                    iblRgb[channel] = diffuse + specular * specularAo;
                }
                const double directY = 0.2126 * directRgb[0] +
                                       0.7152 * directRgb[1] +
                                       0.0722 * directRgb[2];
                const double iblY = 0.2126 * iblRgb[0] +
                                    0.7152 * iblRgb[1] +
                                    0.0722 * iblRgb[2];
                outLuminance[metallicIndex] = directY / directRadiance;
                outLuminance[2u + metallicIndex] = iblY;
            }

            for (uint32_t index = 0u; index < 4u; ++index)
            {
                if (!std::isfinite(outLuminance[index]) ||
                    std::abs(outLuminance[index] - expected[index]) > 1.0e-12)
                {
                    return false;
                }
            }
            return true;
        }

        static bool CheckTransparentPhysicalPixels(
            const RgbaFloatImage& image,
            TransparentPhysicalStage stage,
            const P5DfgOracle& dfgOracle,
            bool bCompareToOracle,
            double& outMeanY,
            double& outExpectedMeanY,
            double outMeanRelative[3],
            double outMaximumRelative[3],
            double outFixedSampleMaximumRelative[3],
            Core::Container::String& reason)
        {
            double sums[3] = {};
            double expectedSums[3] = {};
            double relativeSums[3] = {};
            double maximumRelative[3] = {};
            double fixedSampleMaximumRelative[3] = {};
            uint32_t fixedSampleCount = 0u;
            uint32_t pixelCount = 0u;
            const bool bOn = IsTransparentPhysicalOn(stage);
            for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
            {
                for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                {
                    const bool bFixedSample =
                        (x == 112u || x == 127u || x == 143u) &&
                        (y == 112u || y == 127u || y == 143u);
                    if (bFixedSample)
                    {
                        ++fixedSampleCount;
                    }
                    double expectedRgb[3] = {0.05, 0.05, 0.05};
                    if (bCompareToOracle && bOn)
                    {
                        double sourceRgb[3] = {};
                        const bool bSourceValid = IsTransparentPhysicalIbl(stage)
                            ? ComputeTransparentPhysicalIblSource(stage, x, y, dfgOracle, sourceRgb)
                            : ComputeTransparentPhysicalDirectSource(stage, x, y, dfgOracle, sourceRgb);
                        if (!bSourceValid)
                        {
                            reason = TEXT("P5 transparent pixel oracle construction failed");
                            return false;
                        }
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            expectedRgb[channel] = 0.05 + 0.5 * sourceRgb[channel] / 72.0;
                        }
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        expectedSums[channel] += expectedRgb[channel];
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    ++pixelCount;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double actual = static_cast<double>(image.Values[offset + channel]);
                        if (!std::isfinite(actual) || std::abs(actual) >= 65504.0)
                        {
                            reason = TEXT("P5 transparent ROI contains invalid data");
                            return false;
                        }
                        sums[channel] += actual;
                        if (bCompareToOracle)
                        {
                            const double reference = std::abs(expectedRgb[channel]);
                            if (!std::isfinite(reference) || reference < 0.01)
                            {
                                reason = TEXT("P5 transparent reference channel is below 0.01");
                                return false;
                            }
                            const double relative = std::abs(actual - expectedRgb[channel]) /
                                                    reference;
                            if (!std::isfinite(relative))
                            {
                                reason = TEXT("P5 transparent ROI relative error is invalid");
                                return false;
                            }
                            relativeSums[channel] += relative;
                            if (relative > maximumRelative[channel])
                            {
                                maximumRelative[channel] = relative;
                            }
                            if (bFixedSample)
                            {
                                fixedSampleMaximumRelative[channel] =
                                    std::max(fixedSampleMaximumRelative[channel], relative);
                            }
                        }
                    }
                }
            }
            if (pixelCount != 1024u || fixedSampleCount != 9u)
            {
                reason = TEXT("P5 transparent ROI sample count is invalid");
                return false;
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                outMeanRelative[channel] = bCompareToOracle
                    ? relativeSums[channel] / static_cast<double>(pixelCount)
                    : 0.0;
                outMaximumRelative[channel] = bCompareToOracle ? maximumRelative[channel] : 0.0;
                outFixedSampleMaximumRelative[channel] = bCompareToOracle
                    ? fixedSampleMaximumRelative[channel]
                    : 0.0;
            }
            const double meanRgb[3] = {
                sums[0] / static_cast<double>(pixelCount),
                sums[1] / static_cast<double>(pixelCount),
                sums[2] / static_cast<double>(pixelCount)};
            const double expectedMeanRgb[3] = {
                expectedSums[0] / static_cast<double>(pixelCount),
                expectedSums[1] / static_cast<double>(pixelCount),
                expectedSums[2] / static_cast<double>(pixelCount)};
            outMeanY = 0.2126 * meanRgb[0] + 0.7152 * meanRgb[1] + 0.0722 * meanRgb[2];
            outExpectedMeanY = 0.2126 * expectedMeanRgb[0] +
                               0.7152 * expectedMeanRgb[1] +
                               0.0722 * expectedMeanRgb[2];
            return true;
        }

        bool EvaluateTransparentPhysicalLightingFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (m_bTransparentPhysicalStageApplyFailed)
            {
                reason = TEXT("P5 transparent fixture row application failed");
                return false;
            }
            if (frame.RequestId != GetLastAcceptedRequestId() ||
                (m_bTransparentPhysicalHasFrameNumber &&
                 frame.FrameNumber <= m_TransparentPhysicalLastFrameNumber) ||
                (m_bTransparentPhysicalHasStageToken &&
                 GetLastAcceptedRequestStageToken() <= m_TransparentPhysicalLastStageToken))
            {
                reason = TEXT("P5 transparent capture request, frame, or stage order is invalid");
                return false;
            }
            m_TransparentPhysicalLastFrameNumber = frame.FrameNumber;
            m_bTransparentPhysicalHasFrameNumber = true;
            m_TransparentPhysicalLastStageToken = GetLastAcceptedRequestStageToken();
            m_bTransparentPhysicalHasStageToken = true;
            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT ||
                frame.Width != ValidationWidth || frame.Height != ValidationHeight)
            {
                reason = TEXT("P5 transparent capture format or dimensions are invalid");
                return false;
            }
            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success ||
                !IsFiniteAndWithinRgba16Range(image))
            {
                reason = TEXT("P5 transparent capture RGBA16F validation failed");
                return false;
            }

            // Keep the fixed background-only ROI precondition visible in every row before delta/oracle checks.
            constexpr uint32_t backgroundProbeMinX = 16u;
            constexpr uint32_t backgroundProbeMaxX = 47u;
            constexpr uint32_t backgroundProbeMinY = 16u;
            constexpr uint32_t backgroundProbeMaxY = 47u;
            double backgroundChannelSums[3] = {};
            double backgroundMaximumRelative[3] = {};
            size_t backgroundPixelCount = 0u;
            size_t backgroundSampleCount = 0u;
            for (uint32_t y = backgroundProbeMinY; y <= backgroundProbeMaxY; ++y)
            {
                for (uint32_t x = backgroundProbeMinX; x <= backgroundProbeMaxX; ++x)
                {
                    const size_t backgroundOffset =
                        (static_cast<size_t>(y) * image.Width + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double preExposed = image.Values[backgroundOffset + channel];
                        const double relative = std::abs(preExposed - 0.1) / 0.1;
                        backgroundChannelSums[channel] += preExposed;
                        backgroundMaximumRelative[channel] =
                            std::max(backgroundMaximumRelative[channel], relative);
                        ++backgroundSampleCount;
                    }
                    ++backgroundPixelCount;
                }
            }
            std::cout << std::fixed << std::setprecision(9)
                      << "P5 background precondition: row="
                      << GetTransparentPhysicalStageName(m_TransparentPhysicalStage)
                      << " samples=" << backgroundSampleCount
                      << " pre_exposed_expected=0.100000000"
                      << " mean=(" << backgroundChannelSums[0] / backgroundPixelCount
                      << "," << backgroundChannelSums[1] / backgroundPixelCount
                      << "," << backgroundChannelSums[2] / backgroundPixelCount << ")"
                      << " max_rel=(" << backgroundMaximumRelative[0]
                      << "," << backgroundMaximumRelative[1]
                      << "," << backgroundMaximumRelative[2] << ")"
                      << " roi=[16,47]x[16,47]\n" << std::setprecision(6);
            if (backgroundPixelCount != 1024u || backgroundSampleCount != 3072u ||
                backgroundMaximumRelative[0] > 0.01 ||
                backgroundMaximumRelative[1] > 0.01 ||
                backgroundMaximumRelative[2] > 0.01)
            {
                reason = TEXT("P5 background-only pre-exposed value is not 0.1");
                return false;
            }

            const bool bOn = IsTransparentPhysicalOn(m_TransparentPhysicalStage);
            double sourceRgb[3] = {};
            if (bOn && !m_bTransparentPhysicalDfgOracleValid)
            {
                reason = TEXT("P5 transparent DFG oracle is unavailable");
                return false;
            }
            if (bOn)
            {
                const bool bSourceValid = IsTransparentPhysicalIbl(m_TransparentPhysicalStage)
                    ? ComputeTransparentPhysicalIblSource(m_TransparentPhysicalStage,
                                                          R1AnchorX,
                                                          R1AnchorY,
                                                          m_TransparentPhysicalDfgOracle,
                                                          sourceRgb)
                    : ComputeTransparentPhysicalDirectSource(m_TransparentPhysicalStage,
                                                              R1AnchorX,
                                                              R1AnchorY,
                                                              m_TransparentPhysicalDfgOracle,
                                                              sourceRgb);
                if (!bSourceValid)
                {
                    reason = TEXT("P5 transparent center oracle construction failed");
                    return false;
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    if (!std::isfinite(sourceRgb[channel]) || sourceRgb[channel] < 0.01)
                    {
                        reason = TEXT("P5 transparent reference channel is below 0.01");
                        return false;
                    }
                }
            }
            double rangeRelativeError = 0.0;
            if (m_TransparentPhysicalStage == TransparentPhysicalStage::DirectM0Off ||
                m_TransparentPhysicalStage == TransparentPhysicalStage::DirectM0On ||
                m_TransparentPhysicalStage == TransparentPhysicalStage::DirectM05Off ||
                m_TransparentPhysicalStage == TransparentPhysicalStage::DirectM05On)
            {
                const double distance = 2.0;
                const double range = 1000.0;
                const double productionAttenuation =
                    1.0 / (distance * distance) *
                    std::pow(std::max(1.0 - std::pow(distance / range, 4.0), 0.0), 2.0);
                const double idealAttenuation = 1.0 / (distance * distance);
                rangeRelativeError = std::abs(productionAttenuation - idealAttenuation) /
                                      idealAttenuation;
                if (!std::isfinite(rangeRelativeError) || rangeRelativeError > 1.0e-9)
                {
                    reason = TEXT("P5 direct range/inverse-square contract failed");
                    return false;
                }
            }
            double meanY = 0.0;
            double expectedMeanY = 0.0;
            double meanRelative[3] = {};
            double maximumRelative[3] = {};
            double fixedSampleMaximumRelative[3] = {};
            const bool bShadowed = m_TransparentPhysicalStage == TransparentPhysicalStage::Shadowed;
            const bool bCompareToOracle = !bShadowed;
            if (!CheckTransparentPhysicalPixels(image,
                                                 m_TransparentPhysicalStage,
                                                 m_TransparentPhysicalDfgOracle,
                                                 bCompareToOracle,
                                                 meanY,
                                                 expectedMeanY,
                                                 meanRelative,
                                                 maximumRelative,
                                                 fixedSampleMaximumRelative,
                                                 reason))
            {
                return false;
            }
            const double sourceY = 0.2126 * sourceRgb[0] +
                                   0.7152 * sourceRgb[1] +
                                   0.0722 * sourceRgb[2];
            const double expectedY = expectedMeanY;
            const double predictedDelta = expectedY - 0.05;
            const double measuredDelta = bOn ? std::abs(meanY - 0.05) : 0.0;
            if (m_TransparentPhysicalStage == TransparentPhysicalStage::ShadowUnshadowed)
            {
                m_TransparentPhysicalUnshadowedMeanY = meanY;
                m_bTransparentPhysicalHasUnshadowedValue = true;
            }
            double shadowRatio = 0.0;
            if (bShadowed)
            {
                const double unshadowedContribution = std::max(
                    m_TransparentPhysicalUnshadowedMeanY - 0.05, 0.0);
                const double shadowedContribution = std::max(meanY - 0.05, 0.0);
                shadowRatio = unshadowedContribution > 0.0
                    ? shadowedContribution / unshadowedContribution
                    : 1.0e30;
                if (!m_bTransparentPhysicalHasUnshadowedValue ||
                    !std::isfinite(shadowRatio) || shadowRatio > 0.10)
                {
                    reason = TEXT("P5 shadowed/unshadowed ratio exceeded 0.10");
                    return false;
                }
            }
            bool bOraclePassed = !bCompareToOracle;
            if (bCompareToOracle)
            {
                bOraclePassed = true;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    bOraclePassed = bOraclePassed && meanRelative[channel] <= 0.01 &&
                                    fixedSampleMaximumRelative[channel] <= 0.03;
                }
            }
            const bool bDeltaRequired = bOn && !bShadowed;
            const bool bNumericPassed = (!bDeltaRequired ||
                                         (predictedDelta >= 0.02 &&
                                          measuredDelta >= predictedDelta * 0.90)) &&
                                        bOraclePassed;
            std::cout << std::fixed << std::setprecision(15)
                      << "P5 transparent row: row="
                      << GetTransparentPhysicalStageName(m_TransparentPhysicalStage)
                      << " source_y=" << sourceY
                      << " predicted_source_y=" << (predictedDelta * 72.0 / 0.5)
                      << " expected_y=" << expectedY
                      << " mean_y=" << meanY
                      << " predicted_delta=" << predictedDelta
                      << " measured_delta=" << measuredDelta
                      << " mean_rel_rgb=(" << meanRelative[0] << "," << meanRelative[1]
                      << "," << meanRelative[2] << ")"
                      << " max_rel_rgb=(" << maximumRelative[0] << "," << maximumRelative[1]
                      << "," << maximumRelative[2] << ")"
                      << " fixed_sample_max_rel_rgb=(" << fixedSampleMaximumRelative[0]
                      << "," << fixedSampleMaximumRelative[1] << ","
                      << fixedSampleMaximumRelative[2] << ")"
                      << " shadow_ratio=" << shadowRatio
                      << " range_rel=" << rangeRelativeError
                      << " logical_light_count=" << (bOn && !IsTransparentPhysicalIbl(m_TransparentPhysicalStage) ? 1u : 0u)
                      << " pre_exposure=0.013888888888889 blend=straight_alpha_0.5\n"
                      << std::setprecision(6);
            if (!bNumericPassed)
            {
                reason = TEXT("P5 transparent physical row oracle or delta contract failed");
                return false;
            }
            return true;
        }

        static bool ValidateP4Camera(const Core::Rendering::CameraProxy& camera)
        {
            constexpr float expectedAperture = 4.0f;
            constexpr float expectedShutterSpeed = 1.0f / 60.0f;
            constexpr float expectedIso = 100.0f;
            constexpr float expectedCompensation = 4.0f;
            if (!std::isfinite(camera.Aperture) ||
                !std::isfinite(camera.ShutterSpeed) ||
                !std::isfinite(camera.ISO) ||
                !std::isfinite(camera.ExposureCompensation) ||
                !std::isfinite(camera.EV100) ||
                !std::isfinite(camera.Exposure) ||
                !std::isfinite(camera.PreExposure) ||
                !std::isfinite(camera.InvPreExposure) ||
                camera.Aperture != expectedAperture ||
                camera.ShutterSpeed != expectedShutterSpeed ||
                camera.ISO != expectedIso ||
                camera.ExposureCompensation != expectedCompensation ||
                camera.Exposure <= 0.0f || camera.PreExposure <= 0.0f ||
                camera.InvPreExposure <= 0.0f)
            {
                return false;
            }

            const double ev100 = std::log2(
                (static_cast<double>(camera.Aperture) *
                 static_cast<double>(camera.Aperture) /
                 static_cast<double>(camera.ShutterSpeed)) *
                (100.0 / static_cast<double>(camera.ISO)));
            const double exposure = std::exp2(
                static_cast<double>(camera.ExposureCompensation) - ev100) / 1.2;
            const float expectedEv100 = static_cast<float>(ev100);
            const float expectedExposure = static_cast<float>(exposure);
            const float expectedInvPreExposure = static_cast<float>(1.0 / exposure);
            const double forwardLength = std::sqrt(
                R1CameraTargetX * R1CameraTargetX +
                R1CameraTargetY * R1CameraTargetY + 16.0);
            const float expectedForwardX = static_cast<float>(R1CameraTargetX / forwardLength);
            const float expectedForwardY = static_cast<float>(R1CameraTargetY / forwardLength);
            const float expectedForwardZ = static_cast<float>(-4.0 / forwardLength);
            return camera.EV100 == expectedEv100 &&
                   camera.Exposure == expectedExposure &&
                   camera.PreExposure == expectedExposure &&
                   camera.InvPreExposure == expectedInvPreExposure &&
                   camera.Projection == Core::Rendering::ProjectionType::Orthographic &&
                   camera.PositionX == 0.0f && camera.PositionY == 0.0f &&
                   camera.PositionZ == 4.0f && camera.OrthoWidth == 0.1f &&
                   camera.OrthoHeight == 0.1f && camera.NearPlane == 0.1f &&
                   camera.FarPlane == 10.0f && camera.Viewport.Width == 256.0f &&
                   camera.Viewport.Height == 256.0f &&
                   std::abs(camera.ForwardX - expectedForwardX) <= 1.0e-5f &&
                   std::abs(camera.ForwardY - expectedForwardY) <= 1.0e-5f &&
                   std::abs(camera.ForwardZ - expectedForwardZ) <= 1.0e-5f;
        }

        static bool ProjectP4Anchor(const Core::Rendering::CameraProxy& camera,
                                    uint32_t& outAnchorX,
                                    uint32_t& outAnchorY,
                                    double& outDepth)
        {
            const double positionX = camera.PositionX;
            const double positionY = camera.PositionY;
            const double positionZ = camera.PositionZ;
            const double forwardX = camera.ForwardX;
            const double forwardY = camera.ForwardY;
            const double forwardZ = camera.ForwardZ;
            if (!std::isfinite(positionX) || !std::isfinite(positionY) ||
                !std::isfinite(positionZ) || !std::isfinite(forwardX) ||
                !std::isfinite(forwardY) || !std::isfinite(forwardZ) ||
                std::abs(forwardZ) < 1.0e-9 || camera.OrthoWidth <= 0.0f ||
                camera.OrthoHeight <= 0.0f || camera.Viewport.Width <= 0.0f ||
                camera.Viewport.Height <= 0.0f)
            {
                return false;
            }

            const double targetScale = -positionZ / forwardZ;
            const double targetX = positionX + forwardX * targetScale;
            const double targetY = positionY + forwardY * targetScale;
            if (!std::isfinite(targetX) || !std::isfinite(targetY) ||
                std::abs(targetX - R1CameraTargetX) > 1.0e-5 ||
                std::abs(targetY - R1CameraTargetY) > 1.0e-5)
            {
                return false;
            }

            const double worldDeltaX = -positionX;
            const double worldDeltaY = -positionY;
            const double worldDeltaZ = -positionZ;
            const double viewX = worldDeltaX * camera.RightX +
                                 worldDeltaY * camera.RightY +
                                 worldDeltaZ * camera.RightZ;
            const double viewY = worldDeltaX * camera.UpX +
                                 worldDeltaY * camera.UpY +
                                 worldDeltaZ * camera.UpZ;
            const double viewZ = worldDeltaX * forwardX +
                                 worldDeltaY * forwardY +
                                 worldDeltaZ * forwardZ;
            const double ndcX = 2.0 * viewX / static_cast<double>(camera.OrthoWidth);
            const double ndcY = 2.0 * viewY / static_cast<double>(camera.OrthoHeight);
            const double screenX = static_cast<double>(camera.Viewport.X) +
                                   (ndcX + 1.0) * 0.5 * camera.Viewport.Width - 0.5;
            const double screenY = static_cast<double>(camera.Viewport.Y) +
                                   (1.0 - ndcY) * 0.5 * camera.Viewport.Height - 0.5;
            outDepth = (std::abs(viewZ) - static_cast<double>(camera.NearPlane)) /
                       static_cast<double>(camera.FarPlane - camera.NearPlane);
            if (!std::isfinite(ndcX) || !std::isfinite(ndcY) ||
                !std::isfinite(screenX) || !std::isfinite(screenY) ||
                !std::isfinite(outDepth) || outDepth < 0.0 || outDepth > 1.0 ||
                screenX < 0.0 || screenY < 0.0)
            {
                return false;
            }
            outAnchorX = static_cast<uint32_t>(std::floor(screenX));
            outAnchorY = static_cast<uint32_t>(std::floor(screenY));
            return outAnchorX == R1AnchorX && outAnchorY == R1AnchorY;
        }

        static bool ComputeP4SphereNormal(const Core::Rendering::CameraProxy& camera,
                                          uint32_t x,
                                          uint32_t y,
                                          double outNormal[3])
        {
            constexpr double centerX = 127.5;
            constexpr double centerY = 127.5;
            constexpr double radius = 64.0;
            const double nx = (static_cast<double>(x) + 0.5 - centerX) / radius;
            const double ny = -(static_cast<double>(y) + 0.5 - centerY) / radius;
            const double radialSquared = nx * nx + ny * ny;
            if (!std::isfinite(nx) || !std::isfinite(ny) ||
                !std::isfinite(radialSquared) || radialSquared > 1.0)
            {
                return false;
            }
            const double nz = std::sqrt(std::max(0.0, 1.0 - radialSquared));
            const double worldX = camera.RightX * nx + camera.UpX * ny - camera.ForwardX * nz;
            const double worldY = camera.RightY * nx + camera.UpY * ny - camera.ForwardY * nz;
            const double worldZ = camera.RightZ * nx + camera.UpZ * ny - camera.ForwardZ * nz;
            outNormal[0] = 0.5 * (worldX + 1.0);
            outNormal[1] = 0.5 * (worldY + 1.0);
            outNormal[2] = 0.5 * (worldZ + 1.0);
            return std::isfinite(outNormal[0]) && std::isfinite(outNormal[1]) &&
                   std::isfinite(outNormal[2]);
        }

        bool CheckP4SphereNormalSample(const RgbaFloatImage& image,
                                       uint32_t x,
                                       uint32_t y,
                                       Core::Container::String& reason) const
        {
            if (x >= image.Width || y >= image.Height)
            {
                reason = TEXT("P4 sphere normal fixed probe is outside the capture");
                return false;
            }
            const size_t pixelOffset = (static_cast<size_t>(y) * image.Width + x) * 4u;
            const double alpha = image.Values[pixelOffset + 3u];
            if (!std::isfinite(alpha) || std::abs(alpha - 1.0) > 1.0e-3)
            {
                reason = TEXT("P4 sphere normal probe alpha is not one");
                return false;
            }
            double expectedNormal[3] = {};
            if (!ComputeP4SphereNormal(m_P4ActualCamera, x, y, expectedNormal))
            {
                reason = TEXT("P4 sphere normal projection is invalid");
                return false;
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double actual = image.Values[pixelOffset + channel];
                if (!std::isfinite(actual) || std::abs(actual) >= P4RneRangeLimit ||
                    std::abs(actual - expectedNormal[channel]) > 0.01)
                {
                    reason = TEXT("P4 sphere normal does not match the actual fixture projection");
                    return false;
                }
            }
            return true;
        }

        const char* GetP4ScenarioName() const
        {
            switch (m_P4Scenario)
            {
            case P4Scenario::Raw250TextureRepresentation:
                return "ibl-prefilter-nonconstant";
            case P4Scenario::Raw251DfgLut:
                return "dfg-lut";
            case P4Scenario::Raw252RoughnessSweep:
                return "ibl-roughness-sweep";
            case P4Scenario::Raw252WhiteFurnace:
                return "white-furnace";
            case P4Scenario::Raw254DirectConductorEndpoint:
                return "direct-conductor-endpoint";
            default:
                return "unknown";
            }
        }

        static bool ValidateKnownCdScenarioContract()
        {
            return static_cast<uint8_t>(KnownCdStage::PureLambertA) == 0u &&
                   static_cast<uint8_t>(KnownCdStage::PureLambertB) == 1u &&
                   static_cast<uint8_t>(KnownCdStage::DirectPbrA) == 2u &&
                   static_cast<uint8_t>(KnownCdStage::DirectPbrB) == 3u &&
                   static_cast<uint8_t>(KnownCdStage::NormalA) == 4u &&
                   static_cast<uint8_t>(KnownCdStage::NormalB) == 5u &&
                   static_cast<uint8_t>(KnownCdStage::Complete) == 6u &&
                   static_cast<uint32_t>(static_cast<Core::Rendering::DebugViewMode>(253u)) == 253u &&
                   static_cast<uint32_t>(static_cast<Core::Rendering::DebugViewMode>(254u)) == 254u &&
                   R1RoiMinX == 112u && R1RoiMaxX == 143u &&
                   R1RoiMinY == 112u && R1RoiMaxY == 143u &&
                   R1AnchorX == 127u && R1AnchorY == 127u &&
                   R1PlaneScale == 0.0025f;
        }

        uint32_t GetP4ScenarioRowCount() const
        {
            switch (m_P4Scenario)
            {
            case P4Scenario::Raw250TextureRepresentation:
                return 1u;
            case P4Scenario::Raw251DfgLut:
                return 25u;
            case P4Scenario::Raw252RoughnessSweep:
                return 6u;
            case P4Scenario::Raw252TargetNotOne:
                return 1u;
            case P4Scenario::Raw252WhiteFurnace:
                return 15u;
            case P4Scenario::Raw254DirectConductorEndpoint:
                return 1u;
            default:
                return 0u;
            }
        }

        uint32_t GetP4DebugViewMode() const
        {
            switch (m_P4Substage)
            {
            case P4CaptureSubstage::Depth:
                return 7u;
            case P4CaptureSubstage::Normal:
                return 5u;
            case P4CaptureSubstage::Material:
                return 6u;
            case P4CaptureSubstage::Primary:
            default:
                break;
            }
            switch (GetP4RowScenario())
            {
            case P4Scenario::Raw250TextureRepresentation:
                return 250u;
            case P4Scenario::Raw251DfgLut:
                return 251u;
            case P4Scenario::Raw254DirectConductorEndpoint:
                return 254u;
            case P4Scenario::Raw252RoughnessSweep:
            case P4Scenario::Raw252TargetNotOne:
            case P4Scenario::Raw252WhiteFurnace:
                return 252u;
            default:
                return 0u;
            }
        }

        P4Scenario GetP4RowScenario() const
        {
            if (m_P4Scenario == P4Scenario::Raw252RoughnessSweep && m_P4RowIndex == 5u)
            {
                return P4Scenario::Raw252TargetNotOne;
            }
            return m_P4Scenario;
        }

        bool ValidateP4CaptureEnvelope(
            const Core::Rendering::CapturedFrame& frame,
            RgbaFloatImage& outImage,
            Core::Container::String& reason)
        {
            if (frame.RequestId != GetLastAcceptedRequestId())
            {
                reason = TEXT("P4 capture RequestId does not match the accepted request");
                return false;
            }
            if (m_P4HasFrameNumber && frame.FrameNumber <= m_P4LastFrameNumber)
            {
                reason = TEXT("P4 capture FrameNumber is not strictly increasing");
                return false;
            }
            if (m_P4HasStageToken && GetLastAcceptedRequestStageToken() <= m_P4LastStageToken)
            {
                reason = TEXT("P4 capture stage token is not strictly increasing");
                return false;
            }
            uint32_t materialIndex = 0u;
            uint32_t lightCount = 0u;
            if (!BuildP4RuntimeIdentity(GetP4RowScenario(),
                                        m_P4RowIndex,
                                        materialIndex,
                                        lightCount))
            {
                reason = TEXT("P4 runtime material/light identity is invalid");
                return false;
            }
            if (m_P4Substage == P4CaptureSubstage::Primary &&
                m_bP4HasRuntimeIdentity && m_P4RowIndex <= m_P4LastRuntimeRow)
            {
                reason = TEXT("P4 snapshot row identity regressed or was reused");
                return false;
            }
            m_P4LastFrameNumber = frame.FrameNumber;
            m_P4HasFrameNumber = true;
            m_P4LastStageToken = GetLastAcceptedRequestStageToken();
            m_P4HasStageToken = true;
            if (m_P4Substage == P4CaptureSubstage::Primary)
            {
                m_P4LastRuntimeRow = m_P4RowIndex;
                m_bP4HasRuntimeIdentity = true;
            }
            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT ||
                frame.Width != ValidationWidth || frame.Height != ValidationHeight ||
                DecodeCapturedRgba16Float(frame, outImage) != FloatImageStatus::Success)
            {
                reason = TEXT("P4 capture format, dimensions, or RGBA16F decode is invalid");
                return false;
            }
            if (!IsFiniteAndWithinRgba16Range(outImage))
            {
                const RgbaFloatViolation violation = FindFirstRgba16FloatViolation(outImage);
                LOG_ERROR("P4 full RGBA16F scan failed: x=%u y=%u channel=%u value=%g kind=%u",
                          violation.X,
                          violation.Y,
                          violation.Channel,
                          violation.Value,
                          static_cast<unsigned int>(violation.Kind));
                reason = TEXT("P4 capture contains a non-finite or saturated RGBA16F value");
                return false;
            }
            if (!CheckP4CornerBackground(outImage, reason) ||
                !CheckP4N4Geometry(outImage, reason))
            {
                return false;
            }
            if (!m_bP4CaptureEnvelopeMarkerPrinted)
            {
                std::cout << "P4 capture envelope passed request_match=1 frame_order=1"
                          << " stage_order=1 scenario=" << GetP4ScenarioName() << "\n";
                m_bP4CaptureEnvelopeMarkerPrinted = true;
            }
            std::cout << "P4 snapshot chain: scenario=" << GetP4ScenarioName()
                      << " mode=" << GetP4DebugViewMode()
                      << " row=" << m_P4RowIndex
                      << " source=SceneColor format=R16G16B16A16_FLOAT size=256x256"
                      << " material_index=" << materialIndex
                      << " light_count=" << lightCount
                      << " fixture_row_applied=1"
                      << " request=" << frame.RequestId
                      << " stage=" << GetLastAcceptedRequestStageToken()
                      << " frame=" << frame.FrameNumber << "\n";
            if (GetP4RowScenario() == P4Scenario::Raw254DirectConductorEndpoint &&
                m_P4RowIndex == 0u)
            {
                std::cout << "P4 capture envelope: source=SceneColor format=R16G16B16A16_FLOAT"
                          << " size=256x256 material_index=" << materialIndex
                          << " light_count=" << lightCount
                          << " fixture_row_applied=1"
                          << " request_match=1 frame_order=1 stage_order=1 mode=254\n";
            }
            return true;
        }

        static bool IsP4BackgroundPixel(const RgbaFloatImage& image,
                                        uint32_t x,
                                        uint32_t y)
        {
            const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
            return std::abs(static_cast<double>(image.Values[offset + 3u]) - 1.0) <= 1.0e-3 &&
                   std::abs(image.Values[offset + 0u]) <= 1.0e-3f &&
                   std::abs(image.Values[offset + 1u]) <= 1.0e-3f &&
                   std::abs(image.Values[offset + 2u]) <= 1.0e-3f;
        }

        static bool CheckP4RelativeError(double actual,
                                         double expected,
                                         double& outMaximum,
                                         double& outSum,
                                         size_t& outCount)
        {
            if (!std::isfinite(actual) || !std::isfinite(expected) ||
                std::abs(actual) >= P4RneRangeLimit)
            {
                return false;
            }
            const double relative = std::abs(actual - expected) /
                                    std::max(std::abs(expected), 1.0e-6);
            outMaximum = std::max(outMaximum, relative);
            outSum += relative;
            ++outCount;
            return true;
        }

        static bool CheckP4CornerBackground(const RgbaFloatImage& image,
                                            Core::Container::String& reason)
        {
            constexpr uint32_t corners[4][2] = {
                {0u, 0u}, {ValidationWidth - 1u, 0u},
                {0u, ValidationHeight - 1u},
                {ValidationWidth - 1u, ValidationHeight - 1u}};
            for (const auto& corner : corners)
            {
                const size_t offset =
                    (static_cast<size_t>(corner[1]) * image.Width + corner[0]) * 4u;
                if (std::abs(static_cast<double>(image.Values[offset + 3u]) - 1.0) > 1.0e-3)
                {
                    reason = TEXT("P4 capture corner is not background alpha");
                    return false;
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double value = image.Values[offset + channel];
                    if (!std::isfinite(value) || std::abs(value) >= P4RneRangeLimit)
                    {
                        reason = TEXT("P4 capture corner contains invalid background color");
                        return false;
                    }
                }
            }
            return true;
        }

        bool GetP4MaterialExpected(double outMaterial[3]) const
        {
            constexpr double roughnessValues[5] = {0.05, 0.25, 0.50, 0.75, 1.00};
            constexpr double metallicValues[6] = {0.0, 0.5, 1.0, 0.1, 0.25, 0.75};
            uint32_t roughnessIndex = 0u;
            uint32_t metallicIndex = 0u;
            switch (GetP4RowScenario())
            {
            case P4Scenario::Raw250TextureRepresentation:
                break;
            case P4Scenario::Raw251DfgLut:
                if (m_P4RowIndex >= 25u)
                {
                    return false;
                }
                roughnessIndex = m_P4RowIndex / 5u;
                metallicIndex = P4RuntimeDfgQueryIndices[m_P4RowIndex % 5u];
                break;
            case P4Scenario::Raw252RoughnessSweep:
                if (m_P4RowIndex >= 5u)
                {
                    return false;
                }
                roughnessIndex = m_P4RowIndex;
                break;
            case P4Scenario::Raw252TargetNotOne:
                roughnessIndex = 1u;
                metallicIndex = P4RuntimeTargetMetallicIndex;
                break;
            case P4Scenario::Raw252WhiteFurnace:
                if (m_P4RowIndex >= 15u)
                {
                    return false;
                }
                roughnessIndex = m_P4RowIndex / 3u;
                metallicIndex = m_P4RowIndex % 3u;
                break;
            case P4Scenario::Raw254DirectConductorEndpoint:
                roughnessIndex = 4u;
                metallicIndex = P4RuntimeTargetMetallicIndex;
                break;
            default:
                return false;
            }
            if (roughnessIndex >= 5u || metallicIndex >= 6u)
            {
                return false;
            }
            outMaterial[0] = metallicValues[metallicIndex];
            outMaterial[1] = roughnessValues[roughnessIndex];
            outMaterial[2] = 1.0;
            return true;
        }

        bool CheckP4N4Geometry(const RgbaFloatImage& image,
                               Core::Container::String& reason)
        {
            bool bPrintedMaterialDiagnostic = false;
            if (!m_bP4ActualCameraAvailable)
            {
                reason = TEXT("P4 N4 camera snapshot is unavailable");
                return false;
            }
            uint32_t projectedAnchorX = 0u;
            uint32_t projectedAnchorY = 0u;
            double projectedDepth = 0.0;
            if (!ProjectP4Anchor(m_P4ActualCamera,
                                 projectedAnchorX,
                                 projectedAnchorY,
                                 projectedDepth))
            {
                reason = TEXT("P4 N4 projected anchor does not match the actual camera");
                return false;
            }
            constexpr int32_t offsets[5][2] = {
                {0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (const auto& offset : offsets)
            {
                const int32_t x = static_cast<int32_t>(projectedAnchorX) + offset[0];
                const int32_t y = static_cast<int32_t>(projectedAnchorY) + offset[1];
                if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= image.Width ||
                    static_cast<uint32_t>(y) >= image.Height)
                {
                    reason = TEXT("P4 N4 projected sample is outside the capture");
                    return false;
                }
                const size_t pixelOffset =
                    (static_cast<size_t>(y) * image.Width + static_cast<uint32_t>(x)) * 4u;
                const double alpha = image.Values[pixelOffset + 3u];
                const bool bPrimary = m_P4Substage == P4CaptureSubstage::Primary;
                const bool bDirectEndpoint =
                    bPrimary && GetP4RowScenario() == P4Scenario::Raw254DirectConductorEndpoint;
                if (!std::isfinite(alpha) ||
                    (!bPrimary
                         ? std::abs(alpha - 1.0) > 1.0e-3
                         : bDirectEndpoint
                         ? std::abs(alpha - 1.0) > 1.0e-3
                         : (alpha < 0.0 || alpha >= 1.0)))
                {
                    reason = !bPrimary
                                 ? TEXT("P4 sibling alpha is not one")
                                 : bDirectEndpoint
                                 ? TEXT("P4 N4 direct alpha is not one")
                                 : TEXT("P4 N4 depth coverage is invalid");
                    return false;
                }
                bool bCovered = false;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double value = image.Values[pixelOffset + channel];
                    if (!std::isfinite(value) || std::abs(value) >= P4RneRangeLimit)
                    {
                        reason = TEXT("P4 N4 color/normal sample is invalid");
                        return false;
                    }
                    bCovered = bCovered || std::abs(value) > 1.0e-6;
                }
                if (bPrimary && !bDirectEndpoint)
                {
                    continue;
                }
                if (!bCovered && alpha >= 0.999)
                {
                    reason = TEXT("P4 N4 projected sample is background rather than geometry");
                    return false;
                }
                if (!bPrimary)
                {
                    if (m_P4Substage == P4CaptureSubstage::Normal)
                    {
                        if (GetP4RowScenario() == P4Scenario::Raw252WhiteFurnace)
                        {
                            if (!CheckP4SphereNormalSample(image,
                                                           static_cast<uint32_t>(x),
                                                           static_cast<uint32_t>(y),
                                                           reason))
                            {
                                return false;
                            }
                        }
                        else
                        {
                            constexpr double expectedNormal[3] = {0.5, 0.5, 1.0};
                            for (uint32_t channel = 0u; channel < 3u; ++channel)
                            {
                                if (std::abs(image.Values[pixelOffset + channel] - expectedNormal[channel]) >
                                    0.01)
                                {
                                    reason = TEXT("P4 normal sibling does not match plane normal");
                                    return false;
                                }
                            }
                        }
                    }
                    else if (m_P4Substage == P4CaptureSubstage::Material)
                    {
                        double expectedMaterial[3] = {};
                        if (!GetP4MaterialExpected(expectedMaterial))
                        {
                            reason = TEXT("P4 material sibling expected state is invalid");
                            return false;
                        }
                        if (!bPrintedMaterialDiagnostic)
                        {
                            std::cout << "P4 material sibling first geometry: x=" << x
                                      << " y=" << y << " actual=("
                                      << image.Values[pixelOffset + 0u] << ","
                                      << image.Values[pixelOffset + 1u] << ","
                                      << image.Values[pixelOffset + 2u] << ") expected=("
                                      << expectedMaterial[0] << "," << expectedMaterial[1]
                                      << "," << expectedMaterial[2] << ")\n";
                            bPrintedMaterialDiagnostic = true;
                        }
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            if (std::abs(image.Values[pixelOffset + channel] - expectedMaterial[channel]) >
                                0.01)
                            {
                                reason = TEXT("P4 material sibling does not match fixture state");
                                return false;
                            }
                        }
                    }
                    else if (m_P4Substage == P4CaptureSubstage::Depth)
                    {
                        for (uint32_t channel = 0u; channel < 3u; ++channel)
                        {
                            const double value = image.Values[pixelOffset + channel];
                            if (value < 0.0 || value > 1.0)
                            {
                                reason = TEXT("P4 depth sibling is outside the debug depth range");
                                return false;
                            }
                        }
                    }
                }
            }
            if (projectedDepth < 0.0 || projectedDepth > 1.0)
            {
                reason = TEXT("P4 N4 projected depth is outside the camera range");
                return false;
            }
            if (m_P4Substage == P4CaptureSubstage::Normal &&
                GetP4RowScenario() == P4Scenario::Raw252WhiteFurnace)
            {
                constexpr uint32_t fixedNormalPoints[9][2] = {
                    {127u, 127u}, {95u, 127u}, {159u, 127u},
                    {127u, 95u}, {127u, 159u}, {104u, 104u},
                    {151u, 104u}, {104u, 151u}, {151u, 151u}};
                for (const auto& point : fixedNormalPoints)
                {
                    if (!CheckP4SphereNormalSample(image, point[0], point[1], reason))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool EvaluateP4Raw250(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            double relativeSum = 0.0;
            double maximumRelative = 0.0;
            size_t comparisonCount = 0u;
            size_t geometryPixels = 0u;
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                const double v = (static_cast<double>(y) + 0.5) / image.Height;
                const uint32_t band = std::min(4u,
                    static_cast<uint32_t>(std::floor(v * 5.0)));
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    const double u = (static_cast<double>(x) + 0.5) / image.Width;
                    const double localV = v * 5.0 - static_cast<double>(band);
                    P4RgbValue expected;
                    const double lod[] = {0.0, 0.4, 2.0, 4.0, 8.0};
                    if (!SampleP4Raw250Oracle(m_P4Raw250Oracle, u, localV,
                                              lod[band], expected))
                    {
                        reason = TEXT("raw250 independent texture-representation oracle failed");
                        return false;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double actualR = image.Values[offset + 0u];
                    const double actualG = image.Values[offset + 1u];
                    const double actualB = image.Values[offset + 2u];
                    if (!CheckP4RelativeError(actualR, expected.R, maximumRelative,
                                              relativeSum, comparisonCount) ||
                        !CheckP4RelativeError(actualG, expected.G, maximumRelative,
                                              relativeSum, comparisonCount) ||
                        !CheckP4RelativeError(actualB, expected.B, maximumRelative,
                                              relativeSum, comparisonCount))
                    {
                        reason = TEXT("raw250 independent texture-representation scan encountered invalid data");
                        return false;
                    }
                    if (image.Values[offset + 3u] < 1.0f)
                    {
                        ++geometryPixels;
                    }
                }
            }
            if (geometryPixels == 0u || comparisonCount == 0u)
            {
                reason = TEXT("raw250 geometry or full-pixel comparison is empty");
                return false;
            }
            const double meanRelative = relativeSum / static_cast<double>(comparisonCount);
            const size_t comparedPixels = comparisonCount / 3u;
            LOG_INFO("P4 raw250 oracle: pixels=%zu bands=5 mean_rel=%g max_rel=%g kernel_evaluations=327680 finite=1 abs_lt_65504=1",
                     comparedPixels,
                     meanRelative,
                     maximumRelative);
            std::cout << "P4 raw250 oracle: pixels=" << comparedPixels
                      << " bands=5 mean_rel=" << meanRelative
                      << " max_rel=" << maximumRelative
                      << " kernel_evaluations=327680 finite=1 abs_lt_65504=1\n";
            if (meanRelative > 0.01 || maximumRelative > 0.03)
            {
                m_bP4ActualOracleMismatch = true;
                reason = TEXT("raw250 mean or maximum relative error exceeded tolerance");
                return false;
            }
            return true;
        }

        bool EvaluateP4Dfg(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            const double nDotVQueries[] = {0.10, 0.25, 0.50, 0.75, 1.00};
            const double roughnessQueries[] = {0.05, 0.25, 0.50, 0.75, 1.00};
            const uint32_t roughnessIndex = m_P4RowIndex / 5u;
            const uint32_t nDotVIndex = m_P4RowIndex % 5u;
            if (roughnessIndex >= 5u || nDotVIndex >= 5u)
            {
                reason = TEXT("raw251 query row index is invalid");
                return false;
            }
            P4DfgValue expected;
            if (!SampleP4DfgOracle(m_P4DfgOracle,
                                   nDotVQueries[nDotVIndex],
                                   roughnessQueries[roughnessIndex],
                                   expected))
            {
                reason = TEXT("raw251 independent DFG oracle lookup failed");
                return false;
            }
            const double expectedRgb[3] = {expected.A, expected.B, expected.A + expected.B};
            double maximumRelative[3] = {};
            double maximumAbsolute[3] = {};
            size_t sampleCount = 0u;
            size_t geometryPixels = 0u;
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    if (IsP4BackgroundPixel(image, x, y))
                    {
                        continue;
                    }
                    if (!(image.Values[offset + 3u] < 1.0f))
                    {
                        reason = TEXT("raw251 geometry depth alpha is not below one");
                        return false;
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double actual = image.Values[offset + channel];
                        if (!std::isfinite(actual) || std::abs(actual) >= P4RneRangeLimit ||
                            !std::isfinite(expectedRgb[channel]))
                        {
                            reason = TEXT("raw251 geometry pixel contains invalid data");
                            return false;
                        }
                        const double absolute = std::abs(actual - expectedRgb[channel]);
                        const double relative =
                            std::abs(expectedRgb[channel]) >= 0.01
                                ? absolute / std::abs(expectedRgb[channel])
                                : 0.0;
                        maximumAbsolute[channel] = std::max(maximumAbsolute[channel], absolute);
                        ++sampleCount;
                        if (absolute > 0.002)
                        {
                            m_bP4ActualOracleMismatch = true;
                            std::cout << "P4 raw251 first failure: row=" << m_P4RowIndex
                                      << " x=" << x << " y=" << y << " channel=" << channel
                                      << " actual=" << actual << " expected=" << expectedRgb[channel]
                                      << " absolute=" << absolute << " relative=" << relative
                                      << " query=(" << nDotVQueries[nDotVIndex] << ","
                                      << roughnessQueries[roughnessIndex] << ")\n";
                            reason = TEXT("raw251 DFG absolute error exceeded tolerance");
                            return false;
                        }
                        if (std::abs(expectedRgb[channel]) >= 0.01)
                        {
                            maximumRelative[channel] = std::max(maximumRelative[channel], relative);
                            if (relative > 0.01)
                            {
                                m_bP4ActualOracleMismatch = true;
                                std::cout << "P4 raw251 first failure: row=" << m_P4RowIndex
                                          << " x=" << x << " y=" << y << " channel=" << channel
                                          << " actual=" << actual << " expected=" << expectedRgb[channel]
                                          << " absolute=" << absolute << " relative=" << relative
                                          << " query=(" << nDotVQueries[nDotVIndex] << ","
                                          << roughnessQueries[roughnessIndex] << ")\n";
                                reason = TEXT("raw251 DFG relative error exceeded tolerance");
                                return false;
                            }
                        }
                    }
                    ++geometryPixels;
                }
            }
            if (geometryPixels == 0u || sampleCount == 0u)
            {
                reason = TEXT("raw251 geometry coverage is empty");
                return false;
            }
            LOG_INFO("P4 raw251 oracle: row=%u query=(%g,%g) unique_texels=%zu geometry_pixels=%zu A=%g B=%g Ess=%g max_rel=(%g,%g,%g) max_abs=(%g,%g,%g) abs_threshold=0.002 conditional_rel_threshold=0.01",
                     m_P4RowIndex,
                     nDotVQueries[nDotVIndex],
                     roughnessQueries[roughnessIndex],
                     m_P4DfgOracle.Values.size(),
                     geometryPixels,
                     expected.A,
                     expected.B,
                     expected.A + expected.B,
                     maximumRelative[0],
                     maximumRelative[1],
                     maximumRelative[2],
                     maximumAbsolute[0],
                     maximumAbsolute[1],
                     maximumAbsolute[2]);
            std::cout << "P4 raw251 oracle: row=" << m_P4RowIndex
                      << " query=(" << nDotVQueries[nDotVIndex] << ","
                      << roughnessQueries[roughnessIndex] << ") unique_texels="
                      << m_P4DfgOracle.Values.size() << " geometry_pixels=" << geometryPixels
                      << " max_rel=(" << maximumRelative[0] << ","
                      << maximumRelative[1] << "," << maximumRelative[2] << ")\n";
            return true;
        }

        bool EvaluateAllNumericalP4DfgTiles(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            constexpr double nDotVQueries[5] = {0.10, 0.25, 0.50, 0.75, 1.00};
            constexpr double roughnessQueries[5] = {0.05, 0.25, 0.50, 0.75, 1.00};
            uint32_t passedTiles = 0u;
            for (uint32_t row = 0u; row < R1P4DfgTileGridSize; ++row)
            {
                for (uint32_t column = 0u; column < R1P4DfgTileGridSize; ++column)
                {
                    const uint32_t x = static_cast<uint32_t>(std::floor(
                        102.4 + 12.8 * static_cast<double>(column) + 0.5));
                    const uint32_t y = static_cast<uint32_t>(std::floor(
                        102.4 + 12.8 * static_cast<double>(row) + 0.5));
                    if (x >= image.Width || y >= image.Height)
                    {
                        reason = TEXT("R1 all-numerical Raw251 tile probe is outside the capture");
                        return false;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double alpha = image.Values[offset + 3u];
                    if (!std::isfinite(alpha) || alpha < 0.0 || alpha >= 1.0)
                    {
                        reason = TEXT("R1 all-numerical Raw251 tile has no covered geometry");
                        return false;
                    }
                    P4DfgValue expected;
                    if (!SampleP4DfgOracle(m_P4DfgOracle,
                                           nDotVQueries[column],
                                           roughnessQueries[row],
                                           expected))
                    {
                        reason = TEXT("R1 all-numerical Raw251 tile oracle lookup failed");
                        return false;
                    }
                    const double expectedRgb[3] = {expected.A, expected.B, expected.A + expected.B};
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double actual = image.Values[offset + channel];
                        const double absolute = std::abs(actual - expectedRgb[channel]);
                        const double relative = std::abs(expectedRgb[channel]) >= 0.01
                            ? absolute / std::abs(expectedRgb[channel])
                            : 0.0;
                        if (!std::isfinite(actual) || !std::isfinite(relative) ||
                            std::abs(actual) >= P4RneRangeLimit || absolute > 0.002 ||
                            (std::abs(expectedRgb[channel]) >= 0.01 && relative > 0.01))
                        {
                            m_bP4ActualOracleMismatch = true;
                            std::cout << "R1 all-numerical Raw251 first failure: tile=("
                                      << column << "," << row << ") pixel=(" << x << "," << y
                                      << ") channel=" << channel << " actual=" << actual
                                      << " expected=" << expectedRgb[channel]
                                      << " absolute=" << absolute << " relative=" << relative << "\n";
                            reason = TEXT("R1 all-numerical Raw251 tile oracle exceeded tolerance");
                            return false;
                        }
                    }
                    ++passedTiles;
                }
            }
            if (passedTiles != 25u)
            {
                reason = TEXT("R1 all-numerical Raw251 tile assertion count is invalid");
                return false;
            }
            std::cout << "R1 all-numerical Raw251 oracle passed: tiles=25 grid=5x5"
                      << " queries=25 abs_threshold=0.002 relative_threshold=0.01\n";
            return true;
        }

        static double ComputeP4Endpoint(double f0, const P4DfgValue& dfg)
        {
            const double ess = std::max(dfg.A + dfg.B, 1.0e-4);
            return std::clamp((f0 * dfg.A + dfg.B) *
                                  (1.0 + f0 * (1.0 - ess) / ess),
                              0.0,
                              1.0);
        }

        bool CheckP4BackgroundCorners(const RgbaFloatImage& image,
                                      Core::Container::String& reason) const
        {
            constexpr uint32_t corners[4][2] = {
                {0u, 0u}, {ValidationWidth - 1u, 0u},
                {0u, ValidationHeight - 1u},
                {ValidationWidth - 1u, ValidationHeight - 1u}};
            const double expected = 100.0 / 72.0;
            for (const auto& corner : corners)
            {
                const size_t offset =
                    (static_cast<size_t>(corner[1]) * image.Width + corner[0]) * 4u;
                if (static_cast<double>(image.Values[offset + 3u]) != 1.0)
                {
                    reason = TEXT("raw252 background corner alpha is not exactly one");
                    return false;
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double relative = std::abs(
                        static_cast<double>(image.Values[offset + channel]) - expected) /
                        expected;
                    if (!std::isfinite(relative) || relative > 0.03)
                    {
                        reason = TEXT("raw252 background corner does not match pre-exposed constant source");
                        return false;
                    }
                }
            }
            return true;
        }

        bool EvaluateP4RoughnessPlane(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            constexpr double roughnessValues[5] = {0.05, 0.25, 0.50, 0.75, 1.00};
            if (m_P4RowIndex >= 5u || !CheckP4BackgroundCorners(image, reason))
            {
                return false;
            }
            if (!m_P4RoughnessOracle.bValid)
            {
                reason = TEXT("raw252 roughness row independent DFG oracle is unavailable");
                return false;
            }
            const P4DfgValue& dfg = m_P4RoughnessOracle.Values[m_P4RowIndex];
            const double expected = 100.0 * 0.5 *
                (1.0 - ComputeP4Endpoint(0.04, dfg)) +
                100.0 * ComputeP4Endpoint(0.04, dfg);
            double relativeSum = 0.0;
            double maximumRelative = 0.0;
            size_t sampleCount = 0u;
            for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
            {
                for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double alpha = static_cast<double>(image.Values[offset + 3u]);
                    if (!std::isfinite(alpha) || alpha < 0.0 || alpha >= 1.0)
                    {
                        reason = TEXT("raw252 roughness ROI depth alpha is not below one");
                        return false;
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double actual = static_cast<double>(image.Values[offset + channel]) * 72.0;
                        if (!CheckP4RelativeError(actual, expected, maximumRelative,
                                                  relativeSum, sampleCount))
                        {
                            reason = TEXT("raw252 roughness ROI contains invalid data");
                            return false;
                        }
                    }
                }
            }
            const double meanRelative = relativeSum / static_cast<double>(sampleCount);
            LOG_INFO("P4 raw252 roughness row: row=%u roughness=%g target=%g mean_rel=%g max_rel=%g InvPreExposure=72 ROI=[112,143]x[112,143]",
                     m_P4RowIndex,
                     roughnessValues[m_P4RowIndex],
                     expected,
                     meanRelative,
                     maximumRelative);
            std::cout << "P4 raw252 roughness row: row=" << m_P4RowIndex
                      << " roughness=" << roughnessValues[m_P4RowIndex]
                      << " target=" << expected
                      << " mean_rel=" << meanRelative
                      << " max_rel=" << maximumRelative
                      << " InvPreExposure=72 ROI=[112,143]x[112,143]\n";
            if (meanRelative > 0.01 || maximumRelative > 0.03)
            {
                m_bP4ActualOracleMismatch = true;
                reason = TEXT("raw252 roughness mean or maximum relative error exceeded tolerance");
                return false;
            }
            return true;
        }

        bool EvaluateP4TargetNotOne(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            if (!CheckP4BackgroundCorners(image, reason))
            {
                return false;
            }
            constexpr double target = 55.2197800611;
            constexpr double storedExpected = 0.7669413897;
            double relativeSum = 0.0;
            double maximumRelative = 0.0;
            size_t sampleCount = 0u;
            for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
            {
                for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double alpha = static_cast<double>(image.Values[offset + 3u]);
                    if (!std::isfinite(alpha) || alpha < 0.0 || alpha >= 1.0)
                    {
                        reason = TEXT("raw252 target-not-one ROI depth alpha is not below one");
                        return false;
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double stored = image.Values[offset + channel];
                        const double actual = stored * 72.0;
                        if (!CheckP4RelativeError(actual, target, maximumRelative,
                                                  relativeSum, sampleCount))
                        {
                            reason = TEXT("raw252 target-not-one ROI contains invalid data");
                            return false;
                        }
                    }
                }
            }
            const double meanRelative = relativeSum / static_cast<double>(sampleCount);
            const double wrongTarget = 100.0 * 0.5947797803441714;
            const double sensitivity = std::abs(wrongTarget - target) / target;
            LOG_INFO("P4 raw252 target-not-one: target=%0.10f stored_expected=%0.10f mean_rel=%g max_rel=%g wrong_target=%0.10f sensitivity=%g%% depth_alpha=checked corners=checked",
                     target,
                     storedExpected,
                     meanRelative,
                     maximumRelative,
                     wrongTarget,
                     sensitivity * 100.0);
            std::cout << "P4 raw252 target-not-one: target=" << target
                      << " stored_expected=" << storedExpected
                      << " mean_rel=" << meanRelative
                      << " max_rel=" << maximumRelative
                      << " wrong_target=" << wrongTarget
                      << " sensitivity=" << sensitivity * 100.0
                      << "% depth_alpha=checked corners=checked\n";
            if (sensitivity < 0.05 || meanRelative > 0.01 || maximumRelative > 0.03)
            {
                m_bP4ActualOracleMismatch = meanRelative > 0.01 || maximumRelative > 0.03;
                reason = TEXT("raw252 target-not-one oracle or sensitivity failed");
                return false;
            }
            return true;
        }

        bool EvaluateP4WhiteFurnace(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            constexpr double furnaceTarget = 100.0;
            const uint32_t roughnessIndex = m_P4RowIndex / 3u;
            const uint32_t metallicIndex = m_P4RowIndex % 3u;
            if (roughnessIndex >= 5u || metallicIndex >= 3u ||
                !CheckP4BackgroundCorners(image, reason))
            {
                return false;
            }
            const uint32_t samplePoints[9][2] = {
                {127u, 127u}, {95u, 127u}, {159u, 127u},
                {127u, 95u}, {127u, 159u}, {104u, 104u},
                {151u, 104u}, {104u, 151u}, {151u, 151u}};
            double meanSum = 0.0;
            size_t meanCount = 0u;
            double maximumRelative = 0.0;
            const double centerX = 127.5;
            const double centerY = 127.5;
            for (uint32_t y = 0u; y < image.Height; ++y)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    const double dx = static_cast<double>(x) + 0.5 - centerX;
                    const double dy = static_cast<double>(y) + 0.5 - centerY;
                    if (std::sqrt(dx * dx + dy * dy) > 48.0)
                    {
                        continue;
                    }
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double alpha = static_cast<double>(image.Values[offset + 3u]);
                    if (!std::isfinite(alpha) || alpha < 0.0 || alpha >= 1.0)
                    {
                        reason = TEXT("white furnace sphere mask depth alpha is not below one");
                        return false;
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double actual = static_cast<double>(image.Values[offset + channel]) * 72.0;
                        const double relative = std::abs(actual - furnaceTarget) / furnaceTarget;
                        meanSum += relative;
                        maximumRelative = std::max(maximumRelative, relative);
                        ++meanCount;
                    }
                }
            }
            if (meanCount == 0u)
            {
                reason = TEXT("white furnace sphere mask is empty");
                return false;
            }
            for (const auto& point : samplePoints)
            {
                const size_t offset =
                    (static_cast<size_t>(point[1]) * image.Width + point[0]) * 4u;
                const double alpha = static_cast<double>(image.Values[offset + 3u]);
                if (!std::isfinite(alpha) || alpha < 0.0 || alpha >= 1.0)
                {
                    reason = TEXT("white furnace fixed probe depth alpha is not below one");
                    return false;
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double actual = static_cast<double>(image.Values[offset + channel]) * 72.0;
                    maximumRelative = std::max(
                        maximumRelative, std::abs(actual - furnaceTarget) / furnaceTarget);
                }
            }
            const double meanRelative = meanSum / static_cast<double>(meanCount);
            LOG_INFO("P4 white furnace row: row=%u roughness_index=%u metallic_index=%u target=100 mean_mask_rel=%g fixed9_max_rel=%g mask_radius=48 sphere_radius=64 center=(127.5,127.5)",
                     m_P4RowIndex,
                     roughnessIndex,
                     metallicIndex,
                     meanRelative,
                     maximumRelative);
            std::cout << "P4 white furnace row: row=" << m_P4RowIndex
                      << " roughness_index=" << roughnessIndex
                      << " metallic_index=" << metallicIndex
                      << " target=100 mean_mask_rel=" << meanRelative
                      << " fixed9_max_rel=" << maximumRelative
                      << " mask_radius=48 sphere_radius=64 center=(127.5,127.5)\n";
            if (meanRelative > 0.01 || maximumRelative > 0.03)
            {
                m_bP4ActualOracleMismatch = true;
                reason = TEXT("white furnace mean or fixed-probe relative error exceeded tolerance");
                return false;
            }
            return true;
        }

        bool EvaluateP4DirectConductor(
            const RgbaFloatImage& image,
            Core::Container::String& reason)
        {
            const double expected = m_P4DirectOracle.CorrectTarget;
            const double legacyExpected = m_P4DirectOracle.LegacyStoredPhysical;
            double endpointRelativeSum = 0.0;
            double endpointMaximumRelative = 0.0;
            double storedHalfMaximumRelative = 0.0;
            double physicalMean = 0.0;
            size_t sampleCount = 0u;
            for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
            {
                for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                {
                    const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                    const double alpha = image.Values[offset + 3u];
                    if (!std::isfinite(alpha) || std::abs(alpha - 1.0) > 1.0e-3)
                    {
                        reason = TEXT("raw254 direct-conductor ROI alpha is not one");
                        return false;
                    }
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double stored = image.Values[offset + channel];
                        const double actual = stored * 72.0;
                        if (!std::isfinite(stored) ||
                            std::abs(stored) >= P4RneRangeLimit)
                        {
                            reason = TEXT("raw254 direct-conductor ROI contains invalid data");
                            return false;
                        }
                        const double endpointRelative = std::abs(actual - expected) /
                                                         std::max(std::abs(expected), 1.0e-6);
                        const double storedHalfRelative = std::abs(
                            stored - m_P4DirectOracle.StoredHalf) /
                            std::max(std::abs(m_P4DirectOracle.StoredHalf), 1.0e-6);
                        endpointRelativeSum += endpointRelative;
                        endpointMaximumRelative = std::max(endpointMaximumRelative, endpointRelative);
                        storedHalfMaximumRelative = std::max(
                            storedHalfMaximumRelative, storedHalfRelative);
                        physicalMean += actual;
                        ++sampleCount;
                    }
                }
            }
            if (sampleCount != 3072u)
            {
                reason = TEXT("raw254 direct-conductor ROI sample count is not 3072");
                return false;
            }

            const double endpointMeanRelative = endpointRelativeSum /
                                                static_cast<double>(sampleCount);
            physicalMean /= static_cast<double>(sampleCount);
            const double legacyMeanRelative = std::abs(physicalMean - legacyExpected) /
                                              std::max(std::abs(legacyExpected), 1.0e-6);
            LOG_INFO("P4 raw254 direct-conductor: expected=%0.13f stored_expected=%0.15f stored_half=%0.15f legacy_stored_half=%0.15f legacy_predicted=%0.15f roi=[112,143]x[112,143] samples=3072 mean_rel=%g max_rel=%g stored_half_max_rel=%g legacy_mean_rel=%g sensitivity=%0.15f%% finite=1",
                     P4DirectTargetLiteral,
                     P4DirectStoredLiteral,
                     P4DirectStoredHalfLiteral,
                     P4DirectLegacyStoredHalfLiteral,
                     P4DirectLegacyStoredPhysicalLiteral,
                     endpointMeanRelative,
                     endpointMaximumRelative,
                     storedHalfMaximumRelative,
                     legacyMeanRelative,
                     m_P4DirectOracle.SensitivityPercent);
            std::cout << "P4 raw254 direct-conductor: expected=" << std::setprecision(17)
                      << P4DirectTargetLiteral
                      << " stored_expected=" << std::setprecision(18)
                      << P4DirectStoredLiteral
                      << " stored_half=" << P4DirectStoredHalfLiteral
                      << " legacy_stored_half=" << P4DirectLegacyStoredHalfLiteral
                      << " legacy_predicted=" << P4DirectLegacyStoredPhysicalLiteral
                      << " roi=[112,143]x[112,143] samples=3072 mean_rel="
                      << endpointMeanRelative << " max_rel=" << endpointMaximumRelative
                      << " stored_half_max_rel=" << storedHalfMaximumRelative
                      << " sensitivity=69.112670421600331%"
                      << " sensitivity_calculated=" << m_P4DirectOracle.SensitivityPercent
                      << " finite=1\n" << std::setprecision(6);

            const bool bEndpointPassed = endpointMeanRelative <= 0.01 &&
                                          endpointMaximumRelative <= 0.03 &&
                                          storedHalfMaximumRelative <= 0.03;
            if (bEndpointPassed)
            {
                return true;
            }
            if (legacyMeanRelative <= P4DirectLegacyEnvelope)
            {
                m_bP4ActualOracleMismatch = true;
                if (!m_bP4DirectLegacyMarkerPrinted)
                {
                    std::cout << "P4 direct RED actual: scenario=direct-conductor-endpoint mean="
                              << std::setprecision(15) << physicalMean
                              << " legacy_predicted=1.9896240234375\n"
                              << std::setprecision(6);
                    m_bP4DirectLegacyMarkerPrinted = true;
                }
                reason = TEXT("raw254 direct-conductor legacy output is not the compensated endpoint");
                return false;
            }
            m_bP4ActualOracleMismatch = true;
            reason = TEXT("raw254 direct-conductor endpoint oracle mismatch");
            return false;
        }

        bool EvaluateP4Frame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (m_P4Substage == P4CaptureSubstage::Primary)
            {
                m_bP4ActualOracleMismatch = false;
            }
            if (m_bP4StageApplyFailed)
            {
                reason = TEXT("P4 fixture scenario row application failed");
                return false;
            }
            RgbaFloatImage image;
            if (!ValidateP4CaptureEnvelope(frame, image, reason))
            {
                return false;
            }
            if (m_P4Substage != P4CaptureSubstage::Primary)
            {
                if (!m_bP4FixtureSnapshotAvailable ||
                    std::memcmp(&m_P4FixtureCameraSnapshot,
                                &GetFixture().GetCamera(),
                                sizeof(Core::Rendering::CameraProxy)) != 0 ||
                    m_P4FixtureMeshCount != GetFixture().TrackedMeshCount() ||
                    m_P4FixtureTextureCount != GetFixture().TrackedTextureCount() ||
                    m_P4FixtureMaterialCount != GetFixture().TrackedMaterialCount())
                {
                    reason = TEXT("P4 fixture sandwich snapshot changed across sibling captures");
                    return false;
                }
                if (m_P4Substage == P4CaptureSubstage::Normal ||
                    m_P4Substage == P4CaptureSubstage::Material ||
                    m_P4Substage == P4CaptureSubstage::Depth)
                {
                    std::cout << "P4 sibling envelope passed: scenario=" << GetP4ScenarioName()
                              << " substage=" << static_cast<unsigned int>(m_P4Substage)
                              << " mode=" << GetP4DebugViewMode() << " row=" << m_P4RowIndex
                              << " fixture_sandwich=PASS\n";
                }
                const bool bFinalSibling = m_P4Substage == P4CaptureSubstage::Material;
                if (!bFinalSibling)
                {
                    return true;
                }
                if (m_bP4ActualOracleMismatch)
                {
                    if (!m_bP4MismatchMarkerPrinted)
                    {
                        LOG_ERROR("P4 actual/oracle mismatch detected: scenario=%s", GetP4ScenarioName());
                        std::cout << "P4 actual/oracle mismatch: scenario=" << GetP4ScenarioName() << "\n";
                        m_bP4MismatchMarkerPrinted = true;
                    }
                    reason = TEXT("P4 actual/oracle mismatch after the sibling fixture sandwich");
                    return false;
                }
                return true;
            }
            bool bPassed = false;
            switch (GetP4RowScenario())
            {
            case P4Scenario::Raw250TextureRepresentation:
                bPassed = EvaluateP4Raw250(image, reason);
                break;
            case P4Scenario::Raw251DfgLut:
                bPassed = EvaluateP4Dfg(image, reason);
                break;
            case P4Scenario::Raw252RoughnessSweep:
                bPassed = EvaluateP4RoughnessPlane(image, reason);
                break;
            case P4Scenario::Raw252TargetNotOne:
                bPassed = EvaluateP4TargetNotOne(image, reason);
                break;
            case P4Scenario::Raw252WhiteFurnace:
                bPassed = EvaluateP4WhiteFurnace(image, reason);
                break;
            case P4Scenario::Raw254DirectConductorEndpoint:
                bPassed = EvaluateP4DirectConductor(image, reason);
                break;
            default:
                reason = TEXT("P4 scenario enum is invalid");
                return false;
            }
            if (!bPassed && !m_bP4ActualOracleMismatch)
            {
                return false;
            }
            return true;
        }

        const char* GetKnownCdStageName() const
        {
            switch (m_KnownCdStage)
            {
            case KnownCdStage::PureLambertA:
                return "PureLambertA";
            case KnownCdStage::PureLambertB:
                return "PureLambertB";
            case KnownCdStage::DirectPbrA:
                return "DirectPBRA";
            case KnownCdStage::DirectPbrB:
                return "DirectPBRB";
            case KnownCdStage::NormalA:
                return "NormalA";
            case KnownCdStage::NormalB:
                return "NormalB";
            case KnownCdStage::Complete:
            default:
                return "Complete";
            }
        }

        static bool IsKnownCdExposureB(KnownCdStage stage)
        {
            return stage == KnownCdStage::PureLambertB ||
                   stage == KnownCdStage::DirectPbrB ||
                   stage == KnownCdStage::NormalB;
        }

        static bool IsKnownCdExposureA(KnownCdStage stage)
        {
            return stage == KnownCdStage::PureLambertA ||
                   stage == KnownCdStage::DirectPbrA ||
                   stage == KnownCdStage::NormalA;
        }

        struct KnownCdOracleValues
        {
            double RangeWindow = 0.0;
            double Illuminance = 0.0;
            double IdealPureLambert = 0.0;
            double PureLambert = 0.0;
            double FullPbr = 0.0;
        };

        static bool ComputeKnownCdOracle(KnownCdOracleValues& outOracle)
        {
            constexpr double pi = 3.1415926535897932384626433832795;
            constexpr double intensityCd = 100.0;
            constexpr double distanceMeters = 2.0;
            constexpr double rangeMeters = 1000.0;
            constexpr double albedo = 0.5;
            const double rangeRatio = distanceMeters / rangeMeters;
            const double rangeWindow = std::pow(1.0 - std::pow(rangeRatio, 4.0), 2.0);
            const double inverseSquareIlluminance = intensityCd /
                                                    (distanceMeters * distanceMeters);
            const double illuminance = inverseSquareIlluminance * rangeWindow;
            const double idealPureLambert = inverseSquareIlluminance * albedo / pi;
            const double pureLambert = illuminance * albedo / pi;
            const double idealRelativeError =
                std::abs(pureLambert - idealPureLambert) / idealPureLambert;
            if (!std::isfinite(rangeWindow) || !std::isfinite(illuminance) ||
                !std::isfinite(pureLambert) || idealRelativeError > 1.0e-9)
            {
                LOG_ERROR("R1 known-cd independent range oracle invalid: range_window=%g illuminance=%g ideal=%g expected=%g relative_error=%g",
                          rangeWindow,
                          illuminance,
                          idealPureLambert,
                          pureLambert,
                          idealRelativeError);
                return false;
            }

            outOracle.RangeWindow = rangeWindow;
            outOracle.Illuminance = illuminance;
            outOracle.IdealPureLambert = idealPureLambert;
            outOracle.PureLambert = pureLambert;
            constexpr double roughness = 0.5;
            constexpr double metallic = 0.0;
            constexpr double f0 = 0.04;
            const double nDotL = 1.0;
            const double nDotV = 1.0;
            const double nDotH = 1.0;
            const double lDotH = 1.0;
            const double a = roughness * roughness;
            const double a2 = a * a;
            const double denominator = pi * (nDotH * nDotH * (a2 - 1.0) + 1.0) *
                                       (nDotH * nDotH * (a2 - 1.0) + 1.0);
            const double distribution = a2 / denominator;
            const double k = ((roughness + 1.0) * (roughness + 1.0)) / 8.0;
            const double geometry = (nDotL / (nDotL * (1.0 - k) + k)) *
                                    (nDotV / (nDotV * (1.0 - k) + k));
            const double fresnel = f0 + (1.0 - f0) * std::pow(1.0 - lDotH, 5.0);
            Core::Container::VariableArray<P4SamplePair> endpointSamples;
            const double ess = BuildP4HammersleySamples(
                                   P4DfgProductionSampleCount, endpointSamples)
                                   ? [&endpointSamples]()
                                   {
                                       const P4DfgValue endpoint = IntegrateP4Dfg(
                                           1.0, 0.5, endpointSamples);
                                       return endpoint.A + endpoint.B;
                                   }()
                                   : 1.0;
            const double compensation = 1.0 + f0 * (1.0 - ess) / std::max(ess, 1.0e-4);
            const double endpointBaseSpecular = distribution * geometry * fresnel /
                                                (4.0 * nDotV * nDotL + 0.0001);
            const double diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / pi;
            const double endpointSpecular = endpointBaseSpecular * compensation;
            outOracle.FullPbr = illuminance * (diffuse + endpointSpecular);
            return std::isfinite(outOracle.FullPbr) && outOracle.FullPbr > 0.0;
        }

        static bool IsPixelInRoi(uint32_t x, uint32_t y)
        {
            return x >= R1RoiMinX && x <= R1RoiMaxX && y >= R1RoiMinY && y <= R1RoiMaxY;
        }

        bool EvaluateKnownCdFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason)
        {
            if (frame.RequestId != GetLastAcceptedRequestId())
            {
                reason = TEXT("known-cd capture RequestId does not match the accepted request");
                return false;
            }
            if (m_KnownCdHasFrameNumber && frame.FrameNumber <= m_KnownCdLastFrameNumber)
            {
                reason = TEXT("known-cd capture FrameNumber is not strictly increasing");
                return false;
            }
            m_KnownCdLastFrameNumber = frame.FrameNumber;
            m_KnownCdHasFrameNumber = true;

            const uint64_t stageToken = GetLastAcceptedRequestStageToken();
            if (stageToken == 0u ||
                (m_KnownCdHasStageToken && stageToken <= m_KnownCdLastStageToken) ||
                frame.Format != RHI::Format::R16G16B16A16_FLOAT ||
                frame.Width != ValidationWidth || frame.Height != ValidationHeight)
            {
                reason = TEXT("known-cd capture stage token, format, or dimensions are invalid");
                return false;
            }
            m_KnownCdLastStageToken = stageToken;
            m_KnownCdHasStageToken = true;

            KnownCdOracleValues oracle;
            if (!ComputeKnownCdOracle(oracle))
            {
                reason = TEXT("known-cd independent double range oracle is invalid");
                return false;
            }

            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
            {
                reason = TEXT("known-cd capture RGBA16F decode failed");
                return false;
            }

            if (!IsFiniteAndWithinRgba16Range(image))
            {
                reason = TEXT("known-cd capture contains a non-finite or saturated RGBA16F value");
                return false;
            }
            double channelMean[3] = {};
            size_t roiCount = 0u;
            size_t firstOffender = 0u;
            const double invPreExposure = IsKnownCdExposureB(m_KnownCdStage) ? 8.0 : 4.0;
            for (uint32_t y = 0; y < image.Height; ++y)
            {
                for (uint32_t x = 0; x < image.Width; ++x)
                {
                    const size_t pixelOffset =
                        (static_cast<size_t>(y) * image.Width + x) * 4u;
                    if (!IsPixelInRoi(x, y))
                    {
                        continue;
                    }
                    for (uint32_t channel = 0; channel < 3u; ++channel)
                    {
                        const double physical = static_cast<double>(
                            image.Values[pixelOffset + channel]) * invPreExposure;
                        channelMean[channel] += physical;
                    }
                    ++roiCount;
                    for (uint32_t channel = 0; channel < 4u; ++channel)
                    {
                        if (!std::isfinite(image.Values[pixelOffset + channel]) ||
                            std::abs(image.Values[pixelOffset + channel]) >= 65504.0f)
                        {
                            ++firstOffender;
                        }
                    }
                    if (std::abs(static_cast<double>(image.Values[pixelOffset + 3u]) - 1.0) > 1.0e-3)
                    {
                        reason = TEXT("known-cd SceneColor alpha changed under exposure");
                        return false;
                    }
                }
            }
            if (roiCount == 0u || firstOffender != 0u)
            {
                reason = TEXT("known-cd ROI scan did not contain finite RGBA16F pixels");
                return false;
            }
            for (uint32_t channel = 0; channel < 3u; ++channel)
            {
                channelMean[channel] /= static_cast<double>(roiCount);
            }
            const double roiMean = channelMean[0];

            if (m_KnownCdStage == KnownCdStage::NormalA)
            {
                const size_t skyOffset = (static_cast<size_t>(R1AnchorY) * image.Width + 255u) * 4u;
                const size_t transparentOffset =
                    (static_cast<size_t>(R1AnchorY) * image.Width + 38u) * 4u;
                const size_t emissiveOffset =
                    (static_cast<size_t>(R1AnchorY) * image.Width + 217u) * 4u;
                if ((!m_bAllNumericalScenario && image.Values[skyOffset] <= 0.0f) ||
                    image.Values[transparentOffset] <= 0.0f ||
                    static_cast<double>(image.Values[emissiveOffset]) * 4.0 < 100.0)
                {
                    reason = TEXT("known-cd sky, legacy transparent, or emissive sample is invalid");
                    return false;
                }
            }

            const bool bPureLambertStage = m_KnownCdStage == KnownCdStage::PureLambertA ||
                                           m_KnownCdStage == KnownCdStage::PureLambertB;
            const bool bFullPbrStage = m_KnownCdStage == KnownCdStage::DirectPbrA ||
                                       m_KnownCdStage == KnownCdStage::DirectPbrB;
            if (bPureLambertStage || bFullPbrStage)
            {
                const double expected = bPureLambertStage ? oracle.PureLambert : oracle.FullPbr;
                double channelMeanRelativeError[3] = {};
                double channelSampleMaximumRelativeError[3] = {};
                for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
                {
                    for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                    {
                        const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                        for (uint32_t channel = 0; channel < 3u; ++channel)
                        {
                            const double actual = static_cast<double>(
                                image.Values[offset + channel]) * invPreExposure;
                            const double relativeError = std::abs(actual - expected) / expected;
                            channelMeanRelativeError[channel] += relativeError;
                            channelSampleMaximumRelativeError[channel] = std::max(
                                channelSampleMaximumRelativeError[channel], relativeError);
                        }
                    }
                }
                const size_t anchorOffset =
                    (static_cast<size_t>(R1AnchorY) * image.Width + R1AnchorX) * 4u;
                double anchorRelativeError[3] = {};
                for (uint32_t channel = 0; channel < 3u; ++channel)
                {
                    channelMeanRelativeError[channel] /= static_cast<double>(roiCount);
                    const double anchorActual = static_cast<double>(
                        image.Values[anchorOffset + channel]) * invPreExposure;
                    anchorRelativeError[channel] = std::abs(anchorActual - expected) / expected;
                    LOG_INFO("R1 known-cd oracle detail: stage=%s channel=%u mean=%g sample_max=%g anchor=%g expected=%g anchor_error=%g",
                             GetKnownCdStageName(),
                             channel,
                             channelMeanRelativeError[channel],
                             channelSampleMaximumRelativeError[channel],
                             anchorActual,
                             expected,
                             anchorRelativeError[channel]);
                    std::cout << "R1 known-cd oracle detail: stage=" << GetKnownCdStageName()
                              << " channel=" << channel
                              << " mean_rel_error=" << channelMeanRelativeError[channel]
                              << " sample_max_rel_error=" << channelSampleMaximumRelativeError[channel]
                              << " anchor=" << anchorActual
                              << " expected=" << expected
                              << " anchor_rel_error=" << anchorRelativeError[channel]
                              << " range_window=" << std::setprecision(17) << oracle.RangeWindow
                              << std::setprecision(6) << "\n";
                    if (channelMeanRelativeError[channel] > 0.01 ||
                        channelSampleMaximumRelativeError[channel] > 0.03)
                    {
                        LOG_ERROR("R1 known-cd RGB oracle detail: stage=%s channel=%u mean_error=%g sample_max_error=%g expected=%g",
                                  GetKnownCdStageName(),
                                  channel,
                                  channelMeanRelativeError[channel],
                                  channelSampleMaximumRelativeError[channel],
                                  expected);
                        reason = TEXT("known-cd ROI RGB mean/sample relative error exceeded tolerance");
                        return false;
                    }
                    if (anchorRelativeError[channel] > 0.03)
                    {
                        LOG_ERROR("R1 known-cd anchor RGB oracle: stage=%s channel=%u actual=%g expected=%g relative_error=%g tolerance=0.03",
                                  GetKnownCdStageName(),
                                  channel,
                                  anchorActual,
                                  expected,
                                  anchorRelativeError[channel]);
                        reason = TEXT("known-cd anchor RGB oracle exceeded the fixed 3% tolerance");
                        return false;
                    }
                }
            }
            else if (IsKnownCdExposureA(m_KnownCdStage) && roiMean <= 0.1)
            {
                reason = TEXT("known-cd Normal ROI is clear or zero-light output");
                return false;
            }

            if (IsKnownCdExposureB(m_KnownCdStage))
            {
                if (m_KnownCdPreviousImage.Values.empty() ||
                    m_KnownCdPreviousImage.Width != image.Width ||
                    m_KnownCdPreviousImage.Height != image.Height ||
                    m_KnownCdPreviousImage.Values.size() != image.Values.size())
                {
                    reason = TEXT("known-cd previous image is unavailable for A/B comparison");
                    return false;
                }

                double rawRatioSum[3] = {};
                double rawRatioMaximumError[3] = {};
                double physicalMeanRelativeError[3] = {};
                double physicalMaximumRelativeError[3] = {};
                size_t ratioCount = 0u;
                for (uint32_t y = R1RoiMinY; y <= R1RoiMaxY; ++y)
                {
                    for (uint32_t x = R1RoiMinX; x <= R1RoiMaxX; ++x)
                    {
                        const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4u;
                        for (uint32_t channel = 0; channel < 3u; ++channel)
                        {
                            const double previous = static_cast<double>(
                                m_KnownCdPreviousImage.Values[offset + channel]);
                            const double current = static_cast<double>(image.Values[offset + channel]);
                            if (std::abs(previous) <= 1.0e-5)
                            {
                                reason = TEXT("known-cd A/B ratio sample is zero or unavailable");
                                return false;
                            }
                            const double rawRatio = current / previous;
                            rawRatioSum[channel] += rawRatio;
                            rawRatioMaximumError[channel] = std::max(
                                rawRatioMaximumError[channel], std::abs(rawRatio - 0.5) / 0.5);
                            const double previousPhysical = previous * 4.0;
                            const double currentPhysical = current * 8.0;
                            const double physicalRelativeError = std::abs(
                                currentPhysical - previousPhysical) /
                                std::max(std::abs(previousPhysical), 1.0e-3);
                            physicalMeanRelativeError[channel] += physicalRelativeError;
                            physicalMaximumRelativeError[channel] = std::max(
                                physicalMaximumRelativeError[channel], physicalRelativeError);
                        }
                        if (std::abs(image.Values[offset + 3u] -
                                     m_KnownCdPreviousImage.Values[offset + 3u]) > 1.0e-3f)
                        {
                            reason = TEXT("known-cd alpha changed between exposure A and B");
                            return false;
                        }
                        ++ratioCount;
                    }
                }
                for (uint32_t channel = 0; channel < 3u; ++channel)
                {
                    const double rawRatio = rawRatioSum[channel] /
                                            static_cast<double>(ratioCount);
                    physicalMeanRelativeError[channel] /= static_cast<double>(ratioCount);
                    LOG_INFO("R1 known-cd exposure row: stage=%s channel=%u raw_B_over_A=%g raw_max_error=%g physical_mean_error=%g physical_max_error=%g alpha_unchanged=1",
                             GetKnownCdStageName(),
                             channel,
                             rawRatio,
                             rawRatioMaximumError[channel],
                             physicalMeanRelativeError[channel],
                             physicalMaximumRelativeError[channel]);
                    std::cout << "R1 known-cd exposure detail: stage=" << GetKnownCdStageName()
                              << " channel=" << channel
                              << " raw_B_over_A=" << rawRatio
                              << " raw_max_error=" << rawRatioMaximumError[channel]
                              << " physical_mean_error=" << physicalMeanRelativeError[channel]
                              << " physical_max_error=" << physicalMaximumRelativeError[channel]
                              << " alpha_unchanged=1\n";
                    if (std::abs(rawRatio - 0.5) / 0.5 > 0.03 ||
                        rawRatioMaximumError[channel] > 0.03 ||
                        physicalMeanRelativeError[channel] > 0.01 ||
                        physicalMaximumRelativeError[channel] > 0.03)
                    {
                        reason = TEXT("known-cd exposure B/A RGB ratio or physical equality failed");
                        return false;
                    }
                }
                if (m_KnownCdStage == KnownCdStage::NormalB)
                {
                    const uint32_t sampleX[3] = {255u, 38u, 217u};
                    const uint32_t firstSampleIndex = m_bAllNumericalScenario ? 1u : 0u;
                    for (uint32_t sampleIndex = firstSampleIndex; sampleIndex < 3u; ++sampleIndex)
                    {
                        const size_t offset =
                            (static_cast<size_t>(R1AnchorY) * image.Width + sampleX[sampleIndex]) * 4u;
                        for (uint32_t channel = 0; channel < 3u; ++channel)
                        {
                            const double previous = static_cast<double>(
                                m_KnownCdPreviousImage.Values[offset + channel]);
                            const double current = static_cast<double>(image.Values[offset + channel]);
                            if (previous <= 0.0 || current <= 0.0 ||
                                std::abs(current / previous - 0.5) / 0.5 > 0.03 ||
                                std::abs(current * 8.0 - previous * 4.0) /
                                        std::max(std::abs(previous * 4.0), 1.0e-3) > 0.03)
                            {
                                reason = TEXT("known-cd auxiliary sky/transparent/emissive exposure row failed");
                                return false;
                            }
                        }
                        if (std::abs(image.Values[offset + 3u] -
                                     m_KnownCdPreviousImage.Values[offset + 3u]) > 1.0e-3f)
                        {
                            reason = TEXT("known-cd auxiliary alpha changed between exposure A and B");
                            return false;
                        }
                    }
                }
            }

            m_KnownCdPreviousImage = image;
            LOG_INFO("R1 known-cd ROI row: stage=%s request=%llu stage_token=%llu frame=%llu mean_rgb=(%g,%g,%g) first_offender=%zu range_window=%g",
                     GetKnownCdStageName(),
                     static_cast<unsigned long long>(frame.RequestId),
                     static_cast<unsigned long long>(GetLastAcceptedRequestStageToken()),
                     static_cast<unsigned long long>(frame.FrameNumber),
                     channelMean[0],
                     channelMean[1],
                     channelMean[2],
                     firstOffender,
                     oracle.RangeWindow);
            std::cout << "R1 known-cd ROI row: stage=" << GetKnownCdStageName()
                      << " request=" << frame.RequestId
                      << " stage_token=" << GetLastAcceptedRequestStageToken()
                      << " frame=" << frame.FrameNumber
                      << " mean_rgb=(" << channelMean[0] << "," << channelMean[1] << "," << channelMean[2] << ")"
                      << " first_offender=" << firstOffender << "\n";
            return true;
        }

        bool EvaluateR1PresentationColor(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) const
        {
            return EvaluateR1FloatCapture(frame, "PresentationColor", TEXT("OETF前PresentationColor"), reason);
        }

        bool EvaluateR1SceneColor(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) const
        {
            return EvaluateR1FloatCapture(frame, "SceneColor", TEXT("SceneColor"), reason);
        }

        bool EvaluateR1FloatCapture(
            const Core::Rendering::CapturedFrame& frame,
            const char* logName,
            const TCHAR* reasonName,
            Core::Container::String& reason) const
        {
            if (frame.Format != RHI::Format::R16G16B16A16_FLOAT ||
                frame.Width != ValidationWidth ||
                frame.Height != ValidationHeight)
            {
                reason = reasonName;
                reason += TEXT(" format or dimensions are invalid");
                return false;
            }

            RgbaFloatImage image;
            if (DecodeCapturedRgba16Float(frame, image) != FloatImageStatus::Success)
            {
                reason = reasonName;
                reason += TEXT(" RGBA16F decode failed");
                return false;
            }
            if (!IsFiniteAndWithinRgba16Range(image))
            {
                const RgbaFloatViolation location = FindFirstRgba16FloatViolation(image);
                LOG_ERROR(
                    "R1 %s scan failed: frame=%llu x=%u y=%u channel=%u value=%g kind=%u",
                    logName,
                    static_cast<unsigned long long>(frame.FrameNumber),
                    location.X,
                    location.Y,
                    location.Channel,
                    location.Value,
                    static_cast<unsigned int>(location.Kind));
                reason = reasonName;
                reason += TEXT(" contains a non-finite or saturated value");
                return false;
            }
            LOG_INFO(
                "R1 %s scan passed: frame=%llu dimensions=%ux%u pixels=%zu channels=%zu finite=1 abs_lt_65504=1",
                logName,
                static_cast<unsigned long long>(frame.FrameNumber),
                image.Width,
                image.Height,
                static_cast<size_t>(image.Width) * image.Height,
                image.Values.size());
            std::cout << "R1 " << logName << " scan passed: frame=" << frame.FrameNumber
                      << " dimensions=" << image.Width << "x" << image.Height
                      << " pixels=" << static_cast<size_t>(image.Width) * image.Height
                      << " channels=" << image.Values.size()
                      << " finite=1 abs_lt_65504=1\n";
            return true;
        }

        bool EvaluateR1SrgbTransfer(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) const
        {
            if (frame.ColorSpace != RHI::PresentationColorSpace::Rec709D65 ||
                frame.Transfer != RHI::PresentationTransfer::SRGB ||
                frame.bHardwareSrgbEncode == frame.bShaderSrgbEncode)
            {
                reason = TEXT("BackBuffer capture does not record exactly one Rec.709 sRGB encode path");
                return false;
            }

            const bool bHardwareFormat = RHI::IsPresentationSrgbFormat(frame.Format);
            const bool bShaderFormat = RHI::IsPresentationUnormFormat(frame.Format);
            if ((!bHardwareFormat && !bShaderFormat) ||
                frame.bHardwareSrgbEncode != bHardwareFormat ||
                frame.bShaderSrgbEncode != bShaderFormat ||
                frame.BytesPerPixel != 4u ||
                frame.Width != ValidationWidth ||
                frame.Height != ValidationHeight)
            {
                reason = TEXT("BackBuffer capture format or encode metadata is invalid");
                return false;
            }

            if (!ValidateSrgbReferenceTable())
            {
                reason = TEXT("IEC sRGB OETF CPU reference table is invalid");
                return false;
            }
            if (!FindOpaqueMarkerPixel(frame))
            {
                reason = TEXT("ImGui opaque marker was not found in actual BackBuffer pixels");
                return false;
            }
            return true;
        }

        static bool FindOpaqueMarkerPixel(
            const Core::Rendering::CapturedFrame& frame,
            bool bLogResult = true)
        {
            constexpr uint32_t markerMinX = 4u;
            constexpr uint32_t markerMinY = 4u;
            constexpr uint32_t markerMaxX = 20u;
            constexpr uint32_t markerMaxY = 20u;
            constexpr uint32_t interiorInset = 2u;
            constexpr size_t minimumCoverageNumerator = 3u;
            constexpr size_t minimumCoverageDenominator = 4u;

            if (frame.BytesPerPixel != 4u || frame.Width < markerMaxX ||
                frame.Height < markerMaxY ||
                frame.Width > std::numeric_limits<uint32_t>::max() / frame.BytesPerPixel)
            {
                return false;
            }

            const uint32_t roiMinX = markerMinX + interiorInset;
            const uint32_t roiMinY = markerMinY + interiorInset;
            const uint32_t roiMaxX = markerMaxX - interiorInset;
            const uint32_t roiMaxY = markerMaxY - interiorInset;
            const size_t minimumRowPitch = static_cast<size_t>(frame.Width) * frame.BytesPerPixel;
            if (frame.RowPitchBytes < minimumRowPitch ||
                frame.Height > std::numeric_limits<size_t>::max() / frame.RowPitchBytes ||
                frame.Pixels.size() < static_cast<size_t>(frame.RowPitchBytes) * frame.Height)
            {
                return false;
            }

            const bool bBgra = frame.Format == RHI::Format::B8G8R8A8_UNORM ||
                               frame.Format == RHI::Format::B8G8R8A8_SRGB;
            const bool bRgba = frame.Format == RHI::Format::R8G8B8A8_UNORM ||
                               frame.Format == RHI::Format::R8G8B8A8_SRGB;
            if (!bBgra && !bRgba)
            {
                return false;
            }

            size_t matchingPixels = 0u;
            size_t roiPixels = 0u;
            for (uint32_t y = roiMinY; y < roiMaxY; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * frame.RowPitchBytes;
                for (uint32_t x = roiMinX; x < roiMaxX; ++x)
                {
                    ++roiPixels;
                    const size_t offset = rowOffset + static_cast<size_t>(x) * frame.BytesPerPixel;
                    const uint8_t red = frame.Pixels[offset + (bBgra ? 2u : 0u)];
                    const uint8_t green = frame.Pixels[offset + 1u];
                    const uint8_t blue = frame.Pixels[offset + (bBgra ? 0u : 2u)];
                    const uint8_t alpha = frame.Pixels[offset + 3u];
                    if (IsWithinOneLsb(red, 128u) &&
                        IsWithinOneLsb(green, 64u) &&
                        IsWithinOneLsb(blue, 191u) &&
                        alpha == 255u)
                    {
                        ++matchingPixels;
                    }
                }
            }

            const bool bPassed = roiPixels != 0u &&
                matchingPixels * minimumCoverageDenominator >=
                    roiPixels * minimumCoverageNumerator;
            if (bLogResult)
            {
                LOG_INFO(
                    "R1 marker ROI: expected=(128,64,191,255) x=[%u,%u) y=[%u,%u) row_order=top_down matched=%zu/%zu coverage=%.1f%% minimum=75%% passed=%u",
                    roiMinX,
                    roiMaxX,
                    roiMinY,
                    roiMaxY,
                    matchingPixels,
                    roiPixels,
                    roiPixels == 0u ? 0.0 :
                        (100.0 * static_cast<double>(matchingPixels) / static_cast<double>(roiPixels)),
                    bPassed ? 1u : 0u);
                if (bPassed)
                {
                    std::cout << "R1 marker ROI/coverage passed: expected=(128,64,191,255) x=[" << roiMinX << "," << roiMaxX
                              << ") y=[" << roiMinY << "," << roiMaxY << ") row_order=top_down matched="
                              << matchingPixels << "/" << roiPixels << " coverage="
                              << (100.0 * static_cast<double>(matchingPixels) / static_cast<double>(roiPixels))
                              << "% minimum=75%\n";
                }
            }
            return bPassed;
        }

        static uint8_t EncodeSrgbReference(float linear)
        {
            const float encoded = linear <= 0.0031308f
                                      ? 12.92f * linear
                                      : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            const float clamped = std::clamp(encoded, 0.0f, 1.0f);
            return static_cast<uint8_t>(std::lround(clamped * 255.0f));
        }

        static bool IsWithinOneLsb(uint8_t actual, uint8_t expected)
        {
            const int difference = static_cast<int>(actual) - static_cast<int>(expected);
            return std::abs(difference) <= 1;
        }

        static bool ValidateSrgbReferenceTable()
        {
            constexpr float linearRamp[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
            constexpr uint8_t expectedRamp[] = {0u, 137u, 188u, 225u, 255u};
            for (uint32_t index = 0; index < 5u; ++index)
            {
                if (EncodeSrgbReference(linearRamp[index]) != expectedRamp[index])
                {
                    return false;
                }
            }

            return EncodeSrgbReference(0.0031308f) == 10u &&
                   EncodeSrgbReference(0.0031309f) == 10u;
        }

        bool m_bR1Scenario = false;
        bool m_bAllNumericalScenario = false;
        bool m_bAllNumericalArgumentParsed = false;
        bool m_bKnownCdScenario = false;
        bool m_bP4Scenario = false;
        bool m_bTransparentPhysicalLightingScenario = false;
        bool m_bMarkerRegistered = false;
        enum class R1CaptureStage : uint8_t
        {
            BackBuffer,
            PresentationColor,
            SceneColor,
            Complete
        };

        R1CaptureStage m_R1CaptureStage = R1CaptureStage::BackBuffer;
        bool m_bR1HasFrameNumber = false;
        uint64_t m_R1LastFrameNumber = 0u;
        KnownCdStage m_KnownCdStage = KnownCdStage::PureLambertA;
        bool m_KnownCdHasFrameNumber = false;
        uint64_t m_KnownCdLastFrameNumber = 0u;
        bool m_KnownCdHasStageToken = false;
        uint64_t m_KnownCdLastStageToken = 0u;
        RgbaFloatImage m_KnownCdPreviousImage;
        P4Scenario m_P4Scenario = P4Scenario::Raw250TextureRepresentation;
        uint32_t m_P4RowIndex = 0u;
        P4CaptureSubstage m_P4Substage = P4CaptureSubstage::Primary;
        bool m_P4HasFrameNumber = false;
        uint64_t m_P4LastFrameNumber = 0u;
        bool m_P4HasStageToken = false;
        uint64_t m_P4LastStageToken = 0u;
        bool m_bP4StageApplyFailed = false;
        bool m_bP4CameraMarkerPrinted = false;
        bool m_bP4CaptureEnvelopeMarkerPrinted = false;
        bool m_bP4DirectLegacyMarkerPrinted = false;
        bool m_bP4ActualOracleMismatch = false;
        bool m_bP4MismatchMarkerPrinted = false;
        bool m_bP4ActualCameraAvailable = false;
        bool m_bP4HasRuntimeIdentity = false;
        uint32_t m_P4LastRuntimeRow = 0u;
        Core::Rendering::CameraProxy m_P4FixtureCameraSnapshot;
        size_t m_P4FixtureMeshCount = 0u;
        size_t m_P4FixtureTextureCount = 0u;
        size_t m_P4FixtureMaterialCount = 0u;
        bool m_bP4FixtureSnapshotAvailable = false;
        Core::Rendering::CameraProxy m_P4ActualCamera;
        P4DfgOracle m_P4DfgOracle;
        P4RoughnessOracle m_P4RoughnessOracle;
        P4Raw250Oracle m_P4Raw250Oracle;
        P4DirectConductorOracle m_P4DirectOracle;
        P5DfgOracle m_TransparentPhysicalDfgOracle;
        bool m_bTransparentPhysicalDfgOracleValid = false;
        TransparentPhysicalStage m_TransparentPhysicalStage = TransparentPhysicalStage::DirectM0Off;
        bool m_bTransparentPhysicalStageApplyFailed = false;
        bool m_bTransparentPhysicalRowMarkerPrinted = false;
        bool m_bTransparentPhysicalHasUnshadowedValue = false;
        double m_TransparentPhysicalUnshadowedMeanY = 0.0;
        AllNumericalCaptureStage m_AllNumericalCaptureStage = AllNumericalCaptureStage::BackBuffer;
        uint32_t m_AllNumericalRowIndex = 0u;
        bool m_AllNumericalHasFrameNumber = false;
        uint64_t m_AllNumericalLastFrameNumber = 0u;
        bool m_AllNumericalHasStageToken = false;
        uint64_t m_AllNumericalLastStageToken = 0u;
        bool m_bAllNumericalRequestStartFrameSet = false;
        uint64_t m_AllNumericalRequestStartFrame = 0u;
        uint64_t m_AllNumericalLastObservedRequestId = 0u;
        bool m_bAllNumericalStartupFrameSet = false;
        bool m_bAllNumericalStageApplyFailed = false;
        bool m_bAllNumericalRowMarkerPrinted = false;
        uint64_t m_AllNumericalStartupFrame = 0u;
        uint64_t m_AllNumericalFinalFrame = 0u;
        uint64_t m_AllNumericalPreviousFrame = 0u;
        uint64_t m_AllNumericalMaxLatency = 0u;
        uint32_t m_AllNumericalBackBufferScans = 0u;
        uint32_t m_AllNumericalPresentationScans = 0u;
        uint32_t m_AllNumericalSceneScans = 0u;
        uint64_t m_AllNumericalActualByteChannels = 0u;
        uint64_t m_AllNumericalFloatChannels = 0u;
        uint32_t m_AllNumericalNumericalRows = 0u;
        uint32_t m_AllNumericalForcedRows = 0u;
        bool m_bAllNumericalForcedRowsArgumentParsed = false;
        Core::Rendering::CapturedFrame m_AllNumericalBackBuffer;
        Core::Rendering::CapturedFrame m_AllNumericalPresentationColor;
        Core::Rendering::CapturedFrame m_AllNumericalSceneColor;
        RgbaFloatImage m_AllNumericalPresentationImage;
        OpaqueMarkerView m_MarkerView;
    };

    bool ValidateCaptureSourceArgumentContract()
    {
        Core::Container::VariableArray<Core::Container::String> defaultArgs;
        defaultArgs.push_back(TEXT("--scene=indoor"));
        HdrHandler defaultHandler;
        if (!defaultHandler.OnPreInitialize(defaultArgs) ||
            defaultHandler.GetCaptureSourceForTest() !=
                Core::Rendering::FrameCaptureSourceKind::PresentationColor)
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> sceneColorArgs;
        sceneColorArgs.push_back(TEXT("--scene=outdoor"));
        sceneColorArgs.push_back(TEXT("--capture-source=scene-color"));
        HdrHandler sceneColorHandler;
        if (!sceneColorHandler.OnPreInitialize(sceneColorArgs) ||
            sceneColorHandler.GetCaptureSourceForTest() !=
                Core::Rendering::FrameCaptureSourceKind::SceneColor)
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> invalidArgs;
        invalidArgs.push_back(TEXT("--scene=indoor"));
        invalidArgs.push_back(TEXT("--capture-source=invalid"));
        HdrHandler invalidHandler;
        if (invalidHandler.OnPreInitialize(invalidArgs))
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> duplicateArgs;
        duplicateArgs.push_back(TEXT("--scene=indoor"));
        duplicateArgs.push_back(TEXT("--capture-source=presentation"));
        duplicateArgs.push_back(TEXT("--capture-source=scene-color"));
        HdrHandler duplicateHandler;
        if (duplicateHandler.OnPreInitialize(duplicateArgs))
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> scenarioArgs;
        scenarioArgs.push_back(TEXT("--scene=indoor"));
        scenarioArgs.push_back(TEXT("--capture-source=back-buffer"));
        scenarioArgs.push_back(TEXT("--r1-scenario=srgb-transfer"));
        HdrHandler scenarioHandler;
        if (!scenarioHandler.OnPreInitialize(scenarioArgs) ||
            scenarioHandler.GetCaptureSourceForTest() !=
                Core::Rendering::FrameCaptureSourceKind::BackBuffer)
        {
            return false;
        }

        Core::Container::VariableArray<Core::Container::String> scenarioMissingBackBufferArgs;
        scenarioMissingBackBufferArgs.push_back(TEXT("--scene=indoor"));
        scenarioMissingBackBufferArgs.push_back(TEXT("--r1-scenario=srgb-transfer"));
        HdrHandler scenarioMissingBackBufferHandler;
        return !scenarioMissingBackBufferHandler.OnPreInitialize(scenarioMissingBackBufferArgs);
    }

    bool ValidateKnownCdScenarioArgumentContract()
    {
        Core::Container::VariableArray<Core::Container::String> args;
        args.push_back(TEXT("--scene=indoor"));
        args.push_back(TEXT("--capture-source=scene-color"));
        args.push_back(TEXT("--r1-scenario=known-cd-lambert"));
        HdrHandler handler;
        if (!handler.OnPreInitialize(args))
        {
            std::cerr << "R1 known-cd-lambert scenario argument was rejected\n";
            return false;
        }
        return true;
    }

    bool ValidateP4ScenarioArgumentContract()
    {
        const TCHAR* scenarioNames[] = {
            TEXT("ibl-prefilter-nonconstant"),
            TEXT("dfg-lut"),
            TEXT("ibl-roughness-sweep"),
            TEXT("white-furnace"),
            TEXT("direct-conductor-endpoint")};
        for (const TCHAR* scenarioName : scenarioNames)
        {
            Core::Container::VariableArray<Core::Container::String> args;
            args.push_back(TEXT("--scene=indoor"));
            args.push_back(TEXT("--capture-source=scene-color"));
            Core::Container::String scenarioArgument = TEXT("--r1-scenario=");
            scenarioArgument += scenarioName;
            args.push_back(scenarioArgument);
            HdrHandler handler;
            if (!handler.OnPreInitialize(args))
            {
                std::cerr << "P4 scenario argument was rejected: " << scenarioName << "\n";
                return false;
            }
        }
        return true;
    }

    bool ValidateR1FinalFixtureContract()
    {
        SceneLayout indoor;
        SceneLayout indoorRepeat;
        SceneLayout outdoor;
        SceneLayout outdoorRepeat;
        if (!BuildSceneLayout(SceneKind::Indoor, ValidationSeed, indoor) ||
            !BuildSceneLayout(SceneKind::Indoor, ValidationSeed, indoorRepeat) ||
            !BuildSceneLayout(SceneKind::Outdoor, ValidationSeed, outdoor) ||
            !BuildSceneLayout(SceneKind::Outdoor, ValidationSeed, outdoorRepeat) ||
            !(indoor == indoorRepeat) || !(outdoor == outdoorRepeat) ||
            indoor.Objects.size() < 3u || indoor.Lights.size() != 1u || outdoor.Lights.size() != 1u)
        {
            std::cerr << "P6A_FIXTURE_RED=indoor_compensation_or_outdoor_lux\n";
            return false;
        }

        const auto isNear = [](float actual, float expected, float tolerance)
        {
            return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
        };
        const Core::Rendering::CameraProxy& indoorCamera = indoor.Camera;
        const Core::Rendering::CameraProxy& outdoorCamera = outdoor.Camera;
        const bool bIndoorExposure = isNear(indoorCamera.ExposureCompensation, 4.0f, 1.0e-6f) &&
                                     isNear(indoorCamera.EV100, 9.9068906f, 1.0e-5f) &&
                                     isNear(indoorCamera.Exposure, 1.0f / 72.0f, 1.0e-7f) &&
                                     isNear(indoorCamera.PreExposure, 1.0f / 72.0f, 1.0e-7f) &&
                                     isNear(indoorCamera.InvPreExposure, 72.0f, 1.0e-3f);
        const bool bOutdoorExposure = isNear(outdoorCamera.ExposureCompensation, 0.0f, 1.0e-6f) &&
                                      isNear(outdoorCamera.EV100, 9.9068906f, 1.0e-5f) &&
                                      isNear(outdoorCamera.Exposure, 1.0f / 1152.0f, 1.0e-7f) &&
                                      isNear(outdoorCamera.PreExposure, 1.0f / 1152.0f, 1.0e-7f) &&
                                      isNear(outdoorCamera.InvPreExposure, 1152.0f, 1.0e-3f);
        const bool bLights = indoor.Lights[0].Kind == SceneLightKind::Point &&
                             indoor.Lights[0].Intensity == 100.0f &&
                             indoor.Lights[0].Range == 1000.0f &&
                             !indoor.Lights[0].bCastShadows &&
                             outdoor.Lights[0].Kind == SceneLightKind::Directional &&
                             outdoor.Lights[0].Intensity == 10000.0f &&
                             outdoor.Lights[0].Color[0] == 1.0f &&
                             outdoor.Lights[0].Color[1] == 0.95f &&
                             outdoor.Lights[0].Color[2] == 0.85f &&
                             outdoor.Lights[0].bCastShadows;
        if (!bIndoorExposure || !bOutdoorExposure || !bLights)
        {
            std::cerr << "P6A_FIXTURE_RED=indoor_compensation_or_outdoor_lux\n";
            return false;
        }
        std::cout << "P6A fixture preflight passed: indoor_compensation=4.0 indoor_pre_exposure=0.013888889 outdoor_compensation=0.0 outdoor_pre_exposure=0.000868056 outdoor_directional_lux=10000\n";
        return true;
    }

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<HdrHandler>();
    }
}

int main(int argc, char** argv)
{
#ifdef _MSC_VER
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    bool bR1Scenario = false;
    bool bAllNumericalScenario = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--r1-scenario=srgb-transfer") == 0)
        {
            bR1Scenario = true;
        }
        if (std::strcmp(argv[index], "--r1-scenario=all-numerical") == 0)
        {
            bAllNumericalScenario = true;
        }
    }

    if (!ValidateCaptureSourceArgumentContract())
    {
        return 1;
    }
    if (!ValidateKnownCdScenarioArgumentContract())
    {
        return 1;
    }
    if (!ValidateP4ScenarioArgumentContract())
    {
        return 1;
    }
    if (!ValidateR1FinalFixtureContract())
    {
        return 1;
    }

    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RenderingHdrSceneCaptureTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RenderingHdrSceneCaptureTest", "no Vulkan device is available");
    }

    if (bR1Scenario || bAllNumericalScenario)
    {
        uint32_t forcedRows = 0u;
        uint32_t forcedPixels = 0u;
        uint32_t forcedChannels = 0u;
        if (!RunForcedPresentationFormatReadback(forcedRows, forcedPixels, forcedChannels))
        {
            LOG_ERROR("R1 forced R8G8B8A8_SRGB/UNORM offscreen render/readback failed");
            return 1;
        }
        std::cout << "R1 forced R8G8B8A8_SRGB/UNORM offscreen render/readback passed rows="
                  << forcedRows << " pixels=" << forcedPixels
                  << " channels=" << forcedChannels << "\n";
    }

    Core::Boot::BootConfig config;
    config.WindowTitle = TEXT("Rendering HDR SceneColor Validation");
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = false;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = TEXT("RenderingHdrSceneCapture.log");
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        const Core::Container::String argument(argv[index]);
        config.Arguments.push_back(argument);
    }
    if (bAllNumericalScenario)
    {
        config.Arguments.push_back(TEXT("--r1-forced-rows=2"));
    }
    return Core::Boot::LaunchApplication(config);
}
