#pragma once

#include <map>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用する連想配列の実装
     * std::mapのラッパーとして機能します
     * 
     * @tparam Key キーの型
     * @tparam T 値の型
     * @tparam Compare 比較関数オブジェクトの型
     * @tparam Alloc アロケータの型
     */
    template<
        typename Key, 
        typename T, 
        typename Compare = std::less<Key>,
        typename Alloc = Allocator<std::pair<const Key, T>>
    >
    class Map : public std::map<Key, T, Compare, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::map<Key, T, Compare, Alloc>::map;

        // 標準のmapからの変換コンストラクタ
        explicit Map(const std::map<Key, T, Compare>& other) 
            : std::map<Key, T, Compare, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のmapからの変換ムーブコンストラクタ
        explicit Map(std::map<Key, T, Compare>&& other) 
            : std::map<Key, T, Compare, Alloc>(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // std::mapへの変換演算子
        operator std::map<Key, T, Compare>() const
        {
            return std::map<Key, T, Compare>(this->begin(), this->end());
        }
    };
}
