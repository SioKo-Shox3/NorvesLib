#pragma once

#include "RenderTypes.h"
#include "FramePacket.h"
#include "RenderThread.h"
#include "RenderResourceManager.h"
#include "MeshResourceManager.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class ISwapChain;
    class ICommandList;
    class IRenderPass;
    class IFramebuffer;
    class IPipeline;
    class IBuffer;
    class IShader;
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
        void *WindowHandle = nullptr;

        // 解像度
        uint32_t Width = 1280;
        uint32_t Height = 720;

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
         * @brief 三角形を描画（デバッグ/テスト用）
         *
         * RHIを使用してハードコードされた三角形を1つ描画します。
         */
        void RenderTriangle();

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
         * @brief リソースマネージャーを取得
         */
        RenderResourceManager &GetResourceManager()
        {
            return m_ResourceManager;
        }

        /**
         * @brief メッシュリソースマネージャーを取得
         */
        MeshResourceManager &GetMeshResourceManager()
        {
            return m_MeshResourceManager;
        }

        // ========================================
        // 解像度変更
        // ========================================

        /**
         * @brief 解像度を変更
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        void Resize(uint32_t width, uint32_t height);

        /**
         * @brief 現在の解像度を取得
         */
        void GetResolution(uint32_t &outWidth, uint32_t &outHeight) const
        {
            outWidth = m_Width;
            outHeight = m_Height;
        }

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
        // 内部ヘルパー
        // ========================================

        /**
         * @brief スワップチェーンのフレームバッファを作成
         * @return 成功時true
         */
        bool CreateSwapChainFramebuffers();

        // ========================================
        // メンバ変数
        // ========================================

        // RHIリソース
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::TSharedPtr<RHI::ISwapChain> m_SwapChain;
        Container::TSharedPtr<RHI::ICommandList> m_CommandList;

        // 三角形描画用リソース
        Container::TSharedPtr<RHI::IRenderPass> m_TriangleRenderPass;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_SwapChainFramebuffers;
        Container::TSharedPtr<RHI::IPipeline> m_TrianglePipeline;
        Container::TSharedPtr<RHI::IShader> m_TriangleVertexShader;
        Container::TSharedPtr<RHI::IShader> m_TriangleFragmentShader;

        // サブシステム（GEngine配下で実体保持）
        RenderResourceManager m_ResourceManager;
        MeshResourceManager m_MeshResourceManager;
        FramePacketManager m_PacketManager;
        RenderThread m_RenderThread;

        // 現在のフレームパケット
        FramePacket *m_CurrentPacket = nullptr;

        // 解像度
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;

        // 設定
        bool m_bVSyncEnabled = true;
        bool m_bFullscreen = false;
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
    };

} // namespace NorvesLib::Core::Rendering
