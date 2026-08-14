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
            if (m_bKnownCdScenario &&
                GetRunConfig().CaptureSource != Core::Rendering::FrameCaptureSourceKind::SceneColor)
            {
                LOG_ERROR("--r1-scenario=known-cd-lambert は SceneColor capture と組み合わせてください");
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
            if (argument == TEXT("--r1-scenario=known-cd-lambert"))
            {
                if (m_bKnownCdScenario || m_bR1Scenario)
                {
                    outFailureReason = TEXT("duplicate r1 scenario");
                    return false;
                }
                m_bKnownCdScenario = true;
                return true;
            }
            return RenderingValidationApplicationHandler::ParseAdditionalArgument(argument, outFailureReason);
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame& frame,
            Core::Container::String& reason) override
        {
            if (m_bKnownCdScenario)
            {
                return EvaluateKnownCdFrame(frame, reason);
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
            const double specular = distribution * geometry * fresnel /
                                    (4.0 * nDotV * nDotL + 0.0001);
            const double diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / pi;
            outOracle.FullPbr = illuminance * (diffuse + specular);
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
                if (image.Values[skyOffset] <= 0.0f || image.Values[transparentOffset] <= 0.0f ||
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
                    for (uint32_t sampleIndex = 0; sampleIndex < 3u; ++sampleIndex)
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
        bool m_bKnownCdScenario = false;
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
    if (!ValidateKnownCdScenarioArgumentContract())
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
