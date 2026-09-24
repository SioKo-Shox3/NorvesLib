// PTの収束とCornell公開参照との一致を確かめる。
//
// 収束: 本番設定（本番BSDF・MIS・画素内一様標本、環境光なし）のCornell boxを1 frame 16試料で累積し、
// 同じ乱数列の入れ子の接頭列（16/64/256 spp）を読み戻す。256 sppの自己収束画像に対するMSEが
// 16→64→256で単調に減ることを、画像全体と64x64区画ごとに確かめる。独立な試料の入れ子の接頭列では
// MSE(16)/MSE(64)の期待値は(1/16-1/256)/(1/64-1/256)=5になる。区画ごとの比の中央値が4〜6.25に
// あることで、試料が独立に1/Nで収束していること（乱数列の相関や履歴の取り違えがないこと）を確かめる。
// 乱数列は試料番号だけで決まるため、履歴を捨てて16試料を引き直すと同じ画像になることも確かめる。
//
// 公開参照: 公開データの反射面は拡散なので、純Lambert・環境光なしで4096 sppまで累積し、Cornell Bowers
// 公開RGBE（R4と同じ）と比べる。露出はR4のdirect white ROIで1回決める。R4の影・赤・緑ROIの平均輝度の
// 相対誤差10%以内と優勢色度の差0.05以内、発光面の画素範囲が参照の±2画素以内、16x16区画の平均輝度の
// 相対誤差（分母の下限はdirect white ROIの参照輝度の10%）の中央値10%以内・90%点20%以内を求める。
// --dump=<path> を渡すと、公開参照と比べた4096 sppの画像をRenderingFloatImageの形式で書き出す。
#include "RenderingValidation/CornellBoxData.h"
#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"

#include "Math/MathTypes.h"
#include "Math/VectorUtils.h"
#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/RenderResources.h"
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
#include "RHI/Vulkan/VulkanDevice.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "PathTracingConvergenceVulkanTest";
    constexpr uint32_t Width = CornellBox::ImageWidth;
    constexpr uint32_t Height = CornellBox::ImageHeight;
    constexpr uint64_t ReadbackBytes = static_cast<uint64_t>(Width) * Height * 4u * sizeof(float);

    // 収束の判定（比較の前に固定）。
    constexpr uint32_t SamplesPerFrame = 16u;
    constexpr uint32_t PrefixSamples[3] = {16u, 64u, 256u};
    constexpr uint32_t TileSize = 64u;
    constexpr double MinimumNestedMseRatio = 4.0;
    constexpr double MaximumNestedMseRatio = 6.25;

    // 公開参照との判定（比較の前に固定）。
    constexpr uint32_t ReferenceSamples = 4096u;
    constexpr double MaximumRoiRelativeYError = 0.10;
    constexpr double MaximumRoiChromaDifference = 0.05;
    constexpr uint32_t BlockSize = 16u;
    constexpr double MaximumBlockMedianError = 0.10;
    constexpr double MaximumBlockPercentile90Error = 0.20;
    constexpr double BlockErrorFloorFraction = 0.1;
    // 参照でこの輝度を超える画素（発光面）を含む区画は区画比較から除く。
    constexpr double EmitterExclusionY = 0.5;
    constexpr int32_t MaximumEmitterBoundsOffset = 2;

    std::atomic<uint32_t> GValidationMessageCount{0u};

    VKAPI_ATTR VkBool32 VKAPI_CALL CountValidationMessage(
        VkDebugUtilsMessageSeverityFlagBitsEXT,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void*)
    {
        GValidationMessageCount.fetch_add(1u, std::memory_order_relaxed);
        std::cerr << "validation_message="
                  << (callbackData && callbackData->pMessage ? callbackData->pMessage : "unknown")
                  << '\n';
        return VK_FALSE;
    }

    // 形状・発光表の束縛誤りを画素値以外でも捉えるため、validationの警告とエラーを数える。
    class ValidationMessenger
    {
    public:
        explicit ValidationMessenger(const DevicePtr& device)
        {
            auto vulkanDevice = DynamicPointerCast<Vulkan::VulkanDevice>(device);
            if (!vulkanDevice)
            {
                return;
            }
            m_Instance = static_cast<VkInstance>(vulkanDevice->GetVkInstance());
            m_Destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT"));
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));
            VkDebugUtilsMessengerCreateInfoEXT createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            createInfo.pfnUserCallback = &CountValidationMessage;
            m_bCreated = create && m_Destroy &&
                         create(m_Instance, &createInfo, nullptr, &m_Messenger) == VK_SUCCESS;
        }

        ~ValidationMessenger()
        {
            if (m_bCreated)
            {
                m_Destroy(m_Instance, m_Messenger, nullptr);
            }
        }

        bool IsCreated() const { return m_bCreated; }

    private:
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
        PFN_vkDestroyDebugUtilsMessengerEXT m_Destroy = nullptr;
        bool m_bCreated = false;
    };

    struct TestGeometry
    {
        BufferPtr VertexBuffer;
        BufferPtr IndexBuffer;
        AccelerationStructurePtr BottomLevel;
        uint32_t VertexCount = 0u;
        uint32_t IndexCount = 0u;
    };

    bool BuildGeometry(const DevicePtr& device, const VariableArray<Mesh3DVertex>& vertices,
                       const VariableArray<uint32_t>& indices, TestGeometry& outGeometry)
    {
        const uint64_t vertexBytes = vertices.size() * sizeof(Mesh3DVertex);
        const uint64_t indexBytes = indices.size() * sizeof(uint32_t);
        BufferDesc vertexDesc(vertexBytes,
                              ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress,
                              true, "PathTracingConvergenceTest.Vertices");
        BufferDesc indexDesc(indexBytes,
                             ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress,
                             true, "PathTracingConvergenceTest.Indices");
        outGeometry.VertexBuffer = device->CreateBuffer(vertexDesc);
        outGeometry.IndexBuffer = device->CreateBuffer(indexDesc);
        if (!outGeometry.VertexBuffer || !outGeometry.IndexBuffer ||
            outGeometry.VertexBuffer->GetDeviceAddress() == 0u ||
            outGeometry.IndexBuffer->GetDeviceAddress() == 0u)
        {
            return false;
        }
        outGeometry.VertexBuffer->Update(vertices.data(), vertexBytes);
        outGeometry.IndexBuffer->Update(indices.data(), indexBytes);

        const uint32_t indexCount = static_cast<uint32_t>(indices.size());
        AccelerationStructureDesc bottomDesc;
        bottomDesc.type = AccelerationStructureType::BottomLevel;
        bottomDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, indexCount / 3u, true});
        outGeometry.BottomLevel = device->CreateAccelerationStructure(bottomDesc);
        AccelerationStructureGeometryDesc geometry;
        geometry.type = AccelerationStructureGeometryType::Triangles;
        geometry.opaque = true;
        geometry.triangles.vertexBuffer = outGeometry.VertexBuffer;
        geometry.triangles.vertexCount = static_cast<uint32_t>(vertices.size());
        geometry.triangles.vertexStride = sizeof(Mesh3DVertex);
        geometry.triangles.vertexFormat = Format::R32G32B32_FLOAT;
        geometry.triangles.indexBuffer = outGeometry.IndexBuffer;
        geometry.triangles.indexCount = indexCount;
        geometry.triangles.indexFormat = IndexType::Uint32;
        AccelerationStructureBuildDesc bottomBuild;
        bottomBuild.type = AccelerationStructureType::BottomLevel;
        bottomBuild.destination = outGeometry.BottomLevel;
        bottomBuild.geometries.push_back(geometry);
        if (!outGeometry.BottomLevel || !outGeometry.BottomLevel->Build(bottomBuild))
        {
            return false;
        }
        outGeometry.VertexCount = static_cast<uint32_t>(vertices.size());
        outGeometry.IndexCount = indexCount;
        return true;
    }

    RayTracingSceneInstanceSnapshot MakeInstance(const TestGeometry& geometry, uint32_t customIndex,
                                                 const float (&reflectance)[3],
                                                 TextureHandle roughnessTexture)
    {
        RayTracingSceneInstanceSnapshot snapshot;
        snapshot.AccelerationStructureVertexBuffer = geometry.VertexBuffer;
        snapshot.AccelerationStructureIndexBuffer = geometry.IndexBuffer;
        snapshot.BottomLevel = geometry.BottomLevel;
        snapshot.VertexCount = geometry.VertexCount;
        snapshot.VertexStride = sizeof(Mesh3DVertex);
        snapshot.IndexCount = geometry.IndexCount;
        snapshot.Instance.bottomLevel = geometry.BottomLevel;
        snapshot.Instance.customIndex = customIndex;
        snapshot.Instance.disableTriangleFacingCull = true;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            snapshot.Material.ObjectColor[channel] = reflectance[channel];
        }
        // R4 fixtureと同じく粗さ1（Lambertに近い本番BSDF）の材質にする。
        snapshot.Material.RoughnessTexture = roughnessTexture;
        return snapshot;
    }

    // R4 fixtureのBuildLookAtCameraと同じ規則（右=前×世界の上、上=右×前）のCornellカメラ。
    CameraProxy MakeCornellCamera()
    {
        const Math::Vector3 position(CornellBox::CameraPosition[0], CornellBox::CameraPosition[1],
                                     CornellBox::CameraPosition[2]);
        const Math::Vector3 target(CornellBox::CameraTarget[0], CornellBox::CameraTarget[1],
                                   CornellBox::CameraTarget[2]);
        const Math::Vector3 forward = Math::VectorUtils::Normalize(target - position);
        const Math::Vector3 right =
            Math::VectorUtils::Normalize(Math::VectorUtils::Cross(forward, Math::Vector3::Up));
        const Math::Vector3 up = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(right, forward));
        CameraProxy camera;
        camera.CameraId = 4u;
        camera.PositionX = position.x;
        camera.PositionY = position.y;
        camera.PositionZ = position.z;
        camera.ForwardX = forward.x;
        camera.ForwardY = forward.y;
        camera.ForwardZ = forward.z;
        camera.RightX = right.x;
        camera.RightY = right.y;
        camera.RightZ = right.z;
        camera.UpX = up.x;
        camera.UpY = up.y;
        camera.UpZ = up.z;
        camera.Projection = ProjectionType::Perspective;
        camera.FieldOfView = CornellBox::CameraFieldOfViewDegrees;
        camera.AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
        camera.NearPlane = CornellBox::CameraNearPlane;
        camera.FarPlane = CornellBox::CameraFarPlane;
        camera.Viewport = {0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f};
        return camera;
    }

    bool BuildTopLevel(const DevicePtr& device, FramePacket& packet)
    {
        AccelerationStructureDesc topDesc;
        topDesc.type = AccelerationStructureType::TopLevel;
        topDesc.maxInstanceCount = static_cast<uint32_t>(packet.RayTracingScene.Instances.size());
        AccelerationStructurePtr top = device->CreateAccelerationStructure(topDesc);
        AccelerationStructureBuildDesc topBuild;
        topBuild.type = AccelerationStructureType::TopLevel;
        topBuild.destination = top;
        for (const RayTracingSceneInstanceSnapshot& snapshot : packet.RayTracingScene.Instances)
        {
            topBuild.instances.push_back(snapshot.Instance);
        }
        if (!top || !top->Build(topBuild))
        {
            return false;
        }
        packet.RayTracingScene.TopLevel = top;
        return packet.HasCompleteRayTracingScene();
    }

    bool RecordHostReadBarrier(const CommandListPtr& commandList, const BufferPtr& readback)
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
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
            {}, 0u, nullptr, 1u, &barrier, 0u, nullptr);
        return true;
    }

    struct FrameRunner
    {
        DevicePtr Device;
        RenderGraph* Graph = nullptr;
        PathTracingPass* Pass = nullptr;
        ViewRenderContext* Context = nullptr;
        uint64_t FrameNumber = 0u;

        // 累積試料数がtargetSamplesになるまでframeを進め、最後の累積画像を読み戻す。bRestartは設定の
        // 切り替えで次のframeが履歴を捨てることを表し、最初のframeの後の試料数が1 frame分であることを求める。
        bool AccumulateTo(uint32_t targetSamples, bool bRestart, VariableArray<float>& outPixels,
                          const char* label)
        {
            bool bFirstAfterRestart = bRestart;
            while (bFirstAfterRestart || Pass->GetAccumulatedSampleCount() < targetSamples)
            {
                const uint32_t before = bFirstAfterRestart ? 0u : Pass->GetAccumulatedSampleCount();
                const bool bLast = before + SamplesPerFrame >= targetSamples;
                if (!RunFrame(bLast, outPixels) ||
                    Pass->GetAccumulatedSampleCount() != before + SamplesPerFrame)
                {
                    std::cerr << label << ": 累積に失敗しました samples="
                              << Pass->GetAccumulatedSampleCount() << '\n';
                    return false;
                }
                bFirstAfterRestart = false;
            }
            if (Pass->GetAccumulatedSampleCount() != targetSamples)
            {
                std::cerr << label << ": 累積試料数が目標と一致しません samples="
                          << Pass->GetAccumulatedSampleCount() << " target=" << targetSamples << '\n';
                return false;
            }
            return true;
        }

        bool RunFrame(bool bReadback, VariableArray<float>& outPixels)
        {
            ++FrameNumber;
            CommandListPtr commandList = Device->CreateCommandList();
            BufferPtr readback;
            if (bReadback)
            {
                BufferDesc readbackDesc(ReadbackBytes, ResourceUsage::TransferDst, true,
                                        "PathTracingConvergenceTest.Readback");
                readback = Device->CreateBuffer(readbackDesc);
            }
            if (!commandList || (bReadback && !readback))
            {
                return false;
            }
            ViewRenderContext& context = *Context;
            context.CommandList = commandList.get();
            context.FrameIndex = static_cast<uint32_t>(FrameNumber % 2u);
            context.FrameNumber = FrameNumber;
            context.SceneRevision = 1u;
            context.LightRevision = 1u;
            context.PhysicalLighting.Begin(FrameNumber, 7u, 3u);
            commandList->SetFrameIndex(context.FrameIndex);
            commandList->Begin();
            Graph->BeginFrame(FrameNumber);
            context.SkyAtmosphere.Reset();
            Graph->AddPass(Pass);
            if (!Graph->Compile(context))
            {
                commandList->End();
                return false;
            }
            const RenderGraphExecutionResult result = Graph->ExecuteWithResult(context);
            TexturePtr output;
            if (!result.bSuccess || result.ExecutedPassCount != 1u ||
                !result.TryGetTexture(RenderGraphResourceNames::SceneColor, output) ||
                output != Pass->GetAccumulatedTexture())
            {
                commandList->End();
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
            outPixels.resize(static_cast<size_t>(Width) * Height * 4u);
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
    };

    struct LinearRgb
    {
        double Red = 0.0;
        double Green = 0.0;
        double Blue = 0.0;
    };

    double Luma(const LinearRgb& color)
    {
        return 0.2126 * color.Red + 0.7152 * color.Green + 0.0722 * color.Blue;
    }

    // 画像はRGBA（float）かRGBE由来の線形RGB（double）の画素列。どちらも左上原点の行順。
    struct LinearImage
    {
        VariableArray<double> Rgb;

        LinearRgb At(uint32_t x, uint32_t y) const
        {
            const size_t offset = (static_cast<size_t>(y) * Width + x) * 3u;
            return {Rgb[offset], Rgb[offset + 1u], Rgb[offset + 2u]};
        }
    };

    LinearImage ToLinearImage(const VariableArray<float>& rgbaPixels)
    {
        LinearImage image;
        image.Rgb.resize(static_cast<size_t>(Width) * Height * 3u);
        for (size_t pixel = 0u; pixel < static_cast<size_t>(Width) * Height; ++pixel)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                image.Rgb[pixel * 3u + channel] = rgbaPixels[pixel * 4u + channel];
            }
        }
        return image;
    }

    // Radiance RGBE（新形式のRLE走査線）を線形RGBへ読む。
    bool LoadRgbe(const char* path, LinearImage& outImage)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            std::cerr << "公開参照RGBEを開けません path=" << path << '\n';
            return false;
        }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        input.seekg(0, std::ios::beg);
        VariableArray<uint8_t> bytes(static_cast<size_t>(size));
        if (size <= 0 || !input.read(reinterpret_cast<char*>(bytes.data()), size))
        {
            return false;
        }
        size_t offset = 0u;
        bool bResolutionFound = false;
        while (offset < bytes.size() && !bResolutionFound)
        {
            size_t end = offset;
            while (end < bytes.size() && bytes[end] != '\n')
            {
                ++end;
            }
            char line[128] = {};
            const size_t length = std::min<size_t>(end - offset, sizeof(line) - 1u);
            std::memcpy(line, bytes.data() + offset, length);
            unsigned parsedHeight = 0u;
            unsigned parsedWidth = 0u;
            if (sscanf_s(line, "-Y %u +X %u", &parsedHeight, &parsedWidth) == 2)
            {
                if (parsedWidth != Width || parsedHeight != Height)
                {
                    std::cerr << "公開参照RGBEの解像度が一致しません\n";
                    return false;
                }
                bResolutionFound = true;
            }
            offset = end + 1u;
        }
        if (!bResolutionFound)
        {
            return false;
        }
        VariableArray<uint8_t> planar(static_cast<size_t>(Width) * 4u);
        outImage.Rgb.resize(static_cast<size_t>(Width) * Height * 3u);
        for (uint32_t y = 0u; y < Height; ++y)
        {
            if (offset + 4u > bytes.size() || bytes[offset] != 2u || bytes[offset + 1u] != 2u ||
                ((static_cast<uint32_t>(bytes[offset + 2u]) << 8u) | bytes[offset + 3u]) != Width)
            {
                std::cerr << "公開参照RGBEの走査線の見出しが不正です row=" << y << '\n';
                return false;
            }
            offset += 4u;
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                uint32_t x = 0u;
                while (x < Width)
                {
                    if (offset >= bytes.size())
                    {
                        return false;
                    }
                    const uint32_t code = bytes[offset++];
                    if (code > 128u)
                    {
                        const uint32_t runLength = code - 128u;
                        if (offset >= bytes.size() || x + runLength > Width)
                        {
                            return false;
                        }
                        const uint8_t value = bytes[offset++];
                        for (uint32_t run = 0u; run < runLength; ++run)
                        {
                            planar[channel * Width + x + run] = value;
                        }
                        x += runLength;
                    }
                    else
                    {
                        if (code == 0u || x + code > Width || offset + code > bytes.size())
                        {
                            return false;
                        }
                        std::memcpy(planar.data() + channel * Width + x, bytes.data() + offset, code);
                        offset += code;
                        x += code;
                    }
                }
            }
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const uint8_t exponent = planar[3u * Width + x];
                const size_t pixel = (static_cast<size_t>(y) * Width + x) * 3u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    outImage.Rgb[pixel + channel] =
                        exponent == 0u
                            ? 0.0
                            : (static_cast<double>(planar[channel * Width + x]) + 0.5) *
                                  std::ldexp(1.0, static_cast<int>(exponent) - 136);
                }
            }
        }
        return true;
    }

    struct Region
    {
        uint32_t Left = 0u;
        uint32_t Top = 0u;
        uint32_t Right = 0u;
        uint32_t Bottom = 0u;
    };

    struct CornellRegions
    {
        Region DirectWhite;
        Region ShadowFloor;
        Region RedBounce;
        Region GreenBounce;
    };

    // R4CornellAcceptance.tsvのROI（左上原点、right/bottomを含まない）を読む。
    bool LoadCornellRegions(CornellRegions& outRegions)
    {
        std::ifstream input(NORVES_SOURCE_ROOT
                            "/Test/Core/Rendering/Thresholds/RenderingValidation/R4CornellAcceptance.tsv");
        if (!input)
        {
            std::cerr << "R4CornellAcceptance.tsvを開けません\n";
            return false;
        }
        struct Field
        {
            const char* Name;
            uint32_t* Target;
            bool bFound;
        } fields[] = {
            {"direct_white_left", &outRegions.DirectWhite.Left, false},
            {"direct_white_top", &outRegions.DirectWhite.Top, false},
            {"direct_white_right", &outRegions.DirectWhite.Right, false},
            {"direct_white_bottom", &outRegions.DirectWhite.Bottom, false},
            {"shadow_floor_left", &outRegions.ShadowFloor.Left, false},
            {"shadow_floor_top", &outRegions.ShadowFloor.Top, false},
            {"shadow_floor_right", &outRegions.ShadowFloor.Right, false},
            {"shadow_floor_bottom", &outRegions.ShadowFloor.Bottom, false},
            {"red_bounce_left", &outRegions.RedBounce.Left, false},
            {"red_bounce_top", &outRegions.RedBounce.Top, false},
            {"red_bounce_right", &outRegions.RedBounce.Right, false},
            {"red_bounce_bottom", &outRegions.RedBounce.Bottom, false},
            {"green_bounce_left", &outRegions.GreenBounce.Left, false},
            {"green_bounce_top", &outRegions.GreenBounce.Top, false},
            {"green_bounce_right", &outRegions.GreenBounce.Right, false},
            {"green_bounce_bottom", &outRegions.GreenBounce.Bottom, false}};
        char line[256] = {};
        while (input.getline(line, sizeof(line)))
        {
            char name[128] = {};
            unsigned value = 0u;
            if (line[0] == '#' ||
                sscanf_s(line, "%127s %u", name, static_cast<unsigned>(sizeof(name)), &value) != 2)
            {
                continue;
            }
            for (Field& field : fields)
            {
                if (std::strcmp(name, field.Name) == 0)
                {
                    *field.Target = value;
                    field.bFound = true;
                }
            }
        }
        for (const Field& field : fields)
        {
            if (!field.bFound)
            {
                std::cerr << "R4CornellAcceptance.tsvに" << field.Name << "がありません\n";
                return false;
            }
        }
        for (const Region* region : {&outRegions.DirectWhite, &outRegions.ShadowFloor,
                                     &outRegions.RedBounce, &outRegions.GreenBounce})
        {
            if (region->Left >= region->Right || region->Top >= region->Bottom ||
                region->Right > Width || region->Bottom > Height)
            {
                std::cerr << "R4のROIが画像の範囲外です\n";
                return false;
            }
        }
        return true;
    }

    LinearRgb RegionMean(const LinearImage& image, const Region& region)
    {
        LinearRgb sum;
        for (uint32_t y = region.Top; y < region.Bottom; ++y)
        {
            for (uint32_t x = region.Left; x < region.Right; ++x)
            {
                const LinearRgb color = image.At(x, y);
                sum.Red += color.Red;
                sum.Green += color.Green;
                sum.Blue += color.Blue;
            }
        }
        const double count =
            static_cast<double>(region.Right - region.Left) * (region.Bottom - region.Top);
        return {sum.Red / count, sum.Green / count, sum.Blue / count};
    }

    double Chroma(const LinearRgb& color, uint32_t channel)
    {
        const double sum = color.Red + color.Green + color.Blue;
        const double value = channel == 0u ? color.Red : color.Green;
        return sum > 1.0e-12 ? value / sum : 0.0;
    }

    struct PixelBounds
    {
        int32_t MinX = INT32_MAX;
        int32_t MinY = INT32_MAX;
        int32_t MaxX = -1;
        int32_t MaxY = -1;
    };

    // 画像の最大輝度の半分を超える画素（発光面）の外接矩形。
    PixelBounds BrightBounds(const LinearImage& image)
    {
        double maximum = 0.0;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                maximum = std::max(maximum, Luma(image.At(x, y)));
            }
        }
        PixelBounds bounds;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                if (Luma(image.At(x, y)) > 0.5 * maximum)
                {
                    bounds.MinX = std::min(bounds.MinX, static_cast<int32_t>(x));
                    bounds.MinY = std::min(bounds.MinY, static_cast<int32_t>(y));
                    bounds.MaxX = std::max(bounds.MaxX, static_cast<int32_t>(x));
                    bounds.MaxY = std::max(bounds.MaxY, static_cast<int32_t>(y));
                }
            }
        }
        return bounds;
    }

    // 2枚のRGBA画像の線形RGBの平均二乗誤差（region内）。
    double MeanSquaredError(const VariableArray<float>& a, const VariableArray<float>& b,
                            uint32_t left, uint32_t top, uint32_t right, uint32_t bottom)
    {
        double sum = 0.0;
        for (uint32_t y = top; y < bottom; ++y)
        {
            for (uint32_t x = left; x < right; ++x)
            {
                const size_t offset = (static_cast<size_t>(y) * Width + x) * 4u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double difference =
                        static_cast<double>(a[offset + channel]) - b[offset + channel];
                    sum += difference * difference;
                }
            }
        }
        return sum / (static_cast<double>(right - left) * (bottom - top) * 3.0);
    }

    double SortedPercentile(VariableArray<double>& values, double fraction)
    {
        std::sort(values.begin(), values.end());
        const size_t index = static_cast<size_t>(
            std::ceil(fraction * static_cast<double>(values.size()))) - 1u;
        return values[std::min(index, values.size() - 1u)];
    }

    double SortedMedian(VariableArray<double>& values)
    {
        std::sort(values.begin(), values.end());
        const size_t middle = values.size() / 2u;
        return values.size() % 2u == 1u ? values[middle]
                                        : 0.5 * (values[middle - 1u] + values[middle]);
    }

    // 入れ子の接頭列のMSEが単調に減り、区画ごとの比が独立な試料の期待値5の近くにあるか。
    bool CheckConvergence(const VariableArray<float> (&prefixes)[3])
    {
        const VariableArray<float>& converged = prefixes[2];
        double mse[3] = {};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            mse[index] = MeanSquaredError(prefixes[index], converged, 0u, 0u, Width, Height);
            std::cout << "convergence_mse spp=" << PrefixSamples[index]
                      << " mse_vs_256=" << mse[index] << '\n';
        }
        bool bPassed = mse[0] > mse[1] && mse[1] > mse[2] && mse[2] == 0.0;

        VariableArray<double> ratios;
        uint32_t zeroVarianceTiles = 0u;
        uint32_t nonMonotonicTiles = 0u;
        for (uint32_t top = 0u; top < Height; top += TileSize)
        {
            for (uint32_t left = 0u; left < Width; left += TileSize)
            {
                const uint32_t right = std::min(left + TileSize, Width);
                const uint32_t bottom = std::min(top + TileSize, Height);
                const double tile16 = MeanSquaredError(prefixes[0], converged, left, top, right, bottom);
                const double tile64 = MeanSquaredError(prefixes[1], converged, left, top, right, bottom);
                if (tile64 <= 0.0 && tile16 <= 0.0)
                {
                    ++zeroVarianceTiles;
                    continue;
                }
                if (!(tile16 > tile64) || tile64 <= 0.0)
                {
                    ++nonMonotonicTiles;
                    std::cout << "convergence_tile_non_monotonic left=" << left << " top=" << top
                              << " mse16=" << tile16 << " mse64=" << tile64 << '\n';
                    continue;
                }
                ratios.push_back(tile16 / tile64);
            }
        }
        const size_t measuredTiles = ratios.size();
        const double medianRatio = ratios.empty() ? 0.0 : SortedMedian(ratios);
        const double minimumRatio = ratios.empty() ? 0.0 : ratios.front();
        const double maximumRatio = ratios.empty() ? 0.0 : ratios.back();
        std::cout << "convergence_tiles measured=" << measuredTiles
                  << " zero_variance=" << zeroVarianceTiles
                  << " non_monotonic=" << nonMonotonicTiles
                  << " ratio_median=" << medianRatio << " ratio_min=" << minimumRatio
                  << " ratio_max=" << maximumRatio << " expected_ratio=5\n";
        bPassed = bPassed && nonMonotonicTiles == 0u && measuredTiles > 0u &&
                  medianRatio >= MinimumNestedMseRatio && medianRatio <= MaximumNestedMseRatio;
        if (!bPassed)
        {
            std::cerr << "入れ子の接頭列のMSEが単調に減らないか、1/Nの収束と一致しません\n";
        }
        return bPassed;
    }

    // 4096 sppの純Lambert画像を公開RGBEとR4のROI・発光面の範囲・16x16区画で比べる。
    bool CheckPublicReference(const LinearImage& measured, const LinearImage& reference,
                              const CornellRegions& regions)
    {
        const LinearRgb directReference = RegionMean(reference, regions.DirectWhite);
        const LinearRgb directMeasured = RegionMean(measured, regions.DirectWhite);
        if (!(Luma(directReference) > 0.0) || !(Luma(directMeasured) > 0.0))
        {
            std::cerr << "direct white ROIの輝度が0です\n";
            return false;
        }
        const double exposureScale = Luma(directReference) / Luma(directMeasured);
        std::cout << "cornell_exposure_scale=" << exposureScale << '\n';
        bool bPassed = true;

        const struct
        {
            const char* Label;
            const Region* RegionOfInterest;
            int32_t ChromaChannel;
        } roiCases[3] = {{"shadow_floor", &regions.ShadowFloor, -1},
                         {"red_bounce", &regions.RedBounce, 0},
                         {"green_bounce", &regions.GreenBounce, 1}};
        for (const auto& roi : roiCases)
        {
            const LinearRgb referenceMean = RegionMean(reference, *roi.RegionOfInterest);
            const LinearRgb measuredMean = RegionMean(measured, *roi.RegionOfInterest);
            const double referenceY = Luma(referenceMean);
            const double measuredY = Luma(measuredMean) * exposureScale;
            const double relativeError = std::abs(measuredY - referenceY) / referenceY;
            double chromaDifference = 0.0;
            if (roi.ChromaChannel >= 0)
            {
                const uint32_t channel = static_cast<uint32_t>(roi.ChromaChannel);
                chromaDifference =
                    std::abs(Chroma(measuredMean, channel) - Chroma(referenceMean, channel));
            }
            std::cout << "cornell_roi " << roi.Label << " reference_y=" << referenceY
                      << " measured_y=" << measuredY << " relative_error=" << relativeError
                      << " chroma_difference=" << chromaDifference << '\n';
            if (!(relativeError <= MaximumRoiRelativeYError) ||
                !(chromaDifference <= MaximumRoiChromaDifference))
            {
                std::cerr << "Cornell公開参照のROI " << roi.Label << " が閾値を超えました\n";
                bPassed = false;
            }
        }

        const PixelBounds referenceBounds = BrightBounds(reference);
        const PixelBounds measuredBounds = BrightBounds(measured);
        const int32_t boundsOffset =
            std::max({std::abs(referenceBounds.MinX - measuredBounds.MinX),
                      std::abs(referenceBounds.MaxX - measuredBounds.MaxX),
                      std::abs(referenceBounds.MinY - measuredBounds.MinY),
                      std::abs(referenceBounds.MaxY - measuredBounds.MaxY)});
        std::cout << "cornell_emitter_bounds reference=" << referenceBounds.MinX << ','
                  << referenceBounds.MinY << '-' << referenceBounds.MaxX << ','
                  << referenceBounds.MaxY << " measured=" << measuredBounds.MinX << ','
                  << measuredBounds.MinY << '-' << measuredBounds.MaxX << ','
                  << measuredBounds.MaxY << " max_offset=" << boundsOffset << '\n';
        if (boundsOffset > MaximumEmitterBoundsOffset)
        {
            std::cerr << "発光面の画素範囲が公開参照とずれています\n";
            bPassed = false;
        }

        const double errorFloor = BlockErrorFloorFraction * Luma(directReference);
        VariableArray<double> blockErrors;
        uint32_t excludedBlocks = 0u;
        double worstError = 0.0;
        uint32_t worstLeft = 0u;
        uint32_t worstTop = 0u;
        for (uint32_t top = 0u; top < Height; top += BlockSize)
        {
            for (uint32_t left = 0u; left < Width; left += BlockSize)
            {
                double referenceSum = 0.0;
                double measuredSum = 0.0;
                bool bExcluded = false;
                for (uint32_t y = top; y < top + BlockSize; ++y)
                {
                    for (uint32_t x = left; x < left + BlockSize; ++x)
                    {
                        const double referenceY = Luma(reference.At(x, y));
                        bExcluded = bExcluded || referenceY <= 0.0 || referenceY > EmitterExclusionY;
                        referenceSum += referenceY;
                        measuredSum += Luma(measured.At(x, y));
                    }
                }
                if (bExcluded)
                {
                    ++excludedBlocks;
                    continue;
                }
                const double count = static_cast<double>(BlockSize) * BlockSize;
                const double referenceMean = referenceSum / count;
                const double measuredMean = measuredSum / count * exposureScale;
                const double error =
                    std::abs(measuredMean - referenceMean) / std::max(referenceMean, errorFloor);
                if (error > worstError)
                {
                    worstError = error;
                    worstLeft = left;
                    worstTop = top;
                }
                blockErrors.push_back(error);
            }
        }
        if (blockErrors.empty())
        {
            std::cerr << "比較できる区画がありません\n";
            return false;
        }
        const size_t comparedBlocks = blockErrors.size();
        VariableArray<double> sortedErrors = blockErrors;
        const double medianError = SortedMedian(blockErrors);
        const double percentile90Error = SortedPercentile(sortedErrors, 0.9);
        std::cout << "cornell_blocks compared=" << comparedBlocks << " excluded=" << excludedBlocks
                  << " median_error=" << medianError << " p90_error=" << percentile90Error
                  << " max_error=" << worstError << " max_block=" << worstLeft << ',' << worstTop
                  << '\n';
        if (!(medianError <= MaximumBlockMedianError) ||
            !(percentile90Error <= MaximumBlockPercentile90Error))
        {
            std::cerr << "Cornell公開参照との区画の輝度差が閾値を超えました\n";
            bPassed = false;
        }
        return bPassed;
    }

    int RunTest(const char* dumpPath)
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
        if (!device || !PathTracingPass::IsSupported(device->GetCapabilities()))
        {
            return ReportGpuTestSkip(TestName, "PTに必要なRT機能を利用できません");
        }
        ValidationMessenger messenger(device);
        if (!messenger.IsCreated())
        {
            std::cerr << "validation messengerを作成できませんでした\n";
            return 1;
        }

        CornellRegions regions;
        LinearImage reference;
        if (!LoadCornellRegions(regions) ||
            !LoadRgbe(NORVES_SOURCE_ROOT
                      "/Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe",
                      reference))
        {
            return 1;
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        RenderResources resources;
        if (!shaderManager.Initialize(device.get(), shaderDirectory) ||
            !resources.Initialize(device))
        {
            std::cerr << "ShaderManagerまたはRenderResourcesを初期化できませんでした\n";
            return 1;
        }
        TextureResources& textures = resources.Textures();
        TextureDesc roughnessDesc;
        roughnessDesc.Width = 1u;
        roughnessDesc.Height = 1u;
        roughnessDesc.TextureFormat = Format::R8G8B8A8_UNORM;
        roughnessDesc.Usage = ResourceUsage::ShaderRead;
        roughnessDesc.DebugName = "PathTracingConvergenceTest.Roughness";
        TexturePtr roughnessTexture = device->CreateTexture(roughnessDesc);
        const uint8_t roughnessPixel[4] = {255u, 255u, 255u, 255u};
        if (!roughnessTexture)
        {
            std::cerr << "粗さtextureを作成できませんでした\n";
            return 1;
        }
        roughnessTexture->Update(roughnessPixel, 4u, 4u);
        const TextureHandle roughnessHandle =
            textures.RegisterExternalTexture(roughnessTexture, "PathTracingConvergenceRoughness");

        // R4 fixtureと同じ4つのメッシュ（白・赤・緑・面光源）と材質。
        VariableArray<Mesh3DVertex> whiteVertices;
        VariableArray<uint32_t> whiteIndices;
        VariableArray<Mesh3DVertex> redVertices;
        VariableArray<uint32_t> redIndices;
        VariableArray<Mesh3DVertex> greenVertices;
        VariableArray<uint32_t> greenIndices;
        VariableArray<Mesh3DVertex> lightVertices;
        VariableArray<uint32_t> lightIndices;
        CornellBox::AppendQuad(whiteVertices, whiteIndices, CornellBox::Floor);
        for (const CornellBox::Quad& quad : CornellBox::Ceiling)
        {
            CornellBox::AppendQuad(whiteVertices, whiteIndices, quad);
        }
        CornellBox::AppendQuad(whiteVertices, whiteIndices, CornellBox::BackWall);
        for (const CornellBox::Quad& quad : CornellBox::ShortBlock)
        {
            CornellBox::AppendQuad(whiteVertices, whiteIndices, quad);
        }
        for (const CornellBox::Quad& quad : CornellBox::TallBlock)
        {
            CornellBox::AppendQuad(whiteVertices, whiteIndices, quad);
        }
        CornellBox::AppendQuad(greenVertices, greenIndices, CornellBox::GreenWall);
        CornellBox::AppendQuad(redVertices, redIndices, CornellBox::RedWall);
        CornellBox::AppendQuad(lightVertices, lightIndices, CornellBox::AreaLight);
        TestGeometry white;
        TestGeometry red;
        TestGeometry green;
        TestGeometry light;
        if (!BuildGeometry(device, whiteVertices, whiteIndices, white) ||
            !BuildGeometry(device, redVertices, redIndices, red) ||
            !BuildGeometry(device, greenVertices, greenIndices, green) ||
            !BuildGeometry(device, lightVertices, lightIndices, light))
        {
            std::cerr << "Cornell boxの形状を作成できませんでした\n";
            return 1;
        }
        FramePacket packet;
        packet.RayTracingScene.Instances.push_back(
            MakeInstance(white, 0u, CornellBox::WhiteReflectance, roughnessHandle));
        packet.RayTracingScene.Instances.push_back(
            MakeInstance(red, 1u, CornellBox::RedReflectance, roughnessHandle));
        packet.RayTracingScene.Instances.push_back(
            MakeInstance(green, 2u, CornellBox::GreenReflectance, roughnessHandle));
        RayTracingSceneInstanceSnapshot lightInstance =
            MakeInstance(light, 3u, CornellBox::LightReflectance, roughnessHandle);
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            lightInstance.Material.EmissiveColor[channel] = CornellBox::LightColor[channel];
        }
        lightInstance.Material.EmissiveLuminanceNits = CornellBox::LightLuminanceNits;
        packet.RayTracingScene.Instances.push_back(lightInstance);
        if (!BuildTopLevel(device, packet))
        {
            std::cerr << "Cornell boxのTLASを作成できませんでした\n";
            return 1;
        }

        CameraProxy camera = MakeCornellCamera();

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.Resources.Textures = &textures;
        context.SnapshotScene = &packet.Scene;
        context.SnapshotLightProxies = &packet.Scene.LightProxies;
        context.SnapshotRayTracingScene = &packet.RayTracingScene;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << "PT RT pipelineを作成できませんでした\n";
            return 1;
        }
        pass.SetSamplesPerFrame(SamplesPerFrame);
        pass.SetEnvironment(PathTracingEnvironment{});
        pass.SetLightSampling(PathTracingLightSampling::MultipleImportance);
        RenderGraph graph;
        graph.Initialize(nullptr);
        FrameRunner runner;
        runner.Device = device;
        runner.Graph = &graph;
        runner.Pass = &pass;
        runner.Context = &context;
        bool bPassed = true;

        // 1. 本番BSDFの入れ子の接頭列（同じ乱数列の16/64/256 spp）。
        pass.SetBsdfMode(PathTracingBsdfMode::Production);
        VariableArray<float> prefixes[3];
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            if (!runner.AccumulateTo(PrefixSamples[index], false, prefixes[index], "production_prefix"))
            {
                return 1;
            }
        }
        if (pass.GetEmissiveTriangleCount() != 2u)
        {
            std::cerr << "面光源の発光三角形が光源表にありません count="
                      << pass.GetEmissiveTriangleCount() << '\n';
            return 1;
        }
        bPassed = CheckConvergence(prefixes) && bPassed;

        // 2. 純Lambertで4096 sppまで累積し、公開RGBEと比べる（BSDFの切り替えで履歴を捨てる）。
        pass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
        VariableArray<float> lambertPixels;
        if (!runner.AccumulateTo(ReferenceSamples, true, lambertPixels, "lambert_reference"))
        {
            return 1;
        }
        if (dumpPath != nullptr)
        {
            RgbaFloatImage dumpImage;
            dumpImage.Width = Width;
            dumpImage.Height = Height;
            dumpImage.Values = lambertPixels;
            if (!WriteRgbaFloatDump(String(dumpPath), dumpImage, ReferenceSamples))
            {
                std::cerr << "4096 sppの画像を書き出せません path=" << dumpPath << '\n';
                return 1;
            }
        }
        bPassed = CheckPublicReference(ToLinearImage(lambertPixels), reference, regions) && bPassed;

        // 3. 履歴を捨てて本番BSDFの16試料を引き直すと、1で読んだ16 sppの接頭列と同じ画像になる。
        pass.SetBsdfMode(PathTracingBsdfMode::Production);
        VariableArray<float> repeatedPrefix;
        if (!runner.AccumulateTo(PrefixSamples[0], true, repeatedPrefix, "production_repeat"))
        {
            return 1;
        }
        const bool bDeterministic =
            repeatedPrefix.size() == prefixes[0].size() &&
            std::memcmp(repeatedPrefix.data(), prefixes[0].data(),
                        prefixes[0].size() * sizeof(float)) == 0;
        std::cout << "convergence_prefix_repeatable=" << (bDeterministic ? 1 : 0) << '\n';
        if (!bDeterministic)
        {
            std::cerr << "同じ試料番号の16 sppを引き直した画像が一致しません\n";
            bPassed = false;
        }

        pass.Shutdown();
        graph.Shutdown();
        device->WaitIdle();
        roughnessTexture = TexturePtr();
        resources.Shutdown();
        shaderManager.Shutdown();
        device->WaitIdle();
        const uint32_t validationMessages = GValidationMessageCount.load(std::memory_order_relaxed);
        std::cout << "validation_messages=" << validationMessages << '\n';
        if (validationMessages != 0u)
        {
            std::cerr << "Vulkan validationの警告またはエラーがありました\n";
            bPassed = false;
        }
        if (bPassed)
        {
            std::cout << "pt_nested_prefix_mse_monotonic=true pt_prefix_ratio_matches_1_over_n=true "
                         "pt_prefix_repeatable=true cornell_public_reference_match=true\n";
        }
        return bPassed ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    const char* dumpPath = nullptr;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strncmp(argv[index], "--dump=", 7u) == 0 && argv[index][7] != '\0')
        {
            dumpPath = argv[index] + 7;
        }
    }
    return RunTest(dumpPath);
}
