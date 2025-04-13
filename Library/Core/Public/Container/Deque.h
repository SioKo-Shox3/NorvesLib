#pragma once

#include <deque>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief 双方向キュー（デック）コンテナ
     * 
     * std::dequeのラッパークラスで、独自のアロケータを使用します。
     * 両端からの要素追加・削除を効率的に行えるコンテナです。
     * 
     * @tparam T 要素の型
     * @tparam Alloc アロケータの型（デフォルトは独自アロケータ）
     */
    template <typename T, typename Alloc = Allocator<T>>
    class Deque : public std::deque<T, Alloc>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::deque<T, Alloc>::deque;

        // 標準のdequeからの変換コンストラクタ
        explicit Deque(const std::deque<T>& other) 
            : std::deque<T, Alloc>(other.begin(), other.end()) 
        {
        }

        // 標準のdequeからの変換ムーブコンストラクタ
        explicit Deque(std::deque<T>&& other) 
            : std::deque<T, Alloc>(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // std::dequeへの変換演算子
        operator std::deque<T>() const
        {
            return std::deque<T>(this->begin(), this->end());
        }
    };
}