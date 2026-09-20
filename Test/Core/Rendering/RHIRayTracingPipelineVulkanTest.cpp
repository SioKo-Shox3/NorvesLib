// Vulkan RT pipelineの作成とShader Binding Tableのgroup配置を確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanDescriptorSet.h"
#include "RHI/Vulkan/VulkanRayTracingPipeline.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::RHI::Vulkan;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RHIRayTracingPipelineVulkanTest";

    ShaderPtr CompileRayTracingShader(
        IDevice& device,
        const ShaderCompilerPtr& compiler,
        const String& fixturePath,
        ShaderStage stage)
    {
        const ShaderCompileResult result = compiler->CompileFromFile(fixturePath, stage);
        if (!result.bSuccess || result.ByteCode.empty())
        {
            std::cerr << "RT shaderのコンパイルに失敗しました: " << result.ErrorMessage.c_str() << '\n';
            return {};
        }

        ShaderDesc desc;
        desc.stage = stage;
        desc.entryPoint = "main";
        desc.byteCode = result.ByteCode;
        return device.CreateShader(desc);
    }

    bool IsValidRegion(
        const vk::StridedDeviceAddressRegionKHR& region,
        uint32_t recordCount,
        uint64_t handleAlignment,
        uint64_t baseAlignment,
        uint64_t bufferAddress,
        uint64_t bufferSize)
    {
        if (recordCount == 0)
        {
            return region.deviceAddress == 0 && region.stride == 0 && region.size == 0;
        }

        return region.deviceAddress != 0 && region.stride != 0 && region.size == region.stride * recordCount &&
            region.stride % handleAlignment == 0 && region.deviceAddress % baseAlignment == 0 &&
            region.deviceAddress >= bufferAddress &&
            region.deviceAddress - bufferAddress <= bufferSize &&
            region.size <= bufferSize - (region.deviceAddress - bufferAddress);
    }

    bool VerifyInvalidGroups(IDevice& device, const RayTracingPipelineDesc& validDesc)
    {
        RayTracingPipelineDesc missingRayGeneration;
        missingRayGeneration.shaderGroups.push_back(validDesc.shaderGroups[1]);
        missingRayGeneration.shaderGroups.push_back(validDesc.shaderGroups[2]);
        if (IsValidRayTracingPipelineDesc(missingRayGeneration) ||
            device.CreateRayTracingPipeline(missingRayGeneration))
        {
            std::cerr << "raygen groupがないpipelineを拒否できませんでした\n";
            return false;
        }

        RayTracingPipelineDesc wrongGeneralStage = validDesc;
        wrongGeneralStage.shaderGroups[1].generalShader = validDesc.shaderGroups[2].closestHitShader;
        RayTracingShaderGroupDesc additionalMissGroup;
        additionalMissGroup.type = RayTracingShaderGroupType::General;
        additionalMissGroup.generalShader = validDesc.shaderGroups[1].generalShader;
        wrongGeneralStage.shaderGroups.push_back(additionalMissGroup);
        if (IsValidRayTracingPipelineDesc(wrongGeneralStage) || device.CreateRayTracingPipeline(wrongGeneralStage))
        {
            std::cerr << "stageが一致しないgeneral groupを拒否できませんでした\n";
            return false;
        }

        RayTracingPipelineDesc emptyHitGroup = validDesc;
        emptyHitGroup.shaderGroups[2].closestHitShader.reset();
        if (IsValidRayTracingPipelineDesc(emptyHitGroup) || device.CreateRayTracingPipeline(emptyHitGroup))
        {
            std::cerr << "shaderがないhit groupを拒否できませんでした\n";
            return false;
        }

        RayTracingPipelineDesc duplicateRayGeneration = validDesc;
        duplicateRayGeneration.shaderGroups.push_back(validDesc.shaderGroups[0]);
        if (IsValidRayTracingPipelineDesc(duplicateRayGeneration) ||
            device.CreateRayTracingPipeline(duplicateRayGeneration))
        {
            std::cerr << "複数raygen groupを拒否できませんでした\n";
            return false;
        }

        return true;
    }

    bool VerifyRayTracingDescriptorVisibility(const TSharedPtr<VulkanDevice>& device)
    {
        struct StageCase
        {
            ShaderStage stage;
            vk::ShaderStageFlagBits vkStage;
        };

        const StageCase stageCases[] = {
            {ShaderStage::RayGen, vk::ShaderStageFlagBits::eRaygenKHR},
            {ShaderStage::Miss, vk::ShaderStageFlagBits::eMissKHR},
            {ShaderStage::ClosestHit, vk::ShaderStageFlagBits::eClosestHitKHR},
            {ShaderStage::AnyHit, vk::ShaderStageFlagBits::eAnyHitKHR},
            {ShaderStage::Intersection, vk::ShaderStageFlagBits::eIntersectionKHR},
            {ShaderStage::Callable, vk::ShaderStageFlagBits::eCallableKHR}};

        vk::ShaderStageFlags expectedAllRayTracingFlags{};
        for (const StageCase& stageCase : stageCases)
        {
            DescriptorBindingDesc binding;
            binding.binding = 0;
            binding.type = DescriptorType::UniformBuffer;
            binding.stages = stageCase.stage;
            VariableArray<DescriptorBindingDesc> bindings;
            bindings.push_back(binding);

            TSharedPtr<VulkanDescriptorSetLayout> layout = MakeShared<VulkanDescriptorSetLayout>(device, bindings);
            const VariableArray<vk::DescriptorSetLayoutBinding>& vkBindings = layout->GetVkBindings();
            if (vkBindings.size() != 1 || vkBindings[0].stageFlags != stageCase.vkStage)
            {
                std::cerr << "RT descriptor stageのVulkan flags変換が不正です\n";
                return false;
            }

            expectedAllRayTracingFlags |= stageCase.vkStage;
        }

        DescriptorBindingDesc allRayTracingBinding;
        allRayTracingBinding.binding = 0;
        allRayTracingBinding.type = DescriptorType::UniformBuffer;
        allRayTracingBinding.stages = ShaderStage::AllRayTracing;
        VariableArray<DescriptorBindingDesc> allRayTracingBindings;
        allRayTracingBindings.push_back(allRayTracingBinding);

        TSharedPtr<VulkanDescriptorSetLayout> allRayTracingLayout =
            MakeShared<VulkanDescriptorSetLayout>(device, allRayTracingBindings);
        const VariableArray<vk::DescriptorSetLayoutBinding>& vkAllRayTracingBindings =
            allRayTracingLayout->GetVkBindings();
        if (vkAllRayTracingBindings.size() != 1 ||
            vkAllRayTracingBindings[0].stageFlags != expectedAllRayTracingFlags)
        {
            std::cerr << "AllRayTracing descriptor stageのVulkan flags変換が不正です\n";
            return false;
        }

        return true;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }

        String unavailableReason;
        if (!CanCreateVulkanDeviceForGpuTest(unavailableReason))
        {
            return ReportGpuTestSkip(TestName, unavailableReason.c_str());
        }

        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkan deviceを利用できません");
        }
        if (!device->GetCapabilities().RayTracing.bRayTracingPipeline ||
            !device->GetCapabilities().bBufferDeviceAddress)
        {
            return ReportGpuTestSkip(TestName, "Vulkan RT pipelineまたはBDA機能を利用できません");
        }

        TSharedPtr<VulkanDevice> vulkanDevice = DynamicPointerCast<VulkanDevice>(device);
        ShaderCompilerPtr compiler = device->CreateShaderCompiler();
        if (!vulkanDevice || !compiler)
        {
            std::cerr << "Vulkan deviceまたはshader compilerを取得できませんでした\n";
            return 1;
        }

        const String fixturePath = NORVES_SOURCE_ROOT "/Assets/Shaders/RayTracing/RayTracingStageFixture.glsl";
        ShaderPtr rayGenerationShader = CompileRayTracingShader(
            *device,
            compiler,
            fixturePath,
            ShaderStage::RayGen);
        ShaderPtr missShader = CompileRayTracingShader(*device, compiler, fixturePath, ShaderStage::Miss);
        ShaderPtr closestHitShader = CompileRayTracingShader(*device, compiler, fixturePath, ShaderStage::ClosestHit);
        if (!rayGenerationShader || !missShader || !closestHitShader)
        {
            return 1;
        }

        if (!VerifyRayTracingDescriptorVisibility(vulkanDevice))
        {
            return 1;
        }

        RayTracingPipelineDesc desc;
        RayTracingShaderGroupDesc rayGenerationGroup;
        rayGenerationGroup.type = RayTracingShaderGroupType::General;
        rayGenerationGroup.generalShader = rayGenerationShader;
        desc.shaderGroups.push_back(rayGenerationGroup);

        RayTracingShaderGroupDesc missGroup;
        missGroup.type = RayTracingShaderGroupType::General;
        missGroup.generalShader = missShader;
        desc.shaderGroups.push_back(missGroup);

        RayTracingShaderGroupDesc hitGroup;
        hitGroup.type = RayTracingShaderGroupType::TrianglesHit;
        hitGroup.closestHitShader = closestHitShader;
        desc.shaderGroups.push_back(hitGroup);

        DescriptorBinding rayTracingBinding;
        rayTracingBinding.binding = 0;
        rayTracingBinding.type = ResourceBindType::ConstantBuffer;
        rayTracingBinding.stages = ShaderStage::AllRayTracing;
        DescriptorSetDesc rayTracingSet;
        rayTracingSet.bindings.push_back(rayTracingBinding);
        desc.descriptorSetLayouts.push_back(rayTracingSet);

        if (!IsValidRayTracingPipelineDesc(desc) || !VerifyInvalidGroups(*device, desc))
        {
            return 1;
        }

        PipelinePtr createdPipeline = device->CreateRayTracingPipeline(desc);
        TSharedPtr<VulkanRayTracingPipeline> pipeline = DynamicPointerCast<VulkanRayTracingPipeline>(createdPipeline);
        if (!pipeline || pipeline->GetPipelineType() != PipelineType::RayTracing ||
            !pipeline->IsRayTracingPipeline() || pipeline->GetShaderGroupCount() != desc.shaderGroups.size() ||
            pipeline->GetBindPointCount() != 1)
        {
            std::cerr << "最小RT pipelineを作成できませんでした\n";
            return 1;
        }

        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR properties{};
        vk::PhysicalDeviceProperties2 properties2{};
        properties2.pNext = &properties;
        vulkanDevice->GetVkPhysicalDevice().getProperties2(&properties2);

        TSharedPtr<VulkanBuffer> shaderBindingTable = DynamicPointerCast<VulkanBuffer>(pipeline->GetShaderBindingTable());
        if (!shaderBindingTable || shaderBindingTable->GetSize() == 0 || shaderBindingTable->GetDeviceAddress() == 0 ||
            !IsValidRegion(pipeline->GetRayGenerationRegion(), 1, properties.shaderGroupHandleAlignment,
                           properties.shaderGroupBaseAlignment, shaderBindingTable->GetDeviceAddress(),
                           shaderBindingTable->GetSize()) ||
            !IsValidRegion(pipeline->GetMissRegion(), 1, properties.shaderGroupHandleAlignment,
                           properties.shaderGroupBaseAlignment, shaderBindingTable->GetDeviceAddress(),
                           shaderBindingTable->GetSize()) ||
            !IsValidRegion(pipeline->GetHitRegion(), 1, properties.shaderGroupHandleAlignment,
                           properties.shaderGroupBaseAlignment, shaderBindingTable->GetDeviceAddress(),
                           shaderBindingTable->GetSize()) ||
            !IsValidRegion(pipeline->GetCallableRegion(), 0, properties.shaderGroupHandleAlignment,
                           properties.shaderGroupBaseAlignment, shaderBindingTable->GetDeviceAddress(),
                           shaderBindingTable->GetSize()))
        {
            std::cerr << "Shader Binding Tableのregionまたはalignmentが不正です\n";
            return 1;
        }

        VariableArray<uint8_t> expectedHandles;
        const uint64_t expectedSize = static_cast<uint64_t>(properties.shaderGroupHandleSize) * 3;
        expectedHandles.resize(static_cast<size_t>(expectedSize));
        const vk::Result handlesResult = vulkanDevice->GetVkDevice().getRayTracingShaderGroupHandlesKHR(
            pipeline->GetVkPipeline(),
            0,
            pipeline->GetShaderGroupCount(),
            static_cast<size_t>(expectedSize),
            expectedHandles.data());
        if (handlesResult != vk::Result::eSuccess)
        {
            std::cerr << "pipeline shader group handleの再取得に失敗しました\n";
            return 1;
        }

        const uint64_t sbtAddress = shaderBindingTable->GetDeviceAddress();
        auto* sbtData = static_cast<const uint8_t*>(shaderBindingTable->Map(0, shaderBindingTable->GetSize()));
        const bool groupHandlesMatch =
            std::memcmp(sbtData + (pipeline->GetRayGenerationRegion().deviceAddress - sbtAddress),
                        expectedHandles.data(), properties.shaderGroupHandleSize) == 0 &&
            std::memcmp(sbtData + (pipeline->GetMissRegion().deviceAddress - sbtAddress),
                        expectedHandles.data() + properties.shaderGroupHandleSize,
                        properties.shaderGroupHandleSize) == 0 &&
            std::memcmp(sbtData + (pipeline->GetHitRegion().deviceAddress - sbtAddress),
                        expectedHandles.data() + properties.shaderGroupHandleSize * 2,
                        properties.shaderGroupHandleSize) == 0;
        shaderBindingTable->Unmap();
        if (!groupHandlesMatch)
        {
            std::cerr << "SBT recordに対応するshader group handleが格納されていません\n";
            return 1;
        }

        std::cout << "raygen_miss_closest_hit_pipeline=true\n";
        std::cout << "sbt_regions_aligned=true\n";
        std::cout << "sbt_group_handles_match=true\n";
        std::cout << "invalid_group_configurations_rejected=true\n";
        return 0;
    }
}

int main()
{
    return RunTest();
}
