#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanTexture.h"

#include <atomic>
#include <filesystem>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr uint32_t TestTextureExtent = 4u;

    std::atomic<uint32_t> GValidationErrorCount{0u};
    std::atomic<const char*> GValidationCaseName{"initialization"};

    VKAPI_ATTR VkBool32 VKAPI_CALL CountValidationError(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void*)
    {
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) == 0u)
        {
            return VK_FALSE;
        }

        GValidationErrorCount.fetch_add(1u, std::memory_order_relaxed);
        const char* caseName = GValidationCaseName.load(std::memory_order_relaxed);
        std::cerr << "validation_case=" << (caseName ? caseName : "unknown")
                  << " message_id="
                  << (callbackData && callbackData->pMessageIdName
                          ? callbackData->pMessageIdName
                          : "unknown")
                  << " message="
                  << (callbackData && callbackData->pMessage
                          ? callbackData->pMessage
                          : "unknown")
                  << '\n';
        return VK_FALSE;
    }

    RHI::DescriptorBinding MakeBinding(uint32_t binding, RHI::ResourceBindType type)
    {
        RHI::DescriptorBinding result;
        result.binding = binding;
        result.type = type;
        result.stages = RHI::ShaderStage::Compute;
        return result;
    }

    RHI::TexturePtr CreateTexture(
        RHI::IDevice& device,
        RHI::Format format,
        RHI::ResourceUsage usage,
        uint32_t mipLevels,
        const char* debugName)
    {
        RHI::TextureDesc desc;
        desc.Width = TestTextureExtent;
        desc.Height = TestTextureExtent;
        desc.MipLevels = mipLevels;
        desc.TextureFormat = format;
        desc.Usage = usage;
        desc.DebugName = debugName;
        return device.CreateTexture(desc);
    }

    RHI::SamplerPtr CreatePointSampler(RHI::IDevice& device)
    {
        RHI::SamplerDesc desc;
        desc.filterMin = RHI::FilterMode::Point;
        desc.filterMag = RHI::FilterMode::Point;
        desc.filterMip = RHI::FilterMode::Point;
        desc.addressU = RHI::TextureAddressMode::Clamp;
        desc.addressV = RHI::TextureAddressMode::Clamp;
        desc.addressW = RHI::TextureAddressMode::Clamp;
        return device.CreateSampler(desc);
    }

    RHI::PipelinePtr CreateComputePipeline(
        RHI::IDevice& device,
        const char* caseName,
        const char* source,
        const RHI::DescriptorSetDesc& descriptorSetDesc)
    {
        RHI::ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!compiler)
        {
            std::cerr << "shader compiler creation failed for " << caseName << '\n';
            return {};
        }

        const RHI::ShaderCompileResult compileResult = compiler->CompileFromSource(
            Core::Container::String(source),
            RHI::ShaderStage::Compute,
            Core::Container::String(caseName));
        if (!compileResult.bSuccess)
        {
            std::cerr << "shader compilation failed for " << caseName << ": "
                      << compileResult.ErrorMessage << '\n';
            return {};
        }

        RHI::ShaderDesc shaderDesc;
        shaderDesc.stage = RHI::ShaderStage::Compute;
        shaderDesc.byteCode = compileResult.ByteCode;
        RHI::ShaderPtr shader = device.CreateShader(shaderDesc);
        if (!shader)
        {
            std::cerr << "shader creation failed for " << caseName << '\n';
            return {};
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = shader;
        pipelineDesc.descriptorSetLayouts.push_back(descriptorSetDesc);
        return device.CreateComputePipeline(pipelineDesc);
    }

    bool CompleteDispatch(
        RHI::IDevice& device,
        const char* caseName,
        const RHI::CommandListPtr& commandList)
    {
        if (!commandList)
        {
            std::cerr << "command list creation failed for " << caseName << '\n';
            return false;
        }

        commandList->End();
        commandList->Submit(true);
        device.WaitIdle();
        std::cout << "dispatch_completed=" << caseName << '\n';
        return true;
    }

    bool RunSampledImageCase(RHI::IDevice& device)
    {
        constexpr const char* CaseName = "sampled_image";
        constexpr const char* ShaderSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D sourceTexture;
layout(set = 0, binding = 1) uniform sampler sourceSampler;
layout(r32f, set = 0, binding = 2) uniform writeonly image2D outputImage;
void main()
{
    float value = texture(sampler2D(sourceTexture, sourceSampler), vec2(0.5)).r;
    imageStore(outputImage, ivec2(0, 0), vec4(value));
}
)glsl";

        GValidationCaseName.store(CaseName, std::memory_order_relaxed);
        RHI::DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(MakeBinding(0u, RHI::ResourceBindType::Texture));
        setDesc.bindings.push_back(MakeBinding(1u, RHI::ResourceBindType::Sampler));
        setDesc.bindings.push_back(MakeBinding(2u, RHI::ResourceBindType::RWTexture));

        RHI::PipelinePtr pipeline = CreateComputePipeline(device, CaseName, ShaderSource, setDesc);
        RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(setDesc);
        RHI::TexturePtr source = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderRead,
            1u,
            "ImageLayoutSampledSource");
        RHI::TexturePtr output = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderWrite,
            1u,
            "ImageLayoutSampledOutput");
        RHI::SamplerPtr sampler = CreatePointSampler(device);
        RHI::CommandListPtr commandList = device.CreateCommandList();
        if (!pipeline || !descriptorSet || !source || !output || !sampler || !commandList)
        {
            std::cerr << "resource creation failed for " << CaseName << '\n';
            return false;
        }

        commandList->Begin();
        commandList->TextureBarrier(
            source,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::ShaderResource);
        commandList->TextureBarrier(
            output,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess);
        descriptorSet->BindTexture(0u, source);
        descriptorSet->BindSampler(1u, sampler);
        descriptorSet->BindStorageTexture(2u, output);
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        return CompleteDispatch(device, CaseName, commandList);
    }

    bool RunCombinedDepthCase(RHI::IDevice& device)
    {
        constexpr const char* CaseName = "combined_depth";
        constexpr const char* ShaderSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D sourceDepth;
layout(r32f, set = 0, binding = 1) uniform writeonly image2D outputImage;
void main()
{
    float value = texture(sourceDepth, vec2(0.5)).r;
    imageStore(outputImage, ivec2(0, 0), vec4(value));
}
)glsl";

        GValidationCaseName.store(CaseName, std::memory_order_relaxed);
        RHI::DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(MakeBinding(0u, RHI::ResourceBindType::CombinedImageSampler));
        setDesc.bindings.push_back(MakeBinding(1u, RHI::ResourceBindType::RWTexture));

        RHI::PipelinePtr pipeline = CreateComputePipeline(device, CaseName, ShaderSource, setDesc);
        RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(setDesc);
        RHI::TexturePtr depth = CreateTexture(
            device,
            RHI::Format::D32_FLOAT,
            RHI::ResourceUsage::DepthStencil | RHI::ResourceUsage::ShaderRead,
            1u,
            "ImageLayoutCombinedDepth");
        RHI::TexturePtr output = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderWrite,
            1u,
            "ImageLayoutCombinedDepthOutput");
        RHI::SamplerPtr sampler = CreatePointSampler(device);
        RHI::CommandListPtr commandList = device.CreateCommandList();
        if (!pipeline || !descriptorSet || !depth || !output || !sampler || !commandList)
        {
            std::cerr << "resource creation failed for " << CaseName << '\n';
            return false;
        }

        commandList->Begin();
        commandList->TextureBarrier(
            depth,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::ShaderResource);
        commandList->TextureBarrier(
            output,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess);
        descriptorSet->BindTexture(0u, depth);
        descriptorSet->BindSampler(0u, sampler);
        descriptorSet->BindStorageTexture(1u, output);
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        return CompleteDispatch(device, CaseName, commandList);
    }

    bool RunStorageImageCase(RHI::IDevice& device)
    {
        constexpr const char* CaseName = "storage_image";
        constexpr const char* ShaderSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(r32f, set = 0, binding = 0) uniform image2D storageImage;
void main()
{
    float value = imageLoad(storageImage, ivec2(0, 0)).r;
    imageStore(storageImage, ivec2(0, 0), vec4(value + 1.0));
}
)glsl";

        GValidationCaseName.store(CaseName, std::memory_order_relaxed);
        RHI::DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(MakeBinding(0u, RHI::ResourceBindType::RWTexture));

        RHI::PipelinePtr pipeline = CreateComputePipeline(device, CaseName, ShaderSource, setDesc);
        RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(setDesc);
        RHI::TexturePtr storage = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderWrite,
            1u,
            "ImageLayoutStorageImage");
        RHI::CommandListPtr commandList = device.CreateCommandList();
        if (!pipeline || !descriptorSet || !storage || !commandList)
        {
            std::cerr << "resource creation failed for " << CaseName << '\n';
            return false;
        }

        commandList->Begin();
        commandList->TextureBarrier(
            storage,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess);
        descriptorSet->BindStorageTexture(0u, storage);
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        return CompleteDispatch(device, CaseName, commandList);
    }

    bool RunGeneralSampledCase(RHI::IDevice& device)
    {
        constexpr const char* CaseName = "general_sampled";
        constexpr const char* ShaderSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform texture2D sourceTexture;
layout(set = 0, binding = 1) uniform sampler sourceSampler;
layout(r32f, set = 0, binding = 2) uniform writeonly image2D outputImage;
void main()
{
    float value = texture(sampler2D(sourceTexture, sourceSampler), vec2(0.5)).r;
    imageStore(outputImage, ivec2(0, 0), vec4(value));
}
)glsl";

        GValidationCaseName.store(CaseName, std::memory_order_relaxed);
        RHI::DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(MakeBinding(0u, RHI::ResourceBindType::Texture));
        setDesc.bindings.push_back(MakeBinding(1u, RHI::ResourceBindType::Sampler));
        setDesc.bindings.push_back(MakeBinding(2u, RHI::ResourceBindType::RWTexture));

        RHI::PipelinePtr pipeline = CreateComputePipeline(device, CaseName, ShaderSource, setDesc);
        RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(setDesc);
        RHI::TexturePtr source = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite,
            1u,
            "ImageLayoutGeneralSampledSource");
        RHI::TexturePtr output = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderWrite,
            1u,
            "ImageLayoutGeneralSampledOutput");
        RHI::SamplerPtr sampler = CreatePointSampler(device);
        RHI::CommandListPtr commandList = device.CreateCommandList();
        if (!pipeline || !descriptorSet || !source || !output || !sampler || !commandList)
        {
            std::cerr << "resource creation failed for " << CaseName << '\n';
            return false;
        }

        commandList->Begin();
        commandList->TextureBarrier(
            source,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess);
        commandList->TextureBarrier(
            output,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess);
        descriptorSet->BindTexture(0u, source);
        descriptorSet->BindSampler(1u, sampler);
        descriptorSet->BindStorageTexture(2u, output);
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        return CompleteDispatch(device, CaseName, commandList);
    }

    bool RunMixedMipCase(RHI::IDevice& device)
    {
        constexpr const char* CaseName = "mixed_mip";
        constexpr const char* ShaderSource = R"glsl(
#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D sourceTexture;
layout(r32f, set = 0, binding = 1) uniform writeonly image2D mipOutput;
void main()
{
    float value = textureLod(sourceTexture, vec2(0.5), 0.0).r;
    imageStore(mipOutput, ivec2(0, 0), vec4(value));
}
)glsl";

        GValidationCaseName.store(CaseName, std::memory_order_relaxed);
        RHI::DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(MakeBinding(0u, RHI::ResourceBindType::CombinedImageSampler));
        setDesc.bindings.push_back(MakeBinding(1u, RHI::ResourceBindType::RWTexture));

        RHI::PipelinePtr pipeline = CreateComputePipeline(device, CaseName, ShaderSource, setDesc);
        RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(setDesc);
        RHI::TexturePtr texture = CreateTexture(
            device,
            RHI::Format::R32_FLOAT,
            RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite,
            2u,
            "ImageLayoutMixedMip");
        RHI::SamplerPtr sampler = CreatePointSampler(device);
        RHI::CommandListPtr commandList = device.CreateCommandList();
        auto vkTexture = Core::Container::DynamicPointerCast<RHI::Vulkan::VulkanTexture>(texture);
        if (!pipeline || !descriptorSet || !texture || !sampler || !commandList || !vkTexture)
        {
            std::cerr << "resource creation failed for " << CaseName << '\n';
            return false;
        }

        commandList->Begin();
        commandList->TextureBarrier(
            texture,
            RHI::ResourceState::Undefined,
            RHI::ResourceState::UnorderedAccess,
            0u,
            0u,
            0u,
            0u);
        const vk::ImageLayout layoutBeforePartialBarrier = vkTexture->GetVkImageLayout();
        commandList->TextureBarrier(
            texture,
            RHI::ResourceState::UnorderedAccess,
            RHI::ResourceState::ShaderResource,
            0u,
            0u,
            1u,
            0u);
        const bool bPartialTrackerPreserved =
            layoutBeforePartialBarrier == vk::ImageLayout::eGeneral &&
            vkTexture->GetVkImageLayout() == layoutBeforePartialBarrier;
        std::cout << "mixed_mip_partial_tracker_preserved="
                  << (bPartialTrackerPreserved ? 1 : 0) << '\n';
        descriptorSet->BindTexture(0u, texture);
        descriptorSet->BindSampler(0u, sampler);
        descriptorSet->BindStorageTexture(1u, texture, 1u);
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        const bool bDispatchCompleted = CompleteDispatch(device, CaseName, commandList);
        return bPartialTrackerPreserved && bDispatchCompleted;
    }

    bool RunImageLayoutDispatchCases(RHI::IDevice& device)
    {
        const bool bSampledImageCompleted = RunSampledImageCase(device);
        const bool bCombinedDepthCompleted = RunCombinedDepthCase(device);
        const bool bStorageImageCompleted = RunStorageImageCase(device);
        const bool bGeneralSampledCompleted = RunGeneralSampledCase(device);
        const bool bMixedMipCompleted = RunMixedMipCase(device);
        return bSampledImageCompleted &&
               bCombinedDepthCompleted &&
               bStorageImageCompleted &&
               bGeneralSampledCompleted &&
               bMixedMipCompleted;
    }

    class ImageLayoutValidationHandler final : public RenderingValidationApplicationHandler
    {
    public:
        bool OnPreInitialize(
            const Core::Container::VariableArray<Core::Container::String>& args) override
        {
            return RenderingValidationApplicationHandler::OnPreInitialize(args) &&
                   m_bMicroCaseSpecified;
        }

        bool OnInitialize() override
        {
            if (!RenderingValidationApplicationHandler::OnInitialize() ||
                Core::Engine::GEngine == nullptr)
            {
                return false;
            }

            Core::Rendering::RenderWorld& renderWorld =
                Core::Engine::GEngine->GetRenderWorld();
            m_Device = Core::Container::DynamicPointerCast<RHI::Vulkan::VulkanDevice>(
                renderWorld.GetRenderingCoordinator().GetDevice());
            if (!m_Device || !CreateValidationMessenger())
            {
                return false;
            }

            m_bDispatchCasesCompleted = RunImageLayoutDispatchCases(*m_Device);
            return true;
        }

        void OnPostInitialize() override
        {
            m_Device->WaitIdle();
            const uint32_t validationErrors =
                GValidationErrorCount.load(std::memory_order_relaxed);
            std::cout << "micro_validation_errors=" << validationErrors << '\n';
            if (Core::Engine::GEngine != nullptr)
            {
                Core::Engine::GEngine->RequestExit(
                    m_bDispatchCasesCompleted && validationErrors == 0u ? 0 : 1);
            }
        }

        void OnPreShutdown() override
        {
            if (m_Device)
            {
                m_Device->WaitIdle();
                if (m_DebugMessenger != VK_NULL_HANDLE && m_DestroyMessenger)
                {
                    m_DestroyMessenger(
                        static_cast<VkInstance>(m_Device->GetVkInstance()),
                        m_DebugMessenger,
                        nullptr);
                    m_DebugMessenger = VK_NULL_HANDLE;
                }
            }

            RenderingValidationApplicationHandler::OnPreShutdown();
            m_Device.reset();
        }

    protected:
        bool ParseAdditionalArgument(
            const Core::Container::String& argument,
            Core::Container::String& reason) override
        {
            if (argument == "--image-layout-case=micro" && !m_bMicroCaseSpecified)
            {
                m_bMicroCaseSpecified = true;
                reason.clear();
                return true;
            }

            reason = "unsupported or duplicate image-layout case";
            return false;
        }

        bool EvaluateCapturedFrame(
            const Core::Rendering::CapturedFrame&,
            Core::Container::String& reason) override
        {
            reason = "frame capture is unreachable in micro mode";
            return false;
        }

    private:
        bool CreateValidationMessenger()
        {
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            createInfo.pfnUserCallback = &CountValidationError;

            const VkInstance instance = static_cast<VkInstance>(m_Device->GetVkInstance());
            m_DestroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            return createMessenger &&
                   m_DestroyMessenger &&
                   createMessenger(instance, &createInfo, nullptr, &m_DebugMessenger) == VK_SUCCESS;
        }

        Core::Container::TSharedPtr<RHI::Vulkan::VulkanDevice> m_Device;
        PFN_vkDestroyDebugUtilsMessengerEXT m_DestroyMessenger = nullptr;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        bool m_bMicroCaseSpecified = false;
        bool m_bDispatchCasesCompleted = false;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<ImageLayoutValidationHandler>();
    }
}

int main(int argc, char** argv)
{
    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip(
            "RHIImageLayoutVulkanValidationTest",
            "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip(
            "RHIImageLayoutVulkanValidationTest",
            "no Vulkan device is available");
    }

    const Core::Container::String runDirectory =
        Core::Container::String(NORVES_BINARY_ROOT) +
        "/RenderingValidation/ImageLayoutRuns";
    std::filesystem::create_directories(runDirectory.c_str());
    Core::Boot::BootConfig config;
    config.WindowTitle = "RHI Image Layout Vulkan Validation";
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = true;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = runDirectory + "/RHIImageLayoutVulkanValidationTest.log";
    config.CreateHandler = &CreateHandler;
    for (int index = 1; index < argc; ++index)
    {
        config.Arguments.push_back(Core::Container::String(argv[index]));
    }
    return Core::Boot::LaunchApplication(config);
}
