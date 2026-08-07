#include "Rendering/RenderThread.h"
#include "Rendering/RenderingCoordinator.h"
#include "Engine/Engine.h"
#include "RHI/IDevice.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <chrono>
#include <exception>

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
        m_Stats = ThreadStats{};
        m_PublishedFramesRendered.Store(0, std::memory_order_release);
        m_bAssetGpuFlushWindowRequested = false;
        m_bAssetGpuFlushWindowReady = false;
        m_bReportAssetGpuFlushWindowResume = false;
        m_AssetGpuFlushWindowId = 0;
        m_AssetGpuFlushWindowReadyFrames = 0;
        m_AssetGpuFlushWindowResumeId = 0;
        m_AssetGpuFlushWindowResumeReadyFrames = 0;

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
        m_bAssetGpuFlushWindowRequested = false;
        m_bAssetGpuFlushWindowReady = false;
        m_bReportAssetGpuFlushWindowResume = false;
        m_AssetGpuFlushWindowId = 0;
        m_AssetGpuFlushWindowReadyFrames = 0;
        m_AssetGpuFlushWindowResumeId = 0;
        m_AssetGpuFlushWindowResumeReadyFrames = 0;
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

    bool RenderThread::TryAcquireAssetGpuFlushWindow()
    {
        if (!m_FrameMutex.TryLock())
        {
            return false;
        }

        const bool bRenderThreadIdle =
            m_bFrameComplete.Load(std::memory_order_acquire) &&
            !m_bNewFrameReady.Load(std::memory_order_acquire) &&
            m_CurrentPacket == nullptr;
        if (m_bAssetGpuFlushWindowReady && bRenderThreadIdle)
        {
            m_bAssetGpuFlushWindowReady = false;
            m_bReportAssetGpuFlushWindowResume = true;
            m_AssetGpuFlushWindowResumeId = m_AssetGpuFlushWindowId;
            m_AssetGpuFlushWindowResumeReadyFrames = m_AssetGpuFlushWindowReadyFrames;
            m_FrameMutex.Unlock();
            return true;
        }

        bool bNotifyRenderThread = false;
        if (!m_bAssetGpuFlushWindowReady && !m_bAssetGpuFlushWindowRequested)
        {
            m_bAssetGpuFlushWindowRequested = true;
            bNotifyRenderThread = true;
        }
        m_FrameMutex.Unlock();

        if (bNotifyRenderThread)
        {
            m_FrameCondition.NotifyOne();
        }
        return false;
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
                                         m_CurrentPacket == nullptr &&
                                         !m_bAssetGpuFlushWindowRequested) ||
                                        m_bShouldExit.Load(std::memory_order_acquire));
                             });
        m_FrameMutex.Unlock();
    }

    void RenderThread::WaitForIdle()
    {
        WaitForFrame();
    }

    void RenderThread::NotifyNewFrame(FramePacket* packet)
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
        m_bAssetGpuFlushWindowReady = false;
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
                                             m_bAssetGpuFlushWindowRequested ||
                                             m_bShouldExit.Load(std::memory_order_acquire);
                                  });

            if (m_bShouldExit.Load(std::memory_order_acquire))
            {
                m_FrameMutex.Unlock();
                break;
            }

            FramePacket* packet = nullptr;
            bool bDrainAssetGpuFlushWindow = false;
            if (m_bNewFrameReady.Load(std::memory_order_acquire))
            {
                packet = m_CurrentPacket;
                m_CurrentPacket = nullptr;
                m_bNewFrameReady.Store(false, std::memory_order_release);
                if (packet)
                {
                    m_bFrameComplete.Store(false, std::memory_order_release);
                }
            }
            else if (m_bAssetGpuFlushWindowRequested)
            {
                bDrainAssetGpuFlushWindow = true;
            }
            m_FrameMutex.Unlock();

            if (bDrainAssetGpuFlushWindow)
            {
                Container::TSharedPtr<RHI::IDevice> device;
                if (m_Coordinator)
                {
                    device = m_Coordinator->GetDevice();
                }

                if (device)
                {
                    device->WaitIdle();
                }

                bool bWindowReady = false;
                uint64_t windowId = 0;
                uint64_t readyFrames = 0;
                m_FrameMutex.Lock();
                if (!m_bShouldExit.Load(std::memory_order_acquire) && device)
                {
                    m_bAssetGpuFlushWindowRequested = false;
                    m_bAssetGpuFlushWindowReady = true;
                    ++m_AssetGpuFlushWindowId;
                    m_AssetGpuFlushWindowReadyFrames = m_Stats.FramesRendered;
                    windowId = m_AssetGpuFlushWindowId;
                    readyFrames = m_AssetGpuFlushWindowReadyFrames;
                    bWindowReady = true;
                }
                else
                {
                    m_bAssetGpuFlushWindowRequested = false;
                }
                m_FrameMutex.Unlock();

                if (bWindowReady)
                {
                    NORVES_LOG_INFO(
                        "Rendering",
                        "stage=asset_gpu_flush_window_ready role=render_thread window_id=%llu frames_rendered=%llu success=1",
                        static_cast<unsigned long long>(windowId),
                        static_cast<unsigned long long>(readyFrames));
                }
                m_IdleCondition.NotifyAll();
                continue;
            }

            // レンダリング実行
            if (m_Coordinator && packet)
            {
                auto recyclePacket = [this](FramePacket* packetToRecycle)
                {
                    if (!packetToRecycle)
                    {
                        return;
                    }
                    if (packetToRecycle->CompareExchangeState(FramePacketState::Queued,
                                                               FramePacketState::Recycling) ||
                        packetToRecycle->CompareExchangeState(FramePacketState::Ready,
                                                               FramePacketState::Recycling))
                    {
                        packetToRecycle->Clear();
                        packetToRecycle->SetState(FramePacketState::Empty);
                        return;
                    }
                    m_Coordinator->ReleasePacket(packetToRecycle);
                };
                auto handleFatalRenderThreadFailure = [this, packet, &recyclePacket](const char* message)
                {
                    NORVES_LOG_ERROR("Rendering",
                                     "RenderThread stopped after an unhandled exception: %s",
                                     message);
                    m_bShouldExit.Store(true, std::memory_order_release);
                    recyclePacket(packet);

                    m_FrameMutex.Lock();
                    FramePacket* pendingPacket = m_CurrentPacket;
                    m_CurrentPacket = nullptr;
                    m_bNewFrameReady.Store(false, std::memory_order_release);
                    m_bFrameComplete.Store(true, std::memory_order_release);
                    m_bAssetGpuFlushWindowRequested = false;
                    m_bAssetGpuFlushWindowReady = false;
                    m_FrameMutex.Unlock();
                    recyclePacket(pendingPacket);

                    if (Engine::GEngine)
                    {
                        Engine::GEngine->RequestExit(1);
                    }
                    m_IdleCondition.NotifyAll();
                };

                try
                {
#if NORVES_ENABLE_STATS
                    auto& statsManager = NorvesLib::Debug::StatsManager::Get();
                    const bool bTraceActive = statsManager.IsTraceActive();
                    std::chrono::high_resolution_clock::time_point startTime;
                    if (bTraceActive)
                    {
                        startTime = std::chrono::high_resolution_clock::now();
                    }
#endif

                    if (m_RenderFrameTestHook)
                    {
                        m_RenderFrameTestHook(packet);
                    }

                    m_Coordinator->RenderFrame(packet);
                    // 描画完了後にパケットをEmpty状態に戻して再利用可能にする
                    m_Coordinator->ReleasePacket(packet);

#if NORVES_ENABLE_STATS
                    // 統計を更新
                    m_Stats.FramesRendered++;
                    m_PublishedFramesRendered.Store(m_Stats.FramesRendered, std::memory_order_release);
                    if (bTraceActive)
                    {
                        auto endTime = std::chrono::high_resolution_clock::now();
                        float frameTime = std::chrono::duration<float, std::milli>(endTime - startTime).count();
                        m_Stats.FrameTimeMs = frameTime;
                        statsManager.SetRenderThreadTimeMs(frameTime);
                    }
#else
                    m_Stats.FramesRendered++;
                    m_PublishedFramesRendered.Store(m_Stats.FramesRendered, std::memory_order_release);
#endif

                    bool bReportResume = false;
                    uint64_t resumeWindowId = 0;
                    uint64_t resumeReadyFrames = 0;
                    m_FrameMutex.Lock();
                    if (m_bReportAssetGpuFlushWindowResume)
                    {
                        m_bReportAssetGpuFlushWindowResume = false;
                        resumeWindowId = m_AssetGpuFlushWindowResumeId;
                        resumeReadyFrames = m_AssetGpuFlushWindowResumeReadyFrames;
                        bReportResume = true;
                    }
                    m_FrameMutex.Unlock();

                    if (bReportResume)
                    {
                        NORVES_LOG_INFO(
                            "Rendering",
                            "stage=asset_gpu_flush_window_resumed role=render_thread window_id=%llu ready_frames=%llu frames_rendered=%llu success=1",
                            static_cast<unsigned long long>(resumeWindowId),
                            static_cast<unsigned long long>(resumeReadyFrames),
                            static_cast<unsigned long long>(m_Stats.FramesRendered));
                    }
                }
                catch (const std::exception& exception)
                {
                    handleFatalRenderThreadFailure(exception.what());
                }
                catch (...)
                {
                    handleFatalRenderThreadFailure("unknown exception");
                }
            }

            // フレーム完了を通知
            m_FrameMutex.Lock();
            m_bFrameComplete.Store(true, std::memory_order_release);
            m_FrameMutex.Unlock();

            m_IdleCondition.NotifyAll();
        }
    }

} // namespace NorvesLib::Core::Rendering
