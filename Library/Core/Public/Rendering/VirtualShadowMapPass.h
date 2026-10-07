#pragma once

// 太陽の仮想シャドウマップ（VSM。--shadow-method=vsm）の資源を持つパス。
// 深度が確定した後・照明の前に置き、物理ページのプールとページの表などを作って名前で公開し、毎フレーム
// 深度から要るページに印を付け・物理ページを割り当て・消去する（VirtualShadowMapPages）。ページは次のフレームへ持ち越し、
// 動かない物のページは描き直さない（動いた投影物の範囲・太陽の向きの変化・深度の原点の移動だけを無効にする）。
// その後、影を落とす手続きメッシュとスキニングの投影物を塊（128 三角形以下）の記録にして展開・描画し（VirtualShadowMapRaster）、
// 物理ページへ深度を描く。集め方は CSM と同じ（VirtualShadowMapCasters.h）。
// 照明（lighting.frag）は、公開したページの表・物理ページのプールを読んで太陽の影を引く（Common/VirtualShadowMap.glsl。VirtualShadowMapSample.h が
// 読むパラメータを作る）。半透明（forward_transparent.frag）とボリューム（Volumetrics）は CSM のまま。
// MegaGeometry の投影物（bCastShadow のインスタンス）は、展開の前に VirtualShadowMapMegaCull が段ごとにカリングして
// （インスタンス、段、クラスタ）の一覧を作る（主の経路の MegaGeometryPass の入力を読み取りだけで使い、主の経路のバッファには書かない）。
//
// 物理ページのプールは storage buffer（画像ではない）。1 ページ = 128×128 の uint32 = 64 KiB で、ページ順に詰め、ページの中は行順。
// 値は光源の深度 [0,1]（0 が光源に近い）の float のビット（floatBitsToUint）で、何も無い texel は 1.0 のビット。
// 画像でなく buffer にするのは、R32_UINT の画像のアトミックの実績がこのエンジンに無く、buffer の 32bit の atomicMin は追加の機能なしで使えるため。
//
// 作れない装置（断片シェーダーの storage の書き込み・アトミックが無い、バッファのアドレスが無い、プールが MIN_POOL_PAGES 未満しか取れない）では
// 資源を作らず、VSM_FALLBACK reason=<fragment_atomics|bda|pool_size|pipeline> を 1 回出して CSM のまま描く
// （pipeline は印付け・割り当て・消去の計算シェーダーのパイプラインを作れなかったとき）。

#include "Container/PointerTypes.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/FrameUseRing.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/ICommandList.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class GpuResources;
    class MegaGeometryPass;
    class SkinningComputePass;
    class VirtualShadowMapMegaCull;
    class VirtualShadowMapMegaCullStatsReporter;
    class VirtualShadowMapPages;
    class VirtualShadowMapRaster;
    class VirtualShadowMapRasterStatsReporter;
    struct ViewRenderContext;
    /** @brief 投影物の塊の記録を作る作業の状態（VirtualShadowMapPass.cpp が定義する） */
    struct VirtualShadowMapCasterState;

    namespace VirtualShadowMap
    {
        /** @brief 1 ページの一辺（texel） */
        constexpr uint32_t PAGE_RESOLUTION = 128;
        /** @brief 1 ページの語（uint32）の数と、バイト数（64 KiB） */
        constexpr uint32_t PAGE_WORDS = PAGE_RESOLUTION * PAGE_RESOLUTION;
        constexpr uint64_t PAGE_BYTES = static_cast<uint64_t>(PAGE_WORDS) * sizeof(uint32_t);
        /** @brief クリップマップの 1 段のページの数（一辺）。ページの表の一辺 */
        constexpr uint32_t TABLE_DIMENSION = 128;
        constexpr uint32_t TABLE_ENTRIES_PER_LEVEL = TABLE_DIMENSION * TABLE_DIMENSION;
        /** @brief 段の数（クリップマップの既定と同じ） */
        constexpr uint32_t LEVEL_COUNT = VirtualShadowMapClipmapSettings{}.LevelCount;
        static_assert(LEVEL_COUNT >= 1 && LEVEL_COUNT <= VirtualShadowMapMaxLevels, "段の数はクリップマップの上限に収まること");

        /**
         * @brief 既定のプールのページの数（5120 ページ = 320 MiB）。--vsm-pool-pages=<n> で替える
         *
         * 低い太陽の角度（影が長く伸びる視点）の要求は 4721〜4728 ページになり、4096 では 625 ページ以上が溢れる。
         * 5120・6144・8192 の測定は要求 4717〜4719 で溢れ 0・フレームの GPU 時間は変わらないので、溢れない最小の 5120 にする。
         */
        constexpr uint32_t DEFAULT_POOL_PAGES = 5120;
        /** @brief これ未満のページしか取れない装置では VSM を作らない（pool_size） */
        constexpr uint32_t MIN_POOL_PAGES = 512;
        /** @brief ページの表の物理ページの番号の欄の幅（ビット）と、その最大のページ数 */
        constexpr uint32_t PAGE_INDEX_BITS = 20;
        constexpr uint32_t PAGE_INDEX_MASK = (1u << PAGE_INDEX_BITS) - 1u;
        constexpr uint32_t MAX_POOL_PAGES = PAGE_INDEX_MASK;
        /** @brief maxStorageBufferRange が不明（0）のときに使う、Vulkan が保証する最小値（2^27） */
        constexpr uint64_t GUARANTEED_MAX_STORAGE_BUFFER_RANGE = 1ull << 27;

        /** @brief ページの表の 1 要素（uint32）の印。下位 PAGE_INDEX_BITS ビットが物理ページの番号 */
        constexpr uint32_t PAGE_ENTRY_ALLOCATED = 1u << 31;
        constexpr uint32_t PAGE_ENTRY_DIRTY = 1u << 30;
        /**
         * @brief 展開が容量の溢れで描けなかった塊の範囲の、dirty のページに付ける印（次フレームの引き継ぎが dirty を付け直して印を外す）
         *
         * 描けなかった塊のぶん、そのページの影が欠けたままキャッシュに残るのを防ぐ。物理ページの番号の欄（下位 20 ビット）とは重ならない。
         */
        constexpr uint32_t PAGE_ENTRY_RETRY = 1u << 29;

        /** @brief 何も無い texel の深度（1.0）の float のビット */
        constexpr uint32_t EMPTY_DEPTH_BITS = 0x3F800000u;

        /**
         * @brief 統計の語（uint32）の並び: 要求・割り当て・溢れ・描いたページの数・要求のあった段のビットの集合・
         *        展開が描く塊の数・展開が書いたインスタンスの数・展開の容量を超えて書かなかったインスタンスの数・
         *        MegaGeometry の投影物のカリングの（インスタンス、段）の数・書いたクラスタの数・容量を超えて書かなかったクラスタの数
         */
        constexpr uint32_t STATS_WORD_COUNT = 53;
        constexpr uint64_t STATS_BYTES = static_cast<uint64_t>(STATS_WORD_COUNT) * sizeof(uint32_t);
        enum StatWord : uint32_t
        {
            StatRequested = 0,
            StatAllocated = 1,
            StatOverflow = 2,
            StatDrawn = 3,
            StatLevelsUsed = 4,
            StatRasterChunks = 5,
            StatRasterInstances = 6,
            StatRasterOverflow = 7,
            StatMegaInstances = 8,
            StatMegaClusters = 9,
            StatMegaOverflow = 10,
            // キャッシュ（要求があり dirty でない = 描かずに持ち越したページ・描いたページ・無効にしたページ・空きへ戻したページの数）
            StatCached = 11,
            StatRendered = 12,
            StatInvalidated = 13,
            StatReleased = 14,
            // 割り当ての作業の語（vsm_allocate.comp が毎フレーム 0 から使う）: 新しく要るページの数・使用中のページの数・
            // 古い順に戻す段階としきい値の年齢・その段階で戻す数の枠と戻した数・割り当ての位置、続けて年齢ごとの数（STATS_AGE_BINS 個）
            StatScratchNeed = 15,
            StatScratchUsed = 16,
            StatScratchEvictAge = 17,
            StatScratchEvictQuota = 18,
            StatScratchEvictTaken = 19,
            StatScratchAllocCursor = 20,
            StatScratchAgeHistogram = 21,
        };
        /** @brief 要求されなかったフレーム数ごとの数の語の数（年齢 0〜31） */
        constexpr uint32_t STATS_AGE_BINS = 32;
        static_assert(STATS_WORD_COUNT == StatScratchAgeHistogram + STATS_AGE_BINS, "統計の語の数が並びと合っていること");
        /** @brief 要求されなくなったページを持ち越すフレーム数（これを超えて要求が無ければ空きへ戻す） */
        constexpr uint32_t CACHE_CARRY_FRAMES = 30;
        /** @brief 1 フレームに渡せる無効化の矩形の数（超えたら全ページを無効にする） */
        constexpr uint32_t MAX_INVALIDATION_RECTS = 256;

        /** @brief 統計のバッファの用途。計算で書き、読み戻しのコピーの元になる（TransferSrc が無いとコピーが検証に違反する） */
        inline RHI::ResourceUsage StatsBufferUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::TransferSrc;
        }
        /** @brief 統計の読み戻し先（host-visible）の用途 */
        inline RHI::ResourceUsage StatsReadbackUsage()
        {
            return RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst;
        }

        /**
         * @brief 統計のバッファを読み戻し先へ写すコマンドを記録する（計算の書き込みの後。統計は UnorderedAccess へ戻して終わる）
         *
         * 読み戻し先は HostRead の状態で受け取り、HostRead へ戻す。読むのは、このコマンドを含む提出が完了してから。
         */
        inline void RecordStatsReadback(RHI::ICommandList& commandList, const RHI::BufferPtr& stats, const RHI::BufferPtr& readback)
        {
            commandList.BufferBarrier(stats, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopySource);
            commandList.BufferBarrier(readback, RHI::ResourceState::HostRead, RHI::ResourceState::CopyDest);
            commandList.CopyBuffer(stats, readback, STATS_BYTES);
            commandList.BufferBarrier(readback, RHI::ResourceState::CopyDest, RHI::ResourceState::HostRead);
            commandList.BufferBarrier(stats, RHI::ResourceState::CopySource, RHI::ResourceState::UnorderedAccess);
        }

        /** @brief 今フレームの要求のビット列（段 × 128 × 128 ビット）の語（uint32）の数 */
        constexpr uint32_t REQUEST_WORDS = LEVEL_COUNT * TABLE_ENTRIES_PER_LEVEL / 32u;

        /** @brief VSM を作れない理由。名前は VSM_FALLBACK reason= の値 */
        enum class FallbackReason : uint32_t
        {
            None = 0,
            FragmentAtomics,
            BufferDeviceAddress,
            PoolSize,
            Pipeline,
        };

        inline const char* FallbackReasonName(FallbackReason reason)
        {
            switch (reason)
            {
            case FallbackReason::FragmentAtomics:
                return "fragment_atomics";
            case FallbackReason::BufferDeviceAddress:
                return "bda";
            case FallbackReason::PoolSize:
                return "pool_size";
            case FallbackReason::Pipeline:
                return "pipeline";
            default:
                return "none";
            }
        }

        /** @brief 装置の能力と要求から決めたプールの計画 */
        struct PoolPlan
        {
            FallbackReason Reason = FallbackReason::None;
            /** @brief 確保するページの数（Reason が None のときだけ意味がある） */
            uint32_t Pages = 0;
            bool IsSupported() const { return Reason == FallbackReason::None; }
        };

        /** @brief 装置の maxStorageBufferRange に収まるページの数の上限（不明は Vulkan の保証する最小値で見る。表の欄の幅でも締める） */
        inline uint32_t MaxPoolPagesForDevice(const RHI::DeviceCapabilities& capabilities)
        {
            const uint64_t range =
                capabilities.MaxStorageBufferRange != 0 ? capabilities.MaxStorageBufferRange : GUARANTEED_MAX_STORAGE_BUFFER_RANGE;
            const uint64_t pages = range / PAGE_BYTES;
            return pages > MAX_POOL_PAGES ? MAX_POOL_PAGES : static_cast<uint32_t>(pages);
        }

        /**
         * @brief 装置の能力と、要求したページの数（0 は既定）から、プールの計画を決める
         *
         * 断片シェーダーの storage の書き込み・アトミック（bFragmentStoresAndAtomics）とバッファのアドレス（bBufferDeviceAddress）が
         * 無ければ作らない。装置の上限に収まるページの数が MIN_POOL_PAGES 未満でも作らない。
         * 要求は装置の上限へ締める（明示した小さな要求は MIN_POOL_PAGES 未満でもそのまま使う）。
         */
        inline PoolPlan PlanPool(const RHI::DeviceCapabilities& capabilities, uint32_t requestedPages)
        {
            PoolPlan plan;
            if (!capabilities.bFragmentStoresAndAtomics)
            {
                plan.Reason = FallbackReason::FragmentAtomics;
                return plan;
            }
            if (!capabilities.bBufferDeviceAddress)
            {
                plan.Reason = FallbackReason::BufferDeviceAddress;
                return plan;
            }
            const uint32_t devicePages = MaxPoolPagesForDevice(capabilities);
            if (devicePages < MIN_POOL_PAGES)
            {
                plan.Reason = FallbackReason::PoolSize;
                return plan;
            }
            const uint32_t wanted = requestedPages != 0 ? requestedPages : DEFAULT_POOL_PAGES;
            plan.Pages = wanted < devicePages ? wanted : devicePages;
            return plan;
        }

        /** @brief ページの表の大きさ（バイト）: 段 × 128 × 128 の uint32 */
        constexpr uint64_t PageTableBytes()
        {
            return static_cast<uint64_t>(LEVEL_COUNT) * TABLE_ENTRIES_PER_LEVEL * sizeof(uint32_t);
        }

        /** @brief 今フレームの要求のビット列の大きさ（バイト） */
        constexpr uint64_t RequestBitsBytes()
        {
            return static_cast<uint64_t>(REQUEST_WORDS) * sizeof(uint32_t);
        }

        /** @brief 物理ページのプールの大きさ（バイト） */
        constexpr uint64_t PoolBytes(uint32_t pages)
        {
            return static_cast<uint64_t>(pages) * PAGE_BYTES;
        }

        /**
         * @brief 空きページの一覧の大きさ（バイト）
         *
         * 先頭の 1 語が数、続く pages 語が空きページの番号。その後ろに作業の領域が 2 つ続く:
         * 物理ページごとの「使用中」の印（pages 語）と、要求されなかったフレーム数（年齢。pages 語）。
         */
        constexpr uint64_t FreeListBytes(uint32_t pages)
        {
            return (static_cast<uint64_t>(pages) * 3u + 1u) * sizeof(uint32_t);
        }
    } // namespace VirtualShadowMap

    /**
     * @brief 太陽の VSM の資源を作って名前で公開する RenderGraph のパス（深度の確定の後・照明の前）
     *
     * 公開する資源（RenderGraphResourceNames）:
     *   VSM.PhysicalPool（物理ページのプール）・VSM.PageTable（段 × 128 × 128 の uint32）・VSM.RequestBits（今フレームの要求）・
     *   VSM.FreeList（先頭が数、続いて空きページの番号）・VSM.Stats（要求・割り当て・溢れ・描いたページの数・使った段のビット集合）・
     *   VSM.DirtyList（消去するページの一覧。先頭 3 語が間接 dispatch の引数、続く 1 語が数、以降が物理ページの番号）。
     * 読むもの: GBuffer.Depth（印付けの入力）・GBuffer.Normal（深度が確定した後に並ぶための依存。中身は読まない）・
     *   スキニングの変形した頂点（SkinningComputePass が持つとき。投影物の読み取りの依存）。
     * 毎フレームの記録: 要求・統計を 0 にし、深度から印を付け（VsmMark）、ページの表を前フレームから引き継いで（範囲の外へ出たページ・
     * 要求の無いまま持ち越しの上限を超えたページを空きへ戻し、無効にするページに dirty を付け）物理ページを割り当て（VsmAllocate）、
     * dirty のページを 1.0 のビットで埋める（VsmClear）。--vsm-cache=off では毎フレーム表を 0 にして、すべて割り当て直す。続けて、影を落とす投影物の塊の記録を作り（CPU。ホストが書くバッファ）、
     * 段ごとにページへ展開し（VsmExpand）、物理ページへ深度を描く（VsmDraw）。投影物が無い・印付けをしなかったフレームは展開・描画を記録しない。
     * MegaGeometry の投影物のカリングは、印付け・割り当て・消去の後・展開の前に VsmCullMega として記録する（SetMegaGeometryPass の相手が
     * 今フレームに影を落とすインスタンスを持つときだけ）。出力は VsmMega_List（頭 4 語 = 選んだクラスタの数・溢れた数・判定を通った
     * （インスタンス、段）の数・予約、続いて uvec4 = インスタンスの表の番号・段・クラスタの番号・予約）。統計の語 8〜10 を
     * VSM_MEGA_CULL instances=<n> clusters=<n> overflow=<n> として 60 回ごとに出す。
     * 統計は数フレーム遅れで読み戻し、値が変わったとき（または 60 フレームごと）に
     * VSM_PAGES requested=<n> allocated=<n> overflow=<n> levels_used=<mask> を出す。60 フレームごとに
     * VSM_CACHE cached=<n> rendered=<n> invalidated=<n> released=<n>（持ち越したページ・描いたページ・無効にしたページ・空きへ戻したページ）も出す。投影物の集めた内訳は
     * VSM_CASTERS procedural_chunks=<n> skinned_chunks=<n> culled=<n> dropped=<n> skipped=<n> に出す（値が変わったとき・60 回ごと）。
     * プールの確保量は GpuResources::SetShadowMapPoolBytes で予算の計算（VideoMemoryPool::ShadowMap）へ伝える。
     */
    class VirtualShadowMapPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        /** @param requestedPoolPages プールのページの数の要求（0 は既定。--vsm-pool-pages） */
        explicit VirtualShadowMapPass(uint32_t requestedPoolPages = 0);
        ~VirtualShadowMapPass() override;

        const char* GetName() const override { return "VirtualShadowMapPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief スキニングの投影物の取り出し元（同じ View のパス。null ならスキニングの投影物は描かない） */
        void SetSkinningComputePass(const SkinningComputePass* pass) { m_SkinningPass = pass; }
        /** @brief MegaGeometry の投影物の取り出し元（同じ View の主の経路。null なら MegaGeometry の投影物はカリングしない） */
        void SetMegaGeometryPass(const MegaGeometryPass* pass) { m_MegaPass = pass; }
        const MegaGeometryPass* GetMegaGeometryPass() const { return m_MegaPass; }

        /** @brief ページのキャッシュ（動かない物のページを次のフレームへ持ち越す）を使うか。既定は使う（--vsm-cache=off で毎フレームすべて描き直す） */
        void SetCacheEnabled(bool bEnabled) { m_bCacheEnabled = bEnabled; }
        bool IsCacheEnabled() const { return m_bCacheEnabled; }

        /** @brief 資源を作れて、このパスが動くか */
        bool IsActive() const { return m_bActive; }
        /** @brief 作れなかった理由（作れたときは None） */
        VirtualShadowMap::FallbackReason GetFallbackReason() const { return m_FallbackReason; }
        /** @brief 確保したプールのページの数（作れなかったときは 0） */
        uint32_t GetPoolPages() const { return m_PoolPages; }
        /** @brief 要求したプールのページの数（0 は既定） */
        uint32_t GetRequestedPoolPages() const { return m_RequestedPoolPages; }

        const RHI::BufferPtr& GetPool() const { return m_Pool; }
        const RHI::BufferPtr& GetPageTable() const { return m_PageTable; }
        const RHI::BufferPtr& GetRequestBits() const { return m_RequestBits; }
        const RHI::BufferPtr& GetFreeList() const { return m_FreeList; }
        const RHI::BufferPtr& GetStats() const { return m_Stats; }
        const RHI::BufferPtr& GetDirtyList() const { return m_DirtyList; }
        /** @brief MegaGeometry の投影物のカリングの出力の一覧と、dirty のページの階層（作れなかったときは null） */
        const RHI::BufferPtr& GetMegaCullList() const { return m_MegaList; }
        const RHI::BufferPtr& GetMegaDirtyBits() const { return m_MegaDirtyBits; }
        /** @brief MegaGeometry のクラスタの影の塊の記録（カリングの一覧の 1 件ごと。GPU が書く。作れなかったときは null） */
        const RHI::BufferPtr& GetMegaChunks() const { return m_MegaChunks; }
        /** @brief 直前の Execute が MegaGeometry の投影物のカリング（VsmCullMega）を記録したか */
        bool WasMegaCullRecorded() const { return m_bMegaCullRecorded; }
        /** @brief 直前の Execute が MegaGeometry のクラスタの記録を物理ページへ描く間接描画（展開の続きの 1 回）を記録したか */
        bool WasMegaDrawRecorded() const { return m_bMegaDrawRecorded; }
        /** @brief 直前の Execute が印付けを記録したか（深度・有効なクリップマップ・カメラが揃ったとき） */
        bool WasMarked() const { return m_bMarked; }
        /** @brief 直前の Execute が展開・描画を記録したか（印付けを記録し、投影物の塊が 1 つ以上あったとき） */
        bool WasRasterRecorded() const { return m_bRasterRecorded; }
        /** @brief 直前の Execute が展開へ渡した投影物の塊の数（記録しなかったときは 0） */
        uint32_t GetLastCasterChunkCount() const { return m_LastCasterChunkCount; }
        /**
         * @brief 統計の読み戻しの枠の数。飛行中のフレームの数とは別に、書いたフレームの順に使う
         *
         * 読んでよいのは、通し番号の差が StatsReadbackMinFrameDelay 以上で、かつ GPU の完了が確かめられた枠だけ。
         * 枠の数は FrameUseRing の飛行中の上限以上なので、飛行中のフレームが上限まで続いても、次の枠は完了済みになっている。
         */
        static constexpr uint32_t StatsReadbackSlotCount = FrameUseRingMaxInFlightSlots;
        /** @brief 統計を書いたフレームから、読むフレームまでに最低限あける通し番号の差（数フレーム遅れて読む） */
        static constexpr uint64_t StatsReadbackMinFrameDelay = 2;
        /** @brief 統計の読み戻し先（書いた順の枠の番号。観測用。無ければ null） */
        const RHI::BufferPtr& GetStatsReadbackBuffer(uint32_t slotIndex) const { return m_StatsSlots[slotIndex % StatsReadbackSlotCount].Buffer; }

    private:
        static constexpr uint32_t StatsSlotCount = StatsReadbackSlotCount;
        /** @brief 統計の値が変わらなくても VSM_PAGES を出す間隔（読み戻したフレーム数） */
        static constexpr uint32_t StatsLogIntervalFrames = 60;

        struct StatsSlot
        {
            RHI::BufferPtr Buffer;
            const uint32_t* Mapped = nullptr;
            bool bPending = false;
            /** @brief 最後にこの枠へ写したフレームの通し番号（同じフレームの複数の Execute は同じ値） */
            uint64_t FrameSerial = 0;
            /** @brief 写した Execute が MegaGeometry の投影物のカリングを記録したか（語 8〜10 が有効か） */
            bool bMegaCull = false;
        };

        void Fallback(VirtualShadowMap::FallbackReason reason);
        void ReleaseResources();
        /** @brief 影を落とす手続きメッシュとスキニングを、塊の記録に集める（CPU。段の範囲に入らない塊は省く） */
        void CollectCasters(ViewRenderContext& context);
        /** @brief 集めた投影物の動きを前フレームの記録と比べて、無効にするページのライト空間の矩形（なければ全ページ）を決める */
        void PlanInvalidation(ViewRenderContext& context);
        /** @brief 集めた塊を書き、展開 → 描画を記録する。バッファは Common から UnorderedAccess へ進めて、Common へ戻す */
        bool RecordRaster(ViewRenderContext& context, uint64_t frameSerial);
        /** @brief MegaGeometry の投影物のカリングを記録する。バッファは Common から UnorderedAccess へ進めて、Common へ戻す */
        bool RecordMegaCull(ViewRenderContext& context, uint64_t frameSerial);
        /** @brief 投影物の内訳（CasterStats）を、値が変わったとき・60 回ごとに VSM_CASTERS として出す */
        void ReportCasters();
        /** @brief 書き終えた枠の統計を読み、値が変わった・60 フレームたったときに VSM_PAGES を出す */
        void HarvestStats(StatsSlot& slot);
        /** @brief 通し番号の差が StatsReadbackMinFrameDelay 以上で、GPU の完了が確かめられた枠を、書いた順に読む */
        void HarvestReadyStats(uint64_t frameSerial, uint64_t completedFrameSerial);

        uint32_t m_RequestedPoolPages = 0;
        uint32_t m_PoolPages = 0;
        bool m_bCacheEnabled = true;
        RHI::IDevice* m_Device = nullptr;
        GpuResources* m_Gpu = nullptr;
        bool m_bActive = false;
        VirtualShadowMap::FallbackReason m_FallbackReason = VirtualShadowMap::FallbackReason::None;

        RHI::BufferPtr m_Pool;
        RHI::BufferPtr m_PageTable;
        RHI::BufferPtr m_RequestBits;
        RHI::BufferPtr m_FreeList;
        RHI::BufferPtr m_Stats;
        RHI::BufferPtr m_DirtyList;
        /** @brief MegaGeometry の投影物のカリングの出力の一覧（GPU が書く）と、dirty のページの階層。カリングを作れた装置だけが持つ */
        RHI::BufferPtr m_MegaList;
        RHI::BufferPtr m_MegaDirtyBits;
        /** @brief 一覧の 1 件ごとの影の塊の記録（GPU が書き、展開・描画が手続き・スキニングの塊の後ろに続けて読む） */
        RHI::BufferPtr m_MegaChunks;
        Container::TUniquePtr<VirtualShadowMapPages> m_Pages;
        /** @brief 展開の統計（語 5〜7）を VSM_RASTER の行にする（投影物を描くようになるまで 0 のままで、出さない） */
        Container::TUniquePtr<VirtualShadowMapRasterStatsReporter> m_RasterReporter;
        /** @brief 展開・描画（投影物の塊を物理ページへ描く）と、塊の記録・展開の出力のバッファ */
        Container::TUniquePtr<VirtualShadowMapRaster> m_Raster;
        Container::TUniquePtr<VirtualShadowMapCasterState> m_Casters;
        /** @brief MegaGeometry の投影物のカリング（作れなかった装置は null。VSM 全体は CSM へ落とさない）と、その統計の報告 */
        Container::TUniquePtr<VirtualShadowMapMegaCull> m_MegaCull;
        Container::TUniquePtr<VirtualShadowMapMegaCullStatsReporter> m_MegaReporter;
        const SkinningComputePass* m_SkinningPass = nullptr;
        const MegaGeometryPass* m_MegaPass = nullptr;
        StatsSlot m_StatsSlots[StatsSlotCount];
        /** @brief 最後に書いた枠の番号と、そのときのフレームの通し番号（同じフレームの Execute は同じ枠を書き直す） */
        uint32_t m_StatsWriteIndex = StatsSlotCount - 1;
        uint64_t m_StatsWriteSerial = 0;
        bool m_bMarked = false;
        bool m_bRasterRecorded = false;
        bool m_bMegaCullRecorded = false;
        bool m_bMegaDrawRecorded = false;
        uint32_t m_LastCasterChunkCount = 0;
        /** @brief 最後に出した統計（変わったときだけ出す）と、出してからのフレーム数 */
        uint32_t m_LoggedStats[4] = {};
        bool m_bStatsLogged = false;
        uint32_t m_FramesSinceStatsLog = 0;
        /** @brief VSM_CACHE を出してからの読み戻したフレーム数（60 フレームごとに出す） */
        uint32_t m_FramesSinceCacheLog = 0;

        RGResourceHandle m_PoolHandle;
        RGResourceHandle m_PageTableHandle;
        RGResourceHandle m_RequestBitsHandle;
        RGResourceHandle m_FreeListHandle;
        RGResourceHandle m_StatsHandle;
        RGResourceHandle m_DirtyListHandle;
        RGResourceHandle m_SkinnedVerticesHandle;
        RGResourceHandle m_MegaCompleteHandle;
        RGTextureHandle m_DepthHandle;
        bool m_bDeclared = false;
        /** @brief プール・表を初期値で埋めたか（最初の実行で 1 回） */
        bool m_bInitialFilled = false;
    };

} // namespace NorvesLib::Core::Rendering
