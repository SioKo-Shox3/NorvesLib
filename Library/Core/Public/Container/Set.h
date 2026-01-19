#pragma once

#include <set>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用する集合の実装
     * std::setのラッパーとして機能します
     * 
     * @tparam Key キーの型
     * @tparam Compare 比較関数オブジェクトの型
     * @tparam Alloc アロケータの型
     */
    template<
        typename Key, 
        typename Compare = std::less<Key>,
        typename Alloc = Allocator<Key>
    >
    class Set : public std::set<Key, Compare, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::set<Key, Compare, Alloc>::set;

        // 標準のsetからの変換コンストラクタ
        explicit Set(const std::set<Key, Compare>& other) 
            : std::set<Key, Compare, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のsetからの変換ムーブコンストラクタ
        explicit Set(std::set<Key, Compare>&& other) 
            : std::set<Key, Compare, Alloc>(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // std::setへの変換演算子
        operator std::set<Key, Compare>() const
        {
            return std::set<Key, Compare>(this->begin(), this->end());
        }
    };
}
