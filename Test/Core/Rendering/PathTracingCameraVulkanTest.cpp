// 薄レンズとシャッターの静止・移動基準画像を実GPUで検証する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#if defined(NORVES_EXR_OUTPUT_TEST)
#include "Rendering/PathTracingExrOutput.h"
#endif
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IAccelerationStructure.h"
#include "RHI/IBuffer.h"
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
#if defined(NORVES_EXR_OUTPUT_TEST)
#include <limits>
#endif

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

#if defined(NORVES_EXR_OUTPUT_TEST)
    constexpr const char* TestName = "PathTracingExrOutputTest";
#else
    constexpr const char* TestName = "PathTracingCameraVulkanTest";
#endif
    constexpr uint32_t Width = 32u;
    constexpr uint32_t Height = 32u;
    constexpr uint64_t ReadbackBytes = Width * Height * 4u * sizeof(float);

    struct Vertex
    {
        float Position[3];
    };

    bool BuildScene(const DevicePtr& device, FramePacket& packet)
    {
        const Vertex vertices[3] = {
            {{-1.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, 0.0f}},
            {{0.0f, 1.0f, 0.0f}}};
        const uint32_t indices[3] = {0u, 1u, 2u};
        BufferDesc vertexDesc(sizeof(vertices),
                              ResourceUsage::VertexBuffer |
                                  ResourceUsage::BufferDeviceAddress,
                              true, "PathTracingTest.Vertices");
        BufferDesc indexDesc(sizeof(indices),
                             ResourceUsage::IndexBuffer |
                                 ResourceUsage::BufferDeviceAddress,
                             true, "PathTracingTest.Indices");
        BufferPtr vertexBuffer = device->CreateBuffer(vertexDesc);
        BufferPtr indexBuffer = device->CreateBuffer(indexDesc);
        if (!vertexBuffer || !indexBuffer ||
            vertexBuffer->GetDeviceAddress() == 0u ||
            indexBuffer->GetDeviceAddress() == 0u)
        {
            return false;
        }
        vertexBuffer->Update(vertices, sizeof(vertices));
        indexBuffer->Update(indices, sizeof(indices));

        AccelerationStructureDesc bottomDesc;
        bottomDesc.type = AccelerationStructureType::BottomLevel;
        bottomDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, 1u, true});
        AccelerationStructurePtr bottom = device->CreateAccelerationStructure(bottomDesc);
        AccelerationStructureGeometryDesc geometry;
        geometry.type = AccelerationStructureGeometryType::Triangles;
        geometry.opaque = true;
        geometry.triangles.vertexBuffer = vertexBuffer;
        geometry.triangles.vertexCount = 3u;
        geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        geometry.triangles.indexBuffer = indexBuffer;
        geometry.triangles.indexCount = 3u;
        geometry.triangles.indexFormat = IndexType::Uint32;
        AccelerationStructureBuildDesc bottomBuild;
        bottomBuild.type = AccelerationStructureType::BottomLevel;
        bottomBuild.destination = bottom;
        bottomBuild.geometries.push_back(geometry);
        if (!bottom || !bottom->Build(bottomBuild))
        {
            return false;
        }

        AccelerationStructureDesc topDesc;
        topDesc.type = AccelerationStructureType::TopLevel;
        topDesc.maxInstanceCount = 1u;
        AccelerationStructurePtr top = device->CreateAccelerationStructure(topDesc);
        AccelerationStructureInstanceDesc instance;
        instance.bottomLevel = bottom;
        instance.customIndex = 0u;
        instance.disableTriangleFacingCull = true;
        AccelerationStructureBuildDesc topBuild;
        topBuild.type = AccelerationStructureType::TopLevel;
        topBuild.destination = top;
        topBuild.instances.push_back(instance);
        if (!top || !top->Build(topBuild))
        {
            return false;
        }

        RayTracingSceneInstanceSnapshot snapshot;
        snapshot.AccelerationStructureVertexBuffer = vertexBuffer;
        snapshot.AccelerationStructureIndexBuffer = indexBuffer;
        snapshot.BottomLevel = bottom;
        snapshot.VertexCount = 3u;
        snapshot.VertexStride = sizeof(Vertex);
        snapshot.IndexCount = 3u;
        snapshot.Instance = instance;
        snapshot.Material.BaseColor[0] = 0.8f;
        snapshot.Material.BaseColor[1] = 0.3f;
        snapshot.Material.BaseColor[2] = 0.1f;
        // パストレーサーはGBufferと同じくinstance色×アルベドtextureを表面色にする。
        snapshot.Material.ObjectColor[0] = 0.8f;
        snapshot.Material.ObjectColor[1] = 0.3f;
        snapshot.Material.ObjectColor[2] = 0.1f;
        snapshot.Material.EmissiveColor[0] = 1.0f;
        snapshot.Material.EmissiveColor[1] = 0.25f;
        snapshot.Material.EmissiveColor[2] = 0.0f;
        snapshot.Material.EmissiveLuminanceNits = 2.0f;
        packet.RayTracingScene.TopLevel = top;
        packet.RayTracingScene.Instances.push_back(snapshot);
        return packet.HasCompleteRayTracingScene();
    }

    bool RecordHostReadBarrier(const CommandListPtr& commandList,
                               const BufferPtr& readback)
    {
        TSharedPtr<Vulkan::VulkanCommandList> nativeCommand =
            DynamicPointerCast<Vulkan::VulkanCommandList>(commandList);
        TSharedPtr<Vulkan::VulkanBuffer> nativeBuffer =
            DynamicPointerCast<Vulkan::VulkanBuffer>(readback);
        if (!nativeCommand || !nativeBuffer)
        {
            return false;
        }
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = nativeBuffer->GetVkBuffer();
        barrier.offset = 0u;
        barrier.size = ReadbackBytes;
        nativeCommand->GetVkCommandBuffer().pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            {}, 0u, nullptr, 1u, &barrier, 0u, nullptr);
        return true;
    }

    bool RunFrame(const DevicePtr& device,
                  RenderGraph& graph,
                  PathTracingPass& pass,
                  ViewRenderContext& context,
                  uint64_t frameNumber,
                  uint64_t sceneRevision,
                  uint64_t lightRevision,
                  VariableArray<float>& outPixels)
    {
        CommandListPtr commandList = device->CreateCommandList();
        BufferDesc readbackDesc(ReadbackBytes, ResourceUsage::TransferDst,
                                true, "PathTracingTest.Readback");
        BufferPtr readback = device->CreateBuffer(readbackDesc);
        if (!commandList || !readback)
        {
            return false;
        }
        context.CommandList = commandList.get();
        context.FrameIndex = static_cast<uint32_t>(frameNumber % 2u);
        context.FrameNumber = frameNumber;
        context.SceneRevision = sceneRevision;
        context.LightRevision = lightRevision;
        context.PhysicalLighting.Begin(frameNumber, 7u, 3u);
        commandList->SetFrameIndex(context.FrameIndex);
        commandList->Begin();
        graph.BeginFrame(frameNumber);
        graph.AddPass(&pass);
        if (!graph.Compile(context))
        {
            commandList->End();
            std::cerr << "PT render graphをコンパイルできませんでした\n";
            return false;
        }
        const RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        TexturePtr output;
        if (!result.bSuccess || result.ExecutedPassCount != 1u ||
            !result.TryGetTexture(RenderGraphResourceNames::SceneColor, output) ||
            output != pass.GetAccumulatedTexture())
        {
            commandList->End();
            std::cerr << "PTのSceneColorが公開されませんでした\n";
            return false;
        }
        commandList->TextureBarrier(output, ResourceState::ShaderResource,
                                    ResourceState::CopySource);
        commandList->BufferBarrier(readback, ResourceState::Undefined,
                                   ResourceState::CopyDest, 0u, ReadbackBytes);
        commandList->CopyTextureToBuffer(output, readback, Width, Height, 0u);
        commandList->TextureBarrier(output, ResourceState::CopySource,
                                    ResourceState::ShaderResource);
        if (!RecordHostReadBarrier(commandList, readback))
        {
            commandList->End();
            return false;
        }
        commandList->End();
        commandList->Submit(true);

        const void* mapped = readback->Map(0u, ReadbackBytes);
        if (!mapped)
        {
            return false;
        }
        outPixels.resize(Width * Height * 4u);
        std::memcpy(outPixels.data(), mapped, ReadbackBytes);
        readback->Unmap();
        for (float value : outPixels)
        {
            if (!std::isfinite(value) || value < 0.0f)
            {
                std::cerr << "PTの画素に非有限値または負値があります\n";
                return false;
            }
        }
        return true;
    }

    double MeanAbsoluteChange(const VariableArray<float>& a,
                              const VariableArray<float>& b)
    {
        double total = 0.0;
        for (size_t index = 0u; index < a.size(); index += 4u)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                total += std::abs(static_cast<double>(a[index + channel]) -
                                  static_cast<double>(b[index + channel]));
            }
        }
        return total / (Width * Height * 3u);
    }

    bool Capture(const DevicePtr& device, ShaderManager& shaderManager,
                 FramePacket& packet, CameraProxy& camera,
                 const CameraProxy* previousCamera,
                 VariableArray<float>& pixels)
    {
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.PreviousMainCamera = previousCamera;
        context.SnapshotDeltaTime = 1.0f / 60.0f;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << "PTカメラ用pipelineを初期化できませんでした\n";
            return false;
        }
        // 固定基準は空が無効なときの一様環境0.05を背景にする（既定の環境光は黒）。
        PathTracingEnvironment environment;
        environment.Mode = PathTracingEnvironmentMode::Uniform;
        for (float& channel : environment.UniformRadiance)
        {
            channel = 0.05f;
        }
        pass.SetEnvironment(environment);
        RenderGraph graph;
        graph.Initialize(nullptr);
        for (uint64_t frame = 1u; frame <= 32u; ++frame)
        {
            context.SnapshotDeltaTime = previousCamera
                                            ? 1.0f / 60.0f
                                            : (frame % 2u == 0u ? 1.0f / 55.0f
                                                               : 1.0f / 60.0f);
            if (!RunFrame(device, graph, pass, context, frame, 1u, 1u, pixels) ||
                pass.GetAccumulatedSampleCount() != frame)
            {
                std::cerr << "PTカメラの32試料を累積できませんでした\n";
                return false;
            }
        }
        pass.Shutdown();
        graph.Shutdown();
        return true;
    }

    bool CheckBlocks(const char* name, const VariableArray<float>& pixels,
                     const float expected[16])
    {
        std::cout << name << "_golden=";
        for (uint32_t blockY = 0u; blockY < 4u; ++blockY)
        {
            for (uint32_t blockX = 0u; blockX < 4u; ++blockX)
            {
                double sum = 0.0;
                for (uint32_t y = blockY * 8u; y < (blockY + 1u) * 8u; ++y)
                {
                    for (uint32_t x = blockX * 8u; x < (blockX + 1u) * 8u; ++x)
                    {
                        sum += pixels[(y * Width + x) * 4u];
                    }
                }
                const double actual = sum / 64.0;
                std::cout << (blockY || blockX ? "," : "") << actual;
                if (std::abs(actual - expected[blockY * 4u + blockX]) > 0.02)
                {
                    std::cerr << "\nPTカメラの基準画像と一致しません\n";
                    return false;
                }
            }
        }
        std::cout << '\n';
        return true;
    }

#if defined(NORVES_EXR_OUTPUT_TEST)
    bool CheckExrOutput(const VariableArray<float>& firstPixels,
                        const VariableArray<float>& secondPixels)
    {
        constexpr uint32_t FixedSeed = 0x6a09e667u;
        String firstDirectory(NORVES_EXR_OUTPUT_ROOT);
        firstDirectory += "/first";
        String secondDirectory(NORVES_EXR_OUTPUT_ROOT);
        secondDirectory += "/second";
        String knownDirectory(NORVES_EXR_OUTPUT_ROOT);
        knownDirectory += "/known";
        if (!WritePathTracingExrFrame(firstDirectory.c_str(), "triangle",
                                      FixedSeed, 32u, 7u, Width, Height,
                                      firstPixels.data(), firstPixels.size()) ||
            !WritePathTracingExrFrame(secondDirectory.c_str(), "triangle",
                                      FixedSeed, 32u, 7u, Width, Height,
                                      secondPixels.data(), secondPixels.size()))
        {
            std::cerr << "固定seedのPT画像をEXR連番へ保存できません\n";
            return false;
        }

        constexpr uint32_t KnownWidth = 3u;
        constexpr uint32_t KnownHeight = 17u;
        VariableArray<float> knownPixels(KnownWidth * KnownHeight * 4u);
        for (uint32_t y = 0u; y < KnownHeight; ++y)
        {
            for (uint32_t x = 0u; x < KnownWidth; ++x)
            {
                const size_t index = (y * KnownWidth + x) * 4u;
                knownPixels[index] = 0.25f * static_cast<float>(x + 1u) +
                    0.01f * static_cast<float>(y);
                knownPixels[index + 1u] = 1.25f + 0.125f * static_cast<float>(y);
                knownPixels[index + 2u] = 0.5f * static_cast<float>(x + y);
                knownPixels[index + 3u] = 1.0f;
            }
        }
        if (!WritePathTracingExrFrame(knownDirectory.c_str(), "known",
                                      42u, 3u, 9u, KnownWidth, KnownHeight,
                                      knownPixels.data(), knownPixels.size()))
        {
            std::cerr << "既知RGB値のEXRを保存できません\n";
            return false;
        }
        knownPixels[0] = std::numeric_limits<float>::quiet_NaN();
        if (WritePathTracingExrFrame(knownDirectory.c_str(), "known",
                                     42u, 3u, 9u, KnownWidth, KnownHeight,
                                     knownPixels.data(), knownPixels.size()))
        {
            std::cerr << "NaNを含むEXRが公開されました\n";
            return false;
        }
        knownPixels[0] = std::numeric_limits<float>::infinity();
        if (WritePathTracingExrFrame(knownDirectory.c_str(), "known",
                                     42u, 3u, 9u, KnownWidth, KnownHeight,
                                     knownPixels.data(), knownPixels.size()))
        {
            std::cerr << "Infを含むEXRが公開されました\n";
            return false;
        }
        std::cout << "exr_fixed_seed=0x6a09e667 exr_spp=32 exr_frame=7 "
                  << "exr_nonfinite_rejected=true\n";
        return true;
    }
#endif

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
        if (!device || !device->GetCapabilities().RayTracing.bAccelerationStructure ||
            !device->GetCapabilities().RayTracing.bRayTracingPipeline ||
            !device->GetCapabilities().bBufferDeviceAddress)
        {
            return ReportGpuTestSkip(TestName, "RT pipeline/BDAを利用できません");
        }
        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            return 1;
        }
        FramePacket packet;
        if (!BuildScene(device, packet))
        {
            return 1;
        }
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionZ = -2.0f;
        camera.ForwardZ = 1.0f;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = 1.0f;
        camera.FocusDistance = 0.3f;
        camera.Aperture = 0.7f;
        camera.ShutterSpeed = 1.0f / 60.0f;
        // 発光はプリエクスポージャを掛けて評価するため、2 nitsの基準値をそのまま比べられるよう露出1にする。
        camera.PreExposure = 1.0f;
        VariableArray<float> staticPixels;
        VariableArray<float> staticRepeat;
        if (!Capture(device, shaderManager, packet, camera, nullptr, staticPixels) ||
            !Capture(device, shaderManager, packet, camera, nullptr, staticRepeat))
        {
            return 1;
        }
        for (size_t index = 0u; index < staticPixels.size(); ++index)
        {
            if (std::abs(staticPixels[index] - staticRepeat[index]) > 1.0e-5f)
            {
                std::cerr << "静止画像の固定seed再実行が一致しません\n";
                return 1;
            }
        }
#if defined(NORVES_EXR_OUTPUT_TEST)
        if (!CheckExrOutput(staticPixels, staticRepeat))
        {
            return 1;
        }
#endif
        std::cout << "varying_delta_time_keeps_static_history=true\n";
        const float staticGolden[16] = {
            0.05f, 0.42604f, 0.412437f, 0.05f,
            0.0519434f, 1.42298f, 1.40161f, 0.0509717f,
            0.438672f, 2.01668f, 2.01279f, 0.429927f,
            1.17229f, 1.7553f, 1.7553f, 1.14314f};
        if (!CheckBlocks("static", staticPixels, staticGolden))
        {
            return 1;
        }

        RayTracingSceneInstanceSnapshot& snapshot = packet.RayTracingScene.Instances[0];
        std::memcpy(snapshot.PreviousTransform, snapshot.Instance.transform,
                    sizeof(snapshot.PreviousTransform));
        snapshot.PreviousTransform[3] = -0.4f;
        snapshot.bHasPreviousTransform = true;
        snapshot.Instance.transform[3] = 0.4f;
        AccelerationStructureBuildDesc currentBuild;
        currentBuild.type = AccelerationStructureType::TopLevel;
        currentBuild.destination = packet.RayTracingScene.TopLevel;
        currentBuild.instances.push_back(snapshot.Instance);
        if (!packet.RayTracingScene.TopLevel->Build(currentBuild))
        {
            std::cerr << "現フレームのTLASを更新できません\n";
            return 1;
        }
        camera.PositionX = 0.15f;
        CameraProxy previousCamera = camera;
        previousCamera.PositionX = -0.15f;
        VariableArray<float> motionPixels;
        VariableArray<float> motionRepeat;
        if (!Capture(device, shaderManager, packet, camera, &previousCamera, motionPixels) ||
            !Capture(device, shaderManager, packet, camera, &previousCamera, motionRepeat))
        {
            return 1;
        }
        for (size_t index = 0u; index < motionPixels.size(); ++index)
        {
            if (std::abs(motionPixels[index] - motionRepeat[index]) > 1.0e-5f)
            {
                std::cerr << "移動画像の固定seed再実行が一致しません\n";
                return 1;
            }
        }
        const float motionGolden[16] = {
            0.05f, 0.383286f, 0.459077f, 0.05f,
            0.0917822f, 1.30444f, 1.4191f, 0.106357f,
            0.471709f, 1.91757f, 1.94575f, 0.56499f,
            1.09261f, 1.7553f, 1.7553f, 1.16646f};
        if (!CheckBlocks("motion", motionPixels, motionGolden))
        {
            return 1;
        }
        const double imageDifference = MeanAbsoluteChange(staticPixels, motionPixels);
        std::cout << "static_motion_difference=" << imageDifference << '\n';
        if (imageDifference < 0.01)
        {
            std::cerr << "移動画像が静止画像と区別できません\n";
            return 1;
        }
        shaderManager.Shutdown();
        device->WaitIdle();
        return 0;
    }
}

int main()
{
    return RunTest();
}
