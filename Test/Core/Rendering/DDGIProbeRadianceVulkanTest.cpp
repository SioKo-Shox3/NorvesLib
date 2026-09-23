// DDGI probe rayの材質・直接光・遮蔽・環境radianceをVulkan GPUで検証する。
#include "Rendering/FramePacket.h"
#include "Rendering/LightingPass.h"
#include "Rendering/LightingPassLightPacking.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/IAccelerationStructure.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "DDGIProbeRadianceVulkanTest";
    constexpr uint32_t ProbeCount = DDGIProbeRayDirectionCount;
    constexpr uint32_t HitDirectionIndex = ProbeCount - 1u;
    constexpr uint32_t PlaneCustomIndex = 17u;
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float EnvironmentScale = 1.5f;

    struct Vertex
    {
        float Position[3];
    };

    struct ProbeObservation
    {
        VariableArray<DDGIProbeRayQueryResult> Results;
        VariableArray<TWeakPtr<IBuffer>> GeometryInputLeases;
    };

    struct TriangleResources
    {
        BufferPtr VertexBuffer;
        BufferPtr IndexBuffer;
        AccelerationStructurePtr BottomLevel;
    };

    struct SceneResources
    {
        AccelerationStructureBuildDesc TopLevelBuild;
    };

    bool RecordHostReadBarrier(
        const TSharedPtr<Vulkan::VulkanCommandList>& commandList,
        const BufferPtr& readbackBuffer)
    {
        TSharedPtr<Vulkan::VulkanBuffer> vulkanBuffer =
            DynamicPointerCast<Vulkan::VulkanBuffer>(readbackBuffer);
        if (!commandList || !vulkanBuffer)
        {
            return false;
        }

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = vulkanBuffer->GetVkBuffer();
        barrier.offset = 0u;
        barrier.size = VK_WHOLE_SIZE;
        commandList->GetVkCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            {},
            0u,
            nullptr,
            1u,
            &barrier,
            0u,
            nullptr);
        return true;
    }

    bool CreateTriangleResources(const DevicePtr& device,
                                 const Vertex (&vertices)[3],
                                 const char* debugName,
                                 TriangleResources& outResources)
    {
        const uint32_t indices[3] = {0u, 1u, 2u};
        BufferDesc vertexDesc;
        vertexDesc.Size = sizeof(vertices);
        vertexDesc.Usage = ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress;
        vertexDesc.CPUAccessible = true;
        vertexDesc.DebugName = debugName;
        outResources.VertexBuffer = device->CreateBuffer(vertexDesc);

        BufferDesc indexDesc;
        indexDesc.Size = sizeof(indices);
        indexDesc.Usage = ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress;
        indexDesc.CPUAccessible = true;
        indexDesc.DebugName = "DDGIProbeRadiance.Indices";
        outResources.IndexBuffer = device->CreateBuffer(indexDesc);
        if (!outResources.VertexBuffer || !outResources.IndexBuffer ||
            outResources.VertexBuffer->GetDeviceAddress() == 0u ||
            outResources.IndexBuffer->GetDeviceAddress() == 0u)
        {
            std::cerr << "radiance fixtureのBDA geometry bufferを作成できません\n";
            return false;
        }
        outResources.VertexBuffer->Update(vertices, sizeof(vertices));
        outResources.IndexBuffer->Update(indices, sizeof(indices));

        AccelerationStructureDesc bottomLevelDesc;
        bottomLevelDesc.type = AccelerationStructureType::BottomLevel;
        bottomLevelDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, 1u, true});
        outResources.BottomLevel = device->CreateAccelerationStructure(bottomLevelDesc);
        if (!outResources.BottomLevel)
        {
            std::cerr << "radiance fixtureのBLASを作成できません\n";
            return false;
        }

        AccelerationStructureGeometryDesc geometry;
        geometry.type = AccelerationStructureGeometryType::Triangles;
        geometry.opaque = true;
        geometry.triangles.vertexBuffer = outResources.VertexBuffer;
        geometry.triangles.vertexCount = 3u;
        geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        geometry.triangles.indexBuffer = outResources.IndexBuffer;
        geometry.triangles.indexCount = 3u;
        geometry.triangles.indexFormat = IndexType::Uint32;

        AccelerationStructureBuildDesc build;
        build.type = AccelerationStructureType::BottomLevel;
        build.destination = outResources.BottomLevel;
        build.geometries.push_back(geometry);
        if (!outResources.BottomLevel->Build(build))
        {
            std::cerr << "radiance fixtureのBLASを構築できません\n";
            return false;
        }
        return true;
    }

    void AppendInstance(FramePacket& packet,
                        const TriangleResources& triangle,
                        uint32_t customIndex,
                        const RayTracingHitMaterialSnapshot& material,
                        uint8_t instanceMask = RayTracingInstanceMaskShadowCaster)
    {
        RayTracingSceneInstanceSnapshot snapshot;
        snapshot.SourceVertexBuffer = triangle.VertexBuffer;
        snapshot.SourceIndexBuffer = triangle.IndexBuffer;
        snapshot.AccelerationStructureVertexBuffer = triangle.VertexBuffer;
        snapshot.AccelerationStructureIndexBuffer = triangle.IndexBuffer;
        snapshot.VertexStride = sizeof(Vertex);
        snapshot.VertexCount = 3u;
        snapshot.IndexCount = 3u;
        snapshot.bGeometryOpaque = true;
        snapshot.Instance.bottomLevel = triangle.BottomLevel;
        snapshot.Instance.customIndex = customIndex;
        snapshot.Instance.mask = instanceMask;
        snapshot.Instance.disableTriangleFacingCull = true;
        snapshot.BottomLevel = triangle.BottomLevel;
        snapshot.Material = material;
        packet.RayTracingScene.Instances.push_back(snapshot);
    }

    bool CreateTestScene(const DevicePtr& device,
                         bool bAddLightOccluders,
                         FramePacket& outPacket,
                         SceneResources& outResources,
                         bool bOccludersCastShadow = true)
    {
        const uint8_t occluderMask = bOccludersCastShadow ? RayTracingInstanceMaskShadowCaster
                                                          : RayTracingInstanceMaskNonShadowCaster;
        DDGIVolumeParameters volume = MakeDefaultDDGIVolumeParameters();
        volume.bEnabled = true;
        volume.Origin = Math::Vector3(0.0f, 0.0f, 2.0f);
        volume.ProbeSpacing = Math::Vector3::One;
        volume.ProbeCountX = 1u;
        volume.ProbeCountY = 1u;
        volume.ProbeCountZ = 1u;
        outPacket.Scene.SetDDGIVolumeParameters(volume);

        const Vertex planeVertices[3] = {
            {{-100.0f, -100.0f, 0.0f}},
            {{100.0f, -100.0f, 0.0f}},
            {{0.0f, 100.0f, 0.0f}},
        };
        TriangleResources plane;
        if (!CreateTriangleResources(device,
                                     planeVertices,
                                     "DDGIProbeRadiance.PlaneVertices",
                                     plane))
        {
            return false;
        }

        RayTracingHitMaterialSnapshot material;
        material.BaseColor[0] = 0.25f;
        material.BaseColor[1] = 0.5f;
        material.BaseColor[2] = 0.75f;
        material.BaseColor[3] = 1.0f;
        const float emissiveInput[3] = {0.8f, 0.35f, 0.12f};
        if (!TryBuildCanonicalEmissive(emissiveInput,
                                       2.5f,
                                       material.EmissiveColor,
                                       material.EmissiveLuminanceNits))
        {
            return false;
        }
        AppendInstance(outPacket, plane, PlaneCustomIndex, material);

        if (bAddLightOccluders)
        {
            const Vertex positiveXVertices[3] = {
                {{2.0f, -10.0f, -10.0f}},
                {{2.0f, 10.0f, -10.0f}},
                {{2.0f, 0.0f, 10.0f}},
            };
            TriangleResources positiveX;
            if (!CreateTriangleResources(device,
                                         positiveXVertices,
                                         "DDGIProbeRadiance.PositiveXOccluder",
                                         positiveX))
            {
                return false;
            }
            AppendInstance(outPacket, positiveX, PlaneCustomIndex + 1u, {}, occluderMask);

            const Vertex negativeXVertices[3] = {
                {{-2.0f, -10.0f, -10.0f}},
                {{-2.0f, 10.0f, -10.0f}},
                {{-2.0f, 0.0f, 10.0f}},
            };
            TriangleResources negativeX;
            if (!CreateTriangleResources(device,
                                         negativeXVertices,
                                         "DDGIProbeRadiance.NegativeXOccluder",
                                         negativeX))
            {
                return false;
            }
            AppendInstance(outPacket, negativeX, PlaneCustomIndex + 2u, {}, occluderMask);
        }

        AccelerationStructureDesc topLevelDesc;
        topLevelDesc.type = AccelerationStructureType::TopLevel;
        topLevelDesc.maxInstanceCount = static_cast<uint32_t>(outPacket.RayTracingScene.Instances.size());
        outPacket.RayTracingScene.TopLevel = device->CreateAccelerationStructure(topLevelDesc);
        if (!outPacket.RayTracingScene.TopLevel)
        {
            std::cerr << "radiance fixtureのTLASを作成できません\n";
            return false;
        }

        outResources.TopLevelBuild.type = AccelerationStructureType::TopLevel;
        outResources.TopLevelBuild.destination = outPacket.RayTracingScene.TopLevel;
        for (const RayTracingSceneInstanceSnapshot& snapshot : outPacket.RayTracingScene.Instances)
        {
            outResources.TopLevelBuild.instances.push_back(snapshot.Instance);
        }
        return true;
    }

    void AddTestLights(FramePacket& packet)
    {
        LightProxy directional;
        directional.Type = LightType::Directional;
        directional.DirectionX = 0.0f;
        directional.DirectionY = 0.0f;
        directional.DirectionZ = -1.0f;
        directional.ColorR = 1.0f;
        directional.ColorG = 0.9f;
        directional.ColorB = 0.8f;
        directional.CanonicalIntensity = 3.0f;
        packet.Scene.LightProxies.push_back(directional);

        LightProxy point;
        point.Type = LightType::Point;
        point.PositionX = 4.0f;
        point.PositionY = 0.0f;
        point.PositionZ = 2.0f;
        point.ColorR = 0.8f;
        point.ColorG = 0.3f;
        point.ColorB = 0.1f;
        point.CanonicalIntensity = 24.0f;
        point.Range = 10.0f;
        packet.Scene.LightProxies.push_back(point);

        LightProxy spot;
        spot.Type = LightType::Spot;
        spot.PositionX = -4.0f;
        spot.PositionY = 0.0f;
        spot.PositionZ = 2.0f;
        spot.DirectionX = 4.0f / std::sqrt(20.0f);
        spot.DirectionY = 0.0f;
        spot.DirectionZ = -2.0f / std::sqrt(20.0f);
        spot.ColorR = 0.1f;
        spot.ColorG = 0.3f;
        spot.ColorB = 1.0f;
        spot.CanonicalIntensity = 32.0f;
        spot.Range = 10.0f;
        spot.InnerConeAngle = 0.98f;
        spot.OuterConeAngle = 0.85f;
        packet.Scene.LightProxies.push_back(spot);
    }

    bool CreateLightingInputs(const DevicePtr& device,
                              const FramePacket& packet,
                              BufferPtr& outLightBuffer,
                              uint32_t& outLightCount,
                              uint32_t& outLightBufferSize,
                              TexturePtr& outEnvironmentTexture,
                              SamplerPtr& outEnvironmentSampler)
    {
        VariableArray<GPULightData> gpuLights;
        for (const LightProxy& proxy : packet.Scene.LightProxies)
        {
            GPULightData packedLight;
            if (PackLightingPassLight(proxy, packedLight))
            {
                gpuLights.push_back(packedLight);
            }
        }

        outLightCount = static_cast<uint32_t>(gpuLights.size());
        const uint32_t lightCapacity = outLightCount > 0u ? outLightCount : 1u;
        outLightBufferSize = lightCapacity * static_cast<uint32_t>(sizeof(GPULightData));
        BufferDesc lightBufferDesc(
            outLightBufferSize,
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead,
            true,
            "DDGIProbeRadiance.Lights");
        outLightBuffer = device->CreateBuffer(lightBufferDesc);
        if (!outLightBuffer)
        {
            std::cerr << "radiance fixtureのライトbufferを作成できません\n";
            return false;
        }
        if (outLightCount > 0u)
        {
            outLightBuffer->Update(gpuLights.data(), outLightBufferSize);
        }
        else
        {
            const GPULightData emptyLight = {};
            outLightBuffer->Update(&emptyLight, sizeof(emptyLight));
        }

        TextureDesc environmentDesc;
        environmentDesc.Width = 1u;
        environmentDesc.Height = 1u;
        environmentDesc.MipLevels = 1u;
        environmentDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
        environmentDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        environmentDesc.DebugName = "DDGIProbeRadiance.Environment";
        outEnvironmentTexture = device->CreateTexture(environmentDesc);
        if (!outEnvironmentTexture)
        {
            std::cerr << "radiance fixtureのenvironment textureを作成できません\n";
            return false;
        }
        const uint16_t environmentPixel[4] = {0x3800u, 0x3400u, 0x3000u, 0x3C00u};
        outEnvironmentTexture->Update(environmentPixel,
                                      sizeof(environmentPixel),
                                      sizeof(environmentPixel));

        SamplerDesc samplerDesc;
        samplerDesc.filterMin = FilterMode::Point;
        samplerDesc.filterMag = FilterMode::Point;
        samplerDesc.filterMip = FilterMode::Point;
        samplerDesc.addressU = TextureAddressMode::Wrap;
        samplerDesc.addressV = TextureAddressMode::Clamp;
        samplerDesc.addressW = TextureAddressMode::Clamp;
        outEnvironmentSampler = device->CreateSampler(samplerDesc);
        return outEnvironmentSampler != nullptr;
    }

    void PublishLighting(ViewRenderContext& context,
                         uint64_t frameNumber,
                         const BufferPtr& lightBuffer,
                         uint32_t lightCount,
                         uint32_t lightBufferSize,
                         const TexturePtr& environmentTexture,
                         const SamplerPtr& environmentSampler)
    {
        context.PhysicalLighting.Begin(frameNumber, 0u, 0u);
        context.PhysicalLighting.PublishLighting(
            lightBuffer,
            lightCount,
            lightBufferSize,
            environmentTexture,
            environmentSampler,
            {},
            {},
            {},
            {},
            {},
            {},
            1u,
            EnvironmentScale,
            true);
    }

    bool RunProbeFrame(const DevicePtr& device,
                       ShaderManager& shaderManager,
                       DDGIProbePass& probePass,
                       FramePacket& packet,
                       const SceneResources& sceneResources,
                       const BufferPtr& lightBuffer,
                       uint32_t lightCount,
                       uint32_t lightBufferSize,
                       const TexturePtr& environmentTexture,
                       const SamplerPtr& environmentSampler,
                       uint32_t frameIndex,
                       ProbeObservation& outObservation)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "radiance fixture用command listを作成できません\n";
            return false;
        }
        TSharedPtr<Vulkan::VulkanCommandList> vulkanCommandList =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        if (!vulkanCommandList)
        {
            std::cerr << "Vulkan command listへ変換できません\n";
            return false;
        }

        const NorvesLib::RHI::DeviceCapabilitiesA& capabilities = device->GetCapabilities();
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &capabilities;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        context.FrameIndex = frameIndex;
        context.FrameNumber = frameIndex + 1u;
        context.PhysicalLighting.Begin(context.FrameNumber, 0u, 0u);
        context.CommandList = commandList.get();
        PublishLighting(context,
                        context.FrameNumber,
                        lightBuffer,
                        lightCount,
                        lightBufferSize,
                        environmentTexture,
                        environmentSampler);

        commandList->SetFrameIndex(frameIndex);
        commandList->Begin();
        if (!commandList->BuildAccelerationStructure(sceneResources.TopLevelBuild) ||
            !probePass.Execute(context))
        {
            std::cerr << "TLAS buildまたはprobe radiance dispatchを記録できません\n";
            return false;
        }

        BufferPtr resultBuffer = probePass.GetResultBuffer(frameIndex, 0u, 0u);
        const uint32_t resultCount = probePass.GetResultCount(frameIndex, 0u, 0u);
        if (!resultBuffer || resultCount != ProbeCount)
        {
            std::cerr << "probe radianceの出力bufferまたは要素数が不正です\n";
            return false;
        }
        outObservation.GeometryInputLeases.clear();
        for (RayTracingSceneInstanceSnapshot& instance : packet.RayTracingScene.Instances)
        {
            if (!instance.AccelerationStructureVertexBuffer ||
                !instance.AccelerationStructureIndexBuffer)
            {
                return false;
            }
            outObservation.GeometryInputLeases.push_back(
                instance.AccelerationStructureVertexBuffer);
            outObservation.GeometryInputLeases.push_back(
                instance.AccelerationStructureIndexBuffer);
            instance.SourceVertexBuffer.reset();
            instance.SourceIndexBuffer.reset();
            instance.AccelerationStructureVertexBuffer.reset();
            instance.AccelerationStructureIndexBuffer.reset();
        }
        for (const TWeakPtr<IBuffer>& geometryLease : outObservation.GeometryInputLeases)
        {
            if (geometryLease.expired())
            {
                std::cerr << "probe radiance dispatchの実行中にgeometry buffer参照が失われました\n";
                return false;
            }
        }
        const uint64_t resultSize =
            static_cast<uint64_t>(resultCount) * sizeof(DDGIProbeRayQueryResult);
        BufferDesc readbackDesc(
            resultSize, ResourceUsage::TransferDst, true, "DDGIProbeRadiance.Readback");
        BufferPtr readbackBuffer = device->CreateBuffer(readbackDesc);
        if (!readbackBuffer)
        {
            std::cerr << "probe radiance readback bufferを作成できません\n";
            return false;
        }

        commandList->BufferBarrier(
            resultBuffer, ResourceState::ShaderResource, ResourceState::CopySource, 0u, resultSize);
        commandList->BufferBarrier(
            readbackBuffer, ResourceState::Undefined, ResourceState::CopyDest, 0u, resultSize);
        commandList->CopyBuffer(resultBuffer, readbackBuffer, resultSize);
        commandList->BufferBarrier(
            resultBuffer, ResourceState::CopySource, ResourceState::ShaderResource, 0u, resultSize);
        if (!RecordHostReadBarrier(vulkanCommandList, readbackBuffer))
        {
            return false;
        }
        commandList->End();
        commandList->Submit(true);

        const void* mapped = readbackBuffer->Map(0u, resultSize);
        if (mapped == nullptr)
        {
            std::cerr << "probe radiance readbackをmapできません\n";
            return false;
        }
        outObservation.Results.resize(resultCount);
        std::memcpy(outObservation.Results.data(), mapped, static_cast<size_t>(resultSize));
        readbackBuffer->Unmap();
        return true;
    }

    double ComputeExpectedLightContribution(const LightProxy& light,
                                            const Math::Vector3& position,
                                            const Math::Vector3& normal,
                                            bool bOccluded)
    {
        if (bOccluded || !light.IsValid())
        {
            return 0.0;
        }

        double directionX = 0.0;
        double directionY = 0.0;
        double directionZ = 0.0;
        double attenuation = 1.0;
        if (light.Type == LightType::Directional)
        {
            directionX = -light.DirectionX;
            directionY = -light.DirectionY;
            directionZ = -light.DirectionZ;
        }
        else
        {
            directionX = static_cast<double>(light.PositionX) - position.x;
            directionY = static_cast<double>(light.PositionY) - position.y;
            directionZ = static_cast<double>(light.PositionZ) - position.z;
            const double distance = std::sqrt(directionX * directionX +
                                              directionY * directionY +
                                              directionZ * directionZ);
            if (!std::isfinite(distance) || distance <= 1.0e-6)
            {
                return 0.0;
            }
            directionX /= distance;
            directionY /= distance;
            directionZ /= distance;
            attenuation = ComputeLocalInverseSquare(distance) *
                          ComputeRangeWindow(distance, light.Range);
            if (light.Type == LightType::Spot)
            {
                const double spotLength = std::sqrt(
                    static_cast<double>(light.DirectionX) * light.DirectionX +
                    static_cast<double>(light.DirectionY) * light.DirectionY +
                    static_cast<double>(light.DirectionZ) * light.DirectionZ);
                const double theta = directionX * (-light.DirectionX / spotLength) +
                                     directionY * (-light.DirectionY / spotLength) +
                                     directionZ * (-light.DirectionZ / spotLength);
                const double coneWidth = std::max(
                    static_cast<double>(light.InnerConeAngle - light.OuterConeAngle), 0.001);
                attenuation *= std::clamp(
                    (theta - light.OuterConeAngle) / coneWidth, 0.0, 1.0);
            }
        }

        const double normalCosine = std::max(
            static_cast<double>(normal.x) * directionX +
                static_cast<double>(normal.y) * directionY +
                static_cast<double>(normal.z) * directionZ,
            0.0);
        const double emittedIntensity = light.CanonicalIntensity * normalCosine * attenuation;
        return emittedIntensity / static_cast<double>(Pi);
    }

    bool IsFiniteRadiance(const float (&radiance)[4])
    {
        return std::isfinite(radiance[0]) && std::isfinite(radiance[1]) &&
               std::isfinite(radiance[2]) && std::isfinite(radiance[3]);
    }

    bool ValidateObservation(const ProbeObservation& observation,
                            const FramePacket& packet,
                            bool bExpectedOcclusion,
                            const char* scenario)
    {
        if (observation.Results.size() != ProbeCount)
        {
            std::cerr << scenario << "の結果要素数が不正です\n";
            return false;
        }

        const DDGIProbeRayQueryResult& hit = observation.Results[HitDirectionIndex];
        const DDGIProbeRayQueryResult& miss = observation.Results[0u];
        Math::Vector3 rayDirection;
        if (!TryGetDDGIProbeRayDirection(HitDirectionIndex, rayDirection))
        {
            return false;
        }
        const float expectedDistance = -2.0f / rayDirection.z;
        if (hit.bHit != 1u ||
            std::abs(hit.Distance - expectedDistance) > 0.01f ||
            hit.InstanceCustomIndex != PlaneCustomIndex ||
            hit.PrimitiveIndex != 0u || !IsFiniteRadiance(hit.Radiance) ||
            miss.bHit != 0u || miss.Distance != -1.0f ||
            miss.InstanceCustomIndex != UINT32_MAX ||
            miss.PrimitiveIndex != UINT32_MAX || !IsFiniteRadiance(miss.Radiance))
        {
            std::cerr << scenario << "でhit/miss属性が不正です\n";
            return false;
        }

        const Math::Vector3 hitPosition(
            rayDirection.x * expectedDistance,
            rayDirection.y * expectedDistance,
            0.0f);
        const Math::Vector3 normal(0.0f, 0.0f, 1.0f);
        const RayTracingHitMaterialSnapshot& material =
            packet.RayTracingScene.Instances[0u].Material;
        float expected[3] = {
            material.EmissiveColor[0] * material.EmissiveLuminanceNits,
            material.EmissiveColor[1] * material.EmissiveLuminanceNits,
            material.EmissiveColor[2] * material.EmissiveLuminanceNits};

        for (uint32_t lightIndex = 0u;
             lightIndex < packet.Scene.LightProxies.size();
             ++lightIndex)
        {
            const bool bOccluded = bExpectedOcclusion && lightIndex > 0u;
            const LightProxy& light = packet.Scene.LightProxies[lightIndex];
            const double scalar = ComputeExpectedLightContribution(
                light, hitPosition, normal, bOccluded);
            expected[0] += static_cast<float>(material.BaseColor[0] * scalar *
                                              (light.ColorR / (0.2126 * light.ColorR +
                                                               0.7152 * light.ColorG +
                                                               0.0722 * light.ColorB)));
            expected[1] += static_cast<float>(material.BaseColor[1] * scalar *
                                              (light.ColorG / (0.2126 * light.ColorR +
                                                               0.7152 * light.ColorG +
                                                               0.0722 * light.ColorB)));
            expected[2] += static_cast<float>(material.BaseColor[2] * scalar *
                                              (light.ColorB / (0.2126 * light.ColorR +
                                                               0.7152 * light.ColorG +
                                                               0.0722 * light.ColorB)));
        }

        const float* actual = hit.Radiance;
        std::cout << scenario << "_hit_expected="
                  << expected[0] << ',' << expected[1] << ',' << expected[2]
                  << " actual=" << actual[0] << ',' << actual[1] << ',' << actual[2] << '\n';
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const float tolerance = std::max(0.025f, std::abs(expected[channel]) * 0.035f);
            if (std::abs(actual[channel] - expected[channel]) > tolerance)
            {
                std::cerr << scenario << "のhit radianceが期待値と不一致です channel="
                          << channel << " actual=" << actual[channel]
                          << " expected=" << expected[channel] << '\n';
                return false;
            }
        }

        const float environment[3] = {
            0.5f * EnvironmentScale,
            0.25f * EnvironmentScale,
            0.125f * EnvironmentScale};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (std::abs(miss.Radiance[channel] - environment[channel]) > 0.002f)
            {
                std::cerr << scenario << "のmiss environment radianceが不一致です channel="
                          << channel << " actual=" << miss.Radiance[channel]
                          << " expected=" << environment[channel] << '\n';
                return false;
            }
        }

        std::cout << scenario << "_hit_radiance="
                  << hit.Radiance[0] << ',' << hit.Radiance[1] << ',' << hit.Radiance[2]
                  << " miss_environment="
                  << miss.Radiance[0] << ',' << miss.Radiance[1] << ',' << miss.Radiance[2]
                  << " hit_custom_index=" << hit.InstanceCustomIndex << '\n';
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
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }

        const NorvesLib::RHI::DeviceCapabilitiesA& capabilities = device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery ||
            !capabilities.bBufferDeviceAddress ||
            !capabilities.bShaderInt64)
        {
            return ReportGpuTestSkip(TestName, "ray query/BDA/shaderInt64機能を利用できません");
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << "probe radiance shader用ShaderManagerを初期化できません\n";
            return 1;
        }

        FramePacket unoccludedPacket;
        FramePacket occludedPacket;
        FramePacket nonCasterPacket;
        SceneResources unoccludedScene;
        SceneResources occludedScene;
        SceneResources nonCasterScene;
        if (!CreateTestScene(device, false, unoccludedPacket, unoccludedScene) ||
            !CreateTestScene(device, true, occludedPacket, occludedScene) ||
            !CreateTestScene(device, true, nonCasterPacket, nonCasterScene, false))
        {
            return 1;
        }
        AddTestLights(unoccludedPacket);
        AddTestLights(occludedPacket);
        AddTestLights(nonCasterPacket);

        BufferPtr lightBuffer;
        uint32_t lightCount = 0u;
        uint32_t lightBufferSize = 0u;
        TexturePtr environmentTexture;
        SamplerPtr environmentSampler;
        if (!CreateLightingInputs(device,
                                  unoccludedPacket,
                                  lightBuffer,
                                  lightCount,
                                  lightBufferSize,
                                  environmentTexture,
                                  environmentSampler))
        {
            return 1;
        }

        DDGIProbePass unoccludedProbePass;
        ProbeObservation unoccludedObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           unoccludedProbePass,
                           unoccludedPacket,
                           unoccludedScene,
                           lightBuffer,
                           lightCount,
                           lightBufferSize,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           unoccludedObservation) ||
            !ValidateObservation(unoccludedObservation,
                                 unoccludedPacket,
                                 false,
                                 "unoccluded"))
        {
            return 1;
        }

        // 遮蔽ケースは独立評価し、前ケースの累積irradianceを持ち越さない。
        DDGIProbePass occludedProbePass;
        ProbeObservation occludedObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           occludedProbePass,
                           occludedPacket,
                           occludedScene,
                           lightBuffer,
                           lightCount,
                           lightBufferSize,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           occludedObservation) ||
            !ValidateObservation(occludedObservation,
                                 occludedPacket,
                                 true,
                                 "point_spot_occluded"))
        {
            return 1;
        }

        // 遮蔽板を影を落とさない設定にすると、TLASに含まれてもprobeの光線と影の問い合わせ
        // （caster bitだけ）には当たらず、遮蔽なしと同じ放射輝度になる。
        DDGIProbePass nonCasterProbePass;
        ProbeObservation nonCasterObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           nonCasterProbePass,
                           nonCasterPacket,
                           nonCasterScene,
                           lightBuffer,
                           lightCount,
                           lightBufferSize,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           nonCasterObservation) ||
            !ValidateObservation(nonCasterObservation,
                                 nonCasterPacket,
                                 false,
                                 "non_shadow_caster_occluders"))
        {
            return 1;
        }

        const DDGIProbeRayQueryResult& unoccludedHit =
            unoccludedObservation.Results[HitDirectionIndex];
        const DDGIProbeRayQueryResult& occludedHit =
            occludedObservation.Results[HitDirectionIndex];
        if (!(unoccludedHit.Radiance[0] > occludedHit.Radiance[0]) ||
            !(unoccludedHit.Radiance[1] > occludedHit.Radiance[1]) ||
            !(unoccludedHit.Radiance[2] > occludedHit.Radiance[2]))
        {
            std::cerr << "point/spot遮蔽でhit radianceが減少しません\n";
            return 1;
        }

        std::cout << "directional_point_spot_lambert=true emissive_snapshot=true"
                     " point_spot_shadow_queries=true environment_miss=true\n";
        unoccludedProbePass.Shutdown();
        occludedProbePass.Shutdown();
        nonCasterProbePass.Shutdown();
        shaderManager.Shutdown();
        device->WaitIdle();
        for (const ProbeObservation* observation :
             {&unoccludedObservation, &occludedObservation, &nonCasterObservation})
        {
            for (const TWeakPtr<IBuffer>& geometryLease : observation->GeometryInputLeases)
            {
                if (!geometryLease.expired())
                {
                    std::cerr << "probe radiance pass終了後にgeometry buffer参照が残っています\n";
                    return 1;
                }
            }
        }
        return 0;
    }
}

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "DDGIProbeRadianceVulkanTest threw an exception: "
                  << exception.what() << '\n';
        return 1;
    }
}
