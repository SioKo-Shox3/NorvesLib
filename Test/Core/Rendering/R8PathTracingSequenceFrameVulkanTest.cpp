// PTの連番の1フレーム（前後のカメラ・変換とシャッター区間を固定した累積）を実GPUで検証する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Engine/NorvesEngine.h"
#include "Math/MatrixUtils.h"
#include "Math/Quaternion.h"
#include "Rendering/FramePacket.h"
#include "Rendering/RayTracingSceneSubsystem.h"
#include "Rendering/RenderResources.h"
#include "Rendering/PathTracingCamera.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
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

    constexpr const char* TestName = "R8PathTracingSequenceFrameVulkanTest";
    constexpr uint32_t Width = 96u;
    constexpr uint32_t Height = 64u;
    constexpr uint64_t ReadbackBytes = Width * Height * 4u * sizeof(float);
    constexpr double Pi = 3.14159265358979323846;

    // 判定の閾値（計測の前に固定する）
    /** @brief 動きぼけの幅とシャッター時間×速度の差の上限（画素） */
    constexpr double MotionWidthTolerancePixels = 1.0;
    /** @brief 焦点外の点のCoCと解析値の相対差の上限 */
    constexpr double CocRelativeTolerance = 0.02;

    struct Vertex
    {
        float Position[3];
    };

    /** @brief 物体空間で1×1の正方形（z=0の面、カメラ側から反時計回り）を発光instanceとして置く。 */
    bool BuildScene(const DevicePtr& device, FramePacket& packet)
    {
        const Vertex vertices[4] = {
            {{-0.5f, -0.5f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}}};
        const uint32_t indices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
        BufferDesc vertexDesc(sizeof(vertices),
                              ResourceUsage::VertexBuffer |
                                  ResourceUsage::BufferDeviceAddress,
                              true, "R8SequenceFrameTest.Vertices");
        BufferDesc indexDesc(sizeof(indices),
                             ResourceUsage::IndexBuffer |
                                 ResourceUsage::BufferDeviceAddress,
                             true, "R8SequenceFrameTest.Indices");
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
            {AccelerationStructureGeometryType::Triangles, 2u, true});
        AccelerationStructurePtr bottom = device->CreateAccelerationStructure(bottomDesc);
        AccelerationStructureGeometryDesc geometry;
        geometry.type = AccelerationStructureGeometryType::Triangles;
        geometry.opaque = true;
        geometry.triangles.vertexBuffer = vertexBuffer;
        geometry.triangles.vertexCount = 4u;
        geometry.triangles.vertexStride = sizeof(Vertex);
        geometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        geometry.triangles.indexBuffer = indexBuffer;
        geometry.triangles.indexCount = 6u;
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
        if (!top)
        {
            return false;
        }

        RayTracingSceneInstanceSnapshot snapshot;
        snapshot.AccelerationStructureVertexBuffer = vertexBuffer;
        snapshot.AccelerationStructureIndexBuffer = indexBuffer;
        snapshot.BottomLevel = bottom;
        snapshot.VertexCount = 4u;
        snapshot.VertexStride = sizeof(Vertex);
        snapshot.IndexCount = 6u;
        snapshot.Instance.bottomLevel = bottom;
        snapshot.Instance.customIndex = 0u;
        snapshot.ObjectId = 1u;
        snapshot.Instance.disableTriangleFacingCull = true;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            snapshot.Material.BaseColor[channel] = 0.0f;
            snapshot.Material.ObjectColor[channel] = 1.0f;
            snapshot.Material.EmissiveColor[channel] = 1.0f;
        }
        snapshot.Material.EmissiveLuminanceNits = 1.0f;
        packet.RayTracingScene.TopLevel = top;
        packet.RayTracingScene.Instances.push_back(snapshot);
        return true;
    }

    /** @brief instanceの現在の変換（xy方向の拡大とx位置）を置き、TLASを作り直す。 */
    bool SetInstance(FramePacket& packet, float scaleX, float scaleY, float positionX)
    {
        RayTracingSceneInstanceSnapshot& snapshot = packet.RayTracingScene.Instances[0];
        std::memset(snapshot.Instance.transform, 0, sizeof(snapshot.Instance.transform));
        snapshot.Instance.transform[0] = scaleX;
        snapshot.Instance.transform[3] = positionX;
        snapshot.Instance.transform[5] = scaleY;
        snapshot.Instance.transform[10] = 1.0f;
        std::memcpy(snapshot.PreviousTransform, snapshot.Instance.transform,
                    sizeof(snapshot.PreviousTransform));
        snapshot.bHasPreviousTransform = false;
        AccelerationStructureBuildDesc build;
        build.type = AccelerationStructureType::TopLevel;
        build.destination = packet.RayTracingScene.TopLevel;
        build.instances.push_back(snapshot.Instance);
        return packet.RayTracingScene.TopLevel->Build(build) &&
               packet.HasCompleteRayTracingScene();
    }

    /** @brief instanceの前の変換を、現在の変換からx位置だけ変えた値にする。 */
    void SetPreviousPositionX(FramePacket& packet, float positionX)
    {
        RayTracingSceneInstanceSnapshot& snapshot = packet.RayTracingScene.Instances[0];
        std::memcpy(snapshot.PreviousTransform, snapshot.Instance.transform,
                    sizeof(snapshot.PreviousTransform));
        snapshot.PreviousTransform[3] = positionX;
        snapshot.bHasPreviousTransform = true;
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

    /** @brief 1回のdispatchを描く。bReadbackのときだけ累積画像を読み戻す。 */
    bool RunFrame(const DevicePtr& device,
                  RenderGraph& graph,
                  PathTracingPass& pass,
                  ViewRenderContext& context,
                  uint64_t frameNumber,
                  bool bReadback,
                  VariableArray<float>& outPixels)
    {
        CommandListPtr commandList = device->CreateCommandList();
        BufferDesc readbackDesc(ReadbackBytes, ResourceUsage::TransferDst,
                                true, "R8SequenceFrameTest.Readback");
        BufferPtr readback = bReadback ? device->CreateBuffer(readbackDesc) : BufferPtr{};
        if (!commandList || (bReadback && !readback))
        {
            return false;
        }
        context.CommandList = commandList.get();
        context.FrameIndex = static_cast<uint32_t>(frameNumber % 2u);
        context.FrameNumber = frameNumber;
        context.SceneRevision = 1u;
        context.LightRevision = 1u;
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
        if (bReadback)
        {
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
        }
        commandList->End();
        commandList->Submit(true);
        if (!bReadback)
        {
            return true;
        }

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

    /**
     * @brief 1つのpassで連番の1フレームをdispatchCount回累積する。
     *
     * prepareはdispatchの添字（0始まり）ごとに、そのパケットのカメラと前の値をcontextとpacketへ置く。
     * 実時間のDeltaTimeは連番の経路で使わないことを確かめるため、dispatchごとに大きく変える。
     * outSampleCountsには各dispatch後の累積試料数を残す。
     */
    template <typename PreparePacket>
    bool Accumulate(const DevicePtr& device, ShaderManager& shaderManager,
                    FramePacket& packet,
                    const PathTracingSequenceFrameSettings& settings,
                    uint32_t dispatchCount,
                    const PreparePacket& prepare,
                    VariableArray<float>& pixels,
                    VariableArray<uint32_t>& outSampleCounts)
    {
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << "PT用pipelineを初期化できませんでした\n";
            return false;
        }
        pass.SetSamplesPerFrame(1u);
        pass.SetSequenceFrame(settings);
        if (pass.GetSequenceFrame().bEnabled != settings.bEnabled)
        {
            std::cerr << "連番の設定を受け付けませんでした\n";
            return false;
        }
        RenderGraph graph;
        graph.Initialize(nullptr);
        outSampleCounts.clear();
        for (uint32_t dispatch = 0u; dispatch < dispatchCount; ++dispatch)
        {
            prepare(dispatch, context);
            context.SnapshotDeltaTime = dispatch % 2u == 0u ? 1.0f / 60.0f : 1.0f / 7.0f;
            if (!RunFrame(device, graph, pass, context, dispatch + 1u,
                          dispatch + 1u == dispatchCount, pixels))
            {
                return false;
            }
            outSampleCounts.push_back(pass.GetAccumulatedSampleCount());
        }
        pass.Shutdown();
        graph.Shutdown();
        return true;
    }

    /** @brief 各dispatch後の累積試料数が1,2,3,…と途切れずに増えたか */
    bool IsContinuous(const VariableArray<uint32_t>& sampleCounts)
    {
        for (size_t index = 0u; index < sampleCounts.size(); ++index)
        {
            if (sampleCounts[index] != index + 1u)
            {
                std::cerr << "累積の途中で履歴が消えました dispatch=" << index + 1u
                          << " samples=" << sampleCounts[index] << '\n';
                return false;
            }
        }
        return true;
    }

    CameraProxy MakeCamera(float fieldOfView)
    {
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionZ = -2.0f;
        camera.ForwardZ = 1.0f;
        camera.FieldOfView = fieldOfView;
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
        // 発光1 nitsをそのまま比べられるよう露出1にする。カメラ自体の絞りとシャッターは連番の設定で置き換える。
        camera.PreExposure = 1.0f;
        camera.Aperture = 16.0f;
        camera.ShutterSpeed = 1.0f / 1000.0f;
        camera.FocusDistance = 0.0f;
        return camera;
    }

    /**
     * @brief 棒の中央の行を平均した横方向の強度の半値全幅（画素）
     *
     * 高さは強度が0.75×最大以上の列の平均。左右の半値の位置は隣の画素との線形補間で求める。
     */
    bool MeasureHalfMaximumWidth(const VariableArray<float>& pixels, double& outWidth)
    {
        double profile[Width] = {};
        constexpr uint32_t FirstRow = 24u;
        constexpr uint32_t LastRow = 40u;
        for (uint32_t x = 0u; x < Width; ++x)
        {
            double sum = 0.0;
            for (uint32_t y = FirstRow; y <= LastRow; ++y)
            {
                sum += pixels[(y * Width + x) * 4u];
            }
            profile[x] = sum / static_cast<double>(LastRow - FirstRow + 1u);
        }
        double maximum = 0.0;
        for (const double value : profile)
        {
            maximum = value > maximum ? value : maximum;
        }
        if (!(maximum > 0.0) || profile[0] > 0.0 || profile[Width - 1u] > 0.0)
        {
            return false;
        }
        double plateauSum = 0.0;
        uint32_t plateauCount = 0u;
        for (const double value : profile)
        {
            if (value >= 0.75 * maximum)
            {
                plateauSum += value;
                ++plateauCount;
            }
        }
        const double half = 0.5 * plateauSum / plateauCount;
        uint32_t left = 1u;
        while (left < Width && profile[left] < half)
        {
            ++left;
        }
        uint32_t right = Width - 2u;
        while (right > 0u && profile[right] < half)
        {
            --right;
        }
        if (left >= Width || right == 0u || right < left)
        {
            return false;
        }
        const double leftEdge = (left - 1u) + 0.5 +
            (half - profile[left - 1u]) / (profile[left] - profile[left - 1u]);
        const double rightEdge = right + 0.5 +
            (profile[right] - half) / (profile[right] - profile[right + 1u]);
        outWidth = rightEdge - leftEdge;
        return true;
    }

    /** @brief 赤の強度で重み付けした重心まわりの画素距離の二乗平均。外周2画素に像があれば失敗。 */
    bool MeasureIntensitySecondMoment(const VariableArray<float>& pixels, double& outSecondMoment)
    {
        double weightSum = 0.0;
        double borderWeight = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const double weight = pixels[(y * Width + x) * 4u];
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
                moment += pixels[(y * Width + x) * 4u] * (dx * dx + dy * dy);
            }
        }
        outSecondMoment = moment / weightSum;
        return true;
    }

    /**
     * @brief 既知の速度で動く細い発光棒の動きぼけの幅がシャッター時間×速度に一致するか
     *
     * フレーム長1/24 sの間に棒がx方向へ速度Velocityで動く（前の変換はフレーム長だけ前の位置）。
     * シャッター区間はフレームの終わりのシャッター時間分で、像は幅（シャッター時間×速度）の台形に広がる。
     * 台形の半値全幅は棒の幅より長ければ移動量そのものになる。
     */
    bool CheckMotionBlurWidth(const DevicePtr& device, ShaderManager& shaderManager,
                              FramePacket& packet)
    {
        constexpr float Velocity = 12.0f;
        constexpr float FrameDuration = 1.0f / 24.0f;
        constexpr float EndPositionX = 0.25f;
        constexpr float BarWidth = 0.06f;
        constexpr uint32_t Dispatches = 128u;
        if (!SetInstance(packet, BarWidth, 1.0f, EndPositionX))
        {
            std::cerr << "動く棒のTLASを構築できません\n";
            return false;
        }
        SetPreviousPositionX(packet, EndPositionX - Velocity * FrameDuration);
        CameraProxy camera = MakeCamera(60.0f);
        camera.SequenceFrame = 1u;
        const double pixelsPerUnit = Height / (2.0 * 2.0 * std::tan(60.0 * Pi / 360.0));
        const struct
        {
            float Shutter;
            bool bExpectBlur;
        } cases[] = {{0.0f, false}, {1.0f / 96.0f, true}, {1.0f / 48.0f, true},
                     {1.0f / 24.0f, true}};
        for (const auto& testCase : cases)
        {
            PathTracingSequenceFrameSettings settings;
            settings.bEnabled = true;
            settings.FrameDuration = FrameDuration;
            settings.ShutterDuration = testCase.Shutter;
            settings.FocusDistance = 0.0f;
            VariableArray<float> pixels;
            VariableArray<uint32_t> sampleCounts;
            double width = 0.0;
            if (!Accumulate(device, shaderManager, packet, settings, Dispatches,
                            [&](uint32_t, ViewRenderContext& context)
                            {
                                context.MainCamera = &camera;
                                context.PreviousMainCamera = nullptr;
                            },
                            pixels, sampleCounts) ||
                !IsContinuous(sampleCounts) || !MeasureHalfMaximumWidth(pixels, width))
            {
                std::cerr << "動く棒の像を測れません shutter=" << testCase.Shutter << '\n';
                return false;
            }
            const double expected = testCase.bExpectBlur
                ? Velocity * testCase.Shutter * pixelsPerUnit
                : BarWidth * pixelsPerUnit;
            const double error = std::abs(width - expected);
            std::cout << "motion_blur shutter_s=" << testCase.Shutter
                      << " frame_duration_s=" << FrameDuration
                      << " velocity_units_per_s=" << Velocity
                      << " expected_width_px=" << expected
                      << " measured_width_px=" << width
                      << " error_px=" << error << '\n';
            if (error > MotionWidthTolerancePixels)
            {
                std::cerr << "動きぼけの幅がシャッター時間×速度と一致しません\n";
                return false;
            }
        }
        return true;
    }

    /**
     * @brief 焦点外の小さな発光正方形のCoCが解析値と一致するか
     *
     * レンズ上の一様な標本なら、焦点外の像は直径CoCの円板で、強度の二次モーメントの増分は
     * (CoC/2)²/2。焦点を合わせた像の二次モーメントは、薄レンズの倍率の差（画角を1-f/ピント距離倍に
     * 狭める）を換算してから引く。
     */
    bool CheckCircleOfConfusion(const DevicePtr& device, ShaderManager& shaderManager,
                                FramePacket& packet)
    {
        constexpr float ObjectDistance = 2.0f;
        constexpr float Aperture = 1.4f;
        constexpr float DefocusDistance = 1.2f;
        constexpr uint32_t Dispatches = 256u;
        if (!SetInstance(packet, 0.02f, 0.02f, 0.0f))
        {
            std::cerr << "点光源のTLASを構築できません\n";
            return false;
        }
        CameraProxy camera = MakeCamera(10.0f);
        PathTracingSequenceFrameSettings focused;
        focused.bEnabled = true;
        focused.Aperture = Aperture;
        focused.FocusDistance = ObjectDistance;
        focused.ShutterDuration = 0.0f;
        PathTracingSequenceFrameSettings defocused = focused;
        defocused.FocusDistance = DefocusDistance;
        double moments[2] = {};
        const PathTracingSequenceFrameSettings* settings[2] = {&focused, &defocused};
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            VariableArray<float> pixels;
            VariableArray<uint32_t> sampleCounts;
            if (!Accumulate(device, shaderManager, packet, *settings[index], Dispatches,
                            [&](uint32_t, ViewRenderContext& context)
                            {
                                context.MainCamera = &camera;
                                context.PreviousMainCamera = nullptr;
                            },
                            pixels, sampleCounts) ||
                !IsContinuous(sampleCounts) ||
                !MeasureIntensitySecondMoment(pixels, moments[index]))
            {
                std::cerr << "点光源の像を測れません\n";
                return false;
            }
        }
        const double focalLength = PathTracingCameraDetail::FocalLength(camera.FieldOfView);
        const double magnification = (1.0 - focalLength / ObjectDistance) /
                                     (1.0 - focalLength / DefocusDistance);
        const double spread = moments[1] - moments[0] * magnification * magnification;
        // 期待値のカメラは描画側の置き換え関数を通さず、指定した値から直接作る。
        CameraProxy expectedCamera = camera;
        expectedCamera.Aperture = Aperture;
        expectedCamera.FocusDistance = DefocusDistance;
        const double expected =
            ComputePathTracingCocPixels(expectedCamera, ObjectDistance, Height);
        if (!(spread > 0.0) || !(expected > 4.0))
        {
            std::cerr << "焦点外の像が広がりません\n";
            return false;
        }
        const double measured = 2.0 * std::sqrt(2.0 * spread);
        const double relativeError = std::abs(measured - expected) / expected;
        std::cout << "circle_of_confusion f_number=" << Aperture
                  << " focus_distance_m=" << DefocusDistance
                  << " object_distance_m=" << ObjectDistance
                  << " expected_coc_px=" << expected
                  << " measured_coc_px=" << measured
                  << " relative_error=" << relativeError << '\n';
        if (relativeError > CocRelativeTolerance)
        {
            std::cerr << "焦点外の点のCoCが解析値と一致しません\n";
            return false;
        }
        return true;
    }

    /** @brief GameThreadが1パケットを書く。生の前の値を置き、carryがあれば連番の前の値を固定する。 */
    void WritePacket(FramePacket& packet, PathTracingSequenceCarry* carry,
                     const CameraProxy& camera, const CameraProxy* rawPreviousCamera,
                     const float* rawPreviousX)
    {
        packet.bHasMainCamera = true;
        packet.Scene.MainCamera = camera;
        packet.bHasPreviousMainCamera = rawPreviousCamera != nullptr;
        packet.PreviousMainCamera = rawPreviousCamera ? *rawPreviousCamera : CameraProxy{};
        RayTracingSceneInstanceSnapshot& snapshot = packet.RayTracingScene.Instances[0];
        if (rawPreviousX)
        {
            SetPreviousPositionX(packet, *rawPreviousX);
        }
        else
        {
            std::memcpy(snapshot.PreviousTransform, snapshot.Instance.transform,
                        sizeof(snapshot.PreviousTransform));
            snapshot.bHasPreviousTransform = false;
        }
        if (carry)
        {
            ApplyPathTracingSequenceCarry(packet, *carry);
        }
    }

    /** @brief RenderThreadがパケットを描くときと同じく、パケットの値をcontextへ渡す。 */
    void BindPacket(const FramePacket& packet, ViewRenderContext& context)
    {
        context.MainCamera = &packet.Scene.MainCamera;
        context.PreviousMainCamera =
            packet.bHasPreviousMainCamera ? &packet.PreviousMainCamera : nullptr;
    }

    /**
     * @brief GameThread側の前の値の固定を、instanceの鍵・並び替え・途中の追加・パケットの取りこぼしで確かめる。
     *
     * GPUを使わないFramePacketの値だけで調べる。
     */
    bool CheckSequenceCarry()
    {
        const auto makeInstance = [](uint64_t objectId, float x)
        {
            RayTracingSceneInstanceSnapshot instance;
            instance.ObjectId = objectId;
            instance.MeshHandle.Id = 5u;
            instance.IndexCount = 6u;
            instance.Instance.transform[0] = 1.0f;
            instance.Instance.transform[3] = x;
            instance.Instance.transform[5] = 1.0f;
            instance.Instance.transform[10] = 1.0f;
            std::memcpy(instance.PreviousTransform, instance.Instance.transform,
                        sizeof(instance.PreviousTransform));
            return instance;
        };
        const auto setRawPrevious = [](RayTracingSceneInstanceSnapshot& instance, float x)
        {
            std::memcpy(instance.PreviousTransform, instance.Instance.transform,
                        sizeof(instance.PreviousTransform));
            instance.PreviousTransform[3] = x;
            instance.bHasPreviousTransform = true;
        };
        CameraProxy camera;
        camera.CameraId = 3u;
        PathTracingSequenceCarry carry;
        FramePacket packet;
        packet.bHasMainCamera = true;

        // フレーム6: A（ID 10）はx=1、B（ID 20）はx=2。
        camera.SequenceFrame = 6u;
        camera.PositionX = -1.0f;
        packet.Scene.MainCamera = camera;
        packet.RayTracingScene.Instances.push_back(makeInstance(10u, 1.0f));
        packet.RayTracingScene.Instances.push_back(makeInstance(20u, 2.0f));
        ApplyPathTracingSequenceCarry(packet, carry);

        // フレーム7の最初のパケットは生の前の値を持たず（GameThreadの前の値が失われた場合）、
        // 2つ目からはB・Aの順に並び、両方とも現在の変換がx=3で同じ。前の値はフレーム6の最後の
        // 状態（A=1、B=2、カメラ-1）になる。
        camera.SequenceFrame = 7u;
        camera.PositionX = 1.0f;
        for (uint32_t write = 0u; write < 3u; ++write)
        {
            packet.RayTracingScene.Instances.clear();
            packet.RayTracingScene.Instances.push_back(makeInstance(write == 0u ? 10u : 20u, 3.0f));
            packet.RayTracingScene.Instances.push_back(makeInstance(write == 0u ? 20u : 10u, 3.0f));
            packet.Scene.MainCamera = camera;
            packet.bHasPreviousMainCamera = write != 0u;
            packet.PreviousMainCamera = camera;
            if (write != 0u)
            {
                setRawPrevious(packet.RayTracingScene.Instances[0], 3.0f);
                setRawPrevious(packet.RayTracingScene.Instances[1], 3.0f);
            }
            ApplyPathTracingSequenceCarry(packet, carry);
            const RayTracingSceneInstanceSnapshot& first = packet.RayTracingScene.Instances[0];
            const RayTracingSceneInstanceSnapshot& second = packet.RayTracingScene.Instances[1];
            const float expectedFirst = first.ObjectId == 10u ? 1.0f : 2.0f;
            const float expectedSecond = second.ObjectId == 10u ? 1.0f : 2.0f;
            if (!packet.bHasPreviousMainCamera || packet.PreviousMainCamera.PositionX != -1.0f ||
                !first.bHasPreviousTransform || first.PreviousTransform[3] != expectedFirst ||
                !second.bHasPreviousTransform || second.PreviousTransform[3] != expectedSecond)
            {
                std::cerr << "連番の前の値が物体の対応で固定されません write=" << write << '\n';
                return false;
            }
            // 描画の並びに依らず、snapshotは物体IDの順に並び、customIndexは並びの番号になる。
            if (first.ObjectId != 10u || second.ObjectId != 20u ||
                first.Instance.customIndex != 0u || second.Instance.customIndex != 1u)
            {
                std::cerr << "連番のsnapshotが物体の鍵の順に揃いません write=" << write << '\n';
                return false;
            }
        }

        // 同じフレームの途中で現れた物体（ID 30）は前の値なし（動かない）にする。
        packet.RayTracingScene.Instances.push_back(makeInstance(30u, 4.0f));
        setRawPrevious(packet.RayTracingScene.Instances[2], 0.0f);
        ApplyPathTracingSequenceCarry(packet, carry);
        if (packet.RayTracingScene.Instances[2].ObjectId != 30u ||
            packet.RayTracingScene.Instances[2].bHasPreviousTransform ||
            packet.RayTracingScene.Instances[1].ObjectId != 20u ||
            packet.RayTracingScene.Instances[1].PreviousTransform[3] != 2.0f)
        {
            std::cerr << "途中で現れた物体に前の値が付きました\n";
            return false;
        }

        // 連番でなくなったらパケットの値をそのまま使い、固定を捨てる。
        camera.SequenceFrame = 0u;
        packet.Scene.MainCamera = camera;
        packet.PreviousMainCamera.PositionX = 0.5f;
        setRawPrevious(packet.RayTracingScene.Instances[0], 0.25f);
        ApplyPathTracingSequenceCarry(packet, carry);
        if (packet.PreviousMainCamera.PositionX != 0.5f ||
            packet.RayTracingScene.Instances[0].PreviousTransform[3] != 0.25f ||
            carry.Frame != 0u || carry.LastFrame != 0u)
        {
            std::cerr << "連番でないパケットの前の値が変わりました\n";
            return false;
        }
        std::cout << "sequence_carry dropped_first_packet=true reordered_same_transform=true "
                  << "new_instance_static=true non_sequence_untouched=true\n";
        return true;
    }

    /**
     * @brief 連番の1フレームの間、パケットを取りこぼしても前の値が変わらず累積し続けるか
     *
     * GameThreadは毎回パケットを書く。フレームを進めた最初のパケットだけ生の前の値が前のフレームの
     * 状態で、後続は前の値が現在の値と同じになる。前の値の固定（ApplyPathTracingSequenceCarry）は
     * 直前のフレームの最後の状態を前の値として全パケットへ書くため、RenderThreadがフレームの
     * 最初のパケットを取りこぼしても、全dispatchで同じ前の値を持つパケットと同じ画像になる。
     */
    bool CheckLatchedHistory(const DevicePtr& device, ShaderManager& shaderManager,
                             FramePacket& packet)
    {
        constexpr uint32_t Dispatches = 64u;
        constexpr float PreviousPositionX = -0.25f;
        constexpr float PositionX = 0.25f;
        CameraProxy previousCamera = MakeCamera(60.0f);
        previousCamera.PositionX = -0.1f;
        previousCamera.SequenceFrame = 6u;
        CameraProxy currentCamera = previousCamera;
        currentCamera.PositionX = 0.1f;
        currentCamera.SequenceFrame = 7u;
        PathTracingSequenceFrameSettings settings;
        settings.bEnabled = true;
        settings.FocusDistance = 0.0f;
        PathTracingSequenceCarry carry;
        bool bSceneReady = true;

        // フレーム6を2パケット書いてからフレーム7へ進める。フレーム7の最初のパケットはRenderThreadが
        // 取りこぼし、2つ目以降（生の前の値は現在と同じ）だけを描く。
        const auto droppedFirstPackets = [&](uint32_t dispatch, ViewRenderContext& context)
        {
            if (dispatch == 0u)
            {
                carry.Reset();
                bSceneReady = SetInstance(packet, 0.2f, 0.6f, PreviousPositionX) && bSceneReady;
                WritePacket(packet, &carry, previousCamera, &previousCamera, nullptr);
                WritePacket(packet, &carry, previousCamera, &previousCamera, nullptr);
                bSceneReady = SetInstance(packet, 0.2f, 0.6f, PositionX) && bSceneReady;
                WritePacket(packet, &carry, currentCamera, &previousCamera, &PreviousPositionX);
            }
            WritePacket(packet, &carry, currentCamera, &currentCamera, &PositionX);
            BindPacket(packet, context);
        };
        // 全dispatchが同じ前の値を持つ（前の値の固定を使わない）。
        const auto fixedPackets = [&](uint32_t dispatch, ViewRenderContext& context)
        {
            if (dispatch == 0u)
            {
                bSceneReady = SetInstance(packet, 0.2f, 0.6f, PositionX) && bSceneReady;
            }
            WritePacket(packet, nullptr, currentCamera, &previousCamera, &PreviousPositionX);
            BindPacket(packet, context);
        };

        VariableArray<float> carried;
        VariableArray<float> carriedRepeat;
        VariableArray<float> fixed;
        VariableArray<uint32_t> sampleCounts;
        if (!Accumulate(device, shaderManager, packet, settings, Dispatches, droppedFirstPackets,
                        carried, sampleCounts) ||
            !IsContinuous(sampleCounts) ||
            !Accumulate(device, shaderManager, packet, settings, Dispatches, droppedFirstPackets,
                        carriedRepeat, sampleCounts) ||
            !IsContinuous(sampleCounts) ||
            !Accumulate(device, shaderManager, packet, settings, Dispatches, fixedPackets,
                        fixed, sampleCounts) ||
            !IsContinuous(sampleCounts) || !bSceneReady)
        {
            std::cerr << "連番の1フレームを累積できません\n";
            return false;
        }
        const bool bRepeatMatches =
            std::memcmp(carried.data(), carriedRepeat.data(), ReadbackBytes) == 0;
        const bool bFixedMatches = std::memcmp(carried.data(), fixed.data(), ReadbackBytes) == 0;
        std::cout << "sequence_frame_history samples=" << Dispatches
                  << " first_packet_dropped=true"
                  << " rerun_byte_identical=" << (bRepeatMatches ? "true" : "false")
                  << " fixed_previous_byte_identical=" << (bFixedMatches ? "true" : "false")
                  << '\n';
        if (!bRepeatMatches || !bFixedMatches)
        {
            std::cerr << "連番の1フレームの累積が再実行または固定の前の値と一致しません\n";
            return false;
        }

        // 対照: 前の値を固定しなければ、最初のパケットの後で前の値が変わり履歴を捨てる。
        const auto rawPackets = [&](uint32_t dispatch, ViewRenderContext& context)
        {
            if (dispatch == 0u)
            {
                WritePacket(packet, nullptr, currentCamera, &previousCamera, &PreviousPositionX);
            }
            else
            {
                WritePacket(packet, nullptr, currentCamera, &currentCamera, &PositionX);
            }
            BindPacket(packet, context);
        };
        VariableArray<float> unfixed;
        if (!Accumulate(device, shaderManager, packet, settings, 4u, rawPackets,
                        unfixed, sampleCounts) ||
            sampleCounts.size() != 4u || sampleCounts[1] != 1u)
        {
            std::cerr << "対照の経路で前の値の変化による履歴の破棄を確かめられません\n";
            return false;
        }
        std::cout << "without_carry_second_dispatch_samples=" << sampleCounts[1] << '\n';

        // 次のフレームへ進めると、状態が同じでも新しいフレームとして累積し直す。
        CameraProxy nextCamera = currentCamera;
        nextCamera.SequenceFrame = currentCamera.SequenceFrame + 1u;
        const auto nextFramePackets = [&](uint32_t dispatch, ViewRenderContext& context)
        {
            if (dispatch < 3u)
            {
                droppedFirstPackets(dispatch, context);
                return;
            }
            WritePacket(packet, &carry, nextCamera, &currentCamera, &PositionX);
            BindPacket(packet, context);
        };
        VariableArray<float> nextFrame;
        if (!Accumulate(device, shaderManager, packet, settings, 6u, nextFramePackets,
                        nextFrame, sampleCounts) ||
            sampleCounts.size() != 6u || sampleCounts[2] != 3u || sampleCounts[3] != 1u ||
            sampleCounts[5] != 3u || !bSceneReady)
        {
            std::cerr << "次の連番のフレームで累積をやり直しません\n";
            return false;
        }
        std::cout << "next_sequence_frame_restarts=true\n";
        return true;
    }

    /**
     * @brief インスタンシング描画の並び替えでも、前の値が物体ごとに固定されるか
     *
     * 同じメッシュ・材質の物体P・A・BをMeshBatcherで1つのインスタンシング描画にまとめ、実際の
     * snapshotの構築（BuildFrameSnapshot）と前の値の固定を通す。フレーム6はP・A・Bの順でAはx=1、
     * Bはx=2。フレーム7はP・B・Aの順に並び、AとBは同じ現在の変換（x=3）になる。描画の物体IDは
     * バッチ先頭のPだけなので、物体色で区別したAとBの前の値が、それぞれのフレーム6の位置になることを確かめる。
     */
    bool CheckInstancedBatchCarry(const DevicePtr& device)
    {
        RenderResources renderResources;
        if (!renderResources.Initialize(device))
        {
            std::cerr << "描画リソースを初期化できません\n";
            return false;
        }
        constexpr MeshDataHandle meshHandle{7};
        Mesh3DVertex vertices[3]{};
        vertices[0].Position[0] = -1.0f;
        vertices[0].Position[1] = -1.0f;
        vertices[1].Position[0] = 1.0f;
        vertices[1].Position[1] = -1.0f;
        vertices[2].Position[1] = 1.0f;
        constexpr uint32_t indices[3] = {0u, 1u, 2u};
        if (!renderResources.Meshes().Register(meshHandle, vertices, sizeof(vertices), indices, 3u))
        {
            std::cerr << "メッシュを登録できません\n";
            renderResources.Shutdown();
            return false;
        }

        struct Body
        {
            uint64_t ObjectId;
            float Red;
        };
        const Body bodyP{100u, 0.75f};
        const Body bodyA{200u, 0.5f};
        const Body bodyB{300u, 0.25f};
        const auto makeProxy = [&](const Body& body, float x, float previousX)
        {
            MeshProxy proxy;
            proxy.ObjectId = body.ObjectId;
            proxy.MeshHandle = meshHandle;
            proxy.MaterialCount = 1u;
            proxy.WorldTransform = Math::MatrixUtils::CreateWorldRowVector(
                Math::Vector3(x, 0.0f, 0.0f), Math::Quaternion::Identity,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            proxy.PreviousWorldTransform = Math::MatrixUtils::CreateWorldRowVector(
                Math::Vector3(previousX, 0.0f, 0.0f), Math::Quaternion::Identity,
                Math::Vector3(1.0f, 1.0f, 1.0f));
            proxy.CustomData[0] = body.Red;
            return proxy;
        };

        RayTracingSceneSubsystem& subsystem = NorvesLib::Core::GEngine.GetRayTracingSceneSubsystem();
        PathTracingSequenceCarry carry;
        CameraProxy camera;
        camera.CameraId = 3u;
        FramePacket packet;
        // GameThreadの1パケット分: proxyをバッチにまとめ、描画とinstance dataとsnapshotを作り、前の値を固定する。
        const auto writePacket = [&](uint64_t sequenceFrame, const VariableArray<MeshProxy>& proxies)
        {
            MeshBatcher batcher;
            batcher.BeginBatching();
            for (const MeshProxy& proxy : proxies)
            {
                batcher.AddMeshProxy(proxy);
            }
            batcher.EndBatching();
            packet.DrawCommands.clear();
            packet.InstanceData.clear();
            batcher.GenerateDrawCommands(packet.DrawCommands, packet.InstanceData, true, 2u);
            packet.OpaqueCommandRange.First = 0u;
            packet.OpaqueCommandRange.Count = static_cast<uint32_t>(packet.DrawCommands.size());
            packet.DrawCommandRange = packet.OpaqueCommandRange;
            packet.Scene.MeshProxies = proxies;
            camera.SequenceFrame = sequenceFrame;
            packet.Scene.MainCamera = camera;
            packet.bHasMainCamera = true;
            packet.bHasPreviousMainCamera = false;
            const bool bBuilt = subsystem.BuildFrameSnapshot(&renderResources.Meshes(), packet) &&
                                packet.DrawCommands.size() == 1u &&
                                packet.DrawCommands[0].Draw.bInstanced &&
                                packet.RayTracingScene.Instances.size() == 3u;
            ApplyPathTracingSequenceCarry(packet, carry);
            return bBuilt;
        };

        bool bPassed = true;
        VariableArray<MeshProxy> frame6;
        frame6.push_back(makeProxy(bodyP, 0.0f, 0.0f));
        frame6.push_back(makeProxy(bodyA, 1.0f, 1.0f));
        frame6.push_back(makeProxy(bodyB, 2.0f, 2.0f));
        bPassed = writePacket(6u, frame6) && bPassed;
        // フレーム7の最初のパケットの生の前の値はフレーム6の位置、2つ目以降は現在の位置と同じ。
        for (uint32_t write = 0u; write < 3u && bPassed; ++write)
        {
            VariableArray<MeshProxy> frame7;
            frame7.push_back(makeProxy(bodyP, 0.0f, 0.0f));
            frame7.push_back(makeProxy(bodyB, 3.0f, write == 0u ? 2.0f : 3.0f));
            frame7.push_back(makeProxy(bodyA, 3.0f, write == 0u ? 1.0f : 3.0f));
            bPassed = writePacket(7u, frame7) && bPassed;
            uint32_t checked = 0u;
            for (const RayTracingSceneInstanceSnapshot& instance : packet.RayTracingScene.Instances)
            {
                const float red = instance.Material.ObjectColor[0];
                const float expectedPrevious = red == bodyP.Red ? 0.0f
                                             : red == bodyA.Red ? 1.0f
                                             : red == bodyB.Red ? 2.0f
                                                                : -100.0f;
                const float expectedCurrent = red == bodyP.Red ? 0.0f : 3.0f;
                std::cout << "instanced_carry write=" << write << " object_color_r=" << red
                          << " current_x=" << instance.Instance.transform[3]
                          << " previous_x=" << instance.PreviousTransform[3] << '\n';
                if (instance.Instance.transform[3] != expectedCurrent ||
                    instance.PreviousTransform[3] != expectedPrevious ||
                    (red != bodyP.Red && !instance.bHasPreviousTransform))
                {
                    bPassed = false;
                }
                ++checked;
            }
            bPassed = bPassed && checked == 3u;
        }
        subsystem.Shutdown();
        renderResources.Shutdown();
        if (!bPassed)
        {
            std::cerr << "インスタンシング描画の並び替えで前の値が物体ごとに固定されません\n";
            return false;
        }
        std::cout << "instanced_batch_reordered_carry=true\n";
        return true;
    }

    /**
     * @brief 同じ連番のフレームの中でinstanceの並びが変わっても、累積が続き固定順と同じ画像になるか
     *
     * 発光色で区別した物体P・A・B（物体IDはP=50、A=40、B=60）を置き、AとBはフレーム6から7へ動く。
     * フレーム7の1回目のdispatchはP・A・Bの順、2回目はP・B・Aの順に並べる。各物体の前後の変換と
     * SequenceFrameは両dispatchで同じなので、試料数は1→2と続き、2回ともP・A・Bの順で描いた画像と
     * byte一致しなければならない。TLASはRenderThreadと同じく前の値の固定の後のsnapshotの並びで作る。
     */
    bool CheckReorderedInstanceAccumulation(const DevicePtr& device, ShaderManager& shaderManager,
                                            const FramePacket& sourcePacket)
    {
        const RayTracingSceneInstanceSnapshot& source = sourcePacket.RayTracingScene.Instances[0];
        AccelerationStructureDesc topDesc;
        topDesc.type = AccelerationStructureType::TopLevel;
        topDesc.maxInstanceCount = 3u;
        AccelerationStructurePtr top = device->CreateAccelerationStructure(topDesc);
        if (!top)
        {
            std::cerr << "並び替えの検査用TLASを作れません\n";
            return false;
        }

        struct Body
        {
            uint64_t ObjectId;
            float Emission[3];
            float Frame6X;
            float Frame7X;
        };
        const Body bodyP{50u, {1.0f, 0.0f, 0.0f}, -0.9f, -0.9f};
        const Body bodyA{40u, {0.0f, 1.0f, 0.0f}, -0.1f, 0.1f};
        const Body bodyB{60u, {0.0f, 0.0f, 1.0f}, 0.5f, 0.9f};
        const auto makeInstance = [&](const Body& body, float x, const float* rawPreviousX)
        {
            RayTracingSceneInstanceSnapshot instance = source;
            instance.ObjectId = body.ObjectId;
            std::memset(instance.Instance.transform, 0, sizeof(instance.Instance.transform));
            instance.Instance.transform[0] = 0.3f;
            instance.Instance.transform[3] = x;
            instance.Instance.transform[5] = 0.3f;
            instance.Instance.transform[10] = 1.0f;
            std::memcpy(instance.PreviousTransform, instance.Instance.transform,
                        sizeof(instance.PreviousTransform));
            instance.bHasPreviousTransform = rawPreviousX != nullptr;
            if (rawPreviousX)
            {
                instance.PreviousTransform[3] = *rawPreviousX;
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                instance.Material.EmissiveColor[channel] = body.Emission[channel];
            }
            return instance;
        };

        CameraProxy previousCamera = MakeCamera(60.0f);
        previousCamera.SequenceFrame = 6u;
        CameraProxy currentCamera = previousCamera;
        currentCamera.SequenceFrame = 7u;
        PathTracingSequenceFrameSettings settings;
        settings.bEnabled = true;
        settings.FocusDistance = 0.0f;
        FramePacket packet;
        packet.RayTracingScene.TopLevel = top;
        PathTracingSequenceCarry carry;
        bool bSceneReady = true;

        // GameThreadの1パケット分: 描画の並びでsnapshotを置き、前の値を固定してからTLASを作る。
        const auto writePacket = [&](const CameraProxy& camera, const Body* const* order,
                                     bool bFrame7, bool bRawPreviousIsFrame6,
                                     PathTracingSequenceCarry* activeCarry)
        {
            packet.RayTracingScene.Instances.clear();
            for (uint32_t slot = 0u; slot < 3u; ++slot)
            {
                const Body& body = *order[slot];
                const float x = bFrame7 ? body.Frame7X : body.Frame6X;
                const float rawPrevious = bRawPreviousIsFrame6 ? body.Frame6X : x;
                packet.RayTracingScene.Instances.push_back(
                    makeInstance(body, x, bFrame7 ? &rawPrevious : nullptr));
                packet.RayTracingScene.Instances.back().Instance.customIndex = slot;
            }
            packet.bHasMainCamera = true;
            packet.Scene.MainCamera = camera;
            packet.bHasPreviousMainCamera = bFrame7;
            packet.PreviousMainCamera = bFrame7 ? previousCamera : CameraProxy{};
            if (activeCarry)
            {
                ApplyPathTracingSequenceCarry(packet, *activeCarry);
            }
            AccelerationStructureBuildDesc build;
            build.type = AccelerationStructureType::TopLevel;
            build.destination = top;
            for (const RayTracingSceneInstanceSnapshot& instance : packet.RayTracingScene.Instances)
            {
                build.instances.push_back(instance.Instance);
            }
            bSceneReady = top->Build(build) && packet.HasCompleteRayTracingScene() && bSceneReady;
        };

        const Body* const orderPAB[3] = {&bodyP, &bodyA, &bodyB};
        const Body* const orderPBA[3] = {&bodyP, &bodyB, &bodyA};
        const auto makePrepare = [&](const Body* const* secondOrder)
        {
            return [&, secondOrder](uint32_t dispatch, ViewRenderContext& context)
            {
                if (dispatch == 0u)
                {
                    carry.Reset();
                    writePacket(previousCamera, orderPAB, false, false, &carry);
                    writePacket(currentCamera, orderPAB, true, true, &carry);
                }
                else
                {
                    // 2つ目以降のパケットの生の前の値は現在と同じ。前の値の固定がフレーム6の位置へ戻す。
                    writePacket(currentCamera, secondOrder, true, false, &carry);
                }
                BindPacket(packet, context);
            };
        };

        constexpr uint32_t Dispatches = 2u;
        VariableArray<float> reordered;
        VariableArray<float> fixedOrder;
        VariableArray<uint32_t> reorderedCounts;
        VariableArray<uint32_t> fixedCounts;
        if (!Accumulate(device, shaderManager, packet, settings, Dispatches, makePrepare(orderPBA),
                        reordered, reorderedCounts) ||
            !Accumulate(device, shaderManager, packet, settings, Dispatches, makePrepare(orderPAB),
                        fixedOrder, fixedCounts) ||
            !bSceneReady)
        {
            std::cerr << "並び替えの検査で累積を実行できません\n";
            return false;
        }
        const bool bContinuous = IsContinuous(reorderedCounts) && IsContinuous(fixedCounts);
        const bool bByteIdentical =
            std::memcmp(reordered.data(), fixedOrder.data(), ReadbackBytes) == 0;
        bool bAllBodiesVisible = true;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            float maximum = 0.0f;
            for (uint32_t pixel = 0u; pixel < Width * Height; ++pixel)
            {
                maximum = std::max(maximum, reordered[pixel * 4u + channel]);
            }
            bAllBodiesVisible = bAllBodiesVisible && maximum > 0.0f;
        }
        std::cout << "reordered_instances second_dispatch_samples="
                  << (reorderedCounts.size() == Dispatches ? reorderedCounts[1] : 0u)
                  << " fixed_order_byte_identical=" << (bByteIdentical ? "true" : "false")
                  << " all_bodies_visible=" << (bAllBodiesVisible ? "true" : "false") << '\n';
        if (!bContinuous || !bByteIdentical || !bAllBodiesVisible)
        {
            std::cerr << "同じ連番のフレームでinstanceの並びが変わると累積が続かないか、固定順と一致しません\n";
            return false;
        }
        return true;
    }

    int RunTest()
    {
        // GameThread側の前の値の固定はGPUなしで確かめる。
        if (!CheckSequenceCarry())
        {
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
        if (!device || !PathTracingPass::IsSupported(device->GetCapabilities()))
        {
            return ReportGpuTestSkip(TestName, "パストレーサーに必要なデバイス機能を利用できません");
        }
        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            return 1;
        }
        if (!CheckInstancedBatchCarry(device))
        {
            shaderManager.Shutdown();
            device->WaitIdle();
            return 1;
        }
        FramePacket packet;
        if (!BuildScene(device, packet) ||
            !CheckMotionBlurWidth(device, shaderManager, packet) ||
            !CheckCircleOfConfusion(device, shaderManager, packet) ||
            !CheckLatchedHistory(device, shaderManager, packet) ||
            !CheckReorderedInstanceAccumulation(device, shaderManager, packet))
        {
            shaderManager.Shutdown();
            device->WaitIdle();
            return 1;
        }
        shaderManager.Shutdown();
        device->WaitIdle();
        std::cout << TestName << " PASS\n";
        return 0;
    }
}

int main()
{
    return RunTest();
}
