#pragma once

#include <unordered_set>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用するハッシュセットの実装
     * std::unordered_setのラッパーとして機能します
     * 
     * @tparam Key キーの型
     * @tparam Hash ハッシュ関数オブジェクトの型
     * @tparam KeyEqual キー比較関数オブジェクトの型
     * @tparam Alloc アロケータの型
     */
    template<
        typename Key, 
        typename Hash = std::hash<Key>,
        typename KeyEqual = std::equal_to<Key>,
        typename Alloc = Allocator<Key>
    >
    class UnorderedSet : public std::unordered_set<Key, Hash, KeyEqual, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::unordered_set<Key, Hash, KeyEqual, Alloc>::unordered_set;

        // 標準のunordered_setからの変換コンストラクタ
        explicit UnorderedSet(const std::unordered_set<Key, Hash, KeyEqual>& other) 
            : std::unordered_set<Key, Hash, KeyEqual, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のunordered_setからの変換ムーブコンストラクタ
        explicit UnorderedSet(std::unordered_set<Key, Hash, KeyEqual>&& other) 
            : std::unordered_set<Key, Hash, KeyEqual, Alloc>(
                std::make_move_iterator(other.begin()), 
                std::make_move_iterator(other.end())
            )
        {
            other.clear();
        }

        // バケットカウントとアロケータを指定するコンストラクタ
        explicit UnorderedSet(size_t bucketCount, const Alloc& alloc = Alloc())
            : std::unordered_set<Key, Hash, KeyEqual, Alloc>(bucketCount, Hash(), KeyEqual(), alloc)
        {
        }

        // std::unordered_setへの変換演算子
        operator std::unordered_set<Key, Hash, KeyEqual>() const
        {
            return std::unordered_set<Key, Hash, KeyEqual>(this->begin(), this->end());
        }
    };
}