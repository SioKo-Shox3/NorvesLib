#pragma once

// ビジビリティバッファの材質の解決の前段: 画面の 8x8 のタイルを、そのタイルに出る材質ごとに分ける。
//
// VisBuffer.Id と描画の記録の表（VisibilityBuffer.h）から、タイルごとに材質の番号の集合を求め、
//   - 材質ごとのタイルの一覧（タイルの番号 = tileY * tilesX + tileX を並べたもの）
//   - 材質ごとの間接 dispatch の引数（先頭の 3 語は VkDispatchIndirectCommand と同じ並び。並びと値は下に書く）
// を作る計算シェーダー（material_tile_classify.comp）のパス。空の画素（ID = 0）だけのタイルはどの材質にも入らない。
//
// 引数は材質の番号 m の分が ARGS_STRIDE_BYTES × m の位置から 8 語（ARG_* の添字）:
//   x = min(タイルの数, MAX_GROUP_COUNT_X)、y = ceil(タイルの数 / MAX_GROUP_COUNT_X)（タイルが 0 個でも 1）、z = 1、
//   一覧の先頭位置、タイルの数、残りは 0。x は Vulkan が保証する maxComputeWorkGroupCount[0] の最小値（65535）以下に抑え、
//   超える分は y に広げる（4K のタイル数 129600 は 1 材質だけでも x に収まらない）。
// 材質の解決はこの引数で材質ごとに 1 回ずつ間接 dispatch し、グループの番号 g = WorkGroupID.y * NumWorkGroups.x +
// WorkGroupID.x を求め、g >= タイルの数のグループは何もせず return する。それ以外は一覧[一覧の先頭位置 + g] のタイルを処理する
// （y > 1 のとき x は x の上限に等しいので、この式は上限の値によらず正しい。上限の定数を式に書かない）。

#include "Container/Containers.h"
#include "Rendering/FrameUseRing.h"
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
        /** @brief 間接 dispatch の 1 次元目のグループ数の上限（Vulkan が保証する maxComputeWorkGroupCount[0] の最小値） */
        constexpr uint32_t MAX_GROUP_COUNT_X = 65535;
        /** @brief 引数 1 つ（VkDispatchIndirectCommand + 一覧の先頭位置 + タイルの数 + 予約 3 語）の語数とバイト数 */
        constexpr uint32_t ARGS_STRIDE_WORDS = 8;
        constexpr uint32_t ARGS_STRIDE_BYTES = ARGS_STRIDE_WORDS * sizeof(uint32_t);
        /** @brief 引数の語の添字（シェーダーの定数と同じ並び） */
        constexpr uint32_t ARG_GROUP_X = 0;
        constexpr uint32_t ARG_GROUP_Y = 1;
        constexpr uint32_t ARG_GROUP_Z = 2;
        constexpr uint32_t ARG_LIST_OFFSET = 3;
        constexpr uint32_t ARG_TILE_COUNT = 4;
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
            /** @brief 引数の x の上限（実機では MAX_GROUP_COUNT_X。小さくして、y へ広げる動きを試せる） */
            uint32_t GroupCountXLimit = MAX_GROUP_COUNT_X;

            uint64_t ArgsBytes() const { return static_cast<uint64_t>(MaxMaterials) * ARGS_STRIDE_BYTES; }
            uint64_t ListBytes() const { return static_cast<uint64_t>(ListCapacity) * sizeof(uint32_t); }
            uint64_t CursorsBytes() const { return static_cast<uint64_t>(MaxMaterials) * sizeof(uint32_t); }
            bool IsValid() const
            {
                return TileCount != 0 && MaxMaterials != 0 && ListCapacity != 0 && GroupCountXLimit != 0 &&
                       GroupCountXLimit <= MAX_GROUP_COUNT_X;
            }
        };

        /**
         * @brief 画面の大きさから、タイルの数と一覧の大きさを決める
         * @param maxEntriesPerTile 1 タイルが一覧へ入れる材質の数の見込み。MAX_MATERIALS_PER_TILE なら一覧が足りなくなることはない
         * @param groupCountXLimit 引数の x の上限。MAX_GROUP_COUNT_X を超える値は MAX_GROUP_COUNT_X に抑える
         */
        inline Layout ComputeLayout(uint32_t width,
                                    uint32_t height,
                                    uint32_t maxMaterials = DEFAULT_MAX_MATERIALS,
                                    uint32_t maxEntriesPerTile = MAX_MATERIALS_PER_TILE,
                                    uint32_t groupCountXLimit = MAX_GROUP_COUNT_X)
        {
            Layout layout;
            if (width == 0 || height == 0 || maxMaterials == 0 || maxEntriesPerTile == 0 || groupCountXLimit == 0)
            {
                return layout;
            }
            layout.GroupCountXLimit = groupCountXLimit < MAX_GROUP_COUNT_X ? groupCountXLimit : MAX_GROUP_COUNT_X;
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
            /** @brief 見えた最大の材質の番号 + 1（材質が無ければ 0）。上限以上の材質も含む（上限を超えた測定値） */
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
        /** @brief 画面の大きさ。画面の外の画素は読まないので IdTexture の大きさ以下でなければならない（超えると記録しない） */
        uint32_t Width = 0;
        uint32_t Height = 0;
        /** @brief 出力のバッファが収まる大きさ（Args・List・Cursors がこの Layout の大きさ以上でなければならない） */
        MaterialTiles::Layout Layout;
    };

    /**
     * @brief 材質ごとのタイルの分類を計算シェーダーで記録する本体（RenderGraph にも GPU のテストにも使う）
     *
     * 1 回の分類は 3 回の dispatch で、それぞれ別の UBO（段階の値）とディスクリプタセットを使う。
     * 資源は FrameUseRing が持ち、BeginFrame で飛行中のフレームの番号の枠を選ぶ。Record のたびに枠の次の 3 組を使い、
     * 足りなければ増やす。使用済みの位置が戻るのはフレームの通し番号が変わったときだけなので、1 フレームに何回
     * Record しても（同じパスが複数のビューポートで何回 Execute されても）、提出前の資源を上書きしない。
     */
    class MaterialTileClassify final
    {
    public:
        static constexpr uint32_t DispatchesPerRecord = 3;

        MaterialTileClassify();
        ~MaterialTileClassify();

        MaterialTileClassify(const MaterialTileClassify&) = delete;
        MaterialTileClassify& operator=(const MaterialTileClassify&) = delete;

        /** @brief シェーダーとパイプラインを作る。失敗したら false（以降の Record は何もしない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const { return m_Pipeline != nullptr; }

        /**
         * @brief 飛行中のフレームの番号の枠を選ぶ。フレームの通し番号が前回と違えば、その枠の使用済みの位置を戻す
         * @param inFlightIndex ViewRenderContext::FrameIndex
         * @param frameSerial ViewRenderContext::ResolveRenderFrameSerial()（同じフレームの間は同じ値）
         */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /** @brief 選んだ枠で、今のフレームにここまで使った資源（dispatch 1 回に 1 組）の数（観測用） */
        uint32_t GetUsedCount() const { return m_Uses.GetUsedCount(); }

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

        bool AcquireUses(Use* outUses);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_Sampler;
        FrameUseRing<Use> m_Uses;
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
        /**
         * @brief 最後の Execute が分類したときの横のタイル数（分類できなかった・まだ Execute していないときは 0）
         *
         * 一覧のタイルの番号（tileY * tilesX + tileX）はこの幅で作られる。一覧を読む側は、自分の画面の幅から求めた横のタイル数と
         * 一致することを確かめてから読む。
         */
        uint32_t GetClassifiedTilesX() const { return m_ClassifiedTilesX; }

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
        uint32_t m_ClassifiedTilesX = 0;
    };

} // namespace NorvesLib::Core::Rendering
