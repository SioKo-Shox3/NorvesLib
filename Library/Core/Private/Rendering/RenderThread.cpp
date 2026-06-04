#include "Rendering/RenderThread.h"
#include "Rendering/RenderingCoordinator.h"
#include "Debug/Stats.h"
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
        Thread::ScopedLock lock(m_FrameMutex);

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
        m_FrameMutex.Lock();

        auto currentState = GetState();
        if (currentState != RenderThreadState::Running &&
            currentState != RenderThreadState::Starting)
        {
            m_FrameMutex.Unlock();
            return;
        }

        m_State.Store(static_cast<uint8_t>(RenderThreadState::Stopping), std::memory_order_release);

        // 終了フラグを設定
        m_bShouldExit.Store(true, std::memory_order_release);

        if (m_CurrentPacket &&
            m_CurrentPacket->CompareExchangeState(FramePacketState::Queued,
                                                  FramePacketState::Recycling))
        {
            m_CurrentPacket->Clear();
            m_CurrentPacket->SetState(FramePacketState::Empty);
        }

        // RenderLoop待機とWaitForIdle待機の両方を解除
        m_CurrentPacket = nullptr;
        m_bNewFrameReady.Store(false, std::memory_order_release);
        m_bFrameComplete.Store(true, std::memory_order_release);
        m_FrameMutex.Unlock();

        m_FrameCondition.NotifyOne();
        m_IdleCondition.NotifyAll();

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
        // 実行中フレームがなく、かつ保留中フレームもない状態を待機する
        m_FrameMutex.Lock();
        m_IdleCondition.Wait(m_FrameMutex,
                             [this]()
                             {
                                 return ((m_bFrameComplete.Load(std::memory_order_acquire) &&
                                         !m_bNewFrameReady.Load(std::memory_order_acquire) &&
                                         m_CurrentPacket == nullptr) ||
                                        m_bShouldExit.Load(std::memory_order_acquire));
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

        if (m_CurrentPacket && m_CurrentPacket != packet)
        {
            if (m_CurrentPacket->CompareExchangeState(FramePacketState::Queued,
                                                      FramePacketState::Recycling))
            {
                m_CurrentPacket->Clear();
                m_CurrentPacket->SetState(FramePacketState::Empty);
            }
        }

        if (packet)
        {
            packet->CompareExchangeState(FramePacketState::Ready, FramePacketState::Queued);
        }

        m_CurrentPacket = packet;
        m_bNewFrameReady.Store(packet != nullptr, std::memory_order_release);
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

            FramePacket *packet = m_CurrentPacket;
            m_CurrentPacket = nullptr;
            m_bNewFrameReady.Store(false, std::memory_order_release);
            m_FrameMutex.Unlock();

            // レンダリング実行
#if NORVES_ENABLE_STATS
            auto startTime = std::chrono::high_resolution_clock::now();
#endif

            if (m_Coordinator && packet)
            {
                m_Coordinator->RenderFrame(packet);
                // 描画完了後にパケットをEmpty状態に戻して再利用可能にする
                m_Coordinator->ReleasePacket(packet);
            }

#if NORVES_ENABLE_STATS
            auto endTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();

            // 統計を更新
            m_Stats.FramesRendered++;
            m_Stats.FrameTimeMs = frameTime;
            NorvesLib::Debug::StatsManager::Get().SetRenderThreadTimeMs(frameTime);
#else
            m_Stats.FramesRendered++;
#endif

            // フレーム完了を通知
            m_FrameMutex.Lock();
            m_bFrameComplete.Store(true, std::memory_order_release);
            m_FrameMutex.Unlock();

            m_IdleCondition.NotifyAll();
        }
    }

} // namespace NorvesLib::Core::Rendering
