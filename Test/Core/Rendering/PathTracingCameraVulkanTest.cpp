// 薄レンズとシャッターの静止・移動基準画像を実GPUで検証する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingCamera.h"
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
                 VariableArray<float>& pixels,
                 uint32_t samplesPerFrame = 1u)
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
        pass.SetSamplesPerFrame(samplesPerFrame);
        RenderGraph graph;
        graph.Initialize(nullptr);
        for (uint64_t frame = 1u; frame <= 32u; ++frame)
        {
            context.SnapshotDeltaTime = previousCamera
                                            ? 1.0f / 60.0f
                                            : (frame % 2u == 0u ? 1.0f / 55.0f
                                                               : 1.0f / 60.0f);
            if (!RunFrame(device, graph, pass, context, frame, 1u, 1u, pixels) ||
                pass.GetAccumulatedSampleCount() != frame * samplesPerFrame)
            {
                std::cerr << "PTカメラの試料を累積できませんでした\n";
                return false;
            }
        }
        pass.Shutdown();
        graph.Shutdown();
        return true;
    }

    /**
     * @brief 背景を引いた赤の強度で重み付けした、重心まわりの画素距離の二乗平均
     *
     * 画像の外周2画素に重みの0.1%を超える強度があれば、像が画面外へはみ出しているとして失敗にする。
     */
    bool MeasureIntensitySecondMoment(const VariableArray<float>& pixels, double background,
                                      double& outSecondMoment)
    {
        double weightSum = 0.0;
        double borderWeight = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        const auto weightAt = [&](uint32_t x, uint32_t y)
        {
            const double value = static_cast<double>(pixels[(y * Width + x) * 4u]) - background;
            return value > 1.0e-4 ? value : 0.0;
        };
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const double weight = weightAt(x, y);
                weightSum += weight;
                sumX += weight * (x + 0.5);
                sumY += weight * (y + 0.5);
                if (x < 2u || y < 2u || x + 2u >= Width || y + 2u >= Height)
                {
                    borderWeight += weight;
                }
            }
        }
        if (!(weightSum > 0.0) || borderWeight > weightSum * 1.0e-3)
        {
            return false;
        }
        const double centerX = sumX / weightSum;
        const double centerY = sumY / weightSum;
        double moment = 0.0;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const double dx = x + 0.5 - centerX;
                const double dy = y + 0.5 - centerY;
                moment += weightAt(x, y) * (dx * dx + dy * dy);
            }
        }
        outSecondMoment = moment / weightSum;
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
        // 1frameに4試料を束ねても、カメラ標本（レンズ位置）はdispatchごとの連続した添字で引く。
        // 32回のdispatchで同じ32個のレンズ位置を使うため、画素内の試料だけが増えて基準と一致する。
        VariableArray<float> staticBatched;
        if (!Capture(device, shaderManager, packet, camera, nullptr, staticBatched, 4u) ||
            !CheckBlocks("static_batched4", staticBatched, staticGolden))
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
        // シャッター時刻も同じく、束ねた試料の数に関係なくdispatchごとの連続した添字で引く。
        VariableArray<float> motionBatched;
        if (!Capture(device, shaderManager, packet, camera, &previousCamera, motionBatched, 4u) ||
            !CheckBlocks("motion_batched4", motionBatched, motionGolden))
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

        // レンズ標本の分布: 小さな発光三角形を焦点面に置いた像と焦点から外した像では、強度の
        // 二次モーメントの差がレンズ標本による像のずれの二乗平均になる（形の寄与は同じで打ち消す）。
        // レンズ上の一様な標本ならCoC半径の二乗の1/2。1frameに2・4・5試料を束ねても、dispatchごとの
        // 連続した添字で同じ分布になることを確かめる（試料数おきの添字では2試料で24%大きい）。
        std::memset(snapshot.Instance.transform, 0, sizeof(snapshot.Instance.transform));
        snapshot.Instance.transform[0] = 0.05f;
        snapshot.Instance.transform[5] = 0.05f;
        snapshot.Instance.transform[10] = 0.05f;
        std::memcpy(snapshot.PreviousTransform, snapshot.Instance.transform,
                    sizeof(snapshot.PreviousTransform));
        snapshot.bHasPreviousTransform = false;
        AccelerationStructureBuildDesc lensBuild;
        lensBuild.type = AccelerationStructureType::TopLevel;
        lensBuild.destination = packet.RayTracingScene.TopLevel;
        lensBuild.instances.push_back(snapshot.Instance);
        if (!packet.RayTracingScene.TopLevel->Build(lensBuild))
        {
            std::cerr << "レンズ分布用のTLASを構築できません\n";
            return 1;
        }
        CameraProxy focusedCamera = camera;
        focusedCamera.PositionX = 0.0f;
        focusedCamera.FieldOfView = 10.0f;
        focusedCamera.FocusDistance = 2.0f;
        CameraProxy defocusedCamera = focusedCamera;
        defocusedCamera.FocusDistance = 1.2f;
        const double cocPixels = ComputePathTracingCocPixels(defocusedCamera, 2.0f, Height);
        // 期待値は、連続したdispatch添字0〜31のカメラ標本によるずれの二乗平均（32個のHalton標本の
        // 離散値。連続な一様分布ならCoC半径の二乗の1/2）。ずれは焦点距離・像距離から求める。
        double expectedRms = 0.0;
        // 薄レンズの像は焦点距離の設定で倍率が変わる（像距離に合わせて画角を1-f/焦点距離倍に狭める）。
        // 焦点を合わせた像の形の二次モーメントを焦点を外した像の倍率へ換算してから差を取る。
        double shapeMagnificationSquared = 1.0;
        {
            const double focalLength = 0.024 /
                (2.0 * std::tan(defocusedCamera.FieldOfView * 3.14159265358979323846 / 360.0));
            const double magnification =
                (1.0 - focalLength / focusedCamera.FocusDistance) /
                (1.0 - focalLength / defocusedCamera.FocusDistance);
            shapeMagnificationSquared = magnification * magnification;
            const double focus = defocusedCamera.FocusDistance;
            const double imageDistance = focalLength * focus / (focus - focalLength);
            const double pixelsPerLensUnit =
                std::abs(1.0 - 2.0 / focus) * imageDistance / 2.0 * Height / 0.024;
            double sum = 0.0;
            for (uint32_t index = 0u; index < 32u; ++index)
            {
                const PathTracingCameraSample lens =
                    SamplePathTracingCamera(defocusedCamera, nullptr, 0.0f, index);
                const double radius = std::hypot(lens.LensOffset[0], lens.LensOffset[1]);
                sum += radius * pixelsPerLensUnit * radius * pixelsPerLensUnit;
            }
            expectedRms = std::sqrt(sum / 32.0);
        }
        for (const uint32_t samplesPerFrame : {1u, 2u, 4u, 5u})
        {
            VariableArray<float> focusedPixels;
            VariableArray<float> defocusedPixels;
            double focusedMoment = 0.0;
            double defocusedMoment = 0.0;
            if (!Capture(device, shaderManager, packet, focusedCamera, nullptr, focusedPixels,
                         samplesPerFrame) ||
                !Capture(device, shaderManager, packet, defocusedCamera, nullptr, defocusedPixels,
                         samplesPerFrame) ||
                !MeasureIntensitySecondMoment(focusedPixels, 0.05, focusedMoment) ||
                !MeasureIntensitySecondMoment(defocusedPixels, 0.05, defocusedMoment) ||
                !(defocusedMoment > focusedMoment * shapeMagnificationSquared))
            {
                std::cerr << "レンズ分布の像を測れません samples_per_frame=" << samplesPerFrame
                          << '\n';
                return 1;
            }
            const double measuredRms =
                std::sqrt(defocusedMoment - focusedMoment * shapeMagnificationSquared);
            const double relativeError = std::abs(measuredRms - expectedRms) / expectedRms;
            std::cout << "lens_spread samples_per_frame=" << samplesPerFrame
                      << " coc_px=" << cocPixels
                      << " uniform_rms_px=" << 0.5 * cocPixels / std::sqrt(2.0)
                      << " expected_rms_px=" << expectedRms
                      << " measured_rms_px=" << measuredRms
                      << " relative_error=" << relativeError << '\n';
            // 許容差3%: 画素の標本化と重心まわりのモーメントの推定誤差。試料数おきの添字では24%ずれる。
            if (relativeError > 0.03)
            {
                std::cerr << "1frameに束ねた試料でレンズ標本の分布が一様になりません\n";
                return 1;
            }
        }

        // シャッター時刻の分布: ピンホールカメラで小さな発光三角形をシャッター区間にx方向へ動かすと、
        // 静止の像に対する強度の二次モーメントの増分は移動量の二乗×シャッター時刻の分散になる。
        // シャッター区間が1frame全体なら時刻は一様で分散1/12。時刻はbase 5のHalton列なので、
        // 5試料を束ねた試料数おきの添字では区間の一部に偏る。
        CameraProxy pinholeCamera = focusedCamera;
        pinholeCamera.FocusDistance = 0.0f;
        const double pixelsPerUnit = Height /
            (2.0 * 2.0 * std::tan(pinholeCamera.FieldOfView * 3.14159265358979323846 / 360.0));
        // 期待値は、連続したdispatch添字0〜31のシャッター時刻の標準偏差×移動量（32個のHalton標本の
        // 離散値。連続な一様分布なら移動量/√12）。
        double expectedShutterRms = 0.0;
        {
            double sum = 0.0;
            double sumSquares = 0.0;
            for (uint32_t index = 0u; index < 32u; ++index)
            {
                const double time = SamplePathTracingCamera(
                    pinholeCamera, &pinholeCamera, 1.0f / 60.0f, index).ShutterTime;
                sum += time;
                sumSquares += time * time;
            }
            const double mean = sum / 32.0;
            expectedShutterRms = 0.1 * pixelsPerUnit * std::sqrt(sumSquares / 32.0 - mean * mean);
        }
        for (const uint32_t samplesPerFrame : {1u, 2u, 4u, 5u})
        {
            snapshot.bHasPreviousTransform = false;
            snapshot.Instance.transform[3] = 0.0f;
            snapshot.PreviousTransform[3] = 0.0f;
            AccelerationStructureBuildDesc stillBuild;
            stillBuild.type = AccelerationStructureType::TopLevel;
            stillBuild.destination = packet.RayTracingScene.TopLevel;
            stillBuild.instances.push_back(snapshot.Instance);
            VariableArray<float> stillPixels;
            double stillMoment = 0.0;
            if (!packet.RayTracingScene.TopLevel->Build(stillBuild) ||
                !Capture(device, shaderManager, packet, pinholeCamera, &pinholeCamera, stillPixels,
                         samplesPerFrame) ||
                !MeasureIntensitySecondMoment(stillPixels, 0.05, stillMoment))
            {
                std::cerr << "静止した小さな発光三角形の像を測れません\n";
                return 1;
            }
            snapshot.bHasPreviousTransform = true;
            snapshot.PreviousTransform[3] = -0.05f;
            snapshot.Instance.transform[3] = 0.05f;
            AccelerationStructureBuildDesc movingBuild;
            movingBuild.type = AccelerationStructureType::TopLevel;
            movingBuild.destination = packet.RayTracingScene.TopLevel;
            movingBuild.instances.push_back(snapshot.Instance);
            VariableArray<float> movingPixels;
            double movingMoment = 0.0;
            if (!packet.RayTracingScene.TopLevel->Build(movingBuild) ||
                !Capture(device, shaderManager, packet, pinholeCamera, &pinholeCamera, movingPixels,
                         samplesPerFrame) ||
                !MeasureIntensitySecondMoment(movingPixels, 0.05, movingMoment) ||
                !(movingMoment > stillMoment))
            {
                std::cerr << "動く小さな発光三角形の像を測れません samples_per_frame="
                          << samplesPerFrame << '\n';
                return 1;
            }
            const double measuredRms = std::sqrt(movingMoment - stillMoment);
            const double relativeError =
                std::abs(measuredRms - expectedShutterRms) / expectedShutterRms;
            std::cout << "shutter_spread samples_per_frame=" << samplesPerFrame
                      << " uniform_rms_px=" << 0.1 * pixelsPerUnit / std::sqrt(12.0)
                      << " expected_rms_px=" << expectedShutterRms
                      << " measured_rms_px=" << measuredRms
                      << " relative_error=" << relativeError << '\n';
            // 許容差3%: レンズと同じ。試料数おきの添字では5試料で標準偏差が約1/5になる。
            if (relativeError > 0.03)
            {
                std::cerr << "1frameに束ねた試料でシャッター時刻の分布が一様になりません\n";
                return 1;
            }
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
