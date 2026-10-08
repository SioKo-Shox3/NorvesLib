// LightingPassのRTGI ray-query computeと既存間接光fallbackをGPU readbackで検証する。
#include "Rendering/FramePacket.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/LightingPass.h"
#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/LightingPassLightPacking.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RTGIContract.h"
#include "Rendering/PointShadowSnapshot.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VirtualShadowMapPointLights.h"
#include "Rendering/VirtualShadowMapSample.h"
#include "RenderingValidation/GpuTestEnvironment.h"

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
#include <utility>

namespace NorvesLib::Core::Rendering
{
    struct RTGIDiffuseIndirectVulkanTestAccess
    {
        static void ExecuteWithInputs(LightingPass& pass,
                                      ViewRenderContext& context,
                                      const RHI::TexturePtr& albedo,
                                      const RHI::TexturePtr& normal,
                                      const RHI::TexturePtr& material,
                                      const RHI::TexturePtr& depth,
                                      const RHI::TexturePtr& velocity,
                                      const RHI::TexturePtr& emissive,
                                      const RHI::TexturePtr& rtgiOutput)
        {
            pass.ExecuteWithInputs(context,
                                   albedo,
                                   normal,
                                   material,
                                   depth,
                                   velocity,
                                   emissive,
                                   RHI::TexturePtr{},
                                   RHI::TexturePtr{},
                                   rtgiOutput,
                                   false);
        }

        // 今の Execute が使っている組（ExecuteWithInputs の直後は、その Execute の組）
        struct ActiveExecuteSet
        {
            RHI::BufferPtr LightData;
            RHI::BufferPtr LightArray;
            RHI::BufferPtr VsmSample;
            RHI::BufferPtr VsmPointSample;
            RHI::BufferPtr VsmSlice;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        static ActiveExecuteSet GetActiveExecuteSet(const LightingPass& pass)
        {
            return ActiveExecuteSet{pass.m_LightDataBuffer,
                                    pass.m_LightArrayBuffer,
                                    pass.m_VsmSampleBuffer,
                                    pass.m_VsmPointSampleBuffer,
                                    pass.m_VsmSliceBuffer,
                                    pass.m_LightingDescriptorSet};
        }

        // グラフを通さずに Execute を呼ぶ試験が、VSM のページの表とプールを渡す
        static void SetFrameVsmBuffers(LightingPass& pass, const RHI::BufferPtr& pageTable, const RHI::BufferPtr& pool)
        {
            pass.m_FrameVsmPageTable = pageTable;
            pass.m_FrameVsmPool = pool;
        }

        static RHI::TexturePtr GetSceneColorTexture(const LightingPass& pass)
        {
            return pass.m_SceneColorTexture;
        }

        static uint32_t GetHistoryAgeCap(const LightingPass& pass)
        {
            return pass.m_RTGIHistoryAgeCap;
        }

        static RHI::TexturePtr GetDenoisedTexture(const LightingPass& pass)
        {
            return pass.m_RTGIDenoisedTexture;
        }

        // 直近のdispatchが書いた画素ごとの履歴の年齢と、その現在の状態。
        static RHI::TexturePtr GetWrittenHistoryAge(const LightingPass& pass,
                                                    RHI::ResourceState& outState)
        {
            outState = pass.m_RTGIHistorySlotState[pass.m_RTGIHistoryWriteIndex];
            return pass.m_RTGIHistoryTextures[pass.m_RTGIHistoryWriteIndex].Age;
        }
    };
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RTGIDiffuseIndirectVulkanTest";
    constexpr uint32_t TestWidth = 2u;
    constexpr uint32_t TestHeight = 2u;
    constexpr uint32_t BytesPerPixel = 8u;
    constexpr uint32_t ExpectedInstanceCustomIndex = 17u;
    constexpr uint32_t ExpectedSceneRevision = 41u;
    constexpr uint32_t ExpectedLightRevision = 7u;

    // 提出（Submit）ごとに 1 つ進める描画フレームの通し番号。照明の Execute ごとの資源の組は、通し番号が同じ間は
    // 同じフレームの別のビューポートとして別の組を使うので、別々に提出する試験の各呼び出しには別の番号を渡す。
    uint64_t NextRenderFrameSerial()
    {
        static uint64_t serial = 1000000u;
        return ++serial;
    }

    struct Vertex
    {
        float Position[3];
    };

    struct TestGBuffer
    {
        TexturePtr Albedo;
        TexturePtr Normal;
        TexturePtr Material;
        TexturePtr Depth;
        TexturePtr Velocity;
        TexturePtr Emissive;
    };

    struct LightingFrameObservation
    {
        VariableArray<uint16_t> RTGIPixels;
        VariableArray<uint8_t> SceneColor;
        RTGIIndirectLightingSource Source = RTGIIndirectLightingSource::Raster;
        RTGIFallbackReason Reason = RTGIFallbackReason::ResourceUnavailable;
        bool bPublished = false;
        uint32_t DrawCallCount = 0u;
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

    bool RecordTextureReadback(const CommandListPtr& commandList,
                               const TSharedPtr<Vulkan::VulkanCommandList>& vulkanCommandList,
                               const TexturePtr& source,
                               const BufferPtr& readback)
    {
        if (!commandList || !vulkanCommandList || !source || !readback)
        {
            return false;
        }

        const uint64_t size = static_cast<uint64_t>(source->GetWidth()) *
                              source->GetHeight() * BytesPerPixel;
        commandList->TextureBarrier(source,
                                     ResourceState::ShaderResource,
                                     ResourceState::CopySource);
        commandList->BufferBarrier(readback,
                                    ResourceState::Undefined,
                                    ResourceState::CopyDest,
                                    0u,
                                    size);
        commandList->CopyTextureToBuffer(source,
                                         readback,
                                         source->GetWidth(),
                                         source->GetHeight(),
                                         0u);
        commandList->TextureBarrier(source,
                                     ResourceState::CopySource,
                                     ResourceState::ShaderResource);
        return RecordHostReadBarrier(vulkanCommandList, readback);
    }

    bool BuildTestTopLevel(const DevicePtr& device,
                           AccelerationStructurePtr& outTopLevel,
                           AccelerationStructurePtr& outBottomLevel,
                           BufferPtr& outVertexBuffer,
                           BufferPtr& outIndexBuffer,
                           AccelerationStructureBuildDesc& outTopLevelBuild)
    {
        const Vertex vertices[] = {
            {{-100.0f, -100.0f, 1.0f}},
            {{100.0f, -100.0f, 1.0f}},
            {{0.0f, 100.0f, 1.0f}},
        };
        const uint32_t indices[] = {0u, 1u, 2u};

        BufferDesc vertexDesc;
        vertexDesc.Size = sizeof(vertices);
        vertexDesc.Usage = ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress;
        vertexDesc.CPUAccessible = true;
        vertexDesc.DebugName = "RTGIDiffuseIndirectVulkanTest.Vertices";
        outVertexBuffer = device->CreateBuffer(vertexDesc);

        BufferDesc indexDesc;
        indexDesc.Size = sizeof(indices);
        indexDesc.Usage = ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress;
        indexDesc.CPUAccessible = true;
        indexDesc.DebugName = "RTGIDiffuseIndirectVulkanTest.Indices";
        outIndexBuffer = device->CreateBuffer(indexDesc);
        if (!outVertexBuffer || !outIndexBuffer ||
            outVertexBuffer->GetDeviceAddress() == 0u ||
            outIndexBuffer->GetDeviceAddress() == 0u)
        {
            std::cerr << "RTGI用のdevice-address bufferを作成できませんでした\n";
            return false;
        }
        outVertexBuffer->Update(vertices, sizeof(vertices));
        outIndexBuffer->Update(indices, sizeof(indices));

        AccelerationStructureDesc bottomLevelDesc;
        bottomLevelDesc.type = AccelerationStructureType::BottomLevel;
        bottomLevelDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, 1u, true});
        outBottomLevel = device->CreateAccelerationStructure(bottomLevelDesc);
        if (!outBottomLevel)
        {
            std::cerr << "RTGI用のBLASを作成できませんでした\n";
            return false;
        }

        AccelerationStructureGeometryDesc triangleGeometry;
        triangleGeometry.type = AccelerationStructureGeometryType::Triangles;
        triangleGeometry.opaque = true;
        triangleGeometry.triangles.vertexBuffer = outVertexBuffer;
        triangleGeometry.triangles.vertexCount = 3u;
        triangleGeometry.triangles.vertexStride = sizeof(Vertex);
        triangleGeometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        triangleGeometry.triangles.indexBuffer = outIndexBuffer;
        triangleGeometry.triangles.indexCount = 3u;
        triangleGeometry.triangles.indexFormat = IndexType::Uint32;

        AccelerationStructureBuildDesc bottomLevelBuild;
        bottomLevelBuild.type = AccelerationStructureType::BottomLevel;
        bottomLevelBuild.destination = outBottomLevel;
        bottomLevelBuild.geometries.push_back(triangleGeometry);
        if (!outBottomLevel->Build(bottomLevelBuild))
        {
            std::cerr << "RTGI用のBLASを構築できませんでした\n";
            return false;
        }

        AccelerationStructureDesc topLevelDesc;
        topLevelDesc.type = AccelerationStructureType::TopLevel;
        topLevelDesc.maxInstanceCount = 1u;
        outTopLevel = device->CreateAccelerationStructure(topLevelDesc);
        if (!outTopLevel)
        {
            std::cerr << "RTGI用のTLASを作成できませんでした\n";
            return false;
        }

        AccelerationStructureInstanceDesc instance;
        instance.bottomLevel = outBottomLevel;
        instance.customIndex = ExpectedInstanceCustomIndex;
        instance.disableTriangleFacingCull = true;
        outTopLevelBuild.type = AccelerationStructureType::TopLevel;
        outTopLevelBuild.destination = outTopLevel;
        outTopLevelBuild.instances.push_back(instance);
        return true;
    }

    TexturePtr CreateTexture(const DevicePtr& device,
                             Format format,
                             ResourceUsage usage,
                             const char* name)
    {
        TextureDesc desc;
        desc.Width = TestWidth;
        desc.Height = TestHeight;
        desc.TextureFormat = format;
        desc.Usage = usage;
        desc.DebugName = name;
        return device->CreateTexture(desc);
    }

    bool CreateTestGBuffer(const DevicePtr& device,
                           SharedResourceRegistry& registry,
                           TestGBuffer& outGBuffer)
    {
        const ResourceUsage sampledUsage = ResourceUsage::ShaderRead |
                                           ResourceUsage::TransferDst;
        outGBuffer.Albedo = CreateTexture(device,
                                          Format::R8G8B8A8_UNORM,
                                          sampledUsage,
                                          "RTGIDiffuseIndirectVulkanTest.Albedo");
        outGBuffer.Normal = CreateTexture(device,
                                          Format::R16G16B16A16_FLOAT,
                                          sampledUsage,
                                          "RTGIDiffuseIndirectVulkanTest.Normal");
        outGBuffer.Material = CreateTexture(device,
                                            Format::R8G8B8A8_UNORM,
                                            sampledUsage,
                                            "RTGIDiffuseIndirectVulkanTest.Material");
        outGBuffer.Depth = CreateTexture(device,
                                         Format::R32_FLOAT,
                                         sampledUsage,
                                         "RTGIDiffuseIndirectVulkanTest.Depth");
        outGBuffer.Velocity = CreateTexture(device,
                                            Format::R16G16B16A16_FLOAT,
                                            sampledUsage,
                                            "RTGIDiffuseIndirectVulkanTest.Velocity");
        outGBuffer.Emissive = CreateTexture(device,
                                            Format::R16G16B16A16_FLOAT,
                                            sampledUsage,
                                            "RTGIDiffuseIndirectVulkanTest.Emissive");
        if (!outGBuffer.Albedo || !outGBuffer.Normal || !outGBuffer.Material ||
            !outGBuffer.Depth || !outGBuffer.Velocity || !outGBuffer.Emissive)
        {
            std::cerr << "RTGI用GBufferを作成できませんでした\n";
            return false;
        }

        const uint8_t albedoPixels[TestWidth * TestHeight * 4u] = {
            255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u,
            255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u,
        };
        const uint8_t materialPixels[TestWidth * TestHeight * 4u] = {
            0u, 128u, 255u, 255u, 0u, 128u, 255u, 255u,
            0u, 128u, 255u, 255u, 0u, 128u, 255u, 255u,
        };
        const uint16_t normalPixels[TestWidth * TestHeight * 4u] = {
            0u, 0u, 0x3C00u, 0x3C00u, 0u, 0u, 0xBC00u, 0x3C00u,
            0u, 0u, 0x3C00u, 0x3C00u, 0u, 0u, 0xBC00u, 0x3C00u,
        };
        const float depthPixels[TestWidth * TestHeight] = {
            0.0f, 0.0f, 0.0f, 0.0f};
        const uint16_t zeroPixels[TestWidth * TestHeight * 4u] = {};
        outGBuffer.Albedo->Update(albedoPixels, TestWidth * 4u, sizeof(albedoPixels));
        outGBuffer.Normal->Update(normalPixels, TestWidth * 8u, sizeof(normalPixels));
        outGBuffer.Material->Update(materialPixels, TestWidth * 4u, sizeof(materialPixels));
        outGBuffer.Depth->Update(depthPixels, TestWidth * sizeof(float), sizeof(depthPixels));
        outGBuffer.Velocity->Update(zeroPixels, TestWidth * 8u, sizeof(zeroPixels));
        outGBuffer.Emissive->Update(zeroPixels, TestWidth * 8u, sizeof(zeroPixels));

        registry.RegisterTexturePtr("GBuffer_Albedo", outGBuffer.Albedo);
        registry.RegisterTexturePtr("GBuffer_Normal", outGBuffer.Normal);
        registry.RegisterTexturePtr("GBuffer_Material", outGBuffer.Material);
        registry.RegisterTexturePtr("GBuffer_Depth", outGBuffer.Depth);
        registry.RegisterTexturePtr("GBuffer_Velocity", outGBuffer.Velocity);
        registry.RegisterTexturePtr("GBuffer_Emissive", outGBuffer.Emissive);
        return true;
    }

    void PopulateRayTracingSnapshot(const AccelerationStructurePtr& topLevel,
                                    const AccelerationStructurePtr& bottomLevel,
                                    const BufferPtr& vertexBuffer,
                                    const BufferPtr& indexBuffer,
                                    FramePacket& outPacket)
    {
        outPacket.RayTracingScene.TopLevel = topLevel;
        RayTracingSceneInstanceSnapshot instance;
        instance.BottomLevel = bottomLevel;
        instance.AccelerationStructureVertexBuffer = vertexBuffer;
        instance.AccelerationStructureIndexBuffer = indexBuffer;
        instance.VertexCount = 3u;
        instance.VertexStride = sizeof(Vertex);
        instance.IndexCount = 3u;
        instance.Instance.bottomLevel = bottomLevel;
        instance.Instance.customIndex = ExpectedInstanceCustomIndex;
        instance.Instance.disableTriangleFacingCull = true;
        instance.Material.BaseColor[0] = 1.0f;
        instance.Material.BaseColor[1] = 1.0f;
        instance.Material.BaseColor[2] = 1.0f;
        instance.Material.BaseColor[3] = 1.0f;
        instance.Material.EmissiveColor[0] = 1.0f;
        instance.Material.EmissiveColor[1] = 0.5f;
        instance.Material.EmissiveColor[2] = 0.25f;
        instance.Material.EmissiveLuminanceNits = 4.0f;
        outPacket.RayTracingScene.Instances.push_back(std::move(instance));
    }

    bool IsFiniteHalf(uint16_t value)
    {
        return (value & 0x7C00u) != 0x7C00u;
    }

    bool IsPositiveHalf(uint16_t value)
    {
        return (value & 0x7FFFu) != 0u;
    }

    bool IsZeroHalf(uint16_t value)
    {
        return (value & 0x7FFFu) == 0u;
    }

    float HalfToFloat(uint16_t value)
    {
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;
        const float sign = (value & 0x8000u) != 0u ? -1.0f : 1.0f;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 31u)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(static_cast<float>(mantissa | 0x400u),
                                 static_cast<int>(exponent) - 25);
    }

    bool ValidateRTGIReadback(const LightingFrameObservation& observation)
    {
        if (observation.RTGIPixels.size() != TestWidth * TestHeight * 4u)
        {
            return false;
        }

        for (uint16_t value : observation.RTGIPixels)
        {
            if (!IsFiniteHalf(value))
            {
                return false;
            }
        }

        const auto pixelOffset = [](uint32_t pixelIndex) -> uint32_t
        {
            return pixelIndex * 4u;
        };
        for (const uint32_t hitPixel : {0u, 2u})
        {
            const uint32_t offset = pixelOffset(hitPixel);
            if (!IsPositiveHalf(observation.RTGIPixels[offset + 0u]) ||
                !IsPositiveHalf(observation.RTGIPixels[offset + 1u]) ||
                !IsPositiveHalf(observation.RTGIPixels[offset + 2u]))
            {
                return false;
            }
        }
        for (const uint32_t missPixel : {1u, 3u})
        {
            const uint32_t offset = pixelOffset(missPixel);
            if (!IsZeroHalf(observation.RTGIPixels[offset + 0u]) ||
                !IsZeroHalf(observation.RTGIPixels[offset + 1u]) ||
                !IsZeroHalf(observation.RTGIPixels[offset + 2u]))
            {
                return false;
            }
        }
        return true;
    }

    bool AreAllRTGIPixelsZero(const LightingFrameObservation& observation)
    {
        if (observation.RTGIPixels.size() != TestWidth * TestHeight * 4u)
        {
            return false;
        }
        for (uint32_t pixel = 0u; pixel < TestWidth * TestHeight; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                if (!IsZeroHalf(observation.RTGIPixels[pixel * 4u + channel]))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool AreSceneColorsEqual(const LightingFrameObservation& lhs,
                             const LightingFrameObservation& rhs)
    {
        if (lhs.SceneColor.size() != rhs.SceneColor.size())
        {
            return false;
        }
        for (size_t index = 0u; index < lhs.SceneColor.size(); ++index)
        {
            if (lhs.SceneColor[index] != rhs.SceneColor[index])
            {
                return false;
            }
        }
        return true;
    }

    bool RunLightingFrame(const DevicePtr& device,
                          const RHI::DeviceCapabilities& capabilities,
                          const RTGIRayQueryCapability& rtgiCapability,
                          LightingPass& lightingPass,
                          SceneRenderer& renderer,
                          ViewRenderContext& context,
                          const RayTracingSceneSnapshot& rayTracingScene,
                          const AccelerationStructureBuildDesc& topLevelBuild,
                          const TestGBuffer& gbuffer,
                          const TexturePtr& rtgiOutput,
                          bool bRTGIEnabled,
                          bool bReadbackRTGI,
                          bool bBuildTopLevel,
                          uint64_t frameNumber,
                          LightingFrameObservation& outObservation)
    {
        renderer.ResetStats();
        const TexturePtr sceneColor =
            RTGIDiffuseIndirectVulkanTestAccess::GetSceneColorTexture(lightingPass);
        if (!device || !sceneColor || !rtgiOutput)
        {
            std::cerr << "RTGIまたはSceneColorの出力textureを取得できませんでした\n";
            return false;
        }

        const uint64_t readbackSize = static_cast<uint64_t>(TestWidth) *
                                      TestHeight * BytesPerPixel;
        BufferDesc sceneColorReadbackDesc(
            readbackSize, ResourceUsage::TransferDst, true,
            "RTGIDiffuseIndirectVulkanTest.SceneColorReadback");
        BufferPtr sceneColorReadback = device->CreateBuffer(sceneColorReadbackDesc);
        BufferPtr rtgiReadback;
        if (!sceneColorReadback)
        {
            std::cerr << "SceneColor readback bufferを作成できませんでした\n";
            return false;
        }
        if (bReadbackRTGI)
        {
            BufferDesc rtgiReadbackDesc(
                readbackSize, ResourceUsage::TransferDst, true,
                "RTGIDiffuseIndirectVulkanTest.RTGIReadback");
            rtgiReadback = device->CreateBuffer(rtgiReadbackDesc);
            if (!rtgiReadback)
            {
                std::cerr << "RTGI readback bufferを作成できませんでした\n";
                return false;
            }
        }

        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "RTGI用command listを作成できませんでした\n";
            return false;
        }
        TSharedPtr<Vulkan::VulkanCommandList> vulkanCommandList =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        if (!vulkanCommandList)
        {
            std::cerr << "Vulkan command listへ変換できません\n";
            return false;
        }

        context.Device = device.get();
        context.Capabilities = &capabilities;
        context.CommandList = commandList.get();
        context.FrameIndex = 0u;
        context.FrameNumber = frameNumber;
        context.RenderFrameSerial = NextRenderFrameSerial();
        context.SnapshotRayTracingScene = &rayTracingScene;
        context.RTGICapability = rtgiCapability;
        context.bRTGIEnabled = bRTGIEnabled;
        // RenderingCoordinatorと同じく、影を落とす物体がないシーンはTLAS利用不可として扱う。
        context.bRTGITLASAvailable = rayTracingScene.IsComplete() &&
                                     rayTracingScene.HasShadowCasters();
        context.SceneRevision = ExpectedSceneRevision;
        context.LightRevision = ExpectedLightRevision;
        context.PhysicalLighting.Begin(frameNumber, 0u, 0u);
        context.PhysicalLighting.ConfigureRTGI(rtgiCapability,
                                               bRTGIEnabled,
                                               context.bRTGITLASAvailable,
                                               context.SceneRevision,
                                               context.LightRevision);
        commandList->SetFrameIndex(0u);
        commandList->Begin();

        bool bRecorded = true;
        if (bBuildTopLevel &&
            !commandList->BuildAccelerationStructure(topLevelBuild))
        {
            std::cerr << "同一command listへのRTGI TLAS buildを記録できませんでした\n";
            bRecorded = false;
        }
        if (bRecorded && bReadbackRTGI)
        {
            commandList->TextureBarrier(rtgiOutput,
                                        ResourceState::Undefined,
                                        ResourceState::UnorderedAccess);
        }

        if (bRecorded)
        {
            RTGIDiffuseIndirectVulkanTestAccess::ExecuteWithInputs(
                lightingPass,
                context,
                gbuffer.Albedo,
                gbuffer.Normal,
                gbuffer.Material,
                gbuffer.Depth,
                gbuffer.Velocity,
                gbuffer.Emissive,
                rtgiOutput);
        }

        const RTGIFallbackDecision decision = context.PhysicalLighting.ResolveIndirectLighting();
        outObservation.Source = decision.Source;
        outObservation.Reason = decision.Reason;
        outObservation.bPublished = context.PhysicalLighting.RTGI.bPublished;
        outObservation.DrawCallCount = renderer.GetStats().DrawCallCount;

        if (bRecorded && bReadbackRTGI)
        {
            bRecorded = RecordTextureReadback(commandList,
                                              vulkanCommandList,
                                              rtgiOutput,
                                              rtgiReadback) &&
                        bRecorded;
        }
        if (bRecorded)
        {
            bRecorded = RecordTextureReadback(commandList,
                                              vulkanCommandList,
                                              sceneColor,
                                              sceneColorReadback) &&
                        bRecorded;
        }
        commandList->End();
        if (!bRecorded)
        {
            return false;
        }
        commandList->Submit(true);

        const void* mappedSceneColor = sceneColorReadback->Map(0u, readbackSize);
        if (!mappedSceneColor)
        {
            std::cerr << "SceneColor readbackをmapできませんでした\n";
            return false;
        }
        outObservation.SceneColor.resize(static_cast<size_t>(readbackSize));
        std::memcpy(outObservation.SceneColor.data(),
                    mappedSceneColor,
                    static_cast<size_t>(readbackSize));
        sceneColorReadback->Unmap();

        if (bReadbackRTGI)
        {
            const void* mappedRTGI = rtgiReadback->Map(0u, readbackSize);
            if (!mappedRTGI)
            {
                std::cerr << "RTGI readbackをmapできませんでした\n";
                return false;
            }
            outObservation.RTGIPixels.resize(TestWidth * TestHeight * 4u);
            std::memcpy(outObservation.RTGIPixels.data(),
                        mappedRTGI,
                        static_cast<size_t>(readbackSize));
            rtgiReadback->Unmap();
        }
        return true;
    }

    // 同じフレームの複数の照明の Execute の観測
    struct ExecutedViewport
    {
        RTGIDiffuseIndirectVulkanTestAccess::ActiveExecuteSet Set;
        // 描画の FrameCommand（提出前に積まれた全画面パス）が束縛する descriptor set
        DescriptorSetPtr BoundDescriptorSet;
    };

    uint32_t CountFullscreenPasses(const VariableArray<FrameCommand>& commands)
    {
        uint32_t count = 0u;
        for (const FrameCommand& command : commands)
        {
            count += command.Type == FrameCommandType::FullscreenPass ? 1u : 0u;
        }
        return count;
    }

    DescriptorSetPtr LastFullscreenPassDescriptorSet(const VariableArray<FrameCommand>& commands)
    {
        for (size_t index = commands.size(); index > 0u; --index)
        {
            if (commands[index - 1u].Type == FrameCommandType::FullscreenPass)
            {
                return commands[index - 1u].FullscreenPass.DescriptorSet;
            }
        }
        return DescriptorSetPtr{};
    }

    bool ReadBufferBytes(const BufferPtr& buffer, uint64_t byteCount, VariableArray<uint8_t>& outBytes)
    {
        if (!buffer || buffer->GetSize() < byteCount)
        {
            return false;
        }
        const void* mapped = buffer->Map(0u, byteCount);
        if (!mapped)
        {
            return false;
        }
        outBytes.resize(static_cast<size_t>(byteCount));
        std::memcpy(outBytes.data(), mapped, static_cast<size_t>(byteCount));
        buffer->Unmap();
        return true;
    }

    bool BytesEqual(const VariableArray<uint8_t>& bytes, const void* expected, size_t expectedSize)
    {
        return bytes.size() >= expectedSize && std::memcmp(bytes.data(), expected, expectedSize) == 0;
    }

    // 同じフレームに照明を複数回 Execute する（同じ SceneView の複数のビューポート）。
    // 実 Vulkan 装置で、Execute ごとに別の descriptor set と定数・ライト配列・VSM のパラメータとスライスの表のバッファの組を使うこと、
    // 描画が束縛する descriptor set が Execute ごとに別であること、各組のバッファの中身がその Execute のカメラ・ライト・VSM の値のまま
    // 後の Execute に書き換えられないこと、組の上限（4）を超えた Execute が描かず組を壊さないこと、
    // フレームが変わると先頭の組から使い直すことを確かめる。
    bool RunMultiViewportResourceSetTest(const DevicePtr& device,
                                         const RHI::DeviceCapabilities& capabilities,
                                         const RTGIRayQueryCapability& rtgiCapability,
                                         LightingPass& lightingPass,
                                         SceneRenderer& renderer,
                                         ViewRenderContext& context,
                                         const TestGBuffer& gbuffer)
    {
        constexpr uint32_t ViewportCount = 4u;
        constexpr uint64_t FrameNumber = 900u;
        const Math::Vector3 sunDirection(0.35f, -0.8f, 0.45f);

        // ビューポートごとに別のカメラ・ライト・太陽の VSM のクリップマップ・点光源の VSM の灯
        CameraProxy cameras[ViewportCount];
        VariableArray<LightProxy> lightSets[ViewportCount];
        VirtualShadowMapClipmap clipmaps[ViewportCount];
        VirtualShadowMapPointLights pointLights[ViewportCount];
        for (uint32_t viewport = 0u; viewport < ViewportCount; ++viewport)
        {
            CameraProxy& camera = cameras[viewport];
            camera.CameraId = 100u + viewport;
            camera.PositionX = 3.0f * static_cast<float>(viewport + 1u);
            camera.PositionY = 1.5f;
            camera.PositionZ = 2.0f + static_cast<float>(viewport);
            camera.FieldOfView = 55.0f + static_cast<float>(viewport);
            camera.AspectRatio = 1.0f;
            camera.NearPlane = 0.1f;
            camera.FarPlane = 200.0f;
            camera.Viewport.Width = static_cast<float>(TestWidth);
            camera.Viewport.Height = static_cast<float>(TestHeight);
            for (uint32_t light = 0u; light <= viewport; ++light)
            {
                LightProxy point;
                point.Type = LightType::Point;
                point.PositionX = static_cast<float>(light + 1u) + 5.0f * static_cast<float>(viewport);
                point.PositionY = 2.0f;
                point.PositionZ = 1.0f;
                point.ColorR = 0.9f;
                point.ColorG = 0.5f;
                point.ColorB = 0.2f;
                point.CanonicalIntensity = 10.0f + 3.0f * static_cast<float>(viewport) + static_cast<float>(light);
                point.Range = 8.0f;
                lightSets[viewport].push_back(point);
            }
            clipmaps[viewport] = BuildVirtualShadowMapClipmap(
                sunDirection, 1u, Math::Vector3(camera.PositionX, camera.PositionY, camera.PositionZ), VirtualShadowMapClipmapSettings{});
            PointShadowSnapshot snapshot;
            snapshot.LightCount = 1u;
            snapshot.Lights[0].LightId = 1u;
            snapshot.Lights[0].Position = Math::Vector3(2.0f * static_cast<float>(viewport) + 1.0f, 2.0f, 3.0f);
            snapshot.Lights[0].Range = 10.0f + static_cast<float>(viewport);
            pointLights[viewport] =
                BuildVirtualShadowMapPointLights(snapshot, VirtualShadowMapPointSettings{}, VirtualShadowMap::LEVEL_COUNT);
            if (!clipmaps[viewport].bEnabled || pointLights[viewport].LightCount != 1u)
            {
                std::cerr << "複数ビューポートの試験: VSM の入力を作れませんでした\n";
                return false;
            }
        }

        // 照明が読む VSM のページの表・プール（中身は 0 で、どのページも未割り当て）
        const uint64_t pageTableBytes =
            VirtualShadowMap::PageTableBytes(pointLights[0].FirstSlice + pointLights[0].SliceCount());
        const uint64_t poolBytes = VirtualShadowMap::PAGE_BYTES * 4u;
        BufferPtr pageTable = device->CreateBuffer(
            BufferDesc(pageTableBytes, ResourceUsage::StorageBuffer, true, "RTGIDiffuseIndirectVulkanTest.VsmPageTable"));
        BufferPtr pool = device->CreateBuffer(
            BufferDesc(poolBytes, ResourceUsage::StorageBuffer, true, "RTGIDiffuseIndirectVulkanTest.VsmPool"));
        if (!pageTable || !pool)
        {
            std::cerr << "複数ビューポートの試験: VSM のバッファを作れませんでした\n";
            return false;
        }
        {
            VariableArray<uint8_t> zeros;
            zeros.resize(static_cast<size_t>(std::max(pageTableBytes, poolBytes)));
            std::memset(zeros.data(), 0, zeros.size());
            pageTable->Update(zeros.data(), pageTableBytes);
            pool->Update(zeros.data(), poolBytes);
        }
        RTGIDiffuseIndirectVulkanTestAccess::SetFrameVsmBuffers(lightingPass, pageTable, pool);

        // 1 フレームぶんの Execute を、viewportOrder の順に記録して提出する
        const auto recordFrame = [&](uint64_t frameNumber,
                                     const uint32_t* viewportOrder,
                                     uint32_t executeCount,
                                     ExecutedViewport* outExecuted,
                                     uint32_t& outDrawCalls,
                                     uint32_t& outPassesAddedByLastExecute) -> bool
        {
            CommandListPtr commandList = device->CreateCommandList();
            if (!commandList)
            {
                std::cerr << "複数ビューポートの試験: command list を作れませんでした\n";
                return false;
            }
            VariableArray<FrameCommand> pending;
            context.Device = device.get();
            context.Capabilities = &capabilities;
            context.CommandList = commandList.get();
            context.PendingFrameCommands = &pending;
            context.FrameIndex = 0u;
            context.FrameNumber = frameNumber;
            context.RenderFrameSerial = NextRenderFrameSerial();
            context.bRTGIEnabled = false;
            context.bRTGITLASAvailable = false;
            renderer.ResetStats();
            commandList->SetFrameIndex(0u);
            commandList->Begin();
            for (uint32_t execute = 0u; execute < executeCount; ++execute)
            {
                const uint32_t viewport = viewportOrder[execute];
                context.MainCamera = &cameras[viewport];
                context.SnapshotLightProxies = &lightSets[viewport];
                context.PhysicalLighting.Begin(frameNumber, 0u, viewport);
                context.PhysicalLighting.ConfigureRTGI(
                    rtgiCapability, false, false, context.SceneRevision, context.LightRevision);
                context.PhysicalLighting.SunClipmap = clipmaps[viewport];
                context.PhysicalLighting.PointVsmLights = pointLights[viewport];
                const uint32_t passesBefore = CountFullscreenPasses(pending);
                RTGIDiffuseIndirectVulkanTestAccess::ExecuteWithInputs(lightingPass,
                                                                       context,
                                                                       gbuffer.Albedo,
                                                                       gbuffer.Normal,
                                                                       gbuffer.Material,
                                                                       gbuffer.Depth,
                                                                       gbuffer.Velocity,
                                                                       gbuffer.Emissive,
                                                                       TexturePtr{});
                outPassesAddedByLastExecute = CountFullscreenPasses(pending) - passesBefore;
                if (outExecuted != nullptr && outPassesAddedByLastExecute == 1u)
                {
                    outExecuted[execute].Set = RTGIDiffuseIndirectVulkanTestAccess::GetActiveExecuteSet(lightingPass);
                    outExecuted[execute].BoundDescriptorSet = LastFullscreenPassDescriptorSet(pending);
                }
            }
            // 提出前に、積んだ描画をまとめて記録する（RenderingCoordinator と同じ順）
            renderer.ExecuteFrameCommands(pending, commandList.get());
            outDrawCalls = renderer.GetStats().DrawCallCount;
            commandList->End();
            commandList->Submit(true);
            context.PendingFrameCommands = nullptr;
            context.MainCamera = nullptr;
            return true;
        };

        // 組のバッファの中身が、cameraViewport のカメラ・ライト・VSM の値であること
        const uint32_t clampedPoolPages = static_cast<uint32_t>(
            std::min<uint64_t>(poolBytes / VirtualShadowMap::PAGE_BYTES, VirtualShadowMap::MAX_POOL_PAGES));
        const float depthHeight = static_cast<float>(gbuffer.Depth->GetHeight());
        const auto verifySet = [&](const ExecutedViewport& entry, uint32_t cameraViewport, const char* label) -> bool
        {
            const CameraProxy& camera = cameras[cameraViewport];
            const float cameraPosition[3] = {camera.PositionX, camera.PositionY, camera.PositionZ};
            const float cameraForward[3] = {camera.ForwardX, camera.ForwardY, camera.ForwardZ};
            bool bOk = true;

            VariableArray<uint8_t> bytes;
            // 照明の定数: そのカメラの位置とライトの数
            GPULightingParams lighting = {};
            if (!ReadBufferBytes(entry.Set.LightData, sizeof(lighting), bytes))
            {
                bOk = false;
            }
            else
            {
                std::memcpy(&lighting, bytes.data(), sizeof(lighting));
                bOk = bOk && std::abs(lighting.cameraPosition[0] - cameraPosition[0]) < 1.0e-4f &&
                      std::abs(lighting.cameraPosition[1] - cameraPosition[1]) < 1.0e-4f &&
                      std::abs(lighting.cameraPosition[2] - cameraPosition[2]) < 1.0e-4f &&
                      lighting.lightCount == static_cast<uint32_t>(lightSets[cameraViewport].size());
            }
            // ライトの配列
            VariableArray<GPULightData> expectedLights;
            const uint32_t expectedLightCount =
                PackLightingPassLights(Span<const LightProxy>(lightSets[cameraViewport]), expectedLights);
            const size_t lightBytes = static_cast<size_t>(expectedLightCount) * sizeof(GPULightData);
            bOk = bOk && expectedLightCount > 0u && ReadBufferBytes(entry.Set.LightArray, lightBytes, bytes) &&
                  BytesEqual(bytes, expectedLights.data(), lightBytes);
            // 太陽の VSM のパラメータ
            GPUVsmSampleParams expectedSample = {};
            const bool bSun = BuildVirtualShadowMapSampleParams(&clipmaps[cameraViewport],
                                                                cameraPosition,
                                                                cameraForward,
                                                                context.PhysicalLighting.CascadedShadow.SplitDistances,
                                                                camera.FieldOfView,
                                                                depthHeight,
                                                                clampedPoolPages,
                                                                expectedSample);
            bOk = bOk && bSun && ReadBufferBytes(entry.Set.VsmSample, sizeof(expectedSample), bytes) &&
                  BytesEqual(bytes, &expectedSample, sizeof(expectedSample));
            // 点光源の VSM のパラメータ
            GPUVsmPointSampleParams expectedPoint = {};
            const bool bPoint = BuildVirtualShadowMapPointSampleParams(
                pointLights[cameraViewport], cameraPosition, camera.FieldOfView, depthHeight, clampedPoolPages, expectedPoint);
            bOk = bOk && bPoint && ReadBufferBytes(entry.Set.VsmPointSample, sizeof(expectedPoint), bytes) &&
                  BytesEqual(bytes, &expectedPoint, sizeof(expectedPoint));
            // スライスの表（太陽の段の後ろに点光源のスライス）
            GPUVsmSlice expectedSlices[VirtualShadowMapMaxSlices];
            std::memset(expectedSlices, 0, sizeof(expectedSlices));
            BuildVirtualShadowMapSlices(&clipmaps[cameraViewport], nullptr, VirtualShadowMapMaxSlices, expectedSlices);
            BuildVirtualShadowMapPointSlices(pointLights[cameraViewport], expectedSlices);
            bOk = bOk && ReadBufferBytes(entry.Set.VsmSlice, sizeof(expectedSlices), bytes) &&
                  BytesEqual(bytes, expectedSlices, sizeof(expectedSlices));
            if (!bOk)
            {
                std::cerr << "複数ビューポートの試験: 組のバッファの中身がカメラ " << cameraViewport << " の値と違います（" << label
                          << "）\n";
            }
            return bOk;
        };

        // フレーム 0: 4 つのビューポート。Execute のたびに別の組が使われ、描画は 4 回
        const uint32_t order[ViewportCount] = {0u, 1u, 2u, 3u};
        ExecutedViewport executed[ViewportCount];
        uint32_t drawCalls = 0u;
        uint32_t lastAdded = 0u;
        if (!recordFrame(FrameNumber, order, ViewportCount, executed, drawCalls, lastAdded) || drawCalls != ViewportCount)
        {
            std::cerr << "複数ビューポートの試験: 4 ビューポートの描画数が不正です draw_calls=" << drawCalls << "\n";
            return false;
        }
        for (uint32_t viewport = 0u; viewport < ViewportCount; ++viewport)
        {
            const ExecutedViewport& entry = executed[viewport];
            if (!entry.BoundDescriptorSet || entry.BoundDescriptorSet != entry.Set.DescriptorSet)
            {
                std::cerr << "複数ビューポートの試験: 描画が束縛する descriptor set が Execute の組と違います viewport=" << viewport
                          << "\n";
                return false;
            }
            for (uint32_t other = viewport + 1u; other < ViewportCount; ++other)
            {
                const RTGIDiffuseIndirectVulkanTestAccess::ActiveExecuteSet& a = entry.Set;
                const RTGIDiffuseIndirectVulkanTestAccess::ActiveExecuteSet& b = executed[other].Set;
                if (entry.BoundDescriptorSet == executed[other].BoundDescriptorSet || a.DescriptorSet == b.DescriptorSet ||
                    a.LightData == b.LightData || a.LightArray == b.LightArray || a.VsmSample == b.VsmSample ||
                    a.VsmPointSample == b.VsmPointSample || a.VsmSlice == b.VsmSlice)
                {
                    std::cerr << "複数ビューポートの試験: 別の Execute が同じ descriptor set またはバッファを使っています viewport="
                              << viewport << " other=" << other << "\n";
                    return false;
                }
            }
            if (!verifySet(entry, viewport, "同じフレームの 4 回の後"))
            {
                return false;
            }
        }

        // 次のフレームで 5 回 Execute する: 先頭の組から使い直し、組の上限を超えた 5 回目は描かず、4 つ目の組を壊さない
        ExecutedViewport overflow[ViewportCount + 1u];
        const uint32_t overflowOrder[ViewportCount + 1u] = {0u, 1u, 2u, 3u, 0u};
        if (!recordFrame(FrameNumber + 1u, overflowOrder, ViewportCount + 1u, overflow, drawCalls, lastAdded) ||
            drawCalls != ViewportCount || lastAdded != 0u)
        {
            std::cerr << "複数ビューポートの試験: 上限を超えた Execute が描いています draw_calls=" << drawCalls
                      << " added=" << lastAdded << "\n";
            return false;
        }
        for (uint32_t viewport = 0u; viewport < ViewportCount; ++viewport)
        {
            if (overflow[viewport].BoundDescriptorSet != executed[viewport].BoundDescriptorSet ||
                !verifySet(overflow[viewport], viewport, "上限を超えた Execute の後"))
            {
                std::cerr << "複数ビューポートの試験: 次のフレームが先頭の組から使い直されていないか、上限超えが組を壊しました viewport="
                          << viewport << "\n";
                return false;
            }
        }

        // カメラの順を逆にしたフレーム: 各組には、その順番の Execute のカメラの値が入る
        ExecutedViewport reversed[ViewportCount];
        const uint32_t reversedOrder[ViewportCount] = {3u, 2u, 1u, 0u};
        if (!recordFrame(FrameNumber + 2u, reversedOrder, ViewportCount, reversed, drawCalls, lastAdded) || drawCalls != ViewportCount)
        {
            std::cerr << "複数ビューポートの試験: 逆順のフレームの描画数が不正です draw_calls=" << drawCalls << "\n";
            return false;
        }
        for (uint32_t execute = 0u; execute < ViewportCount; ++execute)
        {
            if (reversed[execute].BoundDescriptorSet != executed[execute].BoundDescriptorSet ||
                !verifySet(reversed[execute], reversedOrder[execute], "逆順のフレームの後"))
            {
                std::cerr << "複数ビューポートの試験: 逆順のフレームの組が不正です execute=" << execute << "\n";
                return false;
            }
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
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }

        const RHI::DeviceCapabilities& capabilities = device->GetCapabilities();
        const RTGIRayQueryCapability rtgiCapability =
            MakeRTGIRayQueryCapability(capabilities);
        if (!rtgiCapability.IsUsable())
        {
            return ReportGpuTestSkip(TestName,
                                     "GPU ray query/BDA/shaderInt64機能を利用できません");
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << "RTGI shader用のShaderManagerを初期化できませんでした\n";
            return 1;
        }

        FramePacket packet;
        packet.FrameNumber = 1u;
        packet.bRTGIEnabled = true;

        AccelerationStructurePtr topLevel;
        AccelerationStructurePtr bottomLevel;
        BufferPtr vertexBuffer;
        BufferPtr indexBuffer;
        AccelerationStructureBuildDesc topLevelBuild;
        if (!BuildTestTopLevel(device,
                               topLevel,
                               bottomLevel,
                               vertexBuffer,
                               indexBuffer,
                               topLevelBuild))
        {
            return 1;
        }
        PopulateRayTracingSnapshot(topLevel,
                                   bottomLevel,
                                   vertexBuffer,
                                   indexBuffer,
                                   packet);
        RayTracingSceneSnapshot incompleteSnapshot;
        incompleteSnapshot.Instances.push_back(packet.RayTracingScene.Instances[0]);

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &capabilities;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotLightProxies = &packet.Scene.LightProxies;
        context.RenderWidth = TestWidth;
        context.RenderHeight = TestHeight;
        context.ScreenWidth = TestWidth;
        context.ScreenHeight = TestHeight;

        SharedResourceRegistry sharedResources;
        context.SharedResources = &sharedResources;
        TestGBuffer gbuffer;
        if (!CreateTestGBuffer(device, sharedResources, gbuffer))
        {
            return 1;
        }

        TextureDesc rtgiOutputDesc;
        rtgiOutputDesc.Width = TestWidth;
        rtgiOutputDesc.Height = TestHeight;
        rtgiOutputDesc.TextureFormat = RTGIDiffuseIndirectRadianceFormat;
        rtgiOutputDesc.Usage = ResourceUsage::ShaderRead |
                               ResourceUsage::ShaderWrite |
                               ResourceUsage::TransferSrc;
        rtgiOutputDesc.DebugName = "RTGIDiffuseIndirectVulkanTest.Output";
        TexturePtr rtgiOutput = device->CreateTexture(rtgiOutputDesc);
        if (!rtgiOutput)
        {
            std::cerr << "RTGI出力textureを作成できませんでした\n";
            return 1;
        }

        LightingPass lightingPass;
        lightingPass.SetRegisterLegacyBridge(false);
        if (!lightingPass.Initialize(context))
        {
            std::cerr << "LightingPassを初期化できませんでした\n";
            return 1;
        }
        lightingPass.Setup(context);
        SceneRenderer renderer;
        if (!renderer.Initialize(device.get(), nullptr))
        {
            std::cerr << "SceneRendererを初期化できませんでした\n";
            return 1;
        }
        context.Renderer = &renderer;
        if (!RTGIDiffuseIndirectVulkanTestAccess::GetSceneColorTexture(lightingPass))
        {
            std::cerr << "LightingPassのSceneColorが作成されませんでした\n";
            return 1;
        }

        LightingFrameObservation active;
        if (!RunLightingFrame(device,
                              capabilities,
                              rtgiCapability,
                              lightingPass,
                              renderer,
                              context,
                              packet.RayTracingScene,
                              topLevelBuild,
                              gbuffer,
                              rtgiOutput,
                              true,
                              true,
                              true,
                              1u,
                              active) ||
            !ValidateRTGIReadback(active) ||
            !active.bPublished ||
            active.Source != RTGIIndirectLightingSource::RTGI ||
            active.Reason != RTGIFallbackReason::None ||
            active.DrawCallCount != 1u)
        {
            std::cerr << "rtgi_active published=" << active.bPublished
                      << " source=" << static_cast<uint32_t>(active.Source)
                      << " reason=" << static_cast<uint32_t>(active.Reason)
                      << " draw_calls=" << active.DrawCallCount << " pixels=";
            for (uint16_t value : active.RTGIPixels)
            {
                std::cerr << std::hex << value << std::dec << ' ';
            }
            std::cerr << '\n';
            std::cerr << "RTGI有効経路のhit/miss readbackまたは公開状態が不正です\n";
            return 1;
        }

        LightingFrameObservation disabled;
        if (!RunLightingFrame(device,
                              capabilities,
                              rtgiCapability,
                              lightingPass,
                              renderer,
                              context,
                              packet.RayTracingScene,
                              topLevelBuild,
                              gbuffer,
                              rtgiOutput,
                              false,
                              false,
                              false,
                              2u,
                              disabled) ||
            disabled.bPublished ||
            disabled.Source == RTGIIndirectLightingSource::RTGI ||
            disabled.Reason != RTGIFallbackReason::Disabled ||
            disabled.DrawCallCount != 1u)
        {
            std::cerr << "RTGI無効時の既存間接光fallbackが不正です\n";
            return 1;
        }

        LightingFrameObservation incomplete;
        if (!RunLightingFrame(device,
                              capabilities,
                              rtgiCapability,
                              lightingPass,
                              renderer,
                              context,
                              incompleteSnapshot,
                              topLevelBuild,
                              gbuffer,
                              rtgiOutput,
                              true,
                              false,
                              false,
                              3u,
                              incomplete) ||
            incomplete.bPublished ||
            incomplete.Source == RTGIIndirectLightingSource::RTGI ||
            incomplete.Reason != RTGIFallbackReason::TLASUnavailable ||
            incomplete.DrawCallCount != 1u)
        {
            std::cerr << "TLAS不完全時の既存間接光fallbackが不正です\n";
            return 1;
        }

        if (AreSceneColorsEqual(active, disabled) ||
            !AreSceneColorsEqual(disabled, incomplete))
        {
            std::cerr << "RTGI公開時のSceneColor差分またはfallback一致を確認できません\n";
            return 1;
        }

        // 影を落とさない物体だけのシーンは、TLASがあってもTLASのない場合と同じfallbackになる。
        FramePacket nonCasterOnlyPacket;
        PopulateRayTracingSnapshot(topLevel,
                                   bottomLevel,
                                   vertexBuffer,
                                   indexBuffer,
                                   nonCasterOnlyPacket);
        nonCasterOnlyPacket.RayTracingScene.Instances[0].Instance.mask =
            RayTracingInstanceMaskNonShadowCaster;
        AccelerationStructureBuildDesc nonCasterOnlyBuild = topLevelBuild;
        nonCasterOnlyBuild.instances[0].mask = RayTracingInstanceMaskNonShadowCaster;
        LightingFrameObservation nonCasterOnly;
        if (!RunLightingFrame(device,
                              capabilities,
                              rtgiCapability,
                              lightingPass,
                              renderer,
                              context,
                              nonCasterOnlyPacket.RayTracingScene,
                              nonCasterOnlyBuild,
                              gbuffer,
                              rtgiOutput,
                              true,
                              false,
                              true,
                              4u,
                              nonCasterOnly) ||
            !nonCasterOnlyPacket.RayTracingScene.IsComplete() ||
            nonCasterOnly.bPublished ||
            nonCasterOnly.Source == RTGIIndirectLightingSource::RTGI ||
            nonCasterOnly.Reason != RTGIFallbackReason::TLASUnavailable ||
            !AreSceneColorsEqual(nonCasterOnly, incomplete))
        {
            std::cerr << "影を落とさない物体だけのシーンがTLASのない場合と同じfallbackになりません\n";
            return 1;
        }

        // 影を落とす物体があるシーンでは、影を落とさない物体をRTGIの光線と影の問い合わせ（caster bit
        // だけ）が無視する。発光面（影を落とす物体、z=2）だけのシーンと、その手前（z=1）に影を落とさない
        // 発光しない遮蔽板を加えたシーンを、同じ描画フレーム番号（同じ乱数、履歴の再投影なし）で描き、
        // RTGIの出力が画素ごとに一致することを確かめる。1次面は発光面からの光源標本で照らされる。
        const auto buildScene = [&](bool bWithOccluder,
                                    AccelerationStructurePtr& outTopLevel,
                                    FramePacket& outPacket,
                                    AccelerationStructureBuildDesc& outBuild) -> bool
        {
            AccelerationStructureDesc sceneTopLevelDesc;
            sceneTopLevelDesc.type = AccelerationStructureType::TopLevel;
            sceneTopLevelDesc.maxInstanceCount = 2u;
            outTopLevel = device->CreateAccelerationStructure(sceneTopLevelDesc);
            if (!outTopLevel)
            {
                return false;
            }
            PopulateRayTracingSnapshot(outTopLevel, bottomLevel, vertexBuffer, indexBuffer,
                                       outPacket);
            RayTracingSceneInstanceSnapshot emitter = outPacket.RayTracingScene.Instances[0];
            emitter.Instance.customIndex = ExpectedInstanceCustomIndex + 1u;
            emitter.Instance.transform[11] = 1.0f;
            RayTracingSceneInstanceSnapshot& occluder = outPacket.RayTracingScene.Instances[0];
            occluder.Instance.mask = RayTracingInstanceMaskNonShadowCaster;
            for (float& channel : occluder.Material.EmissiveColor)
            {
                channel = 0.0f;
            }
            occluder.Material.EmissiveLuminanceNits = 0.0f;
            if (!bWithOccluder)
            {
                outPacket.RayTracingScene.Instances.clear();
            }
            outPacket.RayTracingScene.Instances.push_back(emitter);
            outBuild.type = AccelerationStructureType::TopLevel;
            outBuild.destination = outTopLevel;
            for (const RayTracingSceneInstanceSnapshot& snapshot : outPacket.RayTracingScene.Instances)
            {
                outBuild.instances.push_back(snapshot.Instance);
            }
            return true;
        };
        AccelerationStructurePtr emitterOnlyTopLevel;
        AccelerationStructurePtr occludedTopLevel;
        FramePacket emitterOnlyPacket;
        FramePacket occludedPacket;
        AccelerationStructureBuildDesc emitterOnlyBuild;
        AccelerationStructureBuildDesc occludedBuild;
        if (!buildScene(false, emitterOnlyTopLevel, emitterOnlyPacket, emitterOnlyBuild) ||
            !buildScene(true, occludedTopLevel, occludedPacket, occludedBuild))
        {
            std::cerr << "発光面と影を落とさない遮蔽板のTLASを作成できませんでした\n";
            return 1;
        }
        constexpr uint64_t CasterComparisonFrame = 40u;
        LightingFrameObservation emitterOnly;
        LightingFrameObservation withNonCaster;
        if (!RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                              context, emitterOnlyPacket.RayTracingScene, emitterOnlyBuild,
                              gbuffer, rtgiOutput, true, true, true, CasterComparisonFrame,
                              emitterOnly) ||
            !RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                              context, occludedPacket.RayTracingScene, occludedBuild, gbuffer,
                              rtgiOutput, true, true, true, CasterComparisonFrame,
                              withNonCaster) ||
            !emitterOnly.bPublished || !withNonCaster.bPublished ||
            emitterOnly.RTGIPixels.size() != withNonCaster.RTGIPixels.size())
        {
            std::cerr << "発光面のRTGIを描けませんでした\n";
            return 1;
        }
        bool bLitByEmitter = true;
        for (const uint32_t hitPixel : {0u, 2u})
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                bLitByEmitter = bLitByEmitter &&
                                IsPositiveHalf(emitterOnly.RTGIPixels[hitPixel * 4u + channel]);
            }
        }
        bool bIdentical = true;
        for (size_t index = 0u; index < emitterOnly.RTGIPixels.size(); ++index)
        {
            bIdentical = bIdentical && emitterOnly.RTGIPixels[index] == withNonCaster.RTGIPixels[index];
        }
        if (!bLitByEmitter || !bIdentical)
        {
            std::cerr << "影を落とさない遮蔽板がRTGIの光線か影の問い合わせに当たりました lit="
                      << bLitByEmitter << " identical=" << bIdentical << "\n";
            return 1;
        }

        // 発光面（z=2）の0.5 mm手前に、発光面より広い影を落とす遮蔽板（z=1.9995）を置く。光源標本の
        // 影の問い合わせが発光面の直前まで調べていれば、発光面の光は1次面へ届かず、遮蔽板だけの
        // シーンとRTGIの出力が画素ごとに一致する。距離を割合で縮める判定（距離2 mの0.1%は2 mm）では
        // 遮蔽板を見落とし、発光面の光が漏れる。
        constexpr float NearOccluderGap = 0.0005f;
        const auto buildNearOccluderScene = [&](bool bWithEmitter,
                                                AccelerationStructurePtr& outTopLevel,
                                                FramePacket& outPacket,
                                                AccelerationStructureBuildDesc& outBuild) -> bool
        {
            AccelerationStructureDesc sceneTopLevelDesc;
            sceneTopLevelDesc.type = AccelerationStructureType::TopLevel;
            sceneTopLevelDesc.maxInstanceCount = 2u;
            outTopLevel = device->CreateAccelerationStructure(sceneTopLevelDesc);
            if (!outTopLevel)
            {
                return false;
            }
            PopulateRayTracingSnapshot(outTopLevel, bottomLevel, vertexBuffer, indexBuffer,
                                       outPacket);
            RayTracingSceneInstanceSnapshot emitter = outPacket.RayTracingScene.Instances[0];
            emitter.Instance.customIndex = ExpectedInstanceCustomIndex + 1u;
            emitter.Instance.transform[11] = 1.0f;
            RayTracingSceneInstanceSnapshot& blocker = outPacket.RayTracingScene.Instances[0];
            blocker.Instance.transform[0] = 2.0f;
            blocker.Instance.transform[5] = 2.0f;
            blocker.Instance.transform[11] = 1.0f - NearOccluderGap;
            for (float& channel : blocker.Material.EmissiveColor)
            {
                channel = 0.0f;
            }
            blocker.Material.EmissiveLuminanceNits = 0.0f;
            if (bWithEmitter)
            {
                outPacket.RayTracingScene.Instances.push_back(emitter);
            }
            outBuild.type = AccelerationStructureType::TopLevel;
            outBuild.destination = outTopLevel;
            for (const RayTracingSceneInstanceSnapshot& snapshot : outPacket.RayTracingScene.Instances)
            {
                outBuild.instances.push_back(snapshot.Instance);
            }
            return true;
        };
        AccelerationStructurePtr nearBlockedTopLevel;
        AccelerationStructurePtr blockerOnlyTopLevel;
        FramePacket nearBlockedPacket;
        FramePacket blockerOnlyPacket;
        AccelerationStructureBuildDesc nearBlockedBuild;
        AccelerationStructureBuildDesc blockerOnlyBuild;
        if (!buildNearOccluderScene(true, nearBlockedTopLevel, nearBlockedPacket, nearBlockedBuild) ||
            !buildNearOccluderScene(false, blockerOnlyTopLevel, blockerOnlyPacket, blockerOnlyBuild))
        {
            std::cerr << "発光面の直前の遮蔽板のTLASを作成できませんでした\n";
            return 1;
        }
        LightingFrameObservation nearBlocked;
        LightingFrameObservation blockerOnly;
        if (!RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                              context, nearBlockedPacket.RayTracingScene, nearBlockedBuild,
                              gbuffer, rtgiOutput, true, true, true, CasterComparisonFrame,
                              nearBlocked) ||
            !RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                              context, blockerOnlyPacket.RayTracingScene, blockerOnlyBuild,
                              gbuffer, rtgiOutput, true, true, true, CasterComparisonFrame,
                              blockerOnly) ||
            !nearBlocked.bPublished || !blockerOnly.bPublished ||
            nearBlocked.RTGIPixels.size() != blockerOnly.RTGIPixels.size())
        {
            std::cerr << "発光面の直前の遮蔽板のRTGIを描けませんでした\n";
            return 1;
        }
        bool bNearBlockedIdentical = true;
        for (size_t index = 0u; index < nearBlocked.RTGIPixels.size(); ++index)
        {
            bNearBlockedIdentical = bNearBlockedIdentical &&
                                    nearBlocked.RTGIPixels[index] == blockerOnly.RTGIPixels[index];
        }
        if (!bNearBlockedIdentical)
        {
            std::cerr << "発光面の0.5 mm手前の遮蔽板を光源標本の影の問い合わせが見落としました\n";
            for (size_t index = 0u; index < nearBlocked.RTGIPixels.size(); ++index)
            {
                std::cerr << "  index=" << index << " blocked=" << nearBlocked.RTGIPixels[index]
                          << " blocker_only=" << blockerOnly.RTGIPixels[index] << "\n";
            }
            return 1;
        }

        // RTGIの出力は発光の強さとpre-exposureの積だけで決まる。発光を1e5倍、pre-exposureを1e-5倍に
        // した描画は、途中の物理単位の値が半精度の上限（65504）を超えても元の描画と一致する。途中で
        // 上限へ切り詰めると、強い発光の描画だけが暗くなる。1次面から見て小さく近い発光三角形
        // （一辺約0.2 m、z=2）にして、1試料の光源標本がどのframeでも同程度の値になるようにし、
        // 同じカメラ（正射影、1次面はz=-0.5〜0.5）で両方を描く。
        AccelerationStructureDesc smallEmitterTopLevelDesc;
        smallEmitterTopLevelDesc.type = AccelerationStructureType::TopLevel;
        smallEmitterTopLevelDesc.maxInstanceCount = 1u;
        AccelerationStructurePtr smallEmitterTopLevel =
            device->CreateAccelerationStructure(smallEmitterTopLevelDesc);
        if (!smallEmitterTopLevel)
        {
            std::cerr << "小さな発光面のTLASを作成できませんでした\n";
            return 1;
        }
        FramePacket smallEmitterPacket;
        PopulateRayTracingSnapshot(smallEmitterTopLevel, bottomLevel, vertexBuffer, indexBuffer,
                                   smallEmitterPacket);
        RayTracingSceneInstanceSnapshot& smallEmitter = smallEmitterPacket.RayTracingScene.Instances[0];
        smallEmitter.Instance.transform[0] = 0.001f;
        smallEmitter.Instance.transform[5] = 0.001f;
        smallEmitter.Instance.transform[11] = 1.0f;
        AccelerationStructureBuildDesc smallEmitterBuild;
        smallEmitterBuild.type = AccelerationStructureType::TopLevel;
        smallEmitterBuild.destination = smallEmitterTopLevel;
        smallEmitterBuild.instances.push_back(smallEmitter.Instance);
        CameraProxy exposureCamera;
        exposureCamera.CameraId = 7u;
        exposureCamera.Projection = ProjectionType::Orthographic;
        exposureCamera.PositionZ = -1.0f;
        exposureCamera.ForwardZ = 1.0f;
        exposureCamera.OrthoWidth = 2.0f;
        exposureCamera.OrthoHeight = 2.0f;
        exposureCamera.NearPlane = 0.5f;
        exposureCamera.FarPlane = 1.5f;
        exposureCamera.AspectRatio = 1.0f;
        exposureCamera.Viewport.Width = static_cast<float>(TestWidth);
        exposureCamera.Viewport.Height = static_cast<float>(TestHeight);
        // 基準の描画は発光4000 nits相当（1次面の標本値は1〜10程度）、pre-exposure 1。
        constexpr float UnitNitsScale = 1000.0f;
        constexpr float ExposureScale = 1.0e5f;
        const auto runExposureFrame = [&](float preExposure,
                                          float nitsScale,
                                          LightingFrameObservation& outObservation) -> bool
        {
            exposureCamera.PreExposure = preExposure;
            exposureCamera.InvPreExposure = 1.0f / preExposure;
            RayTracingHitMaterialSnapshot& emitterMaterial =
                smallEmitterPacket.RayTracingScene.Instances[0].Material;
            const float baseNits = emitterMaterial.EmissiveLuminanceNits;
            emitterMaterial.EmissiveLuminanceNits = baseNits * nitsScale;
            context.MainCamera = &exposureCamera;
            const bool bRan = RunLightingFrame(device, capabilities, rtgiCapability, lightingPass,
                                               renderer, context, smallEmitterPacket.RayTracingScene,
                                               smallEmitterBuild, gbuffer, rtgiOutput, true, true,
                                               true, CasterComparisonFrame, outObservation);
            context.MainCamera = nullptr;
            emitterMaterial.EmissiveLuminanceNits = baseNits;
            return bRan && outObservation.bPublished;
        };
        LightingFrameObservation unitExposure;
        LightingFrameObservation scaledExposure;
        if (!runExposureFrame(1.0f, UnitNitsScale, unitExposure) ||
            !runExposureFrame(1.0f / ExposureScale, UnitNitsScale * ExposureScale, scaledExposure) ||
            unitExposure.RTGIPixels.size() != scaledExposure.RTGIPixels.size())
        {
            std::cerr << "露出を変えたRTGIを描けませんでした\n";
            return 1;
        }
        bool bExposureLit = true;
        bool bExposureInvariant = true;
        // 強い発光の描画で、露出前の値（出力÷pre-exposure。アルベドと拡散の重みは1）が半精度の
        // 上限を超える画素があること（切り詰めの有無を区別できる条件になっていること）。
        bool bUnexposedAboveHalfMax = false;
        for (const uint32_t hitPixel : {0u, 2u})
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const size_t index = hitPixel * 4u + channel;
                const float unitValue = HalfToFloat(unitExposure.RTGIPixels[index]);
                const float scaledValue = HalfToFloat(scaledExposure.RTGIPixels[index]);
                bExposureLit = bExposureLit && unitValue > 0.0f;
                bUnexposedAboveHalfMax = bUnexposedAboveHalfMax ||
                                         scaledValue * ExposureScale > 65504.0f;
                // 両方が有限で、差は半精度の丸め2段分まで許す。
                bExposureInvariant = bExposureInvariant && std::isfinite(unitValue) &&
                                     std::isfinite(scaledValue) &&
                                     std::abs(scaledValue - unitValue) <=
                                         2.0e-3f * std::max(unitValue, scaledValue);
                std::cout << "rtgi_exposure_invariance pixel=" << hitPixel
                          << " channel=" << channel << " unit=" << unitValue
                          << " scaled=" << scaledValue
                          << " scaled_unexposed=" << scaledValue * ExposureScale << "\n";
            }
        }
        if (!bExposureLit || !bExposureInvariant || !bUnexposedAboveHalfMax)
        {
            std::cerr << "RTGIの出力が発光の強さとpre-exposureの積だけで決まりません lit="
                      << bExposureLit << " invariant=" << bExposureInvariant
                      << " unexposed_above_half_max=" << bUnexposedAboveHalfMax << "\n";
            return 1;
        }

        // 静止が続くと画素ごとの履歴の年齢の上限が8から上がり、何かが動いたフレームで8へ戻る。
        // 連続しないフレーム番号から始めると最初のフレームは履歴なしで、2フレーム目から静止を数える。
        // 静止がRTGIHistoryStaticWarmupFrames（16）を超えると上限は1 frameに1ずつ上がるため、
        // 27フレーム目（静止26）の上限は8+10=18で、履歴が毎フレーム続いた画素の年齢も18になる。
        constexpr uint64_t StaticFirstFrame = 200u;
        constexpr uint32_t StaticFrameCount = 27u;
        const auto readHistoryAges = [&](VariableArray<float>& outAges) -> bool
        {
            ResourceState ageState = ResourceState::Undefined;
            const TexturePtr ageTexture =
                RTGIDiffuseIndirectVulkanTestAccess::GetWrittenHistoryAge(lightingPass, ageState);
            const uint64_t ageBytes = static_cast<uint64_t>(TestWidth) * TestHeight * sizeof(uint16_t);
            BufferDesc ageReadbackDesc(ageBytes, ResourceUsage::TransferDst, true,
                                       "RTGIDiffuseIndirectVulkanTest.AgeReadback");
            BufferPtr ageReadback = device->CreateBuffer(ageReadbackDesc);
            CommandListPtr ageCommandList = device->CreateCommandList();
            TSharedPtr<Vulkan::VulkanCommandList> vulkanAgeCommandList =
                DynamicPointerCast<Vulkan::VulkanCommandList>(ageCommandList);
            if (!ageTexture || !ageReadback || !ageCommandList || !vulkanAgeCommandList ||
                ageTexture->GetFormat() != RTGIHistoryAgeFormat)
            {
                return false;
            }
            ageCommandList->SetFrameIndex(0u);
            ageCommandList->Begin();
            ageCommandList->TextureBarrier(ageTexture, ageState, ResourceState::CopySource);
            ageCommandList->BufferBarrier(ageReadback, ResourceState::Undefined,
                                          ResourceState::CopyDest, 0u, ageBytes);
            ageCommandList->CopyTextureToBuffer(ageTexture, ageReadback, TestWidth, TestHeight, 0u);
            ageCommandList->TextureBarrier(ageTexture, ResourceState::CopySource, ageState);
            const bool bBarrier = RecordHostReadBarrier(vulkanAgeCommandList, ageReadback);
            ageCommandList->End();
            if (!bBarrier)
            {
                return false;
            }
            ageCommandList->Submit(true);
            const void* mappedAges = ageReadback->Map(0u, ageBytes);
            if (!mappedAges)
            {
                return false;
            }
            uint16_t halfAges[TestWidth * TestHeight] = {};
            std::memcpy(halfAges, mappedAges, sizeof(halfAges));
            ageReadback->Unmap();
            outAges.resize(TestWidth * TestHeight);
            for (uint32_t index = 0u; index < TestWidth * TestHeight; ++index)
            {
                outAges[index] = HalfToFloat(halfAges[index]);
            }
            return true;
        };
        // デノイズ後の値が、画素自身の時間方向の値から同じ面の近傍（画素2）へどれだけ寄るか。年齢が
        // 伸びた画素では近傍の重みが下がり、寄り方が小さくなる。
        const auto readDenoised = [&](VariableArray<uint16_t>& outPixels) -> bool
        {
            const TexturePtr denoised = RTGIDiffuseIndirectVulkanTestAccess::GetDenoisedTexture(lightingPass);
            const uint64_t denoisedBytes = static_cast<uint64_t>(TestWidth) * TestHeight * BytesPerPixel;
            BufferDesc denoisedReadbackDesc(denoisedBytes, ResourceUsage::TransferDst, true,
                                            "RTGIDiffuseIndirectVulkanTest.DenoisedReadback");
            BufferPtr denoisedReadback = device->CreateBuffer(denoisedReadbackDesc);
            CommandListPtr denoisedCommandList = device->CreateCommandList();
            TSharedPtr<Vulkan::VulkanCommandList> vulkanDenoisedCommandList =
                DynamicPointerCast<Vulkan::VulkanCommandList>(denoisedCommandList);
            if (!denoised || !denoisedReadback || !denoisedCommandList || !vulkanDenoisedCommandList)
            {
                return false;
            }
            denoisedCommandList->SetFrameIndex(0u);
            denoisedCommandList->Begin();
            const bool bRecorded = RecordTextureReadback(denoisedCommandList, vulkanDenoisedCommandList,
                                                         denoised, denoisedReadback);
            denoisedCommandList->End();
            if (!bRecorded)
            {
                return false;
            }
            denoisedCommandList->Submit(true);
            const void* mappedDenoised = denoisedReadback->Map(0u, denoisedBytes);
            if (!mappedDenoised)
            {
                return false;
            }
            outPixels.resize(TestWidth * TestHeight * 4u);
            std::memcpy(outPixels.data(), mappedDenoised, static_cast<size_t>(denoisedBytes));
            denoisedReadback->Unmap();
            return true;
        };
        const auto neighborPull = [](const VariableArray<uint16_t>& temporal,
                                     const VariableArray<uint16_t>& denoised) -> double
        {
            const double center = HalfToFloat(temporal[0u]);
            const double neighbor = HalfToFloat(temporal[2u * 4u]);
            const double result = HalfToFloat(denoised[0u]);
            return std::abs(neighbor - center) > 1.0e-6 ? (result - center) / (neighbor - center) : -1.0;
        };
        // 年齢8（上限が上がる直前）と年齢18（最後）のフレームで比べる。
        constexpr uint32_t ShortHistoryFrame = RTGIHistoryStaticWarmupFrames;
        double shortHistoryPull = -1.0;
        double longHistoryPull = -1.0;
        for (uint32_t frame = 0u; frame < StaticFrameCount; ++frame)
        {
            LightingFrameObservation staticFrame;
            if (!RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                                  context, emitterOnlyPacket.RayTracingScene, emitterOnlyBuild,
                                  gbuffer, rtgiOutput, true, true, true, StaticFirstFrame + frame,
                                  staticFrame) ||
                !staticFrame.bPublished)
            {
                std::cerr << "静止の続くRTGIを描けませんでした frame=" << frame << "\n";
                return 1;
            }
            const uint32_t staticFrames = frame;
            const uint32_t expectedCap =
                staticFrames <= RTGIHistoryStaticWarmupFrames
                    ? RTGIHistoryMaximumAge
                    : std::min(RTGIHistoryMaximumAge + staticFrames - RTGIHistoryStaticWarmupFrames,
                               RTGIHistoryStaticMaximumAge);
            if (RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass) != expectedCap)
            {
                std::cerr << "静止フレームの年齢の上限が期待値と一致しません frame=" << frame
                          << " cap=" << RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass)
                          << " expected=" << expectedCap << "\n";
                return 1;
            }
            if (frame == ShortHistoryFrame || frame + 1u == StaticFrameCount)
            {
                VariableArray<uint16_t> denoisedPixels;
                if (!readDenoised(denoisedPixels))
                {
                    std::cerr << "RTGIのデノイズ結果を読み戻せませんでした\n";
                    return 1;
                }
                (frame == ShortHistoryFrame ? shortHistoryPull : longHistoryPull) =
                    neighborPull(staticFrame.RTGIPixels, denoisedPixels);
            }
        }
        std::cout << "rtgi_denoise_neighbor_pull age8=" << shortHistoryPull
                  << " age18=" << longHistoryPull << "\n";
        // 近傍の重みは年齢18で8/18になり、寄り方は小さくなる（どちらも近傍側へ0〜1の割合で寄る）。
        if (!(shortHistoryPull > 0.0 && shortHistoryPull <= 1.0) ||
            !(longHistoryPull >= 0.0 && longHistoryPull < 0.8 * shortHistoryPull))
        {
            std::cerr << "静止が続いた画素でデノイズの近傍の重みが下がりません\n";
            return 1;
        }
        VariableArray<float> staticAges;
        if (!readHistoryAges(staticAges))
        {
            std::cerr << "RTGIの履歴の年齢を読み戻せませんでした\n";
            return 1;
        }
        constexpr float ExpectedStaticAge = 18.0f;
        std::cout << "rtgi_static_accumulation cap="
                  << RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass)
                  << " age_pixel0=" << staticAges[0] << " age_pixel2=" << staticAges[2] << "\n";
        if (staticAges[0] != ExpectedStaticAge || staticAges[2] != ExpectedStaticAge)
        {
            std::cerr << "静止が続いた画素の履歴の年齢が上限まで伸びません\n";
            return 1;
        }

        // 発光面のinstanceを動かしたフレームは静止ではなく、上限と画素ごとの年齢は8へ戻る。
        emitterOnlyPacket.RayTracingScene.Instances[0].Instance.transform[3] += 0.25f;
        emitterOnlyBuild.instances[0].transform[3] += 0.25f;
        LightingFrameObservation movedFrame;
        VariableArray<float> movedAges;
        const bool bMovedRan = RunLightingFrame(device, capabilities, rtgiCapability, lightingPass,
                                                renderer, context, emitterOnlyPacket.RayTracingScene,
                                                emitterOnlyBuild, gbuffer, rtgiOutput, true, true, true,
                                                StaticFirstFrame + StaticFrameCount, movedFrame) &&
                               movedFrame.bPublished && readHistoryAges(movedAges);
        emitterOnlyPacket.RayTracingScene.Instances[0].Instance.transform[3] -= 0.25f;
        emitterOnlyBuild.instances[0].transform[3] -= 0.25f;
        if (!bMovedRan)
        {
            std::cerr << "instanceを動かしたRTGIを描けませんでした\n";
            return 1;
        }
        std::cout << "rtgi_dynamic_frame cap="
                  << RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass)
                  << " age_pixel0=" << movedAges[0] << " age_pixel2=" << movedAges[2] << "\n";
        if (RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass) != RTGIHistoryMaximumAge ||
            movedAges[0] != static_cast<float>(RTGIHistoryMaximumAge) ||
            movedAges[2] != static_cast<float>(RTGIHistoryMaximumAge))
        {
            std::cerr << "動いたフレームで履歴の年齢の上限が8へ戻りません\n";
            return 1;
        }

        // 材質（instance色）とpre-exposureの変更も静止ではない。20静止フレーム（静止19、上限11）の後に
        // 変えたフレームで上限は8へ戻る。
        const auto runStaticThenChange = [&](uint64_t firstFrame, const char* label,
                                             const auto& change, const auto& restore) -> bool
        {
            constexpr uint32_t WarmFrames = 20u;
            for (uint32_t frame = 0u; frame < WarmFrames; ++frame)
            {
                LightingFrameObservation warmFrame;
                if (!RunLightingFrame(device, capabilities, rtgiCapability, lightingPass, renderer,
                                      context, emitterOnlyPacket.RayTracingScene, emitterOnlyBuild,
                                      gbuffer, rtgiOutput, true, true, true, firstFrame + frame,
                                      warmFrame) ||
                    !warmFrame.bPublished)
                {
                    return false;
                }
            }
            const uint32_t warmCap = RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass);
            change();
            LightingFrameObservation changedFrame;
            const bool bRan = RunLightingFrame(device, capabilities, rtgiCapability, lightingPass,
                                               renderer, context, emitterOnlyPacket.RayTracingScene,
                                               emitterOnlyBuild, gbuffer, rtgiOutput, true, true, true,
                                               firstFrame + WarmFrames, changedFrame) &&
                              changedFrame.bPublished;
            const uint32_t changedCap = RTGIDiffuseIndirectVulkanTestAccess::GetHistoryAgeCap(lightingPass);
            restore();
            std::cout << "rtgi_static_break " << label << " warm_cap=" << warmCap
                      << " changed_cap=" << changedCap << "\n";
            return bRan && warmCap == RTGIHistoryMaximumAge + 3u && changedCap == RTGIHistoryMaximumAge;
        };
        float& emitterObjectRed = emitterOnlyPacket.RayTracingScene.Instances[0].Material.ObjectColor[0];
        const float originalObjectRed = emitterObjectRed;
        if (!runStaticThenChange(
                300u, "material",
                [&]() { emitterObjectRed = originalObjectRed * 0.5f; },
                [&]() { emitterObjectRed = originalObjectRed; }))
        {
            std::cerr << "材質の変更で静止の履歴延長が解除されません\n";
            return 1;
        }
        exposureCamera.PreExposure = 1.0f;
        exposureCamera.InvPreExposure = 1.0f;
        context.MainCamera = &exposureCamera;
        const bool bExposureBreaks = runStaticThenChange(
            400u, "pre_exposure",
            [&]()
            {
                exposureCamera.PreExposure = 0.5f;
                exposureCamera.InvPreExposure = 2.0f;
            },
            [&]()
            {
                exposureCamera.PreExposure = 1.0f;
                exposureCamera.InvPreExposure = 1.0f;
            });
        context.MainCamera = nullptr;
        if (!bExposureBreaks)
        {
            std::cerr << "露出の変更で静止の履歴延長が解除されません\n";
            return 1;
        }

        // 同じフレームに照明を複数回 Execute する（同じ SceneView の複数のビューポート）
        if (!RunMultiViewportResourceSetTest(device, capabilities, rtgiCapability, lightingPass, renderer, context, gbuffer))
        {
            std::cerr << "同じフレームの複数の Execute が Execute ごとの組を使っていません\n";
            return 1;
        }
        std::cout << "lighting_execute_sets_per_viewport=true bound_descriptor_sets_distinct=true "
                     "set_buffers_keep_own_viewport_values=true over_limit_execute_not_drawn=true\n";

        std::cout << "rtgi_capability_usable=true tlas_complete=true output_format=R16G16B16A16_FLOAT\n";
        std::cout << "rtgi_hit_miss_readback=finite hit_positive=true miss_zero=true\n";
        std::cout << "rtgi_published=true source=RTGI fallback_disabled=Raster fallback_incomplete_tlas=Raster\n";
        std::cout << "scene_color_rtgi_differs_from_fallback=true disabled_and_incomplete_equal=true\n";
        std::cout << "rtgi_non_shadow_caster_only_scene_falls_back=true "
                     "rtgi_non_shadow_caster_instance_ignored=true\n";
        std::cout << "rtgi_emitter_near_occluder_blocks=true "
                     "rtgi_output_depends_on_emission_times_pre_exposure=true\n";
        std::cout << "rtgi_static_history_extends=true rtgi_dynamic_history_cap=8 "
                     "rtgi_denoise_spatial_weight_follows_age=true "
                     "rtgi_material_and_exposure_changes_end_static=true\n";

        lightingPass.Shutdown();
        renderer.Shutdown();
        shaderManager.Shutdown();
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
        std::cerr << "RTGIDiffuseIndirectVulkanTest threw an exception: "
                  << exception.what() << '\n';
        return 1;
    }
}
