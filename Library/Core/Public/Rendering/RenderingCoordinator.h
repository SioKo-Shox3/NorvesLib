#pragma once

#include "Screen.h"
#include "View.h"
#include "CanvasView.h"
#include "SceneView.h"
#include "SceneRenderer.h"
#include "DrawCommand.h"
#include "FramePacket.h"
#include "ViewRenderContext.h"
#include "Rendering/InstanceBufferRing.h"
#include "Rendering/CompositePass.h"
#include "Rendering/PresentationPass.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "ShaderManager.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Platform/NativeWindowHandle.h"
#include "RHI/TransientResourcePool.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Atomic.h"
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
    class IBuffer;
    class ITexture;
    class IDescriptorSet;
    class ISampler;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言

    /**
     * @brief レンダリング調整設定
     */
    struct RenderingCoordinatorSettings
    {
        // RHIデバイス（RenderWorldから渡される）
        Container::TSharedPtr<RHI::IDevice> Device;

        Platform::NativeWindowHandle WindowHandle;
        uint32_t Width = 1280;
        uint32_t Height = 720;
        float RenderScale = 1.0f;
        uint32_t BackBufferCount = 2;
        bool bVSync = true;
        bool bEnableMultiThreadedRendering = true;
        uint32_t MaxDrawCallsPerFrame = 10000;
        bool bEnableValidation = false;
        RGDumpOptions RenderGraphDumpOptions;
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
         * @brief フレーム終了（GameThread）
         *
         * FramePacketを確定（Writing→Ready）してポインタを返します。
         * Screen.EndFrame（submit/present）はRenderFrame内に移動済みです。
         *
         * @return 完成したFramePacket（BeginFrameでスロット取得失敗時はnullptr）
         */
        FramePacket* EndFrame();

        // ========================================
        // レンダリング実行（RenderThread）
        // ========================================

        /**
         * @brief 1フレームを描画（RenderThread または GameThread ST経路）
         *
         * Screen.BeginFrame（swapchain acquire）と
         * Screen.EndFrame（submit + present）をこの関数内で実行します。
         * @param packet フレームパケット。nullptrの場合は描画をスキップする。
         */
        void RenderFrame(FramePacket *packet);

        /**
         * @brief RenderThread側でのパケット解放（RenderThread用）
         *
         * パケットの状態をReading→Emptyにして再利用可能にします。
         * RenderFrame呼び出し後に必ず呼んでください。
         * @param packet 解放するパケット（nullptrは無視）
         */
        void ReleasePacket(FramePacket *packet);

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
         * @brief CanvasViewを作成
         *
         * F1ではpre-frame専用。既定では作成されず、明示opt-in時のみ登録します。
         */
        Container::TSharedPtr<CanvasView> CreateCanvasView();

        /**
         * @brief Viewを削除
         * @param view 削除するView
         */
        void DestroyView(Container::TSharedPtr<View> view);

        /**
         * @brief メインカメラを設定
         * @param camera カメラ情報
         */
        void SetMainCamera(const CameraProxy &camera);

        /**
         * @brief メインカメラを取得
         */
        const CameraProxy &GetMainCamera() const { return m_MainCamera; }

        /**
         * @brief GameThread owned camera tableへカメラを登録する
         * @return 1始まりのCameraId。0はinvalidとして予約。
         */
        uint64_t RegisterCamera(const CameraProxy &camera);

        /**
         * @brief GameThread owned camera tableのカメラを更新する
         * @return 指定CameraIdが存在して更新できた場合true
         */
        bool UpdateCamera(uint64_t cameraId, const CameraProxy &camera);

        /**
         * @brief GameThread owned camera tableからカメラを取得する
         *
         * 返るポインタはGameThread専用で、保持しないこと。FramePacketへは必ず値コピーする。
         */
        const CameraProxy *FindCamera(uint64_t cameraId) const;

        /**
         * @brief メインSceneViewを取得
         */
        Container::TSharedPtr<SceneView> GetMainSceneView() const { return m_MainSceneView; }

        /**
         * @brief CanvasViewを取得
         */
        Container::TSharedPtr<CanvasView> GetCanvasView() const { return m_CanvasView; }
        bool IsCanvasViewEnabled() const { return m_CanvasView && m_CanvasView->IsEnabled(); }

        // ========================================
        // リソースマネージャー
        // ========================================

        /**
         * @brief SceneRendererを取得
         */
        SceneRenderer &GetSceneRenderer() { return m_SceneRenderer; }
        const SceneRenderer &GetSceneRenderer() const { return m_SceneRenderer; }

        /**
         * @brief MegaGeometryパスが有効かどうか
         */
        bool IsMegaGeometryPassEnabled() const { return m_bMegaGeometryPassEnabled; }

        /**
         * @brief リソースマネージャーを設定
         */
        void SetRenderResources(RenderResources *resources) { m_RenderResources = resources; }

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

        /**
         * @brief スワップチェーン依存の描画リソースを再生成
         * @return 成功時true
         */
        bool RecreateSwapChainPresentationResources();

        /**
         * @brief 初期化済みリソースを解放
         *
         * m_bInitialized に依存せず、初期化途中の巻き戻しからも呼び出せる。
         */
        void ReleaseInitializedResources();

        // RHIリソース
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::TSharedPtr<RHI::ICommandList> m_CommandList;

        // レンダーパス・フレームバッファ（Screen SwapChain用）
        Container::TSharedPtr<RHI::IRenderPass> m_RenderPass;
        Container::TSharedPtr<RHI::IRenderPass> m_PresentationLoadRenderPass;
        Container::TSharedPtr<RHI::IRenderPass> m_GraphPresentationClearRenderPass;
        Container::TSharedPtr<RHI::IRenderPass> m_GraphPresentationLoadRenderPass;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_SwapChainFramebuffers;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_PresentationLoadFramebuffers;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_GraphPresentationClearFramebuffers;
        Container::VariableArray<Container::TSharedPtr<RHI::IFramebuffer>> m_GraphPresentationLoadFramebuffers;
        bool m_bSwapChainFramebuffersReady = false;
        RHI::Format m_SwapChainFormat = RHI::Format::UNKNOWN;

        // テスト三角形用リソース（将来的にマテリアルシステムに移行）
        Container::TSharedPtr<RHI::IPipeline> m_TrianglePipeline;
        Container::TSharedPtr<RHI::IShader> m_TriangleVertexShader;
        Container::TSharedPtr<RHI::IShader> m_TriangleFragmentShader;

        // Blit合成用リソース（ToneMappedColor → SwapChain最終出力）
        Container::TSharedPtr<RHI::IPipeline> m_BlitPipeline;
        Container::TSharedPtr<RHI::IShader> m_BlitVertexShader;
        Container::TSharedPtr<RHI::IShader> m_BlitFragmentShader;
        Container::TSharedPtr<RHI::IDescriptorSet> m_BlitDescriptorSet;
        Container::TSharedPtr<RHI::ISampler> m_BlitSampler;

        // 深度バッファ（スワップチェーン用、将来的にForwardPass等で使用）
        Container::TSharedPtr<RHI::ITexture> m_DepthTexture;

        // メインカメラ（GameThreadから設定される）
        CameraProxy m_MainCamera;
        Container::UnorderedMap<uint64_t, CameraProxy> m_Cameras;
        uint64_t m_NextCameraId = 1;
        uint64_t m_MainCameraId = 0;
        uint64_t m_CanvasCameraId = 0;
        Thread::Atomic<bool> m_bCanvasCameraSyncPending{false};
        bool m_bCameraSet = false;

        // Screen（最終出力先 - SwapChain所有）
        Screen m_Screen;

        // SceneRenderer（実際のRHI描画コマンド発行）
        SceneRenderer m_SceneRenderer;

        // フレーム内一時リソースプール
        RHI::TransientResourcePool m_TransientPool;

        // Viewパスチェーン用RenderGraph
        RenderGraph m_RenderGraph;

        // RenderGraph最終swapchain合成パス
        CompositePass m_CompositePass;
        PresentationPass m_PresentationPass;

        // フレーム別インスタンスデータSSBOリング
        InstanceBufferRing m_InstanceBufferRing;

        // シェーダーマネージャー（ランタイムコンパイル管理）
        ShaderManager m_ShaderManager;

        // View管理
        Container::TSharedPtr<SceneView> m_MainSceneView;
        Container::TSharedPtr<CanvasView> m_CanvasView;
        Container::VariableArray<Container::TSharedPtr<View>> m_Views;

        // レンダリングリソース（RenderWorld所有、フレーム実行中のみ参照）
        RenderResources *m_RenderResources = nullptr;

        // FramePacket管理
        FramePacketManager m_PacketManager;
        FramePacket *m_CurrentPacket = nullptr;

        // 同期
        Thread::Mutex m_Mutex;

        // 設定
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;
        uint32_t m_RenderWidth = 1280;
        uint32_t m_RenderHeight = 720;
        float m_RenderScale = 1.0f;
        bool m_bVSyncEnabled = true;
        bool m_bMultiThreadedRendering = true;
        bool m_bMegaGeometryPassEnabled = false;
        uint32_t m_MaxDrawCallsPerFrame = 10000;

        // 統計（Debug::RenderingStats使用）
        Debug::RenderingStats m_Stats;

        // フレームタイミング
        double m_LastFrameTime = 0.0;
        double m_TotalTime = 0.0;

        // 状態
        bool m_bInitialized = false;
        bool m_bFrameSubmissionStarted = false;

        void UpdateRenderResolution(uint32_t screenWidth, uint32_t screenHeight);
        void RequestCanvasCameraSync();
        void ConsumePendingCanvasCameraSync();
        void UpdateCanvasCameraForRenderResolution();
    };

} // namespace NorvesLib::Core::Rendering
