#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/MeshIndexChunks.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityMaterialTable.h"
#include "Rendering/VisibilityMerge.h"
#include "Rendering/VisibilitySwRaster.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class SkinningComputePass;
    class VisibilityResolvePass;
    struct CameraProxy;
    struct ViewRenderContext;

    /**
     * @brief 1フレームぶんの描画の内訳（検査・ログ用）
     */
    struct VisibilityRasterFrameStats
    {
        /** @brief MegaGeometry のクラスタの記録の枠の数（全パスのコマンドの数。GPU が積んだぶんだけ書かれる） */
        uint32_t MegaCommandSlots = 0;
        /** @brief 手続きメッシュの塊の記録の数（描画の数と同じ） */
        uint32_t ProceduralRecords = 0;
        /** @brief スキニングの塊の記録の数（描画の数と同じ） */
        uint32_t SkinnedRecords = 0;
        /** @brief 記録の表の枠の数（0 番の空の記録を含む） */
        uint32_t TotalSlots = 0;
        /** @brief 容量を超えた・使えない記録で描かなかった塊の数 */
        uint32_t DroppedChunks = 0;
        /** @brief フレームの材質の表に足した、値の違う材質の数（上限を超えても数える） */
        uint32_t MaterialUnique = 0;
        /** @brief 材質の表の件数の上限（予備の番号を含む） */
        uint32_t MaterialLimit = 0;
        /** @brief 上限を超えて予備の番号へ寄せた、値の違う材質の数 */
        uint32_t MaterialOverflowed = 0;
        /** @brief ID を書いたか（false なら何も描かずに戻った） */
        bool bRendered = false;
        /** @brief 64bit のバッファ（深度 + ID）を空で埋め、ID・深度へ合流させたか（対応しない装置・予備の経路では false） */
        bool bMerged = false;
        /** @brief 64bit のバッファの ID・深度への合流を記録した回数（2 パスの遮蔽は HZB の前と 2 パス目の後の 2 回、1 回の判定は 1 回） */
        uint32_t MergeCount = 0;
        /** @brief 64bit のバッファのバイト数（画面の画素数 × 8。持たないときは 0） */
        uint64_t KeyBufferBytes = 0;
        /** @brief ソフトウェアラスタの dispatch を記録した回数（1 パス目・2 パス目で 1 回ずつ。走らないフレームは 0） */
        uint32_t SwRasterDispatchCount = 0;
    };

    /**
     * @brief 不透明の描画のすべてを、画素ごとの ID（VisBuffer.Id）と深度（GBuffer.Depth）へ描く RenderGraph のパス
     *
     * 描くもの（今の GBuffer の描画は変えず、その後ろに足す。深度は GBuffer と同じ式で、LessEqual で比べる）:
     *  - MegaGeometry のクラスタ: MegaGeometryPass のカリング（2パスの遮蔽・BVH・ページの経路のまま）が積んだ
     *    IndirectDraw コマンドを、位置だけを読む頂点シェーダーでもう一度描く。記録の番号は 1 + コマンドの通しの位置で、
     *    記録は GPU（visbuffer_records.comp）が、そのフレームに積まれたコマンドから書く。
     *  - 手続きメッシュの塊: 不透明の描画のインデックスの範囲を128三角形以下の塊に分け、塊ごとに1回描く。
     *  - スキニングの塊: SkinningComputePass が変形した頂点（ワールド空間）を読み、同じく塊ごとに1回描く。
     * 記録の番号は描画ごとの値（頂点シェーダーが開始インスタンスから渡す）、記録の中の三角形は gl_PrimitiveID。
     *
     * 記録の表（storage buffer）は、0 番が空、1 番から MegaGeometry のコマンド、その後ろに手続き・スキニングの塊を置く。
     * 表はこのパスが持つフレームごとのバッファで、書いた後は GenericRead のまま残る（検証表示が読む）。
     *
     * MegaGeometry の 2 パスの遮蔽は、HZB を ID・深度の 1 パス目から作る。幾何の解決が GBuffer を書くとき（GBuffer の描画を止めるので、
     * 深度は ID のラスタだけが書く）、MegaGeometryPass は記録をこのパスの Execute へ移し（MegaGeometryPass::IsFrameRecordDeferred）、
     * 次の順で互いを呼ぶ（MegaGeometryPass::IDrawSink）:
     *   1 パス目のカリング → [ID の render pass: 手続き・スキニングの塊 → MegaGeometry の 1 パス目] → HZB（深度から）
     *   → 2 パス目のカリング → 記録を書く計算 → [ID の render pass: MegaGeometry の 2 パス目]
     * 1 回の判定のとき・移さないとき（GBuffer が先に描く構成）は、MegaGeometry の全部と塊を 1 回の render pass で描く。
     *
     * ソフトウェアラスタが有効（SetSwRasterEnabled。--sw-raster=on）で、装置がソフトウェアラスタを使えるとき
     * （VisibilitySwRaster::IsSupported。64bit のバッファへの atomicMin とバッファのアドレス）は、ソフトウェアラスタの結果を受ける
     * 64bit のバッファ（画面の画素数 × uint64。深度 + ID）を 1 つ持ち、フレームの最初に空で埋め、合流のパス（VisibilityMerge。
     * 全画面で、空でない画素だけ LessOrEqual で ID・深度へ書く）を「1 回目の render pass の後・HZB の前」と「2 回目の
     * render pass の後」に走らせる（1 回の判定では描画の後に 1 回）。
     * ソフトウェアラスタが無効（既定）・対応しない装置（バッファのアドレスが無い装置を含む）・予備の経路（ビジビリティバッファが無効）では、資源もパスも作らない。
     *
     * 2 パスの遮蔽で 64bit のバッファを使えるフレーム（IsSwRasterAvailable）は、MegaGeometryPass のカリングが画面上で小さいクラスタを
     * ソフトの一覧へ積み、そのハードのコマンドを空振りにする。ソフトの dispatch（VisibilitySwRaster。1 ワークグループ = 1 クラスタ）は
     * 1 パス目: [記録を書く計算 → 64bit のバッファを書き込みへ → dispatch → 読み取りへ] → 合流（HZB の前）、
     * 2 パス目: 記録を書く計算（全パス。1 パス目のぶんは同じ値で書き直される）→ [ID の render pass: 2 パス目のハードの描画]
     * → [64bit のバッファを書き込みへ → dispatch → 読み取りへ] → 合流 の順に記録する（ソフトの dispatch は、記録とハードの描画の後ろに置く）。
     *
     * 既定は無効（SceneView::SetupDeferredPipeline の VisibilityBufferMode が Off）。
     */
    class VisibilityRasterPass final : public IViewPass, public IRenderGraphPass, private MegaGeometryPass::IDrawSink
    {
    public:
        VisibilityRasterPass();
        ~VisibilityRasterPass() override;

        const char* GetName() const override { return "VisibilityRasterPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief MegaGeometry のカリング結果の取り出し元（同じ View のパス。null なら MegaGeometry は描かない） */
        void SetMegaGeometryPass(MegaGeometryPass* pass) { m_MegaGeometryPass = pass; }
        /** @brief スキニングの変形結果の取り出し元（同じ View のパス。null ならスキニングは描かない） */
        void SetSkinningComputePass(const SkinningComputePass* pass) { m_SkinningComputePass = pass; }
        /**
         * @brief 解決が使えるかの問い合わせ先（同じ View の VisibilityResolvePass。null なら問い合わせない）
         *
         * 渡すと、解決が使えない（GetFallbackReason が None 以外）フレームは Declare が何も宣言しない。ID は解決と分類だけが読み、
         * 予備の GBuffer の描画へ戻っている間は誰も読まないので、描いても GPU の時間を使うだけになる。
         * 解決を持たない構成（検証表示の Debug）では渡さず、従来どおり描く。
         */
        void SetResolvePass(const VisibilityResolvePass* pass) { m_ResolvePass = pass; }
        const VisibilityResolvePass* GetResolvePass() const { return m_ResolvePass; }

        /**
         * @brief ソフトウェアラスタを使うか（既定は使わない。Initialize の前に決める）
         *
         * false の間は、64bit のバッファとその埋め・合流のパスを作らない（1280x720 で約 7 MB と毎フレーム約 0.03 ms を使わない）。
         */
        void SetSwRasterEnabled(bool bEnabled) { m_bSwRasterEnabled = bEnabled; }
        bool IsSwRasterEnabled() const { return m_bSwRasterEnabled; }

        /** @brief 64bit のバッファの合流（無効・対応しない装置・初期化に失敗したときは IsReady が false） */
        const VisibilityMerge& GetMerge() const { return m_Merge; }
        /** @brief ソフトウェアラスタの計算（無効・対応しない装置・初期化に失敗したときは IsReady が false） */
        const VisibilitySwRaster& GetSwRaster() const { return m_SwRaster; }

        /** @brief 最後の Execute の内訳 */
        const VisibilityRasterFrameStats& GetLastFrameStats() const { return m_Stats; }

        /**
         * @brief ID を描けるか（初期化を済ませ、render pass と 4 つのパイプラインが揃っている）
         *
         * false の間は Declare が ID を宣言せず、Execute も何も描かない。ID を読む解決は使えないので、
         * 解決が GBuffer を書く構成でもこの間は GBuffer の描画を止めてはならない。
         *
         * mode が Wireframe のときは、三角形を線で描くパイプライン（PolygonMode::Line。MegaGeometry・手続き・スキニングの
         * 3 種）も揃っていることを求める。線のパイプラインが作れていない装置では false になり、従来の GBuffer の
         * ワイヤーフレームの描画へ戻る（development ビルドだけ。Release は表示を Normal に丸めるので線のパイプラインを作らない）。
         */
        bool IsDrawReady(DebugViewMode mode = DebugViewMode::Normal) const;

        /** @brief 最後の Execute が書いた記録の表（GenericRead の状態。書かなかったフレームは null） */
        const RHI::BufferPtr& GetRecordTable() const { return m_LastRecordTable; }
        /** @brief 記録の表の、使っている範囲のバイト数 */
        uint64_t GetRecordTableBytes() const { return m_LastRecordTableBytes; }

        /**
         * @brief 最後の Execute が書いた、フレームの材質の表（VisibilityBuffer::MaterialEntry の並び。書かなかったフレームは null）
         *
         * 記録の MaterialIndex（0 から詰めた番号）がこの表の添え字。ホストが書いたままの storage buffer。
         */
        const RHI::BufferPtr& GetMaterialTable() const { return m_LastMaterialTable; }
        /** @brief 材質の表の、使っている範囲の件数 */
        uint32_t GetMaterialTableCount() const { return m_LastMaterialTableCount; }
        /**
         * @brief 最後の Execute が GPU の表へ書いた材質の中身（CPU 側のコピー。添え字が GPU の表と同じ）
         *
         * 材質の解決が、材質ごとにテクスチャ（ハンドル → RHI のテクスチャ）を張るために引く。書かなかったフレームは空。
         */
        const Container::VariableArray<VisibilityBuffer::MaterialEntry>& GetMaterialEntries() const
        {
            return m_LastMaterialEntries;
        }

        /**
         * @brief 最後の Execute が描いた MegaGeometry のインスタンスの表（書かなかった・MegaGeometry を描かなかったフレームは null）
         *
         * 記録の InstanceIndex（MegaGeometry のクラスタ）がこの表の添え字。表の中身は MegaGeometryPass のフレームごとのバッファで、
         * 幾何の解決が変換（今・前）を引くために読む。使っている範囲のバイト数も返す。
         */
        const RHI::BufferPtr& GetMegaInstanceBuffer() const { return m_LastMegaInstanceBuffer; }
        uint64_t GetMegaInstanceBufferBytes() const { return m_LastMegaInstanceBytes; }

        RGResourceHandle GetIdHandle() const { return m_IdHandle.ToResourceHandle(); }
        RGResourceHandle GetDepthHandle() const { return m_DepthHandle; }

        /**
         * @brief このフレームに Execute で ID・深度を描く見込みか（Declare が ID と深度を宣言した）
         *
         * MegaGeometryPass が、記録をこのパスの Execute へ移してよいかの判定に使う（Declare は全パスの Execute より前に済む）。
         */
        bool IsExecutionPlanned() const { return m_IdHandle.IsValid() && m_DepthHandle.IsValid(); }

        /** @brief 手続きメッシュの塊の作業配列が持っている容量（毎フレームの確保をしない確認用。最初の Execute の後は増えない） */
        size_t GetProceduralChunkScratchCapacity() const { return m_ProceduralChunkScratch.capacity(); }
        /** @brief スキニングのメッシュの塊の作業配列が持っている容量（同上） */
        size_t GetSkinnedChunkScratchCapacity() const { return m_SkinnedChunkScratch.capacity(); }
        /** @brief 材質の表を GPU へ上げる並びの作業配列が持っている容量（同上） */
        size_t GetMaterialEntryScratchCapacity() const { return m_MaterialEntryScratch.capacity(); }
        /** @brief 区間から材質の表の番号への対応の作業配列が持っている容量（同上） */
        size_t GetSectionMaterialScratchCapacity() const { return m_SectionMaterialScratch.capacity(); }
        /** @brief フレームの材質の表（積み上げ）。件の配列・索引の容量の確認用 */
        const VisibilityBuffer::MaterialTable& GetMaterialTableBuilder() const { return m_MaterialTable; }

    private:
        /** @brief 1回の描画（塊 1 つ） */
        struct ChunkDraw
        {
            RHI::BufferPtr VertexBuffer; // 手続きメッシュだけ（スキニングは頂点を storage buffer から読む）
            RHI::BufferPtr IndexBuffer;
            uint32_t IndexCount = 0;
            uint32_t FirstIndex = 0;
            int32_t VertexOffset = 0;
            uint32_t RecordNumber = 0;
        };

        /**
         * @brief 1回の Execute が使う資源の組（GPU が前のフレームで読んでいるかもしれないため、Execute のたびに
         *        FrameUseRing から別の組を受け取る）
         */
        struct FrameSlot
        {
            /**
             * 記録の表。GPU 専用のメモリに置く（MegaGeometry の範囲を計算が書くので、ホスト可視のメモリだと
             * PCIe 越しの書き込みが時間の大半になる）。MegaGeometry の範囲は GPU が、残りはホストが
             * RecordUpload へ書いたものをコピーして作る
             */
            RHI::BufferPtr RecordTable;
            uint32_t RecordCapacity = 0; // 要素数
            RHI::ResourceState RecordState = RHI::ResourceState::Common;
            RHI::BufferPtr RecordUpload; // ホストが書く記録（手続き・スキニング）の置き場。host-visible で、コピーの元になる
            uint32_t RecordUploadCapacity = 0; // 要素数
            RHI::BufferPtr FrameUniform;                 // view・projection
            RHI::DescriptorSetPtr MegaSet;               // 描画: UBO・インスタンスの表・描画情報
            RHI::DescriptorSetPtr MeshSet;               // 描画: UBO・描画のインスタンスの表・記録の表
            RHI::DescriptorSetPtr SkinnedSet;            // 描画: UBO・変形した頂点・記録の表
            RHI::BufferPtr RecordParams;                 // 記録を書く計算: 区間の数・区間の表の要素数
            RHI::BufferPtr SectionAddresses;             // 記録を書く計算: 区間ごとの頂点・インデックスのアドレス
            uint32_t SectionAddressCapacity = 0;         // 要素数（uvec4）
            RHI::BufferPtr SectionMaterials;             // 記録を書く計算: 区間ごとの材質の表の番号
            uint32_t SectionMaterialCapacity = 0;        // 要素数（uint）
            RHI::BufferPtr MaterialTable;                // フレームの材質の表（MaterialEntry。ホストが書く）
            uint32_t MaterialTableCapacity = 0;          // 要素数（MaterialEntry）
            RHI::DescriptorSetPtr RecordSet;             // 記録を書く計算（引数を作る計算も同じ組を使う）
            RHI::BufferPtr RecordArgs;                   // 記録を書く計算の間接 dispatch の引数と、区間ごとのグループの先頭の番号
            uint32_t RecordArgsCapacity = 0;             // 区間の表の要素数（パス × 区間）の上限
            RHI::ResourceState RecordArgsState = RHI::ResourceState::Common;
        };

        /** @brief 描画の準備の結果 */
        enum class PrepareResult
        {
            /** @brief 描ける */
            Ready,
            /** @brief 描くものが無い（ID を空で消すだけ） */
            ClearOnly,
            /** @brief 資源を用意できなかった */
            Failed,
        };

        /** @brief 1 回の Execute の描画の準備と状態（Execute の間だけ使う。Execute のたびに作り直す） */
        struct FrameWork
        {
            ViewRenderContext* Context = nullptr;
            RHI::ICommandList* CommandList = nullptr;
            RHI::Viewport Viewport;
            RHI::ScissorRect Scissor;
            const CameraProxy* Camera = nullptr;
            RHI::BufferPtr SkinnedVertices;
            FrameSlot* Slot = nullptr;
            uint64_t TableBytes = 0;
            /** @brief RecordUpload から記録の表へコピーするバイト数（0 ならコピーしない）と、表の中の書き込み先の位置 */
            uint64_t UploadBytes = 0;
            uint64_t UploadDstOffset = 0;
            bool bHasMegaDraw = false;
            bool bDrawMesh = false;
            bool bDrawSkinned = false;
            RHI::PipelinePtr MegaPipeline;
            RHI::PipelinePtr MeshPipeline;
            RHI::PipelinePtr SkinnedPipeline;
            Container::VariableArray<VisibilityBuffer::DrawRecord> CpuRecords;
            Container::VariableArray<ChunkDraw> MeshDraws;
            Container::VariableArray<ChunkDraw> SkinnedDraws;
            Container::VariableArray<uint32_t> SectionAddresses;
            /** @brief MegaGeometry のコマンド・カウンタの今の状態（描画の写しがあるときの戻しに渡す） */
            RHI::ResourceState IndirectState = RHI::ResourceState::IndirectArgument;
            /** @brief 描画の写しのインスタンスの表（FinishFrame が最後の Execute の値として公開する） */
            RHI::BufferPtr MegaInstanceBuffer;
            uint64_t MegaInstanceBytes = 0;
            /** @brief MegaGeometryPass の記録が、2 パスの途中で呼び出し（IDrawSink）を使ったか */
            bool bSinkUsed = false;
            /** @brief 1 回目の呼び出しで描画の準備が済み、2 回目の呼び出しで MegaGeometry の 2 パス目を描けるか */
            bool bStagedReady = false;
            /** @brief 64bit のバッファを使えるフレームか（合流が準備でき、画面の大きさのバッファを用意できた） */
            bool bMerge = false;
        };

        bool CreateRenderPass();
        bool CreatePipelines(ViewRenderContext& context);

        /**
         * @brief 描画の準備（記録・材質の表・描画のディスクリプタセットを作り、ホストが書く資源を書く）
         * @param plan MegaGeometry の描画の写し（null なら MegaGeometry は描かない）
         */
        PrepareResult PrepareFrame(const MegaGeometryPass::VisibilityDrawPlan* plan);
        /** @brief ID を空で消すだけの render pass（描くものが無い・準備できなかったフレーム） */
        void RecordClearOnlyRenderPass();
        /**
         * @brief ホストが書いた記録（手続き・スキニング）を、記録の表へコピーして読める状態にする（render pass の外で呼ぶ）
         * @return コピーを記録して GenericRead まで進めたか（コピーが無いときは何もせず false）
         */
        bool RecordCpuRecordUpload();
        /** @brief MegaGeometry のコマンド・カウンタを読める状態にし、記録を書く計算を dispatch する */
        void RecordMegaRecords(const MegaGeometryPass::VisibilityDrawPlan& plan, const char* timestampName = "VisRasterRecords");
        /** @brief MegaGeometry のクラスタを、パスの番号 firstPass から endPass の手前まで描く（render pass の中で呼ぶ） */
        void RecordMegaDraws(const MegaGeometryPass::VisibilityDrawPlan& plan, uint32_t firstPass, uint32_t endPass);
        /** @brief 手続きメッシュとスキニングの塊を描く（render pass の中で呼ぶ） */
        void RecordChunkDraws();
        /** @brief 64bit のバッファを空で埋める（render pass の外で呼ぶ。使えないフレームは何もしない） */
        void RecordMergeClear();
        /** @brief 64bit のバッファを ID・深度へ合流させる render pass を記録する（render pass の外で呼ぶ。2 回目の render pass の形） */
        void RecordMergePass(const char* timestampName);
        /**
         * @brief ソフトの一覧のパス passIndex を、計算シェーダーで 64bit のバッファへ描く（render pass の外で呼ぶ）
         *
         * 64bit のバッファは GenericRead から UnorderedAccess を経て GenericRead へ戻る。一覧は UnorderedAccess から GenericRead
         * （間接 dispatch の引数と読み取り）を経て UnorderedAccess へ戻る。記録の表は GenericRead のまま読む。
         * @return 記録したら true（この呼び出しが使えないフレームは何もせず false）
         */
        bool RecordSwRaster(const MegaGeometryPass::VisibilityDrawPlan& plan, uint32_t passIndex, const char* timestampName);
        /** @brief 描き終えた後の戻しと、最後の Execute の値・ログの更新 */
        void FinishFrame();

        // MegaGeometryPass::IDrawSink（記録を移されたフレームの、2 パスの遮蔽の途中の描画）
        bool IsSwRasterAvailable() const override;
        uint32_t GetSwRasterDispatchCount() const override { return m_Stats.SwRasterDispatchCount; }
        void RecordFirstPassDraws(RHI::ICommandList* commandList,
                                  const MegaGeometryPass::VisibilityDrawPlan& plan) override;
        void RecordMergeBeforeHiZ(RHI::ICommandList* commandList,
                                  const MegaGeometryPass::VisibilityDrawPlan& plan) override;
        void RecordSecondPassDraws(RHI::ICommandList* commandList,
                                   const MegaGeometryPass::VisibilityDrawPlan& plan) override;

        /** @brief 3 種の線のパイプラインが揃っているか（development ビルド以外では常に false） */
        bool HasWireframePipelines() const;
        bool EnsureFramebuffer(const RHI::TexturePtr& idTexture, const RHI::TexturePtr& depthTexture);
        bool EnsureFrameSlot(FrameSlot& slot,
                             uint32_t recordCapacity,
                             uint32_t sectionCount,
                             uint32_t sectionSlotCount,
                             uint32_t cpuRecordCount,
                             uint32_t materialCount);

        /** @brief 塊に分けられなかった通知を、パスの寿命の中で一度だけ出す */
        void LogChunkFailureOnce();
        /** @brief 不透明の描画から手続きメッシュの塊の記録と描画を集める（材質は materials へ足した番号で記録する） */
        void CollectProceduralChunks(ViewRenderContext& context,
                                     uint32_t recordBase,
                                     VisibilityBuffer::MaterialTable& materials,
                                     Container::VariableArray<VisibilityBuffer::DrawRecord>& records,
                                     Container::VariableArray<ChunkDraw>& draws);
        /** @brief SkinningComputePass の変形結果からスキニングの塊の記録と描画を集める */
        void CollectSkinnedChunks(ViewRenderContext& context,
                                  uint32_t recordBase,
                                  VisibilityBuffer::MaterialTable& materials,
                                  Container::VariableArray<VisibilityBuffer::DrawRecord>& records,
                                  Container::VariableArray<ChunkDraw>& draws);

        RHI::IDevice* m_Device = nullptr;
        MegaGeometryPass* m_MegaGeometryPass = nullptr;
        const SkinningComputePass* m_SkinningComputePass = nullptr;
        const VisibilityResolvePass* m_ResolvePass = nullptr;

        // ID と深度へ描くレンダーパス（ID は Clear、深度は Load。どちらもグラフの添付の状態で始まり ShaderResource で終わる）
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        // 2 回目の render pass（1 回目の描画の後に続けて開く。ID・深度とも Load で、ShaderResource の状態から始まる）
        RHI::RenderPassPtr m_SecondRenderPass;
        RHI::FramebufferPtr m_SecondFramebuffer;
        RHI::ITexture* m_FramebufferId = nullptr;
        RHI::ITexture* m_FramebufferDepth = nullptr;
        FrameWork m_Work;

        RHI::ShaderPtr m_MegaVertexShader;
        RHI::ShaderPtr m_MeshVertexShader;
        RHI::ShaderPtr m_SkinnedVertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::ShaderPtr m_RecordsShader;
        RHI::ShaderPtr m_RecordArgsShader;
        RHI::PipelinePtr m_MegaPipeline;
        RHI::PipelinePtr m_MeshPipeline;
        RHI::PipelinePtr m_SkinnedPipeline;
        RHI::PipelinePtr m_RecordsPipeline;
        RHI::PipelinePtr m_RecordArgsPipeline;
        // 64bit のバッファ（深度 + ID）の ID・深度への合流（対応する装置だけ初期化する）
        VisibilityMerge m_Merge;
        // 画面上で小さいクラスタを、計算シェーダーで 64bit のバッファへ描く（合流と同じ条件で初期化する）
        VisibilitySwRaster m_SwRaster;
        bool m_bSwRasterEnabled = false;
        // ワイヤーフレーム（DebugViewMode::Wireframe）の線の描き方。3 種とも揃ったときだけ使う（development ビルドだけ作る）
        RHI::PipelinePtr m_MegaWireframePipeline;
        RHI::PipelinePtr m_MeshWireframePipeline;
        RHI::PipelinePtr m_SkinnedWireframePipeline;

        // 資源は Execute の回数ではなくフレームの枠で決める（同じフレームに何回 Execute されても提出前の資源を上書きしない）
        FrameUseRing<FrameSlot> m_FrameSlots;
        // 手続き・スキニングの塊の作業配列（Collect*Chunks の間だけ使い、容量を毎フレーム使い回す）
        Container::VariableArray<MeshIndexChunk> m_ProceduralChunkScratch;
        Container::VariableArray<MeshIndexChunk> m_SkinnedChunkScratch;
        // 材質の表を GPU へ上げる並びと、区間ごとの表の番号の作業配列（Execute の間だけ使い、容量を毎フレーム使い回す）
        Container::VariableArray<VisibilityBuffer::MaterialEntry> m_MaterialEntryScratch;
        Container::VariableArray<uint32_t> m_SectionMaterialScratch;

        RGTextureHandle m_IdHandle;
        RGResourceHandle m_DepthHandle;
        RGResourceHandle m_SkinnedVerticesHandle;

        VisibilityRasterFrameStats m_Stats;
        // 最後にログへ書いた内訳（変わったときだけ書く）
        VisibilityRasterFrameStats m_LoggedStats;
        bool m_bLoggedStats = false;
        RHI::BufferPtr m_LastRecordTable;
        uint64_t m_LastRecordTableBytes = 0;
        RHI::BufferPtr m_LastMaterialTable;
        uint32_t m_LastMaterialTableCount = 0;
        Container::VariableArray<VisibilityBuffer::MaterialEntry> m_LastMaterialEntries;
        RHI::BufferPtr m_LastMegaInstanceBuffer;
        uint64_t m_LastMegaInstanceBytes = 0;
        // そのフレームの材質の表の積み上げ（Execute のたびに空にする）
        VisibilityBuffer::MaterialTable m_MaterialTable;
        // 材質の表の件数を最後にログへ書いた値（変わったときだけ書く）
        uint32_t m_LoggedMaterialUnique = 0;
        uint32_t m_LoggedMaterialLimit = 0;
        bool m_bLoggedMaterials = false;
        // 材質の表が溢れた通知を一度だけ出すための印
        bool m_bLoggedMaterialOverflow = false;
        bool m_bLoggedUnsupported = false;
        // 塊に分けられなかった通知を一度だけ出すための印
        bool m_bLoggedChunkFailure = false;
        // 記録の計算を間接 dispatch できず、直接の dispatch に切り替えた通知を一度だけ出すための印
        bool m_bLoggedRecordsDirectFallback = false;
    };

    /**
     * @brief VisBuffer.Id の検証表示（画素の ID を色にして、最後のシーンの色へ重ねずに書き込む）
     *
     * 記録の番号（描画）ごとに色相を散らし、種類（MegaGeometry・手続き・スキニング）で色相の帯を分ける。
     * 記録の表から種類を引けない画素はマゼンタ、何も描かれていない画素は暗い灰色。
     * ビジビリティバッファの検証用（--visibility-buffer=debug のときだけ追加される）。
     */
    class VisibilityDebugPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        VisibilityDebugPass();
        ~VisibilityDebugPass() override;

        const char* GetName() const override { return "VisibilityDebugPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 記録の表の取り出し元（同じ View の VisibilityRasterPass） */
        void SetRasterPass(const VisibilityRasterPass* pass) { m_RasterPass = pass; }

    private:
        // 1 回の Execute が使う資源（フレームの枠の中で、Execute のたびに次の 1 組を使う）
        struct Use
        {
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParamsUniform;
        };

        const VisibilityRasterPass* m_RasterPass = nullptr;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::RenderPassPtr m_RenderPass;
        RHI::SamplerPtr m_Sampler;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferColor = nullptr;
        RHI::Format m_ColorFormat = RHI::Format::UNKNOWN;
        // 資源は Execute の回数ではなくフレームの枠で決める（同じフレームに何回 Execute されても提出前の資源を上書きしない）
        FrameUseRing<Use> m_Uses;

        RGResourceHandle m_ColorHandle;
        RGTextureHandle m_IdHandle;
    };

} // namespace NorvesLib::Core::Rendering
