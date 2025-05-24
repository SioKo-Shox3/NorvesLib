#pragma once

#include <queue>
#include "Deque.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief キューコンテナ
     * 
     * std::queueのラッパークラスで、独自のコンテナとアロケータを使用します。
     * FIFO（先入れ先出し）の動作を提供するアダプタコンテナです。
     * 
     * @tparam T 要素の型
     * @tparam Container 内部コンテナの型（デフォルトは独自のDeque）
     */
    template <typename T, typename Container = Deque<T>>
    class Queue : public std::queue<T, Container>
    {
    public:
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using std::queue<T, Container>::queue;

        // 標準のqueueからの変換コンストラクタ
        template <typename OtherContainer>
        explicit Queue(const std::queue<T, OtherContainer>& other) 
        {
            // queueはコピーが特殊なので、要素をコピーする
            std::queue<T, OtherContainer> temp = other;
            while (!temp.empty()) {
                this->push(temp.front());
                temp.pop();
            }
        }

        // 標準のqueueからの変換ムーブコンストラクタ
        template <typename OtherContainer>
        explicit Queue(std::queue<T, OtherContainer>&& other)
        {
            // queueはムーブが特殊なので、要素をムーブする
            while (!other.empty()) {
                this->push(std::move(other.front()));
                other.pop();
            }
        }

        // std::queueへの変換演算子
        operator std::queue<T>() const
        {
            std::queue<T> result;
            std::queue<T, Container> temp = *this;
            while (!temp.empty()) {
                result.push(temp.front());
                temp.pop();
            }
            return result;
        }
    };
}