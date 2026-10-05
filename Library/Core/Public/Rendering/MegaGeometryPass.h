#pragma once

#include "Rendering/HiZPyramidPass.h"
#include "Rendering/IViewPass.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;
    struct MegaGeometryPassCommand;
    class MegaGeometryResources;

    /**
     * @brief MegaGeometryパス設定
     */
    struct MegaGeometryPassSettings
    {
        /**
         * @brief 材質の区間1つあたりの IndirectDraw コマンドの最大数（1パスで、その材質を使う全インスタンスの合計）
         *
         * 区間の確保は、その材質を使うインスタンスのクラスタ数の合計とこの値の小さい方。可視のクラスタが
         * これを超えた分は描かれない。
         */
        uint32_t MaxDrawCount = 262144;

        /**
         * @brief LODの段を選ぶ誤差の閾値（画素）
         *
         * 段で失われる形の誤差を画面へ投影した大きさがこの値以下になる最も粗い段を描く。
         * 環境変数 NORVES_MEGA_LOD_ERROR_PX（正の数）で替えられる（撮り比べ用）。
         * 1 画素は起動画面の撮り比べで決めた値: 近接（2.5 m）は LOD0、低角度（6 m）は LOD1 が選ばれ、
         * 変更前の段と同じで見た目の差が出ない。これより粗い段（低角度の LOD2）は目地の陰影が目に見えて変わる。
         */
        float LODBias = 1.0f;

        /**
         * @brief グループの BVH（NVMESH v1.1）を持つメッシュで、BVH をたどって判定するか
         *
         * false なら全メッシュを平らなクラスタの列で判定する（BVH をたどった選択と平らな選択の撮り比べ用）。
         * 環境変数 NORVES_MEGA_BVH が 0 か off なら false にする。
         */
        bool bUseGroupBVH = true;

        /**
         * @brief MEGA_OCCLUSION を30フレームごとではなく毎フレーム出すか（フレームごとの揺れを測る撮り比べ用）
         *
         * 環境変数 NORVES_MEGA_STATS_EVERY_FRAME が 1 なら true にする。
         */
        bool bStatsEveryFrame = false;
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
     * 2. 記録（RecordFrameCommand）:
     *    a. 全インスタンスの表（変換・前のフレームの変換・メッシュ・材質の区間の番号など）を1つのストレージバッファに書く。
     *       インスタンスは材質（と、メッシュを置くプールの塊）ごとの「区間」に分かれ、区間ごとに IndirectDraw
     *       コマンドの連続した範囲とカウンタを持つ
     *    b. 区間ごとのカウンタをゼロクリアし、クラスタカリングを全インスタンスに対して1回ディスパッチ
     *    c. バリア: Compute → IndirectDraw
     *    d. GBufferレンダーパス内で、区間ごとに DrawIndexedIndirectCount を発行
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
         * @brief カリング用ユニフォームデータ（GPU送信用。cluster_cull.comp の CullUniforms と一致）
         *
         * インスタンスごとに変わる値（ワールド変換・LODの球・クラスタ数）はインスタンスの表にあり、ここには無い。
         */
        struct alignas(16) CullUniformData
        {
            float ViewMatrix[16];
            float ProjectionMatrix[16];
            float CameraPosition[4];   // xyz + pad
            float FrustumPlanes[6][4]; // 6 planes, each (nx, ny, nz, d)
            uint32_t InstanceCount;    // インスタンスの表の要素数
            uint32_t TotalGroupCount;  // 全インスタンスのワークグループ（64クラスタ）の数
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
            uint32_t SectionBase;   // 区間の表・カウンタのうちこのパスの先頭（1パス目は0、2パス目は区間の数）
            uint32_t VisibleReadStamp;  // 1パス目が「前のフレームで見えた」とみなす印の値（前のフレームの2パス目が書いた値）
            uint32_t VisibleWriteStamp; // 2パス目が見えたクラスタへ書く印の値
            uint32_t BvhStage;      // BVH のたどり: 節の判定の段の番号（BvhStageClusters なら葉のクラスタの判定。平らな判定では使わない）
            uint32_t BvhInputBase;  // この段の入力の列の先頭（要素）
            uint32_t BvhNextBase;   // 次の段の列の先頭
            uint32_t BvhLeafBase;   // 葉の列の先頭
            uint32_t BvhRootCount;  // BVH を持つインスタンスの数（インスタンスの表の先頭からその数。段0の入力の数）
            uint32_t PageRequestCapacity; // ページの要求の列の容量（0 ならこのフレームは要求を書かない）
        };

        /**
         * @brief インスタンスの表の1要素（GPU送信用。cluster_cull.comp・megageometry.vert の MegaInstance と一致）
         */
        struct alignas(16) GPUMegaInstance
        {
            float WorldMatrix[16];         // ワールド変換（行ベクトル規約の列優先の並びのまま）
            float PreviousWorldMatrix[16]; // 直前のフレームの変換（velocity 用）
            float LODSphere[4];            // LODの選択に使うメッシュ共通の境界球（ローカル。半径0ならクラスタごと）
            uint32_t ClusterAddressLow;    // クラスタ配列のデバイスアドレス
            uint32_t ClusterAddressHigh;
            uint32_t ClusterCount;
            uint32_t FirstGroup;           // このインスタンスの最初のワークグループの通し番号
            uint32_t SectionIndex;         // 材質の区間の番号
            uint32_t VertexBase;           // 頂点の基点（プールの塊の先頭から。頂点単位）
            uint32_t IndexBase;            // インデックスの基点（プールの塊の先頭から。インデックス単位）
            uint32_t VisibleOffset;        // 「見えた」ビットの先頭（全インスタンス共通の配列の中の位置）
            uint32_t BvhAddressLow;        // グループの BVH の節の配列のデバイスアドレス（BVH が無ければ 0）
            uint32_t BvhAddressHigh;
            uint32_t BvhNodeCount;
            uint32_t PageTableBase;        // ページの表の中の、このメッシュのページの範囲の先頭
        };
        static_assert(sizeof(GPUMegaInstance) == 192, "cluster_cull.comp の MegaInstance と大きさが一致しません");

        /**
         * @brief MegaMeshインスタンス情報
         */
        struct MegaMeshInstance
        {
            // 「前のフレームで見えた」ビットを引き継ぐ鍵（プロキシのObjectId。0のインスタンスは並びの番号で代用する）
            uint64_t ObjectId = 0;
            // コンポーネントの世代（作り直されたら変わる）。同じ ObjectId でも別のコンポーネントなら見えたビットを捨てる
            uint64_t ComponentId = 0;
            MegaGeometry::MegaMeshHandle Handle;
            float WorldMatrix[16];
            float PreviousWorldMatrix[16];
        };

        /**
         * @brief カリング用GPUリソースを作成
         */
        bool CreateCullResources(RHI::IDevice *device);

        /** @brief カリングのディスクリプタセットの形（cluster_cull.comp の binding 0〜8）。パイプラインとセットが同じ形を使う */
        static RHI::DescriptorSetDesc BuildCullDescriptorSetDesc();

        /** @brief 描画のディスクリプタセットの形（megageometry.vert/frag の binding 0〜9）。パイプラインとセットが同じ形を使う */
        RHI::DescriptorSetDesc BuildDrawDescriptorSetDesc() const;

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

        /** @brief 材質の区間1つ分の描画資源（フレームスロットが持つ） */
        struct SectionDraw
        {
            RHI::BufferPtr Uniform;              // 材質の区間ごとの定数（PerObject UBO）
            RHI::DescriptorSetPtr DescriptorSet; // 描画用（UBO・材質のテクスチャ・VTの要求・インスタンスの表・描画情報）
        };

        /** @brief BVH のたどりの1段分の資源（カリング用UBOと、その UBO を結んだディスクリプタセット） */
        struct BvhStageDraw
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        /** @brief ホストが毎フレーム書く資源の組。フレームごとに交互に使う */
        struct FrameSlot
        {
            RHI::BufferPtr InstanceBuffer; // インスタンスの表（host-visible）
            uint32_t InstanceCapacity = 0; // 要素数
            RHI::BufferPtr SectionBuffer;  // 区間の表（uvec2: コマンドの先頭・最大数。host-visible）
            uint32_t SectionCapacity = 0;  // 要素数
            // ページの表（GeometryPageTable::Entry の並び。host-visible）。このフレームの常駐を固定して、
            // 2パスの間で判定が食い違わないようにする。版が変わったときだけホストが書き直す（GPU は要求の印を書く）
            RHI::BufferPtr PageTableBuffer;
            uint32_t PageTableCapacity = 0; // 要素数
            uint64_t PageTableVersion = 0;  // バッファの中身の版（PageTableBuffer が無いときは意味を持たない）
            RHI::BufferPtr CullUniform[2]; // パスごとのカリング用UBO
            RHI::DescriptorSetPtr CullDescriptorSet[2];
            // BVH のたどり: パスごとに、節の判定の段 + 葉のクラスタの判定の段の数だけ（段ごとにUBOの中身が違うため別々に持つ）
            Container::VariableArray<BvhStageDraw> BvhStages[2];
            Container::VariableArray<SectionDraw> Sections;
        };

        /** @brief フレームスロットに、このフレームのインスタンス・区間の数を収める資源を用意する */
        bool EnsureFrameSlot(FrameSlot &slot, uint32_t instanceCount, uint32_t sectionTableEntries, uint32_t sectionCount);

        /**
         * @brief フレームスロットのページの表を、今の常駐の状態に合わせる
         *
         * 表の版がスロットの写しと違うときだけ、バッファを（足りなければ作り直して）書き直す。
         * @return false ならバッファを作れなかった（このフレームは描けない）
         */
        bool SyncPageTable(FrameSlot &slot, MegaGeometryResources &resources);

        /**
         * @brief IndirectDraw コマンド・区間のカウンタ・描画情報のバッファを、必要な数に収まる大きさにする
         *
         * 足りなければ作り直し、古いバッファは、GPUが使い終わった後に破棄する。
         */
        bool EnsureBatchBuffers(uint32_t commandCapacity, uint32_t counterCapacity);

        /**
         * @brief BVH のたどりの列とカウンタを、必要な数に収まる大きさにする（足りなければ作り直し、古いものは遅れて破棄）
         * @param queueEntries 列の要素数（uvec2）。段ごとの列と葉の列を1本に並べた合計
         */
        bool EnsureBvhBuffers(uint32_t queueEntries);

        /** @brief フレームスロットに、BVH のたどりの段 stageCount 個ぶんの UBO・ディスクリプタセットを用意する */
        bool EnsureBvhStageResources(FrameSlot &slot, uint32_t passIndex, uint32_t stageCount);

        /**
         * @brief 2パスの遮蔽カリングで描けるか。描けるときはHZBを深度の大きさに合わせる
         *
         * 無効（--mega-occlusion=off）・HZBが作れない・描く範囲が深度の全体と一致しない（遮蔽の判定は深度の
         * 全体を画面と見る）ときは false で、従来の1回の判定で描く。
         */
        bool CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command);

        /** @brief 「見えた」ビットの区画を要求するインスタンス1つ分 */
        struct VisibilityRequest
        {
            uint64_t Key = 0;
            uint64_t MeshId = 0;
            uint64_t ComponentId = 0;
            const void *ClusterBufferIdentity = nullptr;
            uint64_t ClusterBufferOffsetBytes = 0;
            uint32_t ClusterCount = 0;
            uint32_t Offset = 0; // 出力: 区画の先頭（要素の位置）
        };

        /**
         * @brief 「前のフレームで見えた」ビットの配置を、このフレームのインスタンスに合わせる
         *
         * ビットは全インスタンスで1本のバッファに、インスタンスの並びで区画を詰めて持つ。配置（鍵とクラスタ数の並び）が
         * 前のフレームと同じなら何もしない。変わったときは、ぴったりの大きさのバッファを作り直して0で埋め、
         * 直前のフレームに描かれていた同じコンポーネント・メッシュのインスタンスの区画だけを写して引き継ぐ。
         * 追加されたインスタンスやメッシュの差し替えは0から始まる。LODの切り替えでは、選ばれなくなったクラスタを
         * 2パス目が0に書き戻すので、古いビットは残らない。
         *
         * @return false ならバッファを作れなかった（2パスの遮蔽カリングは使えない）
         */
        bool UpdateVisibilityLayout(RHI::ICommandList *commandList, Container::VariableArray<VisibilityRequest> &requests);

        /** @brief 手放した古いバッファを、GPUが使い終わった後に破棄する */
        void ReleaseStaleBuffers();

        /** @brief 統計（MEGA_OCCLUSION）の読み戻しのスロットを用意する。作れなければ統計は取らない */
        void EnsureStatsSlots();

        // 設定
        MegaGeometryPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        // カリングコンピュートパイプライン（m_CullPipeline = 平らなクラスタの列、m_BvhCullPipeline = グループの BVH をたどる）
        RHI::PipelinePtr m_CullPipeline;
        RHI::ShaderPtr m_CullShader;
        RHI::PipelinePtr m_BvhCullPipeline;
        RHI::ShaderPtr m_BvhCullShader;

        // BVH のたどりの列（uvec2: インスタンスの番号・節の番号。段ごとの列と葉の列）とカウンタ。全インスタンス・全パスで共用する
        RHI::BufferPtr m_BvhQueueBuffer;
        RHI::BufferPtr m_BvhCounterBuffer;
        uint32_t m_BvhQueueCapacity = 0; // m_BvhQueueBuffer の要素数
        // 最後に記録した BVH のたどりの規模（変わったときだけ記録する）
        uint32_t m_LoggedBvhInstances = 0xFFFFFFFFu;
        uint32_t m_LoggedBvhLevels = 0xFFFFFFFFu;
        uint32_t m_LoggedFlatInstances = 0xFFFFFFFFu;

        // カリング・描画用GPUバッファ（全インスタンス・全パスで1組。コマンドは区間ごとの連続した範囲で、
        // パスごとに別の範囲を使う。範囲の配置は区間の表が持つ）
        RHI::BufferPtr m_IndirectDrawBuffer; // DrawIndexedIndirectCommand[]
        RHI::BufferPtr m_DrawCountBuffer;    // uint32_t[] 区間ごとの可視クラスタ数
        RHI::BufferPtr m_DrawInfoBuffer;     // uint32_t[2] × コマンド（インスタンスの番号・payload）
        uint32_t m_CommandCapacity = 0;      // m_IndirectDrawBuffer・m_DrawInfoBuffer のコマンド数
        uint32_t m_CounterCapacity = 0;      // m_DrawCountBuffer のカウンタ数
        RGResourceHandle m_IndirectDrawBufferHandle;
        RGResourceHandle m_DrawCountBufferHandle;
        RGResourceHandle m_MegaGeometryCompleteHandle;

        // ホストが毎フレーム書く資源。フレームごとに交互に使う（直前のフレームのGPUがまだ読んでいるかもしれないため）
        static constexpr uint32_t FrameSlotCount = 2;
        FrameSlot m_FrameSlots[FrameSlotCount];

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
        // 従来の経路が binding 5・6 に結ぶ代わりのバッファ（シェーダーは触らない）
        RHI::BufferPtr m_DummyVisibilityBuffer;
        RHI::BufferPtr m_DummyStatsBuffer;

        // 「前のフレームで見えた」ビット（全インスタンスで1本）と、その配置（インスタンスの並び順）
        struct VisibilityEntry
        {
            uint64_t Key = 0;
            uint64_t MeshId = 0;
            uint64_t ComponentId = 0;
            const void *ClusterBufferIdentity = nullptr;
            uint64_t ClusterBufferOffsetBytes = 0;
            uint32_t ClusterCount = 0;
            uint32_t Offset = 0;
            uint64_t LastUsedFrame = 0;
        };
        struct RetiredBuffer
        {
            RHI::BufferPtr Buffer;
            uint64_t RetiredFrame = 0;
        };
        RHI::BufferPtr m_VisibilityBuffer; // uint32_t[]。1=前のフレームで見えた
        Container::VariableArray<VisibilityEntry> m_VisibilityEntries;
        Container::VariableArray<RetiredBuffer> m_RetiredBuffers;
        uint64_t m_OcclusionFrameCount = 0;

        // 統計（MEGA_OCCLUSION）。ホストが読めるバッファを数フレームずつ遅らせて読み戻す（GPUを待たない）。
        struct StatsSlot
        {
            RHI::BufferPtr Buffer;
            const uint32_t *Mapped = nullptr;
            uint64_t Frame = 0;
            uint64_t RenderFrame = 0;  // 描画のフレームの番号（ViewRenderContext::FrameNumber）
            int64_t EpochFrame = -1;   // 決定的な撮影のエポックからの相対フレーム。エポック前は -1
            bool bPending = false;
        };
        static constexpr uint32_t StatsSlotCount = 4;
        bool m_bStatsEpochActive = false;
        bool m_bDropVisibilityContinuity = false; // 決定的な撮影のエポックの最初のフレーム: 見えたビットを引き継がない
        StatsSlot m_StatsSlots[StatsSlotCount];
        bool m_bStatsSlotsTried = false;
        bool m_bStatsLoggedOnce = false;
        bool m_bOcclusionFallbackLogged = false;
        bool m_bBatchUnsupportedLogged = false;

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
