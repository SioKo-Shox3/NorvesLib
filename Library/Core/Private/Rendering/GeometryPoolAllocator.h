#pragma once

#include "Container/Containers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief ジオメトリの共有プールの中の1区画（塊の番号・オフセット・大きさ）
     */
    struct GeometryAllocation
    {
        static constexpr uint32_t InvalidBlock = 0xFFFFFFFFu;

        uint32_t BlockIndex = InvalidBlock;
        uint64_t OffsetBytes = 0;
        // 粒度（GeometryPoolAllocator::GranularityBytes）の倍数へ切り上げた大きさ
        uint64_t SizeBytes = 0;

        bool IsValid() const { return BlockIndex != InvalidBlock && SizeBytes != 0; }
    };

    /** @brief プールの使用状況・断片化の統計 */
    struct GeometryPoolStats
    {
        uint32_t BlockCount = 0;
        uint64_t CapacityBytes = 0;
        uint64_t UsedBytes = 0;
        uint64_t FreeBytes = 0;
        // 最も大きい連続した空き（これより大きい区画は確保できない）
        uint64_t LargestFreeBytes = 0;
        uint32_t FreeRangeCount = 0;
        uint32_t AllocationCount = 0;

        /** @brief 空きの断片化の度合い（0 = 空きが1つにまとまっている、1 に近いほど細かく割れている） */
        double GetFragmentation() const
        {
            if (FreeBytes == 0)
            {
                return 0.0;
            }
            return 1.0 - static_cast<double>(LargestFreeBytes) / static_cast<double>(FreeBytes);
        }
    };

    /**
     * @brief 複数の塊（固定長のアドレス空間）の中を区画に分けるサブアロケータ（GPU を持たない計算だけの型）
     *
     * 区画は粒度の倍数へ切り上げ、指定の整列に合わせて置く。空きは最良適合（収まる最小の空き）で選び、
     * 解放した区画は隣の空きと結合する。塊は呼び出し側が AddBlock で足す（実際のバッファの確保は
     * GeometryPool が行う）。スレッドセーフではないので、呼び出し側が排他する。
     */
    class GeometryPoolAllocator final
    {
    public:
        /** @brief 区画の大きさの粒度。小さな端数の空きを作らないために、大きさをこの倍数へ切り上げる */
        static constexpr uint64_t GranularityBytes = 16;

        /** @brief Allocate に渡す、塊を指定しない値 */
        static constexpr uint32_t AnyBlock = GeometryAllocation::InvalidBlock;

        /** @brief 塊を足す（大きさは粒度の倍数へ切り捨てる）。大きさが粒度に満たなければ InvalidBlock */
        uint32_t AddBlock(uint64_t sizeBytes)
        {
            const uint64_t size = sizeBytes / GranularityBytes * GranularityBytes;
            if (size == 0)
            {
                return GeometryAllocation::InvalidBlock;
            }
            Block block;
            block.SizeBytes = size;
            block.Free.push_back(Range{0, size});
            m_Blocks.push_back(std::move(block));
            return static_cast<uint32_t>(m_Blocks.size() - 1);
        }

        /**
         * @brief 区画を確保する
         * @param sizeBytes 大きさ（0 は失敗）
         * @param alignmentBytes 区画の先頭の整列（2 の累乗。0 は 1 と同じ。2 の累乗でなければ失敗）
         * @param onlyBlockIndex この塊の中だけから選ぶ（AnyBlock なら全ての塊から選ぶ。存在しない塊は失敗）
         * @return 収まる空きが無ければ無効な区画（塊を足して呼び直せる）
         */
        GeometryAllocation Allocate(uint64_t sizeBytes, uint64_t alignmentBytes,
                                    uint32_t onlyBlockIndex = GeometryAllocation::InvalidBlock)
        {
            GeometryAllocation result;
            if (onlyBlockIndex != GeometryAllocation::InvalidBlock && onlyBlockIndex >= m_Blocks.size())
            {
                return result;
            }
            if (alignmentBytes == 0)
            {
                alignmentBytes = 1;
            }
            if (sizeBytes == 0 || (alignmentBytes & (alignmentBytes - 1)) != 0 ||
                sizeBytes > ~0ull - (GranularityBytes - 1))
            {
                return result;
            }
            const uint64_t size = AlignUp(sizeBytes, GranularityBytes);

            // 収まる空きのうち最小のもの（同じ大きさなら若い塊・低いオフセット）を選ぶ
            uint32_t bestBlock = GeometryAllocation::InvalidBlock;
            size_t bestRange = 0;
            uint64_t bestRangeSize = ~0ull;
            uint64_t bestStart = 0;
            for (uint32_t blockIndex = 0; blockIndex < m_Blocks.size(); ++blockIndex)
            {
                if (onlyBlockIndex != GeometryAllocation::InvalidBlock && blockIndex != onlyBlockIndex)
                {
                    continue;
                }
                const Block &block = m_Blocks[blockIndex];
                for (size_t rangeIndex = 0; rangeIndex < block.Free.size(); ++rangeIndex)
                {
                    const Range &range = block.Free[rangeIndex];
                    if (range.SizeBytes >= bestRangeSize)
                    {
                        continue;
                    }
                    const uint64_t start = AlignUp(range.OffsetBytes, alignmentBytes);
                    if (start < range.OffsetBytes) // 溢れた
                    {
                        continue;
                    }
                    const uint64_t padding = start - range.OffsetBytes;
                    if (padding > range.SizeBytes || range.SizeBytes - padding < size)
                    {
                        continue;
                    }
                    bestBlock = blockIndex;
                    bestRange = rangeIndex;
                    bestRangeSize = range.SizeBytes;
                    bestStart = start;
                }
            }
            if (bestBlock == GeometryAllocation::InvalidBlock)
            {
                return result;
            }

            Block &block = m_Blocks[bestBlock];
            const Range range = block.Free[bestRange];
            const uint64_t rangeEnd = range.OffsetBytes + range.SizeBytes;
            const uint64_t allocEnd = bestStart + size;

            // 空きを、確保した区画の前（整列の余白）と後ろの残りに分ける
            const bool bHasHead = bestStart > range.OffsetBytes;
            const bool bHasTail = allocEnd < rangeEnd;
            if (bHasHead && bHasTail)
            {
                block.Free[bestRange] = Range{range.OffsetBytes, bestStart - range.OffsetBytes};
                block.Free.insert(block.Free.begin() + static_cast<std::ptrdiff_t>(bestRange) + 1,
                                  Range{allocEnd, rangeEnd - allocEnd});
            }
            else if (bHasHead)
            {
                block.Free[bestRange] = Range{range.OffsetBytes, bestStart - range.OffsetBytes};
            }
            else if (bHasTail)
            {
                block.Free[bestRange] = Range{allocEnd, rangeEnd - allocEnd};
            }
            else
            {
                block.Free.erase(block.Free.begin() + static_cast<std::ptrdiff_t>(bestRange));
            }

            block.Used[bestStart] = size;
            block.UsedBytes += size;
            result.BlockIndex = bestBlock;
            result.OffsetBytes = bestStart;
            result.SizeBytes = size;
            return result;
        }

        /**
         * @brief 区画を空きへ戻し、隣の空きと結合する
         * @return 確保した覚えの無い区画（二重の解放・別の塊の番号・大きさの食い違い）は false で、何も変えない
         */
        bool Free(const GeometryAllocation &allocation)
        {
            if (!allocation.IsValid() || allocation.BlockIndex >= m_Blocks.size())
            {
                return false;
            }
            Block &block = m_Blocks[allocation.BlockIndex];
            const auto used = block.Used.find(allocation.OffsetBytes);
            if (used == block.Used.end() || used->second != allocation.SizeBytes)
            {
                return false;
            }
            block.Used.erase(used);
            block.UsedBytes -= allocation.SizeBytes;

            // 並んだ空きの中の挿入位置（最初に offset が後ろの空き）を二分探索で探す
            size_t low = 0;
            size_t high = block.Free.size();
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                if (block.Free[mid].OffsetBytes < allocation.OffsetBytes)
                {
                    low = mid + 1;
                }
                else
                {
                    high = mid;
                }
            }

            Range merged{allocation.OffsetBytes, allocation.SizeBytes};
            size_t insertAt = low;
            // 後ろの空きと隣り合うなら取り込む
            if (insertAt < block.Free.size() &&
                merged.OffsetBytes + merged.SizeBytes == block.Free[insertAt].OffsetBytes)
            {
                merged.SizeBytes += block.Free[insertAt].SizeBytes;
                block.Free.erase(block.Free.begin() + static_cast<std::ptrdiff_t>(insertAt));
            }
            // 前の空きと隣り合うなら取り込む
            if (insertAt > 0 &&
                block.Free[insertAt - 1].OffsetBytes + block.Free[insertAt - 1].SizeBytes == merged.OffsetBytes)
            {
                merged.OffsetBytes = block.Free[insertAt - 1].OffsetBytes;
                merged.SizeBytes += block.Free[insertAt - 1].SizeBytes;
                --insertAt;
                block.Free.erase(block.Free.begin() + static_cast<std::ptrdiff_t>(insertAt));
            }
            block.Free.insert(block.Free.begin() + static_cast<std::ptrdiff_t>(insertAt), merged);
            return true;
        }

        uint32_t GetBlockCount() const { return static_cast<uint32_t>(m_Blocks.size()); }

        /** @brief 塊の大きさ（範囲外は 0） */
        uint64_t GetBlockSizeBytes(uint32_t blockIndex) const
        {
            return blockIndex < m_Blocks.size() ? m_Blocks[blockIndex].SizeBytes : 0;
        }

        GeometryPoolStats GetStats() const
        {
            GeometryPoolStats stats;
            stats.BlockCount = static_cast<uint32_t>(m_Blocks.size());
            for (const Block &block : m_Blocks)
            {
                stats.CapacityBytes += block.SizeBytes;
                stats.UsedBytes += block.UsedBytes;
                stats.AllocationCount += static_cast<uint32_t>(block.Used.size());
                stats.FreeRangeCount += static_cast<uint32_t>(block.Free.size());
                for (const Range &range : block.Free)
                {
                    if (range.SizeBytes > stats.LargestFreeBytes)
                    {
                        stats.LargestFreeBytes = range.SizeBytes;
                    }
                }
            }
            stats.FreeBytes = stats.CapacityBytes - stats.UsedBytes;
            return stats;
        }

    private:
        struct Range
        {
            uint64_t OffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };

        struct Block
        {
            uint64_t SizeBytes = 0;
            uint64_t UsedBytes = 0;
            // オフセット順の空き（隣り合う空きは結合済み）
            Container::VariableArray<Range> Free;
            // 確保済みの区画（オフセット → 大きさ）
            Container::UnorderedMap<uint64_t, uint64_t> Used;
        };

        static uint64_t AlignUp(uint64_t value, uint64_t alignment)
        {
            const uint64_t mask = alignment - 1;
            // 溢れたときは value より小さい値を返す（呼び出し側が検出する）
            return (value + mask) & ~mask;
        }

        Container::VariableArray<Block> m_Blocks;
    };
}
