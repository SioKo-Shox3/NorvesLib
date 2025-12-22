#pragma once

#include <cstring>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <initializer_list>
#include <type_traits>
#include <Windows.h>
#include <tchar.h>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    // 前方宣言
    template<typename CharT>
    class TStringView;
    /**
     * @brief 動的文字列クラステンプレート
     * @tparam CharT 文字型
     * 
     * 独自のメモリ管理を行う文字列クラステンプレートです。
     * STLからの継承は行わず、全てのメモリ管理をカスタムアロケータで実装します。
     */
    template<typename CharT>
    class TString
    {    public:
        // 型定義
        using value_type = CharT;
        using pointer = CharT*;
        using const_pointer = const CharT*;
        using reference = CharT&;
        using const_reference = const CharT&;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using allocator_type = Allocator<CharT>;

        // 特殊値
        static constexpr size_type npos = static_cast<size_type>(-1);    private:
        pointer m_data;
        size_type m_size;
        size_type m_capacity;
        Allocator<CharT> m_allocator;

        // 文字列長計算（NULL終端対応）
        static size_type StringLength(const_pointer str)
        {
            if constexpr (std::is_same_v<CharT, char>)
            {
                return str ? std::strlen(str) : 0;
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                return str ? std::wcslen(str) : 0;
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                return str ? _tcslen(str) : 0;
            }
            else
            {
                // 汎用実装
                if (!str) return 0;
                size_type len = 0;
                while (str[len] != CharT{}) ++len;
                return len;
            }
        }

        // 文字列比較
        static int StringCompare(const_pointer lhs, const_pointer rhs, size_type count)
        {
            if constexpr (std::is_same_v<CharT, char>)
            {
                return std::strncmp(lhs, rhs, count);
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                return std::wcsncmp(lhs, rhs, count);
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                return _tcsncmp(lhs, rhs, count);
            }
            else
            {
                // 汎用実装
                for (size_type i = 0; i < count; ++i)
                {
                    if (lhs[i] < rhs[i]) return -1;
                    if (lhs[i] > rhs[i]) return 1;
                    if (lhs[i] == CharT{}) return 0;
                }
                return 0;
            }
        }        // 文字列コピー
        static void StringCopy(pointer dest, const_pointer src, size_type count)
        {
            if constexpr (std::is_same_v<CharT, char>)
            {
                strncpy_s(dest, count + 1, src, count);
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                wcsncpy_s(dest, count + 1, src, count);
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                _tcsncpy_s(dest, count + 1, src, count);
            }
            else
            {
                // 汎用実装
                for (size_type i = 0; i < count; ++i)
                {
                    dest[i] = src[i];
                }
            }
        }

        // 容量拡張
        void Reserve(size_type newCapacity)
        {
            if (newCapacity <= m_capacity)
                return;

            // 新しいバッファを確保
            pointer newData = m_allocator.allocate(newCapacity + 1); // NULL終端用の+1

            // 既存データをコピー
            if (m_data && m_size > 0)
            {
                StringCopy(newData, m_data, m_size);
            }

            // NULL終端
            newData[m_size] = CharT{};

            // 古いバッファを解放
            if (m_data)
            {
                m_allocator.deallocate(m_data, m_capacity + 1);
            }

            m_data = newData;
            m_capacity = newCapacity;
        }

        // 容量成長計算
        size_type CalculateGrowth(size_type newSize) const
        {
            const size_type maxSize = max_size();
            const size_type oldCapacity = m_capacity;

            if (oldCapacity > maxSize - oldCapacity / 2)
            {
                return maxSize; // オーバーフローを避ける
            }

            const size_type newCapacity = oldCapacity + oldCapacity / 2;
            if (newCapacity < newSize)
            {
                return newSize;
            }

            return newCapacity;
        }

    public:        // デフォルトコンストラクタ
        TString()
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
#ifdef _WIN32
            char debugMsg[256];
            sprintf_s(debugMsg, "TString DEFAULT CTOR: this=%p\n", this);
            OutputDebugStringA(debugMsg);
#endif
        }

        // コピーコンストラクタ
        TString(const TString& other)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
#ifdef _WIN32
            char debugMsg[256];
            sprintf_s(debugMsg, "TString COPY CTOR: this=%p, other=%p, other.m_data=%p\n", this, &other, other.m_data);
            OutputDebugStringA(debugMsg);
#endif
            if (other.m_size > 0)
            {
                Reserve(other.m_size);
                StringCopy(m_data, other.m_data, other.m_size);
                m_size = other.m_size;
                m_data[m_size] = CharT{};
            }
        }

        // ムーブコンストラクタ
        TString(TString&& other) noexcept
            : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity), m_allocator(std::move(other.m_allocator))
        {
#ifdef _WIN32
            char debugMsg[256];
            sprintf_s(debugMsg, "TString MOVE CTOR: this=%p, other=%p, data=%p\n", this, &other, m_data);
            OutputDebugStringA(debugMsg);
#endif
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }

        // C文字列からのコンストラクタ
        TString(const_pointer str)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
            if (str)
            {
                const size_type len = StringLength(str);
                if (len > 0)
                {
                    Reserve(len);
                    StringCopy(m_data, str, len);
                    m_size = len;
                    m_data[m_size] = CharT{};
                }
            }
        }

        // 文字数と文字を指定するコンストラクタ
        TString(size_type count, CharT ch)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
            if (count > 0)
            {
                Reserve(count);
                std::fill_n(m_data, count, ch);
                m_size = count;
                m_data[m_size] = CharT{};
            }
        }        // イニシャライザリストからのコンストラクタ
        TString(std::initializer_list<CharT> ilist)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
            if (ilist.size() > 0)
            {
                Reserve(ilist.size());
                std::copy(ilist.begin(), ilist.end(), m_data);
                m_size = ilist.size();
                m_data[m_size] = CharT{};
            }
        }

        // std::stringからの変換コンストラクタ（CharT == charの場合のみ）
        template<typename T = CharT>
        TString(const std::basic_string<T>& str, 
                typename std::enable_if_t<std::is_same_v<T, CharT>>* = nullptr)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
            if (!str.empty())
            {
                Reserve(str.size());
                StringCopy(m_data, str.c_str(), str.size());
                m_size = str.size();
                m_data[m_size] = CharT{};
            }
        }

        // StringViewからの変換コンストラクタ
        TString(const TStringView<CharT>& sv)
            : m_data(nullptr), m_size(0), m_capacity(0), m_allocator()
        {
            if (!sv.empty())
            {
                Reserve(sv.size());
                StringCopy(m_data, sv.data(), sv.size());
                m_size = sv.size();
                m_data[m_size] = CharT{};
            }
        }        // デストラクタ
        ~TString()
        {
            // デバッグトレース
            std::printf("[TString::~TString] Destructor called this=%p, m_data=%p, m_size=%zu, m_capacity=%zu\n", 
                       this, m_data, m_size, m_capacity);
            if (m_data)
            {
                std::printf("[TString::~TString] Deallocating memory for %p\n", m_data);
                m_allocator.deallocate(m_data, m_capacity + 1);
                std::printf("[TString::~TString] Memory deallocated successfully for %p\n", m_data);
            }
            else
            {
                std::printf("[TString::~TString] No memory to deallocate (m_data is null)\n");
            }
        }

        // 代入演算子
        TString& operator=(const TString& other)
        {
            if (this != &other)
            {
                clear();
                if (other.m_size > 0)
                {
                    Reserve(other.m_size);
                    StringCopy(m_data, other.m_data, other.m_size);
                    m_size = other.m_size;
                    m_data[m_size] = CharT{};
                }
            }
            return *this;
        }        // ムーブ代入演算子
        TString& operator=(TString&& other) noexcept
        {
            std::printf("[TString::operator=(move)] Called this=%p (data=%p), other=%p (data=%p)\n", 
                       this, m_data, &other, other.m_data);
            if (this != &other)
            {
                if (m_data)
                {
                    std::printf("[TString::operator=(move)] Deallocating current data %p\n", m_data);
                    m_allocator.deallocate(m_data, m_capacity + 1);
                }

                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                m_allocator = std::move(other.m_allocator);

                std::printf("[TString::operator=(move)] Moving data %p from other=%p to this=%p\n", 
                           m_data, &other, this);
                
                other.m_data = nullptr;
                other.m_size = 0;
                other.m_capacity = 0;
                
                std::printf("[TString::operator=(move)] Cleared other=%p (data now=%p)\n", 
                           &other, other.m_data);
            }
            return *this;
        }

        // C文字列からの代入
        TString& operator=(const_pointer str)
        {
            clear();
            if (str)
            {
                const size_type len = StringLength(str);
                if (len > 0)
                {
                    Reserve(len);
                    StringCopy(m_data, str, len);
                    m_size = len;
                    m_data[m_size] = CharT{};
                }
            }
            return *this;
        }

        // イテレータ
        iterator begin() noexcept { return m_data; }
        const_iterator begin() const noexcept { return m_data; }
        const_iterator cbegin() const noexcept { return m_data; }
        
        iterator end() noexcept { return m_data + m_size; }
        const_iterator end() const noexcept { return m_data + m_size; }
        const_iterator cend() const noexcept { return m_data + m_size; }

        // 要素アクセス
        reference at(size_type pos)
        {
            if (pos >= m_size)
                throw std::out_of_range("TString::at: index out of range");
            return m_data[pos];
        }

        const_reference at(size_type pos) const
        {
            if (pos >= m_size)
                throw std::out_of_range("TString::at: index out of range");
            return m_data[pos];
        }

        reference operator[](size_type pos) noexcept
        {
            return m_data[pos];
        }

        const_reference operator[](size_type pos) const noexcept
        {
            return m_data[pos];
        }

        reference front() noexcept
        {
            return m_data[0];
        }

        const_reference front() const noexcept
        {
            return m_data[0];
        }

        reference back() noexcept
        {
            return m_data[m_size - 1];
        }

        const_reference back() const noexcept
        {
            return m_data[m_size - 1];
        }

        const_pointer data() const noexcept
        {
            return m_data ? m_data : reinterpret_cast<const_pointer>(&npos); // 空の場合の安全性
        }

        const_pointer c_str() const noexcept
        {
            return m_data ? m_data : reinterpret_cast<const_pointer>(&npos); // 空の場合の安全性
        }

        // 容量
        bool empty() const noexcept
        {
            return m_size == 0;
        }

        size_type size() const noexcept
        {
            return m_size;
        }

        size_type length() const noexcept
        {
            return m_size;
        }

        size_type max_size() const noexcept
        {
            return m_allocator.max_size() - 1; // NULL終端用の-1
        }

        void reserve(size_type newCapacity)
        {
            Reserve(newCapacity);
        }

        size_type capacity() const noexcept
        {
            return m_capacity;
        }

        void shrink_to_fit()
        {
            if (m_capacity > m_size)
            {
                if (m_size == 0)
                {
                    if (m_data)
                    {
                        m_allocator.deallocate(m_data, m_capacity + 1);
                        m_data = nullptr;
                        m_capacity = 0;
                    }
                }
                else
                {
                    pointer newData = m_allocator.allocate(m_size + 1);
                    StringCopy(newData, m_data, m_size);
                    newData[m_size] = CharT{};

                    m_allocator.deallocate(m_data, m_capacity + 1);
                    m_data = newData;
                    m_capacity = m_size;
                }
            }
        }        // 変更
        void clear() noexcept
        {
            std::printf("[TString::clear] Called this=%p, m_data=%p, m_size=%zu\n", this, m_data, m_size);
            m_size = 0;
            if (m_data)
            {
                m_data[0] = CharT{};
            }
            std::printf("[TString::clear] Cleared this=%p, new m_size=%zu\n", this, m_size);
        }

        TString& append(const TString& str)
        {
            return append(str.data(), str.size());
        }

        TString& append(const_pointer str)
        {
            if (str)
            {
                return append(str, StringLength(str));
            }
            return *this;
        }

        TString& append(const_pointer str, size_type count)
        {
            if (str && count > 0)
            {
                const size_type newSize = m_size + count;
                if (newSize > m_capacity)
                {
                    Reserve(CalculateGrowth(newSize));
                }

                StringCopy(m_data + m_size, str, count);
                m_size = newSize;
                m_data[m_size] = CharT{};
            }
            return *this;
        }

        TString& append(size_type count, CharT ch)
        {
            if (count > 0)
            {
                const size_type newSize = m_size + count;
                if (newSize > m_capacity)
                {
                    Reserve(CalculateGrowth(newSize));
                }

                std::fill_n(m_data + m_size, count, ch);
                m_size = newSize;
                m_data[m_size] = CharT{};
            }
            return *this;
        }

        void push_back(CharT ch)
        {
            append(1, ch);
        }

        void pop_back()
        {
            if (m_size > 0)
            {
                --m_size;
                m_data[m_size] = CharT{};
            }
        }

        // 演算子
        TString& operator+=(const TString& str)
        {
            return append(str);
        }

        TString& operator+=(const_pointer str)
        {
            return append(str);
        }

        TString& operator+=(CharT ch)
        {
            push_back(ch);
            return *this;
        }

        // 比較
        int compare(const TString& str) const noexcept
        {
            const size_type minSize = std::min(m_size, str.m_size);
            const int result = (minSize > 0) ? StringCompare(m_data, str.m_data, minSize) : 0;
            
            if (result != 0)
                return result;
            
            if (m_size < str.m_size)
                return -1;
            if (m_size > str.m_size)
                return 1;
            
            return 0;
        }

        int compare(const_pointer str) const noexcept
        {
            if (!str)
                return empty() ? 0 : 1;
            
            const size_type strLen = StringLength(str);
            const size_type minSize = std::min(m_size, strLen);
            const int result = (minSize > 0) ? StringCompare(m_data, str, minSize) : 0;
            
            if (result != 0)
                return result;
            
            if (m_size < strLen)
                return -1;
            if (m_size > strLen)
                return 1;
            
            return 0;
        }

        // 検索
        size_type find(const TString& str, size_type pos = 0) const noexcept
        {
            return find(str.data(), pos, str.size());
        }

        size_type find(const_pointer str, size_type pos = 0) const noexcept
        {
            if (!str)
                return npos;
            return find(str, pos, StringLength(str));
        }

        size_type find(const_pointer str, size_type pos, size_type count) const noexcept
        {
            if (!str || pos > m_size || count == 0)
                return npos;
            
            if (count > m_size - pos)
                return npos;

            const const_pointer dataEnd = m_data + m_size - count + 1;
            for (const_pointer it = m_data + pos; it < dataEnd; ++it)
            {
                if (StringCompare(it, str, count) == 0)
                {
                    return static_cast<size_type>(it - m_data);
                }
            }
            
            return npos;
        }

        size_type find(CharT ch, size_type pos = 0) const noexcept
        {
            if (pos >= m_size)
                return npos;

            const const_pointer result = std::find(m_data + pos, m_data + m_size, ch);
            return (result != m_data + m_size) ? static_cast<size_t>(result - m_data) : npos;
        }

        // 後方検索
        size_type FindLast(CharT ch) const noexcept
        {
            if (m_size == 0)
                return npos;

            for (size_type i = m_size; i > 0; --i)
            {
                if (m_data[i - 1] == ch)
                {
                    return i - 1;
                }
            }
            return npos;
        }

        size_type FindLast(const TString& str) const noexcept
        {
            return FindLast(str.data(), str.size());
        }

        size_type FindLast(const_pointer str) const noexcept
        {
            if (!str)
                return npos;
            return FindLast(str, StringLength(str));
        }

        size_type FindLast(const_pointer str, size_type count) const noexcept
        {
            if (!str || count == 0 || count > m_size)
                return npos;

            for (size_type i = m_size - count + 1; i > 0; --i)
            {
                if (StringCompare(m_data + i - 1, str, count) == 0)
                {
                    return i - 1;
                }
            }
            return npos;
        }

        // 部分文字列
        TString substr(size_type pos = 0, size_type count = npos) const
        {
            if (pos > m_size)
                throw std::out_of_range("TString::substr: position out of range");

            const size_type rcount = std::min(count, m_size - pos);
            if (rcount == 0)
                return TString{};

            TString result;
            result.Reserve(rcount);
            StringCopy(result.m_data, m_data + pos, rcount);
            result.m_size = rcount;
            result.m_data[result.m_size] = CharT{};
            
            return result;
        }

        // Substringエイリアス（substr用のエイリアス）
        TString Substring(size_type pos = 0, size_type count = npos) const
        {
            return substr(pos, count);
        }

        // 文字列置換
        TString& replace(size_type pos, size_type count, const TString& str)
        {
            if (pos > m_size)
                throw std::out_of_range("TString::replace: position out of range");

            const size_type actualCount = std::min(count, m_size - pos);
            const size_type newSize = m_size - actualCount + str.size();

            if (newSize > m_capacity)
            {
                Reserve(CalculateGrowth(newSize));
            }

            // 置換後の部分を後ろに移動
            if (str.size() != actualCount)
            {
                const size_type moveStart = pos + actualCount;
                const size_type moveCount = m_size - moveStart;
                if (moveCount > 0)
                {
                    // メモリ移動（重複可能）
                    std::memmove(m_data + pos + str.size(), m_data + moveStart, moveCount * sizeof(CharT));
                }
            }

            // 新しい文字列をコピー
            if (str.size() > 0)
            {
                StringCopy(m_data + pos, str.m_data, str.size());
            }

            m_size = newSize;
            m_data[m_size] = CharT{};

            return *this;
        }

        TString& replace(size_type pos, size_type count, const_pointer str)
        {
            return replace(pos, count, TString{str});
        }
    };

    // 比較演算子
    template<typename CharT>
    bool operator==(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return lhs.compare(rhs) == 0;
    }

    template<typename CharT>
    bool operator!=(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    template<typename CharT>
    bool operator<(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return lhs.compare(rhs) < 0;
    }

    template<typename CharT>
    bool operator<=(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return lhs.compare(rhs) <= 0;
    }

    template<typename CharT>
    bool operator>(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return lhs.compare(rhs) > 0;
    }

    template<typename CharT>
    bool operator>=(const TString<CharT>& lhs, const TString<CharT>& rhs) noexcept
    {
        return lhs.compare(rhs) >= 0;
    }

    // C文字列との比較
    template<typename CharT>
    bool operator==(const TString<CharT>& lhs, const CharT* rhs) noexcept
    {
        return lhs.compare(rhs) == 0;
    }

    template<typename CharT>
    bool operator==(const CharT* lhs, const TString<CharT>& rhs) noexcept
    {
        return rhs.compare(lhs) == 0;
    }

    template<typename CharT>
    bool operator!=(const TString<CharT>& lhs, const CharT* rhs) noexcept
    {
        return !(lhs == rhs);
    }

    template<typename CharT>
    bool operator!=(const CharT* lhs, const TString<CharT>& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    // 連結演算子
    template<typename CharT>
    TString<CharT> operator+(const TString<CharT>& lhs, const TString<CharT>& rhs)
    {
        TString<CharT> result = lhs;
        result += rhs;
        return result;
    }

    template<typename CharT>
    TString<CharT> operator+(const TString<CharT>& lhs, const CharT* rhs)
    {
        TString<CharT> result = lhs;
        result += rhs;
        return result;
    }

    template<typename CharT>
    TString<CharT> operator+(const CharT* lhs, const TString<CharT>& rhs)
    {
        TString<CharT> result{lhs};
        result += rhs;
        return result;
    }

    template<typename CharT>
    TString<CharT> operator+(const TString<CharT>& lhs, CharT rhs)
    {
        TString<CharT> result = lhs;
        result += rhs;
        return result;
    }

    template<typename CharT>
    TString<CharT> operator+(CharT lhs, const TString<CharT>& rhs)
    {
        TString<CharT> result{1, lhs};
        result += rhs;
        return result;
    }    // Type alias definitions
    using String = TString<TCHAR>;
    using AnsiString = TString<char>;
    using WideString = TString<wchar_t>;

} // namespace NorvesLib::Core::Container

#include "StringView.h"
