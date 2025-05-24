#pragma once

#include <cstddef>
#include <array>
#include <iterator>
#include <type_traits>
#include <stdexcept>

namespace NorvesLib::Core::Container
{
    /**
     * @brief 連続したメモリ範囲を参照するビュークラス
     * 所有権を持たず、メモリのコピーを行わない効率的な参照型
     * 
     * @tparam T 要素の型
     * @tparam Extent 範囲の要素数（-1は動的サイズを表す）
     */
    template <typename T, std::ptrdiff_t Extent = -1>
    class Span
    {
    public:
        // 要素型の定義
        using element_type = T;
        using value_type = std::remove_cv_t<T>;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using const_pointer = const T*;
        using reference = T&;
        using const_reference = const T&;
        
        // イテレータ型の定義
        using iterator = pointer;
        using const_iterator = const_pointer;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // 静的エクステント情報
        static constexpr std::ptrdiff_t extent = Extent;

        // デフォルトコンストラクタ（空のSpan）
        constexpr Span() noexcept : data_(nullptr), size_(0) {
            static_assert(Extent == 0 || Extent == -1, "Non-empty Span requires non-null data");
        }

        // ポインタとサイズからのコンストラクタ
        constexpr Span(pointer ptr, size_type count) noexcept : data_(ptr), size_(count) {
            static_assert(Extent == -1 || Extent == count, "Size mismatch in fixed-size Span");
        }

        // 範囲指定コンストラクタ
        constexpr Span(pointer first, pointer last) noexcept : data_(first), size_(last - first) {
            static_assert(Extent == -1 || Extent == (last - first), "Size mismatch in fixed-size Span");
        }

        // 配列からのコンストラクタ
        template <std::size_t N>
        constexpr Span(element_type (&arr)[N]) noexcept : data_(arr), size_(N) {
            static_assert(Extent == -1 || Extent == N, "Size mismatch in fixed-size Span");
        }

        // std::arrayからのコンストラクタ
        template <std::size_t N, typename = std::enable_if_t<(Extent == -1 || Extent == N)>>
        constexpr Span(std::array<value_type, N>& arr) noexcept : data_(arr.data()), size_(N) {}

        // const std::arrayからのコンストラクタ
        template <std::size_t N, typename = std::enable_if_t<(Extent == -1 || Extent == N)>>
        constexpr Span(const std::array<value_type, N>& arr) noexcept : data_(arr.data()), size_(N) {}

        // コンテナからのコンストラクタ（data()とsize()メソッドを持つもの）
        template <
            typename Container,
            typename = std::enable_if_t<
                !std::is_array_v<Container> &&
                std::is_convertible_v<
                    std::remove_pointer_t<decltype(std::declval<Container&>().data())>(*)[],
                    element_type(*)[]
                > &&
                std::is_convertible_v<
                    decltype(std::declval<Container&>().size()),
                    size_type
                >
            >
        >
        constexpr Span(Container& cont) noexcept : data_(cont.data()), size_(cont.size()) {
            static_assert(Extent == -1 || Extent == cont.size(), "Size mismatch in fixed-size Span");
        }

        // constコンテナからのコンストラクタ
        template <
            typename Container,
            typename = std::enable_if_t<
                !std::is_array_v<Container> &&
                std::is_convertible_v<
                    std::remove_pointer_t<decltype(std::declval<const Container&>().data())>(*)[],
                    element_type(*)[]
                > &&
                std::is_convertible_v<
                    decltype(std::declval<const Container&>().size()),
                    size_type
                >
            >
        >
        constexpr Span(const Container& cont) noexcept : data_(cont.data()), size_(cont.size()) {
            static_assert(Extent == -1 || Extent == cont.size(), "Size mismatch in fixed-size Span");
        }

        // 他のSpanからのコンストラクタ
        template <typename U, std::ptrdiff_t OtherExtent,
            typename = std::enable_if_t<
                std::is_convertible_v<U(*)[], T(*)[]> &&
                (Extent == -1 || OtherExtent == -1 || Extent == OtherExtent)
            >
        >
        constexpr Span(const Span<U, OtherExtent>& other) noexcept 
            : data_(other.data()), size_(other.size()) {}

        // コピーコンストラクタとコピー代入演算子（暗黙に定義）
        constexpr Span(const Span&) noexcept = default;
        constexpr Span& operator=(const Span&) noexcept = default;

        // イテレータアクセス
        constexpr iterator begin() const noexcept { return data_; }
        constexpr iterator end() const noexcept { return data_ + size_; }
        constexpr const_iterator cbegin() const noexcept { return data_; }
        constexpr const_iterator cend() const noexcept { return data_ + size_; }
        constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
        constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
        constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
        constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

        // 要素アクセス
        constexpr reference operator[](size_type idx) const noexcept {
            return data_[idx];
        }

        constexpr reference at(size_type idx) const {
            if (idx >= size_) {
                throw std::out_of_range("Span: index out of range");
            }
            return data_[idx];
        }

        constexpr reference front() const noexcept {
            return data_[0];
        }

        constexpr reference back() const noexcept {
            return data_[size_ - 1];
        }

        constexpr pointer data() const noexcept {
            return data_;
        }

        // 観察メソッド
        constexpr size_type size() const noexcept {
            return size_;
        }

        constexpr size_type size_bytes() const noexcept {
            return size_ * sizeof(T);
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return size_ == 0;
        }

        // 部分範囲取得
        template <std::ptrdiff_t Count>
        constexpr Span<T, Count> first() const noexcept {
            static_assert(Count >= 0, "Count must be non-negative");
            static_assert(Extent == -1 || Count <= Extent, "Count out of range in first()");
            return Span<T, Count>(data_, Count);
        }

        constexpr Span<T, -1> first(size_type count) const noexcept {
            return Span<T, -1>(data_, count);
        }

        template <std::ptrdiff_t Count>
        constexpr Span<T, Count> last() const noexcept {
            static_assert(Count >= 0, "Count must be non-negative");
            static_assert(Extent == -1 || Count <= Extent, "Count out of range in last()");
            return Span<T, Count>(data_ + (size_ - Count), Count);
        }

        constexpr Span<T, -1> last(size_type count) const noexcept {
            return Span<T, -1>(data_ + (size_ - count), count);
        }

        template <std::ptrdiff_t Offset, std::ptrdiff_t Count = -1>
        constexpr auto subspan() const noexcept {
            static_assert(Offset >= 0, "Offset must be non-negative");
            static_assert(Extent == -1 || Offset <= Extent, "Offset out of range in subspan()");
            static_assert(Count == -1 || Count >= 0, "Count must be non-negative");
            static_assert(Extent == -1 || (Count == -1 || Offset + Count <= Extent), "Count out of range in subspan()");
            
            constexpr std::ptrdiff_t NewExtent = Count != -1 ? Count : (Extent != -1 ? Extent - Offset : -1);
            return Span<T, NewExtent>(data_ + Offset, Count == -1 ? size_ - Offset : Count);
        }

        constexpr Span<T, -1> subspan(size_type offset, size_type count = -1) const noexcept {
            size_type newCount = count == static_cast<size_type>(-1) ? size_ - offset : count;
            return Span<T, -1>(data_ + offset, newCount);
        }

    private:
        pointer data_;
        size_type size_;
    };

    // 推論ガイド：配列からの変換
    template <typename T, std::size_t N>
    Span(T (&)[N]) -> Span<T, N>;

    // 推論ガイド：std::arrayからの変換
    template <typename T, std::size_t N>
    Span(std::array<T, N>&) -> Span<T, N>;

    template <typename T, std::size_t N>
    Span(const std::array<T, N>&) -> Span<const T, N>;

    // 比較演算子
    template <typename T, std::ptrdiff_t X, typename U, std::ptrdiff_t Y>
    constexpr bool operator==(const Span<T, X>& lhs, const Span<U, Y>& rhs) noexcept {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i] != rhs[i]) return false;
        }
        return true;
    }

    template <typename T, std::ptrdiff_t X, typename U, std::ptrdiff_t Y>
    constexpr bool operator!=(const Span<T, X>& lhs, const Span<U, Y>& rhs) noexcept {
        return !(lhs == rhs);
    }
}