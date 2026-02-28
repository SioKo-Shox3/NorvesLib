#pragma once

#include "Screen.h"
#include "View.h"
#include "SceneView.h"
#include "SceneRenderer.h"
#include "DrawCommand.h"
#include "FramePacket.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Debug/Stats.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
    class ISwapChain;
    class IRenderPass;
    class IFramebuffer;
    class IPipeline;
    class IShader;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class RenderResourceManager;

    /**
     * @brief レンダリング調整設定
     */
    struct RenderingCoordinatorSettings
    {
        // RHIデバイス（RenderWorldから渡される）
        Container::TSharedPtr<RHI::IDevice> Device;

        void *WindowHandle = nullptr;
        uint32_t Width = 1280;
        uint32_t Height = 720;
        uint32_t BackBufferCount = 2;
        bool bVSync = true;
        bool bEnableMultiThreadedRendering = true;
        uint32_t MaxDrawCallsPerFrame = 10000;
        bool bEnableValidation = false;
    };

    /**
     * @brief レンダリングコーディネーター
     *
     * レンダリングシステムの調整・統括を担当します。
     * RenderThreadからレンダリングロジックを分離し、
     * 描画フローの管理を行います。
     *
     * 責務:
     * - Screen/View/Viewportの管理
     * - DrawCommandの発行とサブミット
     * - リソースマネージャーとの連携
     * - フレームパケット管理
     * - 描画統計の収集
     *
     * RenderThreadとの関係:
     * - RenderThreadは純粋なスレッド管理のみ
     * - RenderingCoordinatorが実際の描画ロジックを持つ
     */
    class RenderingCoordinator
    {
    public:
        /**
         * @brief コンストラクタ
         */
        RenderingCoordinator() = default;

        /**
         * @brief デストラクタ
         */
        ~RenderingCoordinator() = default;

        // コピー・ムーブ禁止
        RenderingCoordinator(const RenderingCoordinator &) = delete;
        RenderingCoordinator &operator=(const RenderingCoordinator &) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param settings 設定
         * @return 初期化成功時true
         */
        bool Initialize(const RenderingCoordinatorSettings &settings);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // フレーム処理（GameThread）
        // ========================================

        /**
         * @brief フレーム開始
         *
         * 新しいフレームの描画データ収集を開始します。
         */
        void BeginFrame();

        /**
         * @brief シーン収集
         *
         * 全SceneViewからProxyを収集し、カリング・バッチングを行います。
         */
        void CollectScene();

        /**
         * @brief DrawCommandを生成
         *
         * バッチからDrawCommandを生成します。
         */
        void GenerateDrawCommands();

        /**
         * @brief フレーム終了
         *
         * FramePacketを確定し、レンダリングに備えます。
         */
        void EndFrame();

        // ========================================
        // レンダリング実行（RenderThread）
        // ========================================

        /**
         * @brief 1フレームを描画
         * @param packet フレームパケット
         */
        void RenderFrame(FramePacket *packet);

        /**
         * @brief DrawCommandを実行
         * @param commands 実行するDrawCommand
         */
        void ExecuteDrawCommands(const Container::VariableArray<DrawCommand> &commands);

        /**
         * @brief GPUにコマンドをサブミット
         */
        void SubmitToGPU();

        /**
         * @brief Presentを実行
         */
        void Present();

        // ========================================
        // Screen/View管理
        // ========================================

        /**
         * @brief Screenを取得
         */
        Screen &GetScreen() { return m_Screen; }
        const Screen &GetScreen() const { return m_Screen; }

        /**
         * @brief メインSceneViewを作成
         * @param settings 設定
         * @return 作成されたSceneView
         */
        Container::TSharedPtr<SceneView> CreateSceneView(const SceneViewSettings &settings);

        /**
         * @brief Viewを削除
         * @param view 削除するView
         */
        void DestroyView(Container::TSharedPtr<View> view);

        /**
         * @brief メインSceneViewを取得
         */
        Container::TSharedPtr<SceneView> GetMainSceneView() const { return m_MainSceneView; }

        // ========================================
        // リソースマネージャー
        // ========================================

        /**
         * @brief SceneRendererを取得
         */
        SceneRenderer &GetSceneRenderer() { return m_SceneRenderer; }
        const SceneRenderer &GetSceneRenderer() const { return m_SceneRenderer; }

        /**
         * @brief リソースマネージャーを設定
         */
        void SetResourceManager(RenderResourceManager *manager) { m_ResourceManager = manager; }

        // ========================================
        // RHIアクセス
        // ========================================

        /**
         * @brief RHIデバイスを取得
         */
        Container::TSharedPtr<RHI::IDevice> GetDevice() const { return m_Device; }

        /**
         * @brief コマンドリストを取得
         */
        Container::TSharedPtr<RHI::ICommandList> GetCommandList() const { return m_CommandList; }

        // ========================================
        // 解像度変更
        // ========================================

        /**
         * @brief 解像度を変更
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        void Resize(uint32_t width, uint32_t height);

        // ========================================
        // 統計
        // ========================================

        /**
         * @brief レンダリングスタットを取得
         * @note Debug::RenderingStats を使用します
         */
        Debug::RenderingStats &GetStats() { return m_Stats; }
        const Debug::RenderingStats &GetStats() const { return m_Stats; }

    private:
        // ========================================
        // 内部ヘルパー
        // ========================================

        /**
         * @brief スワップチェーンのフレームバッファを作成
         * @return 成功時true
         */
        bool CreateSwapChainFramebuffers();

        // RHIリソース
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::TSharedPtr<RHI::ICommandList> m_CommandList;

        // レンダーパス・フレームバッファ（Screen SwapChain用）
        Container::TSharedPtr<RHI::IRenderPass> m_RenderPass;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_SwapChainFramebuffers;

        // テスト三角形用リソース（将来的にマテリアルシステムに移行）
        Container::TSharedPtr<RHI::IPipeline> m_TrianglePipeline;
        Container::TSharedPtr<RHI::IShader> m_TriangleVertexShader;
        Container::TSharedPtr<RHI::IShader> m_TriangleFragmentShader;

        // Screen（最終出力先 - SwapChain所有）
        Screen m_Screen;

        // SceneRenderer（実際のRHI描画コマンド発行）
        SceneRenderer m_SceneRenderer;

        // View管理
        Container::TSharedPtr<SceneView> m_MainSceneView;
        Container::VariableArray<Container::TSharedPtr<View>> m_Views;

        // リソースマネージャー（外部参照）
        RenderResourceManager *m_ResourceManager = nullptr;

        // FramePacket管理
        FramePacketManager m_PacketManager;
        FramePacket *m_CurrentPacket = nullptr;

        // DrawCommand
        Container::VariableArray<DrawCommand> m_FrameDrawCommands;

        // 同期
        Thread::Mutex m_Mutex;

        // 設定
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;
        bool m_bVSyncEnabled = true;
        bool m_bMultiThreadedRendering = true;
        uint32_t m_MaxDrawCallsPerFrame = 10000;

        // 統計（Debug::RenderingStats使用）
        Debug::RenderingStats m_Stats;

        // フレームタイミング
        double m_LastFrameTime = 0.0;
        double m_TotalTime = 0.0;

        // 状態
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering
