#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cstdlib>
#include <iostream>

namespace NorvesLib::Test::RenderingValidation
{
    bool IsForcedGpuTestSkipRequested()
    {
        char* value = nullptr;
        size_t valueLength = 0;
        const errno_t result = _dupenv_s(&value, &valueLength, "NORVESLIB_FORCE_GPU_TEST_SKIP");
        const bool bRequested = result == 0 && value != nullptr && valueLength == 2 && value[0] == '1';
        free(value);
        return bRequested;
    }

    bool CanCreateVulkanDeviceForGpuTest(Core::Container::String& outReason)
    {
        RHI::RHIDeviceDesc desc;
        desc.Api = RHI::GraphicsAPI::Vulkan;
        desc.bEnableValidation = false;
        RHI::DevicePtr probe = RHI::CreateRHIDevice(desc);
        if (!probe)
        {
            outReason = TEXT("no Vulkan device is available");
            return false;
        }
        probe.reset();
        outReason.clear();
        return true;
    }

    int ReportGpuTestSkip(const char* testName, const char* reason)
    {
        std::cout << testName << " skipped: " << reason << '\n';
        return GpuTestSkipReturnCode;
    }
}
