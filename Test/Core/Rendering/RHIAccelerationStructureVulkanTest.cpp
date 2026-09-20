// BDA入力から加速構造を構築し、ray queryの交差結果をGPUで検証します。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanAccelerationStructure.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanDevice.h"

#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::RHI::Vulkan;

    constexpr const char* TestName = "RHIAccelerationStructureVulkanTest";

    struct RayQueryObjects
    {
        vk::Device Device;
        vk::ShaderModule Shader;
        vk::DescriptorSetLayout DescriptorSetLayout;
        vk::DescriptorPool DescriptorPool;
        vk::PipelineLayout PipelineLayout;
        vk::Pipeline Pipeline;

        ~RayQueryObjects()
        {
            if (Pipeline)
            {
                Device.destroyPipeline(Pipeline);
            }
            if (PipelineLayout)
            {
                Device.destroyPipelineLayout(PipelineLayout);
            }
            if (DescriptorPool)
            {
                Device.destroyDescriptorPool(DescriptorPool);
            }
            if (DescriptorSetLayout)
            {
                Device.destroyDescriptorSetLayout(DescriptorSetLayout);
            }
            if (Shader)
            {
                Device.destroyShaderModule(Shader);
            }
        }
    };

    bool RunRayQuery(
        VulkanDevice& device,
        const AccelerationStructurePtr& topLevel,
        const BufferPtr& resultBuffer)
    {
        const String shaderSource = R"glsl(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x = 1) in;
layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;
layout(set = 0, binding = 1, std430) buffer QueryResults
{
    uint values[];
} queryResults;
void main()
{
    uint rayIndex = gl_GlobalInvocationID.x;
    vec3 origin = rayIndex == 0u ? vec3(0.0, 0.0, 2.0) : vec3(3.0, 0.0, 2.0);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT, 0xffu, origin, 0.0,
                          vec3(0.0, 0.0, -1.0), 10.0);
    while (rayQueryProceedEXT(query))
    {
    }
    uint intersectionType = rayQueryGetIntersectionTypeEXT(query, true);
    queryResults.values[rayIndex] =
        intersectionType == gl_RayQueryCommittedIntersectionTriangleEXT ? 1u : 0u;
}
)glsl";
        ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!compiler)
        {
            std::cerr << "ray query shader compilerを作成できませんでした\n";
            return false;
        }

        const ShaderCompileResult compileResult = compiler->CompileFromSource(
            shaderSource,
            ShaderStage::Compute,
            "RHIAccelerationStructureRayQuery.comp");
        if (!compileResult.bSuccess || compileResult.ByteCode.empty() ||
            compileResult.ByteCode.size() % sizeof(uint32_t) != 0)
        {
            std::cerr << "ray query shaderのコンパイルに失敗しました: "
                      << compileResult.ErrorMessage.c_str() << '\n';
            return false;
        }

        RayQueryObjects objects;
        objects.Device = device.GetVkDevice();
        vk::ShaderModuleCreateInfo shaderInfo{};
        shaderInfo.codeSize = compileResult.ByteCode.size();
        shaderInfo.pCode = reinterpret_cast<const uint32_t*>(compileResult.ByteCode.data());
        objects.Shader = objects.Device.createShaderModule(shaderInfo);
        if (!objects.Shader)
        {
            std::cerr << "ray query shader moduleを作成できませんでした\n";
            return false;
        }

        vk::DescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.bindingCount = 2;
        setLayoutInfo.pBindings = bindings;
        objects.DescriptorSetLayout = objects.Device.createDescriptorSetLayout(setLayoutInfo);
        if (!objects.DescriptorSetLayout)
        {
            std::cerr << "ray query descriptor layoutを作成できませんでした\n";
            return false;
        }

        vk::DescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = vk::DescriptorType::eAccelerationStructureKHR;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 1;
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        objects.DescriptorPool = objects.Device.createDescriptorPool(poolInfo);
        if (!objects.DescriptorPool)
        {
            std::cerr << "ray query descriptor poolを作成できませんでした\n";
            return false;
        }

        vk::DescriptorSetAllocateInfo setAllocateInfo{};
        setAllocateInfo.descriptorPool = objects.DescriptorPool;
        setAllocateInfo.descriptorSetCount = 1;
        setAllocateInfo.pSetLayouts = &objects.DescriptorSetLayout;
        const auto descriptorSets = objects.Device.allocateDescriptorSets(setAllocateInfo);
        if (descriptorSets.empty())
        {
            std::cerr << "ray query descriptor setを確保できませんでした\n";
            return false;
        }
        const vk::DescriptorSet descriptorSet = descriptorSets[0];

        TSharedPtr<VulkanAccelerationStructure> vulkanTopLevel =
            DynamicPointerCast<VulkanAccelerationStructure>(topLevel);
        TSharedPtr<VulkanBuffer> vulkanResultBuffer = DynamicPointerCast<VulkanBuffer>(resultBuffer);
        if (!vulkanTopLevel || !vulkanResultBuffer)
        {
            std::cerr << "Vulkan query resourceの型が一致しません\n";
            return false;
        }

        vk::AccelerationStructureKHR topLevelHandle = vulkanTopLevel->GetVkAccelerationStructure();
        vk::WriteDescriptorSetAccelerationStructureKHR accelerationStructureWrite{};
        accelerationStructureWrite.accelerationStructureCount = 1;
        accelerationStructureWrite.pAccelerationStructures = &topLevelHandle;

        vk::DescriptorBufferInfo resultBufferInfo{};
        resultBufferInfo.buffer = vulkanResultBuffer->GetVkBuffer();
        resultBufferInfo.offset = 0;
        resultBufferInfo.range = sizeof(uint32_t) * 2u;
        vk::WriteDescriptorSet writes[2]{};
        writes[0].pNext = &accelerationStructureWrite;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eAccelerationStructureKHR;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &resultBufferInfo;
        objects.Device.updateDescriptorSets(2, writes, 0, nullptr);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &objects.DescriptorSetLayout;
        objects.PipelineLayout = objects.Device.createPipelineLayout(pipelineLayoutInfo);
        if (!objects.PipelineLayout)
        {
            std::cerr << "ray query pipeline layoutを作成できませんでした\n";
            return false;
        }

        vk::PipelineShaderStageCreateInfo shaderStage{};
        shaderStage.stage = vk::ShaderStageFlagBits::eCompute;
        shaderStage.module = objects.Shader;
        shaderStage.pName = "main";
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = shaderStage;
        pipelineInfo.layout = objects.PipelineLayout;
        const auto pipelineResult = objects.Device.createComputePipeline(nullptr, pipelineInfo);
        if (pipelineResult.result != vk::Result::eSuccess)
        {
            std::cerr << "ray query compute pipelineを作成できませんでした\n";
            return false;
        }
        objects.Pipeline = pipelineResult.value;

        vk::CommandBuffer commandBuffer = device.BeginSingleTimeCommands();
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, objects.Pipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            objects.PipelineLayout,
            0,
            descriptorSet,
            {});
        commandBuffer.dispatch(2, 1, 1);

        vk::BufferMemoryBarrier resultBarrier{};
        resultBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        resultBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        resultBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        resultBarrier.buffer = vulkanResultBuffer->GetVkBuffer();
        resultBarrier.offset = 0;
        resultBarrier.size = sizeof(uint32_t) * 2u;
        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eHost,
            {},
            0,
            nullptr,
            1,
            &resultBarrier,
            0,
            nullptr);
        device.EndSingleTimeCommands(commandBuffer);

        const auto* results = static_cast<const uint32_t*>(resultBuffer->Map(0, sizeof(uint32_t) * 2u));
        const uint32_t hit = results[0];
        const uint32_t miss = results[1];
        resultBuffer->Unmap();
        std::cout << "ray_query_hit=" << hit << '\n';
        std::cout << "ray_query_miss=" << miss << '\n';
        if (hit != 1u || miss != 0u)
        {
            std::cerr << "ray queryの交差/非交差結果が解析値と一致しません\n";
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
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }

        const AccelerationStructureDesc blasResourceDesc = []
        {
            AccelerationStructureDesc desc;
            desc.type = AccelerationStructureType::BottomLevel;
            desc.geometryCapacities.push_back({AccelerationStructureGeometryType::Triangles, 1, true});
            return desc;
        }();
        AccelerationStructurePtr accelerationStructure = device->CreateAccelerationStructure(blasResourceDesc);
        if (!device->GetCapabilities().RayTracing.bAccelerationStructure)
        {
            BufferPtr vertexBuffer = device->CreateBuffer(
                BufferDesc{36, ResourceUsage::VertexBuffer, false, "RHIAccelerationStructure.LegacyVertex"});
            BufferPtr indexBuffer = device->CreateBuffer(
                BufferDesc{12, ResourceUsage::IndexBuffer, false, "RHIAccelerationStructure.LegacyIndex"});
            if (accelerationStructure || !vertexBuffer || !indexBuffer ||
                vertexBuffer->GetUsage() != ResourceUsage::VertexBuffer ||
                indexBuffer->GetUsage() != ResourceUsage::IndexBuffer)
            {
                std::cerr << "RT無効時の資源非公開または従来バッファ作成に失敗しました\n";
                return 1;
            }
            std::cout << "ray_tracing_supported=false\n";
            std::cout << "legacy_vertex_index_buffers=true\n";
            return 0;
        }

        if (!accelerationStructure || accelerationStructure->GetSize() == 0 ||
            accelerationStructure->GetDeviceAddress() == 0)
        {
            std::cerr << "BLAS resourceを作成できませんでした\n";
            return 1;
        }
        if (!device->GetCapabilities().RayTracing.bRayQuery)
        {
            return ReportGpuTestSkip(TestName, "GPU ray query機能を利用できません");
        }

        TSharedPtr<VulkanDevice> vulkanDevice = DynamicPointerCast<VulkanDevice>(device);
        if (!vulkanDevice)
        {
            std::cerr << "Vulkan backend deviceへ変換できませんでした\n";
            return 1;
        }

        struct Vertex
        {
            float position[3];
        };
        const Vertex vertices[3] = {
            {{-1.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}},
        };
        const uint32_t indices[3] = {0, 1, 2};
        BufferDesc vertexDesc;
        vertexDesc.Size = sizeof(vertices);
        vertexDesc.Usage = ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress;
        vertexDesc.CPUAccessible = true;
        vertexDesc.DebugName = "RHIAccelerationStructure.Vertices";
        BufferDesc indexDesc;
        indexDesc.Size = sizeof(indices);
        indexDesc.Usage = ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress;
        indexDesc.CPUAccessible = true;
        indexDesc.DebugName = "RHIAccelerationStructure.Indices";
        BufferPtr vertexBuffer = device->CreateBuffer(vertexDesc);
        BufferPtr indexBuffer = device->CreateBuffer(indexDesc);
        if (!vertexBuffer || !indexBuffer || vertexBuffer->GetDeviceAddress() == 0 ||
            indexBuffer->GetDeviceAddress() == 0)
        {
            std::cerr << "BDA対応のvertex/index bufferを作成できませんでした\n";
            return 1;
        }
        vertexBuffer->Update(vertices, sizeof(vertices));
        indexBuffer->Update(indices, sizeof(indices));

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
        AccelerationStructureBuildDesc blasBuildDesc;
        blasBuildDesc.type = AccelerationStructureType::BottomLevel;
        blasBuildDesc.destination = accelerationStructure;
        blasBuildDesc.geometries.push_back(triangleGeometry);
        if (!accelerationStructure->Build(blasBuildDesc))
        {
            std::cerr << "不透明triangle BLASを構築できませんでした\n";
            return 1;
        }

        AccelerationStructureDesc tlasResourceDesc;
        tlasResourceDesc.type = AccelerationStructureType::TopLevel;
        tlasResourceDesc.maxInstanceCount = 1;
        AccelerationStructurePtr topLevel = device->CreateAccelerationStructure(tlasResourceDesc);
        if (!topLevel || topLevel->GetDeviceAddress() == 0)
        {
            std::cerr << "ray query用TLAS resourceを作成できませんでした\n";
            return 1;
        }
        AccelerationStructureInstanceDesc instance;
        instance.bottomLevel = accelerationStructure;
        AccelerationStructureBuildDesc tlasBuildDesc;
        tlasBuildDesc.type = AccelerationStructureType::TopLevel;
        tlasBuildDesc.destination = topLevel;
        tlasBuildDesc.instances.push_back(instance);
        if (!topLevel->Build(tlasBuildDesc))
        {
            std::cerr << "ray query用TLASを構築できませんでした\n";
            return 1;
        }

        BufferDesc resultDesc;
        resultDesc.Size = sizeof(uint32_t) * 2u;
        resultDesc.Usage = ResourceUsage::StorageBuffer;
        resultDesc.CPUAccessible = true;
        resultDesc.DebugName = "RHIAccelerationStructure.QueryResults";
        BufferPtr resultBuffer = device->CreateBuffer(resultDesc);
        if (!resultBuffer || !RunRayQuery(*vulkanDevice, topLevel, resultBuffer))
        {
            return 1;
        }

        std::cout << "RHI acceleration structure Vulkan検証に成功しました\n";
        return 0;
    }
}

int main()
{
    return RunTest();
}
