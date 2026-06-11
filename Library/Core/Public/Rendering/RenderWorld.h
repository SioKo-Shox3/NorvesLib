#pragma once

#include "RenderTypes.h"
#include "RenderingCoordinator.h"
#include "RenderThread.h"
#include "RenderResources.h"
#include "RHI/RHITypes.h"
#include "Platform/NativeWindowHandle.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // レンダリングシステム設定
    // ========================================

    /**
     * @brief レンダリングシステム初期化設定
     */
    struct RenderWorldSettings
    {
        // RHIデバイス（エンジン層で初期化済み）
        RHI::DevicePtr Device;

        // ウィンドウハンドル
        Platform::NativeWindowHandle WindowHandle;

        // 解像度
        uint32_t Width = 1280;
        uint32_t Height = 720;
        float RenderScale = 1.0f;

        // フレームバッファ設定
        uint32_t BackBufferCount = 2;
        bool bVSync = true;
        bool bFullscreen = false;

        // レンダリング設定
        bool bEnableMultiThreadedRendering = true;
        uint32_t MaxDrawCallsPerFrame = 10000;

        // デバッグ設定
        bool bEnableValidation = false;
        bool bEnableGPUDebug = false;
    };

    // ========================================
    // RenderWorld
    // ========================================

    /**
     * @brief レンダリングワールド
     *
     * レンダリングシステム全体を統括するメインクラス。
     * Game側からの唯一のエントリポイントとして機能します。
     *
     * 責務:
     * - RHIの初期化と管理
     * - リソースマネージャーの初期化
     * - FramePacket管理
     * - RenderThreadの制御
     * - SceneCollectorとの連携
     */
    class RenderWorld
    {
    public:
        /**
         * @brief コンストラクタ
         */
        /**
         * @brief コンストラクタ
         */
        RenderWorld() = default;

        /**
         * @brief デストラクタ
         */
        ~RenderWorld() = default;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param settings 初期化設定
         * @return 初期化成功時true
         */
        bool Initialize(const RenderWorldSettings &settings);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // フレーム処理
        // ========================================

        /**
         * @brief フレーム開始（GameThread）
         *
         * 新しいフレームの描画データ収集を開始します。
         * FramePacketを取得し、書き込み準備を行います。
         */
        void BeginFrame();

        /**
         * @brief 描画実行
         *
         * RenderingCoordinator経由でコマンドを録画します。
         * 現在はテスト三角形を描画。将来的にはDrawCommand経由に移行します。
         */
        void Render();

        /**
         * @brief シーン収集（GameThread）
         *
         * 登録されたオブジェクトからMeshProxy等を収集し、
         * 現在のFramePacketに書き込みます。
         */
        void CollectScene();

        /**
         * @brief カメラを設定（GameThread）
         * @param camera メインカメラ情報
         */
        void SetMainCamera(const CameraProxy &camera);

        /**
         * @brief フレーム終了（GameThread）
         *
         * FramePacketを完了状態にし、RenderThreadに通知します。
         */
        void EndFrame();

        /**
         * @brief フレームの描画完了を待機
         */
        void WaitForRender();

        // ========================================
        // リソースアクセス
        // ========================================

        /**
         * @brief レンダリングリソースを取得
         */
        RenderResources &GetRenderResources()
        {
            return m_RenderResources;
        }

        const RenderResources &GetRenderResources() const
        {
            return m_RenderResources;
        }

        /**
         * @brief レンダリングコーディネーターを取得
         */
        RenderingCoordinator &GetRenderingCoordinator()
        {
            return m_RenderingCoordinator;
        }
        const RenderingCoordinator &GetRenderingCoordinator() const
        {
            return m_RenderingCoordinator;
        }

        // ========================================
        // 解像度変更
        // ========================================

        /**
         * @brief 解像度を変更
         * @param width 新しい幅
         * @param height 新しい高さ
         *
         * 初期化前または同サイズの場合は無視します。
         * Render中に呼ばれた場合はBeginFrame時に安全に適用します。
         */
        void Resize(uint32_t width, uint32_t height);

        /**
         * @brief 現在の解像度を取得
         */
        void GetResolution(uint32_t &outWidth, uint32_t &outHeight) const
        {
            outWidth = m_PendingWidth.Load(std::memory_order_acquire);
            outHeight = m_PendingHeight.Load(std::memory_order_acquire);
        }

        /**
         * @brief 内部描画スケールを設定
         * @param renderScale 0.5〜1.0 の描画スケール
         */
        void SetRenderScale(float renderScale);

        /**
         * @brief 内部描画スケールを取得
         */
        float GetRenderScale() const { return m_RenderScale; }

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief VSync設定
         */
        void SetVSync(bool bEnabled);
        bool IsVSyncEnabled() const { return m_bVSyncEnabled; }

        /**
         * @brief フルスクリーン設定
         */
        void SetFullscreen(bool bEnabled);
        bool IsFullscreen() const { return m_bFullscreen; }

        // ========================================
        // 統計
        // ========================================

        /**
         * @brief レンダリング統計
         */
        struct RenderingStats
        {
            // フレーム情報
            uint64_t FrameNumber = 0;
            float DeltaTime = 0.0f;
            float FPS = 0.0f;

            // 描画統計
            uint32_t DrawCalls = 0;
            uint32_t TrianglesRendered = 0;
            uint32_t VisibleObjects = 0;

            // メモリ統計
            size_t GPUMemoryUsed = 0;
            size_t CPUMemoryUsed = 0;

            // タイミング
            float GameThreadTimeMs = 0.0f;
            float RenderThreadTimeMs = 0.0f;
            float GPUTimeMs = 0.0f;
        };

        /**
         * @brief 統計を取得
         */
        const RenderingStats &GetStats() const { return m_Stats; }

        // ========================================
        // デバッグ
        // ========================================

        /**
         * @brief ワイヤーフレームモード切り替え
         */
        void SetWireframeMode(bool bEnabled) { m_bWireframeMode = bEnabled; }
        bool IsWireframeMode() const { return m_bWireframeMode; }

        /**
         * @brief バウンディングボックス表示切り替え
         */
        void SetShowBoundingBoxes(bool bEnabled) { m_bShowBoundingBoxes = bEnabled; }
        bool IsShowBoundingBoxes() const { return m_bShowBoundingBoxes; }

    private:
        // コピー・ムーブ禁止
        RenderWorld(const RenderWorld &) = delete;
        RenderWorld &operator=(const RenderWorld &) = delete;

        // ========================================
        // メンバ変数
        // ========================================

        // RHIデバイス（Engine層から受け取り）
        Container::TSharedPtr<RHI::IDevice> m_Device;

        // レンダリングコーディネーター（描画フロー管理）
        RenderingCoordinator m_RenderingCoordinator;

        // サブシステム（GEngine配下で実体保持）
        RenderResources m_RenderResources;
        RenderThread m_RenderThread;

        // 解像度
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;

        // 設定
        bool m_bVSyncEnabled = true;
        bool m_bFullscreen = false;
        float m_RenderScale = 1.0f;
        bool m_bMultiThreadedRendering = true;

        // デバッグ設定
        bool m_bWireframeMode = false;
        bool m_bShowBoundingBoxes = false;

        // 統計
        RenderingStats m_Stats;

        // フレームタイミング
        double m_LastFrameTime = 0.0;
        double m_TotalTime = 0.0;

        // 初期化フラグ
        bool m_bInitialized = false;

        // ========================================
        // リサイズ保留
        // ========================================

        // Render中にResizeが呼ばれた場合はBeginFrame冒頭で安全に適用する
        Thread::Atomic<bool> m_bResizePending{false};
        Thread::Atomic<uint32_t> m_PendingWidth{0};
        Thread::Atomic<uint32_t> m_PendingHeight{0};
    };

} // namespace NorvesLib::Core::Rendering
