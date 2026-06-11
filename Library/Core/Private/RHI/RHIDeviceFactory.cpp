#include "RHI/RHIDeviceFactory.h"
#include "Platform/PlatformGraphics.h"
#include "RHI/Vulkan/VulkanDevice.h"
#include "Logging/LogMacros.h"
#include <exception>

namespace NorvesLib::RHI
{

    DevicePtr CreateRHIDevice(const RHIDeviceDesc& desc)
    {
        // Default の場合はプラットフォームの既定APIへ解決する
        GraphicsAPI resolvedApi = desc.Api;
        if (resolvedApi == GraphicsAPI::Default)
        {
            resolvedApi = Core::Platform::GetDefaultGraphicsAPI();
        }

        switch (resolvedApi)
        {
        case GraphicsAPI::Vulkan:
        {
            LOG_INFO("CreateRHIDevice: Initializing Vulkan backend");
            try
            {
                Vulkan::VulkanInitParams params;
                params.bEnableValidation = desc.bEnableValidation;
                return Vulkan::VulkanDevice::Create(params);
            }
            catch (const std::exception& e)
            {
                NORVES_LOG_ERROR("RHI", "CreateRHIDevice: Vulkan backend initialization failed: %s", e.what());
                return nullptr;
            }
        }

        case GraphicsAPI::D3D12:
        {
            LOG_ERROR("CreateRHIDevice: D3D12 backend is not implemented");
            return nullptr;
        }

        case GraphicsAPI::Null:
        {
            LOG_ERROR("CreateRHIDevice: Null backend is not implemented");
            return nullptr;
        }

        default:
        {
            LOG_ERROR("CreateRHIDevice: Unknown graphics API");
            return nullptr;
        }
        }
    }

} // namespace NorvesLib::RHI
