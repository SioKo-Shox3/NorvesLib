#pragma once

#include <string>
#include <string_view>
#include <Windows.h>
#include <tchar.h>
#include "Allocator.h"
#include "VariableArray.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用するTCHAR文字列クラス
     * メモリオーバーライドとの競合を避けるため、コンポジションパターンで実装
     */
    class String
    {
    private:
        using StringType = std::basic_string<TCHAR, std::char_traits<TCHAR>, Allocator<TCHAR>>;
        StringType m_string;

    public:
        // 型エイリアス
        using value_type = TCHAR;
        using size_type = typename StringType::size_type;
        using difference_type = typename StringType::difference_type;
        using reference = typename StringType::reference;
        using const_reference = typename StringType::const_reference;
        using pointer = typename StringType::pointer;
        using const_pointer = typename StringType::const_pointer;
        using iterator = typename StringType::iterator;
        using const_iterator = typename StringType::const_iterator;
        using reverse_iterator = typename StringType::reverse_iterator;
        using const_reverse_iterator = typename StringType::const_reverse_iterator;
        using allocator_type = Allocator<TCHAR>;

        static constexpr size_type npos = StringType::npos;

        // デフォルトコンストラクタ
        String() = default;

        // コピーコンストラクタ
        String(const String &other) : m_string(other.m_string) {}

        // ムーブコンストラクタ
        String(String &&other) noexcept : m_string(std::move(other.m_string)) {}

        // TCHAR文字列からの構築
        String(const TCHAR *str) : m_string(str) {}

        // 文字列長を指定したTCHAR文字列からの構築
        String(const TCHAR *str, size_type count) : m_string(str, count) {}

        // 文字の繰り返し
        String(size_type count, TCHAR ch) : m_string(count, ch) {}

        // イテレータからの構築
        template <typename InputIt>
        String(InputIt first, InputIt last) : m_string(first, last) {}        // std::basic_stringからの変換コンストラクタ
        explicit String(const std::basic_string<TCHAR> &other)
        {
            // 安全なコピーベースの変換
            m_string.assign(other.data(), other.size());
        }

        // std::basic_stringからの変換ムーブコンストラクタ
        explicit String(std::basic_string<TCHAR> &&other)
        {
            // 異なるアロケータ間のムーブは危険なので、コピーベースで実装
            m_string.assign(other.data(), other.size());
            other.clear();
        }

        // basic_string_viewからの構築
        explicit String(std::basic_string_view<TCHAR> view) : m_string(view.data(), view.size()) {}

        // デストラクタ
        ~String() = default; // std::basic_stringへの変換演算子
        operator std::basic_string<TCHAR>() const
        {
            return std::basic_string<TCHAR>(m_string.data(), m_string.size());
        }

        // basic_string_viewへの変換演算子
        operator std::basic_string_view<TCHAR>() const
        {
            return std::basic_string_view<TCHAR>(m_string.data(), m_string.size());
        }

        // 代入演算子
        String &operator=(const String &other)
        {
            if (this != &other)
            {
                m_string = other.m_string;
            }
            return *this;
        }

        String &operator=(String &&other) noexcept
        {
            if (this != &other)
            {
                m_string = std::move(other.m_string);
            }
            return *this;
        }

        String &operator=(const TCHAR *str)
        {
            m_string = str;
            return *this;
        }        String &operator=(const std::basic_string<TCHAR> &str)
        {
            m_string.assign(str.data(), str.size());
            return *this;
        }

        String &operator=(std::basic_string<TCHAR> &&str)
        {
            m_string.assign(str.data(), str.size());
            str.clear();
            return *this;
        }

        String &operator=(std::basic_string_view<TCHAR> view)
        {
            m_string.assign(view.data(), view.size());
            return *this;
        }

        // basic_stringインターフェースの委譲
        const TCHAR *c_str() const { return m_string.c_str(); }
        const TCHAR *data() const { return m_string.data(); }
        size_type size() const { return m_string.size(); }
        size_type length() const { return m_string.length(); }
        bool empty() const { return m_string.empty(); }
        void clear() { m_string.clear(); }
        void reserve(size_type new_cap) { m_string.reserve(new_cap); }
        size_type capacity() const { return m_string.capacity(); }
        void shrink_to_fit() { m_string.shrink_to_fit(); }

        // アクセス演算子
        reference operator[](size_type pos) { return m_string[pos]; }
        const_reference operator[](size_type pos) const { return m_string[pos]; }
        reference at(size_type pos) { return m_string.at(pos); }
        const_reference at(size_type pos) const { return m_string.at(pos); }
        reference front() { return m_string.front(); }
        const_reference front() const { return m_string.front(); }
        reference back() { return m_string.back(); }
        const_reference back() const { return m_string.back(); }

        // イテレータ
        iterator begin() { return m_string.begin(); }
        const_iterator begin() const { return m_string.begin(); }
        const_iterator cbegin() const { return m_string.cbegin(); }
        iterator end() { return m_string.end(); }
        const_iterator end() const { return m_string.end(); }
        const_iterator cend() const { return m_string.cend(); }
        reverse_iterator rbegin() { return m_string.rbegin(); }
        const_reverse_iterator rbegin() const { return m_string.rbegin(); }
        const_reverse_iterator crbegin() const { return m_string.crbegin(); }
        reverse_iterator rend() { return m_string.rend(); }
        const_reverse_iterator rend() const { return m_string.rend(); }
        const_reverse_iterator crend() const { return m_string.crend(); }

        // 変更操作
        void push_back(TCHAR ch) { m_string.push_back(ch); }
        void pop_back() { m_string.pop_back(); }
        String &append(const String &str)
        {
            m_string.append(str.m_string);
            return *this;
        }
        String &append(const TCHAR *str)
        {
            m_string.append(str);
            return *this;
        }
        String &append(const TCHAR *str, size_type count)
        {
            m_string.append(str, count);
            return *this;
        }
        String &append(size_type count, TCHAR ch)
        {
            m_string.append(count, ch);
            return *this;
        }
        String &assign(const String &str)
        {
            m_string.assign(str.m_string);
            return *this;
        }
        String &assign(const TCHAR *str)
        {
            m_string.assign(str);
            return *this;
        }
        String &assign(const TCHAR *str, size_type count)
        {
            m_string.assign(str, count);
            return *this;
        }
        String &assign(size_type count, TCHAR ch)
        {
            m_string.assign(count, ch);
            return *this;
        }

        // 比較
        int compare(const String &str) const { return m_string.compare(str.m_string); }
        int compare(const TCHAR *str) const { return m_string.compare(str); }
        int compare(size_type pos1, size_type count1, const String &str) const { return m_string.compare(pos1, count1, str.m_string); }
        int compare(size_type pos1, size_type count1, const TCHAR *str) const { return m_string.compare(pos1, count1, str); }

        // 検索
        size_type find(const String &str, size_type pos = 0) const { return m_string.find(str.m_string, pos); }
        size_type find(const TCHAR *str, size_type pos = 0) const { return m_string.find(str, pos); }
        size_type find(TCHAR ch, size_type pos = 0) const { return m_string.find(ch, pos); }
        size_type rfind(const String &str, size_type pos = npos) const { return m_string.rfind(str.m_string, pos); }
        size_type rfind(const TCHAR *str, size_type pos = npos) const { return m_string.rfind(str, pos); }
        size_type rfind(TCHAR ch, size_type pos = npos) const { return m_string.rfind(ch, pos); }

        // NorvesLib独自のメソッド（下位互換性のため）
        size_type FindLast(const String &str) const { return rfind(str); }
        size_type FindLast(const TCHAR *str) const { return rfind(str); }
        size_type FindLast(TCHAR ch) const { return rfind(ch); }

        // 部分文字列
        String substr(size_type pos = 0, size_type len = npos) const { return String(m_string.substr(pos, len)); } // 部分文字列取得
        String Substring(size_t start, size_t length = npos) const
        {
            if (empty() || start >= size())
            {
                return String();
            }

            return String(m_string.substr(start, length));
        }

        // 文字列の長さ（文字数）を取得
        size_t Length() const
        {
            return size();
        }

        // 文字列が空かどうか
        bool IsEmpty() const
        {
            return empty();
        }

        // 文字列の比較
        bool Equals(const String &other) const
        {
            return compare(other) == 0;
        }

        bool Equals(const TCHAR *other) const
        {
            return compare(other) == 0;
        }

        // 指定した文字列で始まるかどうか
        bool StartsWith(const String &prefix) const
        {
            if (prefix.size() > size())
                return false;

            return compare(0, prefix.size(), prefix) == 0;
        }

        // 指定した文字列で終わるかどうか
        bool EndsWith(const String &suffix) const
        {
            if (suffix.size() > size())
                return false;

            return compare(size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        // 文字列を小文字に変換
        String ToLower() const
        {
            String result(*this);
            for (TCHAR &c : result)
            {
                c = static_cast<TCHAR>(_totlower(c));
            }
            return result;
        }

        // 文字列を大文字に変換
        String ToUpper() const
        {
            String result(*this);
            for (TCHAR &c : result)
            {
                c = static_cast<TCHAR>(_totupper(c));
            }
            return result;
        }

        // 比較演算子
        bool operator==(const String &other) const { return compare(other) == 0; }
        bool operator!=(const String &other) const { return compare(other) != 0; }
        bool operator<(const String &other) const { return compare(other) < 0; }
        bool operator<=(const String &other) const { return compare(other) <= 0; }
        bool operator>(const String &other) const { return compare(other) > 0; }
        bool operator>=(const String &other) const { return compare(other) >= 0; }
        bool operator==(const TCHAR *other) const { return compare(other) == 0; }
        bool operator!=(const TCHAR *other) const { return compare(other) != 0; }
        bool operator<(const TCHAR *other) const { return compare(other) < 0; }
        bool operator<=(const TCHAR *other) const { return compare(other) <= 0; }
        bool operator>(const TCHAR *other) const { return compare(other) > 0; }
        bool operator>=(const TCHAR *other) const { return compare(other) >= 0; }

        // 連結演算子
        String &operator+=(const String &other)
        {
            m_string.append(other.m_string);
            return *this;
        }

        String &operator+=(const TCHAR *other)
        {
            m_string.append(other);
            return *this;
        }

        String &operator+=(TCHAR ch)
        {
            m_string.push_back(ch);
            return *this;
        }

        String operator+(const String &other) const
        {
            String result(*this);
            result += other;
            return result;
        }

        String operator+(const TCHAR *other) const
        {
            String result(*this);
            result += other;
            return result;
        }

        String operator+(TCHAR ch) const
        {
            String result(*this);
            result += ch;
            return result;
        }
    };

    // フリー関数での比較演算子（左辺がTCHAR*の場合）
    inline bool operator==(const TCHAR *lhs, const String &rhs) { return rhs == lhs; }
    inline bool operator!=(const TCHAR *lhs, const String &rhs) { return rhs != lhs; }
    inline bool operator<(const TCHAR *lhs, const String &rhs) { return rhs > lhs; }
    inline bool operator<=(const TCHAR *lhs, const String &rhs) { return rhs >= lhs; }
    inline bool operator>(const TCHAR *lhs, const String &rhs) { return rhs < lhs; }
    inline bool operator>=(const TCHAR *lhs, const String &rhs) { return rhs <= lhs; } // フリー関数での文字列結合（左辺がTCHAR*の場合）
    inline String operator+(const TCHAR *lhs, const String &rhs)
    {
        String result(lhs);
        result += rhs;
        return result;
    }

    // フリー関数での文字列結合（左辺がTCHARの場合）
    inline String operator+(TCHAR lhs, const String &rhs)
    {
        String result(1, lhs);
        result += rhs;
        return result;
    }

} // namespace NorvesLib::Core::Container