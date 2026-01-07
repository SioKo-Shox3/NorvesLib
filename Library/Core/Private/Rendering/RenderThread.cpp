#include "Rendering/RenderThread.h"
#include "Rendering/RenderingCoordinator.h"
#include <chrono>

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // RenderThread
    // ========================================

    RenderThread::~RenderThread()
    {
        Shutdown();
    }

    bool RenderThread::Initialize(RenderingCoordinator *coordinator)
    {
        if (!coordinator)
        {
            return false;
        }

        m_Coordinator = coordinator;
        m_State.Store(static_cast<uint8_t>(RenderThreadState::Stopped), std::memory_order_release);
        return true;
    }

    void RenderThread::Start()
    {
        auto currentState = GetState();
        if (currentState != RenderThreadState::Stopped)
        {
            return;
        }

        m_State.Store(static_cast<uint8_t>(RenderThreadState::Starting), std::memory_order_release);
        m_bShouldExit.Store(false, std::memory_order_release);
        m_bNewFrameReady.Store(false, std::memory_order_release);
        m_bFrameComplete.Store(true, std::memory_order_release);

        // スレッドを作成して開始
        m_Thread = Container::MakeUnique<Thread::Thread>([this]()
                                                         { RenderLoop(); });

        m_State.Store(static_cast<uint8_t>(RenderThreadState::Running), std::memory_order_release);
    }

    void RenderThread::Stop()
    {
        auto currentState = GetState();
        if (currentState != RenderThreadState::Running)
        {
            return;
        }

        m_State.Store(static_cast<uint8_t>(RenderThreadState::Stopping), std::memory_order_release);

        // 終了フラグを設定
        m_bShouldExit.Store(true, std::memory_order_release);

        // 待機解除のため新フレームを通知
        {
            Thread::ScopedLock lock(m_FrameMutex);
            m_bNewFrameReady.Store(true, std::memory_order_release);
        }
        m_FrameCondition.NotifyOne();

        // スレッド終了を待機
        if (m_Thread && m_Thread->Joinable())
        {
            m_Thread->Join();
        }

        m_Thread.reset();
        m_State.Store(static_cast<uint8_t>(RenderThreadState::Stopped), std::memory_order_release);
    }

    void RenderThread::Shutdown()
    {
        Stop();
        m_Coordinator = nullptr;
    }

    void RenderThread::WaitForFrame()
    {
        // フレーム完了を待機
        m_FrameMutex.Lock();
        m_IdleCondition.Wait(m_FrameMutex,
                             [this]()
                             {
                                 return m_bFrameComplete.Load(std::memory_order_acquire);
                             });
        m_FrameMutex.Unlock();
    }

    void RenderThread::WaitForIdle()
    {
        WaitForFrame();
    }

    void RenderThread::NotifyNewFrame(FramePacket *packet)
    {
        m_FrameMutex.Lock();
        m_CurrentPacket = packet;
        m_bNewFrameReady.Store(true, std::memory_order_release);
        m_bFrameComplete.Store(false, std::memory_order_release);
        m_FrameMutex.Unlock();

        m_FrameCondition.NotifyOne();
    }

    void RenderThread::RenderLoop()
    {
        while (!m_bShouldExit.Load(std::memory_order_acquire))
        {
            // 新しいフレームの待機
            m_FrameMutex.Lock();
            m_FrameCondition.Wait(m_FrameMutex,
                                  [this]()
                                  {
                                      return m_bNewFrameReady.Load(std::memory_order_acquire) ||
                                             m_bShouldExit.Load(std::memory_order_acquire);
                                  });

            if (m_bShouldExit.Load(std::memory_order_acquire))
            {
                m_FrameMutex.Unlock();
                break;
            }

            // フレーム準備完了フラグをクリア
            m_bNewFrameReady.Store(false, std::memory_order_release);
            FramePacket *packet = m_CurrentPacket;
            m_FrameMutex.Unlock();

            // レンダリング実行
            auto startTime = std::chrono::high_resolution_clock::now();

            if (m_Coordinator && packet)
            {
                m_Coordinator->RenderFrame(packet);
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

            // 統計を更新
            m_Stats.FramesRendered++;
            m_Stats.FrameTimeMs = frameTime;

            // フレーム完了を通知
            m_FrameMutex.Lock();
            m_bFrameComplete.Store(true, std::memory_order_release);
            m_FrameMutex.Unlock();

            m_IdleCondition.NotifyAll();
        }
    }

} // namespace NorvesLib::Core::Rendering
