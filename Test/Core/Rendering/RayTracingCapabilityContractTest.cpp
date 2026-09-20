// RT機能の依存組み合わせと実デバイス初期化時の能力公開を検証します。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RayTracingCapabilityContractTest";

    bool CheckCase(
        const char* name,
        const RHI::RayTracingFeatureAvailability& availability,
        bool bExpectedAccelerationStructure,
        bool bExpectedRayQuery,
        bool bExpectedRayTracingPipeline)
    {
        const RHI::RayTracingCapabilities actual = RHI::ResolveRayTracingCapabilities(availability);
        if (actual.bAccelerationStructure != bExpectedAccelerationStructure ||
            actual.bRayQuery != bExpectedRayQuery ||
            actual.bRayTracingPipeline != bExpectedRayTracingPipeline)
        {
            std::cerr << "case=" << name
                      << " acceleration_structure=" << actual.bAccelerationStructure
                      << " ray_query=" << actual.bRayQuery
                      << " ray_tracing_pipeline=" << actual.bRayTracingPipeline
                      << '\n';
            return false;
        }
        return true;
    }

    RHI::RayTracingFeatureAvailability MakeCompleteAvailability()
    {
        RHI::RayTracingFeatureAvailability availability;
        availability.bAccelerationStructureExtension = true;
        availability.bDeferredHostOperationsExtension = true;
        availability.bBufferDeviceAddress = true;
        availability.bAccelerationStructureFeature = true;
        availability.bRayQueryExtension = true;
        availability.bRayQueryFeature = true;
        availability.bRayTracingPipelineExtension = true;
        availability.bRayTracingPipelineFeature = true;
        return availability;
    }

    bool TestFeatureCombinations()
    {
        bool bPassed = CheckCase("unsupported", {}, false, false, false);

        RHI::RayTracingFeatureAvailability availability = MakeCompleteAvailability();
        bPassed = CheckCase("all_features", availability, true, true, true) && bPassed;

        availability.bRayQueryExtension = false;
        availability.bRayQueryFeature = false;
        bPassed = CheckCase("pipeline_without_ray_query", availability, true, false, true) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bRayTracingPipelineExtension = false;
        availability.bRayTracingPipelineFeature = false;
        bPassed = CheckCase("ray_query_without_pipeline", availability, true, true, false) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bDeferredHostOperationsExtension = false;
        bPassed = CheckCase("missing_deferred_host_operations", availability, false, false, false) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bBufferDeviceAddress = false;
        bPassed = CheckCase("missing_buffer_device_address", availability, false, false, false) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bAccelerationStructureFeature = false;
        bPassed = CheckCase("missing_acceleration_structure_feature", availability, false, false, false) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bRayQueryFeature = false;
        bPassed = CheckCase("missing_ray_query_feature", availability, true, false, true) && bPassed;

        availability = MakeCompleteAvailability();
        availability.bRayTracingPipelineFeature = false;
        bPassed = CheckCase("missing_pipeline_feature", availability, true, true, false) && bPassed;
        return bPassed;
    }

    int TestVulkanDeviceCapabilities()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }

        RHI::RHIDeviceDesc deviceDesc;
        deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }
        if (device->GetAPI() != RHI::API::Vulkan)
        {
            std::cerr << "Vulkanデバイスを作成できませんでした\n";
            return 1;
        }

        const RHI::DeviceCapabilities& capabilities = device->GetCapabilities();
        if ((capabilities.RayTracing.bAccelerationStructure && !capabilities.bBufferDeviceAddress) ||
            (capabilities.RayTracing.bRayQuery && !capabilities.RayTracing.bAccelerationStructure) ||
            (capabilities.RayTracing.bRayTracingPipeline && !capabilities.RayTracing.bAccelerationStructure))
        {
            std::cerr << "RT機能の依存関係が論理デバイスの能力に反映されていません\n";
            return 1;
        }

        std::cout << "acceleration_structure_enabled="
                  << (capabilities.RayTracing.bAccelerationStructure ? "true" : "false") << '\n';
        std::cout << "ray_query_enabled="
                  << (capabilities.RayTracing.bRayQuery ? "true" : "false") << '\n';
        std::cout << "ray_tracing_pipeline_enabled="
                  << (capabilities.RayTracing.bRayTracingPipeline ? "true" : "false") << '\n';
        return 0;
    }
}

int main()
{
    if (!TestFeatureCombinations())
    {
        return 1;
    }

    const int deviceStatus = TestVulkanDeviceCapabilities();
    if (deviceStatus != 0)
    {
        return deviceStatus;
    }

    std::cout << "RayTracingCapabilityContractTest passed: optional feature combinations and Vulkan device capabilities are consistent\n";
    return 0;
}