// 独立PTのGPU累積、静止収束、履歴破棄と明示選択を確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkyAtmospherePass.h"
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
#include "RHI/TransientResourcePool.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

#ifdef NORVES_PATH_TRACING_OUTDOOR_TEST
    constexpr const char* TestName = "PathTracingOutdoorVulkanTest";
#else
    constexpr const char* TestName = "PathTracingVulkanTest";
#endif
    constexpr uint32_t Width = 32u;
    constexpr uint32_t Height = 32u;
    constexpr uint64_t ReadbackBytes = Width * Height * 4u * sizeof(float);

    struct Vertex
    {
        float Position[3];
    };

    bool BuildScene(const DevicePtr& device, FramePacket& packet,
                    bool bEmissive = true)
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
        snapshot.Material.EmissiveColor[0] = 1.0f;
        snapshot.Material.EmissiveColor[1] = 0.25f;
        snapshot.Material.EmissiveColor[2] = 0.0f;
        snapshot.Material.EmissiveLuminanceNits = bEmissive ? 2.0f : 0.0f;
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
                  VariableArray<float>& outPixels,
                  SkyAtmospherePass* skyPass = nullptr)
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
        context.SkyAtmosphere.Reset();
        if (skyPass)
        {
            graph.AddPass(skyPass);
        }
        graph.AddPass(&pass);
        if (!graph.Compile(context))
        {
            commandList->End();
            std::cerr << "PT render graphをコンパイルできませんでした\n";
            return false;
        }
        const RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        TexturePtr output;
        if (!result.bSuccess ||
            result.ExecutedPassCount != (skyPass ? 2u : 1u) ||
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

    int RunTest()
    {
        SceneView view;
        SceneViewSettings settings;
        if (!view.Initialize(settings) || view.GetPassCount() != 0u)
        {
            std::cerr << "既定SceneViewがPTを選択しました\n";
            return 1;
        }
        view.SetupPathTracingPipeline();
        if (view.GetPassCount() != 2u || !view.FindPass("SkyAtmospherePass") ||
            !view.FindPass("PathTracingPass"))
        {
            std::cerr << "PTの明示選択が独立passを登録しませんでした\n";
            return 1;
        }
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
            std::cerr << "PT用のFramePacket RT snapshotを構築できませんでした\n";
            return 1;
        }
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionZ = -2.0f;
        camera.ForwardZ = 1.0f;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = 1.0f;

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << "PT RT pipeline/SBTを作成できませんでした\n";
            return 1;
        }
        RenderGraph graph;
        graph.Initialize(nullptr);
        VariableArray<float> first;
        VariableArray<float> second;
        VariableArray<float> prior;
        VariableArray<float> current;
        for (uint64_t frame = 1u; frame <= 16u; ++frame)
        {
            if (!RunFrame(device, graph, pass, context, frame, 1u, 1u, current) ||
                pass.GetAccumulatedSampleCount() != frame)
            {
                std::cerr << "静止時に1 sample/frameで累積しませんでした\n";
                return 1;
            }
            if (frame == 1u)
            {
                first = current;
            }
            if (frame == 2u)
            {
                second = current;
            }
            if (frame == 15u)
            {
                prior = current;
            }
        }
        const size_t center = (Height / 2u * Width + Width / 2u) * 4u;
        if (current[center] < 1.0f || current[center + 1u] < 0.2f ||
            current[center] <= current[center + 1u])
        {
            std::cerr << "FramePacket材質のemissive hitをPTで確認できませんでした\n";
            return 1;
        }
        const double firstChange = MeanAbsoluteChange(first, second);
        const double lateChange = MeanAbsoluteChange(prior, current);
        std::cout << "pt_static_samples=16 first_change=" << firstChange
                  << " late_change=" << lateChange
                  << " alternating_frame_slots=true\n";
        if (firstChange <= 0.0 || lateChange >= firstChange)
        {
            std::cerr << "静止画素の累積平均が収束しませんでした\n";
            return 1;
        }
        if (!RunFrame(device, graph, pass, context, 19u, 1u, 1u, current) ||
            pass.GetAccumulatedSampleCount() != 17u)
        {
            std::cerr << "描画packet番号が飛んだ静止frameで履歴を維持しませんでした\n";
            return 1;
        }
        std::cout << "pt_frame_gap_keeps_history=true\n";

        camera.PositionX = 0.1f;
        if (!RunFrame(device, graph, pass, context, 20u, 1u, 1u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "camera変更時にPT履歴を破棄しませんでした\n";
            return 1;
        }
        if (!RunFrame(device, graph, pass, context, 21u, 2u, 1u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "scene revision変更時にPT履歴を破棄しませんでした\n";
            return 1;
        }
        packet.RayTracingScene.Instances[0].Material.BaseColor[0] = 0.4f;
        if (!RunFrame(device, graph, pass, context, 22u, 2u, 1u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "材質snapshot変更時にPT履歴を破棄しませんでした\n";
            return 1;
        }
        packet.RayTracingScene.Instances[0].Instance.transform[3] = 0.25f;
        AccelerationStructureBuildDesc movedBuild;
        movedBuild.type = AccelerationStructureType::TopLevel;
        movedBuild.destination = packet.RayTracingScene.TopLevel;
        movedBuild.instances.push_back(packet.RayTracingScene.Instances[0].Instance);
        if (!packet.RayTracingScene.TopLevel->Build(movedBuild))
        {
            std::cerr << "移動したPT用TLASを再構築できませんでした\n";
            return 1;
        }
        if (!RunFrame(device, graph, pass, context, 23u, 2u, 1u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "geometry snapshot変更時にPT履歴を破棄しませんでした\n";
            return 1;
        }
        if (!RunFrame(device, graph, pass, context, 24u, 2u, 2u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "light revision変更時にPT履歴を破棄しませんでした\n";
            return 1;
        }
        std::cout << "pt_camera_scene_material_geometry_light_resets=true\n";

        TransientResourcePool pool;
        if (!pool.Initialize(device->GetResourceAllocator(), 1u))
        {
            std::cerr << "PT選択経路の一時resource poolを初期化できませんでした\n";
            return 1;
        }
        graph.Initialize(&pool);
        pool.BeginFrame(0u);
        SceneRenderer renderer;
        if (!renderer.Initialize(device.get(), nullptr, &pool))
        {
            std::cerr << "PT選択経路のSceneRendererを初期化できませんでした\n";
            return 1;
        }
        CommandListPtr selectedCommand = device->CreateCommandList();
        context.CommandList = selectedCommand.get();
        context.Renderer = &renderer;
        context.Graph = &graph;
        context.TransientPool = &pool;
        context.FrameNumber = 25u;
        context.SceneRevision = 2u;
        context.LightRevision = 2u;
        context.PhysicalLighting.Begin(25u, 7u, 3u);
        selectedCommand->SetFrameIndex(0u);
        selectedCommand->Begin();
        view.Render(context);
        const bool bSelectedPipelineSucceeded =
            view.GetFrameSceneColorTexture() && view.GetFrameOutputTexture() &&
            view.GetFrameOutputTexture() != view.GetFrameSceneColorTexture();
        if (!bSelectedPipelineSucceeded)
        {
            std::cerr << "明示選択したPTからToneMappingまで実行できませんでした"
                      << " graph_passes=" << graph.GetPassCount()
                      << " executed=" << graph.GetLastExecutedPassCount()
                      << " graph_success=" << graph.GetLastExecutionResult().bSuccess
                      << " scene_color=" << static_cast<bool>(view.GetFrameSceneColorTexture())
                      << " output=" << static_cast<bool>(view.GetFrameOutputTexture()) << '\n';
        }
        selectedCommand->End();
        selectedCommand->Submit(true);
        if (bSelectedPipelineSucceeded)
        {
            std::cout << "pt_selected_scene_view_pipeline=true\n";
        }

        view.Shutdown();
        renderer.Shutdown();
        pass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
        device->WaitIdle();
        return bSelectedPipelineSucceeded ? 0 : 1;
    }

#ifdef NORVES_PATH_TRACING_OUTDOOR_TEST
    bool MatchesSkyParameters(const SkyAtmosphereParameters& actual,
                              const SkyAtmosphereParameters& expected)
    {
        return actual.bEnabled == expected.bEnabled &&
            actual.SunAltitudeDegrees == expected.SunAltitudeDegrees &&
            actual.SunAzimuthDegrees == expected.SunAzimuthDegrees &&
            actual.SunLuminanceNits == expected.SunLuminanceNits &&
            actual.PlanetRadiusMeters == expected.PlanetRadiusMeters &&
            actual.AtmosphereHeightMeters == expected.AtmosphereHeightMeters &&
            actual.RayleighScaleHeightMeters == expected.RayleighScaleHeightMeters &&
            actual.MieScaleHeightMeters == expected.MieScaleHeightMeters &&
            actual.MieAnisotropy == expected.MieAnisotropy &&
            actual.GroundAlbedo.x == expected.GroundAlbedo.x &&
            actual.GroundAlbedo.y == expected.GroundAlbedo.y &&
            actual.GroundAlbedo.z == expected.GroundAlbedo.z;
    }

    float MaxCornerRadiance(const VariableArray<float>& pixels)
    {
        const size_t corners[] = {0u, Width - 1u,
                                  (Height - 1u) * Width, Width * Height - 1u};
        float maximum = 0.0f;
        for (size_t corner : corners)
        {
            const size_t offset = corner * 4u;
            maximum = std::max(maximum, pixels[offset] + pixels[offset + 1u] +
                                            pixels[offset + 2u]);
        }
        return maximum;
    }

    float CenterRadiance(const VariableArray<float>& pixels)
    {
        const size_t offset = (Height / 2u * Width + Width / 2u) * 4u;
        return pixels[offset] + pixels[offset + 1u] + pixels[offset + 2u];
    }

    int RunOutdoorTest()
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
        if (!BuildScene(device, packet, false))
        {
            std::cerr << "屋外PT用のRT snapshotを構築できませんでした\n";
            return 1;
        }
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionZ = -2.0f;
        camera.ForwardZ = 1.0f;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = 1.0f;
        camera.PreExposure = 1.0f / 50000.0f;

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        context.SkyAtmosphereSnapshot.bEnabled = true;
        context.SkyAtmosphereSnapshot.SunAltitudeDegrees = 89.0f;
        context.SkyAtmosphereSnapshot.SunLuminanceNits = 1000.0f;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        SkyAtmospherePass skyPass;
        PathTracingPass pathPass;
        if (!skyPass.Initialize(context) || !pathPass.Initialize(context))
        {
            std::cerr << "屋外PTの空LUTまたはRT pipelineを初期化できませんでした\n";
            return 1;
        }
        RenderGraph graph;
        graph.Initialize(nullptr);
        VariableArray<float> pixels;
        struct TimeCase
        {
            const char* Name;
            float Altitude;
            float Azimuth;
        };
        const TimeCase cases[] = {
            {"morning", 8.0f, -60.0f},
            {"noon", 65.0f, -90.0f},
            {"evening", 22.0f, -120.0f}};
        float cornerRadiance[3] = {};
        float centerRadiance[3] = {};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
            sky.bEnabled = true;
            sky.SunAltitudeDegrees = cases[index].Altitude;
            sky.SunAzimuthDegrees = cases[index].Azimuth;
            sky.RayleighScaleHeightMeters = 8500.0f;
            sky.MieAnisotropy = 0.65f;
            sky.GroundAlbedo = Math::Vector3(0.2f, 0.15f, 0.1f);
            packet.Scene.SkyAtmosphere = sky;
            if (!RunFrame(device, graph, pathPass, context, index + 1u,
                          1u, 1u, pixels, &skyPass) ||
                pathPass.GetAccumulatedSampleCount() != 1u)
            {
                std::cerr << "時刻変更時にPTを描画・履歴破棄できませんでした\n";
                return 1;
            }
            const SkyAtmosphereParameters expected =
                SanitizeSkyAtmosphereParameters(sky);
            const float expectedSun =
                ComputeSunDiskPreExposedLuminance(expected, camera.PreExposure);
            const SkyRadianceSample zenith = EvaluateHillaireSkyReference(
                expected, Math::Vector3::UnitY);
            if (!MatchesSkyParameters(skyPass.GetLastParameters(), expected) ||
                !MatchesSkyParameters(context.SkyAtmosphere.Parameters, expected) ||
                !context.SkyAtmosphere.bValid ||
                std::abs(context.SkyAtmosphere.SunDiskPreExposedLuminance -
                         expectedSun) > 0.1f ||
                !zenith.bValid || !std::isfinite(zenith.Radiance.x) ||
                !std::isfinite(zenith.Radiance.y) ||
                !std::isfinite(zenith.Radiance.z))
            {
                std::cerr << "R2空スナップショットとPTの入力が一致しませんでした\n";
                return 1;
            }
            cornerRadiance[index] = MaxCornerRadiance(pixels);
            centerRadiance[index] = CenterRadiance(pixels);
            const Math::Vector3 sunDirection =
                MakeSunDirectionFromAltitudeAzimuth(expected.SunAltitudeDegrees,
                                                    expected.SunAzimuthDegrees);
            const float expectedSurface =
                ComputeSunDiskIrradiance(expected) * camera.PreExposure *
                std::max(-sunDirection.z, 0.0f) * 1.2f /
                3.14159265358979323846f;
            std::cout << cases[index].Name << "_sky=" << cornerRadiance[index]
                      << " solar_surface=" << centerRadiance[index]
                      << " expected_solar_surface=" << expectedSurface
                      << " sun_disk=" << expectedSun << '\n';
            if (cornerRadiance[index] <= 0.0f ||
                centerRadiance[index] <= 0.0f ||
                std::abs(centerRadiance[index] - expectedSurface) > 0.03f)
            {
                std::cerr << "空missまたは太陽照度の解析値とPTが一致しませんでした\n";
                return 1;
            }
        }
        if (std::abs(cornerRadiance[0] - cornerRadiance[1]) < 0.0001f &&
            std::abs(cornerRadiance[1] - cornerRadiance[2]) < 0.0001f)
        {
            std::cerr << "時刻変更が空miss radianceに反映されませんでした\n";
            return 1;
        }

        if (!RunFrame(device, graph, pathPass, context, 4u,
                      1u, 1u, pixels) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            context.SkyAtmosphere.bValid || MaxCornerRadiance(pixels) > 0.0001f)
        {
            std::cerr << "同一空設定のLUT欠落時に履歴を破棄できませんでした\n";
            return 1;
        }
        if (!RunFrame(device, graph, pathPass, context, 5u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            !context.SkyAtmosphere.bValid ||
            std::abs(MaxCornerRadiance(pixels) - cornerRadiance[2]) > 0.0001f)
        {
            std::cerr << "同一空設定のLUT復帰時に履歴を破棄できませんでした\n";
            return 1;
        }

        packet.Scene.SkyAtmosphere.SunAltitudeDegrees = cases[1].Altitude;
        packet.Scene.SkyAtmosphere.SunAzimuthDegrees = 90.0f;
        if (!RunFrame(device, graph, pathPass, context, 6u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            centerRadiance[1] <= CenterRadiance(pixels) + 0.01f)
        {
            std::cerr << "太陽を裏面へ移しても直接照明が減りませんでした\n";
            return 1;
        }
        packet.Scene.SkyAtmosphere.bEnabled = false;
        if (!RunFrame(device, graph, pathPass, context, 7u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            context.SkyAtmosphere.bSnapshotEnabled ||
            std::abs(MaxCornerRadiance(pixels) - 0.15f) > 0.0001f)
        {
            std::cerr << "空無効時のPT環境光へ戻りませんでした\n";
            return 1;
        }
        packet.Scene.SkyAtmosphere.bEnabled = true;
        if (!RunFrame(device, graph, pathPass, context, 8u,
                      1u, 1u, pixels) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            context.SkyAtmosphere.bValid || MaxCornerRadiance(pixels) > 0.0001f)
        {
            std::cerr << "空要求時のLUT欠落を黒へ戻せませんでした\n";
            return 1;
        }
        std::cout << "sky_parameter_parity=3 solar_sampling=true "
                     "sky_disabled_fallback=true sky_missing_fallback=true "
                     "sky_recovery_reset=true\n";
        skyPass.Shutdown();
        pathPass.Shutdown();
        graph.Shutdown();
        shaderManager.Shutdown();
        device->WaitIdle();
        return 0;
    }
#endif
}

int main()
{
#ifdef NORVES_PATH_TRACING_OUTDOOR_TEST
    return RunOutdoorTest();
#else
    return RunTest();
#endif
}
