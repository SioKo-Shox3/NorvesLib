#include "Asset/AssetSystem.h"
#include "Debug/DebugConfig.h"
#include "Logging/Logger.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include "Thread/JobSystem.h"

#include <atomic>
#include <chrono>
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
namespace Logging = NorvesLib::Core::Logging;
namespace Rendering = NorvesLib::Core::Rendering;
namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    Container::String ToCoreString(const std::string& text)
    {
        return CookedModelSupport::ToCoreString(text);
    }

    std::filesystem::path CreateTestRoot()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("NorvesLibModelResourcesAssetLoadProfileContract_" + std::to_string(stamp));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        return root;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(input.is_open(), "profile log file must open");
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    void AssertNotContains(const std::string& text, const std::string& needle)
    {
        Require(text.find(needle) == std::string::npos, "Unexpected legacy log substring present: " + needle);
    }

    size_t CountOccurrences(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        size_t offset = 0;
        while ((offset = text.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

    void AssertCount(const std::string& text, const std::string& needle, size_t expected)
    {
        Require(CountOccurrences(text, needle) == expected,
                "Unexpected log cardinality for: " + needle);
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

        uint64_t GetSize() const override { return Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            if (data != nullptr && offset + size <= Bytes.size())
            {
                std::memcpy(Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
            }
        }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

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
            return Container::MakeShared<FakeBuffer>(desc);
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

        NorvesLib::RHI::DeviceCapabilities Capabilities;
    };

#if NORVES_ENABLE_LOGGING
    class ProfileSession final
    {
    public:
        ProfileSession(const std::filesystem::path& root, const std::filesystem::path& logPath)
        {
            Logging::LogConfig config;
            config.minLevel = Logging::LogLevel::Info;
            config.consoleMinLevel = Logging::LogLevel::Fatal;
            config.outputType = Logging::LogOutput::File;
            config.logFilePath = ToCoreString(logPath.generic_string());
            config.bAsyncLogging = false;
            config.bAutoFlush = true;
            config.bIncludeSourceInfo = false;
            Require(Logging::Logger::GetInstance().Initialize(config), "file logger must initialize");
            m_bLoggerInitialized = true;

            NorvesLib::Thread::JobSystem::Get().Initialize(
                2,
                NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);
            m_bJobSystemInitialized = true;

            const CookedModelSupport::ByteArray payload = CookedModelSupport::BuildCookedModelMesh();
            CookedModelSupport::WriteBinaryFile(
                root / "Cooked" / "runtime_profile.nvpkg",
                CookedModelSupport::BuildModelPackage(payload, "Models/runtime_profile.nvmesh"));
            const uint64_t hash = Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
            AssetSystem = Container::MakeShared<Asset::AssetSystem>(root.generic_string().c_str());
            Require(AssetSystem->LoadManifestFromJsonText(CookedModelSupport::BuildModelManifest(
                        hash,
                        "Models/runtime_profile.gltf",
                        "Cooked/runtime_profile.nvpkg",
                        "Models/runtime_profile.nvmesh")),
                    "model manifest must load");

            Device = Container::MakeShared<FakeDevice>();
            Require(Resources.Initialize(Device), "render resources must initialize");
        }

        ~ProfileSession()
        {
            Cleanup();
        }

        void Cleanup()
        {
            if (SyncModel.IsValid())
            {
                Resources.MegaGeometry().ReleaseModel(SyncModel);
                SyncModel = Rendering::ModelHandle::Invalid();
            }
            if (AsyncModel.IsValid())
            {
                Resources.MegaGeometry().ReleaseModel(AsyncModel);
                AsyncModel = Rendering::ModelHandle::Invalid();
            }
            Resources.Shutdown();
            AssetSystem.reset();
            if (m_bJobSystemInitialized)
            {
                NorvesLib::Thread::JobSystem::Get().WaitForAll();
                NorvesLib::Thread::JobSystem::Get().Shutdown();
                m_bJobSystemInitialized = false;
            }
            if (m_bLoggerInitialized)
            {
                Logging::Logger::GetInstance().Flush();
                Logging::Logger::GetInstance().Shutdown();
                m_bLoggerInitialized = false;
            }
        }

        bool m_bLoggerInitialized = false;
        bool m_bJobSystemInitialized = false;
        Rendering::RenderResources Resources;
        Container::TSharedPtr<FakeDevice> Device;
        Container::TSharedPtr<Asset::AssetSystem> AssetSystem;
        Rendering::ModelHandle SyncModel = Rendering::ModelHandle::Invalid();
        Rendering::ModelHandle AsyncModel = Rendering::ModelHandle::Invalid();
    };

    std::string CaptureProfileLog(const std::filesystem::path& root, const std::filesystem::path& logPath,
                                  uint32_t& outAsyncRequestId)
    {
        ProfileSession session(root, logPath);
        const Container::String logicalPath = ToCoreString("Models/runtime_profile.gltf");

        session.SyncModel = session.Resources.MegaGeometry().LoadModel(*session.AssetSystem, logicalPath);
        Require(session.SyncModel.IsValid(), "sync cooked model load must succeed");
        session.Resources.MegaGeometry().ReleaseModel(session.SyncModel);
        session.SyncModel = Rendering::ModelHandle::Invalid();

        Require(session.Resources.MegaGeometry().SetModelAssetSystem(session.AssetSystem),
                "model asset system must bind for async load");
        std::vector<Rendering::ModelHandle> callbacks;
        outAsyncRequestId = session.Resources.MegaGeometry().LoadModelAsync(
            logicalPath,
            [&callbacks](Rendering::ModelHandle handle)
            {
                callbacks.push_back(handle);
            });
        Require(outAsyncRequestId != 0, "async cooked model request must be accepted");
        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        Require(session.Resources.MegaGeometry().FlushCompletedModelLoads(0) == 1,
                "async flush must process one cooked model");
        Require(callbacks.size() == 1 && callbacks[0].IsValid(),
                "async cooked model callback must succeed exactly once");
        session.AsyncModel = callbacks[0];

        session.Cleanup();
        Require(FakeBuffer::LiveCount.load() == 0, "profile cleanup must release every fake buffer");
        return ReadTextFile(logPath);
    }
#endif
} // namespace

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "ModelResourcesAssetLoadProfileContractTest start\n";
#if !NORVES_ENABLE_LOGGING
    std::cout << "ModelResourcesAssetLoadProfileContractTest skipped: NORVES_ENABLE_LOGGING == 0\n";
    return 0;
#else
    const std::filesystem::path root = CreateTestRoot();
    const std::filesystem::path logPath = root / "AssetLoadProfileContract.log";
    try
    {
        uint32_t asyncRequestId = 0;
        const std::string logText = CaptureProfileLog(root, logPath, asyncRequestId);
        const std::string requestId = std::to_string(asyncRequestId);
        const std::string path = " normalized_path=\"Models/runtime_profile.gltf\" status=0 success=1";

        AssertCount(logText, "stage=model_asset_resolve role=caller source=cooked_nvmesh request_id=0" + path, 1);
        AssertCount(logText, "stage=model_cooked_parse role=caller source=cooked_nvmesh request_id=0" + path, 1);
        AssertCount(logText, "stage=model_asset_resolve role=worker source=cooked_nvmesh request_id=" + requestId + path, 1);
        AssertCount(logText, "stage=model_cooked_parse role=worker source=cooked_nvmesh request_id=" + requestId + path, 1);
        AssertCount(logText, "stage=model_finalize_textures role=main_render request_id=0", 1);
        AssertCount(logText, "stage=model_finalize_megamesh role=main_render request_id=0", 1);
        AssertCount(logText, "stage=megamesh_gpu_upload role=main_render", 2);
        AssertCount(logText, "stage=model_finalize_register role=main_render request_id=0", 1);
        AssertCount(logText, "stage=model_finalize_total role=main_render request_id=0", 1);
        AssertCount(logText, "stage=model_finalize_textures role=main_render request_id=" + requestId, 1);
        AssertCount(logText, "stage=model_finalize_megamesh role=main_render request_id=" + requestId, 1);
        AssertCount(logText, "stage=model_finalize_register role=main_render request_id=" + requestId, 1);
        AssertCount(logText, "stage=model_finalize_total role=main_render request_id=" + requestId, 1);
        AssertCount(logText, "stage=model_async_flush role=main_render processed=1 success=1", 1);

        AssertNotContains(logText, "stage=gltf_finalize_textures");
        AssertNotContains(logText, "stage=gltf_finalize_megamesh");
        AssertNotContains(logText, "stage=gltf_finalize_register");
        AssertNotContains(logText, "stage=gltf_finalize_total");
        std::filesystem::remove_all(root);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Requirement failed: " << error.what() << "\n";
        std::filesystem::remove_all(root);
        return 1;
    }

    std::cout << "ModelResourcesAssetLoadProfileContractTest passed\n";
    return 0;
#endif
}
