#pragma once

#include "RenderTypes.h"
#include "FramePacket.h"
#include "RenderResourceManager.h"
#include "MeshResourceManager.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Thread.h"
#include "Thread/Mutex.h"
#include "Thread/ConditionVariable.h"
#include "Thread/Atomic.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
    class ISwapChain;
}

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // レンダーコマンド
    // ========================================

    /**
     * @brief レンダーコマンドタイプ
     */
    enum class RenderCommandType : uint8_t
    {
        None,
        BeginFrame,
        EndFrame,
        BeginRenderPass,
        EndRenderPass,
        SetViewport,
        SetScissor,
        SetPipeline,
        SetVertexBuffer,
        SetIndexBuffer,
        SetDescriptorSet,
        DrawIndexed,
        Draw,
        DrawInstanced,
        Dispatch,
        CopyBuffer,
        CopyTexture,
        ResourceBarrier,
        Custom
    };

    /**
     * @brief レンダーコマンド基底
     */
    struct RenderCommand
    {
        RenderCommandType Type = RenderCommandType::None;

        RenderCommand() = default;
        explicit RenderCommand(RenderCommandType type) : Type(type) {}
        virtual ~RenderCommand() = default;
    };

    /**
     * @brief 描画コマンド
     */
    struct DrawCommand : public RenderCommand
    {
        MeshDataHandle Mesh;
        uint32_t SubMeshIndex = 0;
        MaterialHandle Material;
        uint32_t InstanceCount = 1;
        uint32_t FirstInstance = 0;

        // トランスフォーム定数バッファ用データ
        float WorldMatrix[16];
        float NormalMatrix[12];  // 3x4行列

        DrawCommand() : RenderCommand(RenderCommandType::DrawIndexed) {}
    };

    // ========================================
    // レンダースレッド状態
    // ========================================

    /**
     * @brief レンダースレッド状態
     */
    enum class RenderThreadState : uint8_t
    {
        Stopped,
        Starting,
        Running,
        Stopping
    };

    // ========================================
    // レンダースレッド
    // ========================================

    /**
     * @brief レンダースレッド
     *
     * 専用スレッドでGPUコマンドの発行とフレームの描画を行います。
     * FramePacketからデータを読み取り、RHIを通じて描画します。
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
        ~RenderThread() = default;

        // コピー・ムーブ禁止
        RenderThread(const RenderThread &) = delete;
        RenderThread &operator=(const RenderThread &) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @param swapChain スワップチェーン
         * @param packetManager FramePacketマネージャー
         * @return 初期化成功時true
         */
        bool Initialize(Container::TSharedPtr<RHI::IDevice> device,
                        Container::TSharedPtr<RHI::ISwapChain> swapChain,
                        FramePacketManager *packetManager);

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

        /**
         * @brief 現在の状態を取得
         */
        RenderThreadState GetState() const
        {
            return static_cast<RenderThreadState>(m_State.load(std::memory_order_acquire));
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
         */
        void NotifyNewFrame();

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief 垂直同期設定
         */
        void SetVSync(bool bEnabled) { m_bVSyncEnabled = bEnabled; }
        bool IsVSyncEnabled() const { return m_bVSyncEnabled; }

        /**
         * @brief フレームレート制限
         */
        void SetTargetFrameRate(float fps) { m_TargetFrameRate = fps; }
        float GetTargetFrameRate() const { return m_TargetFrameRate; }

        // ========================================
        // 統計
        // ========================================

        struct RenderStats
        {
            uint64_t FramesRendered = 0;
            uint64_t DrawCalls = 0;
            uint64_t TrianglesRendered = 0;
            float FrameTimeMs = 0.0f;
            float GPUTimeMs = 0.0f;
            float PresentTimeMs = 0.0f;
        };

        /**
         * @brief レンダリング統計を取得
         */
        const RenderStats &GetStats() const { return m_Stats; }

    private:
        // ========================================
        // スレッドエントリポイント
        // ========================================

        /**
         * @brief レンダースレッドメインループ
         */
        void RenderLoop();

        /**
         * @brief 1フレームを処理
         * @param packet フレームパケット
         */
        void ProcessFrame(FramePacket *packet);

        /**
         * @brief シーンを描画
         * @param scene シーンプロキシ
         */
        void RenderScene(const SceneProxy &scene);

        /**
         * @brief メッシュを描画
         * @param meshProxy メッシュプロキシ
         */
        void RenderMesh(const MeshProxy &meshProxy);

        /**
         * @brief 描画コマンドを生成
         * @param meshProxy メッシュプロキシ
         * @param outCommands 出力先コマンドリスト
         */
        void GenerateDrawCommands(const MeshProxy &meshProxy,
                                   Container::VariableArray<DrawCommand> &outCommands);

        /**
         * @brief コマンドをGPUに発行
         * @param commands コマンドリスト
         */
        void SubmitCommands(const Container::VariableArray<DrawCommand> &commands);

        // ========================================
        // メンバ変数
        // ========================================

        // RHIリソース
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::TSharedPtr<RHI::ISwapChain> m_SwapChain;
        Container::TSharedPtr<RHI::ICommandList> m_CommandList;

        // FramePacketマネージャー
        FramePacketManager *m_PacketManager = nullptr;

        // スレッド
        Container::TUniquePtr<Thread::Thread> m_Thread;

        // 状態
        Thread::Atomic<uint8_t> m_State{static_cast<uint8_t>(RenderThreadState::Stopped)};

        // 同期プリミティブ
        Thread::Mutex m_FrameMutex;
        Thread::ConditionVariable m_FrameCondition;
        Thread::Atomic<bool> m_bNewFrameReady{false};
        Thread::Atomic<bool> m_bShouldExit{false};

        // 設定
        bool m_bVSyncEnabled = true;
        float m_TargetFrameRate = 60.0f;

        // 統計
        RenderStats m_Stats;

        // 描画コマンドバッファ
        Container::VariableArray<DrawCommand> m_DrawCommands;
    };

    // ========================================
    // グローバルアクセス
    // ========================================

    /**
     * @brief グローバルレンダースレッドを取得
     */
    RenderThread &GetRenderThread();

} // namespace NorvesLib::Core::Rendering
