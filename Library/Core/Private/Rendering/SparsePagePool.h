#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "Thread/Mutex.h"

#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief sparse テクスチャへ結ぶ物理メモリのページのプール
     *
     * DeviceLocal の大きな塊（既定 64 MiB）を必要になったときに作り、64 KiB のページに切って配る。
     * 配ったページは PageLease が持ち、手放す（破棄・Reset）とプールへ戻る。GPU が使っているかもしれない
     * ページは、外した直後に GpuRetireQueue へ渡して、最後に使った提出の serial が完了してから戻す。
     *
     * ページの確保・返却は内部のミューテックスで守るので、どのスレッドからでも呼べる。
     * 状態は共有で持つので、PageLease がプールより長く生きても安全。ただし塊の確保した物理メモリは
     * RHI デバイスより先に手放すこと（最後の PageLease とプールを、デバイスの破棄より前に破棄する）。
     */
    class SparsePagePool final
    {
    private:
        struct FreePage
        {
            RHI::SparseMemoryBlockPtr Block;
            uint64_t OffsetBytes = 0;
        };

        struct State
        {
            Container::TSharedPtr<RHI::IDevice> Device;
            uint64_t BlockBytes = 0;
            // プールが持つ物理メモリの上限（バイト。0 は上限なし）
            uint64_t LimitBytes = 0;
            mutable Thread::Mutex Mutex;
            Container::VariableArray<RHI::SparseMemoryBlockPtr> Blocks;
            Container::VariableArray<FreePage> Free;
            uint64_t UsedPages = 0;
            // 最後に VRAM_LEDGER へ出したときの値（変化したときだけ出し直す）
            uint32_t LoggedBlockCount = 0;
            uint64_t LoggedUsedPages = 0;
            bool bLedgerLogged = false;

            void Return(RHI::SparseMemoryBlockPtr block, uint64_t offsetBytes)
            {
                Thread::ScopedLock lock(Mutex);
                FreePage page;
                page.Block = std::move(block);
                page.OffsetBytes = offsetBytes;
                Free.push_back(std::move(page));
                if (UsedPages > 0)
                {
                    --UsedPages;
                }
            }
        };

    public:
        static constexpr uint64_t PageSizeBytes = RHI::SparsePageSizeBytes;
        static constexpr uint64_t DefaultBlockBytes = 64ull * 1024ull * 1024ull;

        /** @brief プールの使用状況（VRAM_LEDGER・観測用） */
        struct Stats
        {
            uint32_t BlockCount = 0;
            uint64_t CapacityBytes = 0;
            uint64_t UsedBytes = 0;
            uint64_t FreeBytes = 0;
        };

        /**
         * @brief プールから借りた 1 ページ。手放すとプールへ戻る（移動だけできる）
         */
        class PageLease final
        {
        public:
            PageLease() = default;
            ~PageLease() { Reset(); }

            PageLease(const PageLease &) = delete;
            PageLease &operator=(const PageLease &) = delete;

            PageLease(PageLease &&other) noexcept
                : m_State(std::move(other.m_State)),
                  m_Block(std::move(other.m_Block)),
                  m_OffsetBytes(other.m_OffsetBytes)
            {
                other.m_OffsetBytes = 0;
            }

            PageLease &operator=(PageLease &&other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    m_State = std::move(other.m_State);
                    m_Block = std::move(other.m_Block);
                    m_OffsetBytes = other.m_OffsetBytes;
                    other.m_OffsetBytes = 0;
                }
                return *this;
            }

            bool IsValid() const { return m_Block != nullptr; }

            /** @brief RHI の結び付けへ渡すページ（無効な PageLease では Block が null） */
            RHI::SparsePageRef GetPage() const
            {
                RHI::SparsePageRef page;
                page.Block = m_Block.get();
                page.OffsetBytes = m_OffsetBytes;
                return page;
            }

            /** @brief ページをプールへ返す */
            void Reset()
            {
                if (m_Block != nullptr && m_State != nullptr)
                {
                    m_State->Return(std::move(m_Block), m_OffsetBytes);
                }
                m_Block = nullptr;
                m_State.reset();
                m_OffsetBytes = 0;
            }

        private:
            friend class SparsePagePool;

            PageLease(Container::TSharedPtr<State> state, RHI::SparseMemoryBlockPtr block, uint64_t offsetBytes)
                : m_State(std::move(state)),
                  m_Block(std::move(block)),
                  m_OffsetBytes(offsetBytes)
            {
            }

            Container::TSharedPtr<State> m_State;
            RHI::SparseMemoryBlockPtr m_Block;
            uint64_t m_OffsetBytes = 0;
        };

        /**
         * @param device 塊を作る RHI デバイス
         * @param blockBytes 1つの塊の大きさ（バイト。64 KiB の倍数。0 や倍数でなければ既定の大きさ）
         */
        explicit SparsePagePool(Container::TSharedPtr<RHI::IDevice> device, uint64_t blockBytes = DefaultBlockBytes)
            : m_State(Container::MakeShared<State>())
        {
            m_State->Device = std::move(device);
            m_State->BlockBytes = (blockBytes != 0 && blockBytes % PageSizeBytes == 0) ? blockBytes : DefaultBlockBytes;
        }

        SparsePagePool(const SparsePagePool &) = delete;
        SparsePagePool &operator=(const SparsePagePool &) = delete;

        /**
         * @brief ページを1つ借りる。空きが無ければ塊を1つ増やす
         * @return 無効な PageLease は、上限に達した・塊を作れなかったことを表す
         */
        PageLease Acquire()
        {
            Thread::ScopedLock lock(m_State->Mutex);
            if (m_State->Free.empty() && !GrowLocked())
            {
                return PageLease();
            }

            FreePage page = std::move(m_State->Free.back());
            m_State->Free.pop_back();
            ++m_State->UsedPages;
            return PageLease(m_State, std::move(page.Block), page.OffsetBytes);
        }

        /** @brief プールが持てる物理メモリの上限を決める（バイト。0 は上限なし）。持っている分は減らさない */
        void SetCapacityLimitBytes(uint64_t limitBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            m_State->LimitBytes = limitBytes;
        }

        Stats GetStats() const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            return GetStatsLocked();
        }

        /**
         * @brief 上限の下で、プールが持てる物理メモリの量（バイト。上限なしは uint64_t の最大値）
         *
         * 塊の単位でしか増えないので、上限が塊の倍数でなければ端数は使えない。持っている分は上限を超えていても減らさない。
         */
        uint64_t GetReachableCapacityBytes() const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            const State &state = *m_State;
            if (state.LimitBytes == 0)
            {
                return ~0ull;
            }
            const uint64_t capacityBytes = static_cast<uint64_t>(state.Blocks.size()) * state.BlockBytes;
            const uint64_t growableBytes = state.LimitBytes / state.BlockBytes * state.BlockBytes;
            return capacityBytes > growableBytes ? capacityBytes : growableBytes;
        }

        /**
         * @brief 塊の数か貸し出し数が前回の出力から変わっていれば、使用量を VRAM_LEDGER に出す
         *
         * 貸し借りのたびには出さない（1フレームに多数動くので）。フレーム境界から呼ぶ。
         * @return 出したとき true
         */
        bool LogLedgerIfChanged()
        {
            Thread::ScopedLock lock(m_State->Mutex);
            State &state = *m_State;
            const uint32_t blockCount = static_cast<uint32_t>(state.Blocks.size());
            if (state.bLedgerLogged && state.LoggedBlockCount == blockCount && state.LoggedUsedPages == state.UsedPages)
            {
                return false;
            }
            LogLedgerLocked();
            return true;
        }

    private:
        Stats GetStatsLocked() const
        {
            Stats stats;
            stats.BlockCount = static_cast<uint32_t>(m_State->Blocks.size());
            stats.CapacityBytes = static_cast<uint64_t>(m_State->Blocks.size()) * m_State->BlockBytes;
            stats.UsedBytes = m_State->UsedPages * PageSizeBytes;
            stats.FreeBytes = static_cast<uint64_t>(m_State->Free.size()) * PageSizeBytes;
            return stats;
        }

        // 塊を1つ増やして、そのページを空きへ積む（呼び出し側がミューテックスを持っている）
        bool GrowLocked()
        {
            State &state = *m_State;
            const uint64_t capacityBytes = static_cast<uint64_t>(state.Blocks.size()) * state.BlockBytes;
            if (state.Device == nullptr ||
                (state.LimitBytes != 0 && capacityBytes + state.BlockBytes > state.LimitBytes))
            {
                return false;
            }

            RHI::SparseMemoryBlockPtr block = state.Device->CreateSparseMemoryBlock(state.BlockBytes, "SparsePagePoolBlock");
            if (block == nullptr)
            {
                return false;
            }

            // 低いオフセットから貸し出す（後ろから取るので、逆順に積む）
            const uint64_t pageCount = state.BlockBytes / PageSizeBytes;
            for (uint64_t page = pageCount; page > 0; --page)
            {
                FreePage entry;
                entry.Block = block;
                entry.OffsetBytes = (page - 1) * PageSizeBytes;
                state.Free.push_back(std::move(entry));
            }
            state.Blocks.push_back(std::move(block));

            LogLedgerLocked();
            return true;
        }

        // 使用量を出して、出した値を覚える（呼び出し側がミューテックスを持っている）
        void LogLedgerLocked()
        {
            State &state = *m_State;
            const Stats stats = GetStatsLocked();
            constexpr double BytesPerMb = 1024.0 * 1024.0;
            LOG_INFO("VRAM_LEDGER sparse_pool blocks=%u capacity_mb=%.1f used_mb=%.1f free_mb=%.1f",
                     static_cast<unsigned>(stats.BlockCount),
                     static_cast<double>(stats.CapacityBytes) / BytesPerMb,
                     static_cast<double>(stats.UsedBytes) / BytesPerMb,
                     static_cast<double>(stats.FreeBytes) / BytesPerMb);
            state.LoggedBlockCount = stats.BlockCount;
            state.LoggedUsedPages = state.UsedPages;
            state.bLedgerLogged = true;
        }

        Container::TSharedPtr<State> m_State;
    };
}
