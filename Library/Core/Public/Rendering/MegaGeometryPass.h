#pragma once

#include "Rendering/FrameUseRing.h"
#include "Rendering/HiZPyramidPass.h"
#include "Rendering/IViewPass.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstddef>

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    class SceneRenderer;
    class VisibilityRasterPass;
    class VisibilityResolvePass;
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
     * 2. その時点の深度（GBufferPassの不透明＋1パス目）からHZBを作る。ビジビリティバッファの幾何の解決が GBuffer を書くとき
     *    （--visibility-buffer=on）は GBuffer の描画を止めるので、ID のラスタ（VisibilityRasterPass）が記録を駆動し、
     *    ID・深度へ描いた手続き・スキニングの塊と1パス目の深度から HZB を作る（IDrawSink）。
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

        class IDrawSink;

        /**
         * @brief フレームの記録（クラスタカリングと描画）
         * @param sink null なら従来どおり GBuffer の render pass へ描く。null でないとき、GBuffer の添付の状態遷移
         *        （render pass の開始と終了）は呼び出し側が済ませてあるものとして、このパスは GBuffer の render pass を開かず、
         *        2パスの遮蔽のときの描画を sink が行う。1回の判定（2パスにならなかったとき）は sink を呼ばず、
         *        描画の写し（TakeVisibilityDrawPlan）を残して終える
         */
        void RecordFrameCommand(const MegaGeometryPassCommand &command,
                                RHI::ICommandList *commandList,
                                IDrawSink *sink = nullptr);

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

        // ========================================
        // ビジビリティバッファ連携
        // ========================================

        /**
         * @brief そのフレームの描画（クラスタカリングの結果）をビジビリティバッファのラスタが引くための写し
         *
         * 1パス目・2パス目の IndirectDraw コマンド（区間ごとの連続した範囲）と、それを描くのに要るバッファ。
         * RecordFrameCommand が、SetVisibilityDrawPlanEnabled(true) のときだけ作る。
         */
        struct VisibilityDrawPlan
        {
            /** @brief 材質の区間1つ分（頂点・インデックスはプールの塊の先頭から引く） */
            struct Section
            {
                RHI::BufferPtr VertexBuffer;
                RHI::BufferPtr IndexBuffer;
                uint32_t Capacity = 0;    // 1パスのコマンドの最大数
                uint32_t CommandBase = 0; // 1パスの範囲での先頭（コマンドの位置）
                MegaGeometry::MegaMeshMaterial Material; // 区間の材質（値。ビジビリティバッファの材質の表が引く）
            };

            bool bValid = false;
            uint32_t PassCount = 0;        // 1（従来の1回の判定）か 2（2パスの遮蔽）
            uint32_t SectionCount = 0;
            uint32_t CommandsPerPass = 0;  // 2パス目の範囲は、1パス目の範囲の後ろ
            uint32_t CommandsTotal = 0;    // 全パスのコマンドの数（記録の番号は 1 + コマンドの通しの位置）
            bool bUseIndirectCount = false;
            RHI::BufferPtr IndirectBuffer; // DrawIndexedIndirectCommand[]（IndirectArgument の状態で渡す）
            RHI::BufferPtr CountBuffer;    // 区間ごとのカウンタ（添え字は パス × 区間の数 + 区間。IndirectArgument の状態で渡す）
            RHI::BufferPtr DrawInfoBuffer; // コマンドごとの描画情報（uvec2: インスタンスの番号・payload。GenericRead の状態で渡す）
            RHI::BufferPtr InstanceBuffer; // インスタンスの表（GPUMegaInstance[]。ホストが書いたまま）
            uint64_t InstanceBufferBytes = 0;
            RHI::BufferPtr SectionBuffer;  // 区間の表（uvec2: コマンドの先頭・最大数。パス × 区間。ホストが書いたまま）
            uint64_t SectionBufferBytes = 0;
            /**
             * ソフトウェアラスタの一覧（振り分けを行ったフレームだけ。行わなければ null・容量 0）。UnorderedAccess の状態で渡す。
             * パスごとの頭 4 語（間接 dispatch の引数）・8 語の余白の後に、パスごとにコマンドの位置の列が並ぶ（cluster_cull.comp の SwRasterBuffer）
             */
            RHI::BufferPtr SwRasterBuffer;
            uint32_t SwRasterCapacity = 0; // 一覧のパスごとの容量（クラスタ数。パスごとのコマンド数まで）
            float SwRasterMaxPixels = 0.0f; // 振り分けたしきい値（画面上の半径、画素）。ソフトの矩形の上限を決める
            /**
             * 統計のバッファ（UnorderedAccess の状態。bStatsEnabled が偽のときは代わりのバッファ）。ソフトのラスタが、描かなかった三角形・走ったワークグループを数える
             */
            RHI::BufferPtr StatsBuffer;
            bool bStatsEnabled = false;
            Container::VariableArray<Section> Sections;
        };

        /**
         * @brief 2パスの遮蔽の途中で、ID・深度へ描く側（VisibilityRasterPass）が呼ばれる受け口
         *
         * 幾何の解決が GBuffer を書くとき、2パスの遮蔽の HZB は GBuffer ではなく ID のラスタの深度から作る。そのため
         * RecordFrameCommand が次の順で呼び出す（sink を渡したときだけ）:
         *   1パス目のカリング → RecordFirstPassDraws → RecordMergeBeforeHiZ → HZB の生成 → 2パス目のカリング → RecordSecondPassDraws
         * 描画の写し plan は両方の呼び出しで同じ内容（1パス目のカリングの前に作る）。バッファの状態は、1回目の呼び出しの時点で
         * 1パス目の範囲が IndirectArgument（描画情報は GenericRead）、2回目の時点で2パス目の範囲まで書き終えている。
         * 呼び出しの間に sink が必要とする状態の遷移は sink が行い、RecordFrameCommand が戻った後の戻し
         * （ReleaseVisibilityDrawBuffers）も sink 側の責任になる。
         *
         * ソフトウェアラスタの一覧（plan.SwRasterBuffer）は、sink が読むとき（間接 dispatch の引数と一覧の読み取り）だけ GenericRead にし、
         * 戻る時点では UnorderedAccess に戻す（次のカリングが続けて書き、最後に RecordFrameCommand が Common へ戻す）。
         */
        class IDrawSink
        {
        public:
            virtual ~IDrawSink() = default;

            /**
             * @brief このフレームで、ソフトウェアラスタ（一覧のクラスタを計算シェーダーで 64bit のバッファへ描く）が確実に走るか
             *
             * RecordFrameCommand がカリングの前に 1 回だけ問い合わせる。true を返したら、sink は 2 回の呼び出しの中で
             * 一覧を dispatch する責任を負い、カリングは一覧へ積めたクラスタのハードのコマンドを空振り（instanceCount = 0）にする。
             * false なら、一覧へ積むだけでハードがすべてのクラスタを描く（振り分けの統計だけが取れる）。
             */
            virtual bool IsSwRasterAvailable() const = 0;

            /** @brief これまでに記録したソフトウェアラスタの dispatch の累計（確認用。フレームの差を統計に載せる）。ソフトを持たない sink は 0 */
            virtual uint32_t GetSwRasterDispatchCount() const { return 0; }

            /** @brief 1パス目のカリングの後。ID・深度へ手続き・スキニングの塊と1パス目を描く（深度は描いた後 ShaderResource になる） */
            virtual void RecordFirstPassDraws(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
            /**
             * @brief 1回目の描画の後・HZB を作る前。ハードのラスタの外で書かれた ID・深度（ソフトウェアラスタの 64bit のバッファ）を、
             *        HZB の元の深度へ合流させる。2 パスの遮蔽の構成では毎フレーム呼ばれる（RecordFirstPassDraws が描画を記録できなかったフレームは、sink 側で何もしない）
             *
             * 深度は呼ばれる時点と戻る時点のどちらも ShaderResource の状態（HZB がそのまま読む）。
             */
            virtual void RecordMergeBeforeHiZ(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
            /** @brief 2パス目のカリングの後。ID・深度へ2パス目を描く */
            virtual void RecordSecondPassDraws(RHI::ICommandList *commandList, const VisibilityDrawPlan &plan) = 0;
        };

        /**
         * @brief ビジビリティバッファの描画の写しを作るか（既定は作らない）
         *
         * 作るとき、RecordFrameCommand は IndirectDraw・カウンタ・描画情報のバッファを次のフレーム用に戻さず、
         * ビジビリティバッファのラスタが読めるまま残す（ラスタが使い終わったら ReleaseVisibilityDrawBuffers で戻す）。
         */
        void SetVisibilityDrawPlanEnabled(bool bEnabled) { m_bVisibilityPlanEnabled = bEnabled; }
        bool IsVisibilityDrawPlanEnabled() const { return m_bVisibilityPlanEnabled; }

        /**
         * @brief ソフトウェアラスタの振り分けを要求するか（--sw-raster=on。既定は要求しない）
         *
         * 要求しても、64bit アトミックが使えない・ビジビリティバッファのラスタが無い・2 パスの遮蔽の判定を使えないときは、
         * 振り分けず SW_RASTER_FALLBACK reason=<理由> を 1 回ログへ出す。使えるとき、カリングは画面上の半径（画素）が
         * maxPixels 以下で近平面と交わらないクラスタを、パスごとのソフトの一覧（コマンドの位置）へ積み、計算シェーダーの
         * 間接 dispatch の引数（2 次元）を書く。sink（IDrawSink::IsSwRasterAvailable）がソフトのラスタを走らせると答えたフレームは、
         * 一覧へ積めたクラスタのハードのコマンドを空振り（instanceCount = 0）にする。答えなかったフレームは、
         * ハードがすべてのクラスタを描く（コマンドは減らさない）。
         */
        void SetSwRasterBinning(bool bRequested, float maxPixels)
        {
            m_bSwRasterRequested = bRequested;
            m_SwRasterMaxPixels = maxPixels;
        }
        bool IsSwRasterBinningRequested() const { return m_bSwRasterRequested; }

        /**
         * @brief 設定されている振り分けのしきい値（画面上の半径、画素）
         *
         * 製品の経路はこの getter を通らない（カリングの定数と VisibilityDrawPlan へ内部の値を直接入れる）。
         * SceneView が組んだ設定が MegaGeometryPass まで届いたことを、テストが読んで確かめるために置いている
         */
        float GetSwRasterMaxPixels() const { return m_SwRasterMaxPixels; }

        /** @brief 最後の RecordFrameCommand がソフトウェアラスタの振り分けを行ったか */
        bool DidSwRasterBin() const { return m_bSwRasterBinned; }

        /** @brief ソフトの一覧のパスごとの容量（クラスタ数）。振り分けを行っていなければ 0 */
        uint32_t GetSwRasterListCapacity() const { return m_SwRasterCapacity; }

        /**
         * @brief GBuffer への描画を止めるか（既定は止めない）
         *
         * ビジビリティバッファの幾何の解決が GBuffer を書くとき（--visibility-buffer=on）に true にする。止めても、
         * カリング・遮蔽の判定・描画の写しの作成・render pass の開始と終了（GBuffer の添付の状態遷移）は行い、描画の呼び出しだけを省く。
         * 解決が実際に使えないとき（SetVisibilityResolvePass の相手の GetFallbackReason が None 以外。相手を渡していないときは
         * 装置が VisibilityResolveGeometry::IsSupported を満たさないとき）は、true でも描く。
         *
         * 2 パスの遮蔽の HZB は、止めている間は ID のラスタの深度から作る（SetVisibilityRasterPass で渡した相手が描くとき。
         * IsFrameRecordDeferred）。渡していない・相手が描けないときは GBuffer の深度から作るので、1 パス目のクラスタが
         * 深度に入らず、判定は隠れていない側（描く側）に倒れるだけで、見える物は欠けない。
         */
        void SetSkipGBufferDraw(bool bSkip) { m_bSkipGBufferDraw = bSkip; }
        bool IsSkipGBufferDraw() const { return m_bSkipGBufferDraw; }

        /** @brief 記録したフレームの数（フレームの通し番号が変わるたびに 1 進む。通し番号が無い記録は記録ごとに 1 進む） */
        uint64_t GetRenderFrameCount() const { return m_RenderFrameCount; }
        /** @brief フレームごとの資源の組の数（選んだ飛行中のフレームの番号の枠。検査用） */
        uint32_t GetFrameSlotCapacity() const { return m_FrameSlots.GetCapacity(); }
        /** @brief 手放して GPU の使い終わりを待っているバッファの数（検査用） */
        size_t GetRetiredBufferCount() const { return m_RetiredBuffers.size(); }
        /**
         * @brief ID・深度へ描く相手（同じ View の VisibilityRasterPass）。渡すと、解決が GBuffer を書くフレームは、
         *        相手の Execute が記録を駆動する（Execute(context) は記録をキューへ積まず、GBuffer の添付の状態遷移だけを行う）
         */
        void SetVisibilityRasterPass(const VisibilityRasterPass *pass) { m_VisibilityRaster = pass; }
        const VisibilityRasterPass *GetVisibilityRasterPass() const { return m_VisibilityRaster; }

        /**
         * @brief このフレームの記録を ID のラスタへ移したか（Execute(context) が決める。取り出すと false に戻る）
         *
         * true のとき、ラスタが BuildMegaGeometryPassCommand で作ったコマンドを RecordFrameCommand（sink 付き）へ渡す。
         */
        bool TakeFrameRecordDeferred()
        {
            const bool bDeferred = m_bFrameRecordDeferred;
            m_bFrameRecordDeferred = false;
            return bDeferred;
        }
        bool IsFrameRecordDeferred() const { return m_bFrameRecordDeferred; }

        /** @brief 解決が使えるかの問い合わせ先（同じ View の VisibilityResolvePass。null なら装置の機能だけで判定） */
        void SetVisibilityResolvePass(const VisibilityResolvePass* pass) { m_ResolvePass = pass; }
        const VisibilityResolvePass* GetVisibilityResolvePass() const { return m_ResolvePass; }

        /**
         * @brief 最後の RecordFrameCommand が作った描画の写しを取り出す（取り出すと空になる）
         * @return 写しがあれば true。そのフレームに描いたクラスタが無い・写しを作らない設定なら false
         */
        bool TakeVisibilityDrawPlan(VisibilityDrawPlan &outPlan);

        /**
         * @brief 残してあった IndirectDraw・カウンタ・描画情報のバッファを、次のフレーム用の状態（Common）へ戻す
         *
         * ビジビリティバッファのラスタが使い終わった後に呼ぶ。次の RecordFrameCommand は、残っていれば自分で戻す。
         * @param indirectState IndirectDraw・カウンタの今の状態（渡されたまま触らなければ IndirectArgument）。
         *        描画情報は GenericRead
         */
        void ReleaseVisibilityDrawBuffers(RHI::ICommandList *commandList,
                                          RHI::ResourceState indirectState = RHI::ResourceState::IndirectArgument);

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
            uint32_t bSwRasterEnabled;    // 1 ならソフトウェアラスタの一覧へ積む（ハードも描く）。2 なら積めたクラスタのハードのコマンドを空振りにする
            uint32_t SwRasterCapacity;    // パスごとのソフトの一覧の容量（クラスタ数）
            float SwRasterMaxPixels;      // 振り分ける画面上の半径（画素）のしきい値
            float SwRasterNearPlane;      // 近平面までの距離
        };
        // ソフトウェアラスタの 4 語は、行列 2 つ・視点・平面 6 つ・語 21 個の後ろに並ぶ（cluster_cull.comp の CullUniforms と同じ std140 の位置）
        static_assert(offsetof(CullUniformData, bSwRasterEnabled) == (16 + 16 + 4 + 24 + 21) * sizeof(uint32_t),
                      "cluster_cull.comp の CullUniforms と並びが一致しません");

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

        /** @brief カリングのディスクリプタセットの形（cluster_cull.comp の binding 0〜13）。パイプラインとセットが同じ形を使う */
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

        /** @brief ホストが毎フレーム書く資源の組。1 回の記録（ビューポート）ごとに FrameUseRing から 1 組使う */
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

        /**
         * @brief ソフトウェアラスタの一覧のバッファを、パスごとに capacity 個のクラスタを収められる大きさで用意する
         *
         * 足りないときだけ作り直し、古いものは m_RetiredBuffers へ回す。
         */
        bool EnsureSwRasterBuffer(uint32_t capacity);

        /** @brief フレームスロットに、BVH のたどりの段 stageCount 個ぶんの UBO・ディスクリプタセットを用意する */
        bool EnsureBvhStageResources(FrameSlot &slot, uint32_t passIndex, uint32_t stageCount);

        /**
         * @brief 2パスの遮蔽カリングで描けるか。描けるときはHZBを深度の大きさに合わせる
         *
         * 無効（--mega-occlusion=off）・HZBが作れない・描く範囲が深度の全体と一致しない（遮蔽の判定は深度の
         * 全体を画面と見る）ときは false で、従来の1回の判定で描く。
         */
        bool CanUseTwoPassOcclusion(const MegaGeometryPassCommand &command);

        /** @brief GBuffer の描画を止めるか（止める設定で、解決が実際に使えるとき） */
        bool ShouldSkipGBufferDraw(DebugViewMode mode) const;

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

        // ホストが毎フレーム書く資源。同じパスが1フレームに何回記録されても（複数のビューポート）、提出前・GPU が読み終わる前の
        // 組を上書きしないよう、記録の回数ではなくフレームの通し番号で使用済みの位置を戻す（FrameUseRing）
        FrameUseRing<FrameSlot> m_FrameSlots;
        // 記録したフレームの数え（フレームの通し番号が変わるたびに 1 進む）。退避したバッファの寿命と統計の枠の判定に使う
        // （m_OcclusionFrameCount は記録ごとに進むので、1フレームに複数回記録すると実時間が短くなる）
        uint64_t m_RenderFrameCount = 0;
        uint64_t m_LastRenderFrameSerial = 0;

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

        // ソフトウェアラスタの振り分け: パスごとの一覧と間接 dispatch の引数（binding 13）。振り分けないフレームは触らず、
        // 代わりのバッファ（m_DummyStatsBuffer）を結ぶ
        bool m_bSwRasterRequested = false;
        float m_SwRasterMaxPixels = 32.0f;
        RHI::BufferPtr m_SwRasterBuffer;
        uint32_t m_SwRasterBufferCapacity = 0; // m_SwRasterBuffer がパスごとに収められるクラスタ数
        uint32_t m_SwRasterCapacity = 0;       // 最後に振り分けたフレームの、パスごとの一覧の容量
        bool m_bSwRasterBinned = false;
        bool m_bSwRasterFallbackLogged = false;

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
            uint64_t RetiredFrame = 0; // 退避したときの m_RenderFrameCount
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
            uint64_t RenderFrameCount = 0; // 書いたときの m_RenderFrameCount
            int64_t EpochFrame = -1;   // 決定的な撮影のエポックからの相対フレーム。エポック前は -1
            bool bPending = false;
            bool bSwRasterBin = false; // このフレームがソフトウェアラスタの振り分けを行い、統計の 4〜7 が有効
            uint32_t SwDispatches = 0; // このフレームで sink が記録したソフトの dispatch の数（GPU の統計ではなく CPU の記録の数）
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

        // ビジビリティバッファのラスタへ渡す描画の写し（TakeVisibilityDrawPlan が取り出す）
        bool m_bVisibilityPlanEnabled = false;
        // GBuffer への描画を止めるか（ビジビリティバッファの解決が GBuffer を書くとき）
        bool m_bSkipGBufferDraw = false;
        const VisibilityResolvePass* m_ResolvePass = nullptr;
        const VisibilityRasterPass* m_VisibilityRaster = nullptr;
        // このフレームの記録を ID のラスタへ移したか（Execute(context) が決め、ラスタが TakeFrameRecordDeferred で取り出す）
        bool m_bFrameRecordDeferred = false;
        VisibilityDrawPlan m_VisibilityPlan;
        // 描画の写しを作ったフレームの IndirectDraw・カウンタ・描画情報のバッファが、Common へ戻されないまま残っているか
        bool m_bVisibilityBuffersHeld = false;
        RHI::BufferPtr m_HeldIndirectDrawBuffer;
        RHI::BufferPtr m_HeldDrawCountBuffer;
        RHI::BufferPtr m_HeldDrawInfoBuffer;

        // 現在のGBufferサイズ
        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bPreferRenderGraphGBufferResources = false;
        bool m_bGBufferRenderPassUsesRenderGraphAttachmentStates = false;
        bool m_bMegaGeometryDebugPayloadUnsupportedWarned = false;
        // フレームの通し番号が無い記録の通知を一度だけ出すための印
        bool m_bUnsetFrameSerialWarned = false;

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
