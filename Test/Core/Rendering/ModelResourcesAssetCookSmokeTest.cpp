#include "Asset/AssetSystem.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Thread/JobSystem.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace Rendering = NorvesLib::Core::Rendering;

namespace
{
    struct Options
    {
        std::filesystem::path AssetRoot;
        std::filesystem::path ManifestPath;
        std::filesystem::path PackagePath;
        std::string LogicalPath;
        std::string EntryName;
    };

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    Container::String ToCoreString(const std::string& text)
    {
#if defined(UNICODE)
        std::wstring wide;
        wide.reserve(text.size());
        for (char character : text)
        {
            wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
        }
        return Container::String(wide.c_str());
#else
        return Container::String(text.c_str());
#endif
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(input.is_open(), "manifest file must open");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    Options ParseOptions(int argc, char** argv)
    {
        Options options;
        for (int index = 1; index + 1 < argc; index += 2)
        {
            const std::string name = argv[index];
            const std::string value = argv[index + 1];
            if (name == "--asset-root")
            {
                options.AssetRoot = value;
            }
            else if (name == "--manifest")
            {
                options.ManifestPath = value;
            }
            else if (name == "--package")
            {
                options.PackagePath = value;
            }
            else if (name == "--logical")
            {
                options.LogicalPath = value;
            }
            else if (name == "--entry")
            {
                options.EntryName = value;
            }
            else
            {
                throw std::runtime_error("unknown command-line option: " + name);
            }
        }

        Require(argc == 11, "exactly five option/value pairs are required");
        Require(!options.AssetRoot.empty(), "asset root is required");
        Require(!options.ManifestPath.empty(), "manifest path is required");
        Require(!options.PackagePath.empty(), "package path is required");
        Require(!options.LogicalPath.empty(), "logical path is required");
        Require(!options.EntryName.empty(), "entry name is required");
        return options;
    }

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc& desc)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size))
        {
            ++LiveCount;
        }

        ~FakeBuffer() override
        {
            --LiveCount;
        }

        uint64_t GetSize() const override
        {
            return Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            if (data != nullptr && offset + size <= Bytes.size())
            {
                std::memcpy(Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
            }
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }

        static std::atomic<int> LiveCount;
        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
    };

    std::atomic<int> FakeBuffer::LiveCount{0};

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc& desc) override
        {
            CreatedBufferDescs.push_back(desc);
            Container::TSharedPtr<FakeBuffer> buffer = Container::MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(buffer);
            return buffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc&) override { return {}; }
        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc&) override { return {}; }
        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc&) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc&) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc&) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc&) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc&) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc&) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc&) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        void DropObservedBuffers()
        {
            CreatedBuffers.clear();
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        std::vector<NorvesLib::RHI::BufferDesc> CreatedBufferDescs;
        std::vector<Container::TWeakPtr<FakeBuffer>> CreatedBuffers;
    };

    class RuntimeSession final
    {
    public:
        RuntimeSession()
        {
            NorvesLib::Thread::JobSystem::Get().Initialize(
                2,
                NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);
            m_bJobSystemInitialized = true;
        }

        ~RuntimeSession()
        {
            if (Model.IsValid())
            {
                Resources.MegaGeometry().ReleaseModel(Model);
            }
            Resources.Shutdown();
            AssetSystem.reset();
            if (m_bJobSystemInitialized)
            {
                NorvesLib::Thread::JobSystem::Get().WaitForAll();
                NorvesLib::Thread::JobSystem::Get().Shutdown();
            }
        }

        bool m_bJobSystemInitialized = false;
        Rendering::RenderResources Resources;
        Container::TSharedPtr<FakeDevice> Device;
        Container::TSharedPtr<Asset::AssetSystem> AssetSystem;
        Rendering::ModelHandle Model = Rendering::ModelHandle::Invalid();
    };

    void RunSmoke(const Options& options)
    {
        Require(std::filesystem::exists(options.AssetRoot), "asset root must exist");
        Require(std::filesystem::exists(options.ManifestPath), "manifest must exist");
        Require(std::filesystem::exists(options.PackagePath), "package must exist");
        Require(!std::filesystem::exists(options.AssetRoot / std::filesystem::path(options.LogicalPath)),
                "runtime root must not contain a loose logical model file");

        const std::string manifestText = ReadTextFile(options.ManifestPath);
        Require(manifestText.find(options.EntryName) != std::string::npos,
                "manifest must reference the cooked model entry");

        RuntimeSession session;
        session.Device = Container::MakeShared<FakeDevice>();
        session.AssetSystem = Container::MakeShared<Asset::AssetSystem>(options.AssetRoot.generic_string().c_str());
        Require(session.AssetSystem->LoadManifestFromJsonText(ToCoreString(manifestText)),
                "model manifest must load");
        Require(session.Resources.Initialize(session.Device), "render resources must initialize");
        Require(session.Resources.MegaGeometry().SetModelAssetSystem(session.AssetSystem),
                "model asset system must bind");

        std::vector<Rendering::ModelHandle> callbacks;
        const uint32_t requestId = session.Resources.MegaGeometry().LoadModelAsync(
            ToCoreString(options.LogicalPath),
            [&callbacks](Rendering::ModelHandle handle)
            {
                callbacks.push_back(handle);
            });
        Require(requestId != 0, "async cooked model request must be accepted");
        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        Require(session.Resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 1,
                "exactly one cooked model load must be pending");
        Require(session.Resources.MegaGeometry().FlushCompletedModelLoads(0) == 1,
                "async flush must process one cooked model");
        Require(callbacks.size() == 1, "async callback count must be one");
        Require(callbacks[0].IsValid(), "async callback must receive a valid model");
        session.Model = callbacks[0];

        const Rendering::MegaGeometry::MegaMeshHandle megaMesh =
            session.Resources.MegaGeometry().GetModelMegaMeshHandle(session.Model);
        Require(megaMesh.IsValid(), "model must own a valid mega-mesh");
        const Rendering::MegaGeometry::MegaMeshGPUData* gpuData =
            session.Resources.MegaGeometry().GetMegaMeshGPUData(megaMesh);
        Require(gpuData != nullptr, "mega-mesh GPU data must exist");
        Require(gpuData->VertexCount == 3, "cooked model vertex count must be three");
        Require(gpuData->IndexCount == 3, "cooked model index count must be three");
        Require(gpuData->ClusterCount == 1, "cooked model cluster count must be one");
        Require(session.Device->CreatedBufferDescs.size() == 3, "exactly three GPU buffers must be created");
        Require(FakeBuffer::LiveCount.load() == 3, "exactly three fake buffers must be live");

        const NorvesLib::RHI::BufferDesc& vertex = session.Device->CreatedBufferDescs[0];
        const NorvesLib::RHI::BufferDesc& index = session.Device->CreatedBufferDescs[1];
        const NorvesLib::RHI::BufferDesc& cluster = session.Device->CreatedBufferDescs[2];
        Require(vertex.Size == 96, "runtime vertex buffer must contain three interleaved Mesh3DVertex records");
        Require(index.Size == 12, "runtime index buffer must contain three uint32 indices");
        Require(cluster.Size == sizeof(Rendering::MegaGeometry::GPUClusterData),
                "runtime cluster buffer must contain one GPUClusterData record");
        Require(vertex.Usage == (NorvesLib::RHI::ResourceUsage::VertexBuffer |
                                 NorvesLib::RHI::ResourceUsage::StorageBuffer),
                "vertex buffer usage must match mega-geometry");
        Require(index.Usage == (NorvesLib::RHI::ResourceUsage::IndexBuffer |
                                NorvesLib::RHI::ResourceUsage::StorageBuffer),
                "index buffer usage must match mega-geometry");
        Require(cluster.Usage == NorvesLib::RHI::ResourceUsage::StorageBuffer,
                "cluster buffer usage must be storage");

        session.Resources.MegaGeometry().ReleaseModel(session.Model);
        session.Model = Rendering::ModelHandle::Invalid();
        session.Resources.Shutdown();
        session.Device->DropObservedBuffers();
        Require(!session.Resources.MegaGeometry().GetModelMegaMeshHandle(callbacks[0]).IsValid(),
                "shutdown must retire the cached model handle");
        Require(session.Resources.MegaGeometry().GetMegaMeshGPUData(megaMesh) == nullptr,
                "shutdown must remove the cached mega-mesh GPU data");
        Require(FakeBuffer::LiveCount.load() == 0, "shutdown must leave no fake buffers live");
    }
} // namespace

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "ModelResourcesAssetCookSmokeTest start\n";
    try
    {
        RunSmoke(ParseOptions(argc, argv));
    }
    catch (const std::exception& error)
    {
        std::cerr << "Requirement failed: " << error.what() << "\n";
        return 1;
    }
    std::cout << "ModelResourcesAssetCookSmokeTest passed\n";
    return 0;
}
