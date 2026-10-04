#include "Asset/AssetSystem.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderResources.h"
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include "Rendering/MegaGeometry/ProceduralMegaSphere.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Library/Core/Private/Resource/ModelAssetLoader.h"
#include "Library/Core/Private/Resource/ModelStaging.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <utility>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;
namespace ModelAssetLoader = NorvesLib::Core::Resource;
namespace ModelStaging = NorvesLib::Core::Resource::ModelStaging;

namespace
{
    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc &desc)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size))
        {
        }

        uint64_t GetSize() const override { return Desc.Size; }

        void *Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            const size_t byteOffset = static_cast<size_t>(offset);
            return byteOffset < Bytes.size() ? Bytes.data() + byteOffset : nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
        {
            LastUpdateSize = size;
            LastUpdateOffset = offset;

            const size_t byteSize = static_cast<size_t>(size);
            const size_t byteOffset = static_cast<size_t>(offset);
            if (data == nullptr || byteOffset + byteSize > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + byteOffset, data, byteSize);
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &desc) override
        {
            CreatedBufferDescs.push_back(desc);
            if (FailBufferCreateIndex == CreatedBufferDescs.size())
            {
                return {};
            }

            LastBuffer = MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(LastBuffer);
            return LastBuffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &) override { return {}; }
        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &) override { return {}; }
        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override
        {
            return {};
        }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override
        {
            return {};
        }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override
        {
            return {};
        }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::BufferDesc> CreatedBufferDescs;
        std::vector<NorvesLib::Core::Container::TSharedPtr<FakeBuffer>> CreatedBuffers;
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        size_t FailBufferCreateIndex = 0;
    };

    struct MeshFixture
    {
        float Vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t Indices[3] = {0, 1, 2};
        MegaGeometry::MegaMeshCreateInfo CreateInfo;

        explicit MeshFixture(const char *debugName)
        {
            MegaGeometry::MeshCluster cluster;
            cluster.IndexOffset = 0;
            cluster.IndexCount = 3;
            cluster.VertexOffset = 0;
            cluster.VertexCount = 3;
            cluster.Bounds.CenterX = 0.5f;
            cluster.Bounds.CenterY = 0.5f;
            cluster.Bounds.CenterZ = 0.0f;
            cluster.Bounds.Radius = 0.75f;
            cluster.ConeAxisZ = 1.0f;
            cluster.ConeCutoff = 0.25f;
            cluster.MaterialIndex = 4;
            cluster.LODLevel = 2;
            cluster.LODError = 0.125f;
            cluster.ParentStart = 7;
            cluster.ParentCount = 3;

            CreateInfo.VertexData = Vertices;
            CreateInfo.VertexDataSize = sizeof(Vertices);
            CreateInfo.VertexCount = 3;
            CreateInfo.VertexStride = 4 * sizeof(float);
            CreateInfo.IndexData = Indices;
            CreateInfo.IndexCount = 3;
            CreateInfo.Clusters.push_back(cluster);
            CreateInfo.TotalBounds.CenterX = 0.5f;
            CreateInfo.TotalBounds.CenterY = 0.5f;
            CreateInfo.TotalBounds.CenterZ = 0.0f;
            CreateInfo.TotalBounds.Radius = 1.25f;
            CreateInfo.bBuildLODHierarchy = false;
            CreateInfo.Material.BaseColor[0] = 0.2f;
            CreateInfo.Material.BaseColor[1] = 0.4f;
            CreateInfo.Material.BaseColor[2] = 0.6f;
            CreateInfo.Material.BaseColor[3] = 1.0f;
            CreateInfo.Material.HeightScale = 0.08f;
            CreateInfo.Material.bHasHeightMap = true;
            CreateInfo.DebugName = debugName;
        }
    };

    MegaGeometry::MegaMeshHandle MakeMegaMeshHandle(uint64_t id)
    {
        MegaGeometry::MegaMeshHandle handle;
        handle.Id = id;
        return handle;
    }

    bool HasUsage(NorvesLib::RHI::ResourceUsage value, NorvesLib::RHI::ResourceUsage flag)
    {
        return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
    }

    void AssertGPUClusterLayout()
    {
        assert(sizeof(MegaGeometry::GPUClusterData) == 96);
        assert(offsetof(MegaGeometry::GPUClusterData, BoundsCenterX) == 0);
        assert(offsetof(MegaGeometry::GPUClusterData, ConeAxisX) == 16);
        assert(offsetof(MegaGeometry::GPUClusterData, IndexOffset) == 32);
        assert(offsetof(MegaGeometry::GPUClusterData, LODLevel) == 48);
        assert(offsetof(MegaGeometry::GPUClusterData, LODError) == 52);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentStart) == 56);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentCount) == 60);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentCenterX) == 64);
        assert(offsetof(MegaGeometry::GPUClusterData, Flags) == 80);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentError) == 84);
        assert(offsetof(MegaGeometry::GPUClusterData, GroupId) == 88);
        assert(offsetof(MegaGeometry::GPUClusterData, PageId) == 92);
    }

    void AssertNoLodUploadBuffers(const FakeDevice &device)
    {
        AssertGPUClusterLayout();

        assert(device.CreatedBufferDescs.size() == 3);
        assert(device.CreatedBufferDescs[0].Size == sizeof(MeshFixture::Vertices));
        assert(HasUsage(device.CreatedBufferDescs[0].Usage, NorvesLib::RHI::ResourceUsage::VertexBuffer));
        assert(HasUsage(device.CreatedBufferDescs[0].Usage, NorvesLib::RHI::ResourceUsage::StorageBuffer));

        assert(device.CreatedBufferDescs[1].Size == 3 * sizeof(uint32_t));
        assert(HasUsage(device.CreatedBufferDescs[1].Usage, NorvesLib::RHI::ResourceUsage::IndexBuffer));
        assert(HasUsage(device.CreatedBufferDescs[1].Usage, NorvesLib::RHI::ResourceUsage::StorageBuffer));

        assert(device.CreatedBufferDescs[2].Size == sizeof(MegaGeometry::GPUClusterData));
        assert(device.CreatedBufferDescs[2].Usage == NorvesLib::RHI::ResourceUsage::StorageBuffer);

        assert(device.CreatedBuffers.size() == 3);
        assert(device.CreatedBuffers[0]->LastUpdateSize == sizeof(MeshFixture::Vertices));
        assert(device.CreatedBuffers[1]->LastUpdateSize == 3 * sizeof(uint32_t));
        assert(device.CreatedBuffers[2]->LastUpdateSize == sizeof(MegaGeometry::GPUClusterData));

        MegaGeometry::GPUClusterData uploadedCluster{};
        std::memcpy(&uploadedCluster,
                    device.CreatedBuffers[2]->Bytes.data(),
                    sizeof(MegaGeometry::GPUClusterData));
        assert(uploadedCluster.LODLevel == 2);
        assert(uploadedCluster.LODError == 0.125f);
        assert(uploadedCluster.ParentStart == 7);
        assert(uploadedCluster.ParentCount == 3);
        // 焼き込み済みの階層ではない（従来の段の選び方）
        assert(uploadedCluster.Flags == 0u);
    }

    BufferCreateInfo MakeCounterBufferInfo()
    {
        BufferCreateInfo createInfo;
        createInfo.Size = 16;
        createInfo.bHostVisible = true;
        createInfo.UsageType = BufferCreateInfo::Usage::Vertex;
        createInfo.DebugName = "CounterBuffer";
        return createInfo;
    }

    void TestCreateBeforeInitialize()
    {
        RenderResources manager;
        MeshFixture mesh("PreInitializeMega");

        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(!handle.IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(handle) == nullptr);
    }

    void TestInvalidCreateInfoCreatesNoBuffers()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        MeshFixture mesh("InvalidMega");
        mesh.CreateInfo.VertexDataSize = 0;

        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(!handle.IsValid());
        assert(device->CreatedBufferDescs.empty());
        assert(manager.GetResourceStats().BufferCount == 0);
    }

    struct InvalidMegaEmissiveRow
    {
        const char *Name;
        float Red;
        float Green;
        float Blue;
        float LuminanceNits;
    };

    const InvalidMegaEmissiveRow InvalidMegaEmissiveRows[] = {
        {"nan rgb", std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f},
        {"inf rgb", 0.0f, std::numeric_limits<float>::infinity(), 0.0f, 1.0f},
        {"nan nits", 0.0f, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()},
        {"inf nits", 0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()},
        {"negative rgb", -0.25f, 0.0f, 0.0f, 1.0f},
        {"negative nits", 0.25f, 0.25f, 0.25f, -1.0f},
        {"positive nits with tiny Y", 0.000001f, 0.0f, 0.0f, 1.0f},
        {"float product overflow", 1.0f, 0.0f, 0.0f, 1.0e38f},
    };

    void SetInvalidMegaEmissive(MegaGeometry::MegaMeshCreateInfo &createInfo,
                                const InvalidMegaEmissiveRow &row)
    {
        createInfo.Material.EmissiveColor[0] = row.Red;
        createInfo.Material.EmissiveColor[1] = row.Green;
        createInfo.Material.EmissiveColor[2] = row.Blue;
        createInfo.Material.EmissiveLuminanceNits = row.LuminanceNits;
    }

    void TestMegaEmissiveCanonicalContractRejectsInvalidWithoutSideEffects()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        MeshFixture mesh("MegaEmissiveBaseline");
        const auto zeroHandle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(zeroHandle.IsValid());
        const auto *zeroData = manager.MegaGeometry().GetMegaMeshGPUData(zeroHandle);
        assert(zeroData != nullptr);
        assert(zeroData->Material.EmissiveColor[0] == 0.0f);
        assert(zeroData->Material.EmissiveColor[1] == 0.0f);
        assert(zeroData->Material.EmissiveColor[2] == 0.0f);
        assert(zeroData->Material.EmissiveLuminanceNits == 0.0f);

        const ResourceStats statsBefore = manager.GetResourceStats();
        const size_t bufferDescCountBefore = device->CreatedBufferDescs.size();
        const size_t bufferCountBefore = device->CreatedBuffers.size();

        for (const InvalidMegaEmissiveRow &row : InvalidMegaEmissiveRows)
        {
            MegaGeometry::MegaMeshCreateInfo invalid = mesh.CreateInfo;
            SetInvalidMegaEmissive(invalid, row);

            const auto rejectedHandle = manager.MegaGeometry().CreateMegaMesh(invalid);
            assert(!rejectedHandle.IsValid());
            assert(manager.MegaGeometry().GetMegaMeshGPUData(zeroHandle) == zeroData);
            assert(device->CreatedBufferDescs.size() == bufferDescCountBefore);
            assert(device->CreatedBuffers.size() == bufferCountBefore);

            const ResourceStats statsAfter = manager.GetResourceStats();
            assert(statsAfter.BufferCount == statsBefore.BufferCount);
            assert(statsAfter.TextureCount == statsBefore.TextureCount);
            assert(statsAfter.ShaderCount == statsBefore.ShaderCount);
            assert(statsAfter.SamplerCount == statsBefore.SamplerCount);
            assert(statsAfter.TotalBufferMemory == statsBefore.TotalBufferMemory);
            assert(statsAfter.TotalTextureMemory == statsBefore.TotalTextureMemory);
        }

        MegaGeometry::MegaMeshCreateInfo positive = mesh.CreateInfo;
        positive.Material.EmissiveColor[0] = 1.0f;
        positive.Material.EmissiveColor[1] = 0.0f;
        positive.Material.EmissiveColor[2] = 0.0f;
        positive.Material.EmissiveLuminanceNits = 2.0f;
        const auto positiveHandle = manager.MegaGeometry().CreateMegaMesh(positive);
        assert(positiveHandle.IsValid());
        assert(positiveHandle.Id == zeroHandle.Id + 1);

        const auto *positiveData = manager.MegaGeometry().GetMegaMeshGPUData(positiveHandle);
        assert(positiveData != nullptr);
        const float expectedCanonicalRed = 1.0f / 0.2126f;
        assert(std::isfinite(positiveData->Material.EmissiveColor[0]));
        assert(std::abs(positiveData->Material.EmissiveColor[0] - expectedCanonicalRed) < 0.00001f);
        assert(positiveData->Material.EmissiveColor[1] == 0.0f);
        assert(positiveData->Material.EmissiveColor[2] == 0.0f);
        assert(positiveData->Material.EmissiveLuminanceNits == 2.0f);
    }

    void TestSuccessfulNoLodUpload()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        MeshFixture mesh("NoLodMega");
        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(handle.IsValid());
        AssertNoLodUploadBuffers(*device);

        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        assert(gpuData->VertexBuffer);
        assert(gpuData->IndexBuffer);
        assert(gpuData->ClusterBuffer);
        assert(gpuData->VertexCount == mesh.CreateInfo.VertexCount);
        assert(gpuData->IndexCount == mesh.CreateInfo.IndexCount);
        assert(gpuData->ClusterCount == mesh.CreateInfo.Clusters.size());
        assert(gpuData->TotalBounds.CenterX == mesh.CreateInfo.TotalBounds.CenterX);
        assert(gpuData->TotalBounds.CenterY == mesh.CreateInfo.TotalBounds.CenterY);
        assert(gpuData->TotalBounds.Radius == mesh.CreateInfo.TotalBounds.Radius);
        assert(gpuData->Material.BaseColor[0] == mesh.CreateInfo.Material.BaseColor[0]);
        assert(gpuData->Material.BaseColor[1] == mesh.CreateInfo.Material.BaseColor[1]);
        assert(gpuData->Material.BaseColor[2] == mesh.CreateInfo.Material.BaseColor[2]);
        assert(gpuData->Material.HeightScale == mesh.CreateInfo.Material.HeightScale);
        assert(gpuData->Material.bHasHeightMap == mesh.CreateInfo.Material.bHasHeightMap);
        assert(gpuData->DebugName == mesh.CreateInfo.DebugName);
        assert(manager.GetResourceStats().BufferCount == 0);
    }

    // 列優先（GLSL と同じ [列 * 4 + 行]）の行列とベクトルの積
    void MultiplyColumnMajor(const float *matrix, const double *vector, double *out)
    {
        for (int row = 0; row < 4; ++row)
        {
            out[row] = 0.0;
            for (int column = 0; column < 4; ++column)
            {
                out[row] += static_cast<double>(matrix[column * 4 + row]) * vector[column];
            }
        }
    }

    // ワールドの点を、シェーダーへ渡す view・projection でクリップ座標にし、画素へ直す。視錐台の外なら false
    bool ProjectToPixels(const float *view,
                         const float *projection,
                         const double *worldPosition,
                         double width,
                         double height,
                         double *outPixel)
    {
        const double position[4] = {worldPosition[0], worldPosition[1], worldPosition[2], 1.0};
        double viewPosition[4] = {};
        double clip[4] = {};
        MultiplyColumnMajor(view, position, viewPosition);
        MultiplyColumnMajor(projection, viewPosition, clip);
        if (!(clip[3] > 0.0) || std::abs(clip[0]) > clip[3] || std::abs(clip[1]) > clip[3])
        {
            return false;
        }
        outPixel[0] = (clip[0] / clip[3] * 0.5 + 0.5) * width;
        outPixel[1] = (clip[1] / clip[3] * 0.5 + 0.5) * height;
        return true;
    }

    // LOD球の誤差の上限が、変位の前後の点をクリップ座標から画素へ直した差を超えないことの契約
    // （角度の変化だけで見積もると、視線から外れた点での透視投影の伸びを見落として過小になる）
    void TestLODSphereErrorBoundsPerspectivePixelDisplacement()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;

        const double width = 1280.0;
        const double height = 720.0;
        CameraProxy camera;
        camera.PositionX = 1.0f;
        camera.PositionY = 2.0f;
        camera.PositionZ = 3.0f;
        const double forwardRaw[3] = {0.3, -0.2, -1.0};
        const double forwardLength =
            std::sqrt(forwardRaw[0] * forwardRaw[0] + forwardRaw[1] * forwardRaw[1] + forwardRaw[2] * forwardRaw[2]);
        const double forward[3] = {
            forwardRaw[0] / forwardLength, forwardRaw[1] / forwardLength, forwardRaw[2] / forwardLength};
        // right = forward × (0,1,0)、up = right × forward
        double right[3] = {-forward[2], 0.0, forward[0]};
        const double rightLength = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        right[0] /= rightLength;
        right[2] /= rightLength;
        const double up[3] = {right[1] * forward[2] - right[2] * forward[1],
                              right[2] * forward[0] - right[0] * forward[2],
                              right[0] * forward[1] - right[1] * forward[0]};
        camera.ForwardX = static_cast<float>(forward[0]);
        camera.ForwardY = static_cast<float>(forward[1]);
        camera.ForwardZ = static_cast<float>(forward[2]);
        camera.UpX = static_cast<float>(up[0]);
        camera.UpY = static_cast<float>(up[1]);
        camera.UpZ = static_cast<float>(up[2]);
        camera.RightX = static_cast<float>(right[0]);
        camera.RightY = static_cast<float>(right[1]);
        camera.RightZ = static_cast<float>(right[2]);
        camera.FieldOfView = 60.0f;
        camera.NearPlane = 0.001f;
        camera.FarPlane = 1000.0f;
        const CameraViewConstants constants = CameraViewConstants::Build(camera, static_cast<float>(width / height));
        float view[16] = {};
        constants.CopyShaderView(view);
        // VulkanDevice::AdjustProjectionForClipSpace と同じく Z を反転し、Y 反転の有無の両方で確かめる
        float projection[16] = {};
        float flippedProjection[16] = {};
        NorvesLib::Math::MatrixUtils::TransposeToShaderData(
            constants.ProjectionMatrix *
                NorvesLib::Math::MatrixUtils::CreateScale(NorvesLib::Math::Vector3(1.0f, 1.0f, -1.0f)),
            projection);
        NorvesLib::Math::MatrixUtils::TransposeToShaderData(
            constants.ProjectionMatrix *
                NorvesLib::Math::MatrixUtils::CreateScale(NorvesLib::Math::Vector3(1.0f, -1.0f, -1.0f)),
            flippedProjection);
        const float *projections[2] = {projection, flippedProjection};

        const float projectionFactor =
            static_cast<float>(height / (2.0 * std::tan(camera.FieldOfView * 0.5 * 3.14159265358979323846 / 180.0)));
        const double cameraPosition[3] = {camera.PositionX, camera.PositionY, camera.PositionZ};

        // 中心の深さと画角は MegaGeometryPass・cluster_cull.comp と同じく行列から求める
        auto computeBound = [&](const float *proj, const double *center, float radius) {
            const double position[4] = {center[0], center[1], center[2], 1.0};
            double viewPosition[4] = {};
            double clip[4] = {};
            MultiplyColumnMajor(view, position, viewPosition);
            MultiplyColumnMajor(proj, viewPosition, clip);
            const float centerDepth = static_cast<float>(clip[3] / std::abs(proj[2 * 4 + 3]));
            const double dx = center[0] - cameraPosition[0];
            const double dy = center[1] - cameraPosition[1];
            const double dz = center[2] - cameraPosition[2];
            const float centerDistance = static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
            return static_cast<double>(Mega::ComputeLODSphereErrorPixelsPerMeter(centerDistance,
                                                                                  centerDepth,
                                                                                  radius,
                                                                                  projectionFactor,
                                                                                  1.0f / std::abs(proj[0 * 4 + 0]),
                                                                                  1.0f / std::abs(proj[1 * 4 + 1])));
        };

        // 変位の前後の画素の差 / 変位の大きさ。見えない面・視錐台の外なら負
        auto measure = [&](const float *proj, const double *center, double radius, const double *normal, double delta) {
            double before[3] = {};
            double after[3] = {};
            double facing = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                before[axis] = center[axis] + radius * normal[axis];
                after[axis] = center[axis] + (radius + delta) * normal[axis];
                facing += (cameraPosition[axis] - before[axis]) * normal[axis];
            }
            if (facing <= 0.0)
            {
                return -1.0;
            }
            double pixelBefore[2] = {};
            double pixelAfter[2] = {};
            if (!ProjectToPixels(view, proj, before, width, height, pixelBefore) ||
                !ProjectToPixels(view, proj, after, width, height, pixelAfter))
            {
                return -1.0;
            }
            const double px = pixelAfter[0] - pixelBefore[0];
            const double py = pixelAfter[1] - pixelBefore[1];
            return std::sqrt(px * px + py * py) / std::abs(delta);
        };

        // カメラの基底で (tanX, tanY) の方向をワールドの単位ベクトルへ
        auto directionFromCamera = [&](double tanX, double tanY, double *out) {
            double length = 0.0;
            for (int axis = 0; axis < 3; ++axis)
            {
                out[axis] = forward[axis] + tanX * right[axis] + tanY * up[axis];
                length += out[axis] * out[axis];
            }
            length = std::sqrt(length);
            for (int axis = 0; axis < 3; ++axis)
            {
                out[axis] /= length;
            }
        };

        // 中心距離1.5・半径1・中心角 cos=0.850781059 の点を内向きに 1.336541 mm 変位させる例。
        // 角度だけの見積もり projectionFactor·D/(D²−R²) では1画素だが、画素の差は約1.56画素
        {
            double axis[3] = {};
            directionFromCamera(0.0, 0.0, axis);
            const double center[3] = {cameraPosition[0] + 1.5 * axis[0],
                                      cameraPosition[1] + 1.5 * axis[1],
                                      cameraPosition[2] + 1.5 * axis[2]};
            const double cosPhi = 0.850781059;
            const double sinPhi = std::sqrt(1.0 - cosPhi * cosPhi);
            const double normal[3] = {-cosPhi * axis[0] + sinPhi * right[0],
                                      -cosPhi * axis[1] + sinPhi * right[1],
                                      -cosPhi * axis[2] + sinPhi * right[2]};
            const double delta = 0.001336541;
            const double angularOnly = projectionFactor * 1.5 / (1.5 * 1.5 - 1.0);
            assert(angularOnly * delta > 0.99 && angularOnly * delta < 1.01);
            for (const float *proj : projections)
            {
                const double pixels = measure(proj, center, 1.0, normal, -delta) * delta;
                assert(pixels > 1.5 && pixels < 1.6);
                assert(computeBound(proj, center, 1.0f) * delta >= pixels);
            }
        }

        // 球の中心の距離・方向を変えて、見えて視錐台に入る表面の点を総当たりで内外へ変位させる
        const double golden = 3.14159265358979323846 * (3.0 - std::sqrt(5.0));
        const int sampleCount = 40000;
        const double tanOffsets[][2] = {{0.0, 0.0}, {0.3, 0.0}, {0.0, 0.4}, {-0.7, 0.3}, {0.9, -0.5}, {1.4, 0.0}};
        for (const double distanceRatio : {1.05, 1.5, 2.5, 6.0, 40.0})
        {
            for (const auto &tanOffset : tanOffsets)
            {
                const double radius = 0.75;
                double direction[3] = {};
                directionFromCamera(tanOffset[0], tanOffset[1], direction);
                const double centerDistance = distanceRatio * radius;
                const double center[3] = {cameraPosition[0] + centerDistance * direction[0],
                                          cameraPosition[1] + centerDistance * direction[1],
                                          cameraPosition[2] + centerDistance * direction[2]};
                for (const float *proj : projections)
                {
                    const double bound = computeBound(proj, center, static_cast<float>(radius));
                    assert(std::isfinite(bound) && bound > 0.0);
                    double maxMeasured = 0.0;
                    int visibleCount = 0;
                    for (int sample = 0; sample < sampleCount; ++sample)
                    {
                        const double y = 1.0 - 2.0 * (sample + 0.5) / sampleCount;
                        const double ring = std::sqrt(std::max(1.0 - y * y, 0.0));
                        const double normal[3] = {
                            ring * std::cos(golden * sample), y, ring * std::sin(golden * sample)};
                        const double delta = 1.0e-5 * radius;
                        const double outward = measure(proj, center, radius, normal, delta);
                        const double inward = measure(proj, center, radius, normal, -delta);
                        if (outward < 0.0 || inward < 0.0)
                        {
                            continue;
                        }
                        ++visibleCount;
                        maxMeasured = std::max(maxMeasured, std::max(outward, inward));
                    }
                    if (visibleCount == 0)
                    {
                        continue; // 視錐台の外の置き方
                    }
                    assert(maxMeasured <= bound * 1.001);
                    // 上限が緩すぎて粗い段を選べなくならない（見える点が十分ある置き方で、上限の6割以上が実際に出る）
                    if (visibleCount >= sampleCount / 20 && distanceRatio > 1.05)
                    {
                        assert(maxMeasured >= bound * 0.6);
                    }
                    // 近い球では、角度だけの見積もりを実際の画素の差が上回る
                    if (distanceRatio == 1.5 && tanOffset[0] == 0.0 && tanOffset[1] == 0.0)
                    {
                        const double angularOnly =
                            projectionFactor * centerDistance / (centerDistance * centerDistance - radius * radius);
                        assert(maxMeasured > angularOnly * 1.3);
                    }
                }
            }
        }
    }

    // 手続きの球の段ごとの範囲・誤差と、段の選び方の契約（GBufferは割れ目の無い一律の段、影は影の段より細かくしない）
    void TestProceduralSphereLevelRangesAndLODSelection()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;

        Mega::ProceduralMegaSphereSettings settings;
        settings.Segments = 64;
        settings.Rings = 32;
        settings.LODLevelCount = 4;
        settings.PatchCells = 4;
        Mega::ProceduralMegaSphereData sphere;
        assert(Mega::BuildProceduralMegaSphere(settings, sphere));

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));
        Mega::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = sphere.Vertices.data();
        createInfo.VertexDataSize = sphere.Vertices.size() * sizeof(Mesh3DVertex);
        createInfo.VertexCount = static_cast<uint32_t>(sphere.Vertices.size());
        createInfo.VertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
        createInfo.IndexData = sphere.Indices.data();
        createInfo.IndexCount = static_cast<uint32_t>(sphere.Indices.size());
        createInfo.Clusters = sphere.Clusters;
        createInfo.TotalBounds = sphere.Bounds;
        createInfo.LODBounds = sphere.Bounds;
        createInfo.bBuildLODHierarchy = false;
        createInfo.ShadowLODLevel = 1;
        createInfo.DebugName = "LevelRangeSphere";
        const auto handle = manager.MegaGeometry().CreateMegaMesh(createInfo);
        assert(handle.IsValid());
        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        assert(gpuData->ShadowLODLevel == 1u);
        assert(gpuData->LevelRanges.size() == settings.LODLevelCount);
        uint32_t expectedFirstIndex = 0;
        for (uint32_t level = 0; level < settings.LODLevelCount; ++level)
        {
            const Mega::MegaMeshLevelRange &range = gpuData->LevelRanges[level];
            assert(range.FirstIndex == expectedFirstIndex);
            assert(range.IndexCount == sphere.LevelTriangleCounts[level] * 3u);
            assert(range.Error == sphere.LevelErrors[level]);
            assert(level == 0u || range.Error >= gpuData->LevelRanges[level - 1u].Error);
            expectedFirstIndex += range.IndexCount;
        }
        assert(gpuData->ShadowFirstIndex == gpuData->LevelRanges[1].FirstIndex);
        assert(gpuData->ShadowIndexCount == gpuData->LevelRanges[1].IndexCount);

        // カメラが球の中なら最も細かい段
        assert(Mega::SelectCoarsestLODWithinError(
                   gpuData->LevelRanges,
                   Mega::ComputeLODSphereErrorPixelsPerMeter(0.5f, 0.5f, 1.0f, 623.5f, 1.0f, 0.58f),
                   1.0f) == 0u);

        // 一律の段の選び方は、cluster_cull.comp の親子の判定（自分の誤差 ≤ 閾値 < 親の誤差）と一致する
        for (float errorScale : {0.0f, 1.0f, 10.0f, 100.0f, 1000.0f, 1.0e5f, 1.0e7f})
        {
            const uint32_t selected = Mega::SelectCoarsestLODWithinError(gpuData->LevelRanges, errorScale, 1.0f);
            uint32_t drawnLevels = 0;
            for (uint32_t level = 0; level < settings.LODLevelCount; ++level)
            {
                const bool bSelfWithin = level == 0u || gpuData->LevelRanges[level].Error * errorScale <= 1.0f;
                const bool bParentTooCoarse = level + 1u == settings.LODLevelCount ||
                                              gpuData->LevelRanges[level + 1u].Error * errorScale > 1.0f;
                if (bSelfWithin && bParentTooCoarse)
                {
                    assert(level == selected);
                    ++drawnLevels;
                }
            }
            assert(drawnLevels == 1u);
        }

        // 影の段: 影に指定した段より細かくせず、テクセルが大きいほど粗い（単調）。
        uint32_t previous = 0;
        for (float texelSize : {0.0f, 1.0e-6f, 0.001f, 0.01f, 0.05f, 0.2f, 1.0f, 100.0f})
        {
            const uint32_t level = Mega::SelectShadowLODLevel(*gpuData, 1.0f, texelSize, 1.0f);
            assert(level >= gpuData->ShadowLODLevel);
            assert(level >= previous);
            assert(level == gpuData->ShadowLODLevel ||
                   gpuData->LevelRanges[level].Error <= texelSize);
            previous = level;
        }
        assert(Mega::SelectShadowLODLevel(*gpuData, 1.0f, 100.0f, 1.0f) == settings.LODLevelCount - 1u);
        // ワールドで2倍に伸ばした物は誤差も2倍で、同じテクセルなら細かい段に留まる
        const float texel = gpuData->LevelRanges[settings.LODLevelCount - 1u].Error * 1.5f;
        assert(Mega::SelectShadowLODLevel(*gpuData, 1.0f, texel, 1.0f) == settings.LODLevelCount - 1u);
        assert(Mega::SelectShadowLODLevel(*gpuData, 2.0f, texel, 1.0f) < settings.LODLevelCount - 1u);
    }

    // ----------------------------------------
    // 焼き込み済みの階層（NVMESH v1）の段の選び方
    // ----------------------------------------

    // 立方体の合成の階層。各面は8x8の格子で、段0は面ごとに4クラスタ（4x4格子）、段1は面ごとに1クラスタ
    // （縁の32頂点から中心への扇。縁は段0と同じ辺）、段2（根）は立方体の12三角形。グループは面ごとに4つ（段0→1）と、
    // 6面を1つにまとめるもの（段1→2）。縁の頂点を残して簡略化してあるので、面ごとに違う段を混ぜても閉じたメッシュになる。
    struct BakedCubeDag
    {
        static constexpr uint32_t GridCells = 8;
        static constexpr float FaceError = 0.05f;
        static constexpr float RootGroupError = 0.2f;

        std::vector<Mesh3DVertex> Vertices;
        std::vector<uint32_t> Indices;
        MegaGeometry::MegaMeshCreateInfo CreateInfo;
    };

    // 球の中心の平均と、全ての球を包む半径（丸めで包めなくならないよう、わずかに広げる）
    BoundingSphere EncloseSpheresForTest(const BoundingSphere *spheres, size_t count)
    {
        double center[3] = {};
        for (size_t i = 0; i < count; ++i)
        {
            center[0] += spheres[i].CenterX;
            center[1] += spheres[i].CenterY;
            center[2] += spheres[i].CenterZ;
        }
        for (double &value : center)
        {
            value /= static_cast<double>(count);
        }
        double radius = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            const double dx = spheres[i].CenterX - center[0];
            const double dy = spheres[i].CenterY - center[1];
            const double dz = spheres[i].CenterZ - center[2];
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz) + spheres[i].Radius);
        }
        BoundingSphere sphere;
        sphere.CenterX = static_cast<float>(center[0]);
        sphere.CenterY = static_cast<float>(center[1]);
        sphere.CenterZ = static_cast<float>(center[2]);
        sphere.Radius = static_cast<float>(radius) * 1.0001f + 1.0e-5f;
        return sphere;
    }

    // 頂点の外接のボックスの中心を中心にした、全頂点を包む球
    BoundingSphere SphereOfVerticesForTest(const std::vector<Mesh3DVertex> &vertices)
    {
        double lo[3] = {1e30, 1e30, 1e30};
        double hi[3] = {-1e30, -1e30, -1e30};
        for (const Mesh3DVertex &vertex : vertices)
        {
            for (int a = 0; a < 3; ++a)
            {
                lo[a] = std::min<double>(lo[a], vertex.Position[a]);
                hi[a] = std::max<double>(hi[a], vertex.Position[a]);
            }
        }
        const double center[3] = {(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
        double radius = 0.0;
        for (const Mesh3DVertex &vertex : vertices)
        {
            const double dx = vertex.Position[0] - center[0];
            const double dy = vertex.Position[1] - center[1];
            const double dz = vertex.Position[2] - center[2];
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        BoundingSphere sphere;
        sphere.CenterX = static_cast<float>(center[0]);
        sphere.CenterY = static_cast<float>(center[1]);
        sphere.CenterZ = static_cast<float>(center[2]);
        sphere.Radius = static_cast<float>(radius) * 1.0001f + 1.0e-5f;
        return sphere;
    }

    void BuildBakedCubeDag(BakedCubeDag &dag)
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        constexpr uint32_t N = BakedCubeDag::GridCells;
        constexpr uint32_t faceCount = 6;

        struct ClusterBuild
        {
            std::vector<Mesh3DVertex> Vertices;
            std::vector<uint32_t> Indices; // 自分の頂点の範囲の先頭からの相対
        };
        std::vector<ClusterBuild> builds; // 段0（24）→ 段1（6）→ 根（1）の順

        // 面 face の格子点 (i, j)（0..N）。i・j が増える向きの外積が外向きになる（負の面は u を反転する）
        const auto facePoint = [&](uint32_t face, uint32_t i, uint32_t j) -> Mesh3DVertex
        {
            const uint32_t axis = face / 2;
            const float sign = (face % 2 == 0) ? 1.0f : -1.0f;
            const uint32_t uAxis = (axis + 1) % 3;
            const uint32_t vAxis = (axis + 2) % 3;
            Mesh3DVertex vertex{};
            vertex.Position[axis] = sign;
            vertex.Position[uAxis] = sign * (2.0f * static_cast<float>(i) / N - 1.0f);
            vertex.Position[vAxis] = 2.0f * static_cast<float>(j) / N - 1.0f;
            vertex.Normal[axis] = sign;
            return vertex;
        };

        // 段0: 面ごとに4クラスタ（4x4の格子を象限ごとに）
        std::vector<BoundingSphere> level0Spheres;
        for (uint32_t face = 0; face < faceCount; ++face)
        {
            for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
            {
                const uint32_t qi = (quadrant % 2) * (N / 2);
                const uint32_t qj = (quadrant / 2) * (N / 2);
                const uint32_t side = N / 2 + 1;
                ClusterBuild build;
                for (uint32_t j = 0; j < side; ++j)
                {
                    for (uint32_t i = 0; i < side; ++i)
                    {
                        build.Vertices.push_back(facePoint(face, qi + i, qj + j));
                    }
                }
                for (uint32_t j = 0; j < N / 2; ++j)
                {
                    for (uint32_t i = 0; i < N / 2; ++i)
                    {
                        const uint32_t p00 = j * side + i;
                        const uint32_t p10 = p00 + 1;
                        const uint32_t p01 = p00 + side;
                        const uint32_t p11 = p01 + 1;
                        build.Indices.insert(build.Indices.end(), {p00, p10, p11, p00, p11, p01});
                    }
                }
                level0Spheres.push_back(SphereOfVerticesForTest(build.Vertices));
                builds.push_back(std::move(build));
            }
        }

        // 段1: 面ごとに、縁の 4N 頂点から中心の頂点への扇（縁の辺は段0と同じ）
        std::vector<BoundingSphere> faceGroupSpheres;
        for (uint32_t face = 0; face < faceCount; ++face)
        {
            faceGroupSpheres.push_back(EncloseSpheresForTest(&level0Spheres[face * 4], 4));
            std::vector<std::pair<uint32_t, uint32_t>> ring;
            for (uint32_t i = 0; i < N; ++i)
            {
                ring.emplace_back(i, 0);
            }
            for (uint32_t j = 0; j < N; ++j)
            {
                ring.emplace_back(N, j);
            }
            for (uint32_t i = N; i > 0; --i)
            {
                ring.emplace_back(i, N);
            }
            for (uint32_t j = N; j > 0; --j)
            {
                ring.emplace_back(0, j);
            }
            ClusterBuild build;
            for (const auto &point : ring)
            {
                build.Vertices.push_back(facePoint(face, point.first, point.second));
            }
            build.Vertices.push_back(facePoint(face, N / 2, N / 2));
            const uint32_t centerIndex = static_cast<uint32_t>(ring.size());
            for (uint32_t k = 0; k < ring.size(); ++k)
            {
                const uint32_t next = static_cast<uint32_t>((k + 1) % ring.size());
                build.Indices.insert(build.Indices.end(), {centerIndex, k, next});
            }
            builds.push_back(std::move(build));
        }
        const BoundingSphere rootGroupSphere = EncloseSpheresForTest(faceGroupSpheres.data(), faceGroupSpheres.size());

        // 根: 立方体の12三角形（面ごとに角の4点）
        {
            ClusterBuild build;
            for (uint32_t face = 0; face < faceCount; ++face)
            {
                const uint32_t base = static_cast<uint32_t>(build.Vertices.size());
                build.Vertices.push_back(facePoint(face, 0, 0));
                build.Vertices.push_back(facePoint(face, N, 0));
                build.Vertices.push_back(facePoint(face, N, N));
                build.Vertices.push_back(facePoint(face, 0, N));
                build.Indices.insert(build.Indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            builds.push_back(std::move(build));
        }

        // クラスタの記録と、全段を1組にしたバッファ
        MegaGeometry::MegaMeshCreateInfo &createInfo = dag.CreateInfo;
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        for (size_t clusterIndex = 0; clusterIndex < builds.size(); ++clusterIndex)
        {
            const ClusterBuild &build = builds[clusterIndex];
            Mega::MeshCluster cluster;
            cluster.IndexOffset = indexOffset;
            cluster.IndexCount = static_cast<uint32_t>(build.Indices.size());
            cluster.VertexOffset = static_cast<int32_t>(vertexOffset);
            cluster.VertexCount = static_cast<uint32_t>(build.Vertices.size());
            cluster.ConeCutoff = -1.0f;
            if (clusterIndex < faceCount * 4)
            {
                const uint32_t face = static_cast<uint32_t>(clusterIndex) / 4;
                cluster.Bounds = level0Spheres[clusterIndex];
                cluster.LODLevel = 0;
                cluster.LODError = 0.0f;
                cluster.ParentBounds = faceGroupSpheres[face];
                cluster.ParentError = BakedCubeDag::FaceError;
                cluster.GroupId = face;
            }
            else if (clusterIndex < faceCount * 5)
            {
                // 段1のクラスタは、自分を作ったグループ（面）の球と誤差を自分の値として持つ
                const uint32_t face = static_cast<uint32_t>(clusterIndex) - faceCount * 4;
                cluster.Bounds = faceGroupSpheres[face];
                cluster.LODLevel = 1;
                cluster.LODError = BakedCubeDag::FaceError;
                cluster.ParentBounds = rootGroupSphere;
                cluster.ParentError = BakedCubeDag::RootGroupError;
                cluster.GroupId = faceCount;
            }
            else
            {
                // 根: 親のグループが無い（GroupId・ParentError は既定のまま）
                cluster.Bounds = rootGroupSphere;
                cluster.LODLevel = 2;
                cluster.LODError = BakedCubeDag::RootGroupError;
            }
            createInfo.Clusters.push_back(cluster);
            dag.Vertices.insert(dag.Vertices.end(), build.Vertices.begin(), build.Vertices.end());
            dag.Indices.insert(dag.Indices.end(), build.Indices.begin(), build.Indices.end());
            vertexOffset += cluster.VertexCount;
            indexOffset += cluster.IndexCount;
        }

        createInfo.VertexData = dag.Vertices.data();
        createInfo.VertexDataSize = dag.Vertices.size() * sizeof(Mesh3DVertex);
        createInfo.VertexCount = static_cast<uint32_t>(dag.Vertices.size());
        createInfo.VertexStride = static_cast<uint32_t>(sizeof(Mesh3DVertex));
        createInfo.IndexData = dag.Indices.data();
        createInfo.IndexCount = static_cast<uint32_t>(dag.Indices.size());
        createInfo.TotalBounds = rootGroupSphere;
        createInfo.bBuildLODHierarchy = false;
        createInfo.bBakedLODHierarchy = true;
        createInfo.BakedLODLevelCount = 3;
        for (uint32_t face = 0; face < faceCount; ++face)
        {
            Mega::MeshClusterGroup group;
            group.Bounds = faceGroupSpheres[face];
            group.Error = BakedCubeDag::FaceError;
            group.ClusterOffset = face * 4;
            group.ClusterCount = 4;
            group.LODLevel = 0;
            createInfo.ClusterGroups.push_back(group);
        }
        Mega::MeshClusterGroup rootGroup;
        rootGroup.Bounds = rootGroupSphere;
        rootGroup.Error = BakedCubeDag::RootGroupError;
        rootGroup.ClusterOffset = faceCount * 4;
        rootGroup.ClusterCount = faceCount;
        rootGroup.LODLevel = 1;
        createInfo.ClusterGroups.push_back(rootGroup);
        createInfo.DebugName = "BakedCubeDag";
    }

    // 選んだクラスタの三角形が、位置で溶接した閉じた多様体（すべての辺がちょうど2つの三角形に共有され、向きが釣り合う）か
    bool IsSelectedSurfaceClosed(const BakedCubeDag &dag, const std::vector<uint32_t> &selectedClusters)
    {
        using Key = std::array<int64_t, 3>;
        const auto quantize = [&](uint32_t vertexIndex) -> Key
        {
            const Mesh3DVertex &vertex = dag.Vertices[vertexIndex];
            return Key{static_cast<int64_t>(std::llround(vertex.Position[0] * 4096.0)),
                       static_cast<int64_t>(std::llround(vertex.Position[1] * 4096.0)),
                       static_cast<int64_t>(std::llround(vertex.Position[2] * 4096.0))};
        };
        std::map<std::pair<Key, Key>, int> directed;
        for (const uint32_t clusterIndex : selectedClusters)
        {
            const auto &cluster = dag.CreateInfo.Clusters[clusterIndex];
            for (uint32_t i = 0; i + 2 < cluster.IndexCount; i += 3)
            {
                Key keys[3];
                for (uint32_t k = 0; k < 3; ++k)
                {
                    keys[k] = quantize(static_cast<uint32_t>(cluster.VertexOffset) +
                                       dag.Indices[cluster.IndexOffset + i + k]);
                }
                for (uint32_t k = 0; k < 3; ++k)
                {
                    ++directed[{keys[k], keys[(k + 1) % 3]}];
                }
            }
        }
        for (const auto &entry : directed)
        {
            // 辺ごとに、順方向が1回・逆方向が1回
            const auto reverse = directed.find({entry.first.second, entry.first.first});
            if (entry.second != 1 || reverse == directed.end() || reverse->second != 1)
            {
                return false;
            }
        }
        return !directed.empty();
    }

    void TestBakedLODUploadsParentSphereAndFlags()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        BakedCubeDag dag;
        BuildBakedCubeDag(dag);

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));
        const auto handle = manager.MegaGeometry().CreateMegaMesh(dag.CreateInfo);
        assert(handle.IsValid());
        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);

        // 全段のクラスタを1組の頂点・インデックス・クラスタのバッファに置く
        const uint32_t clusterCount = static_cast<uint32_t>(dag.CreateInfo.Clusters.size());
        assert(clusterCount == 31u);
        assert(gpuData->ClusterCount == clusterCount);
        assert(gpuData->VertexCount == dag.Vertices.size());
        assert(gpuData->IndexCount == dag.Indices.size());
        assert(gpuData->LevelRanges.size() == 3u);
        assert(gpuData->LevelRanges[0].Error == 0.0f);
        assert(gpuData->LevelRanges[1].Error == BakedCubeDag::FaceError);
        assert(gpuData->LevelRanges[2].Error == BakedCubeDag::RootGroupError);
        assert(device->CreatedBuffers.size() == 3);
        assert(device->CreatedBuffers[0]->LastUpdateSize == dag.Vertices.size() * sizeof(Mesh3DVertex));
        assert(device->CreatedBuffers[1]->LastUpdateSize == dag.Indices.size() * sizeof(uint32_t));
        assert(device->CreatedBuffers[2]->LastUpdateSize == clusterCount * sizeof(Mega::GPUClusterData));

        std::vector<Mega::GPUClusterData> uploaded(clusterCount);
        std::memcpy(uploaded.data(),
                    device->CreatedBuffers[2]->Bytes.data(),
                    clusterCount * sizeof(Mega::GPUClusterData));
        for (uint32_t i = 0; i < clusterCount; ++i)
        {
            const Mega::MeshCluster &source = dag.CreateInfo.Clusters[i];
            const Mega::GPUClusterData &gpu = uploaded[i];
            assert((gpu.Flags & Mega::GPU_CLUSTER_FLAG_BAKED_LOD) != 0u);
            assert(gpu.BoundsRadius == source.Bounds.Radius);
            assert(gpu.LODLevel == source.LODLevel);
            assert(gpu.LODError == source.LODError);
            assert(gpu.GroupId == source.GroupId);
            if (source.GroupId == Mega::INVALID_CLUSTER_GROUP_ID)
            {
                assert(i == clusterCount - 1u);
                continue;
            }
            assert(gpu.ParentError == source.ParentError);
            assert(gpu.ParentRadius == source.ParentBounds.Radius);
            assert(gpu.ParentCenterX == source.ParentBounds.CenterX);
            assert(gpu.ParentCenterY == source.ParentBounds.CenterY);
            assert(gpu.ParentCenterZ == source.ParentBounds.CenterZ);
        }
        // 同じグループのクラスタは親の球と誤差が同じ（同じ判断になる前提）
        for (const Mega::MeshClusterGroup &group : dag.CreateInfo.ClusterGroups)
        {
            const Mega::GPUClusterData &first = uploaded[group.ClusterOffset];
            for (uint32_t member = 1; member < group.ClusterCount; ++member)
            {
                const Mega::GPUClusterData &other = uploaded[group.ClusterOffset + member];
                assert(first.ParentRadius == other.ParentRadius);
                assert(first.ParentError == other.ParentError);
                assert(first.ParentCenterX == other.ParentCenterX);
                assert(first.GroupId == other.GroupId);
            }
        }

        // 壊れた階層（インデックスの範囲外・親の誤差が自分より小さい）は何も作らない
        {
            Mega::MegaMeshCreateInfo broken = dag.CreateInfo;
            broken.Clusters[3].IndexCount = broken.IndexCount; // 範囲外
            RenderResources brokenManager;
            auto brokenDevice = MakeShared<FakeDevice>();
            assert(brokenManager.Initialize(brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }
        {
            Mega::MegaMeshCreateInfo broken = dag.CreateInfo;
            broken.Clusters[5].LODError = 1.0f; // 親の誤差（0.05）より大きい
            RenderResources brokenManager;
            auto brokenDevice = MakeShared<FakeDevice>();
            assert(brokenManager.Initialize(brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }
    }

    // 距離を変えたとき、選ばれるクラスタの集まりが閉じたメッシュになり、どの段0のクラスタも欠けも二重もなく描かれる
    void TestBakedLODSelectionKeepsClosedMeshAcrossDistances()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        BakedCubeDag dag;
        BuildBakedCubeDag(dag);
        const auto &clusters = dag.CreateInfo.Clusters;
        const uint32_t clusterCount = static_cast<uint32_t>(clusters.size());
        constexpr uint32_t rootIndex = 30;

        // ワールドは一様な伸び 1.5 と並進（行ベクトル規約）
        const float world[16] = {1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f,
                                 0.0f, 0.0f, 1.5f, 0.0f, 3.0f, -2.0f, 5.0f, 1.0f};
        const float target[3] = {3.0f, -2.0f, 5.0f};
        const float directions[4][3] = {
            {0.0f, 0.0f, 1.0f}, {1.0f, 0.4f, 0.3f}, {-0.6f, 1.0f, -0.2f}, {0.3f, -0.5f, -1.0f}};

        bool bSawFinest = false;
        bool bSawFaceLevel = false;
        bool bSawRoot = false;
        bool bSawMixed = false;
        for (const auto &directionRaw : directions)
        {
            float direction[3] = {directionRaw[0], directionRaw[1], directionRaw[2]};
            const float directionLength =
                std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
            for (float &value : direction)
            {
                value /= directionLength;
            }

            // 0.3 m から 1000 m まで、対数で刻む
            for (int step = 0; step <= 480; ++step)
            {
                const float distance = 0.3f * std::pow(1000.0f / 0.3f, static_cast<float>(step) / 480.0f);
                Mega::BakedLODView view;
                for (int a = 0; a < 3; ++a)
                {
                    view.CameraPosition[a] = target[a] + direction[a] * distance;
                    view.Forward[a] = -direction[a];
                }
                view.TanHalfFovY = std::tan(0.5f * 1.04719755f); // 縦 60 度
                view.TanHalfFovX = view.TanHalfFovY * 16.0f / 9.0f;
                view.ProjectionFactor = 1080.0f / (2.0f * view.TanHalfFovY);
                view.LODBias = 1.0f;

                std::vector<uint32_t> selected;
                std::vector<bool> drawn(clusterCount, false);
                for (uint32_t i = 0; i < clusterCount; ++i)
                {
                    drawn[i] = Mega::ShouldDrawBakedCluster(clusters[i], world, view);
                    if (drawn[i])
                    {
                        selected.push_back(i);
                    }
                }

                // 同じグループのクラスタは、親のグループの判定が同じ（段0のクラスタは自分も同じ誤差0なので判断が全て同じ）
                for (const Mega::MeshClusterGroup &group : dag.CreateInfo.ClusterGroups)
                {
                    const bool bParentTooCoarse =
                        Mega::IsBakedParentTooCoarse(clusters[group.ClusterOffset], world, view);
                    for (uint32_t member = 0; member < group.ClusterCount; ++member)
                    {
                        const uint32_t index = group.ClusterOffset + member;
                        assert(Mega::IsBakedParentTooCoarse(clusters[index], world, view) == bParentTooCoarse);
                        if (group.LODLevel == 0)
                        {
                            assert(drawn[index] == drawn[group.ClusterOffset]);
                        }
                    }
                }

                // 段0のどのクラスタも、自分・面の段1・根のうちちょうど1つが描かれる
                for (uint32_t face = 0; face < 6; ++face)
                {
                    for (uint32_t quadrant = 0; quadrant < 4; ++quadrant)
                    {
                        const uint32_t leaf = face * 4 + quadrant;
                        const int drawnCount = (drawn[leaf] ? 1 : 0) + (drawn[24u + face] ? 1 : 0) +
                                               (drawn[rootIndex] ? 1 : 0);
                        assert(drawnCount == 1);
                    }
                }

                assert(IsSelectedSurfaceClosed(dag, selected));

                bool bAnyLevel0 = false;
                bool bAnyLevel1 = false;
                bool bAnyRoot = false;
                for (const uint32_t index : selected)
                {
                    bAnyLevel0 |= clusters[index].LODLevel == 0;
                    bAnyLevel1 |= clusters[index].LODLevel == 1;
                    bAnyRoot |= clusters[index].LODLevel == 2;
                }
                bSawFinest |= bAnyLevel0 && !bAnyLevel1 && !bAnyRoot;
                bSawFaceLevel |= bAnyLevel1 && !bAnyLevel0 && !bAnyRoot;
                bSawRoot |= bAnyRoot && !bAnyLevel0 && !bAnyLevel1;
                bSawMixed |= bAnyLevel0 && bAnyLevel1;
            }
        }
        // 距離で段が切り替わり、面ごとに違う段が混ざる切り方でも閉じている
        assert(bSawFinest);
        assert(bSawFaceLevel);
        assert(bSawRoot);
        assert(bSawMixed);

        // カメラが球の中なら（最も近い点までの距離が0）最も細かい段
        Mega::BakedLODView inside;
        inside.CameraPosition[0] = target[0];
        inside.CameraPosition[1] = target[1];
        inside.CameraPosition[2] = target[2];
        inside.TanHalfFovY = 0.577f;
        inside.TanHalfFovX = 1.0f;
        inside.ProjectionFactor = 935.0f;
        for (uint32_t i = 0; i < clusterCount; ++i)
        {
            assert(Mega::ShouldDrawBakedCluster(clusters[i], world, inside) == (clusters[i].LODLevel == 0));
        }
    }

    void TestSharedHandleCounter()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        const BufferHandle buffer = manager.Gpu().CreateBuffer(MakeCounterBufferInfo());
        assert(buffer.IsValid());
        assert(buffer.Id == 1);

        MeshFixture mesh("CounterMega");
        const auto megaMesh = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(megaMesh.IsValid());
        assert(megaMesh.Id == 2);

        const ModelHandle model = manager.MegaGeometry().RegisterModel(megaMesh, "CounterModel", "counter.mesh");
        assert(model.IsValid());
        assert(model.Id == 3);

        const BufferHandle secondBuffer = manager.Gpu().CreateBuffer(MakeCounterBufferInfo());
        assert(secondBuffer.IsValid());
        assert(secondBuffer.Id == 4);
    }

    void TestCreateFailureDoesNotRegister(size_t failBufferCreateIndex)
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));
        device->FailBufferCreateIndex = failBufferCreateIndex;

        MeshFixture mesh("FailureMega");
        const auto failedHandle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(!failedHandle.IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(MakeMegaMeshHandle(1)) == nullptr);
        assert(manager.GetResourceStats().BufferCount == 0);

        device->FailBufferCreateIndex = 0;
        const auto retryHandle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(retryHandle.IsValid());
        assert(retryHandle.Id == 1);
        assert(manager.MegaGeometry().GetMegaMeshGPUData(retryHandle) != nullptr);
    }

    void TestModelRegisterAndReleaseCoupledMegaMesh()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        MeshFixture mesh("ModelMega");
        const auto megaMesh = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(megaMesh.IsValid());

        const ModelHandle invalidModel = manager.MegaGeometry().RegisterModel(MegaGeometry::MegaMeshHandle::Invalid());
        assert(!invalidModel.IsValid());

        const ModelHandle model = manager.MegaGeometry().RegisterModel(megaMesh, "Model", "model.mesh");
        assert(model.IsValid());
        assert(manager.MegaGeometry().GetModelMegaMeshHandle(model).Id == megaMesh.Id);

        manager.MegaGeometry().ReleaseModel(model);
        assert(!manager.MegaGeometry().GetModelMegaMeshHandle(model).IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(megaMesh) == nullptr);
    }

    void TestFinalizeModelStagingNoTextureSuccess()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        ModelStaging::ModelStagingData staging;
        staging.Vertices.push_back(Mesh3DVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
        staging.Vertices.push_back(Mesh3DVertex{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
        staging.Vertices.push_back(Mesh3DVertex{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
        staging.ClusterizedIndices.push_back(0);
        staging.ClusterizedIndices.push_back(1);
        staging.ClusterizedIndices.push_back(2);

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        staging.Clusters.push_back(cluster);
        staging.TotalBounds = cluster.Bounds;
        staging.DebugName = "StagedModel";
        staging.ResolvedPath = "staged.model";

        assert(ModelStaging::GetStagedTextureCount(staging) == 0);
        assert(ModelStaging::GetStagedPreparedTextureCount(staging) == 0);
        assert(ModelStaging::GetStagedLooseTextureBytes(staging) == 0);

        const ModelHandle model = ModelStaging::FinalizeModelStaging(
            staging,
            ModelLoadResourceContext{manager.Textures(), manager.MegaGeometry()},
            "test",
            1);
        assert(model.IsValid());

        const MegaGeometry::MegaMeshHandle megaMesh = manager.MegaGeometry().GetModelMegaMeshHandle(model);
        assert(megaMesh.IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(megaMesh) != nullptr);

        const uint64_t expectedVertexBytes = staging.Vertices.size() * sizeof(Mesh3DVertex);
        const uint64_t expectedIndexBytes = staging.ClusterizedIndices.size() * sizeof(uint32_t);
        const uint64_t expectedClusterBytes = staging.Clusters.size() * sizeof(MegaGeometry::GPUClusterData);
        assert(device->CreatedBufferDescs.size() == 3);
        assert(device->CreatedBufferDescs[0].Size == expectedVertexBytes);
        assert(device->CreatedBufferDescs[0].Usage ==
               (NorvesLib::RHI::ResourceUsage::VertexBuffer | NorvesLib::RHI::ResourceUsage::StorageBuffer));
        assert(device->CreatedBufferDescs[1].Size == expectedIndexBytes);
        assert(device->CreatedBufferDescs[1].Usage ==
               (NorvesLib::RHI::ResourceUsage::IndexBuffer | NorvesLib::RHI::ResourceUsage::StorageBuffer));
        assert(device->CreatedBufferDescs[2].Size == expectedClusterBytes);
        assert(device->CreatedBufferDescs[2].Usage == NorvesLib::RHI::ResourceUsage::StorageBuffer);
        assert(device->CreatedBuffers.size() == 3);
        assert(device->CreatedBuffers[0]->LastUpdateSize == expectedVertexBytes);
        assert(device->CreatedBuffers[1]->LastUpdateSize == expectedIndexBytes);
        assert(device->CreatedBuffers[2]->LastUpdateSize == expectedClusterBytes);

        manager.MegaGeometry().ReleaseModel(model);
        assert(!manager.MegaGeometry().GetModelMegaMeshHandle(model).IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(megaMesh) == nullptr);
    }

    void TestBuildModelStagingMapsCookedDataAndOwnsStrings()
    {
        ModelStaging::ModelStagingData staging;
        {
            const std::vector<uint8_t> stringBytes = {
                'T', 'e', 'x', 't', 'u', 'r', 'e', 's', '/', 'A', '.', 'p', 'n', 'g',
                'T', 'e', 'x', 't', 'u', 'r', 'e', 's', '/', 'N', '.', 'p', 'n', 'g',
                'T', 'e', 'x', 't', 'u', 'r', 'e', 's', '/', 'R', '.', 'p', 'n', 'g'};
            NorvesLib::Core::Asset::CookedMeshData cooked;
            cooked.SourceBlob = NorvesLib::Core::Asset::AssetBlob::CopyBytes(
                NorvesLib::Core::Container::Span<const uint8_t>(stringBytes.data(), stringBytes.size()),
                "mapping.nvmesh");
            cooked.StringTableOffset = 0;
            cooked.StringTableSize = stringBytes.size();
            cooked.TotalBoundsCenter = {0.25f, 0.5f, 0.75f};
            cooked.TotalBoundsRadius = 2.5f;
            cooked.Vertices.push_back({{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 1.0f}, {0.25f, 0.5f}});
            cooked.Vertices.push_back({{4.0f, 5.0f, 6.0f}, {0.0f, 1.0f, 0.0f}, {0.75f, 1.0f}});
            cooked.Indices.push_back(2);
            cooked.Indices.push_back(1);
            cooked.Indices.push_back(0);

            NorvesLib::Core::Asset::CookedMeshCluster cluster;
            cluster.BoundsCenter = {0.5f, 0.75f, 1.0f};
            cluster.BoundsRadius = 1.25f;
            cluster.ConeAxis = {0.0f, 0.0f, 1.0f};
            cluster.ConeCutoff = 0.4f;
            cluster.IndexOffset = 0;
            cluster.IndexCount = 3;
            cluster.VertexOffset = 0;
            cluster.VertexCount = 2;
            cooked.Clusters.push_back(cluster);

            NorvesLib::Core::Asset::CookedMeshMaterial material;
            material.AlbedoTexture = {0, 14};
            material.NormalTexture = {14, 14};
            material.ArmTexture = {28, 14};
            cooked.Materials.push_back(material);

            assert(ModelAssetLoader::BuildModelStagingFromCookedMesh(
                cooked,
                "MappingModel",
                "Models/Mapping.nvmesh",
                staging));
        }

        assert(staging.Vertices.size() == 2);
        assert(staging.Vertices[0].Position[0] == 1.0f);
        assert(staging.Vertices[0].Position[1] == 2.0f);
        assert(staging.Vertices[0].Position[2] == 3.0f);
        assert(staging.Vertices[0].Normal[2] == 1.0f);
        assert(staging.Vertices[0].TexCoord[0] == 0.25f);
        assert(staging.Vertices[1].TexCoord[1] == 1.0f);
        assert(staging.ClusterizedIndices.size() == 3);
        assert(staging.ClusterizedIndices[0] == 2);
        assert(staging.ClusterizedIndices[2] == 0);
        assert(staging.Clusters.size() == 1);
        assert(staging.Clusters[0].Bounds.CenterX == 0.5f);
        assert(staging.Clusters[0].Bounds.CenterY == 0.75f);
        assert(staging.Clusters[0].Bounds.CenterZ == 1.0f);
        assert(staging.Clusters[0].Bounds.Radius == 1.25f);
        assert(staging.Clusters[0].ConeAxisZ == 1.0f);
        assert(staging.Clusters[0].ConeCutoff == 0.4f);
        assert(staging.Clusters[0].IndexOffset == 0);
        assert(staging.Clusters[0].IndexCount == 3);
        assert(staging.Clusters[0].VertexOffset == 0);
        assert(staging.Clusters[0].VertexCount == 2);
        assert(staging.Clusters[0].MaterialIndex == 0);
        assert(staging.Clusters[0].LODLevel == 0);
        assert(staging.Clusters[0].LODError == 0.0f);
        assert(staging.Clusters[0].ParentStart == 0);
        assert(staging.Clusters[0].ParentCount == 0);
        assert(staging.TotalBounds.CenterX == 0.25f);
        assert(staging.TotalBounds.CenterY == 0.5f);
        assert(staging.TotalBounds.CenterZ == 0.75f);
        assert(staging.TotalBounds.Radius == 2.5f);
        assert(staging.DebugName == "MappingModel");
        assert(staging.ResolvedPath == "Models/Mapping.nvmesh");
        assert(staging.TextureReferences.Albedo.RequestPath == "Textures/A.png");
        assert(staging.TextureReferences.Normal.RequestPath == "Textures/N.png");
        assert(staging.TextureReferences.Arm.RequestPath == "Textures/R.png");
    }

    std::filesystem::path CreateCookedModelTestRoot(const char* suffix)
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path root = std::filesystem::temp_directory_path() /
                                     (std::string("NorvesLibCookedModel_") + suffix + "_" + std::to_string(now));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    NorvesLib::Core::Asset::AssetSystem CreateCookedModelAssetSystem(
        const std::filesystem::path& root,
        const CookedModelSupport::ByteArray& payload)
    {
        CookedModelSupport::WriteBinaryFile(
            root / "Cooked" / "Models.nvpkg",
            CookedModelSupport::BuildModelPackage(payload));
        const uint64_t cookedHash = NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash(
            payload.data(),
            payload.size());
        NorvesLib::Core::Asset::AssetSystem assetSystem(root.generic_string().c_str());
        assert(assetSystem.LoadManifestFromJsonText(CookedModelSupport::BuildModelManifest(cookedHash)));
        return assetSystem;
    }

    void TestLoadCookedModelThroughPublicResources()
    {
        const std::filesystem::path root = CreateCookedModelTestRoot("valid");
        const CookedModelSupport::ByteArray payload = CookedModelSupport::BuildCookedModelMesh();
        NorvesLib::Core::Asset::AssetSystem assetSystem = CreateCookedModelAssetSystem(root, payload);

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));
        const ModelHandle model = manager.MegaGeometry().LoadModel(assetSystem, "Models/Triangle.nvmesh");
        assert(model.IsValid());

        const MegaGeometry::MegaMeshHandle megaMesh = manager.MegaGeometry().GetModelMegaMeshHandle(model);
        assert(megaMesh.IsValid());
        const MegaGeometry::MegaMeshGPUData* gpuData = manager.MegaGeometry().GetMegaMeshGPUData(megaMesh);
        assert(gpuData != nullptr);
        assert(gpuData->VertexCount == 3);
        assert(gpuData->IndexCount == 3);
        assert(gpuData->ClusterCount == 1);
        assert(gpuData->TotalBounds.CenterX == 0.5f);
        assert(gpuData->TotalBounds.CenterY == 0.5f);
        assert(gpuData->TotalBounds.Radius == 1.0f);
        assert(device->CreatedBufferDescs.size() == 3);
        assert(device->CreatedBuffers.size() == 3);
        assert(device->CreatedBufferDescs[0].Size == 3 * sizeof(Mesh3DVertex));
        assert(device->CreatedBufferDescs[1].Size == 3 * sizeof(uint32_t));
        assert(device->CreatedBufferDescs[2].Size == sizeof(MegaGeometry::GPUClusterData));
        assert(device->CreatedBuffers[0]->LastUpdateSize == 3 * sizeof(Mesh3DVertex));
        assert(device->CreatedBuffers[1]->LastUpdateSize == 3 * sizeof(uint32_t));
        assert(device->CreatedBuffers[2]->LastUpdateSize == sizeof(MegaGeometry::GPUClusterData));

        MegaGeometry::GPUClusterData uploadedCluster{};
        std::memcpy(&uploadedCluster,
                    device->CreatedBuffers[2]->Bytes.data(),
                    sizeof(MegaGeometry::GPUClusterData));
        assert(uploadedCluster.IndexOffset == 0);
        assert(uploadedCluster.IndexCount == 3);
        assert(uploadedCluster.VertexOffset == 0);
        assert(uploadedCluster.MaterialIndex == 0);
        assert(uploadedCluster.LODLevel == 0);
        assert(uploadedCluster.LODError == 0.0f);
        assert(uploadedCluster.ParentStart == 0);
        assert(uploadedCluster.ParentCount == 0);

        manager.MegaGeometry().ReleaseModel(model);
        assert(!manager.MegaGeometry().GetModelMegaMeshHandle(model).IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(megaMesh) == nullptr);
        std::filesystem::remove_all(root);
    }

    void TestCorruptCookedModelCreatesNoBuffers()
    {
        const std::filesystem::path root = CreateCookedModelTestRoot("corrupt");
        const CookedModelSupport::ByteArray payload = {'B', 'A', 'D'};
        NorvesLib::Core::Asset::AssetSystem assetSystem = CreateCookedModelAssetSystem(root, payload);
        const NorvesLib::Core::Asset::AssetResolveResult resolveResult = assetSystem.ResolveAsset(
            "Models/Triangle.nvmesh",
            NorvesLib::Core::Asset::AssetKind::Model,
            NorvesLib::Core::Asset::AssetManifest::DefaultVariant,
            NorvesLib::Core::Asset::AssetFallbackMode::FailOnCookedFailure);
        assert(resolveResult.UsedCooked());
        assert(NorvesLib::Core::Asset::ParseCookedMesh(resolveResult.Blob).Status !=
               NorvesLib::Core::Asset::CookedMeshParseStatus::Success);

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));
        const size_t bufferDescCount = device->CreatedBufferDescs.size();
        const size_t bufferCount = device->CreatedBuffers.size();
        const ModelHandle model = manager.MegaGeometry().LoadModel(assetSystem, "Models/Triangle.nvmesh");
        assert(!model.IsValid());
        assert(device->CreatedBufferDescs.size() == bufferDescCount);
        assert(device->CreatedBuffers.size() == bufferCount);
        std::filesystem::remove_all(root);
    }

    void TestReleaseClearShutdown()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(manager.Initialize(device));

        MeshFixture releaseMesh("ReleaseMega");
        const auto releaseHandle = manager.MegaGeometry().CreateMegaMesh(releaseMesh.CreateInfo);
        assert(releaseHandle.IsValid());
        manager.MegaGeometry().ReleaseMegaMesh(MegaGeometry::MegaMeshHandle::Invalid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(releaseHandle) != nullptr);
        manager.MegaGeometry().ReleaseMegaMesh(releaseHandle);
        assert(manager.MegaGeometry().GetMegaMeshGPUData(releaseHandle) == nullptr);

        MeshFixture clearMesh("ClearMega");
        const auto clearHandle = manager.MegaGeometry().CreateMegaMesh(clearMesh.CreateInfo);
        assert(clearHandle.IsValid());
        manager.ClearAllResources();
        assert(manager.MegaGeometry().GetMegaMeshGPUData(clearHandle) == nullptr);
        assert(manager.GetResourceStats().BufferCount == 0);

        MeshFixture shutdownMesh("ShutdownMega");
        const auto shutdownHandle = manager.MegaGeometry().CreateMegaMesh(shutdownMesh.CreateInfo);
        assert(shutdownHandle.IsValid());
        manager.Shutdown();
        assert(manager.MegaGeometry().GetMegaMeshGPUData(shutdownHandle) == nullptr);
        assert(manager.GetResourceStats().BufferCount == 0);
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "MegaGeometryResourcesTest start\n";

    TestCreateBeforeInitialize();
    TestInvalidCreateInfoCreatesNoBuffers();
    TestMegaEmissiveCanonicalContractRejectsInvalidWithoutSideEffects();
    TestSuccessfulNoLodUpload();
    TestProceduralSphereLevelRangesAndLODSelection();
    TestLODSphereErrorBoundsPerspectivePixelDisplacement();
    TestBakedLODUploadsParentSphereAndFlags();
    TestBakedLODSelectionKeepsClosedMeshAcrossDistances();
    TestSharedHandleCounter();
    TestCreateFailureDoesNotRegister(1);
    TestCreateFailureDoesNotRegister(2);
    TestCreateFailureDoesNotRegister(3);
    TestModelRegisterAndReleaseCoupledMegaMesh();
    TestFinalizeModelStagingNoTextureSuccess();
    TestBuildModelStagingMapsCookedDataAndOwnsStrings();
    TestLoadCookedModelThroughPublicResources();
    TestCorruptCookedModelCreatesNoBuffers();
    TestReleaseClearShutdown();

    std::cout << "MegaGeometryResourcesTest passed\n";
    return 0;
}
