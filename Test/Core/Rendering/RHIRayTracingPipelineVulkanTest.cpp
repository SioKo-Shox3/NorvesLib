// Vulkan RT pipelineの作成とShader Binding Tableのgroup配置を確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanAccelerationStructure.h"
#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"
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

    bool VerifyTriangleVisibility(
        IDevice& device,
        const PipelinePtr& pipeline,
        const DescriptorSetDesc& descriptorSetDesc)
    {
        struct Vertex
        {
            float Position[3];
        };

        const Vertex vertices[3] = {
            {{-1.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}},
        };
        const uint32_t indices[3] = {0, 1, 2};

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.Size = sizeof(vertices);
        vertexBufferDesc.Usage = ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress;
        vertexBufferDesc.CPUAccessible = true;
        vertexBufferDesc.DebugName = "RHIRayTracingPipeline.VisibilityVertices";
        BufferPtr vertexBuffer = device.CreateBuffer(vertexBufferDesc);

        BufferDesc indexBufferDesc;
        indexBufferDesc.Size = sizeof(indices);
        indexBufferDesc.Usage = ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress;
        indexBufferDesc.CPUAccessible = true;
        indexBufferDesc.DebugName = "RHIRayTracingPipeline.VisibilityIndices";
        BufferPtr indexBuffer = device.CreateBuffer(indexBufferDesc);
        if (!vertexBuffer || !indexBuffer || vertexBuffer->GetDeviceAddress() == 0 ||
            indexBuffer->GetDeviceAddress() == 0)
        {
            std::cerr << "visibility用のBDA vertex/index bufferを作成できませんでした\n";
            return false;
        }
        vertexBuffer->Update(vertices, sizeof(vertices));
        indexBuffer->Update(indices, sizeof(indices));

        AccelerationStructureDesc bottomLevelDesc;
        bottomLevelDesc.type = AccelerationStructureType::BottomLevel;
        AccelerationStructureGeometryCapacityDesc geometryCapacity;
        geometryCapacity.type = AccelerationStructureGeometryType::Triangles;
        geometryCapacity.maxPrimitiveCount = 1;
        geometryCapacity.opaque = true;
        bottomLevelDesc.geometryCapacities.push_back(geometryCapacity);
        AccelerationStructurePtr bottomLevel = device.CreateAccelerationStructure(bottomLevelDesc);
        AccelerationStructureGeometryDesc triangleGeometry;
        triangleGeometry.type = AccelerationStructureGeometryType::Triangles;
        triangleGeometry.opaque = true;
        triangleGeometry.triangles.vertexBuffer = vertexBuffer;
        triangleGeometry.triangles.vertexCount = 3;
        triangleGeometry.triangles.vertexStride = sizeof(Vertex);
        triangleGeometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        triangleGeometry.triangles.indexBuffer = indexBuffer;
        triangleGeometry.triangles.indexCount = 3;
        triangleGeometry.triangles.indexFormat = IndexType::Uint32;
        AccelerationStructureBuildDesc bottomLevelBuildDesc;
        bottomLevelBuildDesc.type = AccelerationStructureType::BottomLevel;
        bottomLevelBuildDesc.destination = bottomLevel;
        bottomLevelBuildDesc.geometries.push_back(triangleGeometry);
        if (!bottomLevel || !bottomLevel->Build(bottomLevelBuildDesc))
        {
            std::cerr << "visibility用BLASを構築できませんでした\n";
            return false;
        }

        AccelerationStructureDesc topLevelDesc;
        topLevelDesc.type = AccelerationStructureType::TopLevel;
        topLevelDesc.maxInstanceCount = 1;
        AccelerationStructurePtr topLevel = device.CreateAccelerationStructure(topLevelDesc);
        AccelerationStructureInstanceDesc instance;
        instance.bottomLevel = bottomLevel;
        AccelerationStructureBuildDesc topLevelBuildDesc;
        topLevelBuildDesc.type = AccelerationStructureType::TopLevel;
        topLevelBuildDesc.destination = topLevel;
        topLevelBuildDesc.instances.push_back(instance);
        if (!topLevel || !topLevel->Build(topLevelBuildDesc))
        {
            std::cerr << "visibility用TLASを構築できませんでした\n";
            return false;
        }
        topLevelBuildDesc.destination.reset();
        topLevelBuildDesc.instances.clear();
        instance.bottomLevel.reset();
        TWeakPtr<IAccelerationStructure> topLevelLifetime = topLevel;

        BufferDesc resultBufferDesc;
        resultBufferDesc.Size = sizeof(uint32_t) * 2u;
        resultBufferDesc.Usage = ResourceUsage::StorageBuffer;
        resultBufferDesc.CPUAccessible = true;
        resultBufferDesc.DebugName = "RHIRayTracingPipeline.VisibilityResults";
        BufferPtr resultBuffer = device.CreateBuffer(resultBufferDesc);
        DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(descriptorSetDesc);
        if (!resultBuffer || !descriptorSet)
        {
            std::cerr << "visibility用の結果bufferまたはdescriptor setを作成できませんでした\n";
            return false;
        }
        const uint32_t initialResults[2] = {0xffffffffu, 0xffffffffu};
        resultBuffer->Update(initialResults, sizeof(initialResults));
        if (descriptorSet->BindAccelerationStructure(0, AccelerationStructurePtr{}) ||
            descriptorSet->BindAccelerationStructure(1, topLevel))
        {
            std::cerr << "nullまたは別種のdescriptor bindingを加速構造として受理しました\n";
            return false;
        }
        if (!descriptorSet->BindAccelerationStructure(0, topLevel))
        {
            std::cerr << "有効なTLASを加速構造descriptorへ設定できませんでした\n";
            return false;
        }
        if (descriptorSet->BindAccelerationStructure(0, bottomLevel))
        {
            std::cerr << "BLASをTLAS専用descriptorへ誤bindingできてしまいました\n";
            return false;
        }
        RHIDeviceDesc foreignDeviceDesc;
        foreignDeviceDesc.Api = GraphicsAPI::Vulkan;
        foreignDeviceDesc.bEnableValidation = false;
        DevicePtr foreignDevice = CreateRHIDevice(foreignDeviceDesc);
        if (!foreignDevice || !foreignDevice->GetCapabilities().RayTracing.bAccelerationStructure)
        {
            std::cerr << "異なるdevice所有の加速構造bindingを検証できませんでした\n";
            return false;
        }
        AccelerationStructurePtr foreignTopLevel = foreignDevice->CreateAccelerationStructure(topLevelDesc);
        if (!foreignTopLevel || descriptorSet->BindAccelerationStructure(0, foreignTopLevel))
        {
            std::cerr << "異なるVulkan device所有のTLASを拒否できませんでした\n";
            return false;
        }
        foreignDevice->WaitIdle();

        descriptorSet->BindStorageBuffer(1, resultBuffer, 0, sizeof(uint32_t) * 2u);
        descriptorSet->Update();
        topLevel.reset();
        if (topLevelLifetime.expired())
        {
            std::cerr << "descriptor setがTLASの寿命を保持しませんでした\n";
            return false;
        }

        CommandListPtr commandList = device.CreateCommandList();
        TSharedPtr<VulkanCommandList> vulkanCommandList = DynamicPointerCast<VulkanCommandList>(commandList);
        TSharedPtr<VulkanBuffer> vulkanResultBuffer = DynamicPointerCast<VulkanBuffer>(resultBuffer);
        TSharedPtr<VulkanRayTracingPipeline> rayTracingPipeline = DynamicPointerCast<VulkanRayTracingPipeline>(pipeline);
        if (!commandList || !vulkanCommandList || !vulkanResultBuffer || !rayTracingPipeline)
        {
            std::cerr << "visibility用のVulkan command listまたはbufferを取得できませんでした\n";
            return false;
        }

        commandList->Begin();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0);
        const uint64_t maxUint32 = static_cast<uint64_t>(~uint32_t{0});
        const uint64_t maxInvocationCount = rayTracingPipeline->GetMaxRayDispatchInvocationCount();
        uint32_t axisDimensions[3] = {1, 1, 1};
        uint32_t testedAxisCount = 0;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            const uint64_t axisLimit = rayTracingPipeline->GetMaxRayDispatchDimension(axis);
            if (axisLimit >= maxUint32 || axisLimit >= maxInvocationCount)
            {
                continue;
            }

            axisDimensions[axis] = static_cast<uint32_t>(axisLimit + 1);
            if (commandList->TraceRays(axisDimensions[0], axisDimensions[1], axisDimensions[2]))
            {
                commandList->End();
                std::cerr << "Vulkanのdispatch軸上限を超える寸法を拒否できませんでした: " << axis << '\n';
                return false;
            }
            axisDimensions[axis] = 1;
            ++testedAxisCount;
        }
        if (commandList->TraceRays(0, 1, 1) || commandList->TraceRays(65536, 65536, 1) ||
            !commandList->TraceRays(2, 1, 1))
        {
            commandList->End();
            std::cerr << "TraceRaysが無効/上限超過の寸法を受理するか、有効なtraceを記録できませんでした\n";
            return false;
        }

        vk::BufferMemoryBarrier resultBarrier{};
        resultBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        resultBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        resultBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultBarrier.buffer = vulkanResultBuffer->GetVkBuffer();
        resultBarrier.offset = 0;
        resultBarrier.size = sizeof(uint32_t) * 2u;
        vulkanCommandList->GetVkCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
            vk::PipelineStageFlagBits::eHost,
            {},
            0,
            nullptr,
            1,
            &resultBarrier,
            0,
            nullptr);
        commandList->End();
        commandList->Submit(true);

        const auto* results = static_cast<const uint32_t*>(resultBuffer->Map(0, sizeof(uint32_t) * 2u));
        if (!results)
        {
            std::cerr << "visibility結果bufferをmapできませんでした\n";
            return false;
        }
        const uint32_t hit = results[0];
        const uint32_t miss = results[1];
        resultBuffer->Unmap();
        std::cout << "rt_trace_hit=" << hit << '\n';
        std::cout << "rt_trace_miss=" << miss << '\n';
        if (hit != 1u || miss != 0u)
        {
            std::cerr << "TraceRaysのtriangle hit/missが解析結果と一致しません\n";
            return false;
        }

        descriptorSet.reset();
        if (!topLevelLifetime.expired())
        {
            std::cerr << "descriptor set破棄後もTLAS resourceが解放されませんでした\n";
            return false;
        }

        std::cout << "tlas_descriptor_binding_and_lifetime=true\n";
        std::cout << "vulkan_trace_rays_readback=true\n";
        std::cout << "ray_dispatch_axis_limit_rejections=" << testedAxisCount << '\n';
        return true;
    }

    bool VerifyRayTracingDescriptorVisibility(const TSharedPtr<VulkanDevice>& device)
    {
        if (VulkanDevice::ConvertResourceBindType(ResourceBindType::AccelerationStructure) !=
            DescriptorType::AccelerationStructure)
        {
            std::cerr << "加速構造のResourceBindType変換が不正です\n";
            return false;
        }

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

        DescriptorBindingDesc accelerationStructureBinding;
        accelerationStructureBinding.binding = 0;
        accelerationStructureBinding.type = DescriptorType::AccelerationStructure;
        accelerationStructureBinding.stages = ShaderStage::RayGen;
        VariableArray<DescriptorBindingDesc> accelerationStructureBindings;
        accelerationStructureBindings.push_back(accelerationStructureBinding);
        TSharedPtr<VulkanDescriptorSetLayout> accelerationStructureLayout =
            MakeShared<VulkanDescriptorSetLayout>(device, accelerationStructureBindings);
        const VariableArray<vk::DescriptorSetLayoutBinding>& vkAccelerationStructureBindings =
            accelerationStructureLayout->GetVkBindings();
        if (vkAccelerationStructureBindings.size() != 1 ||
            vkAccelerationStructureBindings[0].descriptorType != vk::DescriptorType::eAccelerationStructureKHR ||
            vkAccelerationStructureBindings[0].stageFlags != vk::ShaderStageFlagBits::eRaygenKHR)
        {
            std::cerr << "加速構造descriptorのVulkan型またはRT stageが不正です\n";
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

        const String rayGenerationPath =
            NORVES_SOURCE_ROOT "/Assets/Shaders/RayTracing/RayTracingVisibilityRayGen.glsl";
        const String missPath = NORVES_SOURCE_ROOT "/Assets/Shaders/RayTracing/RayTracingVisibilityMiss.glsl";
        const String closestHitPath =
            NORVES_SOURCE_ROOT "/Assets/Shaders/RayTracing/RayTracingVisibilityClosestHit.glsl";
        ShaderPtr rayGenerationShader = CompileRayTracingShader(
            *device,
            compiler,
            rayGenerationPath,
            ShaderStage::RayGen);
        ShaderPtr missShader = CompileRayTracingShader(*device, compiler, missPath, ShaderStage::Miss);
        ShaderPtr closestHitShader = CompileRayTracingShader(*device, compiler, closestHitPath, ShaderStage::ClosestHit);
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

        DescriptorBinding accelerationStructureBinding;
        accelerationStructureBinding.binding = 0;
        accelerationStructureBinding.type = ResourceBindType::AccelerationStructure;
        accelerationStructureBinding.stages = ShaderStage::RayGen;
        DescriptorBinding resultBufferBinding;
        resultBufferBinding.binding = 1;
        resultBufferBinding.type = ResourceBindType::RWBuffer;
        resultBufferBinding.stages = ShaderStage::RayGen;
        DescriptorSetDesc rayTracingSet;
        rayTracingSet.bindings.push_back(accelerationStructureBinding);
        rayTracingSet.bindings.push_back(resultBufferBinding);
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

        if (!VerifyTriangleVisibility(*device, createdPipeline, rayTracingSet))
        {
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
