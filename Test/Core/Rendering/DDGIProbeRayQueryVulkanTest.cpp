// LightingPassに接続したDDGI probe ray queryと描画fallbackをGPUで検証する。
#include "Rendering/DDGIVolume.h"
#include "Rendering/FramePacket.h"
#include "Rendering/LightingPass.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/DeviceCapabilities.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    struct DDGIProbeRayQueryVulkanTestAccess
    {
        static RHI::BufferPtr GetResultBuffer(const LightingPass& pass, uint32_t frameIndex)
        {
            return pass.m_DDGIProbePass.GetResultBuffer(frameIndex, 0u, 0u);
        }

        static uint32_t GetResultCount(const LightingPass& pass, uint32_t frameIndex)
        {
            return pass.m_DDGIProbePass.GetResultCount(frameIndex, 0u, 0u);
        }

        static RHI::TexturePtr GetSceneColorTexture(const LightingPass& pass)
        {
            return pass.m_SceneColorTexture;
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

    constexpr const char* TestName = "DDGIProbeRayQueryVulkanTest";
    constexpr uint32_t ExpectedInstanceCustomIndex = 17u;
    constexpr uint32_t ExpectedHitDirectionIndex = DDGIProbeRayDirectionCount - 1u;
    constexpr uint32_t TestWidth = 2u;
    constexpr uint32_t TestHeight = 2u;
    constexpr uint32_t SceneColorBytesPerPixel = 8u;

    enum class FailurePoint : uint8_t
    {
        ResultBuffer,
        ComputePipeline,
    };

    struct Vertex
    {
        float Position[3];
    };

    class FailingDDGIResourceDevice final : public IDevice
    {
    public:
        FailingDDGIResourceDevice(IDevice* device, FailurePoint failurePoint)
            : m_Device(device),
              m_FailurePoint(failurePoint)
        {
        }

        BufferPtr CreateBuffer(const BufferDesc& desc) override
        {
            if (m_FailurePoint == FailurePoint::ResultBuffer &&
                desc.DebugName != nullptr &&
                std::strcmp(desc.DebugName, "DDGIProbeRayQuery.Results") == 0)
            {
                ++m_FailureAttemptCount;
                if (!m_bFailureInjected)
                {
                    m_bFailureInjected = true;
                    throw std::runtime_error("injected DDGI result-buffer allocation failure");
                }
            }
            return m_Device->CreateBuffer(desc);
        }

        AccelerationStructurePtr CreateAccelerationStructure(
            const AccelerationStructureDesc& desc) override
        {
            return m_Device->CreateAccelerationStructure(desc);
        }

        TexturePtr CreateTexture(const TextureDesc& desc) override
        {
            return m_Device->CreateTexture(desc);
        }

        SamplerPtr CreateSampler(const SamplerDesc& desc) override
        {
            return m_Device->CreateSampler(desc);
        }

        ShaderPtr CreateShader(const ShaderDesc& desc) override
        {
            return m_Device->CreateShader(desc);
        }

        CommandListPtr CreateCommandList() override
        {
            return m_Device->CreateCommandList();
        }

        SwapChainPtr CreateSwapChain(const SwapChainDesc& desc) override
        {
            return m_Device->CreateSwapChain(desc);
        }

        RenderPassPtr CreateRenderPass(const RenderPassDesc& desc) override
        {
            return m_Device->CreateRenderPass(desc);
        }

        FramebufferPtr CreateFramebuffer(const FramebufferDesc& desc) override
        {
            return m_Device->CreateFramebuffer(desc);
        }

        PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override
        {
            return m_Device->CreateGraphicsPipeline(desc);
        }

        PipelinePtr CreateComputePipeline(const ComputePipelineDesc& desc) override
        {
            if (m_FailurePoint == FailurePoint::ComputePipeline)
            {
                ++m_FailureAttemptCount;
                if (!m_bFailureInjected)
                {
                    m_bFailureInjected = true;
                    throw std::runtime_error("injected DDGI compute-pipeline creation failure");
                }
            }
            return m_Device->CreateComputePipeline(desc);
        }

        PipelinePtr CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) override
        {
            return m_Device->CreateRayTracingPipeline(desc);
        }

        DescriptorSetPtr CreateDescriptorSet(const DescriptorSetDesc& desc) override
        {
            return m_Device->CreateDescriptorSet(desc);
        }

        ShaderCompilerPtr CreateShaderCompiler() override
        {
            return m_Device->CreateShaderCompiler();
        }

        ShaderCompilerPtr CreateSlangShaderCompiler() override
        {
            return m_Device->CreateSlangShaderCompiler();
        }

        IGPUResourceAllocator* GetResourceAllocator() override
        {
            return m_Device->GetResourceAllocator();
        }

        void WaitIdle() override
        {
            m_Device->WaitIdle();
        }

        API GetAPI() const override
        {
            return m_Device->GetAPI();
        }

        const NorvesLib::RHI::DeviceCapabilitiesA& GetCapabilities() const override
        {
            return m_Device->GetCapabilities();
        }

        Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            return m_Device->AdjustProjectionForClipSpace(projection, bApplyYFlip);
        }

        bool DidInjectFailure() const
        {
            return m_bFailureInjected;
        }

        uint32_t GetFailureAttemptCount() const
        {
            return m_FailureAttemptCount;
        }

    private:
        IDevice* m_Device = nullptr;
        FailurePoint m_FailurePoint = FailurePoint::ResultBuffer;
        bool m_bFailureInjected = false;
        uint32_t m_FailureAttemptCount = 0u;
    };

    struct LightingFrameObservation
    {
        BufferPtr ProbeResultBuffer;
        VariableArray<DDGIProbeRayQueryResult> ProbeResults;
        VariableArray<uint8_t> SceneColor;
        bool bSentinelIntact = false;
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

    bool BuildTestTopLevel(
        const DevicePtr& device,
        AccelerationStructurePtr& outTopLevel,
        AccelerationStructurePtr& outBottomLevel,
        BufferPtr& outVertexBuffer,
        BufferPtr& outIndexBuffer,
        AccelerationStructureBuildDesc& outTopLevelBuild)
    {
        const Vertex vertices[] = {
            {{-100.0f, -100.0f, 0.0f}},
            {{100.0f, -100.0f, 0.0f}},
            {{0.0f, 100.0f, 0.0f}},
        };
        const uint32_t indices[] = {0u, 1u, 2u};

        BufferDesc vertexDesc;
        vertexDesc.Size = sizeof(vertices);
        vertexDesc.Usage = ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress;
        vertexDesc.CPUAccessible = true;
        vertexDesc.DebugName = "DDGIProbeRayQueryTest.Vertices";
        outVertexBuffer = device->CreateBuffer(vertexDesc);

        BufferDesc indexDesc;
        indexDesc.Size = sizeof(indices);
        indexDesc.Usage = ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress;
        indexDesc.CPUAccessible = true;
        indexDesc.DebugName = "DDGIProbeRayQueryTest.Indices";
        outIndexBuffer = device->CreateBuffer(indexDesc);
        if (!outVertexBuffer || !outIndexBuffer ||
            outVertexBuffer->GetDeviceAddress() == 0u || outIndexBuffer->GetDeviceAddress() == 0u)
        {
            std::cerr << "TLAS用のdevice-address bufferを作成できませんでした\n";
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
            std::cerr << "probe ray用のBLASを作成できませんでした\n";
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
            std::cerr << "probe ray用のBLASを構築できませんでした\n";
            return false;
        }

        AccelerationStructureDesc topLevelDesc;
        topLevelDesc.type = AccelerationStructureType::TopLevel;
        topLevelDesc.maxInstanceCount = 1u;
        outTopLevel = device->CreateAccelerationStructure(topLevelDesc);
        if (!outTopLevel)
        {
            std::cerr << "probe ray用のTLASを作成できませんでした\n";
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

    TexturePtr CreateSampledTexture(const DevicePtr& device,
                                    Format format,
                                    const char* name)
    {
        TextureDesc desc;
        desc.Width = TestWidth;
        desc.Height = TestHeight;
        desc.TextureFormat = format;
        desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        desc.DebugName = name;
        return device->CreateTexture(desc);
    }

    bool CreateTestGBuffer(const DevicePtr& device,
                           SharedResourceRegistry& registry,
                           TexturePtr& outAlbedo,
                           TexturePtr& outNormal,
                           TexturePtr& outMaterial,
                           TexturePtr& outDepth,
                           TexturePtr& outEmissive)
    {
        outAlbedo = CreateSampledTexture(device, Format::R8G8B8A8_UNORM,
                                         "DDGIProbeRayQueryTest.Albedo");
        outNormal = CreateSampledTexture(device, Format::R16G16B16A16_FLOAT,
                                         "DDGIProbeRayQueryTest.Normal");
        outMaterial = CreateSampledTexture(device, Format::R8G8B8A8_UNORM,
                                           "DDGIProbeRayQueryTest.Material");
        outDepth = CreateSampledTexture(device, Format::R32_FLOAT,
                                        "DDGIProbeRayQueryTest.Depth");
        outEmissive = CreateSampledTexture(device, Format::R16G16B16A16_FLOAT,
                                           "DDGIProbeRayQueryTest.Emissive");
        if (!outAlbedo || !outNormal || !outMaterial || !outDepth || !outEmissive)
        {
            std::cerr << "LightingPass用GBufferを作成できませんでした\n";
            return false;
        }

        const uint8_t albedoPixels[TestWidth * TestHeight * 4u] = {
            150u, 120u, 90u, 255u, 150u, 120u, 90u, 255u,
            150u, 120u, 90u, 255u, 150u, 120u, 90u, 255u,
        };
        const uint8_t materialPixels[TestWidth * TestHeight * 4u] = {
            0u, 204u, 255u, 255u, 0u, 204u, 255u, 255u,
            0u, 204u, 255u, 255u, 0u, 204u, 255u, 255u,
        };
        const uint16_t normalPixels[TestWidth * TestHeight * 4u] = {
            0u, 0u, 0x3c00u, 0x3c00u, 0u, 0u, 0x3c00u, 0x3c00u,
            0u, 0u, 0x3c00u, 0x3c00u, 0u, 0u, 0x3c00u, 0x3c00u,
        };
        const uint16_t emissivePixels[TestWidth * TestHeight * 4u] = {};
        const float depthPixels[TestWidth * TestHeight] = {1.0f, 1.0f, 1.0f, 1.0f};
        outAlbedo->Update(albedoPixels, TestWidth * 4u, sizeof(albedoPixels));
        outNormal->Update(normalPixels, TestWidth * 8u, sizeof(normalPixels));
        outMaterial->Update(materialPixels, TestWidth * 4u, sizeof(materialPixels));
        outDepth->Update(depthPixels, TestWidth * sizeof(float), sizeof(depthPixels));
        outEmissive->Update(emissivePixels, TestWidth * 8u, sizeof(emissivePixels));

        registry.RegisterTexturePtr("GBuffer_Albedo", outAlbedo);
        registry.RegisterTexturePtr("GBuffer_Normal", outNormal);
        registry.RegisterTexturePtr("GBuffer_Material", outMaterial);
        registry.RegisterTexturePtr("GBuffer_Depth", outDepth);
        registry.RegisterTexturePtr("GBuffer_Emissive", outEmissive);
        return true;
    }

    bool RunLightingFrame(const DevicePtr& device,
                          IDevice* contextDevice,
                          LightingPass& lightingPass,
                          SceneRenderer& renderer,
                          ViewRenderContext& context,
                          const AccelerationStructureBuildDesc& topLevelBuild,
                          uint32_t frameIndex,
                          uint64_t frameNumber,
                          bool bBuildTopLevel,
                          bool bCaptureProbeResults,
                          bool bSeedProbeSentinel,
                          LightingFrameObservation& outObservation)
    {
        RHI::TexturePtr sceneColor =
            DDGIProbeRayQueryVulkanTestAccess::GetSceneColorTexture(lightingPass);
        if (!sceneColor || contextDevice == nullptr)
        {
            std::cerr << "LightingPassの出力textureまたはdeviceがありません\n";
            return false;
        }

        const uint64_t sceneColorSize =
            static_cast<uint64_t>(sceneColor->GetWidth()) * sceneColor->GetHeight() *
            SceneColorBytesPerPixel;
        BufferDesc sceneColorReadbackDesc(
            sceneColorSize, ResourceUsage::TransferDst, true,
            "DDGIProbeRayQueryTest.SceneColorReadback");
        BufferPtr sceneColorReadback = device->CreateBuffer(sceneColorReadbackDesc);
        if (!sceneColorReadback)
        {
            std::cerr << "SceneColor readback bufferを作成できませんでした\n";
            return false;
        }

        const uint64_t probeResultSize =
            static_cast<uint64_t>(DDGIProbeRayDirectionCount) *
            sizeof(DDGIProbeRayQueryResult);
        BufferPtr probeResultReadback;
        if (bCaptureProbeResults || bSeedProbeSentinel)
        {
            BufferDesc probeReadbackDesc(
                probeResultSize, ResourceUsage::TransferDst, true,
                "DDGIProbeRayQueryTest.ProbeReadback");
            probeResultReadback = device->CreateBuffer(probeReadbackDesc);
            if (!probeResultReadback)
            {
                std::cerr << "probe readback bufferを作成できませんでした\n";
                return false;
            }
        }

        BufferPtr probeResultBuffer;
        if (bSeedProbeSentinel)
        {
            probeResultBuffer = DDGIProbeRayQueryVulkanTestAccess::GetResultBuffer(
                lightingPass, frameIndex);
            if (!probeResultBuffer ||
                DDGIProbeRayQueryVulkanTestAccess::GetResultCount(lightingPass, frameIndex) !=
                    DDGIProbeRayDirectionCount)
            {
                std::cerr << "disabled経路の既存probe bufferを取得できません\n";
                return false;
            }
        }

        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "LightingPass用command listを作成できませんでした\n";
            return false;
        }
        TSharedPtr<Vulkan::VulkanCommandList> vulkanCommandList =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        if (!vulkanCommandList)
        {
            std::cerr << "Vulkan command listへ変換できません\n";
            return false;
        }

        context.Device = contextDevice;
        context.CommandList = commandList.get();
        context.FrameIndex = frameIndex;
        context.FrameNumber = frameNumber;
        context.PhysicalLighting.Begin(frameNumber, 0u, 0u);
        commandList->SetFrameIndex(frameIndex);
        renderer.ResetStats();
        commandList->Begin();

        bool bFrameRecorded = true;
        if (bBuildTopLevel && !commandList->BuildAccelerationStructure(topLevelBuild))
        {
            std::cerr << "同一command listへのTLAS buildを記録できませんでした\n";
            bFrameRecorded = false;
        }

        if (bFrameRecorded && bSeedProbeSentinel)
        {
            commandList->BufferBarrier(
                probeResultBuffer,
                ResourceState::ShaderResource,
                ResourceState::CopyDest,
                0u,
                probeResultSize);
            commandList->FillBuffer(
                probeResultBuffer, 0u, probeResultSize, UINT32_MAX);
            commandList->BufferBarrier(
                probeResultBuffer,
                ResourceState::CopyDest,
                ResourceState::ShaderResource,
                0u,
                probeResultSize);
        }

        if (bFrameRecorded)
        {
            lightingPass.Execute(context);
        }
        outObservation.DrawCallCount = renderer.GetStats().DrawCallCount;
        if (!bFrameRecorded || outObservation.DrawCallCount != 1u)
        {
            std::cerr << "DDGI結果にかかわらずLightingPassのfullscreen drawを維持できません"
                      << " draw_count=" << outObservation.DrawCallCount << '\n';
            bFrameRecorded = false;
        }

        if (bCaptureProbeResults || bSeedProbeSentinel)
        {
            if (!bSeedProbeSentinel)
            {
                probeResultBuffer = DDGIProbeRayQueryVulkanTestAccess::GetResultBuffer(
                    lightingPass, frameIndex);
            }
            if (!probeResultBuffer)
            {
                std::cerr << "LightingPass経由でprobe result bufferが作成されませんでした\n";
                bFrameRecorded = false;
            }
            else
            {
                commandList->BufferBarrier(
                    probeResultBuffer,
                    ResourceState::ShaderResource,
                    ResourceState::CopySource,
                    0u,
                    probeResultSize);
                commandList->BufferBarrier(
                    probeResultReadback,
                    ResourceState::Undefined,
                    ResourceState::CopyDest,
                    0u,
                    probeResultSize);
                commandList->CopyBuffer(
                    probeResultBuffer, probeResultReadback, probeResultSize);
                commandList->BufferBarrier(
                    probeResultBuffer,
                    ResourceState::CopySource,
                    ResourceState::ShaderResource,
                    0u,
                    probeResultSize);
                bFrameRecorded = RecordHostReadBarrier(
                                     vulkanCommandList, probeResultReadback) &&
                                 bFrameRecorded;
            }
        }

        commandList->TextureBarrier(
            sceneColor, ResourceState::ShaderResource, ResourceState::CopySource);
        commandList->BufferBarrier(
            sceneColorReadback,
            ResourceState::Undefined,
            ResourceState::CopyDest,
            0u,
            sceneColorSize);
        commandList->CopyTextureToBuffer(
            sceneColor, sceneColorReadback, sceneColor->GetWidth(), sceneColor->GetHeight(), 0u);
        commandList->TextureBarrier(
            sceneColor, ResourceState::CopySource, ResourceState::ShaderResource);
        bFrameRecorded = RecordHostReadBarrier(
                             vulkanCommandList, sceneColorReadback) &&
                         bFrameRecorded;
        commandList->End();
        commandList->Submit(true);

        const void* mappedSceneColor = sceneColorReadback->Map(0u, sceneColorSize);
        if (mappedSceneColor == nullptr)
        {
            std::cerr << "SceneColor readbackをmapできませんでした\n";
            return false;
        }
        outObservation.SceneColor.resize(static_cast<size_t>(sceneColorSize));
        std::memcpy(outObservation.SceneColor.data(), mappedSceneColor,
                    static_cast<size_t>(sceneColorSize));
        sceneColorReadback->Unmap();

        if (bCaptureProbeResults || bSeedProbeSentinel)
        {
            const void* mappedProbeResults =
                probeResultReadback->Map(0u, probeResultSize);
            if (mappedProbeResults == nullptr)
            {
                std::cerr << "probe result readbackをmapできませんでした\n";
                return false;
            }
            if (bSeedProbeSentinel)
            {
                const auto* words = static_cast<const uint32_t*>(mappedProbeResults);
                outObservation.bSentinelIntact = true;
                for (uint32_t index = 0u;
                     index < probeResultSize / sizeof(uint32_t);
                     ++index)
                {
                    outObservation.bSentinelIntact =
                        outObservation.bSentinelIntact && words[index] == UINT32_MAX;
                }
            }
            else
            {
                outObservation.ProbeResults.resize(DDGIProbeRayDirectionCount);
                std::memcpy(outObservation.ProbeResults.data(), mappedProbeResults,
                            static_cast<size_t>(probeResultSize));
            }
            probeResultReadback->Unmap();
        }
        outObservation.ProbeResultBuffer = probeResultBuffer;
        return bFrameRecorded;
    }

    bool AreSceneColorsEqual(const LightingFrameObservation& lhs,
                             const LightingFrameObservation& rhs);

    bool ValidateResourceFailureFallback(const DevicePtr& device,
                                         FailingDDGIResourceDevice& failingDevice,
                                         LightingPass& lightingPass,
                                         SceneRenderer& renderer,
                                         ViewRenderContext& context,
                                         const AccelerationStructureBuildDesc& topLevelBuild,
                                         FailurePoint failurePoint,
                                         uint32_t frameIndex,
                                         uint64_t firstFrameNumber,
                                         const LightingFrameObservation& reference)
    {
        for (uint32_t attempt = 0u; attempt < 2u; ++attempt)
        {
            LightingFrameObservation observation;
            if (!RunLightingFrame(device,
                                  &failingDevice,
                                  lightingPass,
                                  renderer,
                                  context,
                                  topLevelBuild,
                                  frameIndex,
                                  firstFrameNumber + attempt,
                                  attempt == 0u,
                                  false,
                                  false,
                                  observation) ||
                !failingDevice.DidInjectFailure() ||
                failingDevice.GetFailureAttemptCount() != 1u ||
                DDGIProbeRayQueryVulkanTestAccess::GetResultCount(
                    lightingPass, frameIndex) != 0u ||
                observation.DrawCallCount != 1u ||
                !AreSceneColorsEqual(reference, observation))
            {
                std::cerr << "DDGI資源例外後に更新を停止して既存描画を保てません"
                          << " attempt=" << attempt
                          << " injected=" << failingDevice.DidInjectFailure()
                          << " creation_attempts=" << failingDevice.GetFailureAttemptCount()
                          << " draw_count=" << observation.DrawCallCount << '\n';
                return false;
            }
        }

        const char* failureName = failurePoint == FailurePoint::ComputePipeline
                                      ? "compute_pipeline"
                                      : "result_buffer";
        std::cout << failureName
                  << "_exception_caught=true next_frame_ddgi_disabled=true"
                  << " raster_draw_preserved=true scene_color_equal=true\n";
        return true;
    }

    bool ValidateProbeHitAndMiss(const LightingFrameObservation& observation,
                                  const DDGIVolumeParameters& volume)
    {
        if (observation.ProbeResults.size() != DDGIProbeRayDirectionCount)
        {
            return false;
        }

        const DDGIProbeRayQueryResult& hitResult =
            observation.ProbeResults[ExpectedHitDirectionIndex];
        const DDGIProbeRayQueryResult& missResult = observation.ProbeResults[0u];
        Math::Vector3 expectedHitDirection;
        TryGetDDGIProbeRayDirection(ExpectedHitDirectionIndex, expectedHitDirection);
        const float expectedHitDistance = -volume.Origin.z / expectedHitDirection.z;
        return hitResult.bHit == 1u &&
               std::abs(hitResult.Distance - expectedHitDistance) < 0.01f &&
               hitResult.InstanceCustomIndex == ExpectedInstanceCustomIndex &&
               hitResult.PrimitiveIndex == 0u &&
               missResult.bHit == 0u && missResult.Distance == -1.0f &&
               missResult.InstanceCustomIndex == UINT32_MAX &&
               missResult.PrimitiveIndex == UINT32_MAX;
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

        const NorvesLib::RHI::DeviceCapabilitiesA& capabilities = device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery)
        {
            return ReportGpuTestSkip(TestName, "GPU ray query機能を利用できません");
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << "probe ray shader用のShaderManagerを初期化できませんでした\n";
            return 1;
        }

        FramePacket packet;
        DDGIVolumeParameters volume = MakeDefaultDDGIVolumeParameters();
        volume.bEnabled = true;
        volume.Origin = Math::Vector3(0.0f, 0.0f, 2.0f);
        volume.ProbeSpacing = Math::Vector3::One;
        volume.ProbeCountX = 1u;
        volume.ProbeCountY = 1u;
        volume.ProbeCountZ = 1u;
        packet.Scene.SetDDGIVolumeParameters(volume);
        packet.FrameNumber = 1u;

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
        packet.RayTracingScene.TopLevel = topLevel;
        RayTracingSceneInstanceSnapshot instanceSnapshot;
        instanceSnapshot.BottomLevel = bottomLevel;
        instanceSnapshot.AccelerationStructureVertexBuffer = vertexBuffer;
        instanceSnapshot.AccelerationStructureIndexBuffer = indexBuffer;
        instanceSnapshot.VertexCount = 3u;
        instanceSnapshot.VertexStride = sizeof(Vertex);
        instanceSnapshot.IndexCount = 3u;
        instanceSnapshot.Instance.bottomLevel = bottomLevel;
        instanceSnapshot.Instance.customIndex = ExpectedInstanceCustomIndex;
        instanceSnapshot.Instance.disableTriangleFacingCull = true;
        packet.RayTracingScene.Instances.push_back(std::move(instanceSnapshot));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &capabilities;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        context.RenderWidth = TestWidth;
        context.RenderHeight = TestHeight;
        context.ScreenWidth = TestWidth;
        context.ScreenHeight = TestHeight;

        SharedResourceRegistry sharedResources;
        context.SharedResources = &sharedResources;
        TexturePtr albedo;
        TexturePtr normal;
        TexturePtr material;
        TexturePtr depth;
        TexturePtr emissive;
        if (!CreateTestGBuffer(device, sharedResources, albedo, normal, material, depth, emissive))
        {
            return 1;
        }

        LightingPass lightingPass;
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
        if (!DDGIProbeRayQueryVulkanTestAccess::GetSceneColorTexture(lightingPass))
        {
            std::cerr << "LightingPassのSceneColorが作成されませんでした\n";
            return 1;
        }

        LightingFrameObservation activeFrame0;
        if (!RunLightingFrame(device, device.get(), lightingPass, renderer, context,
                              topLevelBuild, 0u, 1u, true, true, false,
                              activeFrame0) ||
            !ValidateProbeHitAndMiss(activeFrame0, volume))
        {
            std::cerr << "LightingPass経由のframe 0 ray query hit/missが一致しません\n";
            return 1;
        }

        LightingFrameObservation activeFrame1;
        if (!RunLightingFrame(device, device.get(), lightingPass, renderer, context,
                              topLevelBuild, 1u, 2u, true, true, false,
                              activeFrame1) ||
            !ValidateProbeHitAndMiss(activeFrame1, volume))
        {
            std::cerr << "別frame slotのray query hit/missが一致しません\n";
            return 1;
        }

        LightingFrameObservation reusedFrame0;
        if (!RunLightingFrame(device, device.get(), lightingPass, renderer, context,
                              topLevelBuild, 0u, 3u, true, true, false,
                              reusedFrame0) ||
            !ValidateProbeHitAndMiss(reusedFrame0, volume) ||
            reusedFrame0.ProbeResultBuffer.get() != activeFrame0.ProbeResultBuffer.get())
        {
            std::cerr << "frame slot再利用でprobe bufferまたはray query結果が一致しません\n";
            return 1;
        }

        NorvesLib::RHI::DeviceCapabilitiesA rayQueryDisabledCapabilities = capabilities;
        rayQueryDisabledCapabilities.RayTracing.bRayQuery = false;
        context.Capabilities = &rayQueryDisabledCapabilities;
        LightingFrameObservation disabledFrame;
        if (!RunLightingFrame(device, device.get(), lightingPass, renderer, context,
                              topLevelBuild, 0u, 4u, false, false, true,
                              disabledFrame) ||
            !disabledFrame.bSentinelIntact ||
            !AreSceneColorsEqual(reusedFrame0, disabledFrame))
        {
            std::cerr << "RT無効時に番兵値またはLightingPass出力が維持されません\n";
            return 1;
        }

        context.Capabilities = &capabilities;
        FailingDDGIResourceDevice failingPipelineDevice(
            device.get(), FailurePoint::ComputePipeline);
        FailingDDGIResourceDevice failingResultBufferDevice(
            device.get(), FailurePoint::ResultBuffer);
        if (!ValidateResourceFailureFallback(device,
                                             failingPipelineDevice,
                                             lightingPass,
                                             renderer,
                                             context,
                                             topLevelBuild,
                                             FailurePoint::ComputePipeline,
                                             2u,
                                             5u,
                                             reusedFrame0) ||
            !ValidateResourceFailureFallback(device,
                                             failingResultBufferDevice,
                                             lightingPass,
                                             renderer,
                                             context,
                                             topLevelBuild,
                                             FailurePoint::ResultBuffer,
                                             2u,
                                             7u,
                                             reusedFrame0))
        {
            std::cerr << "DDGI資源例外時に既存LightingPass描画が維持されません\n";
            return 1;
        }

        const DDGIProbeRayQueryResult& hitResult =
            activeFrame0.ProbeResults[ExpectedHitDirectionIndex];
        const DDGIProbeRayQueryResult& missResult = activeFrame0.ProbeResults[0u];
        std::cout << "lighting_dispatch_same_command_list=true\n";
        std::cout << "probe_ray_hit=1 distance=" << hitResult.Distance
                  << " instance_custom_index=" << hitResult.InstanceCustomIndex
                  << " primitive_index=" << hitResult.PrimitiveIndex << '\n';
        std::cout << "probe_ray_miss=0 distance=" << missResult.Distance << '\n';
        std::cout << "frame_slot_reuse=true draw_count=" << reusedFrame0.DrawCallCount << '\n';
        std::cout << "ray_query_disabled_dispatch=false sentinel_intact=true"
                  << " scene_color_equal=true\n";
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
        std::cerr << "DDGIProbeRayQueryVulkanTest threw an exception: "
                  << exception.what() << '\n';
        return 1;
    }
}
