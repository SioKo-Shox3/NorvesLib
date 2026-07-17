#include "Game/GameApplicationHandler.h"
#include "Game/Bridge/NorvesLibBridgeAdapter.h"

#include "Asset/AssetSystem.h"
#include "Container/PointerTypes.h"
#include "Engine/Engine.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureAssetResolver.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Resource/ModelAssetRuntime.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include "Thread/JobSystem.h"

#include "Norves/Bridge/server.hpp"

#include <Windows.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;
namespace Engine = NorvesLib::Core::Engine;
namespace Rendering = NorvesLib::Core::Rendering;

namespace NorvesLib::Core::Rendering
{
    struct AssetRuntimeSnapshotReloadTestAccess
    {
        struct State
        {
            const Asset::AssetSystem* pTextureSnapshot = nullptr;
            const Asset::AssetSystem* pModelSnapshot = nullptr;
            uint64_t TextureGeneration = 0;
            uint64_t ModelGeneration = 0;
        };

        static State Observe(RenderResources& resources)
        {
            State state;
            TextureAssetRuntime* pTextureRuntime = resources.GetTextureAssetRuntimeForTesting();
            ModelAssetRuntime* pModelRuntime = resources.GetModelAssetRuntimeForTesting();
            assert(pTextureRuntime != nullptr);
            assert(pModelRuntime != nullptr);

            {
                Thread::ScopedLock textureLock(pTextureRuntime->m_TextureAssetMutex);
                TextureAssetResolver& resolver = pTextureRuntime->GetTextureAssetResolverLocked();
                state.pTextureSnapshot = resolver.m_System.get();
                state.TextureGeneration = resolver.GetGeneration();
            }
            {
                Thread::ScopedLock modelLock(pModelRuntime->m_AssetMutex);
                state.pModelSnapshot = pModelRuntime->m_AssetSystem.get();
                state.ModelGeneration = pModelRuntime->m_Generation;
            }
            return state;
        }
    };
} // namespace NorvesLib::Core::Rendering

namespace
{
    constexpr const char* kCapabilitiesRequest =
        R"({"bridge":"norves.editor.bridge","version":"0.2","kind":"request","id":"capabilities-1","method":"bridge.getCapabilities","params":{}})";
    constexpr const char* kInvalidReloadRequest =
        R"({"bridge":"norves.editor.bridge","version":"0.2","kind":"request","id":"reload-invalid","method":"asset.reloadManifest","params":{}})";
    constexpr const char* kBusyReloadRequest =
        R"({"bridge":"norves.editor.bridge","version":"0.2","kind":"request","id":"reload-busy","method":"asset.reloadManifest","params":{}})";
    constexpr const char* kAcceptedReloadRequest =
        R"({"bridge":"norves.editor.bridge","version":"0.2","kind":"request","id":"reload-accepted","method":"asset.reloadManifest","params":{}})";
    constexpr const char* kTextureAPath = "Textures/A.nvtex";
    constexpr const char* kModelAPath = "Models/A.nvmesh";
    constexpr const char* kTextureBPath = "Textures/B.nvtex";
    constexpr const char* kModelBPath = "Models/B.nvmesh";

    using ByteArray = std::vector<uint8_t>;

    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const NorvesLib::RHI::BufferDesc& desc)
            : m_Desc(desc),
              m_Bytes(static_cast<size_t>(desc.Size))
        {
            ++LiveCount;
        }

        ~FakeBuffer() override
        {
            --LiveCount;
        }

        uint64_t GetSize() const override
        {
            return m_Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < m_Bytes.size() ? m_Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            assert(data != nullptr);
            assert(offset + size <= m_Bytes.size());
            std::memcpy(m_Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }

        static std::atomic<int> LiveCount;

    private:
        NorvesLib::RHI::BufferDesc m_Desc;
        ByteArray m_Bytes;
    };

    std::atomic<int> FakeBuffer::LiveCount{0};

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
            ++LiveCount;
        }

        ~FakeTexture() override
        {
            --LiveCount;
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }

        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            assert(data != nullptr);
            assert(rowPitch != 0);
            assert(slicePitch != 0);
            assert(mipLevel == 0);
            assert(arrayIndex == 0);
            ++UpdateCount;
        }

        static std::atomic<int> LiveCount;
        uint32_t UpdateCount = 0;

    private:
        NorvesLib::RHI::TextureDesc m_Desc;
    };

    std::atomic<int> FakeTexture::LiveCount{0};

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc& desc) override
        {
            return Container::MakeShared<FakeBuffer>(desc);
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc& desc) override
        {
            return Container::MakeShared<FakeTexture>(desc);
        }

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
        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override { return m_Capabilities; }

        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

    private:
        NorvesLib::RHI::DeviceCapabilities m_Capabilities;
    };

    class ScopedEngineOverride final
    {
    public:
        explicit ScopedEngineOverride(Engine::Engine& engine)
            : m_pPrevious(Engine::GEngine)
        {
            Engine::GEngine = &engine;
        }

        ~ScopedEngineOverride()
        {
            Engine::GEngine = m_pPrevious;
        }

    private:
        Engine::Engine* m_pPrevious = nullptr;
    };

    void AssertContains(const std::string& wire, const std::string& expected)
    {
        assert(wire.find(expected) != std::string::npos);
    }

    ByteArray ReadFileBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input.is_open());
        return ByteArray(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    bool AtomicReplaceFileBytes(const std::filesystem::path& destination, const ByteArray& bytes)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path temporary = destination;
        temporary += L".task8-" + std::to_wstring(stamp) + L".tmp";

        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                return false;
            }
            if (!bytes.empty())
            {
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }
            output.flush();
            if (!output.good())
            {
                output.close();
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return false;
            }
        }

        const BOOL bReplaced = ::ReplaceFileW(destination.c_str(),
                                              temporary.c_str(),
                                              nullptr,
                                              REPLACEFILE_WRITE_THROUGH,
                                              nullptr,
                                              nullptr);
        if (!bReplaced)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        return true;
    }

    Container::String ToCoreString(const char* text)
    {
        return CookedModelSupport::ToCoreString(std::string(text));
    }

    Container::String ToCoreString(const std::filesystem::path& path)
    {
        return CookedModelSupport::ToCoreString(path.generic_string());
    }

    bool ParseRendering3DTestOptions(std::initializer_list<const char*> arguments)
    {
        Game::GameApplicationHandler handler;
        Container::VariableArray<Container::String> args;
        args.push_back(ToCoreString("AssetManifestReloadBridgeLoopbackTest"));
        for (const char* argument : arguments)
        {
            args.push_back(ToCoreString(argument));
        }
        return handler.OnPreInitialize(args);
    }

    void TestCookedModelOptInParserContract()
    {
        constexpr const char* kCookedFlag = "--rendering3dtest-use-cooked-model";
        assert(!ParseRendering3DTestOptions({kCookedFlag}));
        assert(!ParseRendering3DTestOptions({kCookedFlag,
                                            "--rendering3dtest-model", "Models/Silver.gltf"}));
        assert(!ParseRendering3DTestOptions({kCookedFlag,
                                            "--texture-asset-root", "RuntimeRoot",
                                            "--texture-asset-manifest", "RuntimeRoot/manifest.json"}));
        assert(ParseRendering3DTestOptions({kCookedFlag,
                                           "--texture-asset-root", "RuntimeRoot",
                                           "--texture-asset-manifest", "RuntimeRoot/manifest.json",
                                           "--rendering3dtest-model", "Models/Silver.gltf"}));
        assert(!ParseRendering3DTestOptions({kCookedFlag,
                                            kCookedFlag,
                                            "--texture-asset-root", "RuntimeRoot",
                                            "--texture-asset-manifest", "RuntimeRoot/manifest.json",
                                            "--rendering3dtest-model", "Models/Silver.gltf"}));
        assert(ParseRendering3DTestOptions({"--texture-asset-root", "RuntimeRoot",
                                           "--texture-asset-manifest", "RuntimeRoot/manifest.json",
                                           "--rendering3dtest-model", "Models/Silver.gltf"}));
    }

    void AssertStateUnchanged(const Rendering::AssetRuntimeSnapshotReloadTestAccess::State& before,
                              const Rendering::AssetRuntimeSnapshotReloadTestAccess::State& after)
    {
        assert(after.pTextureSnapshot == before.pTextureSnapshot);
        assert(after.pModelSnapshot == before.pModelSnapshot);
        assert(after.TextureGeneration == before.TextureGeneration);
        assert(after.ModelGeneration == before.ModelGeneration);
    }

    Rendering::ModelHandle LoadModelAndFlush(Rendering::RenderResources& resources,
                                             const Container::String& logicalPath)
    {
        Rendering::ModelHandle handle = Rendering::ModelHandle::Invalid();
        uint32_t callbackCount = 0;
        const uint32_t requestId = resources.MegaGeometry().LoadModelAsync(
            logicalPath,
            [&handle, &callbackCount](Rendering::ModelHandle completedHandle)
            {
                handle = completedHandle;
                ++callbackCount;
            });
        assert(requestId != 0);
        NorvesLib::Thread::JobSystem::Get().WaitForAll();
        assert(resources.MegaGeometry().FlushCompletedModelLoads(0) == 1);
        assert(callbackCount == 1);
        assert(handle.IsValid());
        return handle;
    }

    void AssertSnapshotContainsOnlyGenerationAsset(const Asset::AssetSystem& snapshot,
                                                   const char* presentTexture,
                                                   const char* presentModel,
                                                   const char* absentTexture,
                                                   const char* absentModel)
    {
        assert(snapshot.FindCookedVariant(presentTexture, Asset::AssetKind::Texture).ShouldUseCooked());
        assert(snapshot.FindCookedVariant(presentModel, Asset::AssetKind::Model).ShouldUseCooked());
        assert(!snapshot.FindCookedVariant(absentTexture, Asset::AssetKind::Texture).ShouldUseCooked());
        assert(!snapshot.FindCookedVariant(absentModel, Asset::AssetKind::Model).ShouldUseCooked());
    }
} // namespace

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    assert(argc == 4);
    TestCookedModelOptInParserContract();
    const std::filesystem::path assetRoot = std::filesystem::path(argv[1]);
    const std::filesystem::path liveManifest = std::filesystem::path(argv[2]);
    const std::filesystem::path manifestB = std::filesystem::path(argv[3]);
    const ByteArray manifestABytes = ReadFileBytes(liveManifest);
    const ByteArray manifestBBytes = ReadFileBytes(manifestB);

    NorvesLib::Thread::JobSystem::Get().Initialize(
        2,
        NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);

    Engine::Engine engine;
    ScopedEngineOverride engineOverride(engine);
    Rendering::RenderResources& resources = engine.GetRenderResources();
    auto device = Container::MakeShared<FakeDevice>();
    assert(resources.Initialize(device));

    Game::GameApplicationHandler handler;
    Container::VariableArray<Container::String> handlerArgs;
    handlerArgs.push_back(ToCoreString("AssetManifestReloadBridgeLoopbackTest"));
    handlerArgs.push_back(ToCoreString("--texture-asset-root"));
    handlerArgs.push_back(ToCoreString(assetRoot));
    handlerArgs.push_back(ToCoreString("--texture-asset-manifest"));
    handlerArgs.push_back(ToCoreString(liveManifest));
    assert(handler.OnPreInitialize(handlerArgs));
    assert(handler.ReloadConfiguredAssetManifest());

    Game::Bridge::NorvesLibBridgeAdapter adapter;
    adapter.SetHandler(handler);
    Norves::Bridge::BridgeEngineServer server(adapter, nullptr);

    const auto capabilitiesResponse = server.handleFrame(kCapabilitiesRequest);
    assert(capabilitiesResponse.has_value());
    AssertContains(*capabilitiesResponse, R"("name":"asset.reload")");

    const auto snapshotA = handler.GetAssetSystemSnapshot();
    assert(Container::IsValid(snapshotA));
    AssertSnapshotContainsOnlyGenerationAsset(
        *snapshotA,
        kTextureAPath,
        kModelAPath,
        kTextureBPath,
        kModelBPath);
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateA =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(resources);
    assert(stateA.pTextureSnapshot == snapshotA.get());
    assert(stateA.pModelSnapshot == snapshotA.get());

    const Rendering::TextureHandle textureA = resources.Textures().LoadTexture(ToCoreString(kTextureAPath));
    assert(textureA.IsValid());
    NorvesLib::RHI::ITexture* pTextureA = resources.Textures().GetRHITexture(textureA);
    assert(pTextureA != nullptr);
    const Rendering::ModelHandle modelA = LoadModelAndFlush(resources, ToCoreString(kModelAPath));
    const auto megaMeshA = resources.MegaGeometry().GetModelMegaMeshHandle(modelA);
    assert(megaMeshA.IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(megaMeshA) != nullptr);

    const ByteArray invalidManifest{'{', '"', 'v', 'e', 'r', 's', 'i', 'o', 'n', '"', ':', '1', '}'};
    assert(AtomicReplaceFileBytes(liveManifest, invalidManifest));
    const auto invalidResponse = server.handleFrame(kInvalidReloadRequest);
    assert(invalidResponse.has_value());
    AssertContains(*invalidResponse, R"("id":"reload-invalid")");
    AssertContains(*invalidResponse, R"("accepted":false)");
    assert(handler.GetAssetSystemSnapshot().get() == snapshotA.get());
    AssertStateUnchanged(stateA, Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(resources));
    assert(AtomicReplaceFileBytes(liveManifest, manifestABytes));

    const uint32_t busyRequest = resources.MegaGeometry().LoadModelAsync("Models/Busy.nvmesh");
    assert(busyRequest != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 1);
    assert(AtomicReplaceFileBytes(liveManifest, manifestBBytes));
    const auto busyResponse = server.handleFrame(kBusyReloadRequest);
    assert(busyResponse.has_value());
    AssertContains(*busyResponse, R"("id":"reload-busy")");
    AssertContains(*busyResponse, R"("accepted":false)");
    assert(handler.GetAssetSystemSnapshot().get() == snapshotA.get());
    AssertStateUnchanged(stateA, Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(resources));
    assert(resources.MegaGeometry().CancelPendingModelLoadsAndWait());
    assert(resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 0);

    const auto acceptedResponse = server.handleFrame(kAcceptedReloadRequest);
    assert(acceptedResponse.has_value());
    AssertContains(*acceptedResponse, R"("id":"reload-accepted")");
    AssertContains(*acceptedResponse, R"("accepted":true)");

    const auto snapshotB = handler.GetAssetSystemSnapshot();
    assert(Container::IsValid(snapshotB));
    assert(snapshotB.get() != snapshotA.get());
    AssertSnapshotContainsOnlyGenerationAsset(
        *snapshotB,
        kTextureBPath,
        kModelBPath,
        kTextureAPath,
        kModelAPath);
    const Rendering::AssetRuntimeSnapshotReloadTestAccess::State stateB =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::Observe(resources);
    assert(stateB.pTextureSnapshot == snapshotB.get());
    assert(stateB.pModelSnapshot == snapshotB.get());
    assert(stateB.TextureGeneration == stateA.TextureGeneration + 1);
    assert(stateB.ModelGeneration == stateA.ModelGeneration + 1);

    const Rendering::TextureHandle textureB = resources.Textures().LoadTexture(ToCoreString(kTextureBPath));
    assert(textureB.IsValid());
    assert(textureB != textureA);
    assert(resources.Textures().GetRHITexture(textureB) != nullptr);
    const Rendering::ModelHandle modelB = LoadModelAndFlush(resources, ToCoreString(kModelBPath));
    const auto megaMeshB = resources.MegaGeometry().GetModelMegaMeshHandle(modelB);
    assert(megaMeshB.IsValid());
    assert(resources.MegaGeometry().GetMegaMeshGPUData(megaMeshB) != nullptr);

    assert(resources.Textures().GetRHITexture(textureA) == pTextureA);
    assert(resources.MegaGeometry().GetModelMegaMeshHandle(modelA) == megaMeshA);
    assert(resources.MegaGeometry().GetMegaMeshGPUData(megaMeshA) != nullptr);

    resources.Textures().ReleaseTexture(textureA);
    resources.Textures().ReleaseTexture(textureB);
    resources.MegaGeometry().ReleaseModel(modelA);
    resources.MegaGeometry().ReleaseModel(modelB);
    resources.Shutdown();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    assert(FakeBuffer::LiveCount.load() == 0);
    assert(FakeTexture::LiveCount.load() == 0);

    std::cout << "[PASS] AssetManifestReloadBridgeLoopbackTest: "
                 "real handler/bridge reload preserved rejection state and old resource leases\n";
    return 0;
}
