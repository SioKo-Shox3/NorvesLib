#pragma once

#include <vector>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用する可変長配列の実装
     * std::vectorのラッパーとして機能します
     * 
     * @tparam T 格納する型
     * @tparam Alloc アロケータの型（デフォルトは内部Allocator）
     */
    template<typename T, typename Alloc = Allocator<T>>
    class VariableArray : public std::vector<T, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::vector<T, Alloc>::vector;

        // 標準のvectorからの変換コンストラクタ
        explicit VariableArray(const std::vector<T>& other) 
            : std::vector<T, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のvectorからの変換ムーブコンストラクタ
        explicit VariableArray(std::vector<T>&& other) 
            : std::vector<T, Alloc>(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // std::vectorへの変換演算子
        operator std::vector<T>() const
        {
            return std::vector<T>(this->begin(), this->end());
        }
    };
}