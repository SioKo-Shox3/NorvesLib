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
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
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
        assert(sizeof(MegaGeometry::GPUClusterData) == 64);
        assert(offsetof(MegaGeometry::GPUClusterData, BoundsCenterX) == 0);
        assert(offsetof(MegaGeometry::GPUClusterData, ConeAxisX) == 16);
        assert(offsetof(MegaGeometry::GPUClusterData, IndexOffset) == 32);
        assert(offsetof(MegaGeometry::GPUClusterData, LODLevel) == 48);
        assert(offsetof(MegaGeometry::GPUClusterData, LODError) == 52);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentStart) == 56);
        assert(offsetof(MegaGeometry::GPUClusterData, ParentCount) == 60);
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
