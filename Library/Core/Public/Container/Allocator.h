#pragma once

#include <limits>
#include <cstddef>
#include <memory>
#include <type_traits>
#include "EngineGlobals/MemoryOverrides.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief STLコンテナと互換性のあるカスタムアロケーターの実装
     * NorvesLib::Memory のメモリ関数を内部的に使用します
     *
     * @tparam T アロケートする型
     */
    template <typename T>
    class Allocator
    {
    public:
        // STL互換性のための型定義
        using value_type = T;
        using pointer = T *;
        using const_pointer = const T *;
        using reference = T &;
        using const_reference = const T &;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;

        // 型変換用の再束縛アロケータータイプ
        template <typename U>
        struct rebind
        {
            using other = Allocator<U>;
        };

        // デフォルトコンストラクタ
        constexpr Allocator() noexcept = default;

        // コピーコンストラクタ
        constexpr Allocator(const Allocator &) noexcept = default;

        // 変換コンストラクタ
        template <typename U>
        constexpr Allocator(const Allocator<U> &) noexcept {}

        // デストラクタ
        ~Allocator() = default;

        /**
         * @brief メモリを確保する
         * @param count 要素数
         * @return 確保されたメモリの先頭ポインタ
         */
        [[nodiscard]] T *allocate(size_type count)
        {
            if (count > max_size())
            {
                throw std::bad_alloc();
            }

            if (count == 0)
            {
                return nullptr;
            }

            size_type bytes = count * sizeof(T);

            // アライメントを考慮
            constexpr size_type alignment = alignof(T);
            void *ptr = NorvesLib::Memory::AlignedMalloc(bytes, alignment);

            if (!ptr)
            {
                throw std::bad_alloc();
            }

            return static_cast<T *>(ptr);
        }

        /**
         * @brief メモリを解放する
         * @param ptr 解放するメモリポインタ
         * @param count 要素数（未使用）
         */
        void deallocate(T *ptr, [[maybe_unused]] size_type count)
        {
            NorvesLib::Memory::AlignedFree(ptr);
        }

        /**
         * @brief 最大アロケート可能なサイズを返す
         * @return 最大アロケート可能な要素数
         */
        [[nodiscard]] constexpr size_type max_size() const noexcept
        {
            return std::numeric_limits<size_type>::max() / sizeof(T);
        }
    };

    // 等値比較演算子
    template <typename T, typename U>
    constexpr bool operator==(const Allocator<T> &, const Allocator<U> &) noexcept
    {
        return true;
    }

    template <typename T, typename U>
    constexpr bool operator!=(const Allocator<T> &, const Allocator<U> &) noexcept
    {
        return false;
    }

} // namespace NorvesLib::Core::Container