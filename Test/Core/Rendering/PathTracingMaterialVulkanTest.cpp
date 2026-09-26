// PTの材質snapshot（texture UV標本化・instance色・metallic/roughness・既定texture・法線マップ・
// 発光のプリエクスポージャ・位置のみ頂点のfallback・texture表の上限）をreadbackで確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
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
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "PathTracingMaterialVulkanTest";
    constexpr uint32_t Width = 32u;
    constexpr uint32_t Height = 32u;
    constexpr uint64_t ReadbackBytes = Width * Height * 4u * sizeof(float);
    // 視野60度・距離2の視錐台（半幅約1.155）より少し大きい四角形で画面全体を覆う。
    constexpr float QuadHalfSize = 1.2f;
    constexpr float ObjectColor[3] = {0.5f, 1.0f, 0.25f};
    constexpr float ValueTolerance = 0.001f;
    constexpr float NormalTolerance = 0.002f;

    // 8x8アルベドtextureの象限色（[v象限][u象限]のRGBA8）。双線形の隣接texelが同じ象限なら値は厳密に一致する。
    constexpr uint8_t QuadrantColors[2][2][3] = {
        {{51u, 51u, 153u}, {204u, 51u, 102u}},
        {{51u, 204u, 255u}, {204u, 204u, 0u}}};
    // 接空間法線(0.36, 0.48, 0.8)を8bitへ符号化した値。x・yの符号を取り違えると検出できる向きにする。
    constexpr uint8_t TiltedNormalBytes[3] = {173u, 189u, 230u};
    // metallicとroughnessはR成分だけを使う。G・Bを変えて成分の取り違えを検出する。
    constexpr uint8_t MetallicBytes[3] = {64u, 200u, 10u};
    constexpr uint8_t RoughnessBytes[3] = {191u, 20u, 240u};

    float Unorm(uint8_t value)
    {
        return static_cast<float>(value) / 255.0f;
    }

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

    // 材質texture配列の束縛誤りは画素値に出ないことがあるため、validationの警告とエラーも数える。
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
        uint32_t VertexStride = 0u;
        uint32_t IndexCount = 0u;
    };

    bool BuildGeometry(const DevicePtr& device, const void* vertices, uint32_t vertexCount,
                       uint32_t vertexStride, const uint32_t* indices, uint32_t indexCount,
                       TestGeometry& outGeometry)
    {
        const uint64_t vertexBytes = static_cast<uint64_t>(vertexCount) * vertexStride;
        const uint64_t indexBytes = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
        BufferDesc vertexDesc(vertexBytes,
                              ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress,
                              true, "PathTracingMaterialTest.Vertices");
        BufferDesc indexDesc(indexBytes,
                             ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress,
                             true, "PathTracingMaterialTest.Indices");
        outGeometry.VertexBuffer = device->CreateBuffer(vertexDesc);
        outGeometry.IndexBuffer = device->CreateBuffer(indexDesc);
        if (!outGeometry.VertexBuffer || !outGeometry.IndexBuffer ||
            outGeometry.VertexBuffer->GetDeviceAddress() == 0u ||
            outGeometry.IndexBuffer->GetDeviceAddress() == 0u)
        {
            return false;
        }
        outGeometry.VertexBuffer->Update(vertices, vertexBytes);
        outGeometry.IndexBuffer->Update(indices, indexBytes);

        AccelerationStructureDesc bottomDesc;
        bottomDesc.type = AccelerationStructureType::BottomLevel;
        bottomDesc.geometryCapacities.push_back(
            {AccelerationStructureGeometryType::Triangles, indexCount / 3u, true});
        outGeometry.BottomLevel = device->CreateAccelerationStructure(bottomDesc);
        AccelerationStructureGeometryDesc geometry;
        geometry.type = AccelerationStructureGeometryType::Triangles;
        geometry.opaque = true;
        geometry.triangles.vertexBuffer = outGeometry.VertexBuffer;
        geometry.triangles.vertexCount = vertexCount;
        geometry.triangles.vertexStride = vertexStride;
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
        outGeometry.VertexCount = vertexCount;
        outGeometry.VertexStride = vertexStride;
        outGeometry.IndexCount = indexCount;
        return true;
    }

    RayTracingSceneInstanceSnapshot MakeInstance(const TestGeometry& geometry,
                                                 uint32_t customIndex,
                                                 float translationZ)
    {
        RayTracingSceneInstanceSnapshot snapshot;
        snapshot.AccelerationStructureVertexBuffer = geometry.VertexBuffer;
        snapshot.AccelerationStructureIndexBuffer = geometry.IndexBuffer;
        snapshot.BottomLevel = geometry.BottomLevel;
        snapshot.VertexCount = geometry.VertexCount;
        snapshot.VertexStride = geometry.VertexStride;
        snapshot.IndexCount = geometry.IndexCount;
        snapshot.Instance.bottomLevel = geometry.BottomLevel;
        snapshot.Instance.customIndex = customIndex;
        snapshot.Instance.disableTriangleFacingCull = true;
        snapshot.Instance.transform[11] = translationZ;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            snapshot.Material.ObjectColor[channel] = ObjectColor[channel];
        }
        return snapshot;
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

    TexturePtr CreateRgba8Texture(const DevicePtr& device, uint32_t width, uint32_t height,
                                  const uint8_t* pixels, const char* debugName)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = debugName;
        TexturePtr texture = device->CreateTexture(desc);
        if (texture)
        {
            texture->Update(pixels, width * 4u, width * height * 4u);
        }
        return texture;
    }

    TexturePtr CreateSolidTexture(const DevicePtr& device, const uint8_t (&color)[3],
                                  const char* debugName)
    {
        const uint8_t pixel[4] = {color[0], color[1], color[2], 255u};
        return CreateRgba8Texture(device, 1u, 1u, pixel, debugName);
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

    bool RunFrame(const DevicePtr& device, RenderGraph& graph, PathTracingPass& pass,
                  ViewRenderContext& context, uint64_t frameNumber,
                  VariableArray<float>& outPixels)
    {
        CommandListPtr commandList = device->CreateCommandList();
        BufferDesc readbackDesc(ReadbackBytes, ResourceUsage::TransferDst, true,
                                "PathTracingMaterialTest.Readback");
        BufferPtr readback = device->CreateBuffer(readbackDesc);
        if (!commandList || !readback)
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
        context.SkyAtmosphere.Reset();
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
            std::cerr << "PTのSceneColorが公開されませんでした frame=" << frameNumber << '\n';
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
            if (!std::isfinite(value))
            {
                std::cerr << "PTの画素に非有限値があります frame=" << frameNumber << '\n';
                return false;
            }
        }
        return true;
    }

    // PTのray生成と同じ規約（画面uv→NDC→逆ビュー射影の遠平面点）で、画面上の点をz=0平面へ写す。
    struct ScreenMapping
    {
        float InverseViewProjection[16] = {};
        float CameraPosition[4] = {};
    };

    bool ProjectToPlane(const ScreenMapping& mapping, float screenU, float screenV,
                        float& outX, float& outY)
    {
        const float ndc[4] = {screenU * 2.0f - 1.0f, screenV * 2.0f - 1.0f, 1.0f, 1.0f};
        float farPoint[4] = {};
        for (uint32_t row = 0u; row < 4u; ++row)
        {
            for (uint32_t column = 0u; column < 4u; ++column)
            {
                farPoint[row] += mapping.InverseViewProjection[column * 4u + row] * ndc[column];
            }
        }
        if (std::abs(farPoint[3]) < 1.0e-6f)
        {
            return false;
        }
        float direction[3] = {};
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            direction[axis] = farPoint[axis] / farPoint[3] - mapping.CameraPosition[axis];
        }
        if (std::abs(direction[2]) < 1.0e-6f)
        {
            return false;
        }
        const float distance = -mapping.CameraPosition[2] / direction[2];
        if (!(distance > 0.0f))
        {
            return false;
        }
        outX = mapping.CameraPosition[0] + direction[0] * distance;
        outY = mapping.CameraPosition[1] + direction[1] * distance;
        return true;
    }

    // 画素の4隅をz=0平面へ写す。ray生成は画素内の任意位置を標本化するため、4隅で画素全体を囲う。
    bool ProjectPixelFootprint(const ScreenMapping& mapping, uint32_t x, uint32_t y,
                               float (&outX)[4], float (&outY)[4])
    {
        for (uint32_t corner = 0u; corner < 4u; ++corner)
        {
            const float screenU =
                (static_cast<float>(x) + static_cast<float>(corner & 1u)) / Width;
            const float screenV =
                (static_cast<float>(y) + static_cast<float>(corner >> 1u)) / Height;
            if (!ProjectToPlane(mapping, screenU, screenV, outX[corner], outY[corner]))
            {
                return false;
            }
        }
        return true;
    }

    bool IsFootprintInsideQuad(const float (&x)[4], const float (&y)[4])
    {
        constexpr float Margin = 0.01f;
        for (uint32_t corner = 0u; corner < 4u; ++corner)
        {
            if (std::abs(x[corner]) > QuadHalfSize - Margin ||
                std::abs(y[corner]) > QuadHalfSize - Margin)
            {
                return false;
            }
        }
        return true;
    }

    // 位置のみの三角形(-1,-1)(1,-1)(0,1)の内側に4隅が入るか。
    bool IsFootprintInsideTriangle(const float (&x)[4], const float (&y)[4])
    {
        constexpr float Margin = 0.01f;
        for (uint32_t corner = 0u; corner < 4u; ++corner)
        {
            if (y[corner] < -1.0f + Margin ||
                2.0f * x[corner] + y[corner] > 1.0f - Margin ||
                -2.0f * x[corner] + y[corner] > 1.0f - Margin)
            {
                return false;
            }
        }
        return true;
    }

    // 8x8 textureで双線形の隣接texelが同じ象限に入る座標なら象限番号、境界付近なら-1。
    int QuadrantInterior(float coordinate)
    {
        constexpr float Margin = 0.005f;
        if (coordinate >= 0.0625f + Margin && coordinate <= 0.4375f - Margin)
        {
            return 0;
        }
        if (coordinate >= 0.5625f + Margin && coordinate <= 0.9375f - Margin)
        {
            return 1;
        }
        return -1;
    }

    const float* PixelAt(const VariableArray<float>& pixels, uint32_t x, uint32_t y)
    {
        return pixels.data() + (static_cast<size_t>(y) * Width + x) * 4u;
    }

    bool NearlyEqual(const float* measured, const float (&expected)[3], float tolerance,
                     float& inOutMaxError)
    {
        bool bMatch = true;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const float error = std::abs(measured[channel] - expected[channel]);
            inOutMaxError = std::max(inOutMaxError, error);
            bMatch = bMatch && error <= tolerance;
        }
        return bMatch;
    }

    void Normalize(float (&value)[3])
    {
        const float length =
            std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        for (float& component : value)
        {
            component /= length;
        }
    }

    // 四角形の接空間基底はT=dP/du=+X、B=dP/dv=-Y、頂点法線N=-Z（カメラ側）。
    void ExpectedQuadShadingNormal(const uint8_t (&normalBytes)[3], float (&outNormal)[3])
    {
        const float tangent[3] = {Unorm(normalBytes[0]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[1]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[2]) * 2.0f - 1.0f};
        outNormal[0] = tangent[0];
        outNormal[1] = -tangent[1];
        outNormal[2] = -tangent[2];
        Normalize(outNormal);
    }

    // ラスタ（gbuffer.fragのCalculateTBN）の余接フレーム。T=∇u、B=∇vを長い方の長さで共通に
    // 正規化し、法線 = normalize(T·t.x + B·t.y + N·t.z)。
    void ExpectedCotangentShadingNormal(const float (&gradientU)[3], const float (&gradientV)[3],
                                        const float (&normal)[3],
                                        const uint8_t (&normalBytes)[3], float (&outNormal)[3])
    {
        const float lengthSquaredU = gradientU[0] * gradientU[0] + gradientU[1] * gradientU[1] +
                                     gradientU[2] * gradientU[2];
        const float lengthSquaredV = gradientV[0] * gradientV[0] + gradientV[1] * gradientV[1] +
                                     gradientV[2] * gradientV[2];
        const float inverseMaxLength = 1.0f / std::sqrt(std::max(lengthSquaredU, lengthSquaredV));
        const float tangent[3] = {Unorm(normalBytes[0]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[1]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[2]) * 2.0f - 1.0f};
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            outNormal[axis] = (gradientU[axis] * tangent[0] + gradientV[axis] * tangent[1]) *
                                  inverseMaxLength +
                              normal[axis] * tangent[2];
        }
        Normalize(outNormal);
    }

    // UVが退化したときの基底（ラスタのCalculateTBNと同じ）: T=normalize(up×N)、B=N×T。
    void ExpectedFallbackShadingNormal(const float (&normal)[3], const uint8_t (&normalBytes)[3],
                                       float (&outNormal)[3])
    {
        const bool bUseY = std::abs(normal[1]) < 0.999f;
        const float upVector[3] = {bUseY ? 0.0f : 1.0f, bUseY ? 1.0f : 0.0f, 0.0f};
        float tangentAxis[3] = {upVector[1] * normal[2] - upVector[2] * normal[1],
                                upVector[2] * normal[0] - upVector[0] * normal[2],
                                upVector[0] * normal[1] - upVector[1] * normal[0]};
        Normalize(tangentAxis);
        const float bitangentAxis[3] = {normal[1] * tangentAxis[2] - normal[2] * tangentAxis[1],
                                        normal[2] * tangentAxis[0] - normal[0] * tangentAxis[2],
                                        normal[0] * tangentAxis[1] - normal[1] * tangentAxis[0]};
        const float tangent[3] = {Unorm(normalBytes[0]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[1]) * 2.0f - 1.0f,
                                  Unorm(normalBytes[2]) * 2.0f - 1.0f};
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            outNormal[axis] = tangentAxis[axis] * tangent[0] + bitangentAxis[axis] * tangent[1] +
                              normal[axis] * tangent[2];
        }
        Normalize(outNormal);
    }

    // 範囲内の全画素が期待値と一致するか調べ、比較した画素数を返す。
    template <typename InsideFunction>
    uint32_t CheckUniformRegion(const ScreenMapping& mapping, const VariableArray<float>& pixels,
                                InsideFunction inside, const float (&expected)[3],
                                float tolerance, const char* label, bool& inOutPassed)
    {
        uint32_t checked = 0u;
        float maxError = 0.0f;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                float planeX[4];
                float planeY[4];
                if (!ProjectPixelFootprint(mapping, x, y, planeX, planeY) ||
                    !inside(planeX, planeY))
                {
                    continue;
                }
                ++checked;
                const float* measured = PixelAt(pixels, x, y);
                if (!NearlyEqual(measured, expected, tolerance, maxError) && inOutPassed)
                {
                    std::cerr << label << "が期待値と一致しません pixel=(" << x << ',' << y
                              << ") measured=(" << measured[0] << ',' << measured[1] << ','
                              << measured[2] << ") expected=(" << expected[0] << ','
                              << expected[1] << ',' << expected[2] << ")\n";
                    inOutPassed = false;
                }
            }
        }
        std::cout << label << "_pixels=" << checked << " max_error=" << maxError << '\n';
        return checked;
    }

    // 材質資源からRTスナップショットへtexture handleを値で写すことを確かめる。
    bool CheckMaterialSnapshotCopiesTextureHandles()
    {
        MaterialResourceData material;
        material.AlbedoTexture = TextureHandle{11u};
        material.NormalTexture = TextureHandle{12u};
        material.MetallicTexture = TextureHandle{13u};
        material.RoughnessTexture = TextureHandle{14u};
        const RayTracingHitMaterialSnapshot snapshot = MakeRayTracingHitMaterialSnapshot(&material);
        const RayTracingHitMaterialSnapshot empty = MakeRayTracingHitMaterialSnapshot(nullptr);
        return snapshot.AlbedoTexture == material.AlbedoTexture &&
               snapshot.NormalTexture == material.NormalTexture &&
               snapshot.MetallicTexture == material.MetallicTexture &&
               snapshot.RoughnessTexture == material.RoughnessTexture &&
               !empty.AlbedoTexture.IsValid() && !empty.NormalTexture.IsValid() &&
               !empty.MetallicTexture.IsValid() && !empty.RoughnessTexture.IsValid() &&
               empty.ObjectColor[0] == 1.0f && empty.ObjectColor[3] == 1.0f;
    }

    int RunTest()
    {
        if (!CheckMaterialSnapshotCopiesTextureHandles())
        {
            std::cerr << "材質snapshotがtexture handleを写しませんでした\n";
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
            !device->GetCapabilities().bBufferDeviceAddress ||
            !device->GetCapabilities().bSampledImageArrayNonUniformIndexing)
        {
            return ReportGpuTestSkip(TestName, "RT pipeline/BDA/非一様texture添字を利用できません");
        }
        ValidationMessenger messenger(device);
        if (!messenger.IsCreated())
        {
            std::cerr << "validation messengerを作成できませんでした\n";
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

        // 材質texture。RenderResourcesへ外部textureとして登録し、PTはhandleから解決する。
        uint8_t albedoPixels[8u * 8u * 4u] = {};
        for (uint32_t y = 0u; y < 8u; ++y)
        {
            for (uint32_t x = 0u; x < 8u; ++x)
            {
                const uint8_t* color = QuadrantColors[y / 4u][x / 4u];
                uint8_t* texel = albedoPixels + (y * 8u + x) * 4u;
                texel[0] = color[0];
                texel[1] = color[1];
                texel[2] = color[2];
                texel[3] = 255u;
            }
        }
        TexturePtr albedoTexture = CreateRgba8Texture(device, 8u, 8u, albedoPixels,
                                                      "PathTracingMaterialTest.Albedo");
        TexturePtr normalTexture = CreateSolidTexture(device, TiltedNormalBytes,
                                                      "PathTracingMaterialTest.Normal");
        TexturePtr metallicTexture = CreateSolidTexture(device, MetallicBytes,
                                                        "PathTracingMaterialTest.Metallic");
        TexturePtr roughnessTexture = CreateSolidTexture(device, RoughnessBytes,
                                                         "PathTracingMaterialTest.Roughness");
        if (!albedoTexture || !normalTexture || !metallicTexture || !roughnessTexture)
        {
            std::cerr << "材質textureを作成できませんでした\n";
            return 1;
        }
        TextureResources& textures = resources.Textures();
        const TextureHandle albedoHandle = textures.RegisterExternalTexture(albedoTexture, "Albedo");
        const TextureHandle normalHandle = textures.RegisterExternalTexture(normalTexture, "Normal");
        const TextureHandle metallicHandle =
            textures.RegisterExternalTexture(metallicTexture, "Metallic");
        const TextureHandle roughnessHandle =
            textures.RegisterExternalTexture(roughnessTexture, "Roughness");
        // 同じRHI textureを別handleで登録し、texture表が実体で重複を除くことを確かめる。
        const TextureHandle metallicAliasHandle =
            textures.RegisterExternalTexture(metallicTexture, "MetallicAlias");
        // 累積の途中で解放し、既定textureへの切り替えで履歴を捨てることを確かめる。
        const TextureHandle releasableAlbedoHandle =
            textures.RegisterExternalTexture(albedoTexture, "ReleasableAlbedo");
        if (!albedoHandle.IsValid() || !normalHandle.IsValid() || !metallicHandle.IsValid() ||
            !roughnessHandle.IsValid() || !metallicAliasHandle.IsValid() ||
            !releasableAlbedoHandle.IsValid() || metallicAliasHandle == metallicHandle)
        {
            std::cerr << "材質textureをRenderResourcesへ登録できませんでした\n";
            return 1;
        }

        // Mesh3DVertex（位置・法線・UV）の四角形。u=(x+S)/2S、v=(S-y)/2S、法線はカメラ側の-Z。
        static_assert(sizeof(Mesh3DVertex) == 32u, "Mesh3DVertexの並びがPTの読み取りと一致しません");
        const Mesh3DVertex quadVertices[4] = {
            {{-QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            {{QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
            {{QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
            {{-QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}}};
        const uint32_t quadIndices[6] = {0u, 1u, 2u, 0u, 2u, 3u};
        // u=(x+S)/2S+(S-y)/4S、v=(S-y)/4S。uとvの勾配の長さが違い直交もしないため、
        // T・Bを個別に正規化するとラスタの余接フレームと結果が変わる。
        const Mesh3DVertex stretchedVertices[4] = {
            {{-QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            {{QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
            {{QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.5f, 0.5f}},
            {{-QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f}}};
        const uint32_t stretchedIndices[6] = {0u, 1u, 2u, 0u, 3u, 2u};
        // 全頂点が同じUVの四角形。UVの勾配がなく、共通の退化判定で既定の基底へ落ちる。
        const Mesh3DVertex degenerateVertices[4] = {
            {{-QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.3f, 0.3f}},
            {{QuadHalfSize, QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.3f, 0.3f}},
            {{QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.3f, 0.3f}},
            {{-QuadHalfSize, -QuadHalfSize, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.3f, 0.3f}}};
        const float trianglePositions[9] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f};
        const uint32_t triangleIndices[3] = {0u, 1u, 2u};
        TestGeometry quad;
        TestGeometry stretchedQuad;
        TestGeometry degenerateQuad;
        TestGeometry positionOnlyTriangle;
        if (!BuildGeometry(device, quadVertices, 4u, sizeof(Mesh3DVertex), quadIndices, 6u, quad) ||
            !BuildGeometry(device, stretchedVertices, 4u, sizeof(Mesh3DVertex), stretchedIndices,
                           6u, stretchedQuad) ||
            !BuildGeometry(device, degenerateVertices, 4u, sizeof(Mesh3DVertex), quadIndices, 6u,
                           degenerateQuad) ||
            !BuildGeometry(device, trianglePositions, 3u, 3u * sizeof(float), triangleIndices, 3u,
                           positionOnlyTriangle))
        {
            std::cerr << "検証用の形状とBLASを作成できませんでした\n";
            return 1;
        }

        FramePacket quadPacket;
        quadPacket.RayTracingScene.Instances.push_back(MakeInstance(quad, 0u, 0.0f));
        RayTracingHitMaterialSnapshot& quadMaterial =
            quadPacket.RayTracingScene.Instances[0].Material;
        quadMaterial.AlbedoTexture = albedoHandle;
        quadMaterial.NormalTexture = normalHandle;
        quadMaterial.MetallicTexture = metallicHandle;
        quadMaterial.RoughnessTexture = roughnessHandle;
        FramePacket trianglePacket;
        trianglePacket.RayTracingScene.Instances.push_back(
            MakeInstance(positionOnlyTriangle, 0u, 0.0f));
        RayTracingHitMaterialSnapshot& triangleMaterial =
            trianglePacket.RayTracingScene.Instances[0].Material;
        triangleMaterial.AlbedoTexture = albedoHandle;
        triangleMaterial.NormalTexture = normalHandle;
        triangleMaterial.MetallicTexture = metallicHandle;
        triangleMaterial.RoughnessTexture = roughnessHandle;
        FramePacket stretchedPacket;
        stretchedPacket.RayTracingScene.Instances.push_back(MakeInstance(stretchedQuad, 0u, 0.0f));
        stretchedPacket.RayTracingScene.Instances[0].Material.NormalTexture = normalHandle;
        FramePacket degeneratePacket;
        degeneratePacket.RayTracingScene.Instances.push_back(MakeInstance(degenerateQuad, 0u, 0.0f));
        degeneratePacket.RayTracingScene.Instances[0].Material.NormalTexture = normalHandle;
        if (!BuildTopLevel(device, quadPacket) || !BuildTopLevel(device, trianglePacket) ||
            !BuildTopLevel(device, stretchedPacket) || !BuildTopLevel(device, degeneratePacket))
        {
            std::cerr << "検証用のTLASを作成できませんでした\n";
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
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.SnapshotScene = &quadPacket.Scene;
        context.SnapshotRayTracingScene = &quadPacket.RayTracingScene;
        context.Resources.Textures = &textures;
        CommandListPtr initializationCommand = device->CreateCommandList();
        context.CommandList = initializationCommand.get();
        PathTracingPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << "PT RT pipelineを作成できませんでした\n";
            return 1;
        }
        RenderGraph graph;
        graph.Initialize(nullptr);

        ScreenMapping mapping;
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(camera, 1.0f, device.get());
        cameraConstants.CopyShaderInverseViewProjection(mapping.InverseViewProjection);
        cameraConstants.CopyCameraPosition(mapping.CameraPosition);
        float centerX = 0.0f;
        float centerY = 0.0f;
        if (!ProjectToPlane(mapping, 0.5f, 0.5f, centerX, centerY) ||
            std::abs(centerX) > 0.001f || std::abs(centerY) > 0.001f)
        {
            std::cerr << "画面中心が平面の原点へ写りませんでした\n";
            return 1;
        }

        bool bPassed = true;
        VariableArray<float> pixels;
        uint64_t frame = 0u;
        const auto render = [&](PathTracingDebugOutput output, uint32_t expectedTextureCount,
                                const char* label)
        {
            pass.SetDebugOutput(output);
            ++frame;
            if (!RunFrame(device, graph, pass, context, frame, pixels) ||
                pass.GetAccumulatedSampleCount() != 1u)
            {
                std::cerr << label << ": 描画または履歴の破棄に失敗しました\n";
                return false;
            }
            if (pass.GetBoundMaterialTextureCount() != expectedTextureCount)
            {
                std::cerr << label << ": 束ねた材質texture数="
                          << pass.GetBoundMaterialTextureCount()
                          << " 期待値=" << expectedTextureCount << '\n';
                return false;
            }
            return true;
        };

        // 1. texture UV標本化とinstance色。象限内部に収まる画素だけを厳密値で比べる。
        if (!render(PathTracingDebugOutput::Albedo, 8u, "textured_albedo"))
        {
            return 1;
        }
        {
            uint32_t checked = 0u;
            uint32_t quadrantCounts[2][2] = {};
            float maxError = 0.0f;
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    float planeX[4];
                    float planeY[4];
                    if (!ProjectPixelFootprint(mapping, x, y, planeX, planeY) ||
                        !IsFootprintInsideQuad(planeX, planeY))
                    {
                        continue;
                    }
                    int uQuadrant = QuadrantInterior((planeX[0] + QuadHalfSize) / (2.0f * QuadHalfSize));
                    int vQuadrant = QuadrantInterior((QuadHalfSize - planeY[0]) / (2.0f * QuadHalfSize));
                    for (uint32_t corner = 1u; corner < 4u; ++corner)
                    {
                        if (QuadrantInterior((planeX[corner] + QuadHalfSize) / (2.0f * QuadHalfSize)) != uQuadrant)
                        {
                            uQuadrant = -1;
                        }
                        if (QuadrantInterior((QuadHalfSize - planeY[corner]) / (2.0f * QuadHalfSize)) != vQuadrant)
                        {
                            vQuadrant = -1;
                        }
                    }
                    if (uQuadrant < 0 || vQuadrant < 0)
                    {
                        continue;
                    }
                    const uint8_t* color = QuadrantColors[vQuadrant][uQuadrant];
                    const float expected[3] = {ObjectColor[0] * Unorm(color[0]),
                                               ObjectColor[1] * Unorm(color[1]),
                                               ObjectColor[2] * Unorm(color[2])};
                    const float* measured = PixelAt(pixels, x, y);
                    if (!NearlyEqual(measured, expected, ValueTolerance, maxError) && bPassed)
                    {
                        std::cerr << "texture UVの標本値が一致しません pixel=(" << x << ',' << y
                                  << ") quadrant=(" << uQuadrant << ',' << vQuadrant
                                  << ") measured=(" << measured[0] << ',' << measured[1] << ','
                                  << measured[2] << ") expected=(" << expected[0] << ','
                                  << expected[1] << ',' << expected[2] << ")\n";
                        bPassed = false;
                    }
                    ++checked;
                    ++quadrantCounts[vQuadrant][uQuadrant];
                }
            }
            std::cout << "textured_albedo_pixels=" << checked << " quadrants=("
                      << quadrantCounts[0][0] << ',' << quadrantCounts[0][1] << ','
                      << quadrantCounts[1][0] << ',' << quadrantCounts[1][1]
                      << ") max_error=" << maxError << '\n';
            if (checked < 256u || quadrantCounts[0][0] < 32u || quadrantCounts[0][1] < 32u ||
                quadrantCounts[1][0] < 32u || quadrantCounts[1][1] < 32u)
            {
                std::cerr << "象限内部の比較画素が足りません\n";
                bPassed = false;
            }
        }

        const auto insideQuad = [](const float (&x)[4], const float (&y)[4])
        {
            return IsFootprintInsideQuad(x, y);
        };
        const auto insideTriangle = [](const float (&x)[4], const float (&y)[4])
        {
            return IsFootprintInsideTriangle(x, y);
        };
        const uint32_t fullScreenPixels = Width * Height;

        // 2. metallic・roughnessはR成分をそれぞれのtextureから読む。
        if (!render(PathTracingDebugOutput::MetallicRoughness, 8u, "textured_material"))
        {
            return 1;
        }
        const float texturedMaterial[3] = {Unorm(MetallicBytes[0]), Unorm(RoughnessBytes[0]), 0.0f};
        if (CheckUniformRegion(mapping, pixels, insideQuad, texturedMaterial, ValueTolerance,
                               "textured_material", bPassed) != fullScreenPixels)
        {
            std::cerr << "四角形が画面全体を覆っていません\n";
            bPassed = false;
        }

        // 3. 法線マップ。UVから作る接空間基底で頂点法線を傾ける。
        if (!render(PathTracingDebugOutput::ShadingNormal, 8u, "textured_normal"))
        {
            return 1;
        }
        float texturedNormal[3];
        ExpectedQuadShadingNormal(TiltedNormalBytes, texturedNormal);
        CheckUniformRegion(mapping, pixels, insideQuad, texturedNormal, NormalTolerance,
                           "textured_normal", bPassed);

        // 3b. 伸縮・傾斜したUVでも、ラスタと同じ余接フレームで法線マップを適用する。
        //     2枚目の三角形は巻き順が逆で、辺から求める行列式の符号が反転する。
        context.SnapshotScene = &stretchedPacket.Scene;
        context.SnapshotRayTracingScene = &stretchedPacket.RayTracingScene;
        if (!render(PathTracingDebugOutput::ShadingNormal, 5u, "stretched_normal"))
        {
            return 1;
        }
        const float stretchedGradientU[3] = {0.5f / QuadHalfSize, -0.25f / QuadHalfSize, 0.0f};
        const float stretchedGradientV[3] = {0.0f, -0.25f / QuadHalfSize, 0.0f};
        const float vertexNormal[3] = {0.0f, 0.0f, -1.0f};
        float stretchedNormal[3];
        ExpectedCotangentShadingNormal(stretchedGradientU, stretchedGradientV, vertexNormal,
                                       TiltedNormalBytes, stretchedNormal);
        CheckUniformRegion(mapping, pixels, insideQuad, stretchedNormal, NormalTolerance,
                           "stretched_normal", bPassed);

        // 3c. UVが退化した面は、ラスタと同じ既定の基底で法線マップを適用する。
        context.SnapshotScene = &degeneratePacket.Scene;
        context.SnapshotRayTracingScene = &degeneratePacket.RayTracingScene;
        if (!render(PathTracingDebugOutput::ShadingNormal, 5u, "degenerate_uv_normal"))
        {
            return 1;
        }
        float degenerateNormal[3];
        ExpectedFallbackShadingNormal(vertexNormal, TiltedNormalBytes, degenerateNormal);
        CheckUniformRegion(mapping, pixels, insideQuad, degenerateNormal, NormalTolerance,
                           "degenerate_uv_normal", bPassed);
        context.SnapshotScene = &quadPacket.Scene;
        context.SnapshotRayTracingScene = &quadPacket.RayTracingScene;

        // 4. 同じRHI textureを指す別handleは表の1要素にまとまる。
        quadMaterial.RoughnessTexture = metallicAliasHandle;
        if (!render(PathTracingDebugOutput::MetallicRoughness, 7u, "aliased_material"))
        {
            return 1;
        }
        const float aliasedMaterial[3] = {Unorm(MetallicBytes[0]), Unorm(MetallicBytes[0]), 0.0f};
        CheckUniformRegion(mapping, pixels, insideQuad, aliasedMaterial, ValueTolerance,
                           "aliased_material", bPassed);

        // 5. 未設定のtextureはGBufferと同じ既定値（白・平坦法線・metallic 0・roughness中間灰）。
        quadMaterial.AlbedoTexture = TextureHandle();
        quadMaterial.NormalTexture = TextureHandle();
        quadMaterial.MetallicTexture = TextureHandle();
        quadMaterial.RoughnessTexture = TextureHandle();
        if (!render(PathTracingDebugOutput::Albedo, 4u, "default_albedo"))
        {
            return 1;
        }
        const float defaultAlbedo[3] = {ObjectColor[0], ObjectColor[1], ObjectColor[2]};
        CheckUniformRegion(mapping, pixels, insideQuad, defaultAlbedo, ValueTolerance,
                           "default_albedo", bPassed);
        if (!render(PathTracingDebugOutput::MetallicRoughness, 4u, "default_material"))
        {
            return 1;
        }
        const float defaultMaterial[3] = {0.0f, Unorm(128u), 0.0f};
        CheckUniformRegion(mapping, pixels, insideQuad, defaultMaterial, ValueTolerance,
                           "default_material", bPassed);
        if (!render(PathTracingDebugOutput::ShadingNormal, 4u, "default_normal"))
        {
            return 1;
        }
        const uint8_t flatNormalBytes[3] = {128u, 128u, 255u};
        float defaultNormal[3];
        ExpectedQuadShadingNormal(flatNormalBytes, defaultNormal);
        CheckUniformRegion(mapping, pixels, insideQuad, defaultNormal, NormalTolerance,
                           "default_normal", bPassed);

        // 5b. 累積中にhandleを解放すると解決結果が既定の白へ変わり、履歴を捨てる。
        quadMaterial.AlbedoTexture = releasableAlbedoHandle;
        if (!render(PathTracingDebugOutput::Albedo, 5u, "release_before"))
        {
            return 1;
        }
        for (uint32_t extra = 0u; extra < 3u; ++extra)
        {
            ++frame;
            if (!RunFrame(device, graph, pass, context, frame, pixels) ||
                pass.GetAccumulatedSampleCount() != extra + 2u)
            {
                std::cerr << "解放前の材質で累積できませんでした\n";
                return 1;
            }
        }
        textures.ReleaseTexture(releasableAlbedoHandle);
        ++frame;
        if (!RunFrame(device, graph, pass, context, frame, pixels) ||
            pass.GetAccumulatedSampleCount() != 1u ||
            pass.GetBoundMaterialTextureCount() != 4u)
        {
            std::cerr << "handle解放後の既定textureへの切り替えで履歴を捨てませんでした samples="
                      << pass.GetAccumulatedSampleCount() << " textures="
                      << pass.GetBoundMaterialTextureCount() << '\n';
            return 1;
        }
        CheckUniformRegion(mapping, pixels, insideQuad, defaultAlbedo, ValueTolerance,
                           "released_albedo", bPassed);
        quadMaterial.AlbedoTexture = TextureHandle();

        // 6. 発光は色×nitsにプリエクスポージャを掛ける。空を要求してLUTがないフレームは不交差が黒になり、
        //    傾いた法線マップで散乱が面の裏へ向いても自己交差で発光を重ねないことを確かめる。
        quadMaterial.NormalTexture = normalHandle;
        quadPacket.Scene.SkyAtmosphere.bEnabled = true;
        quadMaterial.EmissiveColor[0] = 1.0f;
        quadMaterial.EmissiveColor[1] = 0.5f;
        quadMaterial.EmissiveColor[2] = 0.25f;
        quadMaterial.EmissiveLuminanceNits = 4.0f;
        camera.PreExposure = 0.125f;
        if (!render(PathTracingDebugOutput::None, 5u, "emission_low_exposure"))
        {
            return 1;
        }
        VariableArray<float> lowExposure = pixels;
        float expectedEmission[3];
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            expectedEmission[channel] =
                quadMaterial.EmissiveColor[channel] * quadMaterial.EmissiveLuminanceNits *
                camera.PreExposure;
        }
        CheckUniformRegion(mapping, pixels, insideQuad, expectedEmission, ValueTolerance,
                           "emission_low_exposure", bPassed);
        camera.PreExposure = 0.5f;
        if (!render(PathTracingDebugOutput::None, 5u, "emission_high_exposure"))
        {
            return 1;
        }
        {
            // 露出の差はそのまま発光の差になる。
            float maxError = 0.0f;
            for (uint32_t index = 0u; index < Width * Height; ++index)
            {
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const float expectedDifference = quadMaterial.EmissiveColor[channel] *
                        quadMaterial.EmissiveLuminanceNits * (0.5f - 0.125f);
                    const float difference =
                        pixels[index * 4u + channel] - lowExposure[index * 4u + channel];
                    maxError = std::max(maxError, std::abs(difference - expectedDifference));
                }
            }
            std::cout << "emission_exposure_scaling_max_error=" << maxError << '\n';
            if (maxError > ValueTolerance)
            {
                std::cerr << "発光がプリエクスポージャに比例しませんでした\n";
                bPassed = false;
            }
        }
        camera.PreExposure = 1.0f;
        quadPacket.Scene.SkyAtmosphere.bEnabled = false;
        quadMaterial.NormalTexture = TextureHandle();

        // 7. 位置のみの頂点（stride 12）は幾何法線とUV 0へfallbackし、法線マップを使わない。
        context.SnapshotScene = &trianglePacket.Scene;
        context.SnapshotRayTracingScene = &trianglePacket.RayTracingScene;
        if (!render(PathTracingDebugOutput::Albedo, 8u, "position_only_albedo"))
        {
            return 1;
        }
        // UV 0はWrapの双線形で4象限の角texelを等分に混ぜる。
        float positionOnlyAlbedo[3] = {};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            float sum = 0.0f;
            for (uint32_t v = 0u; v < 2u; ++v)
            {
                for (uint32_t u = 0u; u < 2u; ++u)
                {
                    sum += Unorm(QuadrantColors[v][u][channel]);
                }
            }
            positionOnlyAlbedo[channel] = ObjectColor[channel] * sum * 0.25f;
        }
        const uint32_t trianglePixels = CheckUniformRegion(
            mapping, pixels, insideTriangle, positionOnlyAlbedo, 0.003f,
            "position_only_albedo", bPassed);
        if (trianglePixels < 64u)
        {
            std::cerr << "三角形内部の比較画素が足りません\n";
            bPassed = false;
        }
        if (!render(PathTracingDebugOutput::ShadingNormal, 8u, "position_only_normal"))
        {
            return 1;
        }
        const float geometricNormal[3] = {0.0f, 0.0f, -1.0f};
        CheckUniformRegion(mapping, pixels, insideTriangle, geometricNormal, NormalTolerance,
                           "position_only_normal", bPassed);
        if (!render(PathTracingDebugOutput::MetallicRoughness, 8u, "position_only_material"))
        {
            return 1;
        }
        CheckUniformRegion(mapping, pixels, insideTriangle, texturedMaterial, ValueTolerance,
                           "position_only_material", bPassed);

        // 8. texture表の上限。前の63 instanceで表を埋め、最後に並ぶ見える四角形は既定textureへ落ちる。
        constexpr uint32_t OverflowInstanceCount = 64u;
        VariableArray<TexturePtr> overflowTextures;
        FramePacket overflowPacket;
        for (uint32_t instanceIndex = 0u; instanceIndex < OverflowInstanceCount; ++instanceIndex)
        {
            const bool bVisible = instanceIndex + 1u == OverflowInstanceCount;
            RayTracingSceneInstanceSnapshot snapshot = MakeInstance(
                quad, instanceIndex, bVisible ? 0.0f : 1.0f + static_cast<float>(instanceIndex));
            TextureHandle handles[4];
            for (uint32_t slot = 0u; slot < 4u; ++slot)
            {
                const uint32_t textureIndex = instanceIndex * 4u + slot;
                const uint8_t color[3] = {static_cast<uint8_t>(255u - textureIndex % 64u),
                                          static_cast<uint8_t>(textureIndex % 7u),
                                          static_cast<uint8_t>(textureIndex / 4u)};
                TexturePtr texture = CreateSolidTexture(device, color, "PathTracingMaterialTest.Overflow");
                if (!texture)
                {
                    std::cerr << "上限検証用textureを作成できませんでした\n";
                    return 1;
                }
                overflowTextures.push_back(texture);
                handles[slot] = textures.RegisterExternalTexture(texture, "Overflow");
            }
            snapshot.Material.AlbedoTexture = handles[0];
            snapshot.Material.NormalTexture = handles[1];
            snapshot.Material.MetallicTexture = handles[2];
            snapshot.Material.RoughnessTexture = handles[3];
            overflowPacket.RayTracingScene.Instances.push_back(snapshot);
        }
        if (!BuildTopLevel(device, overflowPacket))
        {
            std::cerr << "上限検証用のTLASを作成できませんでした\n";
            return 1;
        }
        context.SnapshotScene = &overflowPacket.Scene;
        context.SnapshotRayTracingScene = &overflowPacket.RayTracingScene;
        if (!render(PathTracingDebugOutput::Albedo, PathTracingMaterialTextureCapacity,
                    "overflow_albedo"))
        {
            return 1;
        }
        CheckUniformRegion(mapping, pixels, insideQuad, defaultAlbedo, ValueTolerance,
                           "overflow_albedo", bPassed);

        pass.Shutdown();
        graph.Shutdown();
        device->WaitIdle();
        overflowTextures.clear();
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
            std::cout << "pt_material_uv=true instance_color=true metallic_roughness=true "
                         "normal_map=true cotangent_frame=true degenerate_uv_fallback=true "
                         "defaults=true texture_dedup=true "
                         "texture_release_reset=true scatter_below_surface_terminated=true "
                         "emission_pre_exposure=true position_only_fallback=true "
                         "texture_table_overflow=true\n";
        }
        return bPassed ? 0 : 1;
    }
}

int main()
{
    return RunTest();
}
