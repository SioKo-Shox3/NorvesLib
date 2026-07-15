#include "Asset/AssetSystem.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Library/Core/Private/Resource/ModelAssetLoader.h"
#include "Library/Core/Private/Resource/ModelStaging.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
    TestSuccessfulNoLodUpload();
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
