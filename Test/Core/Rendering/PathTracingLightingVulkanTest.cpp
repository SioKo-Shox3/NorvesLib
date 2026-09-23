// PTの光輸送を解析値と白炉で確かめる。R1と同じ15行の白炉（と粗さ0）、正距円筒環境の向き、
// 点・spot・方向光の解析輝度（純Lambertと本番BSDF）、面光源と太陽円盤のNEEのみ・BSDFのみ・MISの一致、
// 視線がシェーディング法線の裏になる面での戦略の一致、光源の直前にある遮蔽物。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Rendering/CameraViewConstants.h"
#include "Rendering/DfgLut.h"
#include "Rendering/FramePacket.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderResources.h"
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

    constexpr const char* TestName = "PathTracingLightingVulkanTest";
    constexpr uint32_t Width = 64u;
    constexpr uint32_t Height = 64u;
    constexpr uint64_t ReadbackBytes = Width * Height * 4u * sizeof(float);
    constexpr double Pi = 3.14159265358979323846;

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

    // 光源表・発光表・環境textureの束縛誤りを画素値以外でも捉えるため、validationの警告とエラーを数える。
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

    // 期待値の計算に使う倍精度の3成分ベクトル。
    struct Vec3
    {
        double X = 0.0;
        double Y = 0.0;
        double Z = 0.0;
    };

    Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }
    Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }
    Vec3 operator*(const Vec3& a, double s) { return {a.X * s, a.Y * s, a.Z * s}; }
    double Dot(const Vec3& a, const Vec3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
    Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
    }
    double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
    Vec3 Normalize(const Vec3& a) { return a * (1.0 / Length(a)); }

    struct TestGeometry
    {
        BufferPtr VertexBuffer;
        BufferPtr IndexBuffer;
        AccelerationStructurePtr BottomLevel;
        uint32_t VertexCount = 0u;
        uint32_t VertexStride = 0u;
        uint32_t IndexCount = 0u;
    };

    bool BuildGeometry(const DevicePtr& device, const VariableArray<Mesh3DVertex>& vertices,
                       const VariableArray<uint32_t>& indices, TestGeometry& outGeometry)
    {
        const uint64_t vertexBytes = vertices.size() * sizeof(Mesh3DVertex);
        const uint64_t indexBytes = indices.size() * sizeof(uint32_t);
        BufferDesc vertexDesc(vertexBytes,
                              ResourceUsage::VertexBuffer | ResourceUsage::BufferDeviceAddress,
                              true, "PathTracingLightingTest.Vertices");
        BufferDesc indexDesc(indexBytes,
                             ResourceUsage::IndexBuffer | ResourceUsage::BufferDeviceAddress,
                             true, "PathTracingLightingTest.Indices");
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
        outGeometry.VertexStride = sizeof(Mesh3DVertex);
        outGeometry.IndexCount = indexCount;
        return true;
    }

    // 4隅の四角形（隅は反時計回り）を2枚の三角形にする。
    bool BuildQuad(const DevicePtr& device, const Vec3 (&corners)[4], const Vec3& normal,
                   TestGeometry& outGeometry)
    {
        VariableArray<Mesh3DVertex> vertices;
        const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        for (uint32_t corner = 0u; corner < 4u; ++corner)
        {
            Mesh3DVertex vertex;
            vertex.Position[0] = static_cast<float>(corners[corner].X);
            vertex.Position[1] = static_cast<float>(corners[corner].Y);
            vertex.Position[2] = static_cast<float>(corners[corner].Z);
            vertex.Normal[0] = static_cast<float>(normal.X);
            vertex.Normal[1] = static_cast<float>(normal.Y);
            vertex.Normal[2] = static_cast<float>(normal.Z);
            vertex.TexCoord[0] = uvs[corner][0];
            vertex.TexCoord[1] = uvs[corner][1];
            vertices.push_back(vertex);
        }
        VariableArray<uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
        return BuildGeometry(device, vertices, indices, outGeometry);
    }

    RayTracingSceneInstanceSnapshot MakeInstance(const TestGeometry& geometry, uint32_t customIndex,
                                                 const float (&objectColor)[3])
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
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            snapshot.Material.ObjectColor[channel] = objectColor[channel];
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

    TexturePtr CreateTexture(const DevicePtr& device, uint32_t width, uint32_t height,
                             Format format, uint32_t bytesPerPixel, const void* pixels,
                             const char* debugName)
    {
        TextureDesc desc;
        desc.Width = width;
        desc.Height = height;
        desc.TextureFormat = format;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = debugName;
        TexturePtr texture = device->CreateTexture(desc);
        if (texture)
        {
            texture->Update(pixels, width * bytesPerPixel, width * height * bytesPerPixel);
        }
        return texture;
    }

    TexturePtr CreateGrayTexture(const DevicePtr& device, uint8_t value, const char* debugName)
    {
        const uint8_t pixel[4] = {value, value, value, 255u};
        return CreateTexture(device, 1u, 1u, Format::R8G8B8A8_UNORM, 4u, pixel, debugName);
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
        /** @brief 空と太陽の検証だけで使う。nullなら空LUTを作らない。 */
        SkyAtmospherePass* Sky = nullptr;
        uint64_t FrameNumber = 0u;

        // sampleCount回の試料を累積し、最後の累積画像を読み戻す。初回で履歴を捨てることも確かめる。
        bool Accumulate(uint32_t sampleCount, VariableArray<float>& outPixels, const char* label)
        {
            for (uint32_t sample = 0u; sample < sampleCount; ++sample)
            {
                const bool bLast = sample + 1u == sampleCount;
                if (!RunFrame(bLast, outPixels) ||
                    Pass->GetAccumulatedSampleCount() != sample + 1u)
                {
                    std::cerr << label << ": " << sample + 1u
                              << "試料目の描画または累積に失敗しました samples="
                              << Pass->GetAccumulatedSampleCount() << '\n';
                    return false;
                }
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
                                        "PathTracingLightingTest.Readback");
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
            if (Sky)
            {
                Graph->AddPass(Sky);
            }
            Graph->AddPass(Pass);
            if (!Graph->Compile(context))
            {
                commandList->End();
                return false;
            }
            const RenderGraphExecutionResult result = Graph->ExecuteWithResult(context);
            TexturePtr output;
            if (!result.bSuccess || result.ExecutedPassCount != (Sky ? 2u : 1u) ||
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
    };

    // PTのray生成と同じ規約（画面uv→NDC→逆ビュー射影の遠平面点）でカメラ光線を作る。
    struct ScreenMapping
    {
        float InverseViewProjection[16] = {};
        Vec3 Origin;

        void Build(const CameraProxy& camera, const IDevice* device)
        {
            const CameraViewConstants constants =
                CameraViewConstants::BuildForDevice(camera, 1.0f, device);
            constants.CopyShaderInverseViewProjection(InverseViewProjection);
            float position[4] = {};
            constants.CopyCameraPosition(position);
            Origin = {position[0], position[1], position[2]};
        }

        Vec3 Direction(double screenU, double screenV) const
        {
            const double ndc[4] = {screenU * 2.0 - 1.0, screenV * 2.0 - 1.0, 1.0, 1.0};
            double farPoint[4] = {};
            for (uint32_t row = 0u; row < 4u; ++row)
            {
                for (uint32_t column = 0u; column < 4u; ++column)
                {
                    farPoint[row] += InverseViewProjection[column * 4u + row] * ndc[column];
                }
            }
            const Vec3 target{farPoint[0] / farPoint[3], farPoint[1] / farPoint[3],
                              farPoint[2] / farPoint[3]};
            return Normalize(target - Origin);
        }
    };

    bool IntersectPlane(const Vec3& origin, const Vec3& direction, const Vec3& planePoint,
                        const Vec3& planeNormal, Vec3& outPoint)
    {
        const double denominator = Dot(direction, planeNormal);
        if (std::abs(denominator) < 1.0e-9)
        {
            return false;
        }
        const double distance = Dot(planePoint - origin, planeNormal) / denominator;
        if (!(distance > 0.0))
        {
            return false;
        }
        outPoint = origin + direction * distance;
        return true;
    }

    const float* PixelAt(const VariableArray<float>& pixels, uint32_t x, uint32_t y)
    {
        return pixels.data() + (static_cast<size_t>(y) * Width + x) * 4u;
    }

    float HalfToFloat(uint16_t value)
    {
        const uint32_t sign = (static_cast<uint32_t>(value) & 0x8000u) << 16u;
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;
        uint32_t bits = 0u;
        if (exponent == 0u)
        {
            if (mantissa == 0u)
            {
                bits = sign;
            }
            else
            {
                const float subnormal = std::ldexp(static_cast<float>(mantissa), -24);
                return sign ? -subnormal : subnormal;
            }
        }
        else if (exponent == 31u)
        {
            bits = sign | 0x7F800000u | (mantissa << 13u);
        }
        else
        {
            bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
        }
        float result = 0.0f;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    // シェーダーと同じ座標規則（半texel内側へclamp、双線形）でDFG LUTを読む。
    void SampleDfg(double NdotV, double roughness, double& outA, double& outB)
    {
        const uint16_t* lut = GetDfgLutHalfData();
        const double size = static_cast<double>(DfgLutSize);
        const double u = std::clamp(NdotV, 0.5 / size, (size - 0.5) / size);
        const double v = std::clamp(roughness, 0.5 / size, (size - 0.5) / size);
        const double x = u * size - 0.5;
        const double y = v * size - 0.5;
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, static_cast<int>(DfgLutSize) - 1);
        const int y1 = std::min(y0 + 1, static_cast<int>(DfgLutSize) - 1);
        const double fx = x - x0;
        const double fy = y - y0;
        const auto at = [&](int xi, int yi, int component)
        {
            return static_cast<double>(
                HalfToFloat(lut[(static_cast<size_t>(yi) * DfgLutSize + xi) * 2u + component]));
        };
        for (int component = 0; component < 2; ++component)
        {
            const double value =
                (at(x0, y0, component) * (1.0 - fx) + at(x1, y0, component) * fx) * (1.0 - fy) +
                (at(x0, y1, component) * (1.0 - fx) + at(x1, y1, component) * fx) * fy;
            (component == 0 ? outA : outB) = value;
        }
    }

    // 本番BSDF（PathTracingBsdf.glslのEvaluatePathBsdf）を倍精度で写したもの。
    void EvaluateProductionBsdf(const Vec3& normal, const Vec3& view, const Vec3& light,
                                const double (&albedo)[3], double metallic, double roughness,
                                double (&outBsdf)[3])
    {
        const double NdotV = std::max(Dot(normal, view), 1.0e-4);
        const double NdotL = Dot(normal, light);
        for (double& value : outBsdf)
        {
            value = 0.0;
        }
        if (NdotL <= 0.0)
        {
            return;
        }
        // シェーダーと同じく、葉の粗さをDFG LUTの標本域へ揃える。
        const double size = static_cast<double>(DfgLutSize);
        roughness = std::clamp(roughness, 0.5 / size, (size - 0.5) / size);
        double A = 0.0;
        double B = 0.0;
        SampleDfg(NdotV, roughness, A, B);
        const double Ess = std::max(A + B, 0.0001);
        const double F0d = 0.04;
        const double compD = 1.0 + F0d * (1.0 - Ess) / Ess;
        const double Ed = std::clamp((F0d * A + B) * compD, 0.0, 1.0);
        const Vec3 half = Normalize(view + light);
        const double NdotH = std::max(Dot(normal, half), 0.0);
        const double VdotH = std::max(Dot(view, half), 0.0);
        const double alpha = std::max(roughness * roughness, 1.0e-4);
        const double a2 = alpha * alpha;
        const double dTerm = NdotH * NdotH * (a2 - 1.0) + 1.0;
        const double D = a2 / (Pi * dTerm * dTerm);
        const double k = roughness * roughness * 0.5;
        const double G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
        const double specularCommon = D * G / (4.0 * NdotV * NdotL);
        const double fresnelWeight = std::pow(1.0 - VdotH, 5.0);
        const double Fd = F0d + (1.0 - F0d) * fresnelWeight;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            const double compC = 1.0 + albedo[channel] * (1.0 - Ess) / Ess;
            const double Fc = albedo[channel] + (1.0 - albedo[channel]) * fresnelWeight;
            const double diffuse = (1.0 - metallic) * (1.0 - Ed) * albedo[channel] / Pi;
            outBsdf[channel] = diffuse + specularCommon *
                ((1.0 - metallic) * Fd * compD + metallic * Fc * compC);
        }
    }

    // ラスタ（lighting.frag）と同じ点・spot・方向光の照度（色度×強度×減衰×余弦）。
    struct PunctualLightCase
    {
        LightType Type = LightType::Point;
        Vec3 Position;
        Vec3 Direction;
        float Color[3] = {1.0f, 1.0f, 1.0f};
        float Intensity = 1.0f;
        float Range = 1000.0f;
        float InnerCosine = 1.0f;
        float OuterCosine = 1.0f;
    };

    LightProxy MakeLightProxy(const PunctualLightCase& light)
    {
        LightProxy proxy;
        proxy.LightId = 1u;
        proxy.Type = light.Type;
        proxy.PositionX = static_cast<float>(light.Position.X);
        proxy.PositionY = static_cast<float>(light.Position.Y);
        proxy.PositionZ = static_cast<float>(light.Position.Z);
        proxy.DirectionX = static_cast<float>(light.Direction.X);
        proxy.DirectionY = static_cast<float>(light.Direction.Y);
        proxy.DirectionZ = static_cast<float>(light.Direction.Z);
        proxy.ColorR = light.Color[0];
        proxy.ColorG = light.Color[1];
        proxy.ColorB = light.Color[2];
        proxy.CanonicalIntensity = light.Intensity;
        proxy.Range = light.Range;
        proxy.InnerConeAngle = light.InnerCosine;
        proxy.OuterConeAngle = light.OuterCosine;
        return proxy;
    }

    // 表面点での入射方向と照度（色度を掛ける前の強度×減衰）。
    bool EvaluatePunctualIncidence(const PunctualLightCase& light, const Vec3& point,
                                   Vec3& outDirection, double& outIrradianceScale)
    {
        if (light.Type == LightType::Directional)
        {
            outDirection = Normalize(light.Direction * -1.0);
            outIrradianceScale = light.Intensity;
            return true;
        }
        const Vec3 toLight = light.Position - point;
        const double distance = Length(toLight);
        outDirection = toLight * (1.0 / distance);
        const double ratio = distance / std::max(static_cast<double>(light.Range), 0.0001);
        const double window = std::max(1.0 - ratio * ratio * ratio * ratio, 0.0);
        double attenuation = window * window / std::max(distance * distance, 0.01 * 0.01);
        if (light.Type == LightType::Spot)
        {
            const double theta = Dot(outDirection, Normalize(light.Direction * -1.0));
            const double epsilon = std::max(static_cast<double>(light.InnerCosine) -
                                                light.OuterCosine,
                                            0.001);
            attenuation *= std::clamp((theta - light.OuterCosine) / epsilon, 0.0, 1.0);
        }
        outIrradianceScale = light.Intensity * attenuation;
        return true;
    }

    void Chromaticity(const float (&color)[3], double (&outChroma)[3])
    {
        const double luminance = 0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2];
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            // ラスタの詰め方（LightingPassLightPacking）と同じくfloatへ丸める。
            outChroma[channel] = static_cast<float>(color[channel] / luminance);
        }
    }

    // 均一放射の多角形光源による照度係数（Lambertの公式、E=Le×係数）。
    double PolygonIrradianceFactor(const Vec3& point, const Vec3& normal, const Vec3 (&corners)[4])
    {
        double sum = 0.0;
        for (uint32_t index = 0u; index < 4u; ++index)
        {
            const Vec3 a = Normalize(corners[index] - point);
            const Vec3 b = Normalize(corners[(index + 1u) % 4u] - point);
            const double angle = std::acos(std::clamp(Dot(a, b), -1.0, 1.0));
            sum += angle * Dot(normal, Normalize(Cross(a, b)));
        }
        return 0.5 * std::abs(sum);
    }

    struct PlaneScene
    {
        Vec3 Point;
        Vec3 Normal; // カメラ側
    };

    // 画素内のsubSamples×subSamples点の平均・最小・最大。平面に当たらない点があれば偽。
    template <typename ExpectedFunction>
    bool IntegratePixel(const ScreenMapping& mapping, const PlaneScene& plane,
                        ExpectedFunction& expected, uint32_t x, uint32_t y, uint32_t subSamples,
                        double (&outAverage)[3], double (&outMinimum)[3], double (&outMaximum)[3])
    {
        double sum[3] = {};
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outMinimum[channel] = 1.0e30;
            outMaximum[channel] = -1.0e30;
        }
        for (uint32_t sy = 0u; sy < subSamples; ++sy)
        {
            for (uint32_t sx = 0u; sx < subSamples; ++sx)
            {
                const double screenU = (x + (sx + 0.5) / subSamples) / Width;
                const double screenV = (y + (sy + 0.5) / subSamples) / Height;
                const Vec3 direction = mapping.Direction(screenU, screenV);
                Vec3 point;
                if (!IntersectPlane(mapping.Origin, direction, plane.Point, plane.Normal, point))
                {
                    return false;
                }
                double value[3] = {};
                expected(point, direction * -1.0, value);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    sum[channel] += value[channel];
                    outMinimum[channel] = std::min(outMinimum[channel], value[channel]);
                    outMaximum[channel] = std::max(outMaximum[channel], value[channel]);
                }
            }
        }
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            outAverage[channel] = sum[channel] / (subSamples * subSamples);
        }
        return true;
    }

    // 画素内の点の平均（画素の積分）と、sampleCount試料の累積値を比べる。許容差は相対0.2%に、
    // 画素内の値の広がりから見積もった一様ジッタの標準誤差の3倍を足したもの。
    // 照明の境界（spot円錐の縁）を含む画素は8x8では積分が粗いため、64x64で積分し直す。
    // 画素内のどの点でも照明が0なら、測定値も厳密に0であることを確かめる。
    template <typename ExpectedFunction>
    bool CheckPixelAverages(const ScreenMapping& mapping, const PlaneScene& plane,
                            const VariableArray<float>& pixels, ExpectedFunction expected,
                            uint32_t sampleCount, const char* label, uint32_t& outZeroPixels)
    {
        double maxRelativeError = 0.0;
        outZeroPixels = 0u;
        bool bPassed = true;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                double averageValue[3] = {};
                double minimum[3] = {};
                double maximum[3] = {};
                if (!IntegratePixel(mapping, plane, expected, x, y, 8u, averageValue, minimum,
                                    maximum))
                {
                    continue;
                }
                const bool bBoundary = (minimum[0] <= 0.0 && maximum[0] > 0.0) ||
                                       (minimum[1] <= 0.0 && maximum[1] > 0.0) ||
                                       (minimum[2] <= 0.0 && maximum[2] > 0.0);
                if (bBoundary)
                {
                    IntegratePixel(mapping, plane, expected, x, y, 64u, averageValue, minimum,
                                   maximum);
                }
                const float* measured = PixelAt(pixels, x, y);
                const bool bAllZero = maximum[0] <= 0.0 && maximum[1] <= 0.0 && maximum[2] <= 0.0;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double average = averageValue[channel];
                    const double jitterError = 3.0 * (maximum[channel] - minimum[channel]) /
                                               std::sqrt(12.0 * sampleCount);
                    const double tolerance = 1.0e-6 + 0.002 * average + jitterError;
                    const double error = std::abs(measured[channel] - average);
                    if (average > 1.0e-4)
                    {
                        maxRelativeError = std::max(maxRelativeError, error / average);
                    }
                    if (error > tolerance && bPassed)
                    {
                        std::cerr << label << "が解析値の画素平均と一致しません pixel=(" << x << ','
                                  << y << ") channel=" << channel << " measured="
                                  << measured[channel] << " expected=" << average
                                  << " tolerance=" << tolerance << '\n';
                        bPassed = false;
                    }
                }
                if (bAllZero)
                {
                    ++outZeroPixels;
                    if (measured[0] != 0.0f || measured[1] != 0.0f || measured[2] != 0.0f)
                    {
                        std::cerr << label << ": 照明外の画素が0ではありません pixel=(" << x
                                  << ',' << y << ")\n";
                        bPassed = false;
                    }
                }
            }
        }
        std::cout << label << "_max_relative_error=" << maxRelativeError
                  << " zero_pixels=" << outZeroPixels << '\n';
        return bPassed;
    }

    CameraProxy MakeCamera(const Vec3& position, const Vec3& forward)
    {
        CameraProxy camera;
        camera.CameraId = 1u;
        camera.PositionX = static_cast<float>(position.X);
        camera.PositionY = static_cast<float>(position.Y);
        camera.PositionZ = static_cast<float>(position.Z);
        const Vec3 normalizedForward = Normalize(forward);
        camera.ForwardX = static_cast<float>(normalizedForward.X);
        camera.ForwardY = static_cast<float>(normalizedForward.Y);
        camera.ForwardZ = static_cast<float>(normalizedForward.Z);
        camera.Viewport.Width = static_cast<float>(Width);
        camera.Viewport.Height = static_cast<float>(Height);
        camera.AspectRatio = 1.0f;
        camera.PreExposure = 1.0f;
        return camera;
    }

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

    double MeanOverPixels(const VariableArray<float>& pixels, uint32_t channel,
                          const VariableArray<uint32_t>& indices)
    {
        double sum = 0.0;
        for (uint32_t index : indices)
        {
            sum += pixels[index * 4u + channel];
        }
        return indices.empty() ? 0.0 : sum / static_cast<double>(indices.size());
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
        TextureResources& textures = resources.Textures();

        // 白炉の材質。R1と同じmetallic 3段×roughness 5段を8bit textureで与える。
        const uint8_t metallicBytes[3] = {0u, 128u, 255u};
        const uint8_t roughnessBytes[5] = {13u, 64u, 128u, 191u, 255u};
        TextureHandle metallicHandles[3];
        TextureHandle roughnessHandles[5];
        VariableArray<TexturePtr> ownedTextures;
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            TexturePtr texture = CreateGrayTexture(device, metallicBytes[index], "Metallic");
            ownedTextures.push_back(texture);
            metallicHandles[index] = textures.RegisterExternalTexture(texture, "Metallic");
        }
        for (uint32_t index = 0u; index < 5u; ++index)
        {
            TexturePtr texture = CreateGrayTexture(device, roughnessBytes[index], "Roughness");
            ownedTextures.push_back(texture);
            roughnessHandles[index] = textures.RegisterExternalTexture(texture, "Roughness");
        }
        // 粗さ0（GGXの分母がfloatで0になりやすい鏡面）と、太陽の光沢反射に使う粗さ25/255。
        TexturePtr zeroRoughnessTexture = CreateGrayTexture(device, 0u, "ZeroRoughness");
        TexturePtr glossyRoughnessTexture = CreateGrayTexture(device, 25u, "GlossyRoughness");
        // 接空間法線(0.906, 0.004, 0.176)。+Tへ約80度傾き、視線の反対側の画素では視線が裏になる。
        const uint8_t steepNormalPixel[4] = {243u, 128u, 150u, 255u};
        TexturePtr steepNormalTexture = CreateTexture(device, 1u, 1u, Format::R8G8B8A8_UNORM, 4u,
                                                      steepNormalPixel, "SteepNormal");
        ownedTextures.push_back(zeroRoughnessTexture);
        ownedTextures.push_back(glossyRoughnessTexture);
        ownedTextures.push_back(steepNormalTexture);
        const TextureHandle zeroRoughnessHandle =
            textures.RegisterExternalTexture(zeroRoughnessTexture, "ZeroRoughness");
        const TextureHandle glossyRoughnessHandle =
            textures.RegisterExternalTexture(glossyRoughnessTexture, "GlossyRoughness");
        const TextureHandle steepNormalHandle =
            textures.RegisterExternalTexture(steepNormalTexture, "SteepNormal");

        VariableArray<Mesh3DVertex> sphereVertices;
        VariableArray<uint32_t> sphereIndices;
        ProceduralMeshGenerator::GenerateUVSphere(1.0f, 96u, 48u, sphereVertices, sphereIndices);
        TestGeometry sphere;
        TestGeometry facingPlane;
        TestGeometry floorPlane;
        TestGeometry areaLight;
        TestGeometry pointBlocker;
        TestGeometry areaBlocker;
        TestGeometry farLight;
        const Vec3 facingCorners[4] = {{-3.0, 3.0, 0.0}, {3.0, 3.0, 0.0},
                                       {3.0, -3.0, 0.0}, {-3.0, -3.0, 0.0}};
        const Vec3 floorCorners[4] = {{-40.0, 0.0, 40.0}, {40.0, 0.0, 40.0},
                                      {40.0, 0.0, -40.0}, {-40.0, 0.0, -40.0}};
        // 視野の外（z=-0.6での視野半幅は約0.81）に置き、受光面（z=0）へ向ける両面発光の四角形。
        const Vec3 lightCorners[4] = {{1.0, -0.5, -0.6}, {2.0, -0.5, -0.6},
                                      {2.0, 0.5, -0.6}, {1.0, 0.5, -0.6}};
        // 10m先の点光源（z=-10）の5mm手前、カメラの後ろにある小さな遮蔽板。
        const Vec3 pointBlockerCorners[4] = {{-0.1, 0.1, -9.995}, {0.1, 0.1, -9.995},
                                             {0.1, -0.1, -9.995}, {-0.1, -0.1, -9.995}};
        // 5m先（z=-5、カメラの後ろ）の面光源と、その0.5mm受光側にある面光源より広い遮蔽板。
        // 終端を距離の割合や固定幅で短くする影レイでは見逃す近さにする。
        const Vec3 farLightCorners[4] = {{-2.0, -2.0, -5.0}, {2.0, -2.0, -5.0},
                                         {2.0, 2.0, -5.0}, {-2.0, 2.0, -5.0}};
        const Vec3 areaBlockerCorners[4] = {{-3.0, -3.0, -4.9995}, {3.0, -3.0, -4.9995},
                                            {3.0, 3.0, -4.9995}, {-3.0, 3.0, -4.9995}};
        if (!BuildGeometry(device, sphereVertices, sphereIndices, sphere) ||
            !BuildQuad(device, facingCorners, {0.0, 0.0, -1.0}, facingPlane) ||
            !BuildQuad(device, floorCorners, {0.0, 1.0, 0.0}, floorPlane) ||
            !BuildQuad(device, lightCorners, {0.0, 0.0, 1.0}, areaLight) ||
            !BuildQuad(device, pointBlockerCorners, {0.0, 0.0, 1.0}, pointBlocker) ||
            !BuildQuad(device, areaBlockerCorners, {0.0, 0.0, 1.0}, areaBlocker) ||
            !BuildQuad(device, farLightCorners, {0.0, 0.0, 1.0}, farLight))
        {
            std::cerr << "検証用の形状を作成できませんでした\n";
            return 1;
        }

        const float white[3] = {1.0f, 1.0f, 1.0f};
        FramePacket furnacePacket;
        furnacePacket.RayTracingScene.Instances.push_back(MakeInstance(sphere, 0u, white));
        const float planeColor[3] = {0.5f, 0.7f, 0.9f};
        FramePacket planePacket;
        planePacket.RayTracingScene.Instances.push_back(MakeInstance(facingPlane, 0u, planeColor));
        const float floorColor[3] = {0.6f, 0.6f, 0.6f};
        FramePacket floorPacket;
        floorPacket.RayTracingScene.Instances.push_back(MakeInstance(floorPlane, 0u, floorColor));
        FramePacket areaPacket;
        areaPacket.RayTracingScene.Instances.push_back(MakeInstance(facingPlane, 0u, planeColor));
        // 発光面のアルベドを0にし、受光面との相互反射を除いて直接照明の解析値と比べられるようにする。
        const float black[3] = {0.0f, 0.0f, 0.0f};
        areaPacket.RayTracingScene.Instances.push_back(MakeInstance(areaLight, 1u, black));
        constexpr float AreaLightNits = 5.0f;
        {
            RayTracingHitMaterialSnapshot& lightMaterial =
                areaPacket.RayTracingScene.Instances[1].Material;
            lightMaterial.EmissiveColor[0] = 1.0f;
            lightMaterial.EmissiveColor[1] = 1.0f;
            lightMaterial.EmissiveColor[2] = 1.0f;
            lightMaterial.EmissiveLuminanceNits = AreaLightNits;
        }
        FramePacket pointBlockedPacket;
        pointBlockedPacket.RayTracingScene.Instances.push_back(
            MakeInstance(facingPlane, 0u, planeColor));
        pointBlockedPacket.RayTracingScene.Instances.push_back(MakeInstance(pointBlocker, 1u, black));
        RayTracingSceneInstanceSnapshot farLightInstance = MakeInstance(farLight, 1u, black);
        farLightInstance.Material.EmissiveColor[0] = 1.0f;
        farLightInstance.Material.EmissiveColor[1] = 1.0f;
        farLightInstance.Material.EmissiveColor[2] = 1.0f;
        farLightInstance.Material.EmissiveLuminanceNits = AreaLightNits;
        FramePacket farLitPacket;
        farLitPacket.RayTracingScene.Instances.push_back(MakeInstance(facingPlane, 0u, planeColor));
        farLitPacket.RayTracingScene.Instances.push_back(farLightInstance);
        FramePacket areaBlockedPacket;
        areaBlockedPacket.RayTracingScene.Instances.push_back(
            MakeInstance(facingPlane, 0u, planeColor));
        areaBlockedPacket.RayTracingScene.Instances.push_back(farLightInstance);
        areaBlockedPacket.RayTracingScene.Instances.push_back(MakeInstance(areaBlocker, 2u, black));
        if (!BuildTopLevel(device, furnacePacket) || !BuildTopLevel(device, planePacket) ||
            !BuildTopLevel(device, floorPacket) || !BuildTopLevel(device, areaPacket) ||
            !BuildTopLevel(device, pointBlockedPacket) || !BuildTopLevel(device, farLitPacket) ||
            !BuildTopLevel(device, areaBlockedPacket))
        {
            std::cerr << "検証用のTLASを作成できませんでした\n";
            return 1;
        }

        CameraProxy camera = MakeCamera({0.0, 0.0, -2.5}, {0.0, 0.0, 1.0});
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = Width;
        context.RenderHeight = Height;
        context.ScreenWidth = Width;
        context.ScreenHeight = Height;
        context.MainCamera = &camera;
        context.Resources.Textures = &textures;
        const auto useScene = [&context](FramePacket& packet)
        {
            context.SnapshotScene = &packet.Scene;
            context.SnapshotLightProxies = &packet.Scene.LightProxies;
            context.SnapshotRayTracingScene = &packet.RayTracingScene;
        };
        useScene(furnacePacket);
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
        FrameRunner runner;
        runner.Device = device;
        runner.Graph = &graph;
        runner.Pass = &pass;
        runner.Context = &context;
        bool bPassed = true;
        VariableArray<float> pixels;

        // 1. 白炉。一様環境1の中のアルベド1の球は、全方向で反射率1になり放射輝度1を返す。
        //    R1と同じく、球半径の0.75倍の円の平均相対誤差1%以内、固定9点の最大相対誤差3%以内。
        // 粗い金属は多重散乱の補償1/Ess（roughness 1で約3倍）が試料の分散を広げる。R1の平均相対誤差は
        // 画素ごとの絶対誤差の平均で雑音を含むため、閾値は変えずに収束に足る試料数を行ごとに与える。
        constexpr uint32_t FurnaceSamples = 1024u;
        constexpr uint32_t RoughMetalFurnaceSamples = 16384u;
        const double sphereScreenRadius =
            std::tan(std::asin(1.0 / 2.5)) / std::tan(Pi / 6.0) * (Width / 2.0);
        const double maskRadius = 0.75 * sphereScreenRadius;
        const uint32_t probes[9][2] = {{32u, 32u}, {20u, 32u}, {44u, 32u}, {32u, 20u},
                                       {32u, 44u}, {24u, 24u}, {40u, 24u}, {24u, 40u},
                                       {40u, 40u}};
        const auto checkFurnace = [&](const char* label, uint32_t row)
        {
            double meanSum = 0.0;
            double signedSum = 0.0;
            size_t meanCount = 0u;
            double maximumRelative = 0.0;
            for (uint32_t y = 0u; y < Height; ++y)
            {
                for (uint32_t x = 0u; x < Width; ++x)
                {
                    const double dx = x + 0.5 - Width / 2.0;
                    const double dy = y + 0.5 - Height / 2.0;
                    if (std::sqrt(dx * dx + dy * dy) > maskRadius)
                    {
                        continue;
                    }
                    const float* measured = PixelAt(pixels, x, y);
                    for (uint32_t channel = 0u; channel < 3u; ++channel)
                    {
                        meanSum += std::abs(measured[channel] - 1.0);
                        signedSum += measured[channel] - 1.0;
                        ++meanCount;
                    }
                }
            }
            for (const auto& probe : probes)
            {
                const float* measured = PixelAt(pixels, probe[0], probe[1]);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    maximumRelative = std::max(maximumRelative,
                                               std::abs(measured[channel] - 1.0));
                }
            }
            const float* corner = PixelAt(pixels, 0u, 0u);
            const double meanRelative = meanSum / static_cast<double>(meanCount);
            std::cout << "white_furnace " << label << " row=" << row
                      << " mean_mask_rel=" << meanRelative
                      << " mean_mask_signed=" << signedSum / static_cast<double>(meanCount)
                      << " fixed9_max_rel=" << maximumRelative
                      << " mask_pixels=" << meanCount / 3u << " background=" << corner[0] << '\n';
            if (meanRelative > 0.01 || maximumRelative > 0.03 ||
                std::abs(corner[0] - 1.0f) > 1.0e-5f)
            {
                std::cerr << "白炉の相対誤差が閾値を超えました " << label << " row=" << row << '\n';
                bPassed = false;
            }
        };
        pass.SetEnvironment(MakeUniformEnvironment(1.0f));
        RayTracingHitMaterialSnapshot& sphereMaterial =
            furnacePacket.RayTracingScene.Instances[0].Material;
        for (uint32_t row = 0u; row < 15u; ++row)
        {
            sphereMaterial.MetallicTexture = metallicHandles[row % 3u];
            sphereMaterial.RoughnessTexture = roughnessHandles[row / 3u];
            const bool bRoughMetal = row / 3u >= 3u && row % 3u != 0u;
            if (!runner.Accumulate(bRoughMetal ? RoughMetalFurnaceSamples : FurnaceSamples, pixels,
                                   "white_furnace"))
            {
                return 1;
            }
            char label[64] = {};
            std::snprintf(label, sizeof(label), "roughness=%u/255 metallic=%u/255",
                          static_cast<unsigned>(roughnessBytes[row / 3u]),
                          static_cast<unsigned>(metallicBytes[row % 3u]));
            checkFurnace(label, row);
        }
        // 1b. 粗さ0（葉の粗さはLUTの標本域の下端、alphaは1e-4）でも有限でエネルギーを保つ。
        for (uint32_t metallicIndex = 0u; metallicIndex < 3u; metallicIndex += 2u)
        {
            sphereMaterial.MetallicTexture = metallicHandles[metallicIndex];
            sphereMaterial.RoughnessTexture = zeroRoughnessHandle;
            if (!runner.Accumulate(FurnaceSamples, pixels, "white_furnace_zero_roughness"))
            {
                return 1;
            }
            char label[64] = {};
            std::snprintf(label, sizeof(label), "roughness=0/255 metallic=%u/255",
                          static_cast<unsigned>(metallicBytes[metallicIndex]));
            checkFurnace(label, 15u + metallicIndex / 2u);
        }

        // 2. 正距円筒の環境texture。一定値1なら一様環境と同じ白炉の値になる。
        VariableArray<float> equirectPixels(64u * 32u * 4u, 1.0f);
        TexturePtr constantEnvironment = CreateTexture(
            device, 64u, 32u, Format::R32G32B32A32_FLOAT, 16u, equirectPixels.data(),
            "PathTracingLightingTest.ConstantEnvironment");
        // 上半球（方向のy>0、v<0.5）だけが1の環境。向きを取り違えると床が黒くなる。
        for (uint32_t y = 0u; y < 32u; ++y)
        {
            for (uint32_t x = 0u; x < 64u; ++x)
            {
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    equirectPixels[(y * 64u + x) * 4u + channel] = y < 16u ? 1.0f : 0.0f;
                }
            }
        }
        TexturePtr skyOnlyEnvironment = CreateTexture(
            device, 64u, 32u, Format::R32G32B32A32_FLOAT, 16u, equirectPixels.data(),
            "PathTracingLightingTest.UpperHemisphereEnvironment");
        if (!constantEnvironment || !skyOnlyEnvironment)
        {
            std::cerr << "環境textureを作成できませんでした\n";
            return 1;
        }
        PathTracingEnvironment equirect;
        equirect.Mode = PathTracingEnvironmentMode::Equirect;
        equirect.EquirectTexture = constantEnvironment;
        equirect.Intensity = 1.0f;
        pass.SetEnvironment(equirect);
        sphereMaterial.MetallicTexture = metallicHandles[1];
        sphereMaterial.RoughnessTexture = roughnessHandles[2];
        if (!runner.Accumulate(FurnaceSamples, pixels, "equirect_furnace"))
        {
            return 1;
        }
        checkFurnace("equirect_constant", 7u);
        sphereMaterial.MetallicTexture = TextureHandle();
        sphereMaterial.RoughnessTexture = TextureHandle();

        equirect.EquirectTexture = skyOnlyEnvironment;
        pass.SetEnvironment(equirect);
        pass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
        camera = MakeCamera({0.0, 2.0, -2.0}, {0.0, -1.0, 1.0});
        useScene(floorPacket);
        if (!runner.Accumulate(64u, pixels, "equirect_orientation"))
        {
            return 1;
        }
        {
            VariableArray<uint32_t> all;
            for (uint32_t index = 0u; index < Width * Height; ++index)
            {
                all.push_back(index);
            }
            const double mean = MeanOverPixels(pixels, 0u, all);
            std::cout << "equirect_upper_hemisphere_floor=" << mean << " expected=0.6\n";
            if (std::abs(mean - 0.6) > 0.006)
            {
                std::cerr << "正距円筒環境の上下の向きがラスタの規約と一致しません\n";
                bPassed = false;
            }
        }

        // 3. 点・spot・方向光（純Lambert）。環境光と他の面がないので、累積値は面上の解析輝度の画素平均になる。
        constexpr uint32_t PunctualSamples = 1024u;
        pass.SetEnvironment(PathTracingEnvironment{});
        camera = MakeCamera({0.0, 0.0, -2.0}, {0.0, 0.0, 1.0});
        useScene(planePacket);
        ScreenMapping planeMapping;
        planeMapping.Build(camera, device.get());
        const PlaneScene facing{{0.0, 0.0, 0.0}, {0.0, 0.0, -1.0}};
        // 既定の法線texture（128,128,255）は接空間法線(128/255*2-1, 同, 1)で、ラスタと同じくわずかに傾く。
        // 四角形のUVはT=+X、B=-Yなので、シェーディング法線はnormalize(t, -t, -1)。
        const double flatTilt = 128.0 / 255.0 * 2.0 - 1.0;
        const Vec3 facingShadingNormal = Normalize({flatTilt, -flatTilt, -1.0});
        PunctualLightCase pointLight;
        pointLight.Type = LightType::Point;
        pointLight.Position = {0.4, 0.3, -1.0};
        pointLight.Color[1] = 0.8f;
        pointLight.Color[2] = 0.6f;
        pointLight.Intensity = 5.0f;
        pointLight.Range = 2.5f;
        PunctualLightCase spotLight;
        spotLight.Type = LightType::Spot;
        spotLight.Position = {0.0, 0.0, -1.5};
        spotLight.Direction = {0.0, 0.0, 1.0};
        spotLight.Color[0] = 0.7f;
        spotLight.Intensity = 8.0f;
        spotLight.Range = 100.0f;
        spotLight.InnerCosine = static_cast<float>(std::cos(10.0 * Pi / 180.0));
        spotLight.OuterCosine = static_cast<float>(std::cos(20.0 * Pi / 180.0));
        PunctualLightCase directionalLight;
        directionalLight.Type = LightType::Directional;
        directionalLight.Direction = {0.3, -0.2, 1.0};
        directionalLight.Color[2] = 0.5f;
        directionalLight.Intensity = 2.0f;
        const PunctualLightCase* lambertCases[3] = {&pointLight, &spotLight, &directionalLight};
        const char* lambertLabels[3] = {"lambert_point", "lambert_spot", "lambert_directional"};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            const PunctualLightCase& light = *lambertCases[index];
            planePacket.Scene.LightProxies.clear();
            planePacket.Scene.LightProxies.push_back(MakeLightProxy(light));
            if (!runner.Accumulate(PunctualSamples, pixels, lambertLabels[index]) ||
                pass.GetPunctualLightCount() != 1u)
            {
                std::cerr << lambertLabels[index] << ": 光源表へ載りませんでした\n";
                return 1;
            }
            double chroma[3] = {};
            Chromaticity(light.Color, chroma);
            const auto expected = [&](const Vec3& point, const Vec3&, double (&outValue)[3])
            {
                Vec3 direction;
                double irradiance = 0.0;
                EvaluatePunctualIncidence(light, point, direction, irradiance);
                const double cosine = std::max(Dot(facingShadingNormal, direction), 0.0);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    outValue[channel] = planeColor[channel] / Pi * chroma[channel] *
                                        irradiance * cosine * camera.PreExposure;
                }
            };
            uint32_t zeroPixels = 0u;
            bPassed = CheckPixelAverages(planeMapping, facing, pixels, expected, PunctualSamples,
                                         lambertLabels[index], zeroPixels) && bPassed;
            if (&light == &spotLight && zeroPixels < 1000u)
            {
                std::cerr << "spot円錐外の画素が足りません\n";
                bPassed = false;
            }
        }

        // 4. 本番BSDFの点光源。NEEの評価がDFG補償付きの共通式と一致する。
        pass.SetBsdfMode(PathTracingBsdfMode::Production);
        planePacket.Scene.LightProxies.clear();
        planePacket.Scene.LightProxies.push_back(MakeLightProxy(pointLight));
        if (!runner.Accumulate(PunctualSamples, pixels, "production_point"))
        {
            return 1;
        }
        {
            double chroma[3] = {};
            Chromaticity(pointLight.Color, chroma);
            const double albedo[3] = {planeColor[0], planeColor[1], planeColor[2]};
            // 既定textureはmetallic 0・roughness 128/255。
            const double roughness = 128.0 / 255.0;
            const auto expected = [&](const Vec3& point, const Vec3& view, double (&outValue)[3])
            {
                Vec3 direction;
                double irradiance = 0.0;
                EvaluatePunctualIncidence(pointLight, point, direction, irradiance);
                double bsdf[3] = {};
                EvaluateProductionBsdf(facingShadingNormal, view, direction, albedo, 0.0, roughness,
                                       bsdf);
                const double cosine = std::max(Dot(facingShadingNormal, direction), 0.0);
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    outValue[channel] = bsdf[channel] * chroma[channel] * irradiance * cosine *
                                        camera.PreExposure;
                }
            };
            uint32_t zeroPixels = 0u;
            bPassed = CheckPixelAverages(planeMapping, facing, pixels, expected, PunctualSamples,
                                         "production_point", zeroPixels) && bPassed;
        }
        planePacket.Scene.LightProxies.clear();

        // 5. 面光源。純Lambertでは多角形光源の解析照度に、光源標本のみ・BSDF標本のみ・MISが揃って収束する。
        useScene(areaPacket);
        VariableArray<uint32_t> planePixels;
        VariableArray<double> analytic;
        for (uint32_t y = 0u; y < Height; ++y)
        {
            for (uint32_t x = 0u; x < Width; ++x)
            {
                const Vec3 direction = planeMapping.Direction((x + 0.5) / Width, (y + 0.5) / Height);
                Vec3 point;
                if (!IntersectPlane(planeMapping.Origin, direction, facing.Point, facing.Normal,
                                    point))
                {
                    continue;
                }
                planePixels.push_back(y * Width + x);
                analytic.push_back(AreaLightNits *
                                   PolygonIrradianceFactor(point, facing.Normal, lightCorners) / Pi);
            }
        }
        const struct
        {
            PathTracingLightSampling Sampling;
            const char* Label;
            uint32_t Samples;
            double Tolerance;
        } samplingCases[3] = {
            {PathTracingLightSampling::MultipleImportance, "mis", 256u, 0.02},
            {PathTracingLightSampling::LightOnly, "light_only", 256u, 0.02},
            {PathTracingLightSampling::BsdfOnly, "bsdf_only", 1024u, 0.04}};
        pass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
        for (const auto& samplingCase : samplingCases)
        {
            pass.SetLightSampling(samplingCase.Sampling);
            if (!runner.Accumulate(samplingCase.Samples, pixels, samplingCase.Label) ||
                pass.GetEmissiveTriangleCount() != 2u)
            {
                std::cerr << "面光源の発光三角形を光源表へ載せられませんでした\n";
                return 1;
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                double measuredSum = 0.0;
                double expectedSum = 0.0;
                for (size_t index = 0u; index < planePixels.size(); ++index)
                {
                    measuredSum += pixels[planePixels[index] * 4u + channel];
                    expectedSum += analytic[index] * planeColor[channel];
                }
                const double relative = std::abs(measuredSum - expectedSum) / expectedSum;
                std::cout << "lambert_area_" << samplingCase.Label << " channel=" << channel
                          << " measured_mean=" << measuredSum / planePixels.size()
                          << " analytic_mean=" << expectedSum / planePixels.size()
                          << " relative=" << relative << '\n';
                if (relative > samplingCase.Tolerance)
                {
                    std::cerr << "面光源の" << samplingCase.Label
                              << "が多角形光源の解析照度と一致しません\n";
                    bPassed = false;
                }
            }
        }

        // 光沢金属でも3つの戦略の平均が一致する（解析値なし、MISを基準にする）。
        pass.SetBsdfMode(PathTracingBsdfMode::Production);
        RayTracingHitMaterialSnapshot& glossyMaterial = areaPacket.RayTracingScene.Instances[0].Material;
        glossyMaterial.MetallicTexture = metallicHandles[2];
        glossyMaterial.RoughnessTexture = roughnessHandles[1];
        double glossyMean[3][3] = {};
        for (uint32_t caseIndex = 0u; caseIndex < 3u; ++caseIndex)
        {
            pass.SetLightSampling(samplingCases[caseIndex].Sampling);
            if (!runner.Accumulate(1024u, pixels, samplingCases[caseIndex].Label))
            {
                return 1;
            }
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                glossyMean[caseIndex][channel] = MeanOverPixels(pixels, channel, planePixels);
            }
        }
        for (uint32_t caseIndex = 1u; caseIndex < 3u; ++caseIndex)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                const double relative = std::abs(glossyMean[caseIndex][channel] - glossyMean[0][channel]) /
                                        glossyMean[0][channel];
                std::cout << "glossy_area_" << samplingCases[caseIndex].Label << " channel=" << channel
                          << " mean=" << glossyMean[caseIndex][channel]
                          << " mis_mean=" << glossyMean[0][channel] << " relative=" << relative << '\n';
                if (relative > samplingCases[caseIndex].Tolerance + 0.01)
                {
                    std::cerr << "光沢面で標本化戦略の期待値が一致しません\n";
                    bPassed = false;
                }
            }
        }
        pass.SetLightSampling(PathTracingLightSampling::MultipleImportance);

        const auto meanOfImage = [&](uint32_t channel)
        {
            double sum = 0.0;
            for (uint32_t index = 0u; index < Width * Height; ++index)
            {
                sum += pixels[index * 4u + channel];
            }
            return sum / (Width * Height);
        };
        const auto compareStrategies = [&](const char* label, uint32_t samples, double tolerance)
        {
            double means[3][3] = {};
            for (uint32_t caseIndex = 0u; caseIndex < 3u; ++caseIndex)
            {
                pass.SetLightSampling(samplingCases[caseIndex].Sampling);
                if (!runner.Accumulate(samples, pixels, label))
                {
                    return false;
                }
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    means[caseIndex][channel] = meanOfImage(channel);
                }
            }
            pass.SetLightSampling(PathTracingLightSampling::MultipleImportance);
            for (uint32_t caseIndex = 1u; caseIndex < 3u; ++caseIndex)
            {
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    const double relative = std::abs(means[caseIndex][channel] - means[0][channel]) /
                                            means[0][channel];
                    std::cout << label << '_' << samplingCases[caseIndex].Label
                              << " channel=" << channel << " mean=" << means[caseIndex][channel]
                              << " mis_mean=" << means[0][channel] << " relative=" << relative
                              << '\n';
                    if (!(means[0][channel] > 0.0) || relative > tolerance)
                    {
                        std::cerr << label << "で標本化戦略の期待値が一致しません\n";
                        bPassed = false;
                    }
                }
            }
            return true;
        };

        // 6. 視線がシェーディング法線の裏になる画素（強く傾いた法線マップの右側）でも、
        //    評価・pdf・標本化が同じ法線を使い、3戦略が同じ期待値へ収束する。
        // 金属（metallic 1、roughness 0.5）にしてBSDF標本を全て可視法線分布から引く。
        glossyMaterial.MetallicTexture = metallicHandles[2];
        glossyMaterial.RoughnessTexture = roughnessHandles[2];
        glossyMaterial.NormalTexture = steepNormalHandle;
        // 許容差1%: 修正後の実測は0.14%、裏側の視線で標本分布とpdfがずれる版は2.25%。
        if (!compareStrategies("steep_normal_area", 1024u, 0.01))
        {
            return 1;
        }
        glossyMaterial.MetallicTexture = TextureHandle();
        glossyMaterial.RoughnessTexture = TextureHandle();
        glossyMaterial.NormalTexture = TextureHandle();

        // 7. 太陽円盤の3戦略。光沢金属の床に太陽を正反射させ、飽和しない太陽放射輝度
        //    （事前露出後で約1.6e5）をBSDF標本側でも切らずに累積する。
        SkyAtmospherePass skyPass;
        // 直前のフレームのcommand listは解放済みなので、初期化用を渡す。
        context.CommandList = initializationCommand.get();
        if (!skyPass.Initialize(context))
        {
            std::cerr << "空パスを初期化できませんでした\n";
            return 1;
        }
        SkyAtmosphereParameters sky = MakeDefaultSkyAtmosphereParameters();
        sky.bEnabled = true;
        sky.SunAltitudeDegrees = 45.0f;
        sky.SunAzimuthDegrees = 90.0f;
        floorPacket.Scene.SkyAtmosphere = sky;
        RayTracingHitMaterialSnapshot& floorMaterial = floorPacket.RayTracingScene.Instances[0].Material;
        floorMaterial.MetallicTexture = metallicHandles[2];
        floorMaterial.RoughnessTexture = glossyRoughnessHandle;
        camera = MakeCamera({0.0, 2.0, -2.0}, {0.0, -1.0, 1.0});
        camera.PreExposure = 1.0f / 10000.0f;
        // 視野を8度に絞り、太陽の光沢反射が多くの画素にまたがるようにしてBSDF標本側の分散を抑える。
        camera.FieldOfView = 8.0f;
        useScene(floorPacket);
        pass.SetBsdfMode(PathTracingBsdfMode::Production);
        runner.Sky = &skyPass;
        // 許容差5%: BSDF標本だけの太陽は分布の裾が重く、修正後の実測は0.52%（4096試料・60度視野では1.5%）。
        //            65504で切り詰める版は32%減る。
        if (!compareStrategies("sun_glossy", 1024u, 0.05))
        {
            return 1;
        }
        runner.Sky = nullptr;
        floorPacket.Scene.SkyAtmosphere.bEnabled = false;
        camera = MakeCamera({0.0, 0.0, -2.0}, {0.0, 0.0, 1.0});

        // 8. 光源の直前にある遮蔽物。点光源は10m先の5mm手前、面光源は2.5mm手前の板で全て遮る。
        pass.SetEnvironment(PathTracingEnvironment{});
        pass.SetBsdfMode(PathTracingBsdfMode::ValidationLambert);
        PunctualLightCase distantLight;
        distantLight.Type = LightType::Point;
        distantLight.Position = {0.0, 0.0, -10.0};
        distantLight.Intensity = 100.0f;
        distantLight.Range = 1000.0f;
        const auto imageMax = [&]()
        {
            float maximum = 0.0f;
            for (uint32_t index = 0u; index < Width * Height; ++index)
            {
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    maximum = std::max(maximum, pixels[index * 4u + channel]);
                }
            }
            return maximum;
        };
        planePacket.Scene.LightProxies.clear();
        planePacket.Scene.LightProxies.push_back(MakeLightProxy(distantLight));
        useScene(planePacket);
        if (!runner.Accumulate(1u, pixels, "distant_point_unblocked"))
        {
            return 1;
        }
        const float unblockedMax = imageMax();
        pointBlockedPacket.Scene.LightProxies.push_back(MakeLightProxy(distantLight));
        useScene(pointBlockedPacket);
        if (!runner.Accumulate(1u, pixels, "distant_point_blocked"))
        {
            return 1;
        }
        const float pointBlockedMax = imageMax();
        useScene(farLitPacket);
        if (!runner.Accumulate(64u, pixels, "far_area_unblocked"))
        {
            return 1;
        }
        const float areaUnblockedMax = imageMax();
        // 光源標本・BSDF標本・MISのどれでも遮蔽板の背後は0（戦略間で可視性が一致する）。
        useScene(areaBlockedPacket);
        float areaBlockedMax = 0.0f;
        for (const auto& samplingCase : samplingCases)
        {
            pass.SetLightSampling(samplingCase.Sampling);
            if (!runner.Accumulate(256u, pixels, "far_area_blocked"))
            {
                return 1;
            }
            std::cout << "far_area_blocked_" << samplingCase.Label << "_max=" << imageMax() << '\n';
            areaBlockedMax = std::max(areaBlockedMax, imageMax());
        }
        pass.SetLightSampling(PathTracingLightSampling::MultipleImportance);
        std::cout << "distant_point_unblocked_max=" << unblockedMax
                  << " distant_point_blocked_max=" << pointBlockedMax
                  << " far_area_unblocked_max=" << areaUnblockedMax
                  << " far_area_blocked_max=" << areaBlockedMax << '\n';
        if (!(unblockedMax > 0.0f) || pointBlockedMax != 0.0f || !(areaUnblockedMax > 0.0f) ||
            areaBlockedMax != 0.0f)
        {
            std::cerr << "光源の直前の遮蔽物を影レイが見逃しました\n";
            bPassed = false;
        }
        planePacket.Scene.LightProxies.clear();

        skyPass.Shutdown();
        pass.Shutdown();
        graph.Shutdown();
        device->WaitIdle();
        ownedTextures.clear();
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
            std::cout << "pt_white_furnace_rows=15 zero_roughness=true equirect_environment=true "
                         "lambert_punctual_analytic=true spot_outside_zero=true "
                         "production_bsdf_point=true area_light_mis_consistent=true "
                         "steep_normal_consistent=true sun_mis_consistent=true "
                         "near_light_occluders=true\n";
        }
        return bPassed ? 0 : 1;
    }
}

int main()
{
    return RunTest();
}
