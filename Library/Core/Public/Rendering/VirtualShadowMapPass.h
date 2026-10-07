#pragma once

// 太陽の仮想シャドウマップ（VSM。--shadow-method=vsm）の資源を持つパス。
// 深度が確定した後・照明の前に置き、物理ページのプールとページの表などを作って名前で公開する（中身はまだ使わない。照明は CSM のまま）。
//
// 物理ページのプールは storage buffer（画像ではない）。1 ページ = 128×128 の uint32 = 64 KiB で、ページ順に詰め、ページの中は行順。
// 値は光源の深度 [0,1]（0 が光源に近い）の float のビット（floatBitsToUint）で、何も無い texel は 1.0 のビット。
// 画像でなく buffer にするのは、R32_UINT の画像のアトミックの実績がこのエンジンに無く、buffer の 32bit の atomicMin は追加の機能なしで使えるため。
//
// 作れない装置（断片シェーダーの storage の書き込み・アトミックが無い、バッファのアドレスが無い、プールが MIN_POOL_PAGES 未満しか取れない）では
// 資源を作らず、VSM_FALLBACK reason=<fragment_atomics|bda|pool_size> を 1 回出して CSM のまま描く。

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class GpuResources;
    struct ViewRenderContext;

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

        /** @brief 既定のプールのページの数（4096 ページ = 256 MiB）。--vsm-pool-pages=<n> で替える */
        constexpr uint32_t DEFAULT_POOL_PAGES = 4096;
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

        /** @brief 何も無い texel の深度（1.0）の float のビット */
        constexpr uint32_t EMPTY_DEPTH_BITS = 0x3F800000u;

        /** @brief 統計の語（uint32）の並び: 要求・割り当て・溢れ・描いたページの数 */
        constexpr uint32_t STATS_WORD_COUNT = 4;
        constexpr uint64_t STATS_BYTES = static_cast<uint64_t>(STATS_WORD_COUNT) * sizeof(uint32_t);
        enum StatWord : uint32_t
        {
            StatRequested = 0,
            StatAllocated = 1,
            StatOverflow = 2,
            StatDrawn = 3,
        };

        /** @brief 今フレームの要求のビット列（段 × 128 × 128 ビット）の語（uint32）の数 */
        constexpr uint32_t REQUEST_WORDS = LEVEL_COUNT * TABLE_ENTRIES_PER_LEVEL / 32u;

        /** @brief VSM を作れない理由。名前は VSM_FALLBACK reason= の値 */
        enum class FallbackReason : uint32_t
        {
            None = 0,
            FragmentAtomics,
            BufferDeviceAddress,
            PoolSize,
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

        /** @brief 空きページの一覧の大きさ（バイト）: 先頭の 1 語が数、続く pages 語が空きページの番号 */
        constexpr uint64_t FreeListBytes(uint32_t pages)
        {
            return (static_cast<uint64_t>(pages) + 1u) * sizeof(uint32_t);
        }
    } // namespace VirtualShadowMap

    /**
     * @brief 太陽の VSM の資源を作って名前で公開する RenderGraph のパス（深度の確定の後・照明の前）
     *
     * 公開する資源（RenderGraphResourceNames）:
     *   VSM.PhysicalPool（物理ページのプール）・VSM.PageTable（段 × 128 × 128 の uint32）・VSM.RequestBits（今フレームの要求）・
     *   VSM.FreeList（先頭が数、続いて空きページの番号）・VSM.Stats（要求・割り当て・溢れ・描いたページの数）。
     * 読むもの: GBuffer.Depth・GBuffer.Normal（深度が確定した後に並ぶための依存。このタスクでは中身を読まない）。
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

    private:
        void Fallback(VirtualShadowMap::FallbackReason reason);
        void ReleaseResources();

        uint32_t m_RequestedPoolPages = 0;
        uint32_t m_PoolPages = 0;
        RHI::IDevice* m_Device = nullptr;
        GpuResources* m_Gpu = nullptr;
        bool m_bActive = false;
        VirtualShadowMap::FallbackReason m_FallbackReason = VirtualShadowMap::FallbackReason::None;

        RHI::BufferPtr m_Pool;
        RHI::BufferPtr m_PageTable;
        RHI::BufferPtr m_RequestBits;
        RHI::BufferPtr m_FreeList;
        RHI::BufferPtr m_Stats;

        RGResourceHandle m_PoolHandle;
        RGResourceHandle m_PageTableHandle;
        RGResourceHandle m_RequestBitsHandle;
        RGResourceHandle m_FreeListHandle;
        RGResourceHandle m_StatsHandle;
        bool m_bDeclared = false;
        /** @brief プール・表を初期値で埋めたか（最初の実行で 1 回） */
        bool m_bInitialFilled = false;
    };

} // namespace NorvesLib::Core::Rendering
