#pragma once

// ビジビリティバッファの材質の解決の前段: 画面の 8x8 のタイルを、そのタイルに出る材質ごとに分ける。
//
// VisBuffer.Id と描画の記録の表（VisibilityBuffer.h）から、タイルごとに材質の番号の集合を求め、
//   - 材質ごとのタイルの一覧（タイルの番号 = tileY * tilesX + tileX を並べたもの）
//   - 材質ごとの間接 dispatch の引数（VkDispatchIndirectCommand と同じ並び: x = その材質のタイルの数、y = z = 1、
//     w = 一覧の中の先頭位置）
// を作る計算シェーダー（material_tile_classify.comp）のパス。空の画素（ID = 0）だけのタイルはどの材質にも入らない。
// 材質の解決はこの引数で材質ごとに 1 回ずつ間接 dispatch し、グループ番号 g のタイルを
// 一覧[引数.w + g] から引く（引数は材質の番号 m の分が 16 バイト × m の位置）。

#include "Container/Containers.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
    class ICommandList;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;
    class VisibilityRasterPass;
    struct ViewRenderContext;

    namespace MaterialTiles
    {
        /** @brief タイルの一辺の画素数（シェーダーのワークグループの大きさと同じ） */
        constexpr uint32_t TILE_SIZE = 8;
        /** @brief 1 つのタイルに出うる材質の数の上限（タイルの画素数） */
        constexpr uint32_t MAX_MATERIALS_PER_TILE = TILE_SIZE * TILE_SIZE;
        /** @brief 1 フレームで分けられる材質の番号の数（番号がこれ以上の材質はどのタイルにも入れない） */
        constexpr uint32_t DEFAULT_MAX_MATERIALS = 1024;
        /** @brief 引数 1 つ（VkDispatchIndirectCommand + 一覧の先頭位置）のバイト数 */
        constexpr uint32_t ARGS_STRIDE_BYTES = 16;
        /** @brief 統計の語の数（シェーダーの stats と同じ並び） */
        constexpr uint32_t STATS_WORD_COUNT = 8;
        constexpr uint32_t STATS_BYTES = STATS_WORD_COUNT * sizeof(uint32_t);

        /** @brief 画面の大きさと上限から決まるバッファの大きさ */
        struct Layout
        {
            uint32_t TilesX = 0;
            uint32_t TilesY = 0;
            uint32_t TileCount = 0;
            uint32_t MaxMaterials = 0;
            /** @brief 一覧に置けるタイルの番号の数（材質をまたいだ合計） */
            uint32_t ListCapacity = 0;

            uint64_t ArgsBytes() const { return static_cast<uint64_t>(MaxMaterials) * ARGS_STRIDE_BYTES; }
            uint64_t ListBytes() const { return static_cast<uint64_t>(ListCapacity) * sizeof(uint32_t); }
            uint64_t CursorsBytes() const { return static_cast<uint64_t>(MaxMaterials) * sizeof(uint32_t); }
            bool IsValid() const { return TileCount != 0 && MaxMaterials != 0 && ListCapacity != 0; }
        };

        /**
         * @brief 画面の大きさから、タイルの数と一覧の大きさを決める
         * @param maxEntriesPerTile 1 タイルが一覧へ入れる材質の数の見込み。MAX_MATERIALS_PER_TILE なら一覧が足りなくなることはない
         */
        inline Layout ComputeLayout(uint32_t width,
                                    uint32_t height,
                                    uint32_t maxMaterials = DEFAULT_MAX_MATERIALS,
                                    uint32_t maxEntriesPerTile = MAX_MATERIALS_PER_TILE)
        {
            Layout layout;
            if (width == 0 || height == 0 || maxMaterials == 0 || maxEntriesPerTile == 0)
            {
                return layout;
            }
            layout.TilesX = (width + TILE_SIZE - 1) / TILE_SIZE;
            layout.TilesY = (height + TILE_SIZE - 1) / TILE_SIZE;
            layout.TileCount = layout.TilesX * layout.TilesY;
            layout.MaxMaterials = maxMaterials;
            const uint32_t perTile = maxEntriesPerTile < MAX_MATERIALS_PER_TILE ? maxEntriesPerTile : MAX_MATERIALS_PER_TILE;
            layout.ListCapacity = layout.TileCount * perTile;
            return layout;
        }

        /** @brief 最後に読み戻せる統計（シェーダーの stats と同じ並び） */
        struct Stats
        {
            /** @brief 一覧に入りきらず落としたタイルの数 */
            uint32_t DroppedTiles = 0;
            /** @brief 材質の番号が上限以上で、どの材質にも入れなかった（タイル, 材質）の数 */
            uint32_t MaterialOutOfRange = 0;
            /** @brief 記録の表から引けなかった画素の数 */
            uint32_t UnresolvedPixels = 0;
            /** @brief 見えた最大の材質の番号 + 1（材質が無ければ 0） */
            uint32_t MaxMaterialPlusOne = 0;
            /** @brief 一覧に書いた（タイル, 材質）の数 */
            uint32_t TotalEntries = 0;
        };
    } // namespace MaterialTiles

    /** @brief 1 回の分類（3 回の dispatch）の入力と出力 */
    struct MaterialTileClassifyDispatch
    {
        /** @brief VisBuffer.Id（R32_UINT。ShaderResource の状態で読む） */
        RHI::TexturePtr IdTexture;
        /** @brief 描画の記録の表（storage buffer）と、そのうち使っているバイト数 */
        RHI::BufferPtr RecordTable;
        uint64_t RecordTableBytes = 0;
        /** @brief 出力。いずれも呼び出し前に UnorderedAccess の状態で、終わっても UnorderedAccess のまま */
        RHI::BufferPtr Args;
        RHI::BufferPtr List;
        RHI::BufferPtr Cursors;
        RHI::BufferPtr Stats;
        /** @brief 画面の大きさ（IdTexture の大きさ以下） */
        uint32_t Width = 0;
        uint32_t Height = 0;
        /** @brief 出力のバッファが収まる大きさ（Args・List・Cursors がこの Layout の大きさ以上でなければならない） */
        MaterialTiles::Layout Layout;
    };

    /**
     * @brief 材質ごとのタイルの分類を計算シェーダーで記録する本体（RenderGraph にも GPU のテストにも使う）
     *
     * 1 回の分類は 3 回の dispatch で、それぞれ別の UBO（段階の値）とディスクリプタセットを使う。
     * 飛行中のフレームの数だけ枠を持ち、BeginFrame で枠を選んで使用済みの位置を戻す。
     */
    class MaterialTileClassify final
    {
    public:
        static constexpr uint32_t FrameSlotCount = 2;
        static constexpr uint32_t DispatchesPerRecord = 3;

        MaterialTileClassify();
        ~MaterialTileClassify();

        MaterialTileClassify(const MaterialTileClassify&) = delete;
        MaterialTileClassify& operator=(const MaterialTileClassify&) = delete;

        /** @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record は何もしない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const { return m_Pipeline != nullptr; }

        /** @brief frameIndex に対応する枠を選び、その使用済みの位置を戻す */
        void BeginFrame(uint64_t frameIndex);

        /**
         * @brief 分類を記録する（引数・統計を 0 にしてから 3 回 dispatch する）
         * @return 記録できたら true。入力・出力が足りない、バッファが Layout に収まらないときは false で何も記録しない
         */
        bool Record(RHI::ICommandList* commandList, const MaterialTileClassifyDispatch& dispatch);

        /**
         * @brief 分類できないときに、引数と統計を 0 にする（dispatch のグループ数が 0 になり、後のパスが安全に読める）
         * @return 記録できたら true
         */
        static bool RecordClear(RHI::ICommandList* commandList, const RHI::BufferPtr& args, const RHI::BufferPtr& stats);

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        struct Slot
        {
            Container::VariableArray<Use> Uses;
            uint32_t Cursor = 0;
        };

        bool AcquireUses(Use* outUses);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_Sampler;
        Slot m_Slots[FrameSlotCount];
        uint32_t m_ActiveSlot = 0;
    };

    /**
     * @brief VisBuffer.Id から材質ごとのタイルの一覧と間接 dispatch の引数を作る RenderGraph のパス
     *
     * VisibilityRasterPass が書いた VisBuffer.Id を読み、4 本のフレームごとのバッファ（引数・一覧・内部のカーソル・統計）を
     * 名前付きの資源（MaterialTileArgs ほか）として宣言して書く。材質の解決は引数と一覧を Read する。
     * 分類できないフレーム（記録の表が無い・パイプラインが無い）は、引数を 0 にして終える。
     *
     * 既定は無効（SetEnabled(false)）。材質の解決が使うときに有効にする。
     */
    class MaterialTileClassifyPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        MaterialTileClassifyPass();
        ~MaterialTileClassifyPass() override;

        const char* GetName() const override { return "MaterialTileClassifyPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 記録の表の取り出し元（同じ View の VisibilityRasterPass） */
        void SetRasterPass(const VisibilityRasterPass* pass) { m_RasterPass = pass; }

        /** @brief 最後の Declare が決めたバッファの大きさ（宣言しなかったフレームは無効） */
        const MaterialTiles::Layout& GetLayout() const { return m_Layout; }
        /** @brief 最後の Execute が分類を記録したか（false なら引数を 0 にしただけ、または何もしていない） */
        bool WasClassified() const { return m_bClassified; }

        RGResourceHandle GetArgsHandle() const { return m_ArgsHandle.ToResourceHandle(); }
        RGResourceHandle GetListHandle() const { return m_ListHandle.ToResourceHandle(); }
        RGResourceHandle GetStatsHandle() const { return m_StatsHandle.ToResourceHandle(); }

    private:
        const VisibilityRasterPass* m_RasterPass = nullptr;
        MaterialTileClassify m_Classify;
        MaterialTiles::Layout m_Layout;
        RGTextureHandle m_IdHandle;
        RGBufferHandle m_ArgsHandle;
        RGBufferHandle m_ListHandle;
        RGBufferHandle m_CursorsHandle;
        RGBufferHandle m_StatsHandle;
        bool m_bClassified = false;
        uint64_t m_FrameCounter = 0;
    };

} // namespace NorvesLib::Core::Rendering
