// DDGI irradiance/distance atlasの積分・履歴・境界をVulkan readbackで検証する。
#include "Rendering/DDGIVolume.h"
#include "Rendering/DDGIProbePass.h"
#include "Rendering/FramePacket.h"
#include "Rendering/LightingPassGpuTypes.h"
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

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "DDGIProbeUpdateVulkanTest";
    constexpr uint32_t ProbeCount = 2u;
    constexpr uint32_t RayCount = DDGIProbeRayDirectionCount;
    constexpr uint32_t AtlasTexelCount = 8u;
    constexpr uint32_t AtlasInteriorTexelCount = 6u;
    constexpr uint32_t AtlasArrayLayerCount = 2u;
    constexpr uint32_t EmitterCustomIndex = 17u;
    constexpr uint32_t OccluderCustomIndex = 18u;
    constexpr uint32_t VisibilityWallCustomIndex = 19u;
    constexpr uint32_t VisibilityWallBackCustomIndex = 20u;
    constexpr uint32_t LeftWallCustomIndex = 21u;
    constexpr uint32_t FloorCustomIndex = 22u;
    constexpr uint32_t Probe1FloorCustomIndex = 23u;
    constexpr uint32_t BounceRayIndex = 55u;
    constexpr float OcclusionPlaneHitX = 0.65f;
    constexpr float Pi = 3.14159265358979323846f;
    constexpr float DDGIWrapWeightFloor = 0.004f;
    constexpr float DistanceConeExponent = 8.0f;
    constexpr float BackfaceRatioThreshold = 0.25f;
    constexpr float BackfaceDistanceScale = 0.2f;
    constexpr uint32_t ProbeEmitterSubdivision = 4u;
    constexpr float EmitterShadowEndOffset = 0.004f;
    constexpr float RayMinimumDistance = 0.001f;
    constexpr float AtlasHysteresis = 0.60f;
    constexpr float EmitterRadiance[3] = {8.0f, 4.0f, 2.0f};
    constexpr float OccluderBaseColor[3] = {0.5f, 0.5f, 0.5f};

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture()
        {
            Vulkan::BeginVulkanValidationErrorCaptureForTesting();
        }

        ~VulkanValidationErrorCapture()
        {
            Vulkan::EndVulkanValidationErrorCaptureForTesting();
        }

        bool VerifyNoErrors() const
        {
            const uint32_t hitCount = Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
            std::cout << "VUID_COUNT=" << hitCount << '\n';
            if (hitCount != 0u)
            {
                std::cerr << "Vulkan validation errorを検出しました: " << hitCount << '\n';
                return false;
            }
            return true;
        }
    };

    struct Vertex
    {
        float Position[3];
    };

    struct TriangleResources
    {
        BufferPtr VertexBuffer;
        BufferPtr IndexBuffer;
        AccelerationStructurePtr BottomLevel;
        Vertex Vertices[3] = {};
    };

    // CPUの参照計算に使う、場面の三角形（world座標）と放射。
    struct SceneTriangle
    {
        Math::Vector3 Vertices[3];
        uint32_t CustomIndex = 0u;
        float Emission[3] = {};
    };

    struct SceneResources
    {
        AccelerationStructureBuildDesc TopLevelBuild;
        TriangleResources Plane;
        TriangleResources Occluder;
        TriangleResources VisibilityWall;
        TriangleResources VisibilityWallBack;
        TriangleResources LeftWall;
        TriangleResources Floor;
        TriangleResources Probe1Floor;
        VariableArray<SceneTriangle> Triangles;
    };

    // 照度atlasは全体（slot [0, AtlasArrayLayerCount)）と間接光だけ
    // （slot [AtlasArrayLayerCount, 2×AtlasArrayLayerCount)）の2組を読む。
    struct ProbeObservation
    {
        VariableArray<DDGIProbeRayQueryResult> Rays;
        uint16_t IrradianceBits[2u * AtlasArrayLayerCount * AtlasTexelCount * AtlasTexelCount * 4u] = {};
        uint16_t DistanceBits[AtlasArrayLayerCount * AtlasTexelCount * AtlasTexelCount * 2u] = {};
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

    bool CreateTriangleResourcesFromVertices(const DevicePtr& device,
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
        indexDesc.DebugName = "DDGIProbeUpdate.Indices";
        outResources.IndexBuffer = device->CreateBuffer(indexDesc);
        if (!outResources.VertexBuffer || !outResources.IndexBuffer ||
            outResources.VertexBuffer->GetDeviceAddress() == 0u ||
            outResources.IndexBuffer->GetDeviceAddress() == 0u)
        {
            std::cerr << "probe update fixtureのBDA geometry bufferを作成できません\n";
            return false;
        }
        outResources.VertexBuffer->Update(vertices, sizeof(vertices));
        std::memcpy(outResources.Vertices, vertices, sizeof(vertices));
        outResources.IndexBuffer->Update(indices, sizeof(indices));

        AccelerationStructureDesc bottomLevelDesc;
        bottomLevelDesc.type = AccelerationStructureType::BottomLevel;
        bottomLevelDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, 1u, true});
        outResources.BottomLevel = device->CreateAccelerationStructure(bottomLevelDesc);
        if (!outResources.BottomLevel)
        {
            std::cerr << "probe update fixtureのBLASを作成できません\n";
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
            std::cerr << "probe update fixtureのBLASを構築できません\n";
            return false;
        }
        return true;
    }

    // 頂点順の外積は+Z寄りを向き、表（外積の逆側）は-Z側になる。bFrontTowardPositiveZなら頂点順を
    // 入れ替えて表を+Z側にする。
    bool CreateTriangleResources(const DevicePtr& device,
                                 float planeZ,
                                 const char* debugName,
                                 TriangleResources& outResources,
                                 float halfExtent = 10000.0f,
                                 float slopeX = 0.0f,
                                 bool bFrontTowardPositiveZ = false)
    {
        Vertex vertices[3] = {
            {{-halfExtent, -halfExtent, planeZ - slopeX * halfExtent}},
            {{halfExtent, -halfExtent, planeZ + slopeX * halfExtent}},
            {{0.0f, halfExtent, planeZ}},
        };
        if (bFrontTowardPositiveZ)
        {
            std::swap(vertices[1], vertices[2]);
        }
        return CreateTriangleResourcesFromVertices(device, vertices, debugName, outResources);
    }

    // 頂点順の外積は+Xを向き、表は-X側になる。bFrontTowardPositiveXなら表を+X側にする。
    bool CreateVerticalTriangleResources(const DevicePtr& device,
                                         float planeX,
                                         float minimumZ,
                                         float maximumZ,
                                         const char* debugName,
                                         TriangleResources& outResources,
                                         bool bFrontTowardPositiveX = false)
    {
        Vertex vertices[3] = {
            {{planeX, -10000.0f, minimumZ}},
            {{planeX, 10000.0f, minimumZ}},
            {{planeX, 0.0f, maximumZ}},
        };
        if (bFrontTowardPositiveX)
        {
            std::swap(vertices[1], vertices[2]);
        }
        return CreateTriangleResourcesFromVertices(device, vertices, debugName, outResources);
    }

    bool TryGetOcclusionPlaneOffset(float& outOffset)
    {
        Math::Vector3 direction;
        if (!TryGetDDGIProbeRayDirection(BounceRayIndex, direction) ||
            std::abs(direction.x) <= 1.0e-6f)
        {
            return false;
        }

        outOffset = OcclusionPlaneHitX * (direction.z - direction.x) / direction.x;
        return std::isfinite(outOffset);
    }

    void AppendInstance(FramePacket& packet,
                        SceneResources& scene,
                        const TriangleResources& triangle,
                        uint32_t customIndex,
                        const RayTracingHitMaterialSnapshot& material)
    {
        SceneTriangle sceneTriangle;
        for (uint32_t vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
        {
            sceneTriangle.Vertices[vertexIndex] = Math::Vector3(
                triangle.Vertices[vertexIndex].Position[0],
                triangle.Vertices[vertexIndex].Position[1],
                triangle.Vertices[vertexIndex].Position[2]);
        }
        sceneTriangle.CustomIndex = customIndex;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            sceneTriangle.Emission[channel] =
                material.EmissiveColor[channel] * material.EmissiveLuminanceNits;
        }
        scene.Triangles.push_back(sceneTriangle);

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
        snapshot.Instance.disableTriangleFacingCull = true;
        snapshot.BottomLevel = triangle.BottomLevel;
        snapshot.Material = material;
        packet.RayTracingScene.Instances.push_back(snapshot);
    }

    // probe update fixtureの場面。三角形の表（ラスタの裏面カリングと同じく頂点順の外積の逆側）は、
    // BackfacingFloor以外ではprobeの側を向く。
    enum class ProbeScene
    {
        // 上（z=+1）の発光面だけ。
        EmitterAbove,
        // 上の発光面、probeの間の両面の壁（x=0.98と0.981）、probe 0の左の壁（x=-0.98）、probe 1の下の
        // 小さな床（x≧0.99、z=-0.05）。
        VisibilitySeed,
        // 下（z=-1）の床と斜めの遮蔽面。発光面はない。
        Occlusion,
        // 上の発光面と、下の小さな床（発光しない）。床は発光面に照らされる。
        SeedFloor,
        // 上の発光面と、表が下を向いた床。probeは床の裏を見るため無効になる。
        BackfacingFloor,
    };

    RayTracingHitMaterialSnapshot MakeEmitterMaterial()
    {
        RayTracingHitMaterialSnapshot material;
        material.ObjectColor[0] = 1.0f;
        material.ObjectColor[1] = 1.0f;
        material.ObjectColor[2] = 1.0f;
        material.ObjectColor[3] = 1.0f;
        material.EmissiveColor[0] = 0.8f;
        material.EmissiveColor[1] = 0.4f;
        material.EmissiveColor[2] = 0.2f;
        material.EmissiveLuminanceNits = 10.0f;
        return material;
    }

    RayTracingHitMaterialSnapshot MakeSurfaceMaterial()
    {
        RayTracingHitMaterialSnapshot material;
        material.ObjectColor[0] = OccluderBaseColor[0];
        material.ObjectColor[1] = OccluderBaseColor[1];
        material.ObjectColor[2] = OccluderBaseColor[2];
        material.ObjectColor[3] = 1.0f;
        return material;
    }

    bool CreateTestScene(const DevicePtr& device,
                         ProbeScene sceneKind,
                         FramePacket& outPacket,
                         SceneResources& outResources,
                         uint32_t probeCount = ProbeCount)
    {
        DDGIVolumeParameters volume = MakeDefaultDDGIVolumeParameters();
        volume.bEnabled = true;
        volume.Origin = Math::Vector3::Zero;
        volume.ProbeSpacing = Math::Vector3::One;
        volume.ProbeCountX = probeCount;
        volume.ProbeCountY = 1u;
        volume.ProbeCountZ = 1u;
        outPacket.Scene.SetDDGIVolumeParameters(volume);

        if (sceneKind == ProbeScene::Occlusion)
        {
            if (!CreateTriangleResources(device,
                                         -1.0f,
                                         "DDGIProbeUpdate.PlaneVertices",
                                         outResources.Plane,
                                         10000.0f,
                                         0.0f,
                                         true))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.Plane,
                           EmitterCustomIndex,
                           MakeSurfaceMaterial());

            float occlusionPlaneOffset = 0.0f;
            if (!TryGetOcclusionPlaneOffset(occlusionPlaneOffset) ||
                !CreateTriangleResources(device,
                                         occlusionPlaneOffset,
                                         "DDGIProbeUpdate.OccluderVertices",
                                         outResources.Occluder,
                                         10000.0f,
                                         1.0f,
                                         true))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.Occluder,
                           OccluderCustomIndex,
                           MakeSurfaceMaterial());
        }
        else
        {
            if (!CreateTriangleResources(device,
                                         1.0f,
                                         "DDGIProbeUpdate.PlaneVertices",
                                         outResources.Plane,
                                         4.0f))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.Plane,
                           EmitterCustomIndex,
                           MakeEmitterMaterial());
        }

        if (sceneKind == ProbeScene::SeedFloor || sceneKind == ProbeScene::BackfacingFloor)
        {
            const bool bBackfacing = sceneKind == ProbeScene::BackfacingFloor;
            if (!CreateTriangleResources(device,
                                         -1.0f,
                                         "DDGIProbeUpdate.FloorVertices",
                                         outResources.Floor,
                                         bBackfacing ? 10000.0f : 0.5f,
                                         0.0f,
                                         !bBackfacing))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.Floor,
                           FloorCustomIndex,
                           MakeSurfaceMaterial());
        }

        if (sceneKind == ProbeScene::VisibilitySeed)
        {
            // probeの間の壁は、それぞれのprobeに表を向けた2枚を1mm離して重ねる（1枚ではどちらかのprobeが
            // 面の裏を見て無効になり、可視の重みを確かめられない）。
            if (!CreateVerticalTriangleResources(device,
                                                 0.98f,
                                                 -4.0f,
                                                 4.0f,
                                                 "DDGIProbeUpdate.VisibilityWallVertices",
                                                 outResources.VisibilityWall) ||
                !CreateVerticalTriangleResources(device,
                                                 0.981f,
                                                 -4.0f,
                                                 4.0f,
                                                 "DDGIProbeUpdate.VisibilityWallBackVertices",
                                                 outResources.VisibilityWallBack,
                                                 true) ||
                !CreateVerticalTriangleResources(device,
                                                 -0.98f,
                                                 -4.0f,
                                                 4.0f,
                                                 "DDGIProbeUpdate.LeftWallVertices",
                                                 outResources.LeftWall,
                                                 true))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.VisibilityWall,
                           VisibilityWallCustomIndex,
                           MakeSurfaceMaterial());
            AppendInstance(outPacket,
                           outResources,
                           outResources.VisibilityWallBack,
                           VisibilityWallBackCustomIndex,
                           MakeSurfaceMaterial());
            AppendInstance(outPacket,
                           outResources,
                           outResources.LeftWall,
                           LeftWallCustomIndex,
                           MakeSurfaceMaterial());

            // probe 1の下向きのrayを近くで止め、壁の向こうの点への距離の分布を狭くする（距離のcosineの
            // 裾に遠いmissが入ると分散が大きくなり、壁の向こうの点が見えるとみなされる）。probe 0からは
            // 壁に隠れる。表は上（probe 1の側）。
            const Vertex probe1FloorVertices[3] = {
                {{0.99f, -2.0f, -0.05f}},
                {{0.99f, 2.0f, -0.05f}},
                {{3.0f, 0.0f, -0.05f}},
            };
            if (!CreateTriangleResourcesFromVertices(device,
                                                     probe1FloorVertices,
                                                     "DDGIProbeUpdate.Probe1FloorVertices",
                                                     outResources.Probe1Floor))
            {
                return false;
            }
            AppendInstance(outPacket,
                           outResources,
                           outResources.Probe1Floor,
                           Probe1FloorCustomIndex,
                           MakeSurfaceMaterial());
        }

        AccelerationStructureDesc topLevelDesc;
        topLevelDesc.type = AccelerationStructureType::TopLevel;
        topLevelDesc.maxInstanceCount = static_cast<uint32_t>(
            outPacket.RayTracingScene.Instances.size());
        outPacket.RayTracingScene.TopLevel = device->CreateAccelerationStructure(topLevelDesc);
        if (!outPacket.RayTracingScene.TopLevel)
        {
            std::cerr << "probe update fixtureのTLASを作成できません\n";
            return false;
        }

        outResources.TopLevelBuild.type = AccelerationStructureType::TopLevel;
        outResources.TopLevelBuild.destination = outPacket.RayTracingScene.TopLevel;
        for (const RayTracingSceneInstanceSnapshot& snapshot :
             outPacket.RayTracingScene.Instances)
        {
            outResources.TopLevelBuild.instances.push_back(snapshot.Instance);
        }
        return true;
    }

    bool CreateLightingInputs(const DevicePtr& device,
                              BufferPtr& outLightBuffer,
                              TexturePtr& outEnvironmentTexture,
                              SamplerPtr& outEnvironmentSampler)
    {
        BufferDesc lightBufferDesc(
            sizeof(GPULightData),
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead,
            true,
            "DDGIProbeUpdate.Lights");
        outLightBuffer = device->CreateBuffer(lightBufferDesc);
        if (!outLightBuffer)
        {
            std::cerr << "probe update fixtureのライトbufferを作成できません\n";
            return false;
        }
        const GPULightData emptyLight = {};
        outLightBuffer->Update(&emptyLight, sizeof(emptyLight));

        TextureDesc environmentDesc;
        environmentDesc.Width = 1u;
        environmentDesc.Height = 1u;
        environmentDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
        environmentDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        environmentDesc.DebugName = "DDGIProbeUpdate.Environment";
        outEnvironmentTexture = device->CreateTexture(environmentDesc);
        if (!outEnvironmentTexture)
        {
            std::cerr << "probe update fixtureのenvironment textureを作成できません\n";
            return false;
        }
        const uint16_t emptyEnvironment[4] = {};
        outEnvironmentTexture->Update(
            emptyEnvironment, sizeof(emptyEnvironment), sizeof(emptyEnvironment));

        SamplerDesc samplerDesc;
        samplerDesc.filterMin = FilterMode::Point;
        samplerDesc.filterMag = FilterMode::Point;
        samplerDesc.filterMip = FilterMode::Point;
        samplerDesc.addressU = TextureAddressMode::Clamp;
        samplerDesc.addressV = TextureAddressMode::Clamp;
        samplerDesc.addressW = TextureAddressMode::Clamp;
        outEnvironmentSampler = device->CreateSampler(samplerDesc);
        return outEnvironmentSampler != nullptr;
    }

    void PublishLighting(ViewRenderContext& context,
                         const BufferPtr& lightBuffer,
                         const TexturePtr& environmentTexture,
                         const SamplerPtr& environmentSampler,
                         uint64_t frameNumber)
    {
        context.PhysicalLighting.Begin(frameNumber, 0u, 0u);
        context.PhysicalLighting.PublishLighting(
            lightBuffer,
            0u,
            static_cast<uint32_t>(sizeof(GPULightData)),
            environmentTexture,
            environmentSampler,
            {},
            {},
            {},
            {},
            {},
            {},
            1u,
            0.0f,
            false);
    }

    bool RunProbeFrame(const DevicePtr& device,
                       ShaderManager& shaderManager,
                       DDGIProbePass& probePass,
                       FramePacket& packet,
                       const SceneResources& sceneResources,
                       const BufferPtr& lightBuffer,
                       const TexturePtr& environmentTexture,
                       const SamplerPtr& environmentSampler,
                       uint32_t frameIndex,
                       uint32_t expectedProbeCount,
                       ProbeObservation& outObservation)
    {
        CommandListPtr commandList = device->CreateCommandList();
        if (!commandList)
        {
            std::cerr << "probe update fixture用command listを作成できません\n";
            return false;
        }
        TSharedPtr<Vulkan::VulkanCommandList> vulkanCommandList =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        if (!vulkanCommandList)
        {
            std::cerr << "Vulkan command listへ変換できません\n";
            return false;
        }

        const RHI::DeviceCapabilitiesA& capabilities = device->GetCapabilities();
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.Capabilities = &capabilities;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        context.FrameIndex = frameIndex;
        context.FrameNumber = static_cast<uint64_t>(frameIndex) + 1u;
        context.CommandList = commandList.get();
        PublishLighting(context,
                         lightBuffer,
                         environmentTexture,
                         environmentSampler,
                         context.FrameNumber);

        commandList->SetFrameIndex(frameIndex);
        commandList->Begin();
        if (!commandList->BuildAccelerationStructure(sceneResources.TopLevelBuild) ||
            !probePass.Execute(context))
        {
            std::cerr << "TLAS buildまたはprobe atlas updateを記録できません\n";
            return false;
        }

        BufferPtr resultBuffer = probePass.GetResultBuffer(frameIndex, 0u, 0u);
        TexturePtr irradianceAtlas = probePass.GetIrradianceAtlas(frameIndex, 0u, 0u);
        TexturePtr distanceAtlas = probePass.GetDistanceAtlas(frameIndex, 0u, 0u);
        if (!resultBuffer || !irradianceAtlas || !distanceAtlas ||
            probePass.GetResultCount(frameIndex, 0u, 0u) != expectedProbeCount * RayCount ||
            probePass.GetAtlasProbeCount(frameIndex, 0u, 0u) != expectedProbeCount ||
            irradianceAtlas->GetWidth() != AtlasTexelCount ||
            irradianceAtlas->GetHeight() != AtlasTexelCount ||
            irradianceAtlas->GetArraySize() < AtlasArrayLayerCount ||
            irradianceAtlas->GetArraySize() < 2u * expectedProbeCount ||
            irradianceAtlas->GetFormat() != Format::R16G16B16A16_FLOAT ||
            distanceAtlas->GetWidth() != AtlasTexelCount ||
            distanceAtlas->GetHeight() != AtlasTexelCount ||
            distanceAtlas->GetArraySize() < AtlasArrayLayerCount ||
            distanceAtlas->GetArraySize() < expectedProbeCount ||
            distanceAtlas->GetFormat() != Format::R16G16_FLOAT)
        {
            std::cerr << "probe atlasの寸法・形式・出力参照が不正です\n";
            return false;
        }

        const uint64_t resultSize = static_cast<uint64_t>(expectedProbeCount * RayCount) *
                                    sizeof(DDGIProbeRayQueryResult);
        const uint32_t irradianceLayerSize =
            AtlasTexelCount * AtlasTexelCount * 4u * sizeof(uint16_t);
        const uint32_t distanceLayerSize =
            AtlasTexelCount * AtlasTexelCount * 2u * sizeof(uint16_t);
        const uint32_t irradianceSize = 2u * AtlasArrayLayerCount * irradianceLayerSize;
        const uint32_t distanceSize = AtlasArrayLayerCount * distanceLayerSize;
        BufferPtr resultReadback = device->CreateBuffer(BufferDesc(
            resultSize, ResourceUsage::TransferDst, true, "DDGIProbeUpdate.ResultReadback"));
        BufferPtr irradianceReadback = device->CreateBuffer(BufferDesc(
            irradianceSize,
            ResourceUsage::TransferDst,
            true,
            "DDGIProbeUpdate.IrradianceReadback"));
        BufferPtr distanceReadback = device->CreateBuffer(BufferDesc(
            distanceSize,
            ResourceUsage::TransferDst,
            true,
            "DDGIProbeUpdate.DistanceReadback"));
        if (!resultReadback || !irradianceReadback || !distanceReadback)
        {
            std::cerr << "probe update readback bufferを作成できません\n";
            return false;
        }

        commandList->BufferBarrier(
            resultBuffer, ResourceState::ShaderResource, ResourceState::CopySource, 0u, resultSize);
        commandList->BufferBarrier(
            resultReadback, ResourceState::Undefined, ResourceState::CopyDest, 0u, resultSize);
        commandList->CopyBuffer(resultBuffer, resultReadback, resultSize);
        commandList->BufferBarrier(
            resultBuffer, ResourceState::CopySource, ResourceState::ShaderResource, 0u, resultSize);

        commandList->TextureBarrier(
            irradianceAtlas,
            ResourceState::ShaderResource,
            ResourceState::CopySource,
            0u,
            0u,
            0u,
            0u);
        commandList->TextureBarrier(
            distanceAtlas,
            ResourceState::ShaderResource,
            ResourceState::CopySource,
            0u,
            0u,
            0u,
            0u);
        commandList->BufferBarrier(
            irradianceReadback,
            ResourceState::Undefined,
            ResourceState::CopyDest,
            0u,
            irradianceSize);
        commandList->BufferBarrier(
            distanceReadback,
            ResourceState::Undefined,
            ResourceState::CopyDest,
            0u,
            distanceSize);
        for (uint32_t probeIndex = 0u; probeIndex < expectedProbeCount; ++probeIndex)
        {
            commandList->CopyTextureToBuffer(
                irradianceAtlas,
                irradianceReadback,
                AtlasTexelCount,
                AtlasTexelCount,
                static_cast<uint64_t>(probeIndex) * irradianceLayerSize,
                0u,
                probeIndex);
            commandList->CopyTextureToBuffer(
                irradianceAtlas,
                irradianceReadback,
                AtlasTexelCount,
                AtlasTexelCount,
                static_cast<uint64_t>(AtlasArrayLayerCount + probeIndex) * irradianceLayerSize,
                0u,
                expectedProbeCount + probeIndex);
            commandList->CopyTextureToBuffer(
                distanceAtlas,
                distanceReadback,
                AtlasTexelCount,
                AtlasTexelCount,
                static_cast<uint64_t>(probeIndex) * distanceLayerSize,
                0u,
                probeIndex);
        }
        commandList->TextureBarrier(
            irradianceAtlas,
            ResourceState::CopySource,
            ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);
        commandList->TextureBarrier(
            distanceAtlas,
            ResourceState::CopySource,
            ResourceState::ShaderResource,
            0u,
            0u,
            0u,
            0u);

        if (!RecordHostReadBarrier(vulkanCommandList, resultReadback) ||
            !RecordHostReadBarrier(vulkanCommandList, irradianceReadback) ||
            !RecordHostReadBarrier(vulkanCommandList, distanceReadback))
        {
            std::cerr << "probe update readbackのhost barrierを記録できません\n";
            return false;
        }
        commandList->End();
        commandList->Submit(true);

        const void* resultData = resultReadback->Map(0u, resultSize);
        const void* irradianceData = irradianceReadback->Map(0u, irradianceSize);
        const void* distanceData = distanceReadback->Map(0u, distanceSize);
        if (!resultData || !irradianceData || !distanceData)
        {
            if (resultData)
            {
                resultReadback->Unmap();
            }
            if (irradianceData)
            {
                irradianceReadback->Unmap();
            }
            if (distanceData)
            {
                distanceReadback->Unmap();
            }
            std::cerr << "probe update GPU readbackをmapできません\n";
            return false;
        }

        outObservation.Rays.resize(expectedProbeCount * RayCount);
        std::memcpy(outObservation.Rays.data(), resultData, static_cast<size_t>(resultSize));
        std::memcpy(outObservation.IrradianceBits, irradianceData, irradianceSize);
        std::memcpy(outObservation.DistanceBits, distanceData, distanceSize);
        resultReadback->Unmap();
        irradianceReadback->Unmap();
        distanceReadback->Unmap();
        return true;
    }

    float HalfToFloat(uint16_t bits)
    {
        const float sign = (bits & 0x8000u) != 0u ? -1.0f : 1.0f;
        const uint32_t exponent = (bits >> 10u) & 0x1Fu;
        const uint32_t mantissa = bits & 0x03FFu;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 0x1Fu)
        {
            return mantissa == 0u
                ? sign * std::numeric_limits<float>::infinity()
                : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                 static_cast<int>(exponent) - 15);
    }

    // bIndirectなら間接光だけの組を読む。channel 3はprobeの有効（1）/無効（0）。
    float ReadIrradiance(const ProbeObservation& observation,
                         uint32_t probeIndex,
                         uint32_t x,
                         uint32_t y,
                         uint32_t channel,
                         bool bIndirect = false)
    {
        const uint32_t slot = (bIndirect ? AtlasArrayLayerCount : 0u) + probeIndex;
        const uint32_t index =
            (slot * AtlasTexelCount * AtlasTexelCount + y * AtlasTexelCount + x) * 4u +
            channel;
        return HalfToFloat(observation.IrradianceBits[index]);
    }

    float ReadDistanceMoment(const ProbeObservation& observation,
                             uint32_t probeIndex,
                             uint32_t x,
                             uint32_t y,
                             uint32_t channel)
    {
        const uint32_t index =
            (probeIndex * AtlasTexelCount * AtlasTexelCount + y * AtlasTexelCount + x) * 2u +
            channel;
        return HalfToFloat(observation.DistanceBits[index]);
    }

    bool TryGetTexelDirection(uint32_t x, uint32_t y, Math::Vector3& outDirection)
    {
        const Math::Vector2 uv(
            (static_cast<float>(x - 1u) + 0.5f) / AtlasInteriorTexelCount,
            (static_cast<float>(y - 1u) + 0.5f) / AtlasInteriorTexelCount);
        return DecodeDDGIOctahedralDirection(uv, outDirection);
    }

    Math::Vector3 SubtractVector(const Math::Vector3& lhs, const Math::Vector3& rhs)
    {
        return Math::Vector3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
    }

    Math::Vector3 CrossVector(const Math::Vector3& lhs, const Math::Vector3& rhs)
    {
        return Math::Vector3(lhs.y * rhs.z - lhs.z * rhs.y,
                             lhs.z * rhs.x - lhs.x * rhs.z,
                             lhs.x * rhs.y - lhs.y * rhs.x);
    }

    float DotVector(const Math::Vector3& lhs, const Math::Vector3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    // 両面の三角形とrayの交差（GPUのray queryと同じく裏面も当たる）。tは(RayMinimumDistance, maxDistance)。
    bool IntersectsTriangle(const Math::Vector3& origin,
                            const Math::Vector3& direction,
                            float maxDistance,
                            const SceneTriangle& triangle)
    {
        const Math::Vector3 edge1 = SubtractVector(triangle.Vertices[1], triangle.Vertices[0]);
        const Math::Vector3 edge2 = SubtractVector(triangle.Vertices[2], triangle.Vertices[0]);
        const Math::Vector3 pvec = CrossVector(direction, edge2);
        const double determinant = DotVector(edge1, pvec);
        if (std::abs(determinant) <= 1.0e-12)
        {
            return false;
        }
        const double inverseDeterminant = 1.0 / determinant;
        const Math::Vector3 tvec = SubtractVector(origin, triangle.Vertices[0]);
        const double u = DotVector(tvec, pvec) * inverseDeterminant;
        if (u < 0.0 || u > 1.0)
        {
            return false;
        }
        const Math::Vector3 qvec = CrossVector(tvec, edge1);
        const double v = DotVector(direction, qvec) * inverseDeterminant;
        if (v < 0.0 || u + v > 1.0)
        {
            return false;
        }
        const double distance = DotVector(edge2, qvec) * inverseDeterminant;
        return distance > RayMinimumDistance && distance < maxDistance;
    }

    bool IsSegmentOccluded(const SceneResources& scene,
                           const Math::Vector3& origin,
                           const Math::Vector3& direction,
                           float maxDistance)
    {
        for (const SceneTriangle& triangle : scene.Triangles)
        {
            if (IntersectsTriangle(origin, direction, maxDistance, triangle))
            {
                return true;
            }
        }
        return false;
    }

    bool IsEmissive(const SceneTriangle& triangle)
    {
        return triangle.Emission[0] > 0.0f || triangle.Emission[1] > 0.0f ||
               triangle.Emission[2] > 0.0f;
    }

    // shaderのComputeEmitterIrradiance（DDGI/ProbeEmitterSampling.glsl）と同じ見積もり。
    void ComputeProbeEmitterIrradiance(const SceneResources& scene,
                                       const Math::Vector3& probePosition,
                                       const Math::Vector3& normal,
                                       float (&outIrradiance)[3])
    {
        outIrradiance[0] = 0.0f;
        outIrradiance[1] = 0.0f;
        outIrradiance[2] = 0.0f;
        const float subdivision = static_cast<float>(ProbeEmitterSubdivision);
        for (const SceneTriangle& emitter : scene.Triangles)
        {
            if (!IsEmissive(emitter))
            {
                continue;
            }
            const Math::Vector3 edge1 = SubtractVector(emitter.Vertices[1], emitter.Vertices[0]);
            const Math::Vector3 edge2 = SubtractVector(emitter.Vertices[2], emitter.Vertices[0]);
            const Math::Vector3 crossEdges = CrossVector(edge1, edge2);
            const float doubleArea = std::sqrt(DotVector(crossEdges, crossEdges));
            if (!(doubleArea > 1.0e-6f))
            {
                continue;
            }
            // 頂点法線のない三角形は頂点順の外積の逆側が表で、表の側へ放射する。
            const Math::Vector3 sourceNormal(-crossEdges.x / doubleArea,
                                             -crossEdges.y / doubleArea,
                                             -crossEdges.z / doubleArea);
            const float sampleArea = doubleArea * 0.5f / (subdivision * subdivision);
            const auto evaluate = [&](float u, float v)
            {
                const Math::Vector3 samplePosition(
                    emitter.Vertices[0].x + edge1.x * u + edge2.x * v,
                    emitter.Vertices[0].y + edge1.y * u + edge2.y * v,
                    emitter.Vertices[0].z + edge1.z * u + edge2.z * v);
                const Math::Vector3 toSource = SubtractVector(samplePosition, probePosition);
                const float distanceSquared = DotVector(toSource, toSource);
                if (!(distanceSquared > 1.0e-6f))
                {
                    return;
                }
                const float distance = std::sqrt(distanceSquared);
                const Math::Vector3 direction(
                    toSource.x / distance, toSource.y / distance, toSource.z / distance);
                const float receiverCosine = std::max(DotVector(normal, direction), 0.0f);
                const float sourceCosine = std::max(
                    -DotVector(sourceNormal, direction), 0.0f);
                if (receiverCosine <= 0.0f || sourceCosine <= 0.0f)
                {
                    return;
                }
                const float shadowDistance = std::max(distance - EmitterShadowEndOffset, 0.0f);
                if (shadowDistance > RayMinimumDistance &&
                    IsSegmentOccluded(scene, probePosition, direction, shadowDistance))
                {
                    return;
                }
                const float solidAngle = std::min(
                    sourceCosine * sampleArea / distanceSquared, 2.0f * Pi);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    outIrradiance[channel] +=
                        emitter.Emission[channel] * receiverCosine * solidAngle;
                }
            };
            for (uint32_t row = 0u; row < ProbeEmitterSubdivision; ++row)
            {
                for (uint32_t column = 0u; row + column < ProbeEmitterSubdivision; ++column)
                {
                    evaluate((static_cast<float>(row) + 1.0f / 3.0f) / subdivision,
                             (static_cast<float>(column) + 1.0f / 3.0f) / subdivision);
                    if (row + column + 1u < ProbeEmitterSubdivision)
                    {
                        evaluate((static_cast<float>(row) + 2.0f / 3.0f) / subdivision,
                                 (static_cast<float>(column) + 2.0f / 3.0f) / subdivision);
                    }
                }
            }
        }
    }

    const SceneTriangle* FindSceneTriangle(const SceneResources& scene, uint32_t customIndex)
    {
        for (const SceneTriangle& triangle : scene.Triangles)
        {
            if (triangle.CustomIndex == customIndex)
            {
                return &triangle;
            }
        }
        return nullptr;
    }

    bool IsBackfaceHit(const DDGIProbeRayQueryResult& ray)
    {
        return ray.bHit != 0u && ray.Radiance[3] < 0.5f;
    }

    bool IsProbeActive(const ProbeObservation& observation, uint32_t probeIndex)
    {
        uint32_t backfaceCount = 0u;
        for (uint32_t rayIndex = 0u; rayIndex < RayCount; ++rayIndex)
        {
            if (IsBackfaceHit(observation.Rays[probeIndex * RayCount + rayIndex]))
            {
                ++backfaceCount;
            }
        }
        return static_cast<float>(backfaceCount) <=
               BackfaceRatioThreshold * static_cast<float>(RayCount);
    }

    // 現frameの間接光（面の裏に当たったrayを除き、発光面に当たったrayは放射を除く）。
    void ComputeCurrentIrradiance(const ProbeObservation& observation,
                                  const SceneResources& scene,
                                  uint32_t probeIndex,
                                  uint32_t x,
                                  uint32_t y,
                                  float (&outIrradiance)[3])
    {
        Math::Vector3 targetDirection;
        if (!TryGetTexelDirection(x, y, targetDirection))
        {
            outIrradiance[0] = 0.0f;
            outIrradiance[1] = 0.0f;
            outIrradiance[2] = 0.0f;
            return;
        }

        double sums[3] = {};
        double weightSum = 0.0;
        for (uint32_t rayIndex = 0u; rayIndex < RayCount; ++rayIndex)
        {
            Math::Vector3 rayDirection;
            if (!TryGetDDGIProbeRayDirection(rayIndex, rayDirection))
            {
                continue;
            }
            const float weight = std::max(
                0.0f,
                targetDirection.x * rayDirection.x +
                    targetDirection.y * rayDirection.y +
                    targetDirection.z * rayDirection.z);
            if (weight <= 0.0f)
            {
                continue;
            }
            const DDGIProbeRayQueryResult& ray = observation.Rays[probeIndex * RayCount + rayIndex];
            if (IsBackfaceHit(ray))
            {
                continue;
            }
            const SceneTriangle* hitTriangle = ray.bHit != 0u
                ? FindSceneTriangle(scene, ray.InstanceCustomIndex)
                : nullptr;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float emission = hitTriangle != nullptr ? hitTriangle->Emission[channel] : 0.0f;
                sums[channel] += static_cast<double>(
                    std::max(ray.Radiance[channel] - emission, 0.0f)) * weight;
            }
            weightSum += weight;
        }

        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outIrradiance[channel] = weightSum > 0.0
                ? static_cast<float>(Pi * sums[channel] / weightSum)
                : 0.0f;
        }
    }

    void ComputeCurrentMoments(const ProbeObservation& observation,
                               uint32_t probeIndex,
                               uint32_t x,
                               uint32_t y,
                               float (&outMoments)[2])
    {
        Math::Vector3 targetDirection;
        if (!TryGetTexelDirection(x, y, targetDirection))
        {
            outMoments[0] = 0.0f;
            outMoments[1] = 0.0f;
            return;
        }

        const float maxDistance = 1.5f * std::sqrt(3.0f);
        double firstMoment = 0.0;
        double secondMoment = 0.0;
        double weightSum = 0.0;
        for (uint32_t rayIndex = 0u; rayIndex < RayCount; ++rayIndex)
        {
            Math::Vector3 rayDirection;
            if (!TryGetDDGIProbeRayDirection(rayIndex, rayDirection))
            {
                continue;
            }
            const float cosineWeight = std::max(
                0.0f,
                targetDirection.x * rayDirection.x +
                    targetDirection.y * rayDirection.y +
                    targetDirection.z * rayDirection.z);
            if (cosineWeight <= 0.0f)
            {
                continue;
            }
            const double weight = std::pow(cosineWeight, DistanceConeExponent);
            const DDGIProbeRayQueryResult& ray = observation.Rays[probeIndex * RayCount + rayIndex];
            double distance = ray.bHit != 0u
                ? std::clamp(static_cast<double>(ray.Distance),
                             0.0,
                             static_cast<double>(maxDistance))
                : maxDistance;
            if (IsBackfaceHit(ray))
            {
                distance *= BackfaceDistanceScale;
            }
            firstMoment += distance * weight;
            secondMoment += distance * distance * weight;
            weightSum += weight;
        }
        outMoments[0] = weightSum > 0.0
            ? static_cast<float>(firstMoment / weightSum)
            : 0.0f;
        outMoments[1] = weightSum > 0.0
            ? static_cast<float>(secondMoment / weightSum)
            : 0.0f;
    }

    void SampleIrradianceAtPositiveZ(const ProbeObservation& observation,
                                     uint32_t probeIndex,
                                     float (&outIrradiance)[3])
    {
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outIrradiance[channel] = 0.25f * (
                ReadIrradiance(observation, probeIndex, 3u, 3u, channel) +
                ReadIrradiance(observation, probeIndex, 4u, 3u, channel) +
                ReadIrradiance(observation, probeIndex, 3u, 4u, channel) +
                ReadIrradiance(observation, probeIndex, 4u, 4u, channel));
        }
    }

    Math::Vector2 MapOctahedralUvToAtlas(const Math::Vector2& octahedralUv)
    {
        return Math::Vector2(
            (octahedralUv.x * AtlasInteriorTexelCount + 1.0f) / AtlasTexelCount,
            (octahedralUv.y * AtlasInteriorTexelCount + 1.0f) / AtlasTexelCount);
    }

    float SampleAtlasLinear(const ProbeObservation& observation,
                            uint32_t probeIndex,
                            const Math::Vector2& atlasUv,
                            uint32_t channel,
                            bool bIrradiance,
                            bool bIndirect = false)
    {
        const float sampleX = atlasUv.x * AtlasTexelCount - 0.5f;
        const float sampleY = atlasUv.y * AtlasTexelCount - 0.5f;
        const int32_t baseX = static_cast<int32_t>(std::floor(sampleX));
        const int32_t baseY = static_cast<int32_t>(std::floor(sampleY));
        const float alphaX = sampleX - static_cast<float>(baseX);
        const float alphaY = sampleY - static_cast<float>(baseY);
        const auto sample = [&](int32_t x, int32_t y)
        {
            const uint32_t texelX = static_cast<uint32_t>(std::clamp(
                x, 0, static_cast<int32_t>(AtlasTexelCount) - 1));
            const uint32_t texelY = static_cast<uint32_t>(std::clamp(
                y, 0, static_cast<int32_t>(AtlasTexelCount) - 1));
            return bIrradiance
                ? ReadIrradiance(observation, probeIndex, texelX, texelY, channel, bIndirect)
                : ReadDistanceMoment(observation, probeIndex, texelX, texelY, channel);
        };
        const float top = sample(baseX, baseY) * (1.0f - alphaX) +
                          sample(baseX + 1, baseY) * alphaX;
        const float bottom = sample(baseX, baseY + 1) * (1.0f - alphaX) +
                             sample(baseX + 1, baseY + 1) * alphaX;
        return top * (1.0f - alphaY) + bottom * alphaY;
    }

    float SampleAtlasAtDirection(const ProbeObservation& observation,
                                 uint32_t probeIndex,
                                 const Math::Vector3& direction,
                                 uint32_t channel,
                                 bool bIrradiance,
                                 bool bIndirect = false)
    {
        Math::Vector2 octahedralUv;
        EncodeDDGIOctahedralDirection(direction, octahedralUv);
        return SampleAtlasLinear(observation,
                                 probeIndex,
                                 MapOctahedralUvToAtlas(octahedralUv),
                                 channel,
                                 bIrradiance,
                                 bIndirect);
    }

    float SamplePreviousVisibility(const ProbeObservation& observation,
                                   uint32_t probeIndex,
                                   const Math::Vector3& probeToPoint,
                                   float pointDistance,
                                   float& outMeanDistance)
    {
        const Math::Vector3 direction(
            probeToPoint.x / pointDistance,
            probeToPoint.y / pointDistance,
            probeToPoint.z / pointDistance);
        const float meanDistance = std::max(
            0.0f, SampleAtlasAtDirection(observation, probeIndex, direction, 0u, false));
        outMeanDistance = meanDistance;
        const float secondMoment = SampleAtlasAtDirection(
            observation, probeIndex, direction, 1u, false);
        const float variance = std::max(secondMoment - meanDistance * meanDistance, 1.0e-4f);
        if (pointDistance <= meanDistance)
        {
            return 1.0f;
        }
        const float delta = pointDistance - meanDistance;
        const float chebyshev = variance / (variance + delta * delta);
        return std::max(0.05f, chebyshev * chebyshev * chebyshev);
    }

    bool ComputeExpectedBounce(const ProbeObservation& previousObservation,
                               const DDGIProbeRayQueryResult& hit,
                               float (&outWeighted)[3],
                               float (&outUnweighted)[3],
                               float (&outVisibility)[ProbeCount],
                               float (&outMeanDistance)[ProbeCount],
                               float (&outPointDistance)[ProbeCount])
    {
        Math::Vector3 rayDirection;
        if (!TryGetDDGIProbeRayDirection(BounceRayIndex, rayDirection))
        {
            return false;
        }

        const float inverseSqrtTwo = 1.0f / std::sqrt(2.0f);
        const Math::Vector3 surfaceNormal(-inverseSqrtTwo, 0.0f, inverseSqrtTwo);
        const Math::Vector3 hitPosition(
            rayDirection.x * hit.Distance + surfaceNormal.x * 0.002f,
            rayDirection.y * hit.Distance,
            rayDirection.z * hit.Distance + surfaceNormal.z * 0.002f);
        const float gridPositionX = std::clamp(hitPosition.x, 0.0f, 1.0f);
        float weightedIrradiance[3] = {};
        float unweightedIrradiance[3] = {};
        float weightedWeight = 0.0f;
        float unweightedWeight = 0.0f;
        float visibilityWeight[ProbeCount] = {};
        float visibilitySum[ProbeCount] = {};
        float meanDistanceByProbe[ProbeCount] = {};
        float pointDistanceByProbe[ProbeCount] = {};

        for (uint32_t corner = 0u; corner < 8u; ++corner)
        {
            const uint32_t probeIndex = corner & 1u;
            if (ReadIrradiance(previousObservation, probeIndex, 3u, 3u, 3u, true) < 0.5f)
            {
                continue;
            }
            const float xWeight = probeIndex == 0u ? 1.0f - gridPositionX : gridPositionX;
            const float yWeight = ((corner >> 1u) & 1u) != 0u ? 0.001f : 1.0f;
            const float zWeight = ((corner >> 2u) & 1u) != 0u ? 0.001f : 1.0f;
            float weight = std::max(0.001f, xWeight) * yWeight * zWeight;

            const float probeToPointX = hitPosition.x - static_cast<float>(probeIndex);
            const float probeToPointY = hitPosition.y;
            const float probeToPointZ = hitPosition.z;
            const float pointDistance = std::sqrt(
                probeToPointX * probeToPointX +
                probeToPointY * probeToPointY +
                probeToPointZ * probeToPointZ);
            if (pointDistance <= 1.0e-6f)
            {
                continue;
            }

            const float pointToProbeX = -probeToPointX / pointDistance;
            const float pointToProbeY = -probeToPointY / pointDistance;
            const float pointToProbeZ = -probeToPointZ / pointDistance;
            const float wrapShading =
                (pointToProbeX * surfaceNormal.x + pointToProbeY * surfaceNormal.y +
                 pointToProbeZ * surfaceNormal.z + 1.0f) * 0.5f;
            weight *= wrapShading * wrapShading + DDGIWrapWeightFloor;

            const Math::Vector3 probeToPoint(
                probeToPointX, probeToPointY, probeToPointZ);
            float meanDistance = 0.0f;
            const float visibility = SamplePreviousVisibility(
                previousObservation, probeIndex, probeToPoint, pointDistance, meanDistance);
            visibilitySum[probeIndex] += weight * visibility;
            visibilityWeight[probeIndex] += weight;
            meanDistanceByProbe[probeIndex] = meanDistance;
            pointDistanceByProbe[probeIndex] = pointDistance;

            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float irradiance = SampleAtlasAtDirection(
                    previousObservation, probeIndex, surfaceNormal, channel, true, true);
                weightedIrradiance[channel] += irradiance * weight * visibility;
                unweightedIrradiance[channel] += irradiance * weight;
            }
            weightedWeight += weight * visibility;
            unweightedWeight += weight;
        }

        if (weightedWeight <= 1.0e-6f || unweightedWeight <= 1.0e-6f)
        {
            return false;
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outWeighted[channel] = OccluderBaseColor[channel] *
                                   weightedIrradiance[channel] / weightedWeight / Pi;
            outUnweighted[channel] = OccluderBaseColor[channel] *
                                     unweightedIrradiance[channel] / unweightedWeight / Pi;
        }
        for (uint32_t probeIndex = 0u; probeIndex < ProbeCount; ++probeIndex)
        {
            outVisibility[probeIndex] = visibilityWeight[probeIndex] > 1.0e-6f
                ? visibilitySum[probeIndex] / visibilityWeight[probeIndex]
                : 0.0f;
            outMeanDistance[probeIndex] = meanDistanceByProbe[probeIndex];
            outPointDistance[probeIndex] = pointDistanceByProbe[probeIndex];
        }
        return true;
    }

    bool ValidateBorder(const ProbeObservation& observation,
                        const char* scenario,
                        uint32_t probeCount = ProbeCount)
    {
        for (uint32_t probeIndex = 0u; probeIndex < probeCount; ++probeIndex)
        {
            for (uint32_t y = 0u; y < AtlasTexelCount; ++y)
            {
                for (uint32_t x = 0u; x < AtlasTexelCount; ++x)
                {
                    if (x > 0u && x + 1u < AtlasTexelCount &&
                        y > 0u && y + 1u < AtlasTexelCount)
                    {
                        continue;
                    }

                    uint32_t sourceX = x;
                    uint32_t sourceY = y;
                    if ((x == 0u || x + 1u == AtlasTexelCount) &&
                        (y == 0u || y + 1u == AtlasTexelCount))
                    {
                        sourceX = x == 0u ? AtlasTexelCount - 2u : 1u;
                        sourceY = y == 0u ? AtlasTexelCount - 2u : 1u;
                    }
                    else if (y == 0u || y + 1u == AtlasTexelCount)
                    {
                        sourceX = AtlasTexelCount - 1u - x;
                        sourceY = y == 0u ? 1u : AtlasTexelCount - 2u;
                    }
                    else
                    {
                        sourceX = x == 0u ? 1u : AtlasTexelCount - 2u;
                        sourceY = AtlasTexelCount - 1u - y;
                    }

                    for (uint32_t layerSet = 0u; layerSet < 2u; ++layerSet)
                    {
                        const uint32_t slot = layerSet * AtlasArrayLayerCount + probeIndex;
                        for (uint32_t channel = 0u; channel < 4u; ++channel)
                        {
                            const uint32_t borderIndex =
                                (slot * AtlasTexelCount * AtlasTexelCount +
                                 y * AtlasTexelCount + x) * 4u + channel;
                            const uint32_t sourceIndex =
                                (slot * AtlasTexelCount * AtlasTexelCount +
                                 sourceY * AtlasTexelCount + sourceX) * 4u + channel;
                            if (observation.IrradianceBits[borderIndex] !=
                                observation.IrradianceBits[sourceIndex])
                            {
                                std::cerr << scenario << " irradiance atlasのborderが対応するocta内側texelと不一致です\n";
                                return false;
                            }
                        }
                    }
                    for (uint32_t channel = 0u; channel < 2u; ++channel)
                    {
                        const uint32_t borderIndex =
                            (probeIndex * AtlasTexelCount * AtlasTexelCount +
                             y * AtlasTexelCount + x) * 2u + channel;
                        const uint32_t sourceIndex =
                            (probeIndex * AtlasTexelCount * AtlasTexelCount +
                             sourceY * AtlasTexelCount + sourceX) * 2u + channel;
                        if (observation.DistanceBits[borderIndex] !=
                            observation.DistanceBits[sourceIndex])
                        {
                            std::cerr << scenario << " distance atlasのborderが対応するocta内側texelと不一致です\n";
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    bool ValidateAtlasValues(const ProbeObservation& observation,
                             const ProbeObservation* previousObservation,
                             const SceneResources& scene,
                             const char* scenario,
                             uint32_t probeCount = ProbeCount,
                             bool bExpectedActive = true)
    {
        const uint32_t sampleCount = AtlasInteriorTexelCount * AtlasInteriorTexelCount;
        for (uint32_t probeIndex = 0u; probeIndex < probeCount; ++probeIndex)
        {
            const bool bActive = IsProbeActive(observation, probeIndex);
            if (bActive != bExpectedActive)
            {
                std::cerr << scenario << " probeの有効/無効が想定と異なります probe="
                          << probeIndex << " active=" << bActive << '\n';
                return false;
            }
            const Math::Vector3 probePosition(static_cast<float>(probeIndex), 0.0f, 0.0f);
            for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
            {
                const uint32_t x = sampleIndex % AtlasInteriorTexelCount + 1u;
                const uint32_t y = sampleIndex / AtlasInteriorTexelCount + 1u;
                float currentIndirect[3] = {};
                float emitterIrradiance[3] = {};
                float currentMoments[2] = {};
                ComputeCurrentIrradiance(observation, scene, probeIndex, x, y, currentIndirect);
                Math::Vector3 texelDirection;
                if (bActive && TryGetTexelDirection(x, y, texelDirection))
                {
                    ComputeProbeEmitterIrradiance(
                        scene, probePosition, texelDirection, emitterIrradiance);
                }
                ComputeCurrentMoments(observation, probeIndex, x, y, currentMoments);
                for (uint32_t layerSet = 0u; layerSet < 2u; ++layerSet)
                {
                    const bool bIndirect = layerSet == 1u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const float current = currentIndirect[channel] +
                            (bIndirect ? 0.0f : emitterIrradiance[channel]);
                        const float previous = previousObservation != nullptr
                            ? ReadIrradiance(*previousObservation, probeIndex, x, y, channel, bIndirect)
                            : current;
                        const float expected = previousObservation != nullptr
                            ? current * (1.0f - AtlasHysteresis) + previous * AtlasHysteresis
                            : current;
                        const float actual =
                            ReadIrradiance(observation, probeIndex, x, y, channel, bIndirect);
                        const float tolerance = 0.015f + std::abs(expected) * 0.015f;
                        if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
                        {
                            std::cerr << scenario << (bIndirect ? " 間接光の" : " 全体の")
                                      << "irradiance積分値が不一致です probe="
                                      << probeIndex << " texel=" << x << ',' << y
                                      << " channel=" << channel << " expected=" << expected
                                      << " actual=" << actual << '\n';
                            return false;
                        }
                    }
                    const float state = ReadIrradiance(observation, probeIndex, x, y, 3u, bIndirect);
                    if (state != (bActive ? 1.0f : 0.0f))
                    {
                        std::cerr << scenario << " irradiance atlasのprobe状態が不一致です probe="
                                  << probeIndex << " texel=" << x << ',' << y
                                  << " state=" << state << '\n';
                        return false;
                    }
                }
                for (uint32_t channel = 0u; channel < 2u; ++channel)
                {
                    const float previous = previousObservation != nullptr
                        ? ReadDistanceMoment(*previousObservation, probeIndex, x, y, channel)
                        : currentMoments[channel];
                    const float expected = previousObservation != nullptr
                        ? currentMoments[channel] * (1.0f - AtlasHysteresis) +
                              previous * AtlasHysteresis
                        : currentMoments[channel];
                    const float actual = ReadDistanceMoment(observation, probeIndex, x, y, channel);
                    const float tolerance = 0.025f + std::abs(expected) * 0.015f;
                    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
                    {
                        std::cerr << scenario << " 距離モーメントが不一致です probe="
                                  << probeIndex << " texel=" << x << ',' << y
                                  << " moment=" << channel << " expected=" << expected
                                  << " actual=" << actual << '\n';
                        return false;
                    }
                }
            }
        }
        return ValidateBorder(observation, scenario, probeCount);
    }

    bool ValidateSingleProbeBorderSample(const ProbeObservation& previousObservation,
                                         const ProbeObservation& noBounceObservation,
                                         const ProbeObservation& bounceObservation)
    {
        Math::Vector3 rayDirection;
        if (!TryGetDDGIProbeRayDirection(0u, rayDirection))
        {
            return false;
        }
        const Math::Vector3 surfaceNormal(0.0f, 0.0f, -1.0f);
        Math::Vector2 octahedralUv;
        if (!EncodeDDGIOctahedralDirection(surfaceNormal, octahedralUv))
        {
            std::cerr << "負Z方向をoctahedral UVへ変換できません\n";
            return false;
        }

        const Math::Vector2 atlasUv = MapOctahedralUvToAtlas(octahedralUv);
        const float sampleX = atlasUv.x * AtlasTexelCount - 0.5f;
        const float sampleY = atlasUv.y * AtlasTexelCount - 0.5f;
        const int32_t baseX = static_cast<int32_t>(std::floor(sampleX));
        const int32_t baseY = static_cast<int32_t>(std::floor(sampleY));
        if (baseX + 1 != static_cast<int32_t>(AtlasTexelCount) - 1 ||
            baseY + 1 != static_cast<int32_t>(AtlasTexelCount) - 1 ||
            sampleX <= static_cast<float>(baseX) || sampleY <= static_cast<float>(baseY))
        {
            std::cerr << "負Z方向のirradiance補間がatlas borderへ届きません\n";
            return false;
        }

        const DDGIProbeRayQueryResult& noBounceHit = noBounceObservation.Rays[0u];
        const DDGIProbeRayQueryResult& bounceHit = bounceObservation.Rays[0u];
        if (noBounceHit.bHit != 1u || bounceHit.bHit != 1u ||
            noBounceHit.InstanceCustomIndex != EmitterCustomIndex ||
            bounceHit.InstanceCustomIndex != EmitterCustomIndex)
        {
            std::cerr << "border sample fixtureのsurface ray hitが不正です\n";
            return false;
        }

        float borderExpected[3] = {};
        float interiorOnly[3] = {};
        bool bBorderChangesSample = false;
        bool bGpuPrefersBorderSample = false;
        const uint32_t lastInteriorTexel = AtlasTexelCount - 2u;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            borderExpected[channel] = SampleAtlasAtDirection(
                previousObservation, 0u, surfaceNormal, channel, true, true) / Pi;
            interiorOnly[channel] = ReadIrradiance(previousObservation,
                                                   0u,
                                                   lastInteriorTexel,
                                                   lastInteriorTexel,
                                                   channel,
                                                   true) / Pi;
            const float actualBounce = bounceHit.Radiance[channel] - noBounceHit.Radiance[channel];
            const float tolerance = 0.04f + std::abs(borderExpected[channel]) * 0.03f;
            if (std::abs(actualBounce - borderExpected[channel]) > tolerance)
            {
                std::cerr << "negative-Z irradiance border sampleが不一致です channel="
                          << channel << " expected=" << borderExpected[channel]
                          << " actual=" << actualBounce << '\n';
                return false;
            }
            const float borderContribution =
                std::abs(borderExpected[channel] - interiorOnly[channel]);
            if (borderContribution > 0.01f)
            {
                bBorderChangesSample = true;
                bGpuPrefersBorderSample = bGpuPrefersBorderSample ||
                    std::abs(actualBounce - borderExpected[channel]) <
                        std::abs(actualBounce - interiorOnly[channel]);
            }
        }
        if (!bBorderChangesSample || !bGpuPrefersBorderSample)
        {
            std::cerr << "GPU irradiance sampleがborder補間を選択していません border="
                      << borderExpected[0] << ',' << borderExpected[1] << ',' << borderExpected[2]
                      << " interior=" << interiorOnly[0] << ',' << interiorOnly[1] << ','
                      << interiorOnly[2] << '\n';
            return false;
        }

        std::cout << "single_probe_array_layers=" << AtlasArrayLayerCount
                  << " probes=1 negative_z_border_sample="
                  << borderExpected[0] << ',' << borderExpected[1] << ','
                  << borderExpected[2] << " interior_only="
                  << interiorOnly[0] << ',' << interiorOnly[1] << ',' << interiorOnly[2]
                  << '\n';
        return true;
    }

    bool ValidateSinglePlane(const ProbeObservation& observation)
    {
        Math::Vector3 direction;
        if (!TryGetDDGIProbeRayDirection(0u, direction))
        {
            return false;
        }
        const DDGIProbeRayQueryResult& hit = observation.Rays[0u];
        const float expectedDistance = 1.0f / direction.z;
        if (hit.bHit != 1u || hit.InstanceCustomIndex != EmitterCustomIndex ||
            hit.PrimitiveIndex != 0u || std::abs(hit.Distance - expectedDistance) > 0.01f)
        {
            std::cerr << "単一平面のhit属性または距離が不一致です\n";
            return false;
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (std::abs(hit.Radiance[channel] - EmitterRadiance[channel]) > 0.01f)
            {
                std::cerr << "単一平面のscene-linear radianceが不一致です channel="
                          << channel << " actual=" << hit.Radiance[channel] << '\n';
                return false;
            }
        }
        return true;
    }

    bool ValidateOcclusionAndBounce(const ProbeObservation& observation,
                                    const ProbeObservation& previousObservation)
    {
        Math::Vector3 direction;
        if (!TryGetDDGIProbeRayDirection(BounceRayIndex, direction))
        {
            return false;
        }
        const DDGIProbeRayQueryResult& hit = observation.Rays[BounceRayIndex];
        float occlusionPlaneOffset = 0.0f;
        if (!TryGetOcclusionPlaneOffset(occlusionPlaneOffset))
        {
            return false;
        }
        const float expectedDistance = occlusionPlaneOffset /
                                       (direction.z - direction.x);
        if (hit.bHit != 1u || hit.InstanceCustomIndex != OccluderCustomIndex ||
            hit.PrimitiveIndex != 0u || std::abs(hit.Distance - expectedDistance) > 0.01f)
        {
            std::cerr << "遮蔽ケースで手前のtriangleがray hitになりません\n";
            return false;
        }

        float expectedBounce[3] = {};
        float unweightedBounce[3] = {};
        float probeVisibility[ProbeCount] = {};
        float probeMeanDistance[ProbeCount] = {};
        float probePointDistance[ProbeCount] = {};
        if (!ComputeExpectedBounce(
                previousObservation,
                hit,
                expectedBounce,
                unweightedBounce,
                probeVisibility,
                probeMeanDistance,
                probePointDistance))
        {
            std::cerr << "前フレームirradianceの参照値を計算できません\n";
            return false;
        }
        if (!(probeVisibility[0] > 0.9f) || !(probeVisibility[1] < 0.2f))
        {
            std::cerr << "遮蔽壁によるprobe別visibility差が不足しています visibility="
                      << probeVisibility[0] << ',' << probeVisibility[1]
                      << " mean_distance=" << probeMeanDistance[0] << ','
                      << probeMeanDistance[1] << " point_distance="
                      << probePointDistance[0] << ',' << probePointDistance[1] << '\n';
            return false;
        }
        // 3成分とも可視の重み付きの参照値に一致し、少なくとも1成分で可視の重みの有無が許容差の2倍を
        // 超えて分かれる（発光面の色が赤に寄るため、弱い青の成分では差が小さい）。
        bool bVisibilityDistinguishable = false;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const float tolerance = 0.04f + std::abs(expectedBounce[channel]) * 0.03f;
            if (!(hit.Radiance[channel] > 0.0f) ||
                std::abs(hit.Radiance[channel] - expectedBounce[channel]) > tolerance)
            {
                std::cerr << "前フレームirradianceのvisibility付き1段加算値が不一致です channel="
                          << channel << " expected=" << expectedBounce[channel]
                          << " unweighted=" << unweightedBounce[channel]
                          << " actual=" << hit.Radiance[channel] << '\n';
                return false;
            }
            bVisibilityDistinguishable = bVisibilityDistinguishable ||
                std::abs(unweightedBounce[channel] - expectedBounce[channel]) >=
                    2.0f * tolerance;
        }
        if (!bVisibilityDistinguishable)
        {
            std::cerr << "可視の重みの有無で1段加算値が分かれません expected="
                      << expectedBounce[0] << ',' << expectedBounce[1] << ','
                      << expectedBounce[2] << " unweighted=" << unweightedBounce[0] << ','
                      << unweightedBounce[1] << ',' << unweightedBounce[2] << '\n';
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
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }

        const RHI::DeviceCapabilitiesA& capabilities = device->GetCapabilities();
        if (!capabilities.RayTracing.bAccelerationStructure ||
            !capabilities.RayTracing.bRayQuery ||
            !capabilities.bBufferDeviceAddress ||
            !capabilities.bShaderInt64)
        {
            return ReportGpuTestSkip(TestName, "ray query/BDA/shaderInt64機能を利用できません");
        }

        VulkanValidationErrorCapture validationErrorCapture;

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << "probe update shader用ShaderManagerを初期化できません\n";
            return 1;
        }

        FramePacket planePacket;
        FramePacket visibilitySeedPacket;
        FramePacket occlusionPacket;
        FramePacket singleProbeSeedPacket;
        FramePacket singleProbeSurfacePacket;
        FramePacket backfacingPacket;
        SceneResources planeScene;
        SceneResources visibilitySeedScene;
        SceneResources occlusionScene;
        SceneResources singleProbeSeedScene;
        SceneResources singleProbeSurfaceScene;
        SceneResources backfacingScene;
        if (!CreateTestScene(device, ProbeScene::EmitterAbove, planePacket, planeScene) ||
            !CreateTestScene(device,
                             ProbeScene::VisibilitySeed,
                             visibilitySeedPacket,
                             visibilitySeedScene) ||
            !CreateTestScene(device, ProbeScene::Occlusion, occlusionPacket, occlusionScene) ||
            !CreateTestScene(device,
                             ProbeScene::SeedFloor,
                             singleProbeSeedPacket,
                             singleProbeSeedScene,
                             1u) ||
            !CreateTestScene(device,
                             ProbeScene::EmitterAbove,
                             singleProbeSurfacePacket,
                             singleProbeSurfaceScene,
                             1u) ||
            !CreateTestScene(device,
                             ProbeScene::BackfacingFloor,
                             backfacingPacket,
                             backfacingScene))
        {
            return 1;
        }

        BufferPtr lightBuffer;
        TexturePtr environmentTexture;
        SamplerPtr environmentSampler;
        if (!CreateLightingInputs(
                device, lightBuffer, environmentTexture, environmentSampler))
        {
            return 1;
        }

        DDGIProbePass probePass;
        DDGIProbePass planeProbePass;
        DDGIProbePass singleProbePass;
        DDGIProbePass singleProbeBaselinePass;
        ProbeObservation singleProbeSeedObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           singleProbePass,
                           singleProbeSeedPacket,
                           singleProbeSeedScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           1u,
                           singleProbeSeedObservation) ||
            !ValidateAtlasValues(singleProbeSeedObservation,
                                 nullptr,
                                 singleProbeSeedScene,
                                 "single_probe_seed",
                                 1u))
        {
            return 1;
        }

        ProbeObservation singleProbeBaselineObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           singleProbeBaselinePass,
                           singleProbeSurfacePacket,
                           singleProbeSurfaceScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           1u,
                           singleProbeBaselineObservation) ||
            !ValidateSinglePlane(singleProbeBaselineObservation))
        {
            return 1;
        }

        ProbeObservation singleProbeBounceObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           singleProbePass,
                           singleProbeSurfacePacket,
                           singleProbeSurfaceScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           1u,
                           1u,
                           singleProbeBounceObservation) ||
            !ValidateSingleProbeBorderSample(singleProbeSeedObservation,
                                             singleProbeBaselineObservation,
                                             singleProbeBounceObservation) ||
            !ValidateAtlasValues(singleProbeBounceObservation,
                                 &singleProbeSeedObservation,
                                 singleProbeSurfaceScene,
                                 "single_probe_bounce",
                                 1u))
        {
            return 1;
        }
        singleProbePass.Shutdown();
        singleProbeBaselinePass.Shutdown();

        ProbeObservation planeObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           planeProbePass,
                           planePacket,
                           planeScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           ProbeCount,
                           planeObservation) ||
            !ValidateSinglePlane(planeObservation) ||
            !ValidateAtlasValues(planeObservation, nullptr, planeScene, "single_plane"))
        {
            return 1;
        }
        planeProbePass.Shutdown();

        // 表が下を向いた床の上のprobeは、rayの半分が面の裏に当たるため無効になり、発光面の直接光も持たない。
        DDGIProbePass backfacingProbePass;
        ProbeObservation backfacingObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           backfacingProbePass,
                           backfacingPacket,
                           backfacingScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           ProbeCount,
                           backfacingObservation) ||
            !ValidateAtlasValues(backfacingObservation,
                                 nullptr,
                                 backfacingScene,
                                 "backfacing_floor",
                                 ProbeCount,
                                 false))
        {
            return 1;
        }
        backfacingProbePass.Shutdown();

        ProbeObservation visibilitySeedObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           probePass,
                           visibilitySeedPacket,
                           visibilitySeedScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           0u,
                           ProbeCount,
                           visibilitySeedObservation) ||
            !ValidateAtlasValues(visibilitySeedObservation,
                                 nullptr,
                                 visibilitySeedScene,
                                 "visibility_seed"))
        {
            return 1;
        }
        uint32_t visibilityWallRayHits = 0u;
        for (uint32_t rayIndex = RayCount;
             rayIndex < ProbeCount * RayCount;
             ++rayIndex)
        {
            const DDGIProbeRayQueryResult& ray = visibilitySeedObservation.Rays[rayIndex];
            if (ray.bHit != 0u &&
                (ray.InstanceCustomIndex == VisibilityWallCustomIndex ||
                 ray.InstanceCustomIndex == VisibilityWallBackCustomIndex))
            {
                ++visibilityWallRayHits;
            }
        }
        if (visibilityWallRayHits == 0u)
        {
            std::cerr << "visibility fixtureのprobe rayが遮蔽壁にhitしません\n";
            return 1;
        }

        ProbeObservation occlusionObservation;
        if (!RunProbeFrame(device,
                           shaderManager,
                           probePass,
                           occlusionPacket,
                           occlusionScene,
                           lightBuffer,
                           environmentTexture,
                           environmentSampler,
                           1u,
                           ProbeCount,
                           occlusionObservation) ||
            !ValidateOcclusionAndBounce(occlusionObservation, visibilitySeedObservation) ||
            !ValidateAtlasValues(occlusionObservation,
                                 &visibilitySeedObservation,
                                 occlusionScene,
                                 "occlusion"))
        {
            return 1;
        }

        float oldIrradiance[3] = {};
        float blockedIrradiance[3] = {};
        float currentIrradiance[3] = {};
        float expectedBounce[3] = {};
        float unweightedBounce[3] = {};
        float probeVisibility[ProbeCount] = {};
        float probeMeanDistance[ProbeCount] = {};
        float probePointDistance[ProbeCount] = {};
        float currentMoments[2] = {};
        SampleIrradianceAtPositiveZ(visibilitySeedObservation, 0u, oldIrradiance);
        SampleIrradianceAtPositiveZ(visibilitySeedObservation, 1u, blockedIrradiance);
        ComputeCurrentIrradiance(
            occlusionObservation, occlusionScene, 0u, 3u, 3u, currentIrradiance);
        ComputeExpectedBounce(visibilitySeedObservation,
                              occlusionObservation.Rays[BounceRayIndex],
                              expectedBounce,
                              unweightedBounce,
                              probeVisibility,
                              probeMeanDistance,
                              probePointDistance);
        ComputeCurrentMoments(occlusionObservation, 0u, 3u, 3u, currentMoments);
        std::cout << "single_plane_hit=1 irradiance="
                  << ReadIrradiance(planeObservation, 0u, 3u, 3u, 0u) << ','
                  << ReadIrradiance(planeObservation, 0u, 3u, 3u, 1u) << ','
                  << ReadIrradiance(planeObservation, 0u, 3u, 3u, 2u)
                  << " distance_moments=" << ReadDistanceMoment(planeObservation, 0u, 3u, 3u, 0u)
                  << ',' << ReadDistanceMoment(planeObservation, 0u, 3u, 3u, 1u) << '\n';
        std::cout << "occlusion_custom_index="
                  << occlusionObservation.Rays[BounceRayIndex].InstanceCustomIndex
                  << " hit_distance=" << occlusionObservation.Rays[BounceRayIndex].Distance
                  << " one_bounce_radiance="
                  << occlusionObservation.Rays[BounceRayIndex].Radiance[0] << ','
                  << occlusionObservation.Rays[BounceRayIndex].Radiance[1] << ','
                  << occlusionObservation.Rays[BounceRayIndex].Radiance[2]
                  << " expected_visibility_weighted=" << expectedBounce[0] << ','
                  << expectedBounce[1] << ',' << expectedBounce[2]
                  << " expected_unweighted=" << unweightedBounce[0] << ','
                  << unweightedBounce[1] << ',' << unweightedBounce[2]
                  << " probe_visibility=" << probeVisibility[0] << ',' << probeVisibility[1]
                  << " visibility_mean_distance=" << probeMeanDistance[0] << ','
                  << probeMeanDistance[1]
                  << " visibility_wall_ray_hits=" << visibilityWallRayHits
                  << " seed_positive_z_irradiance=" << oldIrradiance[0] << ','
                  << oldIrradiance[1] << ',' << oldIrradiance[2]
                  << " blocked_positive_z_irradiance=" << blockedIrradiance[0] << ','
                  << blockedIrradiance[1] << ',' << blockedIrradiance[2]
                  << " hysteresis=" << AtlasHysteresis
                  << " current_moments=" << currentMoments[0] << ',' << currentMoments[1]
                  << '\n';

        probePass.Shutdown();
        planeProbePass.Shutdown();
        shaderManager.Shutdown();
        device->WaitIdle();
        return validationErrorCapture.VerifyNoErrors() ? 0 : 1;
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
        std::cerr << "DDGIProbeUpdateVulkanTestで例外が発生しました: "
                  << exception.what() << '\n';
        return 1;
    }
}
