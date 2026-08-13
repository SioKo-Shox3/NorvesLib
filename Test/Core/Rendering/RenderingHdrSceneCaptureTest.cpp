#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Application/IApplicationHandler.h"
#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "Module/ModuleRegistry.h"
#include "ImGuiModule/IImGuiView.h"
#include "ImGuiModule/ImGuiModule.h"
#include "Rendering/ShaderManager.h"
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
#include <iostream>
#include <limits>

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

    bool RunForcedPresentationFormatReadback()
    {
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
        std::cout << "R1 forced actual readback passed: format=R8G8B8A8_SRGB "
                     "encode_path=hardware_srgb samples=7 channels=4 lsb=1\n";
        if (!VerifyForcedPresentationFormat(device.get(),
                                            RHI::Format::R8G8B8A8_UNORM,
                                            RHI::PresentationEncodePath::ShaderOETF))
        {
            return false;
        }
        std::cout << "R1 forced actual readback passed: format=R8G8B8A8_UNORM "
                     "encode_path=shader_oetf samples=7 channels=4 lsb=1\n";
        return true;
    }

    class HdrHandler final : public RenderingValidationApplicationHandler
    {
    public:
        Core::Rendering::FrameCaptureSourceKind GetCaptureSourceForTest() const
        {
            return GetRunConfig().CaptureSource;
        }

        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            if (!RenderingValidationApplicationHandler::OnPreInitialize(args))
            {
                return false;
            }
            if (m_bR1Scenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::BackBuffer)
            {
                LOG_ERROR("--r1-scenario=srgb-transfer は BackBuffer capture と組み合わせてください");
                return false;
            }
            m_R1CaptureStage = R1CaptureStage::BackBuffer;
            m_bR1HasFrameNumber = false;
            m_R1LastFrameNumber = 0u;
            return true;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize())
            {
                return false;
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
            if (argument == TEXT("--r1-scenario=srgb-transfer"))
            {
                if (m_bR1Scenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bR1Scenario = true;
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument, outFailureReason);
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) override
        {
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

        bool RequestFollowupCapture(
            const Core::Rendering::CapturedFrame& frame,
            Core::Rendering::FrameCaptureRequest& outRequest) override
        {
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

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<HdrHandler>();
    }
}

int main(int argc, char** argv)
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    bool bR1Scenario = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--r1-scenario=srgb-transfer") == 0)
        {
            bR1Scenario = true;
            break;
        }
    }

    if (!ValidateCaptureSourceArgumentContract())
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

    if (bR1Scenario)
    {
        if (!RunForcedPresentationFormatReadback())
        {
            LOG_ERROR("R1 forced R8G8B8A8_SRGB/UNORM offscreen render/readback failed");
            return 1;
        }
        std::cout << "R1 forced R8G8B8A8_SRGB/UNORM offscreen render/readback passed\n";
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
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
