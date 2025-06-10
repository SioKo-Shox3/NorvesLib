#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <algorithm>
#include <type_traits>
#include <stdexcept> // std::out_of_range例外のためにインクルード
#include "Allocator.h"

// Windowsマクロを無効化
#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace NorvesLib::Core::Container
{
    // スタック上に確保するサイズの閾値（これより大きいとヒープに確保）
    constexpr size_t STACK_SIZE_THRESHOLD = 64;

    /**
     * @brief 固定長配列の実装
     * 小さいサイズはスタック上、大きいサイズは自動的にGlobalMemoryAllocator経由でヒープ上に確保
     *
     * @tparam T 格納する型
     * @tparam N 配列サイズ
     */
    template <typename T, std::size_t N>
    class FixedArray
    {
    private:
        // 小さいサイズならスタック、大きいサイズならヒープに確保する方式
        using StackStorage = std::array<T, N>;
        using HeapStorage = struct
        {
            T *data;
            Allocator<T> allocator;
        };

        typename std::conditional<(N <= STACK_SIZE_THRESHOLD),
                                  StackStorage,
                                  HeapStorage>::type storage;

        // ヘルパー関数：ヒープストレージかどうかを判定
        static constexpr bool isHeapStorage()
        {
            return N > STACK_SIZE_THRESHOLD;
        }

        // データポインタ取得ヘルパー関数
        T *getData()
        {
            if constexpr (isHeapStorage())
            {
                return storage.data;
            }
            else
            {
                return storage.data();
            }
        }

        const T *getData() const
        {
            if constexpr (isHeapStorage())
            {
                return storage.data;
            }
            else
            {
                return storage.data();
            }
        }

    public:
        // STL互換のための型定義
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference = T &;
        using const_reference = const T &;
        using pointer = T *;
        using const_pointer = const T *;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        /**
         * @brief デフォルトコンストラクタ
         * 全要素をデフォルト初期化
         */
        FixedArray()
        {
            if constexpr (isHeapStorage())
            {
                storage.data = storage.allocator.allocate(N);
                for (size_type i = 0; i < N; ++i)
                {
                    new (storage.data + i) T();
                }
            }
        }

        /**
         * @brief 値で初期化するコンストラクタ
         * @param value 全要素を初期化する値
         */
        FixedArray(const T &value)
        {
            if constexpr (isHeapStorage())
            {
                storage.data = storage.allocator.allocate(N);
                for (size_type i = 0; i < N; ++i)
                {
                    new (storage.data + i) T(value);
                }
            }
            else
            {
                storage.fill(value);
            }
        }

        /**
         * @brief 初期化リストからのコンストラクタ
         * @param init 初期化リスト
         */
        FixedArray(std::initializer_list<T> init)
        {
            if constexpr (isHeapStorage())
            {
                storage.data = storage.allocator.allocate(N);
                std::size_t i = 0;
                for (const auto &value : init)
                {
                    if (i >= N)
                        break;
                    new (storage.data + i) T(value);
                    ++i;
                }
                // 残りの要素をデフォルト初期化
                for (; i < N; ++i)
                {
                    new (storage.data + i) T();
                }
            }
            else
            {
                std::size_t i = 0;
                for (const auto &value : init)
                {
                    if (i >= N)
                        break;
                    storage[i++] = value;
                }
            }
        }

        /**
         * @brief コピーコンストラクタ
         * @param other コピー元の配列
         */
        FixedArray(const FixedArray &other)
        {
            if constexpr (isHeapStorage())
            {
                storage.data = storage.allocator.allocate(N);
                for (size_type i = 0; i < N; ++i)
                {
                    new (storage.data + i) T(other[i]);
                }
            }
            else
            {
                storage = other.storage;
            }
        }

        /**
         * @brief ムーブコンストラクタ
         * @param other ムーブ元の配列
         */
        FixedArray(FixedArray &&other) noexcept
        {
            if constexpr (isHeapStorage())
            {
                storage.data = other.storage.data;
                other.storage.data = nullptr;
            }
            else
            {
                for (size_type i = 0; i < N; ++i)
                {
                    storage[i] = std::move(other.storage[i]);
                }
            }
        }

        /**
         * @brief std::arrayからのコンストラクタ
         * @param arr コピー元のstd::array
         */
        explicit FixedArray(const std::array<T, N> &arr)
        {
            if constexpr (isHeapStorage())
            {
                storage.data = storage.allocator.allocate(N);
                for (size_type i = 0; i < N; ++i)
                {
                    new (storage.data + i) T(arr[i]);
                }
            }
            else
            {
                storage = arr;
            }
        }

        /**
         * @brief デストラクタ
         */
        ~FixedArray()
        {
            if constexpr (isHeapStorage())
            {
                if (storage.data)
                {
                    for (size_type i = 0; i < N; ++i)
                    {
                        (storage.data + i)->~T();
                    }
                    storage.allocator.deallocate(storage.data, N);
                }
            }
        }

        /**
         * @brief コピー代入演算子
         * @param other コピー元の配列
         * @return このオブジェクトへの参照
         */
        FixedArray &operator=(const FixedArray &other)
        {
            if (this != &other)
            {
                if constexpr (isHeapStorage())
                {
                    for (size_type i = 0; i < N; ++i)
                    {
                        storage.data[i] = other.storage.data[i];
                    }
                }
                else
                {
                    storage = other.storage;
                }
            }
            return *this;
        }

        /**
         * @brief ムーブ代入演算子
         * @param other ムーブ元の配列
         * @return このオブジェクトへの参照
         */
        FixedArray &operator=(FixedArray &&other) noexcept
        {
            if (this != &other)
            {
                if constexpr (isHeapStorage())
                {
                    if (storage.data)
                    {
                        for (size_type i = 0; i < N; ++i)
                        {
                            (storage.data + i)->~T();
                        }
                        storage.allocator.deallocate(storage.data, N);
                    }
                    storage.data = other.storage.data;
                    other.storage.data = nullptr;
                }
                else
                {
                    for (size_type i = 0; i < N; ++i)
                    {
                        storage[i] = std::move(other.storage[i]);
                    }
                }
            }
            return *this;
        }

        /**
         * @brief std::arrayへの変換演算子
         * @return このオブジェクトのコピーをstd::arrayとして
         */
        explicit operator std::array<T, N>() const
        {
            std::array<T, N> arr;
            if constexpr (isHeapStorage())
            {
                for (size_type i = 0; i < N; ++i)
                {
                    arr[i] = storage.data[i];
                }
            }
            else
            {
                arr = storage;
            }
            return arr;
        }

        // イテレータアクセス

        /**
         * @brief 先頭要素へのイテレータを取得
         * @return 先頭要素へのイテレータ
         */
        iterator begin() noexcept
        {
            return getData();
        }

        /**
         * @brief 先頭要素への定数イテレータを取得
         * @return 先頭要素への定数イテレータ
         */
        const_iterator begin() const noexcept
        {
            return getData();
        }

        /**
         * @brief 末尾の次の要素へのイテレータを取得
         * @return 末尾の次の要素へのイテレータ
         */
        iterator end() noexcept
        {
            return getData() + N;
        }

        /**
         * @brief 末尾の次の要素への定数イテレータを取得
         * @return 末尾の次の要素への定数イテレータ
         */
        const_iterator end() const noexcept
        {
            return getData() + N;
        }

        /**
         * @brief 先頭要素への定数イテレータを取得
         * @return 先頭要素への定数イテレータ
         */
        const_iterator cbegin() const noexcept
        {
            return begin();
        }

        /**
         * @brief 末尾の次の要素への定数イテレータを取得
         * @return 末尾の次の要素への定数イテレータ
         */
        const_iterator cend() const noexcept
        {
            return end();
        }

        /**
         * @brief 逆順の先頭要素へのイテレータを取得
         * @return 逆順の先頭要素へのイテレータ
         */
        reverse_iterator rbegin() noexcept
        {
            return reverse_iterator(end());
        }

        /**
         * @brief 逆順の先頭要素への定数イテレータを取得
         * @return 逆順の先頭要素への定数イテレータ
         */
        const_reverse_iterator rbegin() const noexcept
        {
            return const_reverse_iterator(end());
        }

        /**
         * @brief 逆順の末尾の次の要素へのイテレータを取得
         * @return 逆順の末尾の次の要素へのイテレータ
         */
        reverse_iterator rend() noexcept
        {
            return reverse_iterator(begin());
        }

        /**
         * @brief 逆順の末尾の次の要素への定数イテレータを取得
         * @return 逆順の末尾の次の要素への定数イテレータ
         */
        const_reverse_iterator rend() const noexcept
        {
            return const_reverse_iterator(begin());
        }

        /**
         * @brief 逆順の先頭要素への定数イテレータを取得
         * @return 逆順の先頭要素への定数イテレータ
         */
        const_reverse_iterator crbegin() const noexcept
        {
            return rbegin();
        }

        /**
         * @brief 逆順の末尾の次の要素への定数イテレータを取得
         * @return 逆順の末尾の次の要素への定数イテレータ
         */
        const_reverse_iterator crend() const noexcept
        {
            return rend();
        }

        // 要素アクセス

        /**
         * @brief 要素へのアクセス（境界チェックなし）
         * @param index インデックス
         * @return インデックスの位置にある要素への参照
         */
        reference operator[](size_type index) noexcept
        {
            if constexpr (isHeapStorage())
            {
                return storage.data[index];
            }
            else
            {
                return storage[index];
            }
        }

        /**
         * @brief 要素への定数アクセス（境界チェックなし）
         * @param index インデックス
         * @return インデックスの位置にある要素への定数参照
         */
        const_reference operator[](size_type index) const noexcept
        {
            if constexpr (isHeapStorage())
            {
                return storage.data[index];
            }
            else
            {
                return storage[index];
            }
        }

        /**
         * @brief 要素へのアクセス（境界チェックあり）
         * @param index インデックス
         * @return インデックスの位置にある要素への参照
         * @throws std::out_of_range インデックスが範囲外の場合
         */
        reference at(size_type index)
        {
            if (index >= N)
            {
                throw std::out_of_range("FixedArray: index out of range");
            }
            return (*this)[index];
        }

        /**
         * @brief 要素への定数アクセス（境界チェックあり）
         * @param index インデックス
         * @return インデックスの位置にある要素への定数参照
         * @throws std::out_of_range インデックスが範囲外の場合
         */
        const_reference at(size_type index) const
        {
            if (index >= N)
            {
                throw std::out_of_range("FixedArray: index out of range");
            }
            return (*this)[index];
        }

        /**
         * @brief 先頭要素への参照を取得
         * @return 先頭要素への参照
         */
        reference front() noexcept
        {
            return (*this)[0];
        }

        /**
         * @brief 先頭要素への定数参照を取得
         * @return 先頭要素への定数参照
         */
        const_reference front() const noexcept
        {
            return (*this)[0];
        }

        /**
         * @brief 末尾要素への参照を取得
         * @return 末尾要素への参照
         */
        reference back() noexcept
        {
            return (*this)[N - 1];
        }

        /**
         * @brief 末尾要素への定数参照を取得
         * @return 末尾要素への定数参照
         */
        const_reference back() const noexcept
        {
            return (*this)[N - 1];
        }

        /**
         * @brief 内部データへのポインタを取得
         * @return 内部データへのポインタ
         */
        pointer data() noexcept
        {
            return getData();
        }

        /**
         * @brief 内部データへの定数ポインタを取得
         * @return 内部データへの定数ポインタ
         */
        const_pointer data() const noexcept
        {
            return getData();
        }

        // 容量

        /**
         * @brief 配列が空かどうかを判定
         * @return 配列が空の場合はtrue、それ以外はfalse
         */
        [[nodiscard]] constexpr bool empty() const noexcept
        {
            return N == 0;
        }

        /**
         * @brief 配列のサイズを取得
         * @return 配列のサイズ
         */
        [[nodiscard]] constexpr size_type size() const noexcept
        {
            return N;
        }

        /**
         * @brief 配列の最大サイズを取得
         * @return 配列の最大サイズ
         */
        [[nodiscard]] constexpr size_type max_size() const noexcept
        {
            return N;
        }

        // オペレーション

        /**
         * @brief 全要素に指定した値を代入
         * @param value 代入する値
         */
        void fill(const T &value)
        {
            if constexpr (isHeapStorage())
            {
                for (size_type i = 0; i < N; ++i)
                {
                    storage.data[i] = value;
                }
            }
            else
            {
                storage.fill(value);
            }
        }

        /**
         * @brief 2つの配列の内容を交換
         * @param other 交換する配列
         */
        void swap(FixedArray &other) noexcept
        {
            if constexpr (isHeapStorage())
            {
                std::swap(storage.data, other.storage.data);
            }
            else
            {
                std::swap(storage, other.storage);
            }
        }
    };

    // 比較演算子

    /**
     * @brief 2つの配列が等しいかどうかを判定
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return 2つの配列が等しい場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator==(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin());
    }

    /**
     * @brief 2つの配列が等しくないかどうかを判定
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return 2つの配列が等しくない場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator!=(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return !(lhs == rhs);
    }

    /**
     * @brief 2つの配列を辞書順で比較（小なり）
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return lhsがrhsより小さい場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator<(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    }

    /**
     * @brief 2つの配列を辞書順で比較（小なりイコール）
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return lhsがrhs以下の場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator<=(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return !(rhs < lhs);
    }

    /**
     * @brief 2つの配列を辞書順で比較（大なり）
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return lhsがrhsより大きい場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator>(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return rhs < lhs;
    }

    /**
     * @brief 2つの配列を辞書順で比較（大なりイコール）
     * @param lhs 比較する配列
     * @param rhs 比較する配列
     * @return lhsがrhs以上の場合はtrue、それ以外はfalse
     */
    template <typename T, std::size_t N>
    bool operator>=(const FixedArray<T, N> &lhs, const FixedArray<T, N> &rhs)
    {
        return !(lhs < rhs);
    }
}