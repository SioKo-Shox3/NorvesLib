#pragma once

// ジオメトリのページの表（ページの番号 → プールの区画、または非常駐）の CPU 側の持ち主。
// メッシュごとにページの範囲を割り当て、GPU のカリングが毎フレーム引く表の中身を持つ。
// GPU には Entry の並びをそのまま置く（Common/MegaGeometryCull.glsl の PageEntry と同じ並び）。
// スレッドの保護は持たない。呼び出し側（MegaGeometryResourceStore）が自分のミューテックスで守る。

#include "Container/Containers.h"

#include <algorithm>
#include <cstdint>

namespace NorvesLib::Core::Rendering::MegaGeometry
{
    using namespace NorvesLib::Core::Container;

    /** @brief ページが常駐していないことを表す区画の値。Common/MegaGeometryCull.glsl の PAGE_NON_RESIDENT と同じ */
    constexpr uint32_t PAGE_NON_RESIDENT = 0xFFFFFFFFu;

    class GeometryPageTable
    {
    public:
        /**
         * @brief 表の1行（GPU の PageEntry と同じ並び）
         *
         * Region: ページの中身を置いたプールの区画の番号。PAGE_NON_RESIDENT なら常駐していない。
         *   ページを区画に分けない今のメッシュは、全部を1つの区画に持つので、常駐は 0。
         * RequestStamp: カリングがこのページを要求した最後のフレームの印（GPU が書く。同じフレームの重複を省く）。
         */
        struct Entry
        {
            uint32_t Region = 0;
            uint32_t RequestStamp = 0;
        };
        static_assert(sizeof(Entry) == 8, "Common/MegaGeometryCull.glsl の PageEntry と大きさが一致しません");

        /** @brief 表の範囲の持ち主と、範囲の中のページの番号（要求の読み戻しが、グローバルな位置から引き直す） */
        struct Location
        {
            uint64_t OwnerId = 0;
            uint32_t PageId = 0;
            /**
             * @brief この範囲を割り当てたときの表の版。ある版の表を見て書かれた要求が、この範囲のものかの判定に使う
             *
             * 範囲は解放の後に別のメッシュへ再利用される。要求を書いた版より後に割り当てた範囲は、要求の持ち主ではない。
             */
            uint64_t AllocatedVersion = 0;
        };

        /**
         * @brief ownerId のメッシュに pageCount 個のページの範囲を割り当てる（全ページ常駐の区画 0 で始まる）
         * @return 割り当てられたら true。pageCount が 0 のときは false
         */
        bool Allocate(uint64_t ownerId, uint32_t pageCount, uint32_t &outBase)
        {
            outBase = 0;
            if (pageCount == 0 || static_cast<uint64_t>(m_Entries.size()) + pageCount > MaxEntries)
            {
                return false;
            }

            // 最初に入る空きへ置く。無ければ末尾へ伸ばす
            for (size_t index = 0; index < m_Ranges.size(); ++index)
            {
                Range &range = m_Ranges[index];
                if (range.bUsed || range.Count < pageCount)
                {
                    continue;
                }
                const uint32_t base = range.Base;
                if (range.Count > pageCount)
                {
                    Range rest;
                    rest.Base = base + pageCount;
                    rest.Count = range.Count - pageCount;
                    rest.bUsed = false;
                    m_Ranges.insert(m_Ranges.begin() + static_cast<std::ptrdiff_t>(index) + 1, rest);
                }
                Range &placed = m_Ranges[index];
                placed.Count = pageCount;
                placed.bUsed = true;
                placed.OwnerId = ownerId;
                ResetEntries(base, pageCount);
                outBase = base;
                placed.AllocatedVersion = ++m_Version;
                return true;
            }

            Range placed;
            placed.Base = static_cast<uint32_t>(m_Entries.size());
            placed.Count = pageCount;
            placed.bUsed = true;
            placed.OwnerId = ownerId;
            m_Ranges.push_back(placed);
            m_Entries.resize(m_Entries.size() + pageCount);
            ResetEntries(placed.Base, pageCount);
            outBase = placed.Base;
            m_Ranges.back().AllocatedVersion = ++m_Version;
            return true;
        }

        /** @brief base から始まる範囲を返す（隣の空きとまとめ、末尾の空きは表を縮める）。無ければ false */
        bool Free(uint32_t base)
        {
            const size_t index = FindRangeIndex(base);
            if (index == InvalidIndex || !m_Ranges[index].bUsed || m_Ranges[index].Base != base)
            {
                return false;
            }
            Range &range = m_Ranges[index];
            range.bUsed = false;
            range.OwnerId = 0;
            range.AllocatedVersion = 0;
            for (uint32_t offset = 0; offset < range.Count; ++offset)
            {
                m_Entries[range.Base + offset] = Entry{PAGE_NON_RESIDENT, 0};
            }

            // 次の空きと合わせる
            if (index + 1 < m_Ranges.size() && !m_Ranges[index + 1].bUsed)
            {
                m_Ranges[index].Count += m_Ranges[index + 1].Count;
                m_Ranges.erase(m_Ranges.begin() + static_cast<std::ptrdiff_t>(index) + 1);
            }
            // 前の空きと合わせる
            size_t merged = index;
            if (index > 0 && !m_Ranges[index - 1].bUsed)
            {
                m_Ranges[index - 1].Count += m_Ranges[index].Count;
                m_Ranges.erase(m_Ranges.begin() + static_cast<std::ptrdiff_t>(index));
                merged = index - 1;
            }
            // 末尾の空きは表を縮める
            if (merged + 1 == m_Ranges.size() && !m_Ranges[merged].bUsed)
            {
                m_Entries.resize(m_Ranges[merged].Base);
                m_Ranges.erase(m_Ranges.begin() + static_cast<std::ptrdiff_t>(merged));
            }
            ++m_Version;
            return true;
        }

        /** @brief ページを区画 region に常駐させる（region が PAGE_NON_RESIDENT なら非常駐にする）。範囲外は false */
        bool SetRegion(uint32_t base, uint32_t pageId, uint32_t region)
        {
            Entry *entry = FindEntry(base, pageId);
            if (entry == nullptr)
            {
                return false;
            }
            if (entry->Region != region)
            {
                entry->Region = region;
                ++m_Version;
            }
            return true;
        }

        bool IsResident(uint32_t base, uint32_t pageId) const
        {
            const size_t index = FindRangeIndex(base);
            if (index == InvalidIndex || !m_Ranges[index].bUsed || m_Ranges[index].Base != base ||
                pageId >= m_Ranges[index].Count)
            {
                return false;
            }
            return m_Entries[base + pageId].Region != PAGE_NON_RESIDENT;
        }

        /** @brief 表のグローバルな位置から、持ち主とページの番号を引く。割り当てられていない位置は false */
        bool Resolve(uint32_t globalIndex, Location &outLocation) const
        {
            const size_t index = FindRangeIndex(globalIndex);
            if (index == InvalidIndex || !m_Ranges[index].bUsed)
            {
                return false;
            }
            outLocation.OwnerId = m_Ranges[index].OwnerId;
            outLocation.PageId = globalIndex - m_Ranges[index].Base;
            outLocation.AllocatedVersion = m_Ranges[index].AllocatedVersion;
            return true;
        }

        /** @brief 表の中身が変わるたびに増える番号（GPU の写しを作り直すかの比較に使う） */
        uint64_t GetVersion() const { return m_Version; }

        uint32_t GetSize() const { return static_cast<uint32_t>(m_Entries.size()); }

        const VariableArray<Entry> &GetEntries() const { return m_Entries; }

    private:
        struct Range
        {
            uint32_t Base = 0;
            uint32_t Count = 0;
            bool bUsed = false;
            uint64_t OwnerId = 0;
            uint64_t AllocatedVersion = 0;
        };

        static constexpr size_t InvalidIndex = static_cast<size_t>(-1);
        // 1つの表に置くページの総数の上限（GPU の添字を 32 ビットに収める）
        static constexpr uint64_t MaxEntries = 0x7FFFFFFFull;

        // position を含む範囲の添字（範囲は Base の昇順で隙間なく並ぶ）
        size_t FindRangeIndex(uint32_t position) const
        {
            size_t low = 0;
            size_t high = m_Ranges.size();
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                const Range &range = m_Ranges[mid];
                if (position < range.Base)
                {
                    high = mid;
                }
                else if (position >= range.Base + range.Count)
                {
                    low = mid + 1;
                }
                else
                {
                    return mid;
                }
            }
            return InvalidIndex;
        }

        Entry *FindEntry(uint32_t base, uint32_t pageId)
        {
            const size_t index = FindRangeIndex(base);
            if (index == InvalidIndex || !m_Ranges[index].bUsed || m_Ranges[index].Base != base ||
                pageId >= m_Ranges[index].Count)
            {
                return nullptr;
            }
            return &m_Entries[base + pageId];
        }

        void ResetEntries(uint32_t base, uint32_t count)
        {
            for (uint32_t offset = 0; offset < count; ++offset)
            {
                m_Entries[base + offset] = Entry{0, 0};
            }
        }

        VariableArray<Range> m_Ranges;
        VariableArray<Entry> m_Entries;
        uint64_t m_Version = 0;
    };
} // namespace NorvesLib::Core::Rendering::MegaGeometry
