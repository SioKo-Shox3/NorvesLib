#pragma once

#include <unordered_map>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用するハッシュマップの実装
     * std::unordered_mapのラッパーとして機能します
     * 
     * @tparam Key キーの型
     * @tparam T 値の型
     * @tparam Hash ハッシュ関数オブジェクトの型
     * @tparam KeyEqual キー比較関数オブジェクトの型
     * @tparam Alloc アロケータの型
     */
    template<
        typename Key, 
        typename T, 
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Alloc = Allocator<std::pair<const Key, T>>
    >
    class UnorderedMap : public std::unordered_map<Key, T, Hash, KeyEqual, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::unordered_map<Key, T, Hash, KeyEqual, Alloc>::unordered_map;

        // 標準のunordered_mapからの変換コンストラクタ
        explicit UnorderedMap(const std::unordered_map<Key, T, Hash, KeyEqual>& other) 
            : std::unordered_map<Key, T, Hash, KeyEqual, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のunordered_mapからの変換ムーブコンストラクタ
        explicit UnorderedMap(std::unordered_map<Key, T, Hash, KeyEqual>&& other) 
            : std::unordered_map<Key, T, Hash, KeyEqual, Alloc>(
                std::make_move_iterator(other.begin()), 
                std::make_move_iterator(other.end())
            )
        {
            other.clear();
        }

        // バケットカウントとアロケータを指定するコンストラクタ
        explicit UnorderedMap(size_t bucketCount, const Alloc& alloc = Alloc())
            : std::unordered_map<Key, T, Hash, KeyEqual, Alloc>(bucketCount, Hash(), KeyEqual(), alloc)
        {
        }

        // std::unordered_mapへの変換演算子
        operator std::unordered_map<Key, T, Hash, KeyEqual>() const
        {
            return std::unordered_map<Key, T, Hash, KeyEqual>(this->begin(), this->end());
        }
    };
}