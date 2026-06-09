#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

    void AssertNoLodUploadBuffers(const FakeDevice &device)
    {
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

    std::cout << "RenderResourceManagerMegaGeometryStoreTest start\n";

    TestCreateBeforeInitialize();
    TestInvalidCreateInfoCreatesNoBuffers();
    TestSuccessfulNoLodUpload();
    TestSharedHandleCounter();
    TestCreateFailureDoesNotRegister(1);
    TestCreateFailureDoesNotRegister(2);
    TestCreateFailureDoesNotRegister(3);
    TestModelRegisterAndReleaseCoupledMegaMesh();
    TestReleaseClearShutdown();

    std::cout << "RenderResourceManagerMegaGeometryStoreTest passed\n";
    return 0;
}
