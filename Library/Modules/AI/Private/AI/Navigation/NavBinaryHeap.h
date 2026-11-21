#pragma once

#include "AI/Navigation/NavTypes.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Modules::AI
{
    /// 開いた節点を (f, h, セルの鍵) の昇順で取り出す二分ヒープ。
    class NavBinaryHeap
    {
    public:
        static constexpr size_t NoPosition = SIZE_MAX;

        struct Entry
        {
            NavCellCoord Cell;
            uint64_t F;
            uint64_t H;
            size_t* Position; // 探索中に移動しないページ内の添字の書き戻し先。
        };

        void Push(Entry entry)
        {
            m_Entries.push_back(entry);
            SiftUp(m_Entries.size() - 1, entry);
        }

        void DecreaseKey(size_t position, uint64_t newF)
        {
            Entry entry = m_Entries[position];
            entry.F = newF;
            SiftUp(position, entry);
        }

        Entry Pop()
        {
            const Entry first = m_Entries.front();
            const Entry last = m_Entries.back();
            m_Entries.pop_back();
            *first.Position = NoPosition;
            if (!m_Entries.empty())
            {
                size_t position = 0;
                while (position < m_Entries.size() / 2)
                {
                    size_t child = 2 * position + 1;
                    if (child + 1 < m_Entries.size() && Less(m_Entries[child + 1], m_Entries[child]))
                    {
                        ++child;
                    }
                    if (!Less(m_Entries[child], last))
                    {
                        break;
                    }
                    Place(position, m_Entries[child]);
                    position = child;
                }
                Place(position, last);
            }
            return first;
        }

        bool IsEmpty() const
        {
            return m_Entries.empty();
        }

        /// 次の探索では訪問印が変わるため、旧探索のページを書き換える必要はない。
        void Clear()
        {
            m_Entries.clear();
        }

    private:
        static bool Less(const Entry& left, const Entry& right)
        {
            if (left.F != right.F)
            {
                return left.F < right.F;
            }
            if (left.H != right.H)
            {
                return left.H < right.H;
            }
            return left.Cell.ToKey() < right.Cell.ToKey();
        }

        void Place(size_t position, const Entry& entry)
        {
            m_Entries[position] = entry;
            *entry.Position = position;
        }

        void SiftUp(size_t position, const Entry& entry)
        {
            while (position != 0)
            {
                const size_t parent = (position - 1) / 2;
                if (!Less(entry, m_Entries[parent]))
                {
                    break;
                }
                Place(position, m_Entries[parent]);
                position = parent;
            }
            Place(position, entry);
        }

        Core::Container::VariableArray<Entry> m_Entries;
    };
} // namespace NorvesLib::Modules::AI
