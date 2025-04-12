#pragma once

#include <list>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用するリスト実装
     * std::listのラッパーとして機能します
     * 
     * @tparam T 格納する型
     * @tparam Alloc アロケータの型（デフォルトは内部Allocator）
     */
    template<typename T, typename Alloc = Allocator<T>>
    class List : public std::list<T, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::list<T, Alloc>::list;

        // 標準のlistからの変換コンストラクタ
        explicit List(const std::list<T>& other) 
            : std::list<T, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のlistからの変換ムーブコンストラクタ
        explicit List(std::list<T>&& other) 
            : std::list<T, Alloc>(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // std::listへの変換演算子
        operator std::list<T>() const
        {
            return std::list<T>(this->begin(), this->end());
        }
    };
}