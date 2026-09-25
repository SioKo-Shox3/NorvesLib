// 独立PTのGPU累積、静止収束、履歴破棄と明示選択を確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/SkySunLight.h"
#include "Rendering/SkyAtmospherePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/VolumetricFogScattering.h"
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

#ifdef NORVES_PATH_TRACING_VOLUMETRIC_TEST
    constexpr const char* TestName = "PathTracingVolumetricTest";
#elif defined(NORVES_PATH_TRACING_OUTDOOR_TEST)
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
        // パストレーサーはGBufferと同じくinstance色×アルベドtextureを表面色にする。
        snapshot.Material.ObjectColor[0] = 0.8f;
        snapshot.Material.ObjectColor[1] = 0.3f;
        snapshot.Material.ObjectColor[2] = 0.1f;
        snapshot.Material.EmissiveColor[0] = 1.0f;
        snapshot.Material.EmissiveColor[1] = 0.25f;
        snapshot.Material.EmissiveColor[2] = 0.0f;
        snapshot.Material.EmissiveLuminanceNits = bEmissive ? 2.0f : 0.0f;
        packet.RayTracingScene.TopLevel = top;
        packet.RayTracingScene.Instances.push_back(snapshot);
        return packet.HasCompleteRayTracingScene();
    }

    // 既存の検証は空が無効なときに一様環境0.05を前提にする（既定の環境光は黒）。
    PathTracingEnvironment MakeUniformEnvironment(float radiance)
    {
        PathTracingEnvironment environment;
        environment.Mode = PathTracingEnvironmentMode::Uniform;
        for (float& channel : environment.UniformRadiance)
        {
            channel = radiance;
        }
        return environment;
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
        // 発光はプリエクスポージャを掛けて評価するため、2 nitsの基準値をそのまま比べられるよう露出1にする。
        camera.PreExposure = 1.0f;

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
        pass.SetEnvironment(MakeUniformEnvironment(0.05f));
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

        // 1回のdispatchで4試料を引く累積は、1試料ずつ4フレーム累積した結果と同じ試料番号と乱数を使う。
        VariableArray<float> fourFrames;
        for (uint64_t frame = 25u; frame <= 28u; ++frame)
        {
            if (!RunFrame(device, graph, pass, context, frame, 2u, 3u, fourFrames) ||
                pass.GetAccumulatedSampleCount() != frame - 24u)
            {
                std::cerr << "1試料ずつの累積を作れませんでした\n";
                return 1;
            }
        }
        pass.SetSamplesPerFrame(4u);
        VariableArray<float> oneDispatch;
        if (!RunFrame(device, graph, pass, context, 29u, 2u, 4u, oneDispatch) ||
            pass.GetAccumulatedSampleCount() != 4u)
        {
            std::cerr << "1回のdispatchで4試料を累積しませんでした samples="
                      << pass.GetAccumulatedSampleCount() << "\n";
            return 1;
        }
        {
            float maxDifference = 0.0f;
            for (size_t index = 0u; index < oneDispatch.size(); ++index)
            {
                maxDifference = std::max(maxDifference,
                                         std::abs(oneDispatch[index] - fourFrames[index]));
            }
            std::cout << "pt_samples_per_frame=4 max_difference_from_four_frames=" << maxDifference
                      << "\n";
            if (maxDifference > 1.0e-4f)
            {
                std::cerr << "1回4試料の累積が1試料ずつ4フレームと一致しません\n";
                return 1;
            }
        }
        pass.SetSamplesPerFrame(1u);

        // 正射影は画素ごとに近平面から視線方向へ平行に光線を出す。CPUで同じ逆ビュー射影から
        // 光線を作り、三角形の内側は材質のアルベド、外側は0（検証出力は命中面だけの値）を確かめる。
        camera.Projection = ProjectionType::Orthographic;
        camera.OrthoWidth = 3.0f;
        camera.OrthoHeight = 3.0f;
        pass.SetDebugOutput(PathTracingDebugOutput::Albedo);
        if (!RunFrame(device, graph, pass, context, 30u, 2u, 4u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "正射影カメラでPTを描画できませんでした\n";
            return 1;
        }
        {
            const CameraViewConstants orthoView =
                CameraViewConstants::BuildForDevice(camera, 1.0f, device.get());
            float inverseViewProjection[16] = {};
            orthoView.CopyShaderInverseViewProjection(inverseViewProjection);
            const auto unproject = [&](double u, double v, double depth, double (&out)[3])
            {
                const double ndc[4] = {u * 2.0 - 1.0, v * 2.0 - 1.0, depth, 1.0};
                double point[4] = {};
                for (uint32_t row = 0u; row < 4u; ++row)
                {
                    for (uint32_t column = 0u; column < 4u; ++column)
                    {
                        point[row] += inverseViewProjection[column * 4u + row] * ndc[column];
                    }
                }
                for (uint32_t axis = 0u; axis < 3u; ++axis)
                {
                    out[axis] = point[axis] / point[3];
                }
            };
            // 画素の4隅から出す平行光線がz=0平面で三角形(-1,-1)(1,-1)(0,1)の内側か。
            const auto cornerInside = [&](double u, double v)
            {
                double nearPoint[3];
                double farPoint[3];
                unproject(u, v, 0.0, nearPoint);
                unproject(u, v, 1.0, farPoint);
                const double t = -nearPoint[2] / (farPoint[2] - nearPoint[2]);
                // 形状変更の検証でinstanceをx方向へ動かしているため、その平行移動を戻して比べる。
                const double x = nearPoint[0] + (farPoint[0] - nearPoint[0]) * t -
                                 packet.RayTracingScene.Instances[0].Instance.transform[3];
                const double y = nearPoint[1] + (farPoint[1] - nearPoint[1]) * t -
                                 packet.RayTracingScene.Instances[0].Instance.transform[7];
                return y > -1.0 && 2.0 * x + y < 1.0 && -2.0 * x + y < 1.0;
            };
            uint32_t insidePixels = 0u;
            uint32_t outsidePixels = 0u;
            const float albedo[3] = {0.8f, 0.3f, 0.1f};
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    uint32_t insideCorners = 0u;
                    for (uint32_t corner = 0u; corner < 4u; ++corner)
                    {
                        insideCorners += cornerInside(
                            (x + static_cast<double>(corner & 1u)) / Width,
                            (y + static_cast<double>(corner >> 1u)) / Height) ? 1u : 0u;
                    }
                    if (insideCorners != 0u && insideCorners != 4u)
                    {
                        continue;
                    }
                    const float* pixel = current.data() + (static_cast<size_t>(y) * Width + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const float expected = insideCorners == 4u ? albedo[channel] : 0.0f;
                        if (std::abs(pixel[channel] - expected) > 1.0e-5f)
                        {
                            std::cerr << "正射影の画素が期待値と一致しません pixel=(" << x << ','
                                      << y << ") measured=" << pixel[channel]
                                      << " expected=" << expected << "\n";
                            return 1;
                        }
                    }
                    (insideCorners == 4u ? insidePixels : outsidePixels) += 1u;
                }
            }
            std::cout << "pt_orthographic inside_pixels=" << insidePixels
                      << " outside_pixels=" << outsidePixels << "\n";
            if (insidePixels < 150u || outsidePixels < 400u)
            {
                std::cerr << "正射影の比較画素が足りません\n";
                return 1;
            }

            // 画素中心の標本化では縁の画素も含めて全画素が画素中心の内外で決まり、試料を重ねても
            // 部分被覆の値にならない（ラスタのGBufferと同じ標本位置）。
            pass.SetPixelSampling(PathTracingPixelSampling::Center);
            for (uint64_t frame = 31u; frame <= 34u; ++frame)
            {
                if (!RunFrame(device, graph, pass, context, frame, 2u, 4u, current) ||
                    pass.GetAccumulatedSampleCount() != frame - 30u)
                {
                    std::cerr << "画素中心の標本化で累積できませんでした\n";
                    return 1;
                }
            }
            uint32_t edgePixels = 0u;
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    const bool bInside = cornerInside((x + 0.5) / Width, (y + 0.5) / Height);
                    uint32_t insideCorners = 0u;
                    for (uint32_t corner = 0u; corner < 4u; ++corner)
                    {
                        insideCorners += cornerInside(
                            (x + static_cast<double>(corner & 1u)) / Width,
                            (y + static_cast<double>(corner >> 1u)) / Height) ? 1u : 0u;
                    }
                    edgePixels += insideCorners != 0u && insideCorners != 4u ? 1u : 0u;
                    const float* pixel = current.data() + (static_cast<size_t>(y) * Width + x) * 4u;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const float expected = bInside ? albedo[channel] : 0.0f;
                        if (std::abs(pixel[channel] - expected) > 1.0e-5f)
                        {
                            std::cerr << "画素中心の標本化の画素が期待値と一致しません pixel=(" << x
                                      << ',' << y << ") measured=" << pixel[channel]
                                      << " expected=" << expected << "\n";
                            return 1;
                        }
                    }
                }
            }
            std::cout << "pt_pixel_center_sampling edge_pixels=" << edgePixels << "\n";
            if (edgePixels < 20u)
            {
                std::cerr << "画素中心の標本化を確かめる縁の画素が足りません\n";
                return 1;
            }
            pass.SetPixelSampling(PathTracingPixelSampling::Box);
        }
        camera.Projection = ProjectionType::Perspective;

        // 1次命中距離の検証出力は、透視カメラの位置から画素中心の光線が三角形と交わる点までの距離に
        // なり、三角形の外は0になる。CPUで同じ逆ビュー射影から光線を作ってz=0平面との交点を求める。
        pass.SetDebugOutput(PathTracingDebugOutput::HitDistance);
        pass.SetPixelSampling(PathTracingPixelSampling::Center);
        if (!RunFrame(device, graph, pass, context, 35u, 2u, 4u, current) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "1次命中距離の検証出力を描画できませんでした\n";
            return 1;
        }
        {
            const CameraViewConstants perspectiveView =
                CameraViewConstants::BuildForDevice(camera, 1.0f, device.get());
            float inverseViewProjection[16] = {};
            perspectiveView.CopyShaderInverseViewProjection(inverseViewProjection);
            const double cameraPosition[3] = {camera.PositionX, camera.PositionY, camera.PositionZ};
            uint32_t hitPixels = 0u;
            uint32_t missPixels = 0u;
            double maximumRelativeError = 0.0;
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    const double ndc[4] = {(x + 0.5) / Width * 2.0 - 1.0,
                                           (y + 0.5) / Height * 2.0 - 1.0, 1.0, 1.0};
                    double farPoint[4] = {};
                    for (uint32_t row = 0u; row < 4u; ++row)
                    {
                        for (uint32_t column = 0u; column < 4u; ++column)
                        {
                            farPoint[row] += inverseViewProjection[column * 4u + row] * ndc[column];
                        }
                    }
                    double direction[3] = {};
                    for (uint32_t axis = 0u; axis < 3u; ++axis)
                    {
                        direction[axis] = farPoint[axis] / farPoint[3] - cameraPosition[axis];
                    }
                    const double t = -cameraPosition[2] / direction[2];
                    const double hitX = cameraPosition[0] + direction[0] * t -
                                        packet.RayTracingScene.Instances[0].Instance.transform[3];
                    const double hitY = cameraPosition[1] + direction[1] * t -
                                        packet.RayTracingScene.Instances[0].Instance.transform[7];
                    // 三角形の縁から離れた画素だけを比べる（縁の判定の丸めを除く）。
                    const double margin = 0.02;
                    const bool bInside = hitY > -1.0 + margin && 2.0 * hitX + hitY < 1.0 - margin &&
                                         -2.0 * hitX + hitY < 1.0 - margin;
                    const bool bOutside = hitY < -1.0 - margin || 2.0 * hitX + hitY > 1.0 + margin ||
                                          -2.0 * hitX + hitY > 1.0 + margin;
                    if (!bInside && !bOutside)
                    {
                        continue;
                    }
                    const float* pixel = current.data() + (static_cast<size_t>(y) * Width + x) * 4u;
                    const double expected =
                        bInside ? t * std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                                direction[2] * direction[2])
                                : 0.0;
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        const double error = std::abs(pixel[channel] - expected);
                        const double relative = bInside ? error / expected : error;
                        maximumRelativeError = std::max(maximumRelativeError, relative);
                        if (relative > 1.0e-4)
                        {
                            std::cerr << "1次命中距離が期待値と一致しません pixel=(" << x << ',' << y
                                      << ") measured=" << pixel[channel] << " expected=" << expected
                                      << "\n";
                            return 1;
                        }
                    }
                    (bInside ? hitPixels : missPixels) += 1u;
                }
            }
            std::cout << "pt_hit_distance hit_pixels=" << hitPixels << " miss_pixels=" << missPixels
                      << " max_relative_error=" << maximumRelativeError << "\n";
            if (hitPixels < 100u || missPixels < 100u)
            {
                std::cerr << "1次命中距離を確かめる画素が足りません\n";
                return 1;
            }
        }
        pass.SetPixelSampling(PathTracingPixelSampling::Box);
        pass.SetDebugOutput(PathTracingDebugOutput::None);

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

#ifdef NORVES_PATH_TRACING_VOLUMETRIC_TEST
    int RunVolumetricTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }
        String reason;
        if (!CanCreateVulkanDeviceForGpuTest(reason))
        {
            return ReportGpuTestSkip(TestName, reason.c_str());
        }
        RHIDeviceDesc desc;
        desc.Api = GraphicsAPI::Vulkan;
        desc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(desc);
        if (!device || !device->GetCapabilities().RayTracing.bAccelerationStructure ||
            !device->GetCapabilities().RayTracing.bRayTracingPipeline ||
            !device->GetCapabilities().bBufferDeviceAddress)
        {
            return ReportGpuTestSkip(TestName, "RT pipeline/BDAを利用できません");
        }
        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaders;
        if (!shaders.Initialize(device.get(), shaderDirectory))
        {
            return 1;
        }
        FramePacket packet;
        if (!BuildScene(device, packet))
        {
            std::cerr << "霧検証用RT snapshotを構築できませんでした\n";
            return 1;
        }
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionZ = -2.0f;
        camera.ForwardZ = 1.0f;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = 1.0f;
        camera.PreExposure = 1.0f;
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaders;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotLightProxies = &packet.Scene.LightProxies;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        SkyAtmospherePass skyPass;
        if (!pass.Initialize(context) || !skyPass.Initialize(context))
        {
            std::cerr << "霧検証用PT/空パスを初期化できませんでした\n";
            return 1;
        }
        // 表面はラスタの検証mode 253と同じ純Lambertにし、方向光の表面直接照明を解析値で比べる。
        pass.SetEnvironment(MakeUniformEnvironment(0.05f));
        pass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
        RenderGraph graph;
        graph.Initialize(nullptr);
        VariableArray<float> baseline;
        VariableArray<float> pixels;
        const size_t center = (Height / 2u * Width + Width / 2u) * 4u;
        if (!RunFrame(device, graph, pass, context, 1u, 1u, 1u, baseline))
        {
            std::cerr << "霧無効の基準画素を取得できませんでした\n";
            return 1;
        }
        VolumetricFogParameters fog = MakeDefaultVolumetricFogParameters();
        fog.bEnabled = true;
        fog.DensityAtBaseHeight = 0.15f;
        fog.HeightFalloffPerUnit = 0.0f;
        packet.Scene.SetVolumetricFogParameters(fog);
        packet.Scene.FogColorR = 0.4f;
        packet.Scene.FogColorG = 0.2f;
        packet.Scene.FogColorB = 0.1f;
        const float fogColor[] = {0.4f, 0.2f, 0.1f};
        auto checkFog = [&](uint64_t frame, float tolerance)
        {
            if (!RunFrame(device, graph, pass, context, frame, 1u, 1u, pixels) ||
                pass.GetAccumulatedSampleCount() != 1u)
            {
                return false;
            }
            const float transmission = ComputeHeightFogTransmittance(
                packet.Scene.VolumetricFog, camera.PositionY, 0.0f, 2.0f);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const float expected = baseline[center + channel] * transmission +
                    fogColor[channel] * (1.0f - transmission);
                if (std::abs(pixels[center + channel] - expected) > tolerance)
                {
                    std::cerr << "霧透過率がR3解析値と一致しませんでした"
                              << " channel=" << channel
                              << " measured=" << pixels[center + channel]
                              << " expected=" << expected << '\n';
                    return false;
                }
            }
            std::cout << "fog_density=" << packet.Scene.VolumetricFog.DensityAtBaseHeight
                      << " fog_base_height=" << packet.Scene.VolumetricFog.BaseHeight
                      << " fog_falloff=" << packet.Scene.VolumetricFog.HeightFalloffPerUnit
                      << " transmittance=" << transmission << '\n';
            return true;
        };
        if (!checkFog(2u, 0.01f))
        {
            return 1;
        }
        fog.DensityAtBaseHeight = 0.3f;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!checkFog(3u, 0.01f))
        {
            return 1;
        }
        fog.DensityAtBaseHeight = 0.15f;
        fog.BaseHeight = 1.0f;
        fog.HeightFalloffPerUnit = 0.25f;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!checkFog(4u, 0.02f))
        {
            return 1;
        }
        fog.BaseHeight = 0.0f;
        fog.HeightFalloffPerUnit = 0.0f;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!checkFog(5u, 0.01f))
        {
            return 1;
        }
        const float noLight[] = {
            pixels[center], pixels[center + 1u], pixels[center + 2u]};
        LightProxy light;
        light.LightId = 99u;
        light.Type = LightType::Directional;
        light.DirectionZ = 1.0f;
        light.DirectionY = 0.0f;
        light.ColorR = 1.0f;
        light.ColorG = 0.5f;
        light.ColorB = 0.25f;
        light.CanonicalIntensity = 100.0f;
        packet.Scene.LightProxies.push_back(light);
        if (!RunFrame(device, graph, pass, context, 6u, 1u, 1u, pixels) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "方向光の霧単一散乱を評価できませんでした\n";
            return 1;
        }
        const float g = VolumetricFogDetail::ScatteringAnisotropy;
        const float phase = (1.0f - g * g) /
            (12.5663706f * std::pow(1.0f + g * g + 2.0f * g, 1.5f));
        const float transmission = ComputeHeightFogTransmittance(
            packet.Scene.VolumetricFog, camera.PositionY, 0.0f, 2.0f);
        const float lightColor[] = {1.0f, 0.5f, 0.25f};
        // 表面の方向光はラスタと同じく色度（Y=1）×照度。視線と同じ向きから当たり、余弦は1。
        const float lightLuminance = 0.2126f * lightColor[0] + 0.7152f * lightColor[1] +
                                     0.0722f * lightColor[2];
        const float surfaceAlbedo[] = {0.8f, 0.3f, 0.1f};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const float surfaceDirect = surfaceAlbedo[channel] / 3.14159265358979323846f *
                100.0f * lightColor[channel] / lightLuminance;
            const float expected = 100.0f * lightColor[channel] * phase *
                (1.0f - transmission) + transmission * surfaceDirect;
            const float measured = pixels[center + channel] - noLight[channel];
            std::cout << "fog_scattering_channel=" << channel
                      << " measured=" << measured << " expected=" << expected << '\n';
            if (std::abs(measured - expected) > 0.025f)
            {
                std::cerr << "方向光の単一散乱がR3位相関数と一致しませんでした\n";
                return 1;
            }
        }
        packet.Scene.LightProxies[0].DirectionZ = -1.0f;
        if (!RunFrame(device, graph, pass, context, 7u, 1u, 1u, pixels) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "方向光の霧遮蔽を評価できませんでした\n";
            return 1;
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (std::abs(pixels[center + channel] - noLight[channel]) > 0.0001f)
            {
                std::cerr << "遮蔽された方向光が霧へ散乱しました\n";
                return 1;
            }
        }
        fog.bEnabled = false;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!RunFrame(device, graph, pass, context, 8u, 1u, 1u, pixels) ||
            pass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "霧無効時にPT基準へ戻れませんでした\n";
            return 1;
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            if (std::abs(pixels[center + channel] -
                         baseline[center + channel]) > 0.0001f)
            {
                std::cerr << "霧無効時の画素が基準と異なります\n";
                return 1;
            }
        }
        packet.Scene.LightProxies.clear();
        camera.PreExposure = 1.0f / 50000.0f;
        SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = true;
        sky.SunAltitudeDegrees = 65.0f;
        packet.Scene.SkyAtmosphere = sky;
        fog.bEnabled = true;
        fog.DensityAtBaseHeight = 0.3f;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!RunFrame(device, graph, pass, context, 9u, 1u, 1u, pixels, &skyPass) ||
            pass.GetAccumulatedSampleCount() != 1u ||
            !context.SkyAtmosphere.bValid)
        {
            std::cerr << "空放射を使う霧を描画できませんでした\n";
            return 1;
        }
        const float fogSkyCenter = pixels[center] + pixels[center + 1u] +
                                   pixels[center + 2u];
        const float fogSkyCorner = pixels[0u] + pixels[1u] + pixels[2u];
        fog.bEnabled = false;
        packet.Scene.SetVolumetricFogParameters(fog);
        if (!RunFrame(device, graph, pass, context, 10u, 1u, 1u, pixels, &skyPass) ||
            pass.GetAccumulatedSampleCount() != 1u ||
            !context.SkyAtmosphere.bValid)
        {
            std::cerr << "霧無効時にR2空を描画できませんでした\n";
            return 1;
        }
        const float clearSkyCenter = pixels[center] + pixels[center + 1u] +
                                     pixels[center + 2u];
        const float clearSkyCorner = pixels[0u] + pixels[1u] + pixels[2u];
        std::cout << "fog_sky_surface_difference="
                  << std::abs(fogSkyCenter - clearSkyCenter)
                  << " fog_sky_miss=" << fogSkyCorner
                  << " sky_miss_difference="
                  << std::abs(fogSkyCorner - clearSkyCorner) << '\n';
        if (fogSkyCorner <= 0.0f ||
            std::abs(fogSkyCenter - clearSkyCenter) <= 0.05f ||
            std::abs(fogSkyCorner - clearSkyCorner) > 0.0001f)
        {
            std::cerr << "霧と空の同時評価がR3の背景規約と一致しませんでした\n";
            return 1;
        }
        packet.Scene.SkyAtmosphere.bEnabled = false;
        // 環境光は物理値にプリエクスポージャを掛けて評価する。
        const float expectedEnvironment = 0.15f * camera.PreExposure;
        if (!RunFrame(device, graph, pass, context, 11u, 1u, 1u, pixels) ||
            pass.GetAccumulatedSampleCount() != 1u ||
            std::abs(pixels[0u] + pixels[1u] + pixels[2u] - expectedEnvironment) >
                expectedEnvironment * 0.001f)
        {
            std::cerr << "空無効時にPT環境光へ戻れませんでした\n";
            return 1;
        }
        std::cout << "fog_r3_parameter_parity=true finite_transmittance=true "
                     "single_scattering=true fog_disabled_fallback=true "
                     "sky_disabled_fallback=true "
                     "fog_shadow_visibility=true sky_fog_color=true\n";
        skyPass.Shutdown();
        pass.Shutdown();
        graph.Shutdown();
        shaders.Shutdown();
        device->WaitIdle();
        return 0;
    }
#endif

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

    // 屋外テストの面（反射率(0.8, 0.3, 0.1)の拡散面）を太陽が照らしたときのRGB和。
    // 太陽は地表照度（大気の透過率込み、チャンネルごと）で照らす。
    float ExpectedSolarSurface(const SkyAtmosphereParameters& sky, float preExposure)
    {
        const Math::Vector3 sunDirection =
            MakeSunDirectionFromAltitudeAzimuth(sky.SunAltitudeDegrees, sky.SunAzimuthDegrees);
        const Math::Vector3 illuminance = ComputeSunGroundIlluminance(sky);
        return (0.8f * illuminance.x + 0.3f * illuminance.y + 0.1f * illuminance.z) *
               preExposure * std::max(-sunDirection.z, 0.0f) / 3.14159265358979323846f;
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
        // 太陽照度の解析値は純Lambertで比べる（ラスタの検証mode 253と同じ表面）。
        pathPass.SetEnvironment(MakeUniformEnvironment(0.05f));
        pathPass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
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
            const float expectedSurface = ExpectedSolarSurface(expected, camera.PreExposure);
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

        camera.PreExposure = 1.0f / 10000.0f;
        if (!RunFrame(device, graph, pathPass, context, 6u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            !context.SkyAtmosphere.bSunDiskSaturated)
        {
            std::cerr << "太陽ディスクのfp16飽和ケースを描画できませんでした\n";
            return 1;
        }
        const SkyAtmosphereParameters evening =
            SanitizeSkyAtmosphereParameters(packet.Scene.SkyAtmosphere);
        const float saturatedExpectedSurface =
            ExpectedSolarSurface(evening, camera.PreExposure);
        const float saturatedSurface = CenterRadiance(pixels);
        std::cout << "saturated_solar_surface=" << saturatedSurface
                  << " expected_saturated_solar_surface="
                  << saturatedExpectedSurface << " display_sun_disk="
                  << context.SkyAtmosphere.SunDiskPreExposedLuminance << '\n';
        if (std::abs(saturatedSurface - saturatedExpectedSurface) > 0.03f ||
            context.SkyAtmosphere.SunDiskPreExposedLuminance >=
                ComputeSunDiskPreExposedLuminance(evening, camera.PreExposure))
        {
            std::cerr << "飽和した表示ディスクがPT直接照明を減らしました\n";
            return 1;
        }
        camera.PreExposure = 1.0f / 50000.0f;

        packet.Scene.SkyAtmosphere.SunAltitudeDegrees = cases[1].Altitude;
        packet.Scene.SkyAtmosphere.SunAzimuthDegrees = 90.0f;
        if (!RunFrame(device, graph, pathPass, context, 7u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            centerRadiance[1] <= CenterRadiance(pixels) + 0.01f)
        {
            std::cerr << "太陽を裏面へ移しても直接照明が減りませんでした\n";
            return 1;
        }
        packet.Scene.SkyAtmosphere.bEnabled = false;
        const float expectedEnvironment = 0.15f * camera.PreExposure;
        if (!RunFrame(device, graph, pathPass, context, 8u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            context.SkyAtmosphere.bSnapshotEnabled ||
            std::abs(MaxCornerRadiance(pixels) - expectedEnvironment) >
                expectedEnvironment * 0.001f)
        {
            std::cerr << "空無効時のPT環境光へ戻りませんでした\n";
            return 1;
        }
        packet.Scene.SkyAtmosphere.bEnabled = true;
        if (!RunFrame(device, graph, pathPass, context, 9u,
                      1u, 1u, pixels) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            context.SkyAtmosphere.bValid || MaxCornerRadiance(pixels) > 0.0001f)
        {
            std::cerr << "空要求時のLUT欠落を黒へ戻せませんでした\n";
            return 1;
        }
        // 空の太陽の方向光（予約LightId）を光源表へ加えても、PTは同じ太陽を円盤の光源標本だけで
        // 数える（詰めた光源表が変わらないので累積履歴も続く）。同じ値の通常の方向光を加えると、
        // 太陽が二つになって面が明るくなり、光源表の変化で履歴を捨てる。
        packet.Scene.SkyAtmosphere.SunAltitudeDegrees = cases[1].Altitude;
        packet.Scene.SkyAtmosphere.SunAzimuthDegrees = cases[1].Azimuth;
        if (!RunFrame(device, graph, pathPass, context, 10u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u)
        {
            std::cerr << "空の太陽の方向光の比較の基準を描画できませんでした\n";
            return 1;
        }
        const float withoutSunLight = CenterRadiance(pixels);
        ReplaceSkySunLight(packet.Scene.SkyAtmosphere, packet.Scene.LightProxies);
        if (packet.Scene.LightProxies.size() != 1u ||
            !IsSkySunLight(packet.Scene.LightProxies[0]) ||
            !RunFrame(device, graph, pathPass, context, 11u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 2u ||
            std::abs(CenterRadiance(pixels) - withoutSunLight) > 0.03f)
        {
            std::cerr << "空の太陽の方向光をPTが二重に数えました\n";
            return 1;
        }
        LightProxy ordinarySun = packet.Scene.LightProxies[0];
        ordinarySun.LightId = 77u;
        packet.Scene.LightProxies.push_back(ordinarySun);
        if (!RunFrame(device, graph, pathPass, context, 12u,
                      1u, 1u, pixels, &skyPass) ||
            pathPass.GetAccumulatedSampleCount() != 1u ||
            CenterRadiance(pixels) < withoutSunLight + 0.2f)
        {
            std::cerr << "通常の方向光をPTが点・spot・方向光として数えませんでした\n";
            return 1;
        }
        std::cout << "sky_sun_light_excluded=true without_sun_light=" << withoutSunLight
                  << " with_ordinary_directional=" << CenterRadiance(pixels) << '\n';
        packet.Scene.LightProxies.clear();
        std::cout << "sky_parameter_parity=3 solar_sampling=true "
                     "sky_disabled_fallback=true sky_missing_fallback=true "
                     "sky_recovery_reset=true saturated_solar_sampling=true\n";
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
#ifdef NORVES_PATH_TRACING_VOLUMETRIC_TEST
    return RunVolumetricTest();
#elif defined(NORVES_PATH_TRACING_OUTDOOR_TEST)
    return RunOutdoorTest();
#else
    return RunTest();
#endif
}
