#include "Asset/AssetSystem.h"
#include "Rendering/RenderResources.h"
#include "Resource/ModelAssetRuntime.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Test/Core/Asset/CookedModelTestSupport.h"
#include "Thread/JobSystem.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

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

namespace CookedModelSupport = NorvesLib::Test::CookedModelSupport;
namespace Rendering = NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Asset = NorvesLib::Core::Asset;

namespace
{
    class FakeBuffer final : public NorvesLib::RHI::IBuffer
    {
    public:
        FakeBuffer(const NorvesLib::RHI::BufferDesc& desc,
                   std::function<void()>* pUpdateHook,
                   std::function<void()>* pDestroyHook)
            : Desc(desc),
              Bytes(static_cast<size_t>(desc.Size)),
              m_pUpdateHook(pUpdateHook),
              m_pDestroyHook(pDestroyHook)
        {
            ++LiveCount;
        }

        ~FakeBuffer() override
        {
            --LiveCount;
            if (m_pDestroyHook != nullptr && *m_pDestroyHook)
            {
                (*m_pDestroyHook)();
            }
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
            if (m_pUpdateHook != nullptr && *m_pUpdateHook)
            {
                std::function<void()> hook = std::move(*m_pUpdateHook);
                *m_pUpdateHook = {};
                hook();
            }
        }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        static std::atomic<int> LiveCount;
        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;

    private:
        std::function<void()>* m_pUpdateHook = nullptr;
        std::function<void()>* m_pDestroyHook = nullptr;
    };

    std::atomic<int> FakeBuffer::LiveCount{0};

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc& desc) override
        {
            CreatedBufferDescs.push_back(desc);
            auto buffer = Container::MakeShared<FakeBuffer>(desc, &OnNextBufferUpdate, &OnBufferDestroyed);
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
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc&) override
        {
            return {};
        }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc&) override
        {
            return {};
        }
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
        std::function<void()> OnNextBufferUpdate;
        std::function<void()> OnBufferDestroyed;
    };

    struct AssetFixture
    {
        std::filesystem::path Root;
        Container::TSharedPtr<Asset::AssetSystem> System;

        explicit AssetFixture(const char* suffix)
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            Root = std::filesystem::temp_directory_path() /
                   (std::string("NorvesLibModelRuntime_") + suffix + "_" + std::to_string(stamp));
            std::filesystem::remove_all(Root);
            std::filesystem::create_directories(Root);
            CookedModelSupport::ByteArray payload = CookedModelSupport::BuildCookedModelMesh();
            CookedModelSupport::WriteBinaryFile(
                Root / "Cooked" / "Models.nvpkg",
                CookedModelSupport::BuildModelPackage(payload));
            const uint64_t hash = Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
            System = Container::MakeShared<Asset::AssetSystem>(Root.generic_string().c_str());
            assert(System->LoadManifestFromJsonText(CookedModelSupport::BuildModelManifest(hash)));
        }

        ~AssetFixture()
        {
            System.reset();
            std::filesystem::remove_all(Root);
        }
    };

    void WaitForWorkers()
    {
        NorvesLib::Thread::JobSystem::Get().WaitForAll();
    }

    void TestPublicSyncAndAsyncLeaseContract()
    {
        AssetFixture fixture("public");
        Rendering::RenderResources resources;
        auto device = Container::MakeShared<FakeDevice>();

        assert(!resources.MegaGeometry().SetModelAssetSystem(nullptr));
        assert(resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh") == 0);
        assert(resources.Initialize(device));
        assert(resources.MegaGeometry().SetModelAssetSystem(fixture.System));
        assert(resources.MegaGeometry().SetModelAssetSystem(fixture.System));
        assert(resources.MegaGeometry().LoadModelAsync("") == 0);
        assert(resources.MegaGeometry().LoadModelAsync("C:/Models/Triangle.nvmesh") == 0);

        Rendering::ModelHandle sync = resources.MegaGeometry().LoadModel(
            *fixture.System,
            "Models/Triangle.nvmesh");
        assert(sync.IsValid());
        const auto syncMega = resources.MegaGeometry().GetModelMegaMeshHandle(sync);
        assert(syncMega.IsValid());
        resources.MegaGeometry().ReleaseModel(sync);
        assert(!resources.MegaGeometry().GetModelMegaMeshHandle(sync).IsValid());
        assert(resources.MegaGeometry().GetMegaMeshGPUData(syncMega) == nullptr);

        Rendering::ModelHandle first = Rendering::ModelHandle::Invalid();
        Rendering::ModelHandle second = Rendering::ModelHandle::Invalid();
        bool bActiveFlushRejectedConfig = false;
        auto replacement = Container::MakeShared<Asset::AssetSystem>(fixture.Root.generic_string().c_str());
        const CookedModelSupport::ByteArray payload = CookedModelSupport::BuildCookedModelMesh();
        const uint64_t hash = Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size());
        assert(replacement->LoadManifestFromJsonText(CookedModelSupport::BuildModelManifest(hash)));
        const uint32_t firstId = resources.MegaGeometry().LoadModelAsync(
            "Models/./Triangle.nvmesh",
            [&resources, &replacement, &first, &bActiveFlushRejectedConfig](Rendering::ModelHandle handle)
            {
                first = handle;
                bActiveFlushRejectedConfig = !resources.MegaGeometry().SetModelAssetSystem(replacement);
            });
        const uint32_t duplicateId = resources.MegaGeometry().LoadModelAsync(
            "Assets/Models/Triangle.nvmesh",
            [&second](Rendering::ModelHandle handle)
            {
                second = handle;
            });
        assert(firstId != 0);
        assert(duplicateId == firstId);
        assert(resources.MegaGeometry().GetPendingAsyncModelLoadCount() == 1);
        WaitForWorkers();
        assert(resources.MegaGeometry().FlushCompletedModelLoads(1) == 1);
        assert(first.IsValid());
        assert(first == second);
        assert(bActiveFlushRejectedConfig);

        Rendering::ModelHandle cacheHit = Rendering::ModelHandle::Invalid();
        bool bBlockingRejected = false;
        const uint32_t cacheHitId = resources.MegaGeometry().LoadModelAsync(
            "Models/Triangle.nvmesh",
            [&resources, &cacheHit, &bBlockingRejected](Rendering::ModelHandle handle)
            {
                cacheHit = handle;
                bBlockingRejected = !resources.MegaGeometry().CancelPendingModelLoadsAndWait();
            });
        assert(cacheHitId != 0);
        assert(cacheHit == first);
        assert(bBlockingRejected);

        assert(resources.MegaGeometry().SetModelAssetSystem(replacement));
        const auto managedMega = resources.MegaGeometry().GetModelMegaMeshHandle(first);
        assert(managedMega.IsValid());
        resources.MegaGeometry().ReleaseModel(first);
        assert(resources.MegaGeometry().GetModelMegaMeshHandle(first).IsValid());
        resources.MegaGeometry().ReleaseModel(second);
        assert(resources.MegaGeometry().GetModelMegaMeshHandle(first).IsValid());
        resources.MegaGeometry().ReleaseModel(cacheHit);
        assert(!resources.MegaGeometry().GetModelMegaMeshHandle(first).IsValid());
        assert(resources.MegaGeometry().GetMegaMeshGPUData(managedMega) == nullptr);
        resources.MegaGeometry().ReleaseModel(cacheHit);

        resources.Shutdown();
        assert(FakeBuffer::LiveCount.load() == 0);
    }

    void TestCancelClearAndShutdownAdmission()
    {
        AssetFixture fixture("close");
        Rendering::RenderResources resources;
        auto device = Container::MakeShared<FakeDevice>();
        assert(resources.Initialize(device));
        assert(resources.MegaGeometry().SetModelAssetSystem(fixture.System));

        const size_t buffersBeforeCancel = device->CreatedBufferDescs.size();
        const uint32_t cancelled = resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh");
        assert(cancelled != 0);
        resources.MegaGeometry().CancelModelLoad(cancelled);
        const uint32_t fresh = resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh");
        assert(fresh != 0 && fresh != cancelled);
        resources.MegaGeometry().CancelModelLoad(fresh);
        WaitForWorkers();
        resources.MegaGeometry().FlushCompletedModelLoads(0);
        assert(device->CreatedBufferDescs.size() == buffersBeforeCancel);

        Rendering::ModelHandle cached = Rendering::ModelHandle::Invalid();
        assert(resources.MegaGeometry().LoadModelAsync(
                   "Models/Triangle.nvmesh",
                   [&cached](Rendering::ModelHandle handle)
                   {
                       cached = handle;
                   }) != 0);
        WaitForWorkers();
        assert(resources.MegaGeometry().FlushCompletedModelLoads(0) == 1);
        assert(cached.IsValid());

        uint32_t requestDuringClear = 99;
        bool bDestroyHookRan = false;
        device->OnBufferDestroyed = [&resources, &requestDuringClear, &bDestroyHookRan]()
        {
            if (!bDestroyHookRan)
            {
                bDestroyHookRan = true;
                requestDuringClear = resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh");
            }
        };
        resources.ClearAllResources();
        assert(bDestroyHookRan);
        assert(requestDuringClear == 0);
        assert(resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh") != 0);
        assert(resources.MegaGeometry().CancelPendingModelLoadsAndWait());

        Rendering::ModelHandle sync = resources.MegaGeometry().LoadModel(
            *fixture.System,
            "Models/Triangle.nvmesh");
        assert(sync.IsValid());
        bDestroyHookRan = false;
        requestDuringClear = 99;
        resources.Shutdown();
        assert(bDestroyHookRan);
        assert(requestDuringClear == 0);
        assert(resources.MegaGeometry().LoadModelAsync("Models/Triangle.nvmesh") == 0);
        assert(FakeBuffer::LiveCount.load() == 0);
    }

    void TestGenerationGatesAndLostPublishCleanup()
    {
        AssetFixture fixture("gates");
        Rendering::RenderResources resources;
        auto device = Container::MakeShared<FakeDevice>();
        assert(resources.Initialize(device));

        Rendering::ModelAssetRuntime runtime;
        runtime.Bind(&resources.Textures(), &resources.MegaGeometry());
        assert(runtime.SetAssetSystem(fixture.System));

        const int baseline = FakeBuffer::LiveCount.load();
        const size_t preGpuBufferCount = device->CreatedBufferDescs.size();
        Rendering::ModelHandle preGpuCallback{777};
        assert(runtime.LoadModelAsync(
                   "Models/Triangle.nvmesh",
                   [&preGpuCallback](Rendering::ModelHandle handle)
                   {
                       preGpuCallback = handle;
                   }) != 0);
        WaitForWorkers();
        runtime.AdvanceGenerationForTesting();
        assert(runtime.FlushCompletedModelLoads(0) == 1);
        assert(!preGpuCallback.IsValid());
        assert(device->CreatedBufferDescs.size() == preGpuBufferCount);

        Rendering::ModelHandle staleCallback{777};
        assert(runtime.LoadModelAsync(
                   "Models/Triangle.nvmesh",
                   [&staleCallback](Rendering::ModelHandle handle)
                   {
                       staleCallback = handle;
                   }) != 0);
        WaitForWorkers();
        device->OnNextBufferUpdate = [&runtime]()
        {
            runtime.AdvanceGenerationForTesting();
        };
        assert(runtime.FlushCompletedModelLoads(0) == 1);
        assert(!staleCallback.IsValid());
        assert(FakeBuffer::LiveCount.load() == baseline);

        assert(runtime.SetAssetSystem(fixture.System));
        Rendering::ModelHandle existing = resources.MegaGeometry().LoadModel(
            *fixture.System,
            "Models/Triangle.nvmesh");
        assert(existing.IsValid());
        const auto existingMega = resources.MegaGeometry().GetModelMegaMeshHandle(existing);
        assert(existingMega.IsValid());
        const int existingLive = FakeBuffer::LiveCount.load();

        Rendering::ModelHandle published = Rendering::ModelHandle::Invalid();
        assert(runtime.LoadModelAsync(
                   "Models/Triangle.nvmesh",
                   [&published](Rendering::ModelHandle handle)
                   {
                       published = handle;
                   }) != 0);
        assert(runtime.SeedCurrentModelForTesting("Models/Triangle.nvmesh", existing));
        WaitForWorkers();
        assert(runtime.FlushCompletedModelLoads(0) == 1);
        assert(published == existing);
        assert(FakeBuffer::LiveCount.load() == existingLive);

        runtime.AdvanceGenerationForTesting();
        auto released = runtime.ReleaseManagedModel(existing);
        assert(released.bManaged);
        assert(released.HandleToRelease == existing);
        resources.MegaGeometry().ReleaseModel(released.HandleToRelease);
        assert(!resources.MegaGeometry().GetModelMegaMeshHandle(existing).IsValid());
        assert(resources.MegaGeometry().GetMegaMeshGPUData(existingMega) == nullptr);
        assert(FakeBuffer::LiveCount.load() == baseline);

        Rendering::ModelHandle zeroLease = resources.MegaGeometry().LoadModel(
            *fixture.System,
            "Models/Triangle.nvmesh");
        const auto zeroLeaseMega = resources.MegaGeometry().GetModelMegaMeshHandle(zeroLease);
        assert(runtime.SeedCurrentModelForTesting("Models/Triangle.nvmesh", zeroLease));
        runtime.AdvanceGenerationForTesting();
        assert(!resources.MegaGeometry().GetModelMegaMeshHandle(zeroLease).IsValid());
        assert(resources.MegaGeometry().GetMegaMeshGPUData(zeroLeaseMega) == nullptr);

        assert(runtime.CloseAndDrain());
        assert(runtime.LoadModelAsync("Models/Triangle.nvmesh", {}) == 0);
        assert(!runtime.SetAssetSystem(fixture.System));
        assert(runtime.FlushCompletedModelLoads(0) == 0);
        runtime.ReopenAfterClear();
        assert(runtime.SetAssetSystem(fixture.System));
        runtime.Unbind();
        resources.Shutdown();
        assert(FakeBuffer::LiveCount.load() == 0);
    }
}

int main()
{
    NorvesLib::Thread::JobSystem::Get().Initialize(2, NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);
    TestPublicSyncAndAsyncLeaseContract();
    TestCancelClearAndShutdownAdmission();
    TestGenerationGatesAndLostPublishCleanup();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "ModelResourcesAssetRuntimeTest passed\n";
    return 0;
}
