#pragma once

// 太陽の仮想シャドウマップ（VSM）の物理ページへ、影の塊 × ページの単位で深度を描く仕組み。
// 塊（三角形 128 個以下の投影物の単位）を投影する記録を渡すと、展開（計算）が「塊の境界のライト空間の矩形が覆うページのうち、
// 割り当て済みで dirty のもの」ごとのインスタンスを作り、描画（128×128 のビューポート）が各インスタンスを物理ページへ直接書く。
// 16K×16K の仮想の解像度のビューポートへ描いて断片ごとにページの表を引く方式は、大きな投影物（地面・壁）が細かい段で全面を塗るので採らない。
//
// 区間（GPU のタイムスタンプの名前）:
//   VsmExpand : 展開（vsm_expand.comp。1 ワークグループ = 1 塊）。インスタンスと塊ごとの間接描画の引数を作る。
//     MegaGeometry のクラスタの記録があるときは、先に vsm_expand_args.comp が一覧の件数から間接 dispatch の引数を作り、
//     ホストが書いた塊 + 件数ぶんだけワークグループを出す（容量ぶんは出さない）。
//   VsmDraw   : 描画（vsm_draw.vert・vsm_draw.frag。添付の無い 128×128 のレンダーパス）。塊ごとに 1 回の間接描画。
//
// 描画の規則:
//   - 頂点シェーダーは記録の頂点を（ビジビリティバッファと同じ BDA の読み方で）読み、ワールドへ変換し、ライト空間の位置を
//     そのページの局所の texel 座標の NDC へ写す。ページの外はビューポートの外になり、ラスタライザが捨てる。背面は省かない。
//   - 断片シェーダーは物理ページの texel へ atomicMin(floatBitsToUint(深度 [0,1])) を書く（何も無い texel は 1.0 のビット）。
//   - 物理ページへ書く前に、消去（VirtualShadowMapPages の VsmClear）が dirty のページを 1.0 のビットで埋めていること。
//
// 同じファイルに、MegaGeometry の投影物（bCastShadow のインスタンス）を段ごとにカリングする VirtualShadowMapMegaCull も置く
// （展開の前に、主の経路のインスタンスの表を読み取りだけで使い、（インスタンス、段、クラスタ）の一覧を別のバッファへ作り、
// 続けて一覧の 1 件ごとの影の塊の記録にする。区間 VsmCullMega）。
// MegaGeometry のクラスタの記録は、手続き・スキニングの塊（ホストが書く）の後ろに GPU が書いた記録として並び、同じ展開・描画の
// 1 回の流れで物理ページへ描く（記録の件数は GPU が決めるので、展開は一覧の件数ぶんのワークグループを間接 dispatch で出し、描画は一覧の件数を数として間接描画の数を GPU から読む DrawIndexedIndirectCount 1 回で描く）。
// CSM の MegaGeometry の影のように、インスタンスごとの定数バッファ（DynamicUniformAllocator のスロット）は使わない。
//
// 展開の容量（インスタンスの数）を超える塊は描かずに数える。統計（VSM.Stats）の語 5〜7 に、描く塊の数・書いたインスタンスの数・
// 溢れて書かなかったインスタンスの数が入り、VirtualShadowMapRasterStatsReporter が VSM_RASTER の行にする。
// 装置が間接描画の firstInstance・バッファのアドレスを使えないときは作れない（IsReady が false）。

#include "Rendering/FrameUseRing.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/VisibilityBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;

    /**
     * @brief 影の塊の記録 1 行（storage buffer の 1 要素。GLSL の VsmShadowChunk と同じ 144 バイト）
     *
     * Record はビジビリティバッファの描画の記録（頂点・インデックスの読み方）。Kind は 0 以外・TriangleCount は 1 以上 128 以下にする。
     * World はローカル空間の位置をワールドへ変える 3×4 行列（行ごとに 4 要素。ワールドの x = World[0..3] と (位置, 1) の内積、y = World[4..7]、z = World[8..11]）。
     * ワールド空間の頂点（スキニングの出力など）は単位行列にする。
     * Bounds はワールドの境界（AABB）で、展開が覆うページを決める。三角形をすべて含むこと。
     * LevelMask はこの塊を展開する段の集合（ビット L が段 L）。CPU が境界から決めた段だけを展開が処理し、外の段は見ない。
     * 既定は全段（CPU が絞らない塊は、展開が段ごとに範囲を見て決める）。
     */
    struct alignas(16) VsmShadowChunk
    {
        VisibilityBuffer::DrawRecord Record;
        float BoundsMin[3] = {};
        uint32_t LevelMask = 0xFFFFFFFFu;
        float BoundsMax[3] = {};
        uint32_t Reserved = 0;
        float World[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    };
    static_assert(sizeof(VsmShadowChunk) == 144, "vsm_*.vert/comp の VsmShadowChunk と同じ大きさにすること");
    static_assert(offsetof(VsmShadowChunk, BoundsMin) == 64 && offsetof(VsmShadowChunk, LevelMask) == 76 &&
                      offsetof(VsmShadowChunk, BoundsMax) == 80 && offsetof(VsmShadowChunk, World) == 96,
                  "Common/VirtualShadowMapChunk.glsl の VsmShadowChunk と同じ並びにすること");

    namespace VirtualShadowMap
    {
        /** @brief 塊の記録の大きさ（バイト）。chunks 件ぶん（最低 1 件） */
        constexpr uint64_t RasterChunkBytes(uint32_t chunks)
        {
            return static_cast<uint64_t>(chunks == 0u ? 1u : chunks) * sizeof(VsmShadowChunk);
        }
        /** @brief インスタンス（塊の番号・段と物理ページ・絶対のページの x・y の uvec4）の大きさ（バイト）。instances 件ぶん（最低 1 件） */
        constexpr uint64_t RasterInstanceBytes(uint32_t instances)
        {
            return static_cast<uint64_t>(instances == 0u ? 1u : instances) * 4u * sizeof(uint32_t);
        }
        /**
         * @brief 間接描画の引数の頭（先頭の 4 語。語 0 = インスタンスの確保の位置、語 1〜3 = MegaGeometry のクラスタの記録があるときの
         * 展開の間接 dispatch の引数 VkDispatchIndirectCommand）と、塊ごとの引数（5 語 = VkDrawIndexedIndirectCommand）の大きさ
         */
        constexpr uint32_t RASTER_DRAWS_HEADER_WORDS = 4;
        constexpr uint32_t RASTER_DRAW_COMMAND_WORDS = 5;
        constexpr uint64_t RasterDrawBytes(uint32_t chunks)
        {
            return (static_cast<uint64_t>(RASTER_DRAWS_HEADER_WORDS) +
                    static_cast<uint64_t>(chunks == 0u ? 1u : chunks) * RASTER_DRAW_COMMAND_WORDS) *
                   sizeof(uint32_t);
        }
        /** @brief 塊の記録のバッファの用途（ホストが書く） */
        inline RHI::ResourceUsage RasterChunkUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        }
        /** @brief インスタンスのバッファの用途（展開が書き、頂点シェーダーが読む） */
        inline RHI::ResourceUsage RasterInstanceUsage()
        {
            return RHI::ResourceUsage::StorageBuffer;
        }
        /** @brief 間接描画の引数のバッファの用途（展開が書き、間接描画が読む。頭はコピーで 0 にする） */
        inline RHI::ResourceUsage RasterDrawUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::IndirectBuffer;
        }
        /** @brief MegaGeometry のクラスタの記録のバッファの用途（vsm_mega_chunks.comp が書き、展開・描画が読む） */
        inline RHI::ResourceUsage MegaChunkUsage()
        {
            return RHI::ResourceUsage::StorageBuffer;
        }
        /** @brief 描画のビューポートの一辺（= 1 ページの一辺） */
        constexpr uint32_t RASTER_VIEWPORT = PAGE_RESOLUTION;
    } // namespace VirtualShadowMap

    /**
     * @brief 1 回の記録の入力。バッファはすべて UnorderedAccess の状態で渡し、同じ状態で戻る
     */
    struct VirtualShadowMapRasterDispatch
    {
        /** @brief 今フレームのクリップマップ（無効なら何も記録しない） */
        const VirtualShadowMapClipmap* Clipmap = nullptr;
        /** @brief 物理ページの数（Pool の大きさと合っていること） */
        uint32_t PoolPages = 0;
        RHI::BufferPtr Pool;
        RHI::BufferPtr PageTable;
        /** @brief 統計（語 5〜7 へ書く。呼ぶ前に 0 にしておくこと） */
        RHI::BufferPtr Stats;
        /** @brief 塊の記録（VsmShadowChunk の並び）と数 */
        RHI::BufferPtr Chunks;
        uint32_t ChunkCount = 0;
        /**
         * @brief 展開の出力: インスタンス（容量はバッファの大きさ ÷ 16 バイト）と、間接描画の引数（塊の容量は (大きさ − 頭) ÷ 20 バイト。
         * ChunkCount + MegaCapacity 件ぶん以上）
         */
        RHI::BufferPtr Instances;
        RHI::BufferPtr Draws;
        /**
         * @brief MegaGeometry のクラスタの記録（VirtualShadowMapMegaCull が書いた VsmShadowChunk の並びで、MegaCapacity 件ぶん以上）と、
         * カリングの出力の一覧（語 0 = 選んだクラスタの数。IndirectBuffer の用途を持つこと）。MegaCapacity が 0 ならクラスタの記録は無い
         *
         * 記録の番号は ChunkCount から MegaCapacity 件が並ぶ（一覧の件数で頭打ち）。装置が DrawIndexedIndirectCount を使えない
         * （SupportsMegaCasters が false）とき、MegaCapacity が 0 でないと Record は false を返す。
         */
        RHI::BufferPtr MegaChunks;
        RHI::BufferPtr MegaList;
        uint32_t MegaCapacity = 0;
    };

    /** @brief 展開と描画の 2 つのパイプラインと、128×128 の添付なしのレンダーパスを持つ */
    class VirtualShadowMapRaster
    {
    public:
        VirtualShadowMapRaster();
        ~VirtualShadowMapRaster();

        /** @brief シェーダーを読み、パイプライン・レンダーパス・フレームバッファを作る。失敗したら false（何も持たない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const;

        /** @brief フレームの枠を選ぶ（frameSerial は 0 以外でフレームごとに増える） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief 展開 → 描画を記録する。入力が揃わなければ false を返し、何も記録しない
         *
         * 塊が 0 件でも、引数の頭を 0 にして展開を記録する（描画の呼び出しは無い）。
         * 展開の前に、塊の記録（ホストが書いたもの）と MegaGeometry のクラスタの記録・一覧が UnorderedAccess へ遷移済みであること。
         */
        bool Record(RHI::ICommandList* commandList, const VirtualShadowMapRasterDispatch& dispatch);

        /** @brief 直前の Record が記録した、ホストが書いた塊ごとの間接描画の回数（= その塊の数。MegaGeometry の 1 回は含まない） */
        uint32_t GetLastDrawCount() const { return m_LastDrawCount; }
        /** @brief 直前の Record が MegaGeometry のクラスタの記録を描く間接描画（DrawIndexedIndirectCount）を記録したか */
        bool WasMegaDrawRecorded() const { return m_bLastMegaDraw; }
        /** @brief MegaGeometry のクラスタの記録を描けるか（装置が DrawIndexedIndirectCount を使える） */
        bool SupportsMegaCasters() const { return m_bMegaSupported; }

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr ExpandSet;
            RHI::DescriptorSetPtr DrawSet;
        };

        bool AcquireUse(Use*& outUse);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ExpandShader;
        RHI::ShaderPtr m_ExpandArgsShader;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_ExpandPipeline;
        RHI::PipelinePtr m_ExpandArgsPipeline;
        RHI::PipelinePtr m_DrawPipeline;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        /** @brief 0, 1, 2, … の連番（頂点の番号 = 塊の中の三角形 × 3 + 角を作るための、描画の中で使う頂点の列） */
        RHI::BufferPtr m_IdentityIndices;
        FrameUseRing<Use> m_Uses;
        uint32_t m_LastDrawCount = 0;
        bool m_bLastMegaDraw = false;
        bool m_bMegaSupported = false;
    };

    namespace VirtualShadowMap
    {
        /** @brief MegaGeometry の投影物のカリングの出力の一覧の頭（語）: 選んだクラスタの数・溢れて書かなかった数・判定を通った（インスタンス、段）の数・予約 */
        constexpr uint32_t MEGA_CULL_LIST_HEADER_WORDS = 4;
        /** @brief 出力の一覧の既定の容量（クラスタの数）。1 件は uvec4（インスタンスの表の番号・段・クラスタの番号・予約）= 16 バイト */
        constexpr uint32_t MEGA_CULL_LIST_CAPACITY = 524288;
        constexpr uint64_t MegaCullListBytes(uint32_t capacity)
        {
            return (static_cast<uint64_t>(MEGA_CULL_LIST_HEADER_WORDS) + static_cast<uint64_t>(capacity == 0u ? 1u : capacity) * 4u) *
                   sizeof(uint32_t);
        }
        /** @brief 出力の一覧のバッファの用途（計算が書く。頭はコピーで 0 にする。語 0 の件数を、描画が間接描画の数として読む） */
        inline RHI::ResourceUsage MegaCullListUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::IndirectBuffer;
        }
        /** @brief dirty のページの階層（段ごとのビット列。mip 0 = 128×128 から mip 7 = 1×1）の 1 段あたりの語の数（21845 ビット = 683 語を 16 語へ切り上げ） */
        constexpr uint32_t MEGA_DIRTY_WORDS_PER_LEVEL = 688;
        constexpr uint32_t MEGA_DIRTY_MIP_COUNT = 8;
        constexpr uint64_t MegaDirtyBitsBytes()
        {
            return static_cast<uint64_t>(LEVEL_COUNT) * MEGA_DIRTY_WORDS_PER_LEVEL * sizeof(uint32_t);
        }
        /** @brief dirty の階層のバッファの用途（計算が atomicOr で書く。毎フレーム 0 へコピーで埋める） */
        inline RHI::ResourceUsage MegaDirtyBitsUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        }
    } // namespace VirtualShadowMap

    /**
     * @brief MegaGeometry の投影物のカリングの 1 回の記録の入力
     *
     * 自分のバッファ（PageTable・Stats・DirtyBits・List）は UnorderedAccess の状態で渡し、同じ状態で戻る。
     * 主の経路のバッファ（Instances・ShadowInstances・MegaPageTable）は、ホストが書いたままの host-visible で、読み取りだけに使う
     * （状態の遷移は要らない）。主の経路の間接描画・見えた印・ページの要求・統計は渡さない（書かない）。
     * PageTable へは、出力の一覧の容量を超えて落としたクラスタの範囲の、割り当て済みで dirty のページに再描画の印（PAGE_ENTRY_RETRY）だけを書く
     * （次フレームの引き継ぎが dirty を付け直し、欠けた影を持ち越さない）。
     */
    struct VirtualShadowMapMegaCullDispatch
    {
        /** @brief 今フレームのクリップマップ（無効なら何も記録しない） */
        const VirtualShadowMapClipmap* Clipmap = nullptr;
        /** @brief VSM のページの表（dirty の階層を作る入力）と統計（語 8〜10 へ書く。呼ぶ前に 0 にしておくこと） */
        RHI::BufferPtr PageTable;
        RHI::BufferPtr Stats;
        /** @brief dirty の階層（MegaDirtyBitsBytes 以上）と、出力の一覧（容量は (大きさ − 頭) ÷ 16 バイト） */
        RHI::BufferPtr DirtyBits;
        RHI::BufferPtr List;
        /**
         * @brief 一覧の 1 件ごとの影の塊の記録の出力（RasterChunkBytes(一覧の容量) 以上。MegaChunkUsage）。null なら記録を作らない
         *
         * 一覧の添字と同じ位置へ書く。UnorderedAccess の状態で渡し、同じ状態で戻る。
         */
        RHI::BufferPtr Chunks;
        /** @brief 主の経路のインスタンスの表（GPUMegaInstance[]）・同じ並びの影の表（MegaGeometryShadowInstance[]）・ジオメトリのページの表 */
        RHI::BufferPtr Instances;
        RHI::BufferPtr ShadowInstances;
        RHI::BufferPtr MegaPageTable;
        /** @brief インスタンスの表の要素数と、影の判定の全ワークグループ（64 クラスタ）の数 */
        uint32_t InstanceCount = 0;
        uint32_t TotalGroups = 0;
        /** @brief LOD を選ぶ誤差の許容（texel）。段の texel の一辺 × この値以下の誤差の段まで粗くする */
        float LodThresholdTexels = 1.0f;
    };

    /**
     * @brief MegaGeometry の投影物（bCastShadow のインスタンス）を VSM の段ごとにカリングする
     *
     * 区間（GPU のタイムスタンプの名前）: VsmCullMega（dirty のページの階層の作成と、クラスタの選択の両方）。
     *   1. dirty の階層（vsm_dirty_mips.comp）: ページの表の「割り当て済みで dirty」のページから、段ごとのページの mip（128² → 1）の
     *      ビット列を作る。
     *   2. 選択（vsm_mega_cull.comp。主の経路の Common/MegaGeometryCull.glsl の判定の本体を正射影の LOD で使う）: 1 ワークグループ =
     *      1 つの（インスタンス、段）の 64 クラスタ。インスタンスの境界のライト空間の矩形が、その段の範囲・深度の範囲に入り、
     *      dirty のページを含むものだけを残し、残ったクラスタを LOD の判定（自分の誤差 ÷ texel ≤ 1 かつ親の誤差 ÷ texel > 1）で選ぶ。
     *      結果は（インスタンスの表の番号・段・クラスタの番号）の一覧と数で、自分のバッファに書く。
     *   3. 影の塊の記録（vsm_mega_chunks.comp。dispatch.Chunks があるときだけ）: 一覧の 1 件を、手続き・スキニングと同じ形の
     *      VsmShadowChunk（クラスタの境界球の AABB・インスタンスのワールド行列・頂点とインデックスの読み方・展開する段 = 選んだ段）にする。
     * 装置がバッファのアドレスを使えない・シェーダーやパイプラインを作れないときは作れない（IsReady が false。VSM 全体は CSM へ落とさない）。
     */
    class VirtualShadowMapMegaCull
    {
    public:
        VirtualShadowMapMegaCull();
        ~VirtualShadowMapMegaCull();

        /** @brief シェーダーを読み、パイプラインを作る。失敗したら false（何も持たない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const;

        /** @brief フレームの枠を選ぶ（frameSerial は 0 以外でフレームごとに増える） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief dirty の階層の作成 → クラスタの選択を記録する。入力が揃わなければ false を返し、何も記録しない
         *
         * 階層のバッファと出力の一覧の頭（先頭 4 語）は、ここで 0 にする。
         */
        bool Record(RHI::ICommandList* commandList, const VirtualShadowMapMegaCullDispatch& dispatch);

        /** @brief 直前の Record が選択へ出したワークグループの数（x × y。段の数 z は含まない） */
        uint32_t GetLastGroupCount() const { return m_LastGroupCount; }
        /** @brief 直前の Record が影の塊の記録を作ったか */
        bool WasChunkBuilt() const { return m_bLastChunkBuilt; }

    private:
        struct Use
        {
            RHI::BufferPtr CullUniform;
            RHI::BufferPtr ParamsUniform;
            RHI::DescriptorSetPtr DirtySet;
            RHI::DescriptorSetPtr CullSet;
            RHI::DescriptorSetPtr ChunkSet;
        };

        bool AcquireUse(Use*& outUse);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_DirtyShader;
        RHI::ShaderPtr m_CullShader;
        RHI::ShaderPtr m_ChunkShader;
        RHI::PipelinePtr m_DirtyPipeline;
        RHI::PipelinePtr m_CullPipeline;
        RHI::PipelinePtr m_ChunkPipeline;
        FrameUseRing<Use> m_Uses;
        uint32_t m_LastGroupCount = 0;
        bool m_bLastChunkBuilt = false;
    };

    /**
     * @brief MegaGeometry の投影物のカリングの統計を VSM_MEGA_CULL の行にする（報告を 60 回受けるごとに出す）
     *
     * 報告は、このカリングを記録したフレームの統計だけを受け取る（記録しなかったフレームは報告しない）。最初の報告は必ず出す。
     */
    class VirtualShadowMapMegaCullStatsReporter
    {
    public:
        /** @brief 報告の間隔（回数） */
        static constexpr uint32_t LogIntervalReports = 60;

        /** @brief 報告する。行を出したとき true */
        bool Report(uint32_t instances, uint32_t clusters, uint32_t overflow);

    private:
        bool m_bLogged = false;
        uint32_t m_ReportsSinceLog = 0;
    };

    /**
     * @brief 展開の統計を VSM_RASTER の行にする（値が変わったとき、または 60 回報告するごとに出す）
     *
     * 投影物を描いていない間（値がずっと 0）は出さない。一度でも 0 以外の値が出たら、以後は 0 に戻ったときも出す。
     */
    class VirtualShadowMapRasterStatsReporter
    {
    public:
        /** @brief 報告の間隔（回数）。値が変わらなくてもこの回数ごとに出す */
        static constexpr uint32_t LogIntervalReports = 60;

        /** @brief 報告する。行を出したとき true */
        bool Report(uint32_t chunks, uint32_t instances, uint32_t overflow);

    private:
        uint32_t m_Logged[3] = {};
        bool m_bEverNonZero = false;
        bool m_bLogged = false;
        uint32_t m_ReportsSinceLog = 0;
    };

} // namespace NorvesLib::Core::Rendering
