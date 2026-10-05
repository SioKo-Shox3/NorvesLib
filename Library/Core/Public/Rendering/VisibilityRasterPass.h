#pragma once

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/MeshIndexChunks.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityMaterialTable.h"
#include "RHI/IDevice.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class MegaGeometryPass;
    class SkinningComputePass;
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
     * 既定は無効（SceneView::SetupDeferredPipeline の VisibilityBufferMode が Off）。
     */
    class VisibilityRasterPass final : public IViewPass, public IRenderGraphPass
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

        /** @brief 塊の作業配列が持っている容量（毎フレームの確保をしない確認用。最初の Execute の後は増えない） */
        size_t GetChunkScratchCapacity() const { return m_ChunkScratch.capacity(); }

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
            RHI::BufferPtr RecordTable; // 記録の表（host-visible。MegaGeometry の範囲は GPU が、残りはホストが書く）
            uint32_t RecordCapacity = 0; // 要素数
            RHI::ResourceState RecordState = RHI::ResourceState::Common;
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
            RHI::DescriptorSetPtr RecordSet;             // 記録を書く計算
        };

        bool CreateRenderPass();
        bool CreatePipelines(ViewRenderContext& context);
        /** @brief 3 種の線のパイプラインが揃っているか（development ビルド以外では常に false） */
        bool HasWireframePipelines() const;
        bool EnsureFramebuffer(const RHI::TexturePtr& idTexture, const RHI::TexturePtr& depthTexture);
        bool EnsureFrameSlot(FrameSlot& slot, uint32_t recordCapacity, uint32_t sectionCount, uint32_t materialCount);

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

        // ID と深度へ描くレンダーパス（ID は Clear、深度は Load。どちらもグラフの添付の状態で始まり ShaderResource で終わる）
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferId = nullptr;
        RHI::ITexture* m_FramebufferDepth = nullptr;

        RHI::ShaderPtr m_MegaVertexShader;
        RHI::ShaderPtr m_MeshVertexShader;
        RHI::ShaderPtr m_SkinnedVertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::ShaderPtr m_RecordsShader;
        RHI::PipelinePtr m_MegaPipeline;
        RHI::PipelinePtr m_MeshPipeline;
        RHI::PipelinePtr m_SkinnedPipeline;
        RHI::PipelinePtr m_RecordsPipeline;
        // ワイヤーフレーム（DebugViewMode::Wireframe）の線の描き方。3 種とも揃ったときだけ使う（development ビルドだけ作る）
        RHI::PipelinePtr m_MegaWireframePipeline;
        RHI::PipelinePtr m_MeshWireframePipeline;
        RHI::PipelinePtr m_SkinnedWireframePipeline;

        // 資源は Execute の回数ではなくフレームの枠で決める（同じフレームに何回 Execute されても提出前の資源を上書きしない）
        FrameUseRing<FrameSlot> m_FrameSlots;
        // 手続き・スキニングの塊の作業配列（Collect*Chunks の間だけ使い、容量を毎フレーム使い回す）
        Container::VariableArray<MeshIndexChunk> m_ChunkScratch;

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
