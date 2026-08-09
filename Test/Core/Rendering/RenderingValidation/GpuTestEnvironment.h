#pragma once

#include "Container/String.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    inline constexpr int GpuTestSkipReturnCode = 125;
    inline constexpr uint32_t ValidationWidth = 256;
    inline constexpr uint32_t ValidationHeight = 256;
    inline constexpr uint32_t ValidationSeed = 0x4E525630u;
    inline constexpr uint64_t ValidationWarmupFixedSteps = 60;

    bool IsForcedGpuTestSkipRequested();
    bool CanCreateVulkanDeviceForGpuTest(Core::Container::String& outReason);
    int ReportGpuTestSkip(const char* testName, const char* reason);
}
