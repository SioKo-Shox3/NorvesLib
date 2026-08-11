#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingValidationApplication.h"

#include "Boot/AppLauncher.h"
#include "Boot/BootConfig.h"
#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Rendering/RenderWorld.h"
#include "RHI/Vulkan/VulkanDevice.h"

#include <atomic>
#include <cmath>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    std::atomic<uint32_t> GValidationErrorCount{0u};

    VKAPI_ATTR VkBool32 VKAPI_CALL CountValidationError(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT*,
        void*)
    {
        if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
        {
            GValidationErrorCount.fetch_add(1u, std::memory_order_relaxed);
        }
        return VK_FALSE;
    }

    class TimestampHandler final : public RenderingValidationApplicationHandler
    {
    public:
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
            if (!m_Device)
            {
                return false;
            }

            const auto queueFamilies = m_Device->GetVkPhysicalDevice().getQueueFamilyProperties();
            const uint32_t queueFamilyIndex = m_Device->GetGraphicsQueueFamilyIndex();
            const auto properties = m_Device->GetVkPhysicalDevice().getProperties();
            const uint32_t validBits = queueFamilyIndex < queueFamilies.size()
                                           ? queueFamilies[queueFamilyIndex].timestampValidBits
                                           : 0u;
            if (!renderWorld.SupportsGPUTimings())
            {
                std::cout << "RHIGPUTimestampVulkanTest skipped: device="
                          << m_Device->GetCapabilities().DeviceName
                          << " timestampValidBits=" << validBits
                          << " timestampPeriod=" << properties.limits.timestampPeriod << '\n';
                m_bUnsupported = true;
                Core::Engine::GEngine->RequestExit(GpuTestSkipReturnCode);
                return true;
            }

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
            if (!createMessenger || !m_DestroyMessenger ||
                createMessenger(instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
            {
                return false;
            }
            return true;
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

            const uint32_t validationErrors =
                GValidationErrorCount.load(std::memory_order_relaxed);
            if (!m_bUnsupported && validationErrors != 0u &&
                Core::Engine::GEngine != nullptr)
            {
                Core::Engine::GEngine->RequestExit(1);
            }
            RenderingValidationApplicationHandler::OnPreShutdown();
            m_Device.reset();
        }

    protected:
        bool EvaluateCapturedFrame(const Core::Rendering::CapturedFrame&,
                                   Core::Container::String& reason) override
        {
            if (Core::Engine::GEngine == nullptr)
            {
                reason = "engine is unavailable";
                return false;
            }

            Core::Container::VariableArray<Core::Rendering::RenderPassGPUTiming> timings;
            uint64_t droppedFrameCount = 0u;
            Core::Rendering::RenderWorld& renderWorld =
                Core::Engine::GEngine->GetRenderWorld();
            if (!renderWorld.TryConsumeCompletedGPUTimings(timings, droppedFrameCount) ||
                droppedFrameCount != 0u)
            {
                reason = "completed GPU timing batch is missing or dropped";
                return false;
            }

            for (const Core::Rendering::RenderPassGPUTiming& frameTiming : timings)
            {
                float frameDuration = 0.0f;
                float gbufferDuration = 0.0f;
                float lightingDuration = 0.0f;
                for (const Core::Rendering::RenderPassGPUTiming& timing : timings)
                {
                    if (timing.FrameNumber != frameTiming.FrameNumber ||
                        !timing.bValid ||
                        !std::isfinite(timing.DurationMs) ||
                        timing.DurationMs <= 0.0f)
                    {
                        continue;
                    }
                    if (timing.PassName == "FrameGPU")
                    {
                        frameDuration = timing.DurationMs;
                    }
                    else if (timing.PassName == "GBufferPass")
                    {
                        gbufferDuration = timing.DurationMs;
                    }
                    else if (timing.PassName == "LightingPass")
                    {
                        lightingDuration = timing.DurationMs;
                    }
                }

                if (frameDuration > 0.0f && gbufferDuration > 0.0f &&
                    lightingDuration > 0.0f)
                {
                    m_Device->WaitIdle();
                    const uint32_t validationErrors =
                        GValidationErrorCount.load(std::memory_order_relaxed);
                    std::cout << "validation_errors=" << validationErrors
                              << " frame=" << frameTiming.FrameNumber
                              << " FrameGPU=" << frameDuration
                              << " GBufferPass=" << gbufferDuration
                              << " LightingPass=" << lightingDuration << '\n';
                    if (validationErrors == 0u)
                    {
                        return true;
                    }
                    reason = "Vulkan validation reported an error";
                    return false;
                }
            }

            reason = "same-frame FrameGPU/GBufferPass/LightingPass timings were not found";
            return false;
        }

    private:
        Core::Container::TSharedPtr<RHI::Vulkan::VulkanDevice> m_Device;
        PFN_vkDestroyDebugUtilsMessengerEXT m_DestroyMessenger = nullptr;
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
        bool m_bUnsupported = false;
    };

    Core::Container::TSharedPtr<Core::Application::IApplicationHandler> CreateHandler()
    {
        return Core::Container::MakeShared<TimestampHandler>();
    }
}

int main()
{
    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("RHIGPUTimestampVulkanTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("RHIGPUTimestampVulkanTest", "no Vulkan device is available");
    }

    const Core::Container::String runDirectory =
        Core::Container::String(NORVES_BINARY_ROOT) +
        "/RenderingValidation/TimestampRuns";
    Core::Boot::BootConfig config;
    config.WindowTitle = "RHI GPU Timestamp Validation";
    config.WindowWidth = ValidationWidth;
    config.WindowHeight = ValidationHeight;
    config.bResizable = false;
    config.bVSync = true;
    config.bEnableMultiThreadedRendering = false;
    config.bEnableRHIValidation = true;
    config.Api = RHI::GraphicsAPI::Vulkan;
    config.LogFileName = runDirectory + "/RHIGPUTimestampVulkanTest.log";
    config.CreateHandler = &CreateHandler;
    config.Arguments.push_back("--scene=indoor");
    config.Arguments.push_back("--trace");
    config.Arguments.push_back(
        Core::Container::String("--trace-file=") +
        runDirectory +
        "/RHIGPUTimestampVulkanTest.trace.csv");
    return Core::Boot::LaunchApplication(config);
}
