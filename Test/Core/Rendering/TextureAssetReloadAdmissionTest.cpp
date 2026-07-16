#include "Asset/AssetSystem.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureAssetResolver.h"
#include "Rendering/TextureAssetRuntime.h"
#include "Rendering/TextureHandleCache.h"
#include "RHI/IDevice.h"
#include "Thread/JobSystem.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace Asset = NorvesLib::Core::Asset;
namespace Container = NorvesLib::Core::Container;
namespace Rendering = NorvesLib::Core::Rendering;

namespace NorvesLib::Core::Rendering
{
    struct AssetRuntimeSnapshotReloadTestAccess
    {
        struct SnapshotState
        {
            const Asset::AssetSystem* pSnapshot = nullptr;
            uint64_t Generation = 0;
        };

        static TextureAssetRuntime& GetTextureRuntime(RenderResources& resources)
        {
            TextureAssetRuntime* pRuntime = resources.GetTextureAssetRuntimeForTesting();
            assert(pRuntime != nullptr);
            return *pRuntime;
        }

        static void SetPlanBarrier(
            RenderResources& resources,
            NorvesLib::Core::Delegate<void, const TextureAssetLoadPlan&> barrier)
        {
            GetTextureRuntime(resources).m_AsyncPlanBuiltBarrierForTesting = std::move(barrier);
        }

        static void ClearPlanBarrier(RenderResources& resources)
        {
            GetTextureRuntime(resources).m_AsyncPlanBuiltBarrierForTesting = {};
        }

        static bool IsTextureMutexHeld(RenderResources& resources)
        {
            TextureAssetRuntime& runtime = GetTextureRuntime(resources);
            if (runtime.m_TextureAssetMutex.TryLock())
            {
                runtime.m_TextureAssetMutex.Unlock();
                return false;
            }
            return true;
        }

        static SnapshotState ObserveSnapshot(RenderResources& resources)
        {
            TextureAssetRuntime& runtime = GetTextureRuntime(resources);
            Thread::ScopedLock lock(runtime.m_TextureAssetMutex);
            TextureAssetResolver& resolver = runtime.GetTextureAssetResolverLocked();
            return {resolver.m_System.get(), resolver.m_Generation};
        }

        static TextureHandle SeedCurrentCache(RenderResources& resources,
                                              const Container::String& logicalPath)
        {
            TextureAssetRuntime& runtime = GetTextureRuntime(resources);
            Thread::ScopedLock lock(runtime.m_TextureAssetMutex);
            const TextureAssetLoadPlan plan =
                runtime.GetTextureAssetResolverLocked().BuildTextureLoadPlan(logicalPath);
            assert(plan.bPathValid);
            assert(runtime.m_TextureHandleCache);
            const TextureHandle handle{0x6b6b6b6bull};
            runtime.m_TextureHandleCache->Store(plan.CacheKey, handle);
            return handle;
        }
    };
} // namespace NorvesLib::Core::Rendering

namespace
{
    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::CommandListPtr CreateCommandList() override
        {
            return {};
        }

        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(
            const NorvesLib::RHI::GraphicsPipelineDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::PipelinePtr CreateComputePipeline(
            const NorvesLib::RHI::ComputePipelineDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(
            const NorvesLib::RHI::DescriptorSetDesc&) override
        {
            return {};
        }

        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return {};
        }

        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override
        {
        }

        NorvesLib::RHI::API GetAPI() const override
        {
            return NorvesLib::RHI::API::None;
        }

        const NorvesLib::RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return m_Capabilities;
        }
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

    struct PlanObservation
    {
        const Asset::AssetSystem* pSnapshot = nullptr;
        Container::String RequestPath;
        Container::String ResolvedPath;
        Container::String CacheKey;
        uint64_t Generation = 0;
    };

    class PlanBarrier final
    {
    public:
        void Enter(const Rendering::TextureAssetLoadPlan& plan)
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            Observed = {plan.AssetSystem.get(), plan.RequestPath, plan.ResolvedPath, plan.CacheKey, plan.Generation};
            m_bEntered = true;
            m_Condition.notify_all();
            m_Condition.wait(lock, [this]()
            {
                return m_bReleased;
            });
        }

        void WaitUntilEntered()
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Condition.wait(lock, [this]()
            {
                return m_bEntered;
            });
        }

        void Release()
        {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_bReleased = true;
            }
            m_Condition.notify_all();
        }

        PlanObservation Observed;

    private:
        std::mutex m_Mutex;
        std::condition_variable m_Condition;
        bool m_bEntered = false;
        bool m_bReleased = false;
    };

    class CallLatch final
    {
    public:
        void Wait()
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_bWaiting = true;
            m_Condition.notify_all();
            m_Condition.wait(lock, [this]()
            {
                return m_bReleased;
            });
        }

        void WaitUntilWaiting()
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Condition.wait(lock, [this]()
            {
                return m_bWaiting;
            });
        }

        void Release()
        {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_bReleased = true;
            }
            m_Condition.notify_all();
        }

    private:
        std::mutex m_Mutex;
        std::condition_variable m_Condition;
        bool m_bWaiting = false;
        bool m_bReleased = false;
    };
}

int main()
{
    const Container::String logicalPath("Textures/Admission.nvtex");
    auto candidateA = Container::MakeShared<Asset::AssetSystem>(Container::AnsiString("snapshot-A"));
    auto candidateB = Container::MakeShared<Asset::AssetSystem>(Container::AnsiString("snapshot-B"));
    Container::TSharedPtr<const Asset::AssetSystem> immutableA = candidateA;
    Container::TSharedPtr<const Asset::AssetSystem> immutableB = candidateB;

    NorvesLib::Thread::JobSystem::Get().Initialize(2, NorvesLib::Thread::JobSystem::EXECUTION_SIMPLE);
    Rendering::RenderResources resources;
    assert(resources.Initialize(Container::MakeShared<FakeDevice>()));
    assert(resources.ReloadAssetRuntimeSnapshot("snapshot-A", immutableA));
    const auto stateA = Rendering::AssetRuntimeSnapshotReloadTestAccess::ObserveSnapshot(resources);

    PlanBarrier barrierA;
    Rendering::AssetRuntimeSnapshotReloadTestAccess::SetPlanBarrier(
        resources,
        [&barrierA](const Rendering::TextureAssetLoadPlan& plan)
        {
            barrierA.Enter(plan);
        });

    std::atomic<uint32_t> callbackCountA{0};
    uint32_t requestA = 0;
    std::thread requestThreadA([&]()
    {
        requestA = resources.Textures().LoadTextureAsync(
            logicalPath,
            [&callbackCountA](Rendering::TextureHandle)
            {
                callbackCountA.fetch_add(1);
            });
    });
    barrierA.WaitUntilEntered();
    assert(Rendering::AssetRuntimeSnapshotReloadTestAccess::IsTextureMutexHeld(resources));
    assert(barrierA.Observed.pSnapshot == candidateA.get());
    assert(barrierA.Observed.RequestPath == logicalPath);
    assert(barrierA.Observed.Generation == stateA.Generation);

    std::atomic<bool> bReloadStarted{false};
    std::atomic<bool> bReloadCompleted{false};
    bool bReloadAccepted = true;
    std::thread reloadThread([&]()
    {
        bReloadStarted.store(true);
        bReloadAccepted = resources.ReloadAssetRuntimeSnapshot("snapshot-B", immutableB);
        bReloadCompleted.store(true);
    });
    while (!bReloadStarted.load())
    {
        std::this_thread::yield();
    }
    assert(!bReloadCompleted.load());

    barrierA.Release();
    requestThreadA.join();
    reloadThread.join();
    Rendering::AssetRuntimeSnapshotReloadTestAccess::ClearPlanBarrier(resources);
    assert(requestA != 0);
    assert(!bReloadAccepted);
    const auto stateAfterRejectedB = Rendering::AssetRuntimeSnapshotReloadTestAccess::ObserveSnapshot(resources);
    assert(stateAfterRejectedB.pSnapshot == candidateA.get());
    assert(stateAfterRejectedB.Generation == stateA.Generation);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.Textures().FlushCompletedTextureLoads() == 1);
    assert(callbackCountA.load() == 1);
    assert(resources.Textures().GetPendingAsyncLoadCount() == 0);

    CallLatch requestLatchB;
    PlanBarrier observeB;
    Rendering::AssetRuntimeSnapshotReloadTestAccess::SetPlanBarrier(
        resources,
        [&observeB](const Rendering::TextureAssetLoadPlan& plan)
        {
            observeB.Enter(plan);
        });
    uint32_t requestB = 0;
    std::atomic<uint32_t> callbackCountB{0};
    std::thread requestThreadB([&]()
    {
        requestLatchB.Wait();
        requestB = resources.Textures().LoadTextureAsync(
            logicalPath,
            [&callbackCountB](Rendering::TextureHandle)
            {
                callbackCountB.fetch_add(1);
            });
    });
    requestLatchB.WaitUntilWaiting();
    assert(resources.ReloadAssetRuntimeSnapshot("snapshot-B", immutableB));
    const auto stateB = Rendering::AssetRuntimeSnapshotReloadTestAccess::ObserveSnapshot(resources);
    assert(stateB.pSnapshot == candidateB.get());
    assert(stateB.Generation == stateA.Generation + 1);
    requestLatchB.Release();
    observeB.WaitUntilEntered();
    assert(observeB.Observed.pSnapshot == candidateB.get());
    assert(observeB.Observed.RequestPath == logicalPath);
    assert(observeB.Observed.Generation == stateB.Generation);
    observeB.Release();
    requestThreadB.join();
    Rendering::AssetRuntimeSnapshotReloadTestAccess::ClearPlanBarrier(resources);
    assert(requestB != 0);
    NorvesLib::Thread::JobSystem::Get().WaitForAll();
    assert(resources.Textures().FlushCompletedTextureLoads() == 1);
    assert(callbackCountB.load() == 1);
    assert(resources.Textures().GetPendingAsyncLoadCount() == 0);

    const Rendering::TextureHandle cached =
        Rendering::AssetRuntimeSnapshotReloadTestAccess::SeedCurrentCache(resources, logicalPath);
    bool bCallbackRan = false;
    bool bNestedMutationSucceeded = false;
    const uint32_t cacheHitRequest = resources.Textures().LoadTextureAsync(
        logicalPath,
        [&](Rendering::TextureHandle handle)
        {
            bCallbackRan = true;
            assert(handle == cached);
            bNestedMutationSucceeded = resources.Textures().SetTextureAssetFallbackMode(
                Rendering::TextureAssetFallbackMode::DebugAllowLooseFallback);
        });
    assert(cacheHitRequest == 0);
    assert(bCallbackRan);
    assert(bNestedMutationSucceeded);

    resources.Shutdown();
    NorvesLib::Thread::JobSystem::Get().Shutdown();
    std::cout << "TextureAssetReloadAdmissionTest passed" << std::endl;
    return 0;
}
