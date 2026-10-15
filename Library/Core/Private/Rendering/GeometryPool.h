#pragma once

#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Logging/LogMacros.h"
#include "Rendering/GeometryPoolAllocator.h"
#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/RHITypes.h"
#include "Thread/Mutex.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ジオメトリのプールの塊（バッファ）を作る窓口。デバイスを持たないテストが差し替えられるようにする
     */
    class IGeometryBlockFactory
    {
    public:
        virtual ~IGeometryBlockFactory() = default;

        /** @brief 塊のバッファを作る。作れなければ null */
        virtual RHI::BufferPtr CreateBlock(uint64_t sizeBytes) = 0;
    };

    /**
     * @brief RHI デバイスの DeviceLocal に、頂点・インデックス・storage・間接・転送先・device address の用途を
     *        すべて持つバッファを作る窓口
     */
    class DeviceGeometryBlockFactory final : public IGeometryBlockFactory
    {
    public:
        explicit DeviceGeometryBlockFactory(Container::TSharedPtr<RHI::IDevice> device)
            : m_Device(std::move(device))
        {
        }

        RHI::BufferPtr CreateBlock(uint64_t sizeBytes) override
        {
            if (m_Device == nullptr)
            {
                return nullptr;
            }
            RHI::BufferDesc desc;
            desc.Size = sizeBytes;
            desc.Usage = RHI::ResourceUsage::VertexBuffer | RHI::ResourceUsage::IndexBuffer |
                         RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst |
                         RHI::ResourceUsage::BufferDeviceAddress;
            desc.CPUAccessible = false;
            desc.DebugName = "GeometryPoolBlock";
            // RHI のバッファは確保に失敗すると例外を投げる（メモリ不足など）。プールは失敗を値で返す。
            try
            {
                return m_Device->CreateBuffer(desc);
            }
            catch (const std::exception &)
            {
                return nullptr;
            }
        }

    private:
        Container::TSharedPtr<RHI::IDevice> m_Device;
    };

    /**
     * @brief ジオメトリ（頂点・インデックス・クラスタ）が共有する DeviceLocal の大きなバッファのプール
     *
     * 大きな塊（既定 256 MiB のバッファ）を必要になったときに作り、その中を GeometryPoolAllocator で区画に分けて配る。
     * 配った区画は RegionLease が持ち、手放す（破棄・Reset）と空きへ戻って隣と結合する。GPU が使っているかもしれない
     * 区画は、手放す前に GpuRetireQueue へ渡して、最後に使った提出の serial が完了してから戻す。
     *
     * 確保・返却は内部のミューテックスで守るので、どのスレッドからでも呼べる。状態は共有で持つので、RegionLease が
     * プールより長く生きても安全。ただし塊のバッファは RHI デバイスより先に手放すこと
     * （最後の RegionLease とプールを、デバイスの破棄より前に破棄する）。
     */
    class GeometryPool final
    {
    private:
        struct State
        {
            Container::TSharedPtr<IGeometryBlockFactory> Factory;
            uint64_t BlockBytes = 0;
            // プールが持つバッファの上限（バイト。0 は上限なし）
            uint64_t LimitBytes = 0;
            // VideoMemoryBudgetManager が Geometry の枠に出した目標（観測用。この値でプールの確保は止めない）
            bool bTargetLimited = false;
            uint64_t TargetBytes = 0;
            mutable Thread::Mutex Mutex;
            GeometryPoolAllocator Allocator;
            // Allocator の塊と同じ番号のバッファ
            Container::VariableArray<RHI::BufferPtr> Blocks;
            // 塊ごとの、持ち主が手放して GPU の完了を待っている（まだ空きへ戻っていない）区画の合計（バイト）
            Container::VariableArray<uint64_t> RetiringBytes;
            // 最後に VRAM_LEDGER へ出したときの値（変化したときだけ出し直す）
            bool bLedgerLogged = false;
            uint32_t LoggedBlockCount = 0;
            uint64_t LoggedUsedBytes = 0;
            uint64_t LoggedTargetBytes = 0;
            bool bLoggedTargetLimited = false;

            void Release(const GeometryAllocation &allocation, bool bRetiring)
            {
                Thread::ScopedLock lock(Mutex);
                if (bRetiring && allocation.BlockIndex < RetiringBytes.size())
                {
                    uint64_t &retiring = RetiringBytes[allocation.BlockIndex];
                    retiring = retiring > allocation.SizeBytes ? retiring - allocation.SizeBytes : 0;
                }
                Allocator.Free(allocation);
            }

            void MarkRetiring(const GeometryAllocation &allocation)
            {
                Thread::ScopedLock lock(Mutex);
                if (allocation.BlockIndex < RetiringBytes.size())
                {
                    RetiringBytes[allocation.BlockIndex] += allocation.SizeBytes;
                }
            }
        };

    public:
        static constexpr uint64_t DefaultBlockBytes = 256ull * 1024ull * 1024ull;
        // 区画の既定の整列。storage buffer の範囲のオフセット（minStorageBufferOffsetAlignment）を満たす
        static constexpr uint64_t DefaultAlignmentBytes = 256;

        /**
         * @brief プールから借りた1区画。手放すと空きへ戻る（移動だけできる）
         *
         * 持っている間は塊のバッファも生かす（プールが先に破棄されても区画のバッファは使える）。
         */
        class RegionLease final
        {
        public:
            RegionLease() = default;
            ~RegionLease() { Reset(); }

            RegionLease(const RegionLease &) = delete;
            RegionLease &operator=(const RegionLease &) = delete;

            RegionLease(RegionLease &&other) noexcept
                : m_State(std::move(other.m_State)),
                  m_Buffer(std::move(other.m_Buffer)),
                  m_Allocation(other.m_Allocation),
                  m_bRetiring(other.m_bRetiring)
            {
                other.m_Allocation = GeometryAllocation();
                other.m_bRetiring = false;
            }

            RegionLease &operator=(RegionLease &&other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    m_State = std::move(other.m_State);
                    m_Buffer = std::move(other.m_Buffer);
                    m_Allocation = other.m_Allocation;
                    m_bRetiring = other.m_bRetiring;
                    other.m_Allocation = GeometryAllocation();
                    other.m_bRetiring = false;
                }
                return *this;
            }

            bool IsValid() const { return m_Allocation.IsValid() && m_Buffer != nullptr; }

            /** @brief 区画を持つ塊のバッファ（無効な RegionLease では null） */
            RHI::IBuffer *GetBuffer() const { return m_Buffer.get(); }
            /** @brief 塊のバッファの共有ハンドル（アップロードのリングなど、コピー先として持ち続ける側へ渡す。無効なら null） */
            const RHI::BufferPtr &GetBufferHandle() const { return m_Buffer; }
            uint32_t GetBlockIndex() const { return m_Allocation.BlockIndex; }
            /** @brief 塊のバッファの中での先頭のオフセット */
            uint64_t GetOffsetBytes() const { return m_Allocation.OffsetBytes; }
            /** @brief 区画の大きさ（要求を粒度へ切り上げた値） */
            uint64_t GetSizeBytes() const { return m_Allocation.SizeBytes; }

            /**
             * @brief 持ち主が手放し、GPU の完了を待って空きへ戻る区画として数える（GpuRetireQueue へ渡す直前に呼ぶ）
             *
             * 空きへ戻る（Reset・破棄）まで、塊の返却待ちの量（GeometryPool::GetRetiringBytes）に入る。
             */
            void MarkRetiring()
            {
                if (m_Allocation.IsValid() && m_State != nullptr && !m_bRetiring)
                {
                    m_State->MarkRetiring(m_Allocation);
                    m_bRetiring = true;
                }
            }

            /** @brief 区画を空きへ返す */
            void Reset()
            {
                if (m_Allocation.IsValid() && m_State != nullptr)
                {
                    m_State->Release(m_Allocation, m_bRetiring);
                }
                m_Allocation = GeometryAllocation();
                m_bRetiring = false;
                m_Buffer = nullptr;
                m_State.reset();
            }

        private:
            friend class GeometryPool;

            RegionLease(Container::TSharedPtr<State> state, RHI::BufferPtr buffer, const GeometryAllocation &allocation)
                : m_State(std::move(state)),
                  m_Buffer(std::move(buffer)),
                  m_Allocation(allocation)
            {
            }

            Container::TSharedPtr<State> m_State;
            RHI::BufferPtr m_Buffer;
            GeometryAllocation m_Allocation;
            bool m_bRetiring = false;
        };

        /**
         * @param factory 塊のバッファを作る窓口
         * @param blockBytes 1つの塊の大きさ（バイト。粒度の倍数。0 や倍数でなければ既定の大きさ）
         */
        explicit GeometryPool(Container::TSharedPtr<IGeometryBlockFactory> factory,
                              uint64_t blockBytes = DefaultBlockBytes)
            : m_State(Container::MakeShared<State>())
        {
            m_State->Factory = std::move(factory);
            m_State->BlockBytes = (blockBytes != 0 && blockBytes % GeometryPoolAllocator::GranularityBytes == 0)
                                      ? blockBytes
                                      : DefaultBlockBytes;
        }

        /** @brief RHI デバイスの DeviceLocal に塊を作るプール */
        explicit GeometryPool(Container::TSharedPtr<RHI::IDevice> device, uint64_t blockBytes = DefaultBlockBytes)
            : GeometryPool(Container::TSharedPtr<IGeometryBlockFactory>(
                               Container::MakeShared<DeviceGeometryBlockFactory>(std::move(device))),
                           blockBytes)
        {
        }

        GeometryPool(const GeometryPool &) = delete;
        GeometryPool &operator=(const GeometryPool &) = delete;

        /**
         * @brief 区画を1つ借りる。収まる空きが無ければ塊を1つ増やす
         *
         * 新しい塊の大きさは、既定の塊の大きさか、要求（整列の余白を含む）が収まる大きさの大きいほう。
         * @return 無効な RegionLease は、要求が不正・上限に達した・塊を作れなかったことを表す
         */
        RegionLease Allocate(uint64_t sizeBytes, uint64_t alignmentBytes = DefaultAlignmentBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            State &state = *m_State;

            GeometryAllocation allocation = state.Allocator.Allocate(sizeBytes, alignmentBytes);
            if (!allocation.IsValid() && !GrowLocked(sizeBytes, alignmentBytes))
            {
                return RegionLease();
            }
            if (!allocation.IsValid())
            {
                allocation = state.Allocator.Allocate(sizeBytes, alignmentBytes);
            }
            if (!allocation.IsValid())
            {
                return RegionLease();
            }
            return RegionLease(m_State, state.Blocks[allocation.BlockIndex], allocation);
        }

        /**
         * @brief 指定した塊の中から区画を1つ借りる（塊は増やさない）
         *
         * 同じバッファを引く資源（ページを区画へ分けたメッシュ）が、塊をまたがないようにするのに使う。
         * @return 無効な RegionLease は、その塊に収まる空きが無い・塊が無い・要求が不正であることを表す
         */
        RegionLease AllocateInBlock(uint32_t blockIndex, uint64_t sizeBytes,
                                    uint64_t alignmentBytes = DefaultAlignmentBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            State &state = *m_State;
            if (blockIndex >= state.Blocks.size())
            {
                return RegionLease();
            }
            const GeometryAllocation allocation = state.Allocator.Allocate(sizeBytes, alignmentBytes, blockIndex);
            if (!allocation.IsValid())
            {
                return RegionLease();
            }
            return RegionLease(m_State, state.Blocks[allocation.BlockIndex], allocation);
        }

        /**
         * @brief 塊を1つ増やし、その中から区画を1つ借りる
         *
         * 新しい塊の大きさは、既定の塊の大きさか、要求が収まる大きさの大きいほう。目標（SetBudgetTarget）があるときは、
         * 持っている塊と合わせて目標を超えない大きさへ縮め、それでは要求が収まらなければ増やさない。上限も超えない。
         * ページを持つメッシュが、ページの置き場を残せる塊を選ぶのに使う（既存の塊にページの置き場が残らないとき）。
         * @return 無効な RegionLease は、要求が不正・目標か上限に達した・塊を作れなかったことを表す
         */
        RegionLease AllocateInNewBlock(uint64_t sizeBytes, uint64_t alignmentBytes = DefaultAlignmentBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            State &state = *m_State;
            const uint64_t granularity = GeometryPoolAllocator::GranularityBytes;
            if (sizeBytes == 0 || sizeBytes > ~0ull - (granularity - 1))
            {
                return RegionLease();
            }
            const uint64_t needBytes = (sizeBytes + granularity - 1) / granularity * granularity;
            uint64_t blockBytes = needBytes > state.BlockBytes ? needBytes : state.BlockBytes;
            if (state.bTargetLimited)
            {
                const uint64_t capacityBytes = state.Allocator.GetStats().CapacityBytes;
                const uint64_t roomBytes =
                    state.TargetBytes > capacityBytes ? (state.TargetBytes - capacityBytes) / granularity * granularity : 0;
                blockBytes = blockBytes < roomBytes ? blockBytes : roomBytes;
                if (blockBytes < needBytes)
                {
                    return RegionLease();
                }
            }
            if (!AddBlockLocked(blockBytes))
            {
                return RegionLease();
            }
            const GeometryAllocation allocation =
                state.Allocator.Allocate(sizeBytes, alignmentBytes, static_cast<uint32_t>(state.Blocks.size() - 1));
            if (!allocation.IsValid())
            {
                return RegionLease();
            }
            return RegionLease(m_State, state.Blocks[allocation.BlockIndex], allocation);
        }

        /** @brief 持っている塊の数 */
        uint32_t GetBlockCount() const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            return m_State->Allocator.GetBlockCount();
        }

        /** @brief 塊の大きさ（バイト。範囲外は 0） */
        uint64_t GetBlockSizeBytes(uint32_t blockIndex) const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            return m_State->Allocator.GetBlockSizeBytes(blockIndex);
        }

        /**
         * @brief 塊の中で、持ち主が手放して GPU の完了を待っている区画の合計（バイト。範囲外は 0）
         *
         * この量は、待ち行列が完了を確かめた後で空きへ戻る（それまでは同じ塊の確保に使えない）。
         */
        uint64_t GetRetiringBytes(uint32_t blockIndex) const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            return blockIndex < m_State->RetiringBytes.size() ? m_State->RetiringBytes[blockIndex] : 0;
        }

        /** @brief プールが持てるバッファの上限を決める（バイト。0 は上限なし）。持っている分は減らさない */
        void SetCapacityLimitBytes(uint64_t limitBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            m_State->LimitBytes = limitBytes;
        }

        /** @brief VideoMemoryBudgetManager が Geometry の枠に出した目標を伝える（VRAM_LEDGER に出すだけ。確保は止めない） */
        void SetBudgetTarget(bool bLimited, uint64_t targetBytes)
        {
            Thread::ScopedLock lock(m_State->Mutex);
            m_State->bTargetLimited = bLimited;
            m_State->TargetBytes = bLimited ? targetBytes : 0;
        }

        GeometryPoolStats GetStats() const
        {
            Thread::ScopedLock lock(m_State->Mutex);
            return m_State->Allocator.GetStats();
        }

        /**
         * @brief 塊の数・使用量・目標が前回の出力から変わっていれば、VRAM_LEDGER geometry_pool に出す
         *
         * 確保・返却のたびには出さない（1フレームに多数動くので）。フレーム境界から呼ぶ。
         * 塊をまだ持たず、一度も出していないときは出さない。
         * @return 出したとき true
         */
        bool LogLedgerIfChanged()
        {
            Thread::ScopedLock lock(m_State->Mutex);
            State &state = *m_State;
            const GeometryPoolStats stats = state.Allocator.GetStats();
            if (!state.bLedgerLogged && stats.BlockCount == 0)
            {
                return false;
            }
            if (state.bLedgerLogged && state.LoggedBlockCount == stats.BlockCount &&
                state.LoggedUsedBytes == stats.UsedBytes && state.LoggedTargetBytes == state.TargetBytes &&
                state.bLoggedTargetLimited == state.bTargetLimited)
            {
                return false;
            }
            LogLedgerLocked(stats);
            return true;
        }

    private:
        // 要求が収まる塊を1つ増やす（呼び出し側がミューテックスを持っている）
        bool GrowLocked(uint64_t sizeBytes, uint64_t alignmentBytes)
        {
            State &state = *m_State;
            if (state.Factory == nullptr || sizeBytes == 0)
            {
                return false;
            }
            if (alignmentBytes == 0)
            {
                alignmentBytes = 1;
            }
            if ((alignmentBytes & (alignmentBytes - 1)) != 0)
            {
                return false;
            }

            // 塊の先頭は 0 で、どの整列にも合う。要求が既定の塊より大きいときだけ、その大きさの塊を作る。
            uint64_t blockBytes = state.BlockBytes;
            const uint64_t granularity = GeometryPoolAllocator::GranularityBytes;
            if (sizeBytes > ~0ull - (granularity - 1))
            {
                return false;
            }
            const uint64_t needBytes = (sizeBytes + granularity - 1) / granularity * granularity;
            if (needBytes > blockBytes)
            {
                blockBytes = needBytes;
            }
            return AddBlockLocked(blockBytes);
        }

        // 大きさ blockBytes の塊を1つ作って足す。上限を超えるなら作らない（呼び出し側がミューテックスを持っている）
        bool AddBlockLocked(uint64_t blockBytes)
        {
            State &state = *m_State;
            if (state.Factory == nullptr || blockBytes == 0)
            {
                return false;
            }
            const GeometryPoolStats stats = state.Allocator.GetStats();
            if (state.LimitBytes != 0 &&
                (blockBytes > state.LimitBytes || stats.CapacityBytes > state.LimitBytes - blockBytes))
            {
                return false;
            }

            RHI::BufferPtr buffer = state.Factory->CreateBlock(blockBytes);
            if (buffer == nullptr)
            {
                return false;
            }
            if (state.Allocator.AddBlock(blockBytes) == GeometryAllocation::InvalidBlock)
            {
                return false;
            }
            state.Blocks.push_back(std::move(buffer));
            state.RetiringBytes.push_back(0);

            LogLedgerLocked(state.Allocator.GetStats());
            return true;
        }

        // 使用量を出して、出した値を覚える（呼び出し側がミューテックスを持っている）
        void LogLedgerLocked(const GeometryPoolStats &stats)
        {
            State &state = *m_State;
            constexpr double BytesPerMb = 1024.0 * 1024.0;
            if (state.bTargetLimited)
            {
                LOG_INFO("VRAM_LEDGER geometry_pool blocks=%u capacity_mb=%.1f used_mb=%.1f free_mb=%.1f "
                         "largest_free_mb=%.1f free_ranges=%u allocations=%u target_mb=%.1f",
                         static_cast<unsigned>(stats.BlockCount),
                         static_cast<double>(stats.CapacityBytes) / BytesPerMb,
                         static_cast<double>(stats.UsedBytes) / BytesPerMb,
                         static_cast<double>(stats.FreeBytes) / BytesPerMb,
                         static_cast<double>(stats.LargestFreeBytes) / BytesPerMb,
                         static_cast<unsigned>(stats.FreeRangeCount),
                         static_cast<unsigned>(stats.AllocationCount),
                         static_cast<double>(state.TargetBytes) / BytesPerMb);
            }
            else
            {
                LOG_INFO("VRAM_LEDGER geometry_pool blocks=%u capacity_mb=%.1f used_mb=%.1f free_mb=%.1f "
                         "largest_free_mb=%.1f free_ranges=%u allocations=%u target_mb=none",
                         static_cast<unsigned>(stats.BlockCount),
                         static_cast<double>(stats.CapacityBytes) / BytesPerMb,
                         static_cast<double>(stats.UsedBytes) / BytesPerMb,
                         static_cast<double>(stats.FreeBytes) / BytesPerMb,
                         static_cast<double>(stats.LargestFreeBytes) / BytesPerMb,
                         static_cast<unsigned>(stats.FreeRangeCount),
                         static_cast<unsigned>(stats.AllocationCount));
            }
            state.LoggedBlockCount = stats.BlockCount;
            state.LoggedUsedBytes = stats.UsedBytes;
            state.LoggedTargetBytes = state.TargetBytes;
            state.bLoggedTargetLimited = state.bTargetLimited;
            state.bLedgerLogged = true;
        }

        Container::TSharedPtr<State> m_State;
    };
}
