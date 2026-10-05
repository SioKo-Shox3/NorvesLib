#include "Asset/AssetSystem.h"
#include "Asset/CookedMeshFormat.h"
#include "Container/FixedArray.h"
#include "Container/Map.h"
#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/GeometryPool.h"
#include "Rendering/RenderResources.h"
#include "Rendering/MegaGeometry/CookedMeshMegaMeshAdapter.h"
#include "Rendering/MegaGeometry/GeometryPageLinks.h"
#include "Rendering/MegaGeometry/GeometryPageRequestSet.h"
#include "Rendering/MegaGeometry/GeometryPageTable.h"
#include "Rendering/MegaGeometry/MegaGeometryBvhSelection.h"
#include "Rendering/MegaGeometry/MegaGeometryLODSelection.h"
#include "Rendering/MegaGeometry/ProceduralMegaSphere.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Library/Core/Private/Resource/ModelAssetLoader.h"
#include "Library/Core/Private/Resource/ModelStaging.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include "Test/Core/Rendering/GeometryUploadTestSupport.h"

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
namespace Container = NorvesLib::Core::Container;
namespace AssetFormat = NorvesLib::Core::Asset;
namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;
namespace ModelAssetLoader = NorvesLib::Core::Resource;
namespace ModelStaging = NorvesLib::Core::Resource::ModelStaging;
namespace GeometryUpload = NorvesLib::Test::GeometryUpload;

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
        assert(sizeof(MegaGeometry::GPUClusterData) == 112);
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
        assert(offsetof(MegaGeometry::GPUClusterData, ChildPageId) == 96);
    }

    // 試験の偽バッファは塊の大きさのメモリを持つので、プールの塊を小さくして動かす
    constexpr uint64_t TestPoolBlockBytes = 1ull << 20;

    bool InitializeWithSmallPool(RenderResources &manager, const Container::TSharedPtr<FakeDevice> &device)
    {
        manager.SetGeometryPoolBlockBytes(TestPoolBlockBytes);
        return manager.Initialize(device);
    }

    // メッシュの領域（クラスタ・頂点・インデックス）の先頭の位置の、プールの塊のバイト列
    const uint8_t *PoolBytesAt(const MegaGeometry::MegaMeshGPUData &gpuData, uint64_t offsetBytes)
    {
        const auto *buffer = static_cast<const FakeBuffer *>(gpuData.VertexBuffer.get());
        assert(buffer != nullptr);
        assert(offsetBytes <= buffer->Bytes.size());
        return buffer->Bytes.data() + offsetBytes;
    }

    // 1つのメッシュの区画: 3つの領域は同じ塊の中に、256 バイト整列で重ならずに並ぶ
    void AssertRegionLayout(const MegaGeometry::MegaMeshGPUData &gpuData)
    {
        assert(gpuData.VertexBuffer);
        assert(gpuData.VertexBuffer.get() == gpuData.IndexBuffer.get());
        assert(gpuData.VertexBuffer.get() == gpuData.ClusterBuffer.get());
        assert(gpuData.ClusterBufferOffsetBytes % 256 == 0);
        assert(gpuData.VertexBufferOffsetBytes % 256 == 0);
        assert(gpuData.IndexBufferOffsetBytes % 256 == 0);
        assert(gpuData.ClusterBufferOffsetBytes + gpuData.ClusterBufferBytes <= gpuData.VertexBufferOffsetBytes);
        assert(gpuData.VertexBufferOffsetBytes + gpuData.VertexBufferBytes <= gpuData.IndexBufferOffsetBytes);
        assert(gpuData.IndexBufferOffsetBytes + gpuData.IndexBufferBytes <= gpuData.VertexBuffer->GetSize());
    }

    // 作成の直後: メッシュごとのバッファは作らず、プールの塊を1つだけ作る（リングは最初の書き込みまで作らない）
    void AssertNoLodUploadBuffers(const FakeDevice &device, const MegaGeometry::MegaMeshGPUData &gpuData)
    {
        AssertGPUClusterLayout();

        assert(device.CreatedBufferDescs.size() == 1);
        const NorvesLib::RHI::BufferDesc &pool = device.CreatedBufferDescs[0];
        assert(pool.Size == TestPoolBlockBytes);
        assert(!pool.CPUAccessible);
        assert(HasUsage(pool.Usage, NorvesLib::RHI::ResourceUsage::VertexBuffer));
        assert(HasUsage(pool.Usage, NorvesLib::RHI::ResourceUsage::IndexBuffer));
        assert(HasUsage(pool.Usage, NorvesLib::RHI::ResourceUsage::StorageBuffer));
        assert(HasUsage(pool.Usage, NorvesLib::RHI::ResourceUsage::TransferDst));
        assert(HasUsage(pool.Usage, NorvesLib::RHI::ResourceUsage::BufferDeviceAddress));

        assert(gpuData.VertexBufferBytes == sizeof(MeshFixture::Vertices));
        assert(gpuData.IndexBufferBytes == 3 * sizeof(uint32_t));
        assert(gpuData.ClusterBufferBytes == sizeof(MegaGeometry::GPUClusterData));
        AssertRegionLayout(gpuData);
    }

    // 書き込みが終わった後: ステージングのリングが1本増え、区画へ頂点・インデックス・クラスタが入っている
    void AssertNoLodUploadContents(const FakeDevice &device, const MegaGeometry::MegaMeshGPUData &gpuData)
    {
        assert(device.CreatedBuffers.size() == 2);
        const MeshFixture expected("ExpectedContents");
        assert(std::memcmp(PoolBytesAt(gpuData, gpuData.VertexBufferOffsetBytes),
                           expected.Vertices,
                           sizeof(MeshFixture::Vertices)) == 0);
        assert(std::memcmp(PoolBytesAt(gpuData, gpuData.IndexBufferOffsetBytes),
                           expected.Indices,
                           sizeof(MeshFixture::Indices)) == 0);

        MegaGeometry::GPUClusterData uploadedCluster{};
        std::memcpy(&uploadedCluster,
                    PoolBytesAt(gpuData, gpuData.ClusterBufferOffsetBytes),
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
        assert(InitializeWithSmallPool(manager, device));

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
        assert(InitializeWithSmallPool(manager, device));

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
        assert(InitializeWithSmallPool(manager, device));

        MeshFixture mesh("NoLodMega");
        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(handle.IsValid());

        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        AssertNoLodUploadBuffers(*device, *gpuData);

        // 書き込みが GPU で完了するまでは、描画・影・レイトレーシングへ渡さない
        assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handle) == nullptr);
        assert(manager.MegaGeometry().HasPendingGpuUploads());
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(!manager.MegaGeometry().HasPendingGpuUploads());
        assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handle) == gpuData);
        AssertNoLodUploadContents(*device, *gpuData);
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
        assert(InitializeWithSmallPool(manager, device));
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
    // 組み立てた階層は NVMESH v1 に書き出して ParseCookedMesh で読み戻し、アダプタで MegaMeshCreateInfo にする
    // （クッカーが書いたものを読み込む経路と同じ）。
    struct BakedCubeDag
    {
        static constexpr uint32_t GridCells = 8;
        static constexpr float FaceError = 0.05f;
        static constexpr float RootGroupError = 0.2f;

        // 読み戻したメッシュ。CreateInfo の頂点・インデックスはこの配列を指す
        AssetFormat::CookedMeshParseResult Parsed;
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
    BoundingSphere SphereOfVerticesForTest(const Container::VariableArray<AssetFormat::CookedMeshVertex> &vertices)
    {
        double lo[3] = {1e30, 1e30, 1e30};
        double hi[3] = {-1e30, -1e30, -1e30};
        for (const AssetFormat::CookedMeshVertex &vertex : vertices)
        {
            const double position[3] = {vertex.Position.X, vertex.Position.Y, vertex.Position.Z};
            for (int a = 0; a < 3; ++a)
            {
                lo[a] = std::min(lo[a], position[a]);
                hi[a] = std::max(hi[a], position[a]);
            }
        }
        const double center[3] = {(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
        double radius = 0.0;
        for (const AssetFormat::CookedMeshVertex &vertex : vertices)
        {
            const double dx = vertex.Position.X - center[0];
            const double dy = vertex.Position.Y - center[1];
            const double dz = vertex.Position.Z - center[2];
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        BoundingSphere sphere;
        sphere.CenterX = static_cast<float>(center[0]);
        sphere.CenterY = static_cast<float>(center[1]);
        sphere.CenterZ = static_cast<float>(center[2]);
        sphere.Radius = static_cast<float>(radius) * 1.0001f + 1.0e-5f;
        return sphere;
    }

    AssetFormat::CookedMeshFloat3 SphereCenterForTest(const BoundingSphere &sphere)
    {
        return {sphere.CenterX, sphere.CenterY, sphere.CenterZ};
    }

    void BuildBakedCubeDag(BakedCubeDag &dag)
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        constexpr uint32_t N = BakedCubeDag::GridCells;
        constexpr uint32_t faceCount = 6;

        struct ClusterBuild
        {
            Container::VariableArray<AssetFormat::CookedMeshVertex> Vertices;
            Container::VariableArray<uint32_t> Indices; // 自分の頂点の範囲の先頭からの相対
        };
        Container::VariableArray<ClusterBuild> builds; // 段0（24）→ 段1（6）→ 根（1）の順

        // 面 face の格子点 (i, j)（0..N）。i・j が増える向きの外積が外向きになる（負の面は u を反転する）
        const auto facePoint = [&](uint32_t face, uint32_t i, uint32_t j) -> AssetFormat::CookedMeshVertex
        {
            const uint32_t axis = face / 2;
            const float sign = (face % 2 == 0) ? 1.0f : -1.0f;
            const uint32_t uAxis = (axis + 1) % 3;
            const uint32_t vAxis = (axis + 2) % 3;
            float position[3] = {};
            float normal[3] = {};
            position[axis] = sign;
            position[uAxis] = sign * (2.0f * static_cast<float>(i) / N - 1.0f);
            position[vAxis] = 2.0f * static_cast<float>(j) / N - 1.0f;
            normal[axis] = sign;
            AssetFormat::CookedMeshVertex vertex;
            vertex.Position = {position[0], position[1], position[2]};
            vertex.Normal = {normal[0], normal[1], normal[2]};
            return vertex;
        };

        // 段0: 面ごとに4クラスタ（4x4の格子を象限ごとに）
        Container::VariableArray<BoundingSphere> level0Spheres;
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
                        for (const uint32_t index : {p00, p10, p11, p00, p11, p01})
                        {
                            build.Indices.push_back(index);
                        }
                    }
                }
                level0Spheres.push_back(SphereOfVerticesForTest(build.Vertices));
                builds.push_back(std::move(build));
            }
        }

        // 段1: 面ごとに、縁の 4N 頂点から中心の頂点への扇（縁の辺は段0と同じ）
        struct RingPoint
        {
            uint32_t I;
            uint32_t J;
        };
        Container::VariableArray<BoundingSphere> faceGroupSpheres;
        for (uint32_t face = 0; face < faceCount; ++face)
        {
            faceGroupSpheres.push_back(EncloseSpheresForTest(&level0Spheres[face * 4], 4));
            Container::VariableArray<RingPoint> ring;
            for (uint32_t i = 0; i < N; ++i)
            {
                ring.push_back({i, 0});
            }
            for (uint32_t j = 0; j < N; ++j)
            {
                ring.push_back({N, j});
            }
            for (uint32_t i = N; i > 0; --i)
            {
                ring.push_back({i, N});
            }
            for (uint32_t j = N; j > 0; --j)
            {
                ring.push_back({0, j});
            }
            ClusterBuild build;
            for (const RingPoint &point : ring)
            {
                build.Vertices.push_back(facePoint(face, point.I, point.J));
            }
            build.Vertices.push_back(facePoint(face, N / 2, N / 2));
            const uint32_t centerIndex = static_cast<uint32_t>(ring.size());
            for (uint32_t k = 0; k < ring.size(); ++k)
            {
                const uint32_t next = static_cast<uint32_t>((k + 1) % ring.size());
                for (const uint32_t index : {centerIndex, k, next})
                {
                    build.Indices.push_back(index);
                }
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
                for (const uint32_t index : {base, base + 1, base + 2, base, base + 2, base + 3})
                {
                    build.Indices.push_back(index);
                }
            }
            builds.push_back(std::move(build));
        }

        // クラスタの記録と、全段を1組にした頂点・インデックス（NVMESH v1 の書き出しの入力）
        AssetFormat::CookedMeshV1WriteInput input;
        input.TotalBoundsCenter = SphereCenterForTest(rootGroupSphere);
        input.TotalBoundsRadius = rootGroupSphere.Radius;
        input.LODLevelCount = 3;
        input.FallbackError = BakedCubeDag::RootGroupError;
        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;
        uint32_t rootVertexOffset = 0;
        for (size_t clusterIndex = 0; clusterIndex < builds.size(); ++clusterIndex)
        {
            const ClusterBuild &build = builds[clusterIndex];
            AssetFormat::CookedMeshCluster cluster;
            cluster.IndexOffset = indexOffset;
            cluster.IndexCount = static_cast<uint32_t>(build.Indices.size());
            cluster.VertexOffset = vertexOffset;
            cluster.VertexCount = static_cast<uint32_t>(build.Vertices.size());
            cluster.ConeCutoff = -1.0f;
            if (clusterIndex < faceCount * 4)
            {
                const uint32_t face = static_cast<uint32_t>(clusterIndex) / 4;
                cluster.BoundsCenter = SphereCenterForTest(level0Spheres[clusterIndex]);
                cluster.BoundsRadius = level0Spheres[clusterIndex].Radius;
                cluster.LODLevel = 0;
                cluster.LODError = 0.0f;
                cluster.ParentBoundsCenter = SphereCenterForTest(faceGroupSpheres[face]);
                cluster.ParentBoundsRadius = faceGroupSpheres[face].Radius;
                cluster.ParentError = BakedCubeDag::FaceError;
                cluster.GroupId = face;
                cluster.bIsRoot = false;
            }
            else if (clusterIndex < faceCount * 5)
            {
                // 段1のクラスタは、自分を作ったグループ（面）の球と誤差を自分の値として持つ
                const uint32_t face = static_cast<uint32_t>(clusterIndex) - faceCount * 4;
                cluster.BoundsCenter = SphereCenterForTest(faceGroupSpheres[face]);
                cluster.BoundsRadius = faceGroupSpheres[face].Radius;
                cluster.LODLevel = 1;
                cluster.LODError = BakedCubeDag::FaceError;
                cluster.ParentBoundsCenter = SphereCenterForTest(rootGroupSphere);
                cluster.ParentBoundsRadius = rootGroupSphere.Radius;
                cluster.ParentError = BakedCubeDag::RootGroupError;
                cluster.GroupId = faceCount;
                cluster.bIsRoot = false;
            }
            else
            {
                // 根: 親のグループが無い（GroupId・ParentError・bIsRoot は既定のまま）
                cluster.BoundsCenter = SphereCenterForTest(rootGroupSphere);
                cluster.BoundsRadius = rootGroupSphere.Radius;
                cluster.LODLevel = 2;
                cluster.LODError = BakedCubeDag::RootGroupError;
                rootVertexOffset = vertexOffset;
            }
            input.Clusters.push_back(cluster);
            for (const AssetFormat::CookedMeshVertex &vertex : build.Vertices)
            {
                input.Vertices.push_back(vertex);
            }
            for (const uint32_t index : build.Indices)
            {
                input.ClusterIndices.push_back(index);
            }
            vertexOffset += cluster.VertexCount;
            indexOffset += cluster.IndexCount;
        }

        // フォールバックの段は根と同じ形（基点の頂点は 0 なので、全体の頂点の番号で書く）
        for (const uint32_t index : builds.back().Indices)
        {
            input.FallbackIndices.push_back(rootVertexOffset + index);
        }

        for (uint32_t face = 0; face < faceCount; ++face)
        {
            AssetFormat::CookedMeshClusterGroup group;
            group.BoundsCenter = SphereCenterForTest(faceGroupSpheres[face]);
            group.BoundsRadius = faceGroupSpheres[face].Radius;
            group.Error = BakedCubeDag::FaceError;
            group.ClusterOffset = face * 4;
            group.ClusterCount = 4;
            group.LODLevel = 0;
            input.Groups.push_back(group);
        }
        AssetFormat::CookedMeshClusterGroup rootGroup;
        rootGroup.BoundsCenter = SphereCenterForTest(rootGroupSphere);
        rootGroup.BoundsRadius = rootGroupSphere.Radius;
        rootGroup.Error = BakedCubeDag::RootGroupError;
        rootGroup.ClusterOffset = faceCount * 4;
        rootGroup.ClusterCount = faceCount;
        rootGroup.LODLevel = 1;
        input.Groups.push_back(rootGroup);

        // 書き出して読み戻し、クック済みメッシュを MegaMeshCreateInfo にする（ローダー側の検証も通る）
        Container::VariableArray<uint8_t> bytes;
        const bool bSerialized = AssetFormat::SerializeCookedMeshV1(input, bytes);
        assert(bSerialized);
        dag.Parsed = AssetFormat::ParseCookedMesh(AssetFormat::AssetBlob::CopyBytes(
            Container::Span<const uint8_t>(bytes.data(), bytes.size()), "baked_cube.nvmesh"));
        assert(dag.Parsed.Succeeded());
        assert(dag.Parsed.Mesh.FormatMajor == 1u);
        const bool bAdapted = Mega::BuildMegaMeshCreateInfoFromCookedMesh(dag.Parsed.Mesh, dag.CreateInfo);
        assert(bAdapted);
        assert(dag.CreateInfo.bBakedLODHierarchy);
        dag.CreateInfo.DebugName = "BakedCubeDag";
    }

    // 選んだクラスタの三角形が、位置で溶接した閉じた多様体（すべての辺がちょうど2つの三角形に共有され、向きが釣り合う）か
    bool IsSelectedSurfaceClosed(const BakedCubeDag &dag, const Container::VariableArray<uint32_t> &selectedClusters)
    {
        // 向きのある辺のキー（始点の量子化した位置3つ + 終点の量子化した位置3つ）
        using EdgeKey = Container::FixedArray<int64_t, 6>;
        const AssetFormat::CookedMeshData &mesh = dag.Parsed.Mesh;
        const auto quantize = [&](uint32_t vertexIndex, int64_t (&out)[3])
        {
            const AssetFormat::CookedMeshVertex &vertex = mesh.Vertices[vertexIndex];
            out[0] = static_cast<int64_t>(std::llround(vertex.Position.X * 4096.0));
            out[1] = static_cast<int64_t>(std::llround(vertex.Position.Y * 4096.0));
            out[2] = static_cast<int64_t>(std::llround(vertex.Position.Z * 4096.0));
        };
        Container::Map<EdgeKey, int> directed;
        for (const uint32_t clusterIndex : selectedClusters)
        {
            const auto &cluster = dag.CreateInfo.Clusters[clusterIndex];
            for (uint32_t i = 0; i + 2 < cluster.IndexCount; i += 3)
            {
                int64_t keys[3][3];
                for (uint32_t k = 0; k < 3; ++k)
                {
                    quantize(static_cast<uint32_t>(cluster.VertexOffset) + mesh.Indices[cluster.IndexOffset + i + k],
                             keys[k]);
                }
                for (uint32_t k = 0; k < 3; ++k)
                {
                    const int64_t(&from)[3] = keys[k];
                    const int64_t(&to)[3] = keys[(k + 1) % 3];
                    ++directed[EdgeKey{from[0], from[1], from[2], to[0], to[1], to[2]}];
                }
            }
        }
        for (const auto &entry : directed)
        {
            // 辺ごとに、順方向が1回・逆方向が1回
            const EdgeKey &key = entry.first;
            const auto reverse = directed.find(EdgeKey{key[3], key[4], key[5], key[0], key[1], key[2]});
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
        assert(InitializeWithSmallPool(manager, device));
        const auto handle = manager.MegaGeometry().CreateMegaMesh(dag.CreateInfo);
        assert(handle.IsValid());
        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);

        // 全段のクラスタを1組の頂点・インデックス・クラスタのバッファに置く
        const uint32_t clusterCount = static_cast<uint32_t>(dag.CreateInfo.Clusters.size());
        assert(clusterCount == 31u);
        assert(gpuData->ClusterCount == clusterCount);
        assert(gpuData->VertexCount == dag.Parsed.Mesh.Vertices.size());
        assert(gpuData->IndexCount == dag.Parsed.Mesh.Indices.size());
        // 段の範囲は焼いた3段（クラスタが散らばるので1回の範囲では描けない）と、最も粗い常駐のフォールバックの段
        assert(gpuData->LevelRanges.size() == 4u);
        assert(gpuData->LevelRanges[0].Error == 0.0f);
        assert(gpuData->LevelRanges[1].Error == BakedCubeDag::FaceError);
        assert(gpuData->LevelRanges[2].Error == BakedCubeDag::RootGroupError);
        for (uint32_t level = 0; level < 3u; ++level)
        {
            assert(gpuData->LevelRanges[level].IndexCount == 0u);
        }
        // 影とレイトレーシングは、クックした「フォールバックの段」の範囲をそのまま使う
        assert(dag.CreateInfo.FallbackIndexCount == 36u);
        assert(gpuData->LevelRanges[3].FirstIndex == dag.CreateInfo.FallbackIndexOffset);
        assert(gpuData->LevelRanges[3].IndexCount == dag.CreateInfo.FallbackIndexCount);
        assert(gpuData->LevelRanges[3].Error == dag.CreateInfo.FallbackError);
        assert(gpuData->ShadowLODLevel == 3u);
        assert(gpuData->ShadowFirstIndex == dag.CreateInfo.FallbackIndexOffset);
        assert(gpuData->ShadowIndexCount == dag.CreateInfo.FallbackIndexCount);
        assert(static_cast<uint64_t>(gpuData->ShadowFirstIndex) + gpuData->ShadowIndexCount <= gpuData->IndexCount);
        // どの距離・テクセルでも、影の段はフォールバックのまま（クラスタの段へ細かくならない）
        for (const float texel : {0.001f, 0.1f, 10.0f})
        {
            assert(Mega::SelectShadowLODLevel(*gpuData, 1.0f, texel, 1.0f) == 3u);
        }
        assert(gpuData->VertexBufferBytes == dag.Parsed.Mesh.Vertices.size() * sizeof(Mesh3DVertex));
        assert(gpuData->IndexBufferBytes == dag.Parsed.Mesh.Indices.size() * sizeof(uint32_t));
        assert(gpuData->ClusterBufferBytes == clusterCount * sizeof(Mega::GPUClusterData));
        AssertRegionLayout(*gpuData);
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(std::memcmp(PoolBytesAt(*gpuData, gpuData->VertexBufferOffsetBytes),
                           dag.Parsed.Mesh.Vertices.data(),
                           gpuData->VertexBufferBytes) == 0);
        assert(std::memcmp(PoolBytesAt(*gpuData, gpuData->IndexBufferOffsetBytes),
                           dag.Parsed.Mesh.Indices.data(),
                           gpuData->IndexBufferBytes) == 0);

        Container::VariableArray<Mega::GPUClusterData> uploaded(clusterCount);
        std::memcpy(uploaded.data(),
                    PoolBytesAt(*gpuData, gpuData->ClusterBufferOffsetBytes),
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
            assert(InitializeWithSmallPool(brokenManager, brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }
        {
            Mega::MegaMeshCreateInfo broken = dag.CreateInfo;
            broken.Clusters[5].LODError = 1.0f; // 親の誤差（0.05）より大きい
            RenderResources brokenManager;
            auto brokenDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(brokenManager, brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }

        // 壊れたフォールバックの段（範囲外・三角形の単位でない・頂点の番号が範囲外・誤差が負）も何も作らない
        for (int variant = 0; variant < 4; ++variant)
        {
            Mega::MegaMeshCreateInfo broken = dag.CreateInfo;
            Container::VariableArray<uint32_t> brokenIndices(broken.IndexCount);
            std::memcpy(brokenIndices.data(), broken.IndexData, broken.IndexCount * sizeof(uint32_t));
            if (variant == 0)
            {
                broken.FallbackIndexOffset = broken.IndexCount - 3u; // 範囲が終端を越える
            }
            else if (variant == 1)
            {
                broken.FallbackIndexCount = 35u; // 3の倍数でない
            }
            else if (variant == 2)
            {
                // フォールバックの頂点の番号が頂点の数以上になる（クラスタのインデックスは触らない）
                brokenIndices[broken.FallbackIndexOffset + 5u] = broken.VertexCount;
                broken.IndexData = brokenIndices.data();
            }
            else
            {
                broken.FallbackError = -1.0f;
            }
            RenderResources brokenManager;
            auto brokenDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(brokenManager, brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }

        // フォールバックの段が無い（0件）なら、段は焼いた3段のままで影・RTへは描かない（従来どおり）
        {
            Mega::MegaMeshCreateInfo noFallback = dag.CreateInfo;
            noFallback.FallbackIndexOffset = 0;
            noFallback.FallbackIndexCount = 0;
            noFallback.FallbackError = 0.0f;
            RenderResources noFallbackManager;
            auto noFallbackDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(noFallbackManager, noFallbackDevice));
            const auto noFallbackHandle = noFallbackManager.MegaGeometry().CreateMegaMesh(noFallback);
            assert(noFallbackHandle.IsValid());
            const auto *noFallbackData = noFallbackManager.MegaGeometry().GetMegaMeshGPUData(noFallbackHandle);
            assert(noFallbackData != nullptr);
            assert(noFallbackData->LevelRanges.size() == 3u);
            assert(noFallbackData->ShadowIndexCount == 0u);
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

                Container::VariableArray<uint32_t> selected;
                Container::VariableArray<uint8_t> drawn(clusterCount, static_cast<uint8_t>(0));
                for (uint32_t i = 0; i < clusterCount; ++i)
                {
                    drawn[i] = Mega::ShouldDrawBakedCluster(clusters[i], world, view) ? 1 : 0;
                    if (drawn[i] != 0)
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
                        const int drawnCount = (drawn[leaf] != 0 ? 1 : 0) + (drawn[24u + face] != 0 ? 1 : 0) +
                                               (drawn[rootIndex] != 0 ? 1 : 0);
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

    // ---- グループの BVH ----

    // 4 分木の LOD の階層（5 段・クラスタ 341・グループ 85）。段 0 は 16x16 のクラスタで、2x2 を 1 グループにまとめて簡略化し、
    // その結果（親）が次の段の 1 クラスタになる（自分の球・誤差として、作ったグループの値を持つ。クッカーと同じ）。
    // 段 4 は根 1 つ。グループのメンバは連続して並ぶ。誤差は段ごとに 4 倍。
    struct SyntheticBvhMesh
    {
        AssetFormat::CookedMeshData Cooked;
        MegaGeometry::MegaMeshCreateInfo CreateInfo;
    };

    void BuildSyntheticQuadtreeMesh(SyntheticBvhMesh &mesh)
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        constexpr uint32_t levelCount = 5;
        AssetFormat::CookedMeshData &cooked = mesh.Cooked;
        cooked.FormatMajor = 1;
        cooked.FormatMinor = 1;
        cooked.LODLevelCount = levelCount;

        Container::VariableArray<uint32_t> previousGroupOfCell; // 1 つ下の段のグループの番号（格子）
        uint32_t previousSize = 0;
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const uint32_t size = 16u >> level;
            // 段のクラスタを、2x2 のブロックごとにメンバが連続する順（ブロックは行の順）で作る
            Container::VariableArray<uint32_t> clusterOfCell(static_cast<size_t>(size) * size, 0);
            const uint32_t blockSize = size > 1 ? size / 2 : 1;
            for (uint32_t blockY = 0; blockY < blockSize; ++blockY)
            {
                for (uint32_t blockX = 0; blockX < blockSize; ++blockX)
                {
                    for (uint32_t member = 0; member < (size > 1 ? 4u : 1u); ++member)
                    {
                        const uint32_t cellX = size > 1 ? blockX * 2 + (member % 2) : 0;
                        const uint32_t cellY = size > 1 ? blockY * 2 + (member / 2) : 0;
                        AssetFormat::CookedMeshCluster cluster;
                        cluster.bIsRoot = false;
                        cluster.LODLevel = level;
                        cluster.IndexOffset = 0;
                        cluster.IndexCount = 3;
                        cluster.VertexOffset = 0;
                        cluster.VertexCount = 3;
                        cluster.ConeCutoff = -1.0f;
                        if (level == 0)
                        {
                            const double cell = 1.0;
                            cluster.BoundsCenter = {static_cast<float>((cellX + 0.5) * cell - 8.0),
                                                    static_cast<float>((cellY + 0.5) * cell - 8.0),
                                                    static_cast<float>(((cellX * 7 + cellY * 13) % 5) * 0.2 - 0.4)};
                            cluster.BoundsRadius = 0.75f;
                            cluster.LODError = 0.0f;
                        }
                        else
                        {
                            // 1 つ下の段のグループ（cellX, cellY）の球と誤差
                            const AssetFormat::CookedMeshClusterGroup &born =
                                cooked.Groups[previousGroupOfCell[static_cast<size_t>(cellY) * previousSize + cellX]];
                            cluster.BoundsCenter = born.BoundsCenter;
                            cluster.BoundsRadius = born.BoundsRadius;
                            cluster.LODError = born.Error;
                        }
                        clusterOfCell[static_cast<size_t>(cellY) * size + cellX] =
                            static_cast<uint32_t>(cooked.Clusters.size());
                        cooked.Clusters.push_back(cluster);
                    }
                }
            }

            if (size == 1)
            {
                // 根: 親のグループが無い
                cooked.Clusters.back().bIsRoot = true;
                break;
            }

            // 2x2 のブロックごとに 1 グループ。メンバの球を包む球（中心はメンバの平均）、誤差は段ごとに 4 倍
            previousGroupOfCell.assign(static_cast<size_t>(blockSize) * blockSize, 0);
            previousSize = blockSize;
            for (uint32_t blockY = 0; blockY < blockSize; ++blockY)
            {
                for (uint32_t blockX = 0; blockX < blockSize; ++blockX)
                {
                    const uint32_t groupIndex = static_cast<uint32_t>(cooked.Groups.size());
                    const uint32_t firstMember = clusterOfCell[static_cast<size_t>(blockY * 2) * size + blockX * 2];
                    double center[3] = {0.0, 0.0, 0.0};
                    for (uint32_t member = 0; member < 4; ++member)
                    {
                        const AssetFormat::CookedMeshCluster &c = cooked.Clusters[firstMember + member];
                        center[0] += c.BoundsCenter.X * 0.25;
                        center[1] += c.BoundsCenter.Y * 0.25;
                        center[2] += c.BoundsCenter.Z * 0.25;
                    }
                    double radius = 0.0;
                    for (uint32_t member = 0; member < 4; ++member)
                    {
                        const AssetFormat::CookedMeshCluster &c = cooked.Clusters[firstMember + member];
                        const double dx = c.BoundsCenter.X - center[0];
                        const double dy = c.BoundsCenter.Y - center[1];
                        const double dz = c.BoundsCenter.Z - center[2];
                        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz) + c.BoundsRadius);
                    }
                    AssetFormat::CookedMeshClusterGroup group;
                    group.BoundsCenter = {static_cast<float>(center[0]), static_cast<float>(center[1]),
                                          static_cast<float>(center[2])};
                    group.BoundsRadius = static_cast<float>(radius * 1.001 + 1.0e-4);
                    group.Error = 0.01f * static_cast<float>(1u << (2 * level));
                    group.ClusterOffset = firstMember;
                    group.ClusterCount = 4;
                    group.LODLevel = level;
                    cooked.Groups.push_back(group);
                    previousGroupOfCell[static_cast<size_t>(blockY) * blockSize + blockX] = groupIndex;
                    for (uint32_t member = 0; member < 4; ++member)
                    {
                        AssetFormat::CookedMeshCluster &c = cooked.Clusters[firstMember + member];
                        c.GroupId = groupIndex;
                        c.ParentBoundsCenter = group.BoundsCenter;
                        c.ParentBoundsRadius = group.BoundsRadius;
                        c.ParentError = group.Error;
                    }
                }
            }
        }

        assert(cooked.Clusters.size() == 341);
        assert(cooked.Groups.size() == 85);
        cooked.Vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
        cooked.Vertices.push_back({{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
        cooked.Vertices.push_back({{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
        for (const uint32_t index : {0u, 1u, 2u})
        {
            cooked.Indices.push_back(index);
        }
        cooked.TotalBoundsRadius = 40.0f;

        // グループの BVH をクッカーと同じ関数で作り、読み込みと同じ検査を通す
        const bool bBuilt = AssetFormat::BuildCookedMeshGroupBVH(cooked.Clusters, cooked.Groups, cooked.GroupBVH);
        assert(bBuilt);
        assert(AssetFormat::CheckCookedMeshGroupBVH(cooked.GroupBVH, cooked.Clusters, cooked.GroupBVHLevelNodeCounts) ==
               AssetFormat::CookedMeshParseStatus::Success);
        const bool bAdapted = Mega::BuildMegaMeshCreateInfoFromCookedMesh(cooked, mesh.CreateInfo);
        assert(bAdapted);
        mesh.CreateInfo.DebugName = "SyntheticQuadtree";
    }

    // 向きと位置から視錐台の 6 平面（内向きの法線）を作る
    void BuildFrustumPlanesForTest(const float (&cameraPosition)[3], const float (&forward)[3], const float (&up)[3],
                                   float fovY, float aspect, float (&outPlanes)[6][4])
    {
        const auto cross = [](const float *a, const float *b, float *out)
        {
            out[0] = a[1] * b[2] - a[2] * b[1];
            out[1] = a[2] * b[0] - a[0] * b[2];
            out[2] = a[0] * b[1] - a[1] * b[0];
        };
        const auto normalize = [](float *v)
        {
            const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (int a = 0; a < 3; ++a)
            {
                v[a] /= length;
            }
        };
        float right[3];
        cross(forward, up, right);
        normalize(right);
        float trueUp[3];
        cross(right, forward, trueUp);
        const float halfV = 0.5f * fovY;
        const float halfH = std::atan(std::tan(halfV) * aspect);
        const float normals[4][3] = {
            {right[0] * std::cos(halfH) + forward[0] * std::sin(halfH), right[1] * std::cos(halfH) + forward[1] * std::sin(halfH),
             right[2] * std::cos(halfH) + forward[2] * std::sin(halfH)},
            {-right[0] * std::cos(halfH) + forward[0] * std::sin(halfH), -right[1] * std::cos(halfH) + forward[1] * std::sin(halfH),
             -right[2] * std::cos(halfH) + forward[2] * std::sin(halfH)},
            {trueUp[0] * std::cos(halfV) + forward[0] * std::sin(halfV), trueUp[1] * std::cos(halfV) + forward[1] * std::sin(halfV),
             trueUp[2] * std::cos(halfV) + forward[2] * std::sin(halfV)},
            {-trueUp[0] * std::cos(halfV) + forward[0] * std::sin(halfV), -trueUp[1] * std::cos(halfV) + forward[1] * std::sin(halfV),
             -trueUp[2] * std::cos(halfV) + forward[2] * std::sin(halfV)}};
        for (int plane = 0; plane < 4; ++plane)
        {
            for (int a = 0; a < 3; ++a)
            {
                outPlanes[plane][a] = normals[plane][a];
            }
            outPlanes[plane][3] = -(normals[plane][0] * cameraPosition[0] + normals[plane][1] * cameraPosition[1] +
                                    normals[plane][2] * cameraPosition[2]);
        }
        // 近（0.1 m）と遠（2000 m）
        const float dotCamera = forward[0] * cameraPosition[0] + forward[1] * cameraPosition[1] + forward[2] * cameraPosition[2];
        for (int a = 0; a < 3; ++a)
        {
            outPlanes[4][a] = forward[a];
            outPlanes[5][a] = -forward[a];
        }
        outPlanes[4][3] = -dotCamera - 0.1f;
        outPlanes[5][3] = dotCamera + 2000.0f;
    }

    // BVH をたどった選択が、平らなクラスタの列の選択と一致する（視錐台・遮蔽・LOD の枝の切り方が保守的）。
    // 距離（4〜600 m）・向き・変換・遮蔽の 3 通り（なし・遠方を隠す板・手前の球の陰）の全てで、選ばれる集合が同じ。
    void TestGroupBvhSelectionMatchesFlatSelection()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        SyntheticBvhMesh mesh;
        BuildSyntheticQuadtreeMesh(mesh);
        const auto &clusters = mesh.CreateInfo.Clusters;
        const auto &nodes = mesh.CreateInfo.GroupBVH;
        assert(clusters.size() == 341 && !nodes.empty());

        // アダプタが BVH の節を過不足なく渡している
        assert(nodes.size() == mesh.Cooked.GroupBVH.size());
        Container::VariableArray<uint32_t> levelCounts;
        uint32_t leafCount = 0;
        assert(Mega::AnalyzeGroupBVH(nodes, static_cast<uint32_t>(clusters.size()), levelCounts, leafCount));
        assert(levelCounts.size() == mesh.Cooked.GroupBVHLevelNodeCounts.size() && levelCounts.size() >= 3);
        assert(leafCount == 86); // グループ 85 + 根のクラスタ 1
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            assert(nodes[index].First == mesh.Cooked.GroupBVH[index].First &&
                   nodes[index].Count == mesh.Cooked.GroupBVH[index].Count &&
                   nodes[index].MaxParentError == mesh.Cooked.GroupBVH[index].MaxParentError &&
                   nodes[index].BoundsRadius == mesh.Cooked.GroupBVH[index].BoundsRadius &&
                   ((nodes[index].Flags & Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0) == mesh.Cooked.GroupBVH[index].bLeaf);
        }

        // 2 つの変換（行ベクトル規約）: 一様な伸び 1.5 に回転と並進
        const auto buildWorld = [](float scale, float yaw, float pitch, float tx, float ty, float tz, float (&out)[16])
        {
            const float cy = std::cos(yaw);
            const float sy = std::sin(yaw);
            const float cp = std::cos(pitch);
            const float sp = std::sin(pitch);
            // 回転 = Ry(yaw) * Rx(pitch)（行ベクトル規約の行）
            const float rows[3][3] = {{cy, sy * sp, -sy * cp}, {0.0f, cp, sp}, {sy, -cy * sp, cy * cp}};
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    out[row * 4 + column] = rows[row][column] * scale;
                }
                out[row * 4 + 3] = 0.0f;
            }
            out[12] = tx;
            out[13] = ty;
            out[14] = tz;
            out[15] = 1.0f;
        };
        float worlds[2][16];
        buildWorld(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, worlds[0]);
        buildWorld(1.5f, 0.6f, -0.35f, 3.0f, -2.0f, 5.0f, worlds[1]);

        const float directions[6][3] = {{0.0f, 0.0f, 1.0f},  {1.0f, 0.4f, 0.3f},  {-0.6f, 1.0f, -0.2f},
                                        {0.3f, -0.5f, -1.0f}, {-1.0f, -0.1f, 0.5f}, {0.2f, 1.0f, 0.9f}};
        // 視線の向き: 対象へ向ける・左へ 35° 外す・後ろ向き（全部視錐台の外）
        const float lookYaws[3] = {0.0f, 0.61f, 3.14159265f};

        const auto neverOccluded = [](const float *, float) { return false; };

        uint32_t caseCount = 0;
        uint32_t casesWithFrustumPrune = 0;
        uint32_t casesWithOcclusionPrune = 0;
        uint32_t casesWithLODPrune = 0;
        uint32_t casesWithFewerClusterTests = 0;
        uint32_t casesWithSelection = 0;
        uint32_t farCases = 0;
        uint32_t farCasesFewerThanQuarter = 0;
        uint32_t cutsSeen[5] = {};

        for (const auto &world : worlds)
        {
            const float target[3] = {world[12], world[13], world[14]};
            for (const auto &directionRaw : directions)
            {
                float direction[3] = {directionRaw[0], directionRaw[1], directionRaw[2]};
                const float directionLength = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                                        direction[2] * direction[2]);
                for (float &value : direction)
                {
                    value /= directionLength;
                }
                for (int step = 0; step <= 40; ++step)
                {
                    const float distance = 4.0f * std::pow(600.0f / 4.0f, static_cast<float>(step) / 40.0f);
                    for (const float lookYaw : lookYaws)
                    {
                        Mega::BvhCullView view;
                        const float cameraPosition[3] = {target[0] + direction[0] * distance, target[1] + direction[1] * distance,
                                                         target[2] + direction[2] * distance};
                        // 対象へ向く前方を、上方向の軸まわりに lookYaw 回す
                        float forward[3] = {-direction[0], -direction[1], -direction[2]};
                        const float upAxis[3] = {0.0f, 1.0f, 0.0f};
                        {
                            const float c = std::cos(lookYaw);
                            const float s = std::sin(lookYaw);
                            const float dotUp = forward[0] * upAxis[0] + forward[1] * upAxis[1] + forward[2] * upAxis[2];
                            const float crossUp[3] = {upAxis[1] * forward[2] - upAxis[2] * forward[1],
                                                      upAxis[2] * forward[0] - upAxis[0] * forward[2],
                                                      upAxis[0] * forward[1] - upAxis[1] * forward[0]};
                            float rotated[3];
                            for (int a = 0; a < 3; ++a)
                            {
                                rotated[a] = forward[a] * c + crossUp[a] * s + upAxis[a] * dotUp * (1.0f - c);
                            }
                            for (int a = 0; a < 3; ++a)
                            {
                                forward[a] = rotated[a];
                            }
                        }
                        const float fovY = 1.04719755f; // 縦 60 度
                        const float aspect = 16.0f / 9.0f;
                        const float upHint[3] = {0.0f, 1.0f, 0.1f};
                        BuildFrustumPlanesForTest(cameraPosition, forward, upHint, fovY, aspect, view.FrustumPlanes);
                        for (int a = 0; a < 3; ++a)
                        {
                            view.Lod.CameraPosition[a] = cameraPosition[a];
                            view.Lod.Forward[a] = forward[a];
                        }
                        view.Lod.TanHalfFovY = std::tan(0.5f * fovY);
                        view.Lod.TanHalfFovX = view.Lod.TanHalfFovY * aspect;
                        view.Lod.ProjectionFactor = 1080.0f / (2.0f * view.Lod.TanHalfFovY);
                        view.Lod.LODBias = 1.0f;

                        // 遮蔽: 距離の 0.7 倍より奥を隠す板／カメラと対象の間の球の陰（球の見かけの円の内側で、球より遠い）
                        const float slabDepth = 0.7f * distance;
                        const auto occludedBySlab = [&](const float *center, float radius)
                        {
                            const float toCenter[3] = {center[0] - cameraPosition[0], center[1] - cameraPosition[1],
                                                       center[2] - cameraPosition[2]};
                            return toCenter[0] * forward[0] + toCenter[1] * forward[1] + toCenter[2] * forward[2] - radius > slabDepth;
                        };
                        const float blockerDistance = 0.5f * distance;
                        const float blockerRadius = 0.18f * distance;
                        const auto occludedByBlocker = [&](const float *center, float radius)
                        {
                            const float toCenter[3] = {center[0] - cameraPosition[0], center[1] - cameraPosition[1],
                                                       center[2] - cameraPosition[2]};
                            const float toLength = std::sqrt(toCenter[0] * toCenter[0] + toCenter[1] * toCenter[1] +
                                                             toCenter[2] * toCenter[2]);
                            if (toLength <= radius || toLength - radius <= blockerDistance)
                            {
                                return false;
                            }
                            const float cosAngle = (toCenter[0] * forward[0] + toCenter[1] * forward[1] + toCenter[2] * forward[2]) / toLength;
                            const float angle = std::acos(std::min(std::max(cosAngle, -1.0f), 1.0f));
                            const float blockerAngle = std::asin(blockerRadius / blockerDistance);
                            return angle + std::asin(radius / toLength) <= blockerAngle;
                        };

                        const auto compare = [&](const char *, const auto &occlusion, bool bCountFeatures)
                        {
                            Container::VariableArray<uint32_t> flat;
                            Container::VariableArray<uint32_t> viaBvh;
                            Mega::BvhTraversalStats stats;
                            Mega::SelectClustersFlat(clusters, world, view, occlusion, flat);
                            Mega::SelectClustersByBvh(clusters, nodes, world, view, occlusion, viaBvh, &stats);
                            assert(flat.size() == viaBvh.size());
                            for (size_t index = 0; index < flat.size(); ++index)
                            {
                                assert(flat[index] == viaBvh[index]);
                            }
                            ++caseCount;
                            if (bCountFeatures)
                            {
                                casesWithFrustumPrune += stats.NodesPrunedByFrustum > 0 ? 1 : 0;
                                casesWithLODPrune += stats.NodesPrunedByLOD > 0 ? 1 : 0;
                                casesWithFewerClusterTests += stats.ClustersTested < clusters.size() ? 1 : 0;
                                casesWithSelection += flat.empty() ? 0 : 1;
                                for (const uint32_t index : flat)
                                {
                                    ++cutsSeen[clusters[index].LODLevel];
                                }
                                if (distance >= 200.0f && lookYaw == 0.0f)
                                {
                                    ++farCases;
                                    farCasesFewerThanQuarter += stats.ClustersTested * 4 < clusters.size() ? 1 : 0;
                                }
                            }
                            else
                            {
                                casesWithOcclusionPrune += stats.NodesPrunedByOcclusion > 0 ? 1 : 0;
                            }
                        };
                        compare("none", neverOccluded, true);
                        compare("slab", occludedBySlab, false);
                        compare("blocker", occludedByBlocker, false);
                    }
                }
            }
        }

        // 枝を切る 3 つの条件が実際に働き、BVH の方が判定するクラスタが少ない（試験が何も切らない自明な場合だけでない）
        assert(caseCount == 2u * 6u * 41u * 3u * 3u);
        assert(casesWithFrustumPrune > 0 && casesWithOcclusionPrune > 0 && casesWithLODPrune > 0);
        assert(casesWithFewerClusterTests > caseCount / 9);
        assert(casesWithSelection > 0);
        // 遠くでは、判定するクラスタが平らな列の 1/4 未満になる場合がある
        assert(farCases > 0 && farCasesFewerThanQuarter > 0);
        // 距離で 5 つの段のうち 3 つ以上が選ばれる
        uint32_t levelsSeen = 0;
        for (const uint32_t seen : cutsSeen)
        {
            levelsSeen += seen > 0 ? 1 : 0;
        }
        assert(levelsSeen >= 3);

        // 試験の感度: 枝を切る条件を保守的でなくした BVH（親の誤差の最大と球を小さくする）では、平らな選択と食い違う視点がある
        {
            Container::VariableArray<Mega::GPUGroupBVHNode> broken = nodes;
            for (Mega::GPUGroupBVHNode &node : broken)
            {
                if ((node.Flags & Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF) == 0)
                {
                    node.MaxParentError *= 1.0e-4f;
                    node.BoundsRadius *= 0.25f;
                }
            }
            bool bMismatchDetected = false;
            const auto &world = worlds[1];
            for (int step = 0; step <= 40 && !bMismatchDetected; ++step)
            {
                const float distance = 4.0f * std::pow(600.0f / 4.0f, static_cast<float>(step) / 40.0f);
                Mega::BvhCullView view;
                const float cameraPosition[3] = {world[12], world[13], world[14] - distance};
                const float forward[3] = {0.0f, 0.0f, 1.0f};
                const float upHint[3] = {0.0f, 1.0f, 0.0f};
                BuildFrustumPlanesForTest(cameraPosition, forward, upHint, 1.04719755f, 16.0f / 9.0f, view.FrustumPlanes);
                view.Lod.CameraPosition[0] = cameraPosition[0];
                view.Lod.CameraPosition[1] = cameraPosition[1];
                view.Lod.CameraPosition[2] = cameraPosition[2];
                view.Lod.Forward[2] = 1.0f;
                view.Lod.TanHalfFovY = std::tan(0.5f * 1.04719755f);
                view.Lod.TanHalfFovX = view.Lod.TanHalfFovY * 16.0f / 9.0f;
                view.Lod.ProjectionFactor = 1080.0f / (2.0f * view.Lod.TanHalfFovY);
                view.Lod.LODBias = 1.0f;
                Container::VariableArray<uint32_t> flat;
                Container::VariableArray<uint32_t> viaBroken;
                Mega::SelectClustersFlat(clusters, world, view, neverOccluded, flat);
                Mega::SelectClustersByBvh(clusters, broken, world, view, neverOccluded, viaBroken);
                bMismatchDetected = flat != viaBroken;
            }
            assert(bMismatchDetected);
        }

        // BVH の構造の検査: 子の位置・葉の範囲・クラスタの二重/漏れの拒否
        {
            Container::VariableArray<uint32_t> counts;
            uint32_t leaves = 0;
            Container::VariableArray<Mega::GPUGroupBVHNode> broken = nodes;
            broken[0].First = 2;
            assert(!Mega::AnalyzeGroupBVH(broken, static_cast<uint32_t>(clusters.size()), counts, leaves));
            broken = nodes;
            for (Mega::GPUGroupBVHNode &node : broken)
            {
                if ((node.Flags & Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0)
                {
                    node.First = 0; // 全ての葉が先頭のクラスタを覆う（二重）
                }
            }
            assert(!Mega::AnalyzeGroupBVH(broken, static_cast<uint32_t>(clusters.size()), counts, leaves));
            broken = nodes;
            broken.back().Count = 0;
            assert(!Mega::AnalyzeGroupBVH(broken, static_cast<uint32_t>(clusters.size()), counts, leaves));
            assert(!Mega::AnalyzeGroupBVH(Container::VariableArray<Mega::GPUGroupBVHNode>(), 341u, counts, leaves));
            assert(!Mega::AnalyzeGroupBVH(nodes, 340u, counts, leaves)); // クラスタの数と合わない
        }
    }

    // グループの BVH は、クラスタ・頂点・インデックスの後ろ（同じ区画の末尾）へ、256 バイト整列で置かれてアップロードされる。
    // 壊れた構造・焼き込み済みの階層でないメッシュの BVH は何も作らず拒否する。BVH の無いメッシュの区画は変わらない
    void TestGroupBvhRegionIsUploadedAndValidated()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        SyntheticBvhMesh mesh;
        BuildSyntheticQuadtreeMesh(mesh);

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));
        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(handle.IsValid());
        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        const auto &nodes = mesh.CreateInfo.GroupBVH;
        assert(gpuData->GroupBVHNodeCount == nodes.size());
        assert(gpuData->GroupBVHBufferBytes == nodes.size() * sizeof(Mega::GPUGroupBVHNode));
        assert(gpuData->GroupBVHLeafCount == 86);
        assert(gpuData->GroupBVHLevelNodeCounts.size() == mesh.Cooked.GroupBVHLevelNodeCounts.size());
        uint32_t levelTotal = 0;
        for (size_t level = 0; level < gpuData->GroupBVHLevelNodeCounts.size(); ++level)
        {
            assert(gpuData->GroupBVHLevelNodeCounts[level] == mesh.Cooked.GroupBVHLevelNodeCounts[level]);
            levelTotal += gpuData->GroupBVHLevelNodeCounts[level];
        }
        assert(levelTotal == nodes.size());
        // 区画の末尾（インデックスの後ろ）に、256 バイト整列で、クラスタ・頂点・インデックスの領域と重ならない
        assert(gpuData->GroupBVHBufferOffsetBytes % 256 == 0);
        assert(gpuData->GroupBVHBufferOffsetBytes >= gpuData->IndexBufferOffsetBytes + gpuData->IndexBufferBytes);
        assert(gpuData->GroupBVHBufferOffsetBytes >= gpuData->ClusterBufferOffsetBytes + gpuData->ClusterBufferBytes);
        assert(gpuData->ClusterBuffer.get() == gpuData->VertexBuffer.get());
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(std::memcmp(PoolBytesAt(*gpuData, gpuData->GroupBVHBufferOffsetBytes), nodes.data(),
                           gpuData->GroupBVHBufferBytes) == 0);
        // 頂点・インデックス・クラスタも従来どおり届く
        assert(std::memcmp(PoolBytesAt(*gpuData, gpuData->VertexBufferOffsetBytes), mesh.Cooked.Vertices.data(),
                           gpuData->VertexBufferBytes) == 0);

        // BVH の無いメッシュ（同じクラスタ）は、BVH の領域を持たない（区画は従来のまま）
        {
            Mega::MegaMeshCreateInfo noBvh = mesh.CreateInfo;
            noBvh.GroupBVH.clear();
            RenderResources otherManager;
            auto otherDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(otherManager, otherDevice));
            const auto otherHandle = otherManager.MegaGeometry().CreateMegaMesh(noBvh);
            assert(otherHandle.IsValid());
            const auto *otherData = otherManager.MegaGeometry().GetMegaMeshGPUData(otherHandle);
            assert(otherData != nullptr && otherData->GroupBVHNodeCount == 0 && otherData->GroupBVHBufferBytes == 0 &&
                   otherData->GroupBVHLevelNodeCounts.empty());
        }

        // 壊れた BVH（子の位置が違う・葉が同じクラスタを二重に覆う）／焼き込み済みの階層でないメッシュの BVH は拒否する
        for (int variant = 0; variant < 3; ++variant)
        {
            Mega::MegaMeshCreateInfo broken = mesh.CreateInfo;
            if (variant == 0)
            {
                broken.GroupBVH[0].First = 2;
            }
            else if (variant == 1)
            {
                for (Mega::GPUGroupBVHNode &node : broken.GroupBVH)
                {
                    if ((node.Flags & Mega::GPU_GROUP_BVH_NODE_FLAG_LEAF) != 0)
                    {
                        node.First = 0;
                    }
                }
            }
            else
            {
                broken.bBakedLODHierarchy = false;
            }
            RenderResources brokenManager;
            auto brokenDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(brokenManager, brokenDevice));
            assert(!brokenManager.MegaGeometry().CreateMegaMesh(broken).IsValid());
            assert(brokenDevice->CreatedBufferDescs.empty());
        }
    }

    // ---- ジオメトリのページ（常駐の表・親での描画・要求） ----

    // ページの表: 範囲の割り当て・返却（隣との結合・末尾の縮み）・常駐の切り替え・グローバルな位置からの引き直し
    void TestGeometryPageTableRanges()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        Mega::GeometryPageTable table;
        uint32_t a = 99;
        uint32_t b = 99;
        uint32_t c = 99;
        assert(table.Allocate(10, 4, a) && a == 0);
        assert(table.Allocate(11, 2, b) && b == 4);
        assert(table.Allocate(12, 3, c) && c == 6);
        assert(table.GetSize() == 9);
        uint32_t none = 0;
        assert(!table.Allocate(1, 0, none));

        // 全ページが常駐の区画 0 で始まる。要求の印は 0
        for (const Mega::GeometryPageTable::Entry &entry : table.GetEntries())
        {
            assert(entry.Region == 0 && entry.RequestStamp == 0);
        }
        Mega::GeometryPageTable::Location location;
        assert(table.Resolve(5, location) && location.OwnerId == 11 && location.PageId == 1);
        assert(table.Resolve(8, location) && location.OwnerId == 12 && location.PageId == 2);
        assert(!table.Resolve(9, location));

        // 常駐の切り替えは版を進める。同じ値の設定は進めない。範囲外・未割り当ては拒否する
        uint64_t version = table.GetVersion();
        assert(table.SetRegion(b, 1, Mega::PAGE_NON_RESIDENT));
        assert(table.GetVersion() > version);
        assert(!table.IsResident(b, 1) && table.IsResident(b, 0));
        version = table.GetVersion();
        assert(table.SetRegion(b, 1, Mega::PAGE_NON_RESIDENT));
        assert(table.GetVersion() == version);
        assert(!table.SetRegion(b, 2, 0));  // メッシュのページの数を超える
        assert(!table.SetRegion(5, 0, 0));  // 範囲の先頭ではない
        assert(!table.SetRegion(100, 0, 0));
        assert(!table.IsResident(b, 2));

        // 中間を返すと空きになり、最初に入る空きへ置く。入らない大きさは末尾へ伸ばす
        assert(table.Free(b));
        assert(!table.Free(b));
        assert(!table.Resolve(4, location));
        uint32_t d = 99;
        uint32_t e = 99;
        assert(table.Allocate(13, 2, d) && d == 4);
        assert(table.Resolve(5, location) && location.OwnerId == 13);
        assert(table.Allocate(14, 3, e) && e == 9);
        assert(table.GetSize() == 12);

        // 末尾を返すと表が縮む。前の空きとも結合する
        assert(table.Free(e));
        assert(table.GetSize() == 9);
        assert(table.Free(d));
        assert(table.Free(c));
        assert(table.GetSize() == 4);
        assert(table.Free(a));
        assert(table.GetSize() == 0);
        uint32_t f = 99;
        assert(table.Allocate(15, 5, f) && f == 0 && table.GetSize() == 5);
        // 返却した範囲の中身は次の割り当てで常駐に戻る
        for (const Mega::GeometryPageTable::Entry &entry : table.GetEntries())
        {
            assert(entry.Region == 0);
        }
    }

    // 要求のバッファの読み取り: 件数は容量で頭打ちにし、同じ位置は最新のフレームで 1 件にまとめ、溢れた件数を数える
    void TestGeometryPageRequestSetDecodesBuffers()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        namespace Buffer = Mega::GeometryPageRequestBuffer;
        static_assert(Buffer::GetBufferBytes(4) == (Buffer::HeaderWords + 4) * sizeof(uint32_t));

        // [件数, 溢れ, 容量, 予約, 位置...]
        uint32_t words[Buffer::HeaderWords + 4] = {3, 2, 4, 0, 7, 5, 7, 99};
        Mega::GeometryPageRequestSet set;
        Mega::GeometryPageRequestDecodeResult result = set.AddBuffer(words, 4, 10);
        assert(!result.bInvalid && result.Accepted == 3 && result.Overflow == 2);
        assert(set.GetRequests().size() == 2);
        assert(set.GetRequests()[0].TableIndex == 5 && set.GetRequests()[0].LastRequestedFrame == 10);
        assert(set.GetRequests()[1].TableIndex == 7 && set.GetRequests()[1].LastRequestedFrame == 10);
        assert(set.GetOverflowTotal() == 2 && !set.IsEmpty());

        // GPU は容量を超えた分も件数に足すので、読むのは容量まで（範囲外を読まない）
        words[0] = 1000;
        words[1] = 996;
        Mega::GeometryPageRequestSet clamped;
        result = clamped.AddBuffer(words, 4, 20);
        assert(result.Accepted == 4 && result.Overflow == 996);
        assert(clamped.GetRequests().size() == 3); // 7, 5, 99

        // 新しいフレームの同じ位置は最後に要求されたフレームを進める。古いフレームでは戻さない
        set.Merge(clamped);
        assert(set.GetRequests().size() == 3);
        for (const auto &request : set.GetRequests())
        {
            assert(request.LastRequestedFrame == (request.TableIndex == 99 || request.TableIndex == 5 || request.TableIndex == 7 ? 20u : 0u));
        }
        set.Add(5, 3);
        assert(set.GetRequests()[0].TableIndex == 5 && set.GetRequests()[0].LastRequestedFrame == 20);

        result = set.AddBuffer(nullptr, 4, 1);
        assert(result.bInvalid);
        result = set.AddBuffer(words, 0, 1);
        assert(result.bInvalid);
        set.Clear();
        assert(set.IsEmpty());
    }

    // 1グループ1ページの割り当て: 根のクラスタ（グループ無し）は 0 番、グループ g のメンバは 1 + g 番
    void AssignSyntheticPages(MegaGeometry::MegaMeshCreateInfo &info)
    {
        for (MegaGeometry::MeshCluster &cluster : info.Clusters)
        {
            cluster.PageId = cluster.GroupId == MegaGeometry::INVALID_CLUSTER_GROUP_ID ? 0u : 1u + cluster.GroupId;
        }
    }

    // ページの親子の関係(ChildPageId)は、クラスタを作ったグループのメンバのページを指す。
    // 常駐していない子のページがあるとき、カリングは穴を作らず親を描き、その子のページを要求する。
    // 子のページが全部常駐なら従来の選択と一致し、非常駐の子と親が重ならず、どの葉から根への道も 1 つのクラスタで覆われる。
    void TestPageLinksAndMissingChildFallback()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        SyntheticBvhMesh mesh;
        BuildSyntheticQuadtreeMesh(mesh);
        AssignSyntheticPages(mesh.CreateInfo);
        auto &clusters = mesh.CreateInfo.Clusters;
        const auto &groups = mesh.CreateInfo.ClusterGroups;
        const auto &nodes = mesh.CreateInfo.GroupBVH;
        assert(clusters.size() == 341 && groups.size() == 85);

        Mega::GeometryPageLinkResult links;
        assert(Mega::ApplyGeometryPageLinks(clusters, groups, links));
        assert(links.PageCount == 86 && links.SplitGroups == 0 && links.AmbiguousClusters == 0);
        assert(links.LinkedClusters == 85); // 最も細かい段(256)以外の全クラスタ

        // 各クラスタの親（そのクラスタを含むグループを簡略化した結果のクラスタ）
        Container::VariableArray<uint32_t> parentOf(clusters.size(), 0xFFFFFFFFu);
        for (uint32_t index = 0; index < clusters.size(); ++index)
        {
            const Mega::MeshCluster &cluster = clusters[index];
            if (cluster.GroupId == Mega::INVALID_CLUSTER_GROUP_ID)
            {
                continue;
            }
            const Mega::MeshClusterGroup &group = groups[cluster.GroupId];
            uint32_t found = 0;
            for (uint32_t other = 0; other < clusters.size(); ++other)
            {
                const Mega::MeshCluster &candidate = clusters[other];
                if (candidate.LODLevel == cluster.LODLevel + 1u && candidate.Bounds.CenterX == group.Bounds.CenterX &&
                    candidate.Bounds.CenterY == group.Bounds.CenterY &&
                    candidate.Bounds.CenterZ == group.Bounds.CenterZ && candidate.Bounds.Radius == group.Bounds.Radius &&
                    candidate.LODError == group.Error)
                {
                    parentOf[index] = other;
                    ++found;
                }
            }
            assert(found == 1);
        }
        // 子のページ = 親を作ったグループのメンバのページ。最も細かい段は子のページを持たない
        for (uint32_t index = 0; index < clusters.size(); ++index)
        {
            if (clusters[index].LODLevel == 0)
            {
                assert(clusters[index].ChildPageId == Mega::INVALID_PAGE_ID);
            }
            if (parentOf[index] != 0xFFFFFFFFu)
            {
                assert(clusters[parentOf[index]].ChildPageId == clusters[index].PageId);
            }
        }

        // グループのメンバが複数のページにまたがるとき、そのグループから作ったクラスタの子のページは決めない（穴を作らない側）
        {
            Container::VariableArray<Mega::MeshCluster> split = clusters;
            const Mega::MeshClusterGroup &group = groups[0];
            split[group.ClusterOffset + 1].PageId = 70;
            Mega::GeometryPageLinkResult splitLinks;
            assert(Mega::ApplyGeometryPageLinks(split, groups, splitLinks));
            assert(splitLinks.SplitGroups == 1 && splitLinks.LinkedClusters == 84);
            assert(split[parentOf[group.ClusterOffset]].ChildPageId == Mega::INVALID_PAGE_ID);
            // ページの数が上限を超える番号は拒否する
            split[0].PageId = 1u << 24;
            assert(!Mega::ApplyGeometryPageLinks(split, groups, splitLinks));
        }

        // 葉から根への道ごとに、選ばれたクラスタを数える（ちょうど 1 なら切り口が閉じていて、穴も重なりも無い）
        const auto countPathCoverage = [&](const Container::VariableArray<uint32_t> &selected, uint32_t &outHoles,
                                           uint32_t &outOverlaps)
        {
            Container::VariableArray<uint8_t> isSelected(clusters.size(), 0);
            for (const uint32_t index : selected)
            {
                isSelected[index] = 1;
            }
            outHoles = 0;
            outOverlaps = 0;
            for (uint32_t leaf = 0; leaf < clusters.size(); ++leaf)
            {
                if (clusters[leaf].LODLevel != 0)
                {
                    continue;
                }
                uint32_t count = 0;
                for (uint32_t current = leaf; current != 0xFFFFFFFFu; current = parentOf[current])
                {
                    count += isSelected[current];
                }
                outHoles += count == 0 ? 1 : 0;
                outOverlaps += count > 1 ? 1 : 0;
            }
        };

        // 視錐台は全てを通し、遮蔽は無し
        Mega::BvhCullView baseView;
        for (auto &plane : baseView.FrustumPlanes)
        {
            plane[0] = plane[1] = plane[2] = 0.0f;
            plane[3] = 1.0f;
        }
        const float fovY = 1.04719755f;
        const float aspect = 16.0f / 9.0f;
        baseView.Lod.TanHalfFovY = std::tan(0.5f * fovY);
        baseView.Lod.TanHalfFovX = baseView.Lod.TanHalfFovY * aspect;
        baseView.Lod.ProjectionFactor = 1080.0f / (2.0f * baseView.Lod.TanHalfFovY);
        baseView.Lod.LODBias = 1.0f;
        const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        const auto neverOccluded = [](const float *, float) { return false; };

        uint32_t seed = 12345u;
        const auto nextRandom = [&seed]() -> uint32_t
        {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 8;
        };

        uint32_t caseCount = 0;
        uint32_t casesWithRequests = 0;
        uint32_t casesWithFallbackSelection = 0;
        uint32_t casesWhereNaiveHasHoles = 0;
        uint32_t coarseSelections = 0;
        for (uint32_t trial = 0; trial < 24; ++trial)
        {
            // 非常駐のページ: 乱数で選び、「非常駐のページのクラスタの子のページも非常駐」になるまで広げる
            // （ストリーマは子が常駐していれば親も常駐させ、外すときは子から外す）。0 番（根）は常駐のまま
            Container::VariableArray<uint8_t> nonResident(links.PageCount, 0);
            const uint32_t probabilityPercent = trial == 0 ? 0u : (trial % 4 == 3 ? 90u : 10u + (trial % 4) * 25u);
            for (uint32_t page = 1; page < links.PageCount; ++page)
            {
                nonResident[page] = (nextRandom() % 100u) < probabilityPercent ? 1 : 0;
            }
            for (bool bChanged = true; bChanged;)
            {
                bChanged = false;
                for (const Mega::MeshCluster &cluster : clusters)
                {
                    if (nonResident[cluster.PageId] != 0 && cluster.ChildPageId != Mega::INVALID_PAGE_ID &&
                        nonResident[cluster.ChildPageId] == 0)
                    {
                        nonResident[cluster.ChildPageId] = 1;
                        bChanged = true;
                    }
                }
            }
            assert(nonResident[0] == 0);
            const auto isResident = [&nonResident](uint32_t page) { return nonResident[page] == 0; };

            for (int step = 0; step <= 24; ++step)
            {
                const float distance = 4.0f * std::pow(600.0f / 4.0f, static_cast<float>(step) / 24.0f);
                Mega::BvhCullView view = baseView;
                view.Lod.CameraPosition[0] = (step % 2 == 0) ? 0.0f : 25.0f;
                view.Lod.CameraPosition[1] = (step % 2 == 0) ? 0.0f : 12.0f;
                view.Lod.CameraPosition[2] = -distance;
                float forward[3] = {-view.Lod.CameraPosition[0], -view.Lod.CameraPosition[1], -view.Lod.CameraPosition[2]};
                const float forwardLength = std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
                for (int a = 0; a < 3; ++a)
                {
                    view.Lod.Forward[a] = forward[a] / forwardLength;
                }

                Container::VariableArray<uint32_t> allResident;
                Mega::SelectClustersFlat(clusters, world, view, neverOccluded, allResident);

                Container::VariableArray<uint32_t> flat;
                Container::VariableArray<uint32_t> flatRequests;
                Mega::SelectClustersFlatPaged(clusters, world, view, neverOccluded, isResident, flat, flatRequests);
                Container::VariableArray<uint32_t> viaBvh;
                Container::VariableArray<uint32_t> bvhRequests;
                Mega::SelectClustersByBvhPaged(clusters, nodes, world, view, neverOccluded, isResident, viaBvh,
                                               bvhRequests);
                ++caseCount;

                // BVH をたどっても、平らな列でも、選択も要求も同じ
                assert(flat == viaBvh);
                assert(flatRequests == bvhRequests);

                // 常駐していないページのクラスタは描かない
                for (const uint32_t index : flat)
                {
                    assert(nonResident[clusters[index].PageId] == 0);
                }

                // 要求 = 自分の誤差では粗すぎるのに描かれたクラスタの、子のページ（全て非常駐）。重複なし・昇順
                Container::VariableArray<uint32_t> expectedRequests;
                for (const uint32_t index : flat)
                {
                    if (!Mega::IsBakedClusterWithinError(clusters[index], world, view.Lod))
                    {
                        const uint32_t page = clusters[index].ChildPageId;
                        assert(page != Mega::INVALID_PAGE_ID && nonResident[page] != 0);
                        if (std::find(expectedRequests.begin(), expectedRequests.end(), page) == expectedRequests.end())
                        {
                            expectedRequests.push_back(page);
                        }
                    }
                }
                std::sort(expectedRequests.begin(), expectedRequests.end());
                assert(flatRequests == expectedRequests);
                for (const uint32_t page : flatRequests)
                {
                    assert(nonResident[page] != 0);
                }

                // 切り口が閉じている: 穴も重なりも無い
                uint32_t holes = 0;
                uint32_t overlaps = 0;
                countPathCoverage(flat, holes, overlaps);
                assert(holes == 0 && overlaps == 0);
                countPathCoverage(allResident, holes, overlaps);
                assert(holes == 0 && overlaps == 0);

                // 非常駐が無ければ従来の選択と一致し、要求も無い
                bool bAnyNonResident = false;
                for (const uint8_t flag : nonResident)
                {
                    bAnyNonResident = bAnyNonResident || flag != 0;
                }
                if (!bAnyNonResident)
                {
                    assert(flat == allResident && flatRequests.empty());
                }
                casesWithRequests += flatRequests.empty() ? 0 : 1;
                casesWithFallbackSelection += flat != allResident ? 1 : 0;
                for (const uint32_t index : flat)
                {
                    coarseSelections += clusters[index].LODLevel > 0 ? 1 : 0;
                }

                // 試験の感度: 親で描かずに、非常駐のクラスタを落とすだけでは、穴ができる視点がある
                Container::VariableArray<uint32_t> naive;
                for (const uint32_t index : allResident)
                {
                    if (nonResident[clusters[index].PageId] == 0)
                    {
                        naive.push_back(index);
                    }
                }
                countPathCoverage(naive, holes, overlaps);
                casesWhereNaiveHasHoles += holes > 0 ? 1 : 0;
            }
        }
        assert(caseCount == 24u * 25u);
        // 要求が書かれる視点・親で描き直す視点・親で描いたクラスタが、実際にある
        assert(casesWithRequests > 20);
        assert(casesWithFallbackSelection > 20);
        assert(coarseSelections > 0);
        assert(casesWhereNaiveHasHoles > 20);
    }

    // ストアは、メッシュごとにページの表の範囲を割り当て、クラスタの記録に PageId と ChildPageId を載せ、
    // 常駐の切り替えを表の版に反映し、メッシュの解放で範囲を返す
    void TestStoreAllocatesPageTableRangesAndUploadsPageLinks()
    {
        namespace Mega = NorvesLib::Core::Rendering::MegaGeometry;
        SyntheticBvhMesh mesh;
        BuildSyntheticQuadtreeMesh(mesh);
        AssignSyntheticPages(mesh.CreateInfo);

        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));
        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(handle.IsValid());
        const auto *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        assert(gpuData->PageCount == 86 && gpuData->PageTableBase == 0);

        // ページを2つ以上持つメッシュを作ったので、要求のバッファ（3つ）が確保されている
        const auto countRequestBuffers = [](const Container::TSharedPtr<FakeDevice> &target)
        {
            size_t count = 0;
            for (const auto &desc : target->CreatedBufferDescs)
            {
                count += (desc.DebugName != nullptr && std::strcmp(desc.DebugName, "GeometryPageRequest") == 0) ? 1 : 0;
            }
            return count;
        };
        assert(countRequestBuffers(device) == 3);
        // 1ページのメッシュだけなら、要求のバッファは確保しない
        {
            RenderResources singleManager;
            auto singleDevice = MakeShared<FakeDevice>();
            assert(InitializeWithSmallPool(singleManager, singleDevice));
            MeshFixture single("PageSingle");
            assert(singleManager.MegaGeometry().CreateMegaMesh(single.CreateInfo).IsValid());
            assert(countRequestBuffers(singleDevice) == 0);
        }

        // アップロードされたクラスタの記録: PageId は渡した値、ChildPageId はグループの表から求めた値
        Container::VariableArray<Mega::MeshCluster> expected = mesh.CreateInfo.Clusters;
        Mega::GeometryPageLinkResult links;
        assert(Mega::ApplyGeometryPageLinks(expected, mesh.CreateInfo.ClusterGroups, links));
        assert(GeometryUpload::DrainGeometryUploads(manager));
        const auto *uploaded =
            reinterpret_cast<const Mega::GPUClusterData *>(PoolBytesAt(*gpuData, gpuData->ClusterBufferOffsetBytes));
        for (size_t index = 0; index < expected.size(); ++index)
        {
            assert(uploaded[index].PageId == expected[index].PageId);
            assert(uploaded[index].ChildPageId == expected[index].ChildPageId);
        }

        // 表の写し: 最初は全ページが常駐。版が同じなら写さない
        uint64_t version = ~0ull;
        Container::VariableArray<Mega::GeometryPageTable::Entry> entries;
        assert(manager.MegaGeometry().CopyPageTableIfChanged(version, entries));
        assert(entries.size() == 86);
        for (const auto &entry : entries)
        {
            assert(entry.Region == 0);
        }
        assert(!manager.MegaGeometry().CopyPageTableIfChanged(version, entries));

        // 常駐の切り替え
        assert(manager.MegaGeometry().SetMegaMeshPageRegion(handle, 5, Mega::PAGE_NON_RESIDENT));
        assert(manager.MegaGeometry().CopyPageTableIfChanged(version, entries));
        assert(entries[gpuData->PageTableBase + 5].Region == Mega::PAGE_NON_RESIDENT);
        assert(entries[gpuData->PageTableBase + 4].Region == 0);
        assert(!manager.MegaGeometry().SetMegaMeshPageRegion(handle, 86, 0));
        assert(!manager.MegaGeometry().SetMegaMeshPageRegion(MakeMegaMeshHandle(999999), 0, 0));

        // 要求の位置から、メッシュとページを引き直す
        uint64_t meshId = 0;
        uint32_t pageId = 0;
        assert(manager.MegaGeometry().ResolvePageTableIndex(gpuData->PageTableBase + 5, meshId, pageId));
        assert(meshId == handle.Id && pageId == 5);
        assert(!manager.MegaGeometry().ResolvePageTableIndex(86, meshId, pageId));

        // 1ページのメッシュは表を 1 つ使う。解放すると範囲が返り、次のメッシュが先頭を再利用する
        MeshFixture smallMesh("PageTableSmall");
        const auto smallHandle = manager.MegaGeometry().CreateMegaMesh(smallMesh.CreateInfo);
        assert(smallHandle.IsValid());
        const auto *smallData = manager.MegaGeometry().GetMegaMeshGPUData(smallHandle);
        assert(smallData != nullptr && smallData->PageCount == 1 && smallData->PageTableBase == 86);
        manager.MegaGeometry().ReleaseMegaMesh(handle);
        assert(manager.MegaGeometry().CopyPageTableIfChanged(version, entries));
        assert(!manager.MegaGeometry().ResolvePageTableIndex(5, meshId, pageId));
        MeshFixture another("PageTableReuse");
        const auto anotherHandle = manager.MegaGeometry().CreateMegaMesh(another.CreateInfo);
        assert(anotherHandle.IsValid());
        const auto *anotherData = manager.MegaGeometry().GetMegaMeshGPUData(anotherHandle);
        assert(anotherData != nullptr && anotherData->PageTableBase == 0 && anotherData->PageCount == 1);
        assert(manager.MegaGeometry().CopyPageTableIfChanged(version, entries));
        assert(entries.size() == 87 && entries[0].Region == 0);
    }

    void TestSharedHandleCounter()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

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
        assert(InitializeWithSmallPool(manager, device));
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
        assert(InitializeWithSmallPool(manager, device));

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
        assert(InitializeWithSmallPool(manager, device));

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
        assert(device->CreatedBufferDescs.size() == 1);
        const MegaGeometry::MegaMeshGPUData *stagedData = manager.MegaGeometry().GetMegaMeshGPUData(megaMesh);
        assert(stagedData->VertexBufferBytes == expectedVertexBytes);
        assert(stagedData->IndexBufferBytes == expectedIndexBytes);
        assert(stagedData->ClusterBufferBytes == expectedClusterBytes);
        AssertRegionLayout(*stagedData);
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(std::memcmp(PoolBytesAt(*stagedData, stagedData->VertexBufferOffsetBytes),
                           staging.Vertices.data(),
                           expectedVertexBytes) == 0);
        assert(std::memcmp(PoolBytesAt(*stagedData, stagedData->IndexBufferOffsetBytes),
                           staging.ClusterizedIndices.data(),
                           expectedIndexBytes) == 0);

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
        assert(InitializeWithSmallPool(manager, device));
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
        assert(device->CreatedBufferDescs.size() == 1);
        assert(gpuData->VertexBufferBytes == 3 * sizeof(Mesh3DVertex));
        assert(gpuData->IndexBufferBytes == 3 * sizeof(uint32_t));
        assert(gpuData->ClusterBufferBytes == sizeof(MegaGeometry::GPUClusterData));
        AssertRegionLayout(*gpuData);
        assert(GeometryUpload::DrainGeometryUploads(manager));

        MegaGeometry::GPUClusterData uploadedCluster{};
        std::memcpy(&uploadedCluster,
                    PoolBytesAt(*gpuData, gpuData->ClusterBufferOffsetBytes),
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
        assert(InitializeWithSmallPool(manager, device));
        const size_t bufferDescCount = device->CreatedBufferDescs.size();
        const size_t bufferCount = device->CreatedBuffers.size();
        const ModelHandle model = manager.MegaGeometry().LoadModel(assetSystem, "Models/Triangle.nvmesh");
        assert(!model.IsValid());
        assert(device->CreatedBufferDescs.size() == bufferDescCount);
        assert(device->CreatedBuffers.size() == bufferCount);
        std::filesystem::remove_all(root);
    }

    // 複数のメッシュは同じプールの塊を共有し、領域は重ならず、中身は混ざらない
    void TestMeshesShareOnePoolBlockWithoutOverlap()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

        MeshFixture meshA("PoolShareA");
        MeshFixture meshB("PoolShareB");
        MeshFixture meshC("PoolShareC");
        MeshFixture *meshes[3] = {&meshA, &meshB, &meshC};
        MegaGeometry::MegaMeshHandle handles[3];
        const MegaGeometry::MegaMeshGPUData *data[3] = {};
        for (int i = 0; i < 3; ++i)
        {
            meshes[i]->Vertices[0] = static_cast<float>(10 + i);
            handles[i] = manager.MegaGeometry().CreateMegaMesh(meshes[i]->CreateInfo);
            assert(handles[i].IsValid());
            data[i] = manager.MegaGeometry().GetMegaMeshGPUData(handles[i]);
            assert(data[i] != nullptr);
            AssertRegionLayout(*data[i]);
            assert(data[i]->VertexBuffer.get() == data[0]->VertexBuffer.get());
        }
        // メッシュごとのバッファは作らない
        assert(device->CreatedBufferDescs.size() == 1);

        for (int i = 0; i < 3; ++i)
        {
            for (int j = i + 1; j < 3; ++j)
            {
                const uint64_t beginI = data[i]->ClusterBufferOffsetBytes;
                const uint64_t endI = data[i]->IndexBufferOffsetBytes + data[i]->IndexBufferBytes;
                const uint64_t beginJ = data[j]->ClusterBufferOffsetBytes;
                const uint64_t endJ = data[j]->IndexBufferOffsetBytes + data[j]->IndexBufferBytes;
                assert(endI <= beginJ || endJ <= beginI);
            }
        }

        assert(GeometryUpload::DrainGeometryUploads(manager));
        for (int i = 0; i < 3; ++i)
        {
            assert(std::memcmp(PoolBytesAt(*data[i], data[i]->VertexBufferOffsetBytes),
                               meshes[i]->Vertices,
                               sizeof(MeshFixture::Vertices)) == 0);
            assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handles[i]) == data[i]);
        }
        assert(manager.GetGeometryPool()->GetStats().AllocationCount == 3);
    }

    // 解放した区画は、解放を頼んだ時点で最後に提出したフレームの完了まで空きへ戻らない。
    // 戻った区画は次のメッシュが使い、そのメッシュの中身は前のメッシュの書き込みに汚されない
    void TestReleaseReturnsRegionAfterSubmissionAndReusesIt()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

        MeshFixture meshA("ReuseA");
        meshA.Vertices[0] = 111.0f;
        const auto handleA = manager.MegaGeometry().CreateMegaMesh(meshA.CreateInfo);
        assert(handleA.IsValid());
        const uint64_t regionOffsetA = manager.MegaGeometry().GetMegaMeshGPUData(handleA)->ClusterBufferOffsetBytes;

        uint32_t frames = 0;
        assert(GeometryUpload::DrainGeometryUploads(manager, 512, &frames));
        const GeometryPoolStats usedStats = manager.GetGeometryPool()->GetStats();
        assert(usedStats.UsedBytes > 0);
        assert(usedStats.AllocationCount == 1);

        // 提出したがまだ完了していないフレームがあるときに解放する
        GeometryUpload::CopyingCommandList commandList;
        uint64_t serial = frames;
        manager.BeginRetireFrame(serial);
        manager.RecordTileUploads(commandList);
        ++serial;
        manager.CommitRetireFrame(serial);
        manager.MegaGeometry().ReleaseMegaMesh(handleA);
        assert(manager.MegaGeometry().GetMegaMeshGPUData(handleA) == nullptr);
        assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handleA) == nullptr);
        assert(manager.GetGeometryPool()->GetStats().UsedBytes == usedStats.UsedBytes);

        manager.BeginRetireFrame(serial);
        const GeometryPoolStats freedStats = manager.GetGeometryPool()->GetStats();
        assert(freedStats.UsedBytes == 0);
        assert(freedStats.AllocationCount == 0);

        // 同じ大きさのメッシュは同じ位置の区画を使い、書き込みの完了までは GPU へ渡らない
        MeshFixture meshB("ReuseB");
        meshB.Vertices[0] = 222.0f;
        const auto handleB = manager.MegaGeometry().CreateMegaMesh(meshB.CreateInfo);
        assert(handleB.IsValid());
        const MegaGeometry::MegaMeshGPUData *dataB = manager.MegaGeometry().GetMegaMeshGPUData(handleB);
        assert(dataB != nullptr);
        assert(dataB->ClusterBufferOffsetBytes == regionOffsetA);
        assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handleB) == nullptr);
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(manager.MegaGeometry().GetReadyMegaMeshGPUData(handleB) == dataB);
        assert(std::memcmp(PoolBytesAt(*dataB, dataB->VertexBufferOffsetBytes),
                           meshB.Vertices,
                           sizeof(MeshFixture::Vertices)) == 0);
    }

    // レイトレーシングのスナップショット（FramePacket）が区画の持ち主を持っている間は、元のメッシュを解放しても
    // 区画が別のメッシュへ使い回されない。最後の参照が消え、提出の完了を待ってから空きへ戻る
    void TestSnapshotOwnerKeepsReleasedRegionFromReuse()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

        MeshFixture meshA("SnapshotA");
        meshA.Vertices[0] = 111.0f;
        const auto handleA = manager.MegaGeometry().CreateMegaMesh(meshA.CreateInfo);
        assert(handleA.IsValid());
        assert(GeometryUpload::DrainGeometryUploads(manager));

        // スナップショットが複製して持つ GPU データ（区画の持ち主を含む）
        MegaGeometry::MegaMeshGPUData snapshotData = *manager.MegaGeometry().GetReadyMegaMeshGPUData(handleA);
        assert(snapshotData.RegionOwner != nullptr);
        const uint64_t regionOffsetA = snapshotData.ClusterBufferOffsetBytes;

        // 元のメッシュを解放しても、スナップショットが持っている間は区画が使われたまま
        manager.MegaGeometry().ReleaseMegaMesh(handleA);
        assert(manager.MegaGeometry().GetMegaMeshGPUData(handleA) == nullptr);
        assert(manager.GetGeometryPool()->GetStats().AllocationCount == 1);

        // 同じ大きさの別のメッシュは、保持中の区画ではなく別の区画へ置かれ、スナップショットの中身を汚さない
        MeshFixture meshB("SnapshotB");
        meshB.Vertices[0] = 222.0f;
        const auto handleB = manager.MegaGeometry().CreateMegaMesh(meshB.CreateInfo);
        assert(handleB.IsValid());
        const MegaGeometry::MegaMeshGPUData *dataB = manager.MegaGeometry().GetMegaMeshGPUData(handleB);
        assert(dataB != nullptr);
        assert(dataB->ClusterBufferOffsetBytes != regionOffsetA);
        assert(GeometryUpload::DrainGeometryUploads(manager));
        assert(manager.GetGeometryPool()->GetStats().AllocationCount == 2);
        assert(std::memcmp(PoolBytesAt(snapshotData, snapshotData.VertexBufferOffsetBytes),
                           meshA.Vertices,
                           sizeof(MeshFixture::Vertices)) == 0);

        // 最後の参照が消えても、記録中のフレームの提出が完了するまでは空きへ戻らない
        snapshotData.RegionOwner.reset();
        assert(manager.GetGeometryPool()->GetStats().AllocationCount == 2);

        GeometryUpload::CopyingCommandList commandList;
        uint64_t serial = 1000;
        GeometryUpload::StepFrame(manager, commandList, serial);
        GeometryUpload::StepFrame(manager, commandList, serial);
        assert(manager.GetGeometryPool()->GetStats().AllocationCount == 1);

        // 戻った区画は次のメッシュが使う
        MeshFixture meshC("SnapshotC");
        meshC.Vertices[0] = 333.0f;
        const auto handleC = manager.MegaGeometry().CreateMegaMesh(meshC.CreateInfo);
        assert(handleC.IsValid());
        assert(manager.MegaGeometry().GetMegaMeshGPUData(handleC)->ClusterBufferOffsetBytes == regionOffsetA);
    }

    // プールの塊より大きいメッシュと、1フレームの上限を超えるメッシュは、チャンクに分けて数フレームで書かれる
    void TestLargeMeshUploadsInChunksAcrossFrames()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

        // 頂点 12 MiB（float4 × 786432）。塊（1 MiB）より大きいので、その大きさの塊を足す
        constexpr size_t VertexCount = 786432;
        Container::VariableArray<float> vertices(VertexCount * 4);
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            vertices[i] = static_cast<float>(i % 9973);
        }
        MeshFixture mesh("LargeChunked");
        mesh.CreateInfo.VertexData = vertices.data();
        mesh.CreateInfo.VertexDataSize = vertices.size() * sizeof(float);
        mesh.CreateInfo.VertexCount = static_cast<uint32_t>(VertexCount);

        const auto handle = manager.MegaGeometry().CreateMegaMesh(mesh.CreateInfo);
        assert(handle.IsValid());
        const MegaGeometry::MegaMeshGPUData *gpuData = manager.MegaGeometry().GetMegaMeshGPUData(handle);
        assert(gpuData != nullptr);
        assert(gpuData->VertexBufferBytes == vertices.size() * sizeof(float));
        assert(gpuData->VertexBuffer->GetSize() > TestPoolBlockBytes);

        uint32_t frames = 0;
        GeometryUpload::CopyingCommandList commandList;
        assert(GeometryUpload::DrainGeometryUploads(manager, 512, &frames, &commandList));
        // 1フレームの上限（8 MiB）を超えるので複数のフレームに分かれ、チャンク（4 MiB）ごとにコピーされる
        assert(frames >= 2);
        assert(commandList.CopyBufferCount >= 3);
        assert(commandList.CopiedBytes >= gpuData->VertexBufferBytes);
        assert(std::memcmp(PoolBytesAt(*gpuData, gpuData->VertexBufferOffsetBytes),
                           vertices.data(),
                           gpuData->VertexBufferBytes) == 0);
    }

    void TestReleaseClearShutdown()
    {
        RenderResources manager;
        auto device = MakeShared<FakeDevice>();
        assert(InitializeWithSmallPool(manager, device));

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
    TestGroupBvhSelectionMatchesFlatSelection();
    TestGroupBvhRegionIsUploadedAndValidated();
    TestGeometryPageTableRanges();
    TestGeometryPageRequestSetDecodesBuffers();
    TestPageLinksAndMissingChildFallback();
    TestStoreAllocatesPageTableRangesAndUploadsPageLinks();
    TestSharedHandleCounter();
    // バッファの作成はプールの塊の1回だけ（頂点・インデックス・クラスタのバッファは作らない）
    TestCreateFailureDoesNotRegister(1);
    TestModelRegisterAndReleaseCoupledMegaMesh();
    TestFinalizeModelStagingNoTextureSuccess();
    TestBuildModelStagingMapsCookedDataAndOwnsStrings();
    TestLoadCookedModelThroughPublicResources();
    TestCorruptCookedModelCreatesNoBuffers();
    TestMeshesShareOnePoolBlockWithoutOverlap();
    TestReleaseReturnsRegionAfterSubmissionAndReusesIt();
    TestSnapshotOwnerKeepsReleasedRegionFromReuse();
    TestLargeMeshUploadsInChunksAcrossFrames();
    TestReleaseClearShutdown();

    std::cout << "MegaGeometryResourcesTest passed\n";
    return 0;
}
