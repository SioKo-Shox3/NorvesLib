#include "Rendering/FramePacket.h"
#include "Rendering/RenderThread.h"
#include "Rendering/RenderingCoordinator.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

namespace NorvesLib::Core::Rendering
{
    struct RenderThreadFrameCompletionTestAccess
    {
        static void SetRenderFrameTestHook(RenderThread& renderThread, void (*hook)(FramePacket*))
        {
            renderThread.m_RenderFrameTestHook = hook;
        }

        static bool IsFrameIdlePredicateSatisfied(RenderThread& renderThread)
        {
            Thread::ScopedLock lock(renderThread.m_FrameMutex);
            return renderThread.m_bFrameComplete.Load(std::memory_order_acquire) &&
                   !renderThread.m_bNewFrameReady.Load(std::memory_order_acquire) &&
                   renderThread.m_CurrentPacket == nullptr &&
                   !renderThread.m_bAssetGpuFlushWindowRequested;
        }
    };
} // namespace NorvesLib::Core::Rendering

namespace
{
    namespace Rendering = NorvesLib::Core::Rendering;

    class RenderFrameLatch final
    {
    public:
        void Enter()
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            ++m_EnteredCount;
            m_Condition.notify_all();
            m_Condition.wait(lock, [this]()
            {
                return m_ReleasedCount >= m_EnteredCount;
            });
        }

        bool WaitUntilEntered(uint32_t expectedCount)
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            return m_Condition.wait_for(lock, std::chrono::seconds(5), [this, expectedCount]()
            {
                return m_EnteredCount >= expectedCount;
            });
        }

        void ReleaseNext()
        {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                ++m_ReleasedCount;
            }
            m_Condition.notify_all();
        }

        void ReleaseAll()
        {
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_ReleasedCount = UINT32_MAX;
            }
            m_Condition.notify_all();
        }

    private:
        std::mutex m_Mutex;
        std::condition_variable m_Condition;
        uint32_t m_EnteredCount = 0;
        uint32_t m_ReleasedCount = 0;
    };

    RenderFrameLatch* g_RenderFrameLatch = nullptr;
    std::atomic<bool> g_bRenderFrameHookContextMissing{false};

    void LatchImmediatelyBeforeRenderFrame(Rendering::FramePacket*)
    {
        if (g_RenderFrameLatch)
        {
            g_RenderFrameLatch->Enter();
        }
        else
        {
            g_bRenderFrameHookContextMissing.store(true, std::memory_order_release);
        }
    }
}

int main()
{
    std::cout << "RenderThreadFrameCompletionTest start\n";

    Rendering::RenderingCoordinator coordinator;
    Rendering::RenderThread renderThread;
    Rendering::FramePacket packetA;
    Rendering::FramePacket packetB;
    packetA.SetState(Rendering::FramePacketState::Ready);
    packetB.SetState(Rendering::FramePacketState::Ready);

    RenderFrameLatch latch;
    g_bRenderFrameHookContextMissing.store(false, std::memory_order_release);
    g_RenderFrameLatch = &latch;
    Rendering::RenderThreadFrameCompletionTestAccess::SetRenderFrameTestHook(
        renderThread, LatchImmediatelyBeforeRenderFrame);

    const bool bInitialized = renderThread.Initialize(&coordinator);
    bool bFirstFrameEntered = false;
    bool bSecondFrameEntered = false;
    bool bIdlePredicateSatisfiedWhileSecondFrameLatched = false;

    if (bInitialized)
    {
        renderThread.Start();
        renderThread.NotifyNewFrame(&packetA);
        bFirstFrameEntered = latch.WaitUntilEntered(1);
        if (bFirstFrameEntered)
        {
            renderThread.NotifyNewFrame(&packetB);
            latch.ReleaseNext();
            bSecondFrameEntered = latch.WaitUntilEntered(2);
            if (bSecondFrameEntered)
            {
                bIdlePredicateSatisfiedWhileSecondFrameLatched =
                    Rendering::RenderThreadFrameCompletionTestAccess::IsFrameIdlePredicateSatisfied(renderThread);
            }
        }
    }

    latch.ReleaseAll();
    renderThread.Shutdown();
    Rendering::RenderThreadFrameCompletionTestAccess::SetRenderFrameTestHook(renderThread, nullptr);
    g_RenderFrameLatch = nullptr;

    const bool bHookContextMissing = g_bRenderFrameHookContextMissing.load(std::memory_order_acquire);
    const bool bPassed = bInitialized &&
                         bFirstFrameEntered &&
                         bSecondFrameEntered &&
                         !bHookContextMissing &&
                         !bIdlePredicateSatisfiedWhileSecondFrameLatched;
    if (!bPassed)
    {
        std::cerr << "RenderThreadFrameCompletionTest failure: initialized=" << bInitialized
                  << " first_frame_entered=" << bFirstFrameEntered
                  << " second_frame_entered=" << bSecondFrameEntered
                  << " hook_context_missing=" << bHookContextMissing
                  << " idle_predicate_while_second_latched="
                  << bIdlePredicateSatisfiedWhileSecondFrameLatched << "\n";
        return 1;
    }

    std::cout << "RenderThreadFrameCompletionTest passed\n";
    return 0;
}
