#pragma once

#include "FrameCaptureTypes.h"
#include "RenderGPUTimingTypes.h"
#include "Screen.h"
#include "View.h"
#include "CanvasView.h"
#include "SceneView.h"
#include "SceneRenderer.h"
#include "DrawCommand.h"
#include "FramePacket.h"
#include "ViewRenderContext.h"
#include "Rendering/DDGIVolume.h"
#include "Rendering/AutoExposure.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/InstanceBufferRing.h"
#include "Rendering/RenderedObjectHistory.h"
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
    struct DeviceCapabilities;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class FrameCaptureReadbackHelper;

    /**
     * @brief カリングと描画順に依存しないシーン構成ハッシュを計算する
     *
     * FramePacketが所有する全MeshProxy/SkinnedMeshProxyと材質・環境設定だけを
     * 入力にし、カメラ依存の描画結果や物体の変換履歴は入力にしません。
     */
    uint64_t ComputeSceneRevisionHash(const FramePacket& packet);

    /**
     * @brief メインSceneViewの描画方式。起動時にだけ選ぶ。
     */
    enum class RenderingMainViewRenderer : uint8_t
    {
        Raster,
        PathTracing
    };

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
        /**
         * @brief メインSceneViewの描画方式（既定はラスタ）。
         *
         * PathTracingはRT pipeline・BDA・非一様texture添字に対応するデバイスだけで有効になり、
         * 非対応ならラスタへ戻して警告する。
         */
        RenderingMainViewRenderer MainViewRenderer = RenderingMainViewRenderer::Raster;
        /** @brief パストレーサーが1フレームで累積する試料数 */
        uint32_t PathTracingSamplesPerFrame = 1;
        /** @brief パストレーサーが追う光輸送の範囲（既定は多重散乱をすべて追う） */
        PathTracingTransportScope PathTracingTransport = PathTracingTransportScope::Full;
        /** @brief パストレーサーの1次光線の画素内の標本位置（既定は画素内を一様にずらす） */
        PathTracingPixelSampling PathTracingPixelSamplingMode = PathTracingPixelSampling::Box;
        /** @brief パストレーサーの試料の組の番号（組ごとに独立した試料の列を引く。既定は0） */
        uint32_t PathTracingSampleBatch = 0u;
        /** @brief パストレーサーの検証出力（既定は放射輝度） */
        PathTracingDebugOutput PathTracingDebug = PathTracingDebugOutput::None;
        /** @brief ラスタの直接光のBRDF（既定は解析BRDF） */
        RasterDirectBrdf RasterDirectBrdfMode = RasterDirectBrdf::Analytic;
        /** @brief ビジビリティバッファの使い方（既定は On。装置が対応しないときは GBuffer の描画へ戻る） */
        VisibilityBufferMode VisibilityBuffer = VisibilityBufferMode::On;
        /** @brief ソフトウェアラスタの使い方（既定は On） */
        SwRasterMode SwRaster = SwRasterMode::On;
        /** @brief ソフトウェアラスタへ振り分けるクラスタの画面上の半径（画素）のしきい値 */
        float SwRasterMaxPixels = DefaultSwRasterMaxPixels;
        /** @brief 太陽の影の標本のパスを足す（--shadow-probe。統計が有効な構成だけで働く） */
        bool bShadowProbe = false;
        /** @brief 太陽の影の方式（--shadow-method。構造体の既定は CSM。Game の起動引数の既定は VSM） */
        ShadowMethod SunShadowMethod = ShadowMethod::Csm;
        /** @brief 点光源の影の方式（--point-shadow-method。既定は cube） */
        PointShadowMethod PointLightShadowMethod = PointShadowMethod::Cube;
        /** @brief VSM の物理ページのプールのページの数（--vsm-pool-pages。0 は既定） */
        uint32_t VsmPoolPages = 0;
    };

    struct RenderingCoordinatorStatsSnapshot
    {
        uint32_t SkinnedGBufferRecordedDraws = 0;
        uint32_t SkinnedShadowRecordedDraws = 0;
        Debug::RenderingStats Stats;
        uint32_t GeneratedDrawCommandCount = 0;
        uint64_t PublicationSequence = 0;
        bool bRenderFrameCompleted = false;
        bool bGameThreadTimingsAvailable = false;
        bool bRenderFrameTimingAvailable = false;
        bool bGPUTimeAvailable = false;
        bool bTotalFrameTimeAvailable = false;

        // 起動画面などのデバッグ表示用。最後に読み戻せた自動露出の測定（無ければ bValid が false）
        AutoExposureMeasurement AutoExposure;
    };

    struct RenderGraphDebugDumpSnapshot
    {
        Container::String Text;
        Container::String UnavailableReason;
        uint64_t FrameNumber = 0;
        uint64_t PublicationSequence = 0;
        bool bAvailable = false;
        bool bTruncated = false;
    };

    class RenderingCoordinatorDiagnostics;

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
        RenderingCoordinator();

        /**
         * @brief デストラクタ
         */
        ~RenderingCoordinator();

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

        /**
         * @brief 次フレームの overlay パス集合を書き込み中パケットへ載せる（GameThread）
         *
         * BeginFrame で確保した書き込み中パケット(m_CurrentPacket)の OverlayPasses へ
         * 借用ポインタをコピーする。EndFrame(FinishWrite)より前に呼ぶこと。書き込み中
         * パケットが無い場合は何もしない。passes が空のときは OverlayPasses も空のままで
         * 描画シームは完全 no-op になる。非所有(寿命はモジュール側が所有)。
         */
        void SetOverlayPassesForNextFrame(Container::Span<IViewPass *> passes);

        FrameCaptureRequestResult RequestFrameCapture();
        FrameCaptureRequestResult RequestFrameCapture(const FrameCaptureRequest& request);
        bool TryConsumeCapturedFrame(CapturedFrame& outFrame);
        bool TryConsumeCompletedGPUTimings(
            Container::VariableArray<RenderPassGPUTiming>& outTimings,
            uint64_t& outDroppedFrameCount);
        bool SupportsGPUTimings() const;

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
         * @brief 空パラメータを次のFramePacketへ公開する
         * @param parameters GameThread側で設定する空スナップショット
         */
        void SetSkyAtmosphere(const SkyAtmosphereParameters& parameters);

        /**
         * @brief DDGIプローブボリュームを次のFramePacketへ公開する
         */
        void SetDDGIVolumeParameters(const DDGIVolumeParameters& parameters);
        DDGIVolumeParameters GetDDGIVolumeParameters() const { return m_DDGIVolume; }

        /** @brief RTGIを次のFramePacketへ明示的に有効化または無効化する。 */
        void SetRTGIEnabled(bool bEnabled);
        bool IsRTGIEnabled() const { return m_bRTGIEnabled; }

        /**
         * @brief 高さフォグ設定を次のFramePacketへ公開する
         * @param parameters GameThread側で保持する高さフォグ設定
         */
        void SetVolumetricFogParameters(const VolumetricFogParameters& parameters);

        /**
         * @brief 空が無効なときの静的HDR環境（背景とIBL）の明るさの倍率を次のFramePacketへ公開する
         * @param scale 0以上の有限の倍率（1で従来どおり）。範囲外は1へ戻す
         */
        void SetStaticEnvironmentIntensityScale(float scale);
        float GetStaticEnvironmentIntensityScale() const { return m_StaticEnvironmentIntensityScale; }

        /** @brief 決定的な撮影にする（GameThread）。FramePacket の経過時間を 1/60 秒に固定する */
        void SetDeterministicCapture(bool bEnabled) { m_bDeterministicCapture = bEnabled; }

        /** @brief 決定的な撮影のエポックを始める（GameThread）。次の FramePacket が経過 0 番になる */
        void BeginDeterministicEpoch() { m_bDeterministicEpochPending = m_bDeterministicCapture; }

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

        /** @brief 初期化で実際に選んだメインSceneViewの描画方式 */
        RenderingMainViewRenderer GetMainViewRenderer() const { return m_MainViewRenderer; }

        /**
         * @brief CanvasViewを取得
         */
        Container::TSharedPtr<CanvasView> GetCanvasView() const { return m_CanvasView; }
        bool IsCanvasViewEnabled() const { return m_CanvasView && m_CanvasView->IsEnabled(); }
        void SetBoardInstanceBatchingEnabled(bool bEnabled);
        bool IsBoardInstanceBatchingEnabled() const { return m_bBoardInstanceBatchingEnabled; }

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

        /**
         * @brief overlay 用の presentation load render pass を取得(借用)
         *
         * overlay モジュール(例: オーバーレイUIモジュール)が GameThread の初期化フェーズで
         * バックエンドのパイプラインを当該 render pass に対して生成するために使う。
         * legacy(PresentationLoad)と composite(GraphPresentationLoad)は構成同一
         * (color1 Load + depth1 Load)で render-pass 互換のため、本アクセサは legacy 側を
         * 返す(一方で生成したパイプラインが両経路で有効)。実行時の経路選択(どちらを Begin
         * するか)は seam が ViewRenderContext::OverlayLoadRenderPass で行う。
         * Initialize 前/解放後は nullptr。借用ポインタのため呼び出し側は delete しない。
         */
        RHI::IRenderPass *GetOverlayLoadRenderPass() const { return m_PresentationLoadRenderPass.get(); }

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
        RenderingCoordinatorStatsSnapshot GetStatsSnapshot() const;
        Debug::RenderingStats GetStats() const;
        void RequestRenderGraphDebugDump();
        /**
         * @brief 最後に消費した公開連番より新しいRenderGraphダンプを値コピーで取得
         * @param knownPublicationSequence 0は未消費を表し、存在する最初の公開を取得する
         * @return 新しい公開が存在した場合true。outSnapshotはtrueの場合だけ更新する
         */
        bool TryGetRenderGraphDebugDumpSnapshot(uint64_t knownPublicationSequence,
                                                 RenderGraphDebugDumpSnapshot &outSnapshot) const;

    private:
        friend struct DDGISnapshotContractTestAccess;

        // CPUのpacket生成は実capability値を受け取る。GPU操作は行わない。
        void GenerateDrawCommands(const RHI::DeviceCapabilities& capabilities);
        void SnapshotSceneParameters(FramePacket& packet,
                                     const RHI::DeviceCapabilities& capabilities) const;
        void UpdateFrameRevisions(FramePacket& packet);

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
        bool EnsureCompositeAlphaOverResources();

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
        Container::TSharedPtr<RHI::IShader> m_CompositeAlphaOverFragmentShader;
        Container::TSharedPtr<RHI::IDescriptorSet> m_CompositeAlphaOverDescriptorSet;

        // 深度バッファ（スワップチェーン用、将来的にForwardPass等で使用）
        Container::TSharedPtr<RHI::ITexture> m_DepthTexture;

        // メインカメラ（GameThreadから設定される）
        CameraProxy m_MainCamera;
        CameraProxy m_PreviousMainCamera;
        SkyAtmosphereParameters m_SkyAtmosphere;
        DDGIVolumeParameters m_DDGIVolume;
        VolumetricFogParameters m_VolumetricFog;
        float m_StaticEnvironmentIntensityScale = 1.0f;
        bool m_bRTGIEnabled = true;
        uint64_t m_SceneRevision = 1u;
        uint64_t m_LightRevision = 1u;
        uint64_t m_LastSceneRevisionHash = 0u;
        uint64_t m_LastLightRevisionHash = 0u;
        bool m_bSceneRevisionHashValid = false;
        bool m_bLightRevisionHashValid = false;
        Container::UnorderedMap<uint64_t, CameraProxy> m_Cameras;
        uint64_t m_NextCameraId = 1;
        uint64_t m_MainCameraId = 0;
        uint64_t m_CanvasCameraId = 0;
        Thread::Atomic<bool> m_bCanvasCameraSyncPending{false};
        bool m_bCameraSet = false;
        bool m_bPreviousMainCameraValid = false;
        // m_PreviousMainCamera を書いたパケットのゲームのフレーム番号
        uint64_t m_PreviousMainCameraFrameNumber = 0;

        // 直前のゲームのフレームのパケットに書いた MegaGeometry の変換と、スキニングの変換・パレット
        // （ComponentId ごと）。次のパケットの前の値（velocity 用）にする。
        Container::UnorderedMap<uint64_t, Math::Matrix4x4> m_PreviousMegaGeometryWorlds;
        SkinnedPoseHistory m_PreviousSkinnedStates;
        uint64_t m_PreviousObjectStateFrameNumber = 0;
        bool m_bPreviousObjectStateValid = false;
        // RenderThread が記録したフレームの通し番号（ViewRenderContext::RenderFrameSerial に渡す。GameThread のフレーム番号と
        // 違い、描画がパケットを飛ばしても、同じパケットを描き直しても、記録のたびに必ず増える）
        uint64_t m_RenderFrameSerial = 0;
        // 提出した描画フレームの通し番号と、その提出 serial の対（完了の通知が来るまで持つ）。
        // 提出 serial が完了済みになったら m_CompletedRenderFrameSerial へ進める（ViewRenderContext::CompletedRenderFrameSerial に渡す）
        struct SubmittedRenderFrame
        {
            uint64_t RenderFrameSerial = 0;
            uint64_t SubmissionSerial = 0;
        };
        Container::VariableArray<SubmittedRenderFrame> m_SubmittedRenderFrames;
        uint64_t m_CompletedRenderFrameSerial = 0;
        uint64_t m_LastCompletedSubmissionSerial = 0;
        void AdvanceCompletedRenderFrameSerial(uint64_t completedSubmissionSerial);

        // RenderThread が最後に描いたフレームの物体の変換。描画がゲームのフレームを飛ばしたとき、TAA を選んだ
        // カメラなら、パケットの前の変換（velocity の基準）をそのフレームのものへ付け替える。
        RenderedObjectHistory m_RenderedObjectHistory;
        // 付け替えたフレーム数と、そのうち照合できないインスタンスがあったフレーム数（終了時にログへ出す）
        uint64_t m_RenderedObjectRebasedFrameCount = 0;
        uint64_t m_RenderedObjectIncompleteFrameCount = 0;

        /** @brief パケットの MegaGeometry・スキニングへ前の値を書き、このパケットの値を次の前の値として覚える。 */
        void ApplyPreviousObjectStates(FramePacket& packet);

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
        RenderingMainViewRenderer m_MainViewRenderer = RenderingMainViewRenderer::Raster;
        Container::TSharedPtr<CanvasView> m_CanvasView;
        Container::VariableArray<Container::TSharedPtr<View>> m_Views;

        // レンダリングリソース（RenderWorld所有、フレーム実行中のみ参照）
        RenderResources *m_RenderResources = nullptr;

        // フレームキャプチャ読み戻し
        Container::TUniquePtr<FrameCaptureReadbackHelper> m_FrameCaptureReadbackHelper;

        // FramePacket管理
        FramePacketManager m_PacketManager;
        FramePacket *m_CurrentPacket = nullptr;

        // 同期
        Thread::Mutex m_Mutex;
        Detail::GPUTimingMailbox m_GPUTimingMailbox;

        // 設定
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;
        uint32_t m_RenderWidth = 1280;
        uint32_t m_RenderHeight = 720;
        float m_RenderScale = 1.0f;
        bool m_bVSyncEnabled = true;
        bool m_bMultiThreadedRendering = true;
        bool m_bMegaGeometryPassEnabled = false;
        bool m_bBoardInstanceBatchingEnabled = true;
        uint32_t m_MaxDrawCallsPerFrame = 10000;

        Container::TUniquePtr<RenderingCoordinatorDiagnostics> m_Diagnostics;

        // 統計はGameThread専用。公開はm_Diagnosticsの値スナップショットだけを使う。
        Debug::RenderingStats m_GameThreadStats;

        // RenderThread専用の完了フレーム履歴。
        float m_PreviousCompletedTotalFrameTimeMs = 0.0f;
        float m_LatestCompletedGPUTimeMs = 0.0f;
        bool m_bLatestCompletedGPUTimeValid = false;
        // RenderThread専用。SceneView の自動露出のパスから最後に取れた測定。
        AutoExposureMeasurement m_LatestAutoExposure;

        // フレームタイミング
        double m_LastFrameTime = 0.0;
        double m_TotalTime = 0.0;

        // 決定的な撮影（GameThread専用）。エポックの 0 番のフレームから数えたフレーム数を FramePacket へ載せる。
        bool m_bDeterministicCapture = false;
        bool m_bDeterministicEpochPending = false;
        bool m_bDeterministicEpochActive = false;
        uint64_t m_DeterministicEpochFrames = 0u;

        // 状態
        bool m_bInitialized = false;
        bool m_bFrameSubmissionStarted = false;

        void UpdateRenderResolution(uint32_t screenWidth, uint32_t screenHeight);
        // キャンバス（UI）の描画先・正射影の大きさ。内部解像度（SetRenderScale）に依らず画面解像度。
        uint32_t GetCanvasWidth() const { return m_Width > 0 ? m_Width : 1u; }
        uint32_t GetCanvasHeight() const { return m_Height > 0 ? m_Height : 1u; }
        void RequestCanvasCameraSync();
        void ConsumePendingCanvasCameraSync();
        void UpdateCanvasCameraForRenderResolution();
        void PublishCompletedGPUTimestampResults();
    };

} // namespace NorvesLib::Core::Rendering
