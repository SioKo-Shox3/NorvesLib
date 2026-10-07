#pragma once

// 太陽の仮想シャドウマップ（VSM）の物理ページへ、影の塊 × ページの単位で深度を描く仕組み。
// 塊（三角形 128 個以下の投影物の単位）を投影する記録を渡すと、展開（計算）が「塊の境界のライト空間の矩形が覆うページのうち、
// 割り当て済みで dirty のもの」ごとのインスタンスを作り、描画（128×128 のビューポート）が各インスタンスを物理ページへ直接書く。
// 16K×16K の仮想の解像度のビューポートへ描いて断片ごとにページの表を引く方式は、大きな投影物（地面・壁）が細かい段で全面を塗るので採らない。
//
// 区間（GPU のタイムスタンプの名前）:
//   VsmExpand : 展開（vsm_expand.comp。1 ワークグループ = 1 塊）。インスタンスと塊ごとの間接描画の引数を作る。
//   VsmDraw   : 描画（vsm_draw.vert・vsm_draw.frag。添付の無い 128×128 のレンダーパス）。塊ごとに 1 回の間接描画。
//
// 描画の規則:
//   - 頂点シェーダーは記録の頂点を（ビジビリティバッファと同じ BDA の読み方で）読み、ワールドへ変換し、ライト空間の位置を
//     そのページの局所の texel 座標の NDC へ写す。ページの外はビューポートの外になり、ラスタライザが捨てる。背面は省かない。
//   - 断片シェーダーは物理ページの texel へ atomicMin(floatBitsToUint(深度 [0,1])) を書く（何も無い texel は 1.0 のビット）。
//   - 物理ページへ書く前に、消去（VirtualShadowMapPages の VsmClear）が dirty のページを 1.0 のビットで埋めていること。
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
     * Bounds はワールドの境界（AABB。w は使わない）で、展開が覆うページを決める。三角形をすべて含むこと。
     */
    struct alignas(16) VsmShadowChunk
    {
        VisibilityBuffer::DrawRecord Record;
        float BoundsMin[4] = {};
        float BoundsMax[4] = {};
        float World[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    };
    static_assert(sizeof(VsmShadowChunk) == 144, "vsm_*.vert/comp の VsmShadowChunk と同じ大きさにすること");
    static_assert(offsetof(VsmShadowChunk, BoundsMin) == 64 && offsetof(VsmShadowChunk, World) == 96,
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
        /** @brief 間接描画の引数の頭（先頭の 4 語。語 0 = インスタンスの確保の位置）と、塊ごとの引数（5 語 = VkDrawIndexedIndirectCommand）の大きさ */
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
        /** @brief 展開の出力: インスタンス（容量はバッファの大きさ ÷ 16 バイト）と、間接描画の引数（塊の容量は (大きさ − 頭) ÷ 20 バイト） */
        RHI::BufferPtr Instances;
        RHI::BufferPtr Draws;
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
         * 展開の前に、塊の記録（ホストが書いたもの）が UnorderedAccess へ遷移済みであること。
         */
        bool Record(RHI::ICommandList* commandList, const VirtualShadowMapRasterDispatch& dispatch);

        /** @brief 直前の Record が記録した間接描画の回数（= 塊の数） */
        uint32_t GetLastDrawCount() const { return m_LastDrawCount; }

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
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_ExpandPipeline;
        RHI::PipelinePtr m_DrawPipeline;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        /** @brief 0, 1, 2, … の連番（頂点の番号 = 塊の中の三角形 × 3 + 角を作るための、描画の中で使う頂点の列） */
        RHI::BufferPtr m_IdentityIndices;
        FrameUseRing<Use> m_Uses;
        uint32_t m_LastDrawCount = 0;
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
