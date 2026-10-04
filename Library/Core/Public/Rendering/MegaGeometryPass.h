#pragma once

#include "Rendering/HiZPyramidPass.h"
#include "Rendering/IViewPass.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;
    struct MegaGeometryPassCommand;

    /**
     * @brief MegaGeometryパス設定
     */
    struct MegaGeometryPassSettings
    {
        /** @brief IndirectDrawバッファの最大ドローコール数 */
        uint32_t MaxDrawCount = 65536;

        /**
         * @brief LODの段を選ぶ誤差の閾値（画素）
         *
         * 段で失われる形の誤差を画面へ投影した大きさがこの値以下になる最も粗い段を描く。
         * 環境変数 NORVES_MEGA_LOD_ERROR_PX（正の数）で替えられる（撮り比べ用）。
         * 1 画素は起動画面の撮り比べで決めた値: 近接（2.5 m）は LOD0、低角度（6 m）は LOD1 が選ばれ、
         * 変更前の段と同じで見た目の差が出ない。これより粗い段（低角度の LOD2）は目地の陰影が目に見えて変わる。
         */
        float LODBias = 1.0f;
    };

    /**
     * @brief Mega Geometry 描画パス
     *
     * GPU駆動カリングとIndirect Drawによるクラスタベースジオメトリ描画。
     *
     * パイプラインの位置: ShadowMap → NeuralMaterialDecode → **MegaGeometry** → GBuffer
     *
     * 動作フロー:
     * 1. Setup(): 登録済みMegaMeshリソースを収集
     * 2. Execute():
     *    a. DrawCountバッファをゼロクリア
     *    b. クラスタカリングコンピュートシェーダーをディスパッチ
     *    c. バリア: Compute → IndirectDraw
     *    d. GBufferレンダーパス内でDrawIndexedIndirectを発行
     *
     * 遮蔽カリング（既定で有効。--mega-occlusion=off で上の従来の1回の判定に戻る）は2パスで行う:
     * 1. 1パス目: 視錐台・法線のコーン・LODの判定を通り、前のフレームで見えたクラスタだけを描く（遮蔽の判定はしない）。
     * 2. その時点の深度（GBufferPassの不透明＋1パス目）からHZBを作る。
     * 3. 2パス目: 判定を通った全クラスタをHZBで判定し直し、1パス目で描かなかったもののうち遮蔽されないものを描く。
     *    全クラスタの「見えた」ビット（インスタンスごとに持続する）を更新し、次のフレームの1パス目が使う。
     * 影（CSM・点光源）はこのパスを通らず、カメラの深度で省かない。
     *
     * MegaMeshが未登録の場合はパスが自動的にスキップされます。
     */
    class MegaGeometryPass : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit MegaGeometryPass(const MegaGeometryPassSettings &settings = MegaGeometryPassSettings{});
        ~MegaGeometryPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "MegaGeometryPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;
        void RecordFrameCommand(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList);

        // ========================================
        // SceneView連携
        // ========================================

        void SetSceneView(SceneView *sceneView) { m_SceneView = sceneView; }
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

        // ========================================
        // MegaMesh登録
        // ========================================

        /**
         * @brief 描画対象のMegaMeshを追加
         * @param handle MegaMeshHandle
         * @param worldMatrix ワールド変換行列（列優先）
         */
        void AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix);

        /**
         * @brief 描画対象のMegaMeshをクリア
         */
        void ClearMegaMeshInstances();

        RGResourceHandle GetIndirectDrawBufferHandle() const { return m_IndirectDrawBufferHandle; }
        RGResourceHandle GetDrawCountBufferHandle() const { return m_DrawCountBufferHandle; }
        RGResourceHandle GetMegaGeometryCompleteHandle() const { return m_MegaGeometryCompleteHandle; }

    private:
        /**
         * @brief カリング用ユニフォームデータ（GPU送信用）
         */
        struct alignas(16) CullUniformData
        {
            float ViewMatrix[16];
            float ProjectionMatrix[16];
            float CameraPosition[4];   // xyz + pad
            float FrustumPlanes[6][4]; // 6 planes, each (nx, ny, nz, d)
            uint32_t TotalClusterCount;
            uint32_t MaxDrawCount;
            float LODBias;
            float ScreenHeight;     // スクリーン高さ（ピクセル）
            float ProjectionFactor; // screenHeight / (2 * tan(fov/2))
            uint32_t HiZWidth;      // Hi-Zの元になった深度の幅（Hi-Zのミップ0はその半分）
            uint32_t HiZHeight;     // Hi-Zの元になった深度の高さ（Hi-Zのミップ0はその半分）
            uint32_t HiZMipCount;   // ミップレベル数
            uint32_t bHiZEnabled;   // Hi-Z有効フラグ（1=有効, 0=無効）
            uint32_t DebugPayloadMode; // firstInstanceへ書き込むデバッグpayload種別
            uint32_t CullPass;      // 0=従来（遮蔽の判定なし）, 1=1パス目, 2=2パス目
            uint32_t bStatsEnabled; // 1なら統計バッファへ数える
            float WorldMatrix[16];
            float LODSphere[4];     // LODの選択に使うメッシュ共通の境界球（ローカル。半径0ならクラスタごと）
        };

        /**
         * @brief MegaMeshインスタンス情報
         */
        struct MegaMeshInstance
        {
            // 「前のフレームで見えた」ビットを引き継ぐ鍵（プロキシのObjectId。0のインスタンスは並びの番号で代用する）
            uint64_t ObjectId = 0;
            MegaGeometry::MegaMeshHandle Handle;
            float WorldMatrix[16];
            float PreviousWorldMatrix[16];
        };

        /**
         * @brief カリング用GPUリソースを作成
         */
        bool CreateCullResources(RHI::IDevice *device);

        /**
         * @brief GBuffer互換のグラフィックスパイプラインを作成
         */
        bool CreateDrawPipeline(ViewRenderContext &context,
                                bool bRequireDrawPipeline = true,
                                bool bUseRenderGraphAttachmentStates = false);
        bool CreateDrawPipelineVariant(RHI::PolygonMode polygonMode, RHI::PipelinePtr &outPipeline);
        RHI::PipelinePtr SelectDrawPipeline(DebugViewMode mode) const;

        /**
         * @brief メッシュ共通のLOD球を持つメッシュの選ばれる段を、GPUと同じ式で求めて変わったときに記録する
         */
        void LogUniformLODSelection(const MegaMeshInstance &instance,
                                    const MegaGeometry::MegaMeshGPUData &gpuData,
                                    const CullUniformData &uniformData);

        /**
         * @brief インスタンスごとに安定した UBO / DescriptorSet を確保
         */
        bool EnsurePerInstanceBindings(uint32_t requiredCount);

        /**
         * @brief 2パスの遮蔽カリングで描けるか。描けるときはHZBを深度の大きさに合わせる
         *
         * 無効（--mega-occlusion=off）・HZBが作れない・描く範囲が深度の全体と一致しない（遮蔽の判定は深度の
         * 全体を画面と見る）ときは false で、従来の1回の判定で描く。
         */
        bool CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command);

        /**
         * @brief 1パス目で読み、2パス目で更新する「前のフレームで見えた」ビットのバッファ
         *
         * インスタンスごとに持続する。インスタンスの追加・メッシュの差し替え（ハンドルかクラスタの数が変わる）で
         * 作り直して0に戻す（outNeedsClear が true）。LODの切り替えでは、選ばれなくなったクラスタを2パス目が
         * 0に書き戻すので、古いビットは残らない。
         */
        RHI::BufferPtr AcquireVisibilityBuffer(uint64_t key,
                                               const MegaMeshInstance &instance,
                                               const MegaGeometry::MegaMeshGPUData &gpuData,
                                               bool &outNeedsClear);

        /** @brief 使われなくなったビットのバッファと、手放した古いバッファを、GPUが使い終わった後に破棄する */
        void ReleaseStaleVisibilityBuffers();

        /** @brief 統計（MEGA_OCCLUSION）の読み戻しのスロットを用意する。作れなければ統計は取らない */
        void EnsureStatsSlots();

        // 設定
        MegaGeometryPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // カリングコンピュートパイプライン
        RHI::PipelinePtr m_CullPipeline;
        RHI::ShaderPtr m_CullShader;

        // カリング用GPUバッファ
        RHI::BufferPtr m_IndirectDrawBuffer; // DrawIndexedIndirectCommand[]
        RHI::BufferPtr m_DrawCountBuffer;    // uint32_t visibleClusterCount
        RGResourceHandle m_IndirectDrawBufferHandle;
        RGResourceHandle m_DrawCountBufferHandle;
        RGResourceHandle m_MegaGeometryCompleteHandle;
        Container::VariableArray<RHI::BufferPtr> m_InstanceIndirectDrawBuffers;
        Container::VariableArray<RHI::BufferPtr> m_InstanceDrawCountBuffers;
        Container::VariableArray<RHI::BufferPtr> m_CullUniformBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_CullDescriptorSets;

        // GBuffer描画用グラフィックスパイプライン
        RHI::PipelinePtr m_DrawPipeline;
        RHI::PipelinePtr m_DrawWireframePipeline;
        RHI::ShaderPtr m_DrawVertexShader;
        RHI::ShaderPtr m_DrawFragmentShader;

        // GBuffer描画用レンダーパス・フレームバッファ（GBufferPassから共有）
        RHI::RenderPassPtr m_GBufferRenderPass;
        RHI::FramebufferPtr m_GBufferFramebuffer;

        // 2パス目のGBuffer描画用レンダーパス・フレームバッファ。1パス目の描画の後に続けて開くので、
        // 全てのアタッチメントが ShaderResource の状態から始まる（1パス目の終わりの状態）。
        RHI::RenderPassPtr m_SecondGBufferRenderPass;
        RHI::FramebufferPtr m_SecondGBufferFramebuffer;

        // 遮蔽カリング（2パス）
        HiZPyramid m_HiZ;
        bool m_bHiZReady = false;
        // 2パス目の、インスタンスごとのIndirectDrawバッファ・カリング用UBO・DescriptorSet
        // （記録中に1パス目の内容を書き換えないよう、パスごとに別に持つ）
        Container::VariableArray<RHI::BufferPtr> m_SecondInstanceIndirectDrawBuffers;
        Container::VariableArray<RHI::BufferPtr> m_SecondInstanceDrawCountBuffers;
        Container::VariableArray<RHI::BufferPtr> m_SecondCullUniformBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_SecondCullDescriptorSets;
        // 従来の経路が binding 5・6 に結ぶ代わりのバッファ（シェーダーは触らない）
        RHI::BufferPtr m_DummyVisibilityBuffer;
        RHI::BufferPtr m_DummyStatsBuffer;

        struct InstanceVisibility
        {
            uint64_t Key = 0;
            uint64_t MeshId = 0;
            const void *ClusterBufferIdentity = nullptr;
            uint32_t ClusterCount = 0;
            uint64_t LastUsedFrame = 0;
            RHI::BufferPtr Buffer; // uint32_t[ClusterCount]。1=前のフレームで見えた
        };
        struct RetiredBuffer
        {
            RHI::BufferPtr Buffer;
            uint64_t RetiredFrame = 0;
        };
        Container::VariableArray<InstanceVisibility> m_InstanceVisibilities;
        Container::VariableArray<RetiredBuffer> m_RetiredBuffers;
        uint64_t m_OcclusionFrameCount = 0;

        // 統計（MEGA_OCCLUSION）。ホストが読めるバッファを数フレームずつ遅らせて読み戻す（GPUを待たない）。
        struct StatsSlot
        {
            RHI::BufferPtr Buffer;
            const uint32_t *Mapped = nullptr;
            uint64_t Frame = 0;
            bool bPending = false;
        };
        static constexpr uint32_t StatsSlotCount = 4;
        StatsSlot m_StatsSlots[StatsSlotCount];
        bool m_bStatsSlotsTried = false;
        bool m_bStatsLoggedOnce = false;
        bool m_bOcclusionFallbackLogged = false;

        // GBufferテクスチャ参照（GBufferPassが作成したものをSharedResourcesから取得）
        RHI::TexturePtr m_AlbedoTexture;
        RHI::TexturePtr m_NormalTexture;
        RHI::TexturePtr m_MaterialTexture;
        RHI::TexturePtr m_EmissiveTexture;
        RHI::TexturePtr m_DepthTexture;
        // velocity（currentUV - previousUV）。GBufferPass と同じテクスチャへ書く。
        RHI::TexturePtr m_VelocityTexture;
        RGResourceHandle m_GBufferAlbedoHandle;
        RGResourceHandle m_GBufferNormalHandle;
        RGResourceHandle m_GBufferMaterialHandle;
        RGResourceHandle m_GBufferEmissiveHandle;
        RGResourceHandle m_GBufferDepthHandle;
        RGResourceHandle m_GBufferVelocityHandle;

        // PerObject UBO用ディスクリプタセット
        Container::VariableArray<RHI::BufferPtr> m_DrawUniformBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_DrawDescriptorSets;

        // デフォルトPBRテクスチャ（マテリアル未設定時のフォールバック）
        RHI::TexturePtr m_DefaultWhiteTexture;      // 1x1 白 — Albedo/AO/Roughnessデフォルト
        RHI::TexturePtr m_DefaultFlatNormalTexture; // 1x1 フラット法線 (128,128,255) — Normalデフォルト
        RHI::TexturePtr m_DefaultBlackTexture;      // 1x1 黒 — Metallic/Heightデフォルト
        RHI::SamplerPtr m_DefaultLinearSampler;     // Linear/Wrapサンプラー

        // フレーム単位のインスタンスリスト
        Container::VariableArray<MegaMeshInstance> m_Instances;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bPreferRenderGraphGBufferResources = false;
        bool m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;
        bool m_bMegaGeometryDebugPayloadUnsupportedWarned = false;

        // メッシュ共通のLOD球を持つメッシュ（全クラスタが同じ段を選ぶ）の、最後に記録した段。
        // 段が変わったときだけ、選んだ段と三角形数を記録する。
        struct LoggedUniformLOD
        {
            uint64_t MegaMeshId = 0;
            uint32_t Level = 0;
        };
        Container::VariableArray<LoggedUniformLOD> m_LoggedUniformLODs;
    };

} // namespace NorvesLib::Core::Rendering
