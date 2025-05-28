#pragma once

// Windowsマクロを無効化
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <string>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <ostream>
#include <limits>
#include <Windows.h>
#include <tchar.h>
#include "Span.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief 文字列の一部を参照するビュークラス
     * 所有権を持たず、メモリのコピーを行わない効率的な文字列参照型
     * TCHARベースでWindowsのUNICODE設定に応じて適切に動作します
     */
    class StringView
    {
    public:
        // 型定義
        using value_type = TCHAR;
        using pointer = const TCHAR*;
        using const_pointer = const TCHAR*;
        using reference = const TCHAR&;
        using const_reference = const TCHAR&;
        using const_iterator = const_pointer;
        using iterator = const_iterator;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using reverse_iterator = const_reverse_iterator;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        // 特殊値
        static constexpr size_type npos = static_cast<size_type>(-1);

        // デフォルトコンストラクタ
        constexpr StringView() noexcept : data_(nullptr), size_(0) {}

        // コピーコンストラクタとコピー代入（デフォルト）
        constexpr StringView(const StringView&) noexcept = default;
        constexpr StringView& operator=(const StringView&) noexcept = default;

        // TCHAR文字列からのコンストラクタ
        constexpr StringView(const TCHAR* str)
            : data_(str), size_(str ? _tcslen(str) : 0) {}

        // ポインタと長さからのコンストラクタ
        constexpr StringView(const TCHAR* str, size_type len) noexcept
            : data_(str), size_(len) {}

        // std::basic_stringからのコンストラクタ
        StringView(const std::basic_string<TCHAR>& str) noexcept
            : data_(str.data()), size_(str.size()) {}

        // NorvesLib::Core::Container::Stringからのコンストラクタ
        template<typename StringType, 
                 typename = std::enable_if_t<
                    std::is_convertible_v<
                        const typename StringType::value_type*, const TCHAR*> &&
                    std::is_convertible_v<
                        typename StringType::size_type, size_type>
                 >
        >
        constexpr StringView(const StringType& str) noexcept
            : data_(str.data()), size_(str.size()) {}

        // イテレータアクセス
        constexpr const_iterator begin() const noexcept { return data_; }
        constexpr const_iterator end() const noexcept { return data_ + size_; }
        constexpr const_iterator cbegin() const noexcept { return begin(); }
        constexpr const_iterator cend() const noexcept { return end(); }
        constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
        constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
        constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
        constexpr const_reverse_iterator crend() const noexcept { return rend(); }

        // 要素アクセス
        constexpr const_reference operator[](size_type pos) const noexcept {
            return data_[pos];
        }

        constexpr const_reference at(size_type pos) const {
            if (pos >= size_) {
                throw std::out_of_range("StringView: index out of range");
            }
            return data_[pos];
        }

        constexpr const_reference front() const noexcept {
            return data_[0];
        }

        constexpr const_reference back() const noexcept {
            return data_[size_ - 1];
        }

        constexpr const_pointer data() const noexcept {
            return data_;
        }

        // 容量
        constexpr size_type size() const noexcept {
            return size_;
        }

        constexpr size_type length() const noexcept {
            return size_;
        }

        constexpr size_type max_size() const noexcept {
            return std::numeric_limits<size_type>::max();
        }

        [[nodiscard]] constexpr bool empty() const noexcept {
            return size_ == 0;
        }

        // 文字列操作
        constexpr void remove_prefix(size_type n) noexcept {
            data_ += n;
            size_ -= n;
        }

        constexpr void remove_suffix(size_type n) noexcept {
            size_ -= n;
        }

        constexpr void swap(StringView& other) noexcept {
            std::swap(data_, other.data_);
            std::swap(size_, other.size_);
        }

        // 部分文字列の取得
        constexpr StringView substr(size_type pos = 0, size_type count = npos) const {
            if (pos > size_) {
                throw std::out_of_range("StringView::substr: position out of range");
            }
            const size_type rcount = std::min(count, size_ - pos);
            return StringView(data_ + pos, rcount);
        }

        // 文字列比較
        int compare(StringView other) const noexcept {
            const size_type rlen = std::min(size_, other.size_);
            int result = ::_tcsncmp(data_, other.data_, rlen);
            if (result == 0) {
                if (size_ < other.size_) return -1;
                if (size_ > other.size_) return 1;
            }
            return result;
        }

        // constexprではない比較関数に変更
        int compare(size_type pos1, size_type count1, StringView other) const {
            return substr(pos1, count1).compare(other);
        }

        int compare(size_type pos1, size_type count1, StringView other,
                            size_type pos2, size_type count2) const {
            return substr(pos1, count1).compare(other.substr(pos2, count2));
        }

        int compare(const TCHAR* s) const {
            return compare(StringView(s));
        }

        int compare(size_type pos1, size_type count1, const TCHAR* s) const {
            return substr(pos1, count1).compare(StringView(s));
        }

        int compare(size_type pos1, size_type count1, const TCHAR* s, size_type count2) const {
            return substr(pos1, count1).compare(StringView(s, count2));
        }

        // 文字列検索
        constexpr size_type find(StringView v, size_type pos = 0) const noexcept {
            if (pos > size_ || v.empty() || v.size_ > size_ - pos) {
                return npos;
            }
            
            const auto it = std::search(
                this->begin() + pos, this->end(),
                v.begin(), v.end(),
                [](TCHAR a, TCHAR b) { return a == b; }
            );
            
            return it == this->end() ? npos : std::distance(this->begin(), it);
        }

        constexpr size_type find(TCHAR ch, size_type pos = 0) const noexcept {
            if (pos >= size_) {
                return npos;
            }
            
            const auto it = std::find_if(
                this->begin() + pos, this->end(),
                [ch](TCHAR c) { return c == ch; }
            );
            
            return it == this->end() ? npos : std::distance(this->begin(), it);
        }

        constexpr size_type find(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return find(StringView(s, count), pos);
        }

        constexpr size_type find(const TCHAR* s, size_type pos = 0) const noexcept {
            return find(StringView(s), pos);
        }

        // 逆方向検索
        size_type rfind(StringView v, size_type pos = npos) const noexcept {
            if (v.empty()) {
                return std::min(pos, size_);
            }
            
            if (v.size_ > size_) {
                return npos;
            }
            
            pos = std::min(pos, size_ - v.size_);
            
            for (auto i = pos + 1; i > 0; --i) {
                const size_type idx = i - 1;
                if (::_tcsncmp(data_ + idx, v.data_, v.size_) == 0) {
                    return idx;
                }
            }
            
            return npos;
        }

        size_type rfind(TCHAR ch, size_type pos = npos) const noexcept {
            if (empty()) {
                return npos;
            }
            
            pos = std::min(pos, size_ - 1);
            
            for (auto i = pos + 1; i > 0; --i) {
                const size_type idx = i - 1;
                if (data_[idx] == ch) {
                    return idx;
                }
            }
            
            return npos;
        }

        size_type rfind(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return rfind(StringView(s, count), pos);
        }

        size_type rfind(const TCHAR* s, size_type pos = npos) const noexcept {
            return rfind(StringView(s), pos);
        }

        // いずれかの文字を検索
        constexpr size_type find_first_of(StringView v, size_type pos = 0) const noexcept {
            if (pos >= size_ || v.empty()) {
                return npos;
            }
            
            for (size_type i = pos; i < size_; ++i) {
                for (const auto ch : v) {
                    if (data_[i] == ch) {
                        return i;
                    }
                }
            }
            
            return npos;
        }

        constexpr size_type find_first_of(TCHAR ch, size_type pos = 0) const noexcept {
            return find(ch, pos);
        }

        constexpr size_type find_first_of(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return find_first_of(StringView(s, count), pos);
        }

        constexpr size_type find_first_of(const TCHAR* s, size_type pos = 0) const noexcept {
            return find_first_of(StringView(s), pos);
        }

        // いずれの文字も検索しない
        constexpr size_type find_first_not_of(StringView v, size_type pos = 0) const noexcept {
            if (pos >= size_) {
                return npos;
            }
            
            for (size_type i = pos; i < size_; ++i) {
                bool found = false;
                for (const auto ch : v) {
                    if (data_[i] == ch) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return i;
                }
            }
            
            return npos;
        }

        constexpr size_type find_first_not_of(TCHAR ch, size_type pos = 0) const noexcept {
            if (pos >= size_) {
                return npos;
            }
            
            for (size_type i = pos; i < size_; ++i) {
                if (data_[i] != ch) {
                    return i;
                }
            }
            
            return npos;
        }

        constexpr size_type find_first_not_of(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return find_first_not_of(StringView(s, count), pos);
        }

        constexpr size_type find_first_not_of(const TCHAR* s, size_type pos = 0) const noexcept {
            return find_first_not_of(StringView(s), pos);
        }

        // 最後のいずれかの文字を検索
        size_type find_last_of(StringView v, size_type pos = npos) const noexcept {
            if (empty() || v.empty()) {
                return npos;
            }
            
            pos = std::min(pos, size_ - 1);
            
            for (auto i = pos + 1; i > 0; --i) {
                const size_type idx = i - 1;
                for (const auto ch : v) {
                    if (data_[idx] == ch) {
                        return idx;
                    }
                }
            }
            
            return npos;
        }

        size_type find_last_of(TCHAR ch, size_type pos = npos) const noexcept {
            return rfind(ch, pos);
        }

        size_type find_last_of(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return find_last_of(StringView(s, count), pos);
        }

        size_type find_last_of(const TCHAR* s, size_type pos = npos) const noexcept {
            return find_last_of(StringView(s), pos);
        }

        // 最後のいずれでもない文字を検索
        constexpr size_type find_last_not_of(StringView v, size_type pos = npos) const noexcept {
            if (empty()) {
                return npos;
            }
            
            pos = std::min(pos, size_ - 1);
            
            for (auto i = pos + 1; i > 0; --i) {
                const size_type idx = i - 1;
                bool found = false;
                for (const auto ch : v) {
                    if (data_[idx] == ch) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return idx;
                }
            }
            
            return npos;
        }

        constexpr size_type find_last_not_of(TCHAR ch, size_type pos = npos) const noexcept {
            if (empty()) {
                return npos;
            }
            
            pos = std::min(pos, size_ - 1);
            
            for (auto i = pos + 1; i > 0; --i) {
                const size_type idx = i - 1;
                if (data_[idx] != ch) {
                    return idx;
                }
            }
            
            return npos;
        }

        constexpr size_type find_last_not_of(const TCHAR* s, size_type pos, size_type count) const noexcept {
            return find_last_not_of(StringView(s, count), pos);
        }

        constexpr size_type find_last_not_of(const TCHAR* s, size_type pos = npos) const noexcept {
            return find_last_not_of(StringView(s), pos);
        }

        // Spanへの変換メソッド
        constexpr Span<const TCHAR> as_span() const noexcept {
            return Span<const TCHAR>(data_, size_);
        }

        // std::basic_string への変換
        std::basic_string<TCHAR> to_string() const {
            return std::basic_string<TCHAR>(data_, size_);
        }

        // ストリーム出力
        friend std::basic_ostream<TCHAR>& operator<<(std::basic_ostream<TCHAR>& os, const StringView& sv) {
            for (const auto c : sv) {
                os << c;
            }
            return os;
        }

    private:
        const_pointer data_;
        size_type size_;
    };    // 比較演算子 - inlineで重複定義を回避
    inline bool operator==(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) == 0;
    }

    inline bool operator!=(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) != 0;
    }

    inline bool operator<(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) < 0;
    }

    inline bool operator<=(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) <= 0;
    }

    inline bool operator>(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) > 0;
    }

    inline bool operator>=(StringView lhs, StringView rhs) noexcept {
        return lhs.compare(rhs) >= 0;
    }
}