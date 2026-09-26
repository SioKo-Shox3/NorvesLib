#include "Rendering/RenderResources.h"
#include "Library/Core/Private/Rendering/TextureAssetRuntime.h"
#include "Library/Core/Private/Rendering/TextureAssetResolver.h"
#include "Library/Core/Private/Rendering/TextureAsyncLoadQueue.h"
#include "Resource/GLTFAnalyzer.h"
#include "Library/Core/Private/Rendering/RenderWorldAssetFlushPolicy.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"
#include "Thread/JobSystem.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
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

namespace NorvesLib::Core::Resource
{
    struct GLTFAnalyzerShutdownTestAccess
    {
        static void Close()
        {
            GLTFAnalyzer::CloseAsyncAssetLoadAdmissionAndWait();
        }

        static void Reopen()
        {
            GLTFAnalyzer::ReopenAsyncAssetLoadAdmission();
        }

        static bool IsOpen()
        {
            return GLTFAnalyzer::IsAsyncAssetLoadAdmissionOpen();
        }
    };
}

namespace NorvesLib::Core::Rendering
{
    struct TextureAsyncLoadQueueShutdownTestAccess
    {
        static void SetWaitHook(TextureAsyncLoadQueue& queue, Delegate<void> hook)
        {
            queue.SetWaitHookForTesting(std::move(hook));
        }
    };

    struct TextureAssetRuntimeShutdownTestAccess
    {
        static TextureAssetRuntime* Get(RenderResources& resources)
        {
            return resources.GetTextureAssetRuntimeForTesting();
        }

        static void SetAdmissionCloseHook(TextureAssetRuntime& runtime, Delegate<void> hook)
        {
            runtime.m_AdmissionCloseHookForTesting = std::move(hook);
        }

        static void Close(TextureAssetRuntime& runtime)
        {
            runtime.CloseAndWait();
        }

        static void Reopen(TextureAssetRuntime& runtime)
        {
            runtime.Bind(runtime.m_pDevice, runtime.m_pGpuResources);
        }

        static void SetQueueWaitHook(TextureAssetRuntime& runtime, Delegate<void> hook)
        {
            TextureAsyncLoadQueueShutdownTestAccess::SetWaitHook(*runtime.m_TextureAsyncLoads, std::move(hook));
        }
    };
}

namespace
{
    void AssertAssetGpuFlushDecisionMatrix()
    {
        using NorvesLib::Core::Rendering::Detail::AssetGpuFlushAction;
        using NorvesLib::Core::Rendering::Detail::DecideAssetGpuFlushAction;

        assert(DecideAssetGpuFlushAction(false, false, false, true) == AssetGpuFlushAction::FlushAndRender);
        assert(DecideAssetGpuFlushAction(true, true, false, true) == AssetGpuFlushAction::FlushAndRender);
        assert(DecideAssetGpuFlushAction(true, false, false, false) ==
               AssetGpuFlushAction::DeferFlushAndRender);
        assert(DecideAssetGpuFlushAction(true, false, false, true) ==
               AssetGpuFlushAction::DeferFlushAndSkipRender);
        assert(DecideAssetGpuFlushAction(true, false, true, true) == AssetGpuFlushAction::FlushAndRender);

        AssetGpuFlushAction action = DecideAssetGpuFlushAction(true, false, false, true);
        assert(action == AssetGpuFlushAction::DeferFlushAndSkipRender);
        action = DecideAssetGpuFlushAction(true, false, true, true);
        assert(action == AssetGpuFlushAction::FlushAndRender);
    }

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc &desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }

        void Update(const void *data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

        NorvesLib::RHI::TextureDesc Desc;
    };

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
            return offset < Bytes.size() ? Bytes.data() + offset : nullptr;
        }

        void Unmap() override {}

        void Update(const void *data, uint64_t size, uint64_t offset = 0) override
        {
            LastUpdateSize = size;
            LastUpdateOffset = offset;
            if (data == nullptr || offset + size > Bytes.size())
            {
                return;
            }

            std::memcpy(Bytes.data() + offset, data, static_cast<size_t>(size));
        }

        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }

        NorvesLib::RHI::BufferDesc Desc;
        std::vector<uint8_t> Bytes;
        uint64_t LastUpdateSize = 0;
        uint64_t LastUpdateOffset = 0;
    };

    class FakeSampler final : public NorvesLib::RHI::ISampler
    {
    public:
        explicit FakeSampler(const NorvesLib::RHI::SamplerDesc &desc)
            : Desc(desc)
        {
        }

        NorvesLib::RHI::FilterMode GetFilterMin() const override { return Desc.filterMin; }
        NorvesLib::RHI::FilterMode GetFilterMag() const override { return Desc.filterMag; }
        NorvesLib::RHI::FilterMode GetFilterMip() const override { return Desc.filterMip; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeU() const override { return Desc.addressU; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeV() const override { return Desc.addressV; }
        NorvesLib::RHI::TextureAddressMode GetAddressModeW() const override { return Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return Desc.maxAnisotropy; }
        NorvesLib::RHI::CompareFunc GetCompareFunc() const override { return Desc.compareFunc; }

        NorvesLib::RHI::SamplerDesc Desc;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &desc) override
        {
            CreatedBufferDescs.push_back(desc);
            LastBuffer = MakeShared<FakeBuffer>(desc);
            return LastBuffer;
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc);
            return LastTexture;
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &desc) override
        {
            CreatedSamplerDescs.push_back(desc);
            LastSampler = MakeShared<FakeSampler>(desc);
            return LastSampler;
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::CommandListPtr CreateCommandList() override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override { return {}; }
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
        std::vector<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        std::vector<NorvesLib::RHI::SamplerDesc> CreatedSamplerDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeBuffer> LastBuffer;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
        NorvesLib::Core::Container::TSharedPtr<FakeSampler> LastSampler;
    };

    TextureCreateInfo MakeTextureCreateInfo(const char *debugName)
    {
        TextureCreateInfo createInfo;
        createInfo.Width = 4;
        createInfo.Height = 2;
        createInfo.MipLevels = 1;
        createInfo.PixelFormat = TextureCreateInfo::Format::RGBA8_UNORM;
        createInfo.DebugName = debugName;
        return createInfo;
    }

    NorvesLib::RHI::TextureDesc MakeExternalTextureDesc()
    {
        NorvesLib::RHI::TextureDesc desc;
        desc.Width = 8;
        desc.Height = 4;
        desc.Depth = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = NorvesLib::RHI::Format::R8G8B8A8_UNORM;
        desc.Usage = NorvesLib::RHI::ResourceUsage::ShaderRead;
        desc.DebugName = "ExternalTexture";
        return desc;
    }

    void TestLegacyGLTFAdmission(RenderResources& manager)
    {
        using NorvesLib::Core::Resource::GLTFAnalyzer;
        using NorvesLib::Core::Resource::GLTFAnalyzerShutdownTestAccess;

        GLTFAnalyzerShutdownTestAccess::Reopen();
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());
        const ModelLoadResourceContext context{manager.Textures(), manager.MegaGeometry()};
        const uint32_t firstRequest = GLTFAnalyzer::LoadModelAsync("missing_shutdown_contract.gltf", context);
        assert(firstRequest != 0);
        GLTFAnalyzer::CancelModelLoad(firstRequest);
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());

        GLTFAnalyzerShutdownTestAccess::Close();
        assert(!GLTFAnalyzerShutdownTestAccess::IsOpen());
        assert(GLTFAnalyzer::GetPendingAsyncModelLoadCount() == 0);
        assert(GLTFAnalyzer::LoadModelAsync("missing_shutdown_contract.gltf", context) == 0);
        GLTFAnalyzerShutdownTestAccess::Close();

        GLTFAnalyzerShutdownTestAccess::Reopen();
        assert(GLTFAnalyzerShutdownTestAccess::IsOpen());
        const uint32_t reopenedRequest = GLTFAnalyzer::LoadModelAsync("missing_shutdown_reopen.gltf", context);
        assert(reopenedRequest != 0);
        GLTFAnalyzer::CancelModelLoad(reopenedRequest);
        GLTFAnalyzerShutdownTestAccess::Close();
        GLTFAnalyzerShutdownTestAccess::Reopen();
    }

    void TestTextureAdmission(RenderResources& manager)
    {
        TextureAssetRuntime* runtime =
            NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Get(manager);
        assert(runtime != nullptr);

        bool bAdmissionCloseHookRan = false;
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetAdmissionCloseHook(
            *runtime,
            [&bAdmissionCloseHookRan]()
            {
                bAdmissionCloseHookRan = true;
            });
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        assert(bAdmissionCloseHookRan);
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);
        assert(manager.Textures().LoadTextureAsync("missing_shutdown_contract.png") == 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);

        const uint32_t reopenedRequest = manager.Textures().LoadTextureAsync("missing_shutdown_reopen.png");
        assert(reopenedRequest != 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
    }

    void TestTextureQueueCloseWaitHandshake()
    {
        using namespace std::chrono_literals;
        TextureAsyncLoadQueue queue;
        TextureAssetLoadPlan plan;
        plan.RequestPath = "queue_wait_handshake.png";
        plan.CacheKey = "queue_wait_handshake";
        auto request = queue.CreateRequest(
            plan,
            TextureAssetFallbackMode::FailOnCookedFailure,
            {});
        assert(request != nullptr);
        request->Task = NorvesLib::Thread::Task::Create([]() {});
        assert(queue.EnqueueOrAppendDuplicateAndSubmit(request).bSubmitted);
        request->Task->Cancel();
        request->Task->Wait();

        TextureAsyncLoadQueue::CompletedBatch batch = queue.DetachCompletedRequests();
        assert(batch.Requests.size() == 1);

        std::mutex gateMutex;
        std::condition_variable gateCondition;
        bool bWaitHookReached = false;
        std::atomic<bool> bCloseReturned{false};
        NorvesLib::Core::Rendering::TextureAsyncLoadQueueShutdownTestAccess::SetWaitHook(
            queue,
            [&gateMutex, &gateCondition, &bWaitHookReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bWaitHookReached = true;
                gateCondition.notify_all();
            });

        std::thread closeThread([&queue, &bCloseReturned]()
        {
            queue.CloseCancelAllAndWait();
            bCloseReturned.store(true, std::memory_order_release);
        });

        bool bHookObserved = false;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            bHookObserved = gateCondition.wait_for(lock, 2s, [&bWaitHookReached]()
            {
                return bWaitHookReached;
            });
        }
        if (!bHookObserved)
        {
            batch.Guard.Reset();
            closeThread.join();
            assert(bHookObserved);
        }
        assert(!bCloseReturned.load(std::memory_order_acquire));

        batch.Guard.Reset();
        closeThread.join();
        assert(bCloseReturned.load(std::memory_order_acquire));
        assert(queue.GetPendingCount() == 0);
    }

    void TestTextureProductionCallbackCloseWait(RenderResources& manager)
    {
        using namespace std::chrono_literals;
        TextureAssetRuntime* runtime =
            NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Get(manager);
        assert(runtime != nullptr);

        std::mutex gateMutex;
        std::condition_variable gateCondition;
        bool bCallbackEntered = false;
        bool bCloseAdmissionReached = false;
        bool bQueueWaitReached = false;
        bool bReleaseCallback = false;
        uint32_t lateRequestId = 99;
        std::atomic<bool> bCloseReturned{false};
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetAdmissionCloseHook(
            *runtime,
            [&gateMutex, &gateCondition, &bCloseAdmissionReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bCloseAdmissionReached = true;
                gateCondition.notify_all();
            });
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::SetQueueWaitHook(
            *runtime,
            [&gateMutex, &gateCondition, &bQueueWaitReached]()
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                bQueueWaitReached = true;
                gateCondition.notify_all();
            });

        assert(manager.Textures().LoadTextureAsync(
                   "missing_texture_callback_close.png",
                   [&manager, &gateMutex, &gateCondition, &bCallbackEntered,
                    &bCloseAdmissionReached, &bReleaseCallback, &lateRequestId](TextureHandle)
                   {
                       std::unique_lock<std::mutex> lock(gateMutex);
                       bCallbackEntered = true;
                       gateCondition.notify_all();
                       gateCondition.wait(lock, [&bCloseAdmissionReached]()
                       {
                           return bCloseAdmissionReached;
                       });
                       lateRequestId = manager.Textures().LoadTextureAsync("late_texture_callback.png");
                       gateCondition.wait(lock, [&bReleaseCallback]()
                       {
                           return bReleaseCallback;
                       });
                   }) != 0);
        NorvesLib::Thread::JobSystem::Get().DrainAcceptedFiniteTasks();

        std::thread flushThread([&manager]()
        {
            manager.Textures().FlushCompletedTextureLoads();
        });
        bool bCallbackObserved = false;
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            bCallbackObserved = gateCondition.wait_for(lock, 2s, [&bCallbackEntered]()
            {
                return bCallbackEntered;
            });
        }
        bool bWaitObserved = false;
        bool bCloseBlocked = false;
        std::thread closeThread;
        if (bCallbackObserved)
        {
            closeThread = std::thread([runtime, &bCloseReturned]()
            {
                NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
                bCloseReturned.store(true, std::memory_order_release);
            });
            {
                std::unique_lock<std::mutex> lock(gateMutex);
                bWaitObserved = gateCondition.wait_for(lock, 2s, [&bQueueWaitReached]()
                {
                    return bQueueWaitReached;
                });
            }
            bCloseBlocked = !bCloseReturned.load(std::memory_order_acquire);
        }

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            bCloseAdmissionReached = true;
            bReleaseCallback = true;
            gateCondition.notify_all();
        }
        flushThread.join();
        if (closeThread.joinable())
        {
            closeThread.join();
        }

        assert(bCallbackObserved);
        assert(bWaitObserved);
        assert(lateRequestId == 0);
        assert(bCloseBlocked);
        assert(bCloseReturned.load(std::memory_order_acquire));
        assert(manager.Textures().GetPendingAsyncLoadCount() == 0);

        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
        const uint32_t reopenedRequest = manager.Textures().LoadTextureAsync("reopen_texture_callback.png");
        assert(reopenedRequest != 0);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Close(*runtime);
        NorvesLib::Core::Rendering::TextureAssetRuntimeShutdownTestAccess::Reopen(*runtime);
    }

    void TestTextureQueueRejectsAfterJobSystemStop()
    {
        TextureAsyncLoadQueue queue;
        TextureAssetLoadPlan plan;
        plan.RequestPath = "queue_rejected_after_stop.png";
        plan.CacheKey = "queue_rejected_after_stop";
        auto request = queue.CreateRequest(
            plan,
            TextureAssetFallbackMode::FailOnCookedFailure,
            {});
        assert(request != nullptr);
        request->Task = NorvesLib::Thread::Task::Create([]() {});

        NorvesLib::Thread::JobSystem::Get().StopAcceptingTasks();
        const TextureAsyncLoadQueue::EnqueueResult rejected = queue.EnqueueOrAppendDuplicateAndSubmit(request);
        assert(rejected.RequestId == 0);
        assert(!rejected.bSubmitted);
        assert(queue.GetPendingCount() == 0);
        TextureAsyncLoadQueue::Callback callback;
        assert(queue.TryAppendDuplicate(plan.CacheKey, callback) == 0);
        NorvesLib::Thread::JobSystem::Get().DrainAcceptedFiniteTasks();
        NorvesLib::Thread::JobSystem::Get().Shutdown();
    }
}

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

    std::cout << "RenderResourcesDomainContractTest start\n";
    NorvesLib::Thread::JobSystem::Get().Initialize(2, NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);

    AssertAssetGpuFlushDecisionMatrix();

    RenderResources manager;
    const TextureCreateInfo createInfo = MakeTextureCreateInfo("OwnedTexture");
    const TextureHandle preInitializeHandle = manager.Textures().CreateTexture(createInfo);
    assert(!preInitializeHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 0);

    auto device = MakeShared<FakeDevice>();
    assert(manager.Initialize(device));
    TestLegacyGLTFAdmission(manager);
    TestTextureAdmission(manager);
    TestTextureQueueCloseWaitHandshake();
    TestTextureProductionCallbackCloseWait(manager);

    BufferCreateInfo bufferInfo;
    bufferInfo.Size = 64;
    bufferInfo.bHostVisible = true;
    bufferInfo.UsageType = BufferCreateInfo::Usage::Vertex;
    bufferInfo.DebugName = "ContractBuffer";

    const uint32_t bufferData[4] = {1, 2, 3, 4};
    const BufferHandle bufferHandle = manager.Gpu().CreateBuffer(bufferInfo, bufferData, sizeof(bufferData));
    assert(bufferHandle.IsValid());
    assert(device->CreatedBufferDescs.size() == 1);
    assert(device->CreatedBufferDescs[0].Size == bufferInfo.Size);
    assert(device->CreatedBufferDescs[0].Usage == NorvesLib::RHI::ResourceUsage::VertexBuffer);
    assert(manager.GetResourceStats().BufferCount == 1);
    assert(manager.GetResourceStats().TotalBufferMemory == bufferInfo.Size);
    assert(manager.Gpu().GetRHIBuffer(bufferHandle) == device->LastBuffer.get());
    assert(device->LastBuffer->LastUpdateSize == sizeof(bufferData));

    const uint32_t updatedData[2] = {7, 8};
    assert(manager.Gpu().UpdateBuffer(bufferHandle, updatedData, sizeof(updatedData)));
    assert(device->LastBuffer->LastUpdateSize == sizeof(updatedData));

    manager.Gpu().ReleaseBuffer(BufferHandle::Invalid());
    assert(manager.GetResourceStats().BufferCount == 1);
    manager.Gpu().ReleaseBuffer(bufferHandle);
    assert(manager.GetResourceStats().BufferCount == 0);
    assert(manager.Gpu().GetRHIBuffer(bufferHandle) == nullptr);

    const SamplerHandle defaultSampler = manager.Gpu().GetDefaultSampler();
    assert(defaultSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 1);
    assert(device->CreatedSamplerDescs[0].filterMin == NorvesLib::RHI::FilterMode::Anisotropic);
    assert(manager.GetResourceStats().SamplerCount == 1);
    assert(manager.Gpu().GetDefaultSampler().Id == defaultSampler.Id);
    assert(device->CreatedSamplerDescs.size() == 1);

    const SamplerHandle pointSampler = manager.Gpu().GetPointSampler();
    assert(pointSampler.IsValid());
    assert(device->CreatedSamplerDescs.size() == 2);
    assert(device->CreatedSamplerDescs[1].filterMin == NorvesLib::RHI::FilterMode::Point);
    assert(manager.GetResourceStats().SamplerCount == 2);
    manager.Gpu().ReleaseSampler(defaultSampler);
    assert(manager.GetResourceStats().SamplerCount == 1);
    manager.Gpu().ReleaseSampler(pointSampler);
    assert(manager.GetResourceStats().SamplerCount == 0);

    VertexLayout layout = VertexLayout::CreateStandard();
    const VertexLayoutHandle layoutHandle = manager.Gpu().RegisterVertexLayout(layout);
    assert(layoutHandle.IsValid());
    const VertexLayout *registeredLayout = manager.Gpu().GetVertexLayout(layoutHandle);
    assert(registeredLayout != nullptr);
    assert(registeredLayout->Stride == layout.Stride);
    assert(registeredLayout->HasSemantic(VertexSemantic::Position));
    assert(registeredLayout->HasSemantic(VertexSemantic::Normal));
    assert(manager.Gpu().GetVertexLayout(VertexLayoutHandle::Invalid()) == nullptr);

    const TextureHandle ownedHandle = manager.Textures().CreateTexture(createInfo);
    assert(ownedHandle.IsValid());
    assert(device->CreatedTextureDescs.size() == 1);
    assert(manager.GetResourceStats().TextureCount == 1);

    NorvesLib::RHI::ITexture *ownedRaw = manager.Textures().GetRHITexture(ownedHandle);
    auto ownedShared = manager.Textures().GetRHITexturePtr(ownedHandle);
    assert(ownedRaw != nullptr);
    assert(ownedShared);
    assert(ownedShared.get() == ownedRaw);
    assert(ownedRaw == device->LastTexture.get());

    manager.Textures().ReleaseTexture(TextureHandle::Invalid());
    assert(manager.GetResourceStats().TextureCount == 1);

    manager.Textures().ReleaseTexture(ownedHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(ownedHandle) == nullptr);
    assert(!manager.Textures().GetRHITexturePtr(ownedHandle));

    auto externalTexture = MakeShared<FakeTexture>(MakeExternalTextureDesc());
    const TextureHandle externalHandle = manager.Textures().RegisterExternalTexture(externalTexture, "ExternalTexture");
    assert(externalHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    assert(manager.Textures().GetRHITexture(externalHandle) == externalTexture.get());
    assert(manager.Textures().GetRHITexturePtr(externalHandle).get() == externalTexture.get());

    manager.Textures().ReleaseTexture(externalHandle);
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(externalHandle) == nullptr);

    const TextureHandle shutdownHandle = manager.Textures().CreateTexture(createInfo);
    assert(shutdownHandle.IsValid());
    assert(manager.GetResourceStats().TextureCount == 1);
    manager.Shutdown();
    assert(manager.GetResourceStats().TextureCount == 0);
    assert(manager.Textures().GetRHITexture(shutdownHandle) == nullptr);
    assert(!manager.Textures().GetRHITexturePtr(shutdownHandle));

    TestTextureQueueRejectsAfterJobSystemStop();

    std::cout << "RenderResourcesDomainContractTest passed\n";
    return 0;
}
