#pragma once

#include "Container/PointerTypes.h"
#include "Thread/Thread.h"
#include "Thread/Mutex.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Atomic.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class RenderingCoordinator;
    struct FramePacket;

    /**
     * @brief レンダースレッド状態
     */
    enum class RenderThreadState : uint8_t
    {
        Stopped,    // 停止中
        Starting,   // 開始中
        Running,    // 実行中
        Stopping    // 停止処理中
    };

    /**
     * @brief レンダースレッド（シンプル版）
     *
     * 純粋なスレッド管理のみを担当します。
     * レンダリングロジックはRenderingCoordinatorが持ちます。
     *
     * 責務:
     * - レンダースレッドの起動・停止
     * - フレーム同期
     * - RenderingCoordinatorへのRenderFrame呼び出し
     */
    class RenderThread
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        RenderThread() = default;

        /**
         * @brief デストラクタ
         */
        ~RenderThread();

        // コピー・ムーブ禁止
        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param coordinator レンダリングコーディネーター
         * @return 初期化成功時true
         */
        bool Initialize(RenderingCoordinator* coordinator);

        /**
         * @brief レンダースレッドを開始
         */
        void Start();

        /**
         * @brief レンダースレッドを停止
         */
        void Stop();

        /**
         * @brief 終了処理
         */
        void Shutdown();

        // ========================================
        // 状態
        // ========================================

        /**
         * @brief 現在の状態を取得
         */
        RenderThreadState GetState() const
        {
            return static_cast<RenderThreadState>(m_State.Load(std::memory_order_acquire));
        }

        /**
         * @brief 実行中かどうか
         */
        bool IsRunning() const
        {
            return GetState() == RenderThreadState::Running;
        }

        // ========================================
        // 同期
        // ========================================

        /**
         * @brief 次のフレームの描画を待機
         */
        void WaitForFrame();

        /**
         * @brief 全フレームの完了を待機
         */
        void WaitForIdle();

        /**
         * @brief 新しいフレームの準備完了を通知
         * @param packet 描画するフレームパケット
         */
        void NotifyNewFrame(FramePacket* packet);

        // ========================================
        // 統計
        // ========================================

        struct ThreadStats
        {
            uint64_t FramesRendered = 0;
            float FrameTimeMs = 0.0f;
            float IdleTimeMs = 0.0f;
        };

        const ThreadStats& GetStats() const { return m_Stats; }

    private:
        // ========================================
        // スレッドエントリポイント
        // ========================================

        /**
         * @brief レンダースレッドメインループ
         */
        void RenderLoop();

    private:
        // RenderingCoordinator（外部参照）
        RenderingCoordinator* m_Coordinator = nullptr;

        // スレッド
        Container::TUniquePtr<Thread::Thread> m_Thread;

        // 状態
        Thread::Atomic<uint8_t> m_State{static_cast<uint8_t>(RenderThreadState::Stopped)};

        // 同期プリミティブ
        Thread::Mutex m_FrameMutex;
        Thread::ConditionVariable m_FrameCondition;
        Thread::ConditionVariable m_IdleCondition;
        Thread::Atomic<bool> m_bNewFrameReady{false};
        Thread::Atomic<bool> m_bShouldExit{false};
        Thread::Atomic<bool> m_bFrameComplete{true};

        // 現在のフレームパケット
        FramePacket* m_CurrentPacket = nullptr;

        // 統計
        ThreadStats m_Stats;
    };

} // namespace NorvesLib::Core::Rendering
