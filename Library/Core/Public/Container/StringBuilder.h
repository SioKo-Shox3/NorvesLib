#pragma once

#include <Windows.h>
#include <tchar.h>
#include <stdarg.h>
#include <type_traits>
#include "Allocator.h"
#include "String.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief 効率的な文字列構築のためのStringBuilderクラステンプレート
     * @tparam CharT 文字型
     * 
     * 文字列を効率的に構築するためのクラスです。
     * 文字列の結合や追加を最適化し、必要に応じてバッファを拡張します。
     */
    template<typename CharT>
    class TStringBuilder
    {
    public:
        // 型定義
        using char_type = CharT;
        using string_type = TString<CharT>;
        using size_type = std::size_t;
        using value_type = CharT;

        /**
         * @brief デフォルトコンストラクタ
         * @param initialCapacity 初期バッファ容量
         */
        explicit TStringBuilder(size_type initialCapacity = 16)
        {
            m_buffer.reserve(initialCapacity);
        }

        /**
         * @brief 初期文字列を指定するコンストラクタ
         * @param initialStr 初期文字列
         */
        explicit TStringBuilder(const string_type& initialStr)
        {
            m_buffer = initialStr;
            m_buffer.reserve(initialStr.size() * 2);
        }    private:
        // 文字列長計算（NULL終端対応）
        static size_type GetStringLength(const CharT* str)
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

    public:
        /**
         * @brief 初期文字列を指定するコンストラクタ
         * @param initialStr 初期文字列
         */
        explicit TStringBuilder(const CharT* initialStr)
        {
            if (initialStr)
            {
                m_buffer = initialStr;
                m_buffer.reserve(GetStringLength(initialStr) * 2);
            }
            else
            {
                m_buffer.reserve(16);
            }
        }

        /**
         * @brief 文字列を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Append(const string_type& str)
        {
            m_buffer.append(str);
            return *this;
        }

        /**
         * @brief 文字列を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Append(const CharT* str)
        {
            if (str)
            {
                m_buffer.append(str);
            }
            return *this;
        }

        /**
         * @brief 1文字を追加
         * @param ch 追加する文字
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Append(CharT ch)
        {
            m_buffer.push_back(ch);
            return *this;
        }        /**
         * @brief 数値を文字列として追加
         * @param value 追加する数値
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Append(int value)
        {
            CharT buffer[32];
            if constexpr (std::is_same_v<CharT, char>)
            {
                std::sprintf(buffer, "%d", value);
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                std::swprintf(buffer, 32, L"%d", value);
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                _stprintf_s(buffer, _T("%d"), value);
            }
            m_buffer.append(buffer);
            return *this;
        }

        /**
         * @brief 数値を文字列として追加
         * @param value 追加する数値
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Append(float value)
        {
            CharT buffer[32];
            if constexpr (std::is_same_v<CharT, char>)
            {
                std::sprintf(buffer, "%f", value);
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                std::swprintf(buffer, 32, L"%f", value);
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                _stprintf_s(buffer, _T("%f"), value);
            }
            m_buffer.append(buffer);
            return *this;
        }

        /**
         * @brief 新しい行を追加
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& AppendLine()
        {
            if constexpr (std::is_same_v<CharT, char>)
            {
                m_buffer.append("\r\n");
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                m_buffer.append(L"\r\n");
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                m_buffer.append(_T("\r\n"));
            }
            return *this;
        }

        /**
         * @brief 文字列と改行を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& AppendLine(const string_type& str)
        {
            m_buffer.append(str);
            AppendLine();
            return *this;
        }

        /**
         * @brief 文字列と改行を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& AppendLine(const CharT* str)
        {
            if (str)
            {
                m_buffer.append(str);
            }
            AppendLine();
            return *this;
        }        /**
         * @brief 書式指定文字列を追加
         * @param format 書式文字列
         * @param ... フォーマット引数
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& AppendFormat(const CharT* format, ...)
        {
            if (!format) return *this;

            va_list args;
            va_start(args, format);

            // 必要なバッファサイズを計算
            int bufferSize;
            if constexpr (std::is_same_v<CharT, char>)
            {
                bufferSize = _vscprintf(format, args) + 1;
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                bufferSize = _vscwprintf(format, args) + 1;
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                bufferSize = _vsctprintf(format, args) + 1;
            }
            else
            {
                bufferSize = 256; // デフォルトサイズ
            }
            va_end(args);

            // 十分なサイズの一時バッファを作成
            CharT* buffer = new CharT[bufferSize];
            
            // 再度引数を設定してバッファにフォーマット
            va_start(args, format);
            if constexpr (std::is_same_v<CharT, char>)
            {
                vsprintf_s(buffer, bufferSize, format, args);
            }
            else if constexpr (std::is_same_v<CharT, wchar_t>)
            {
                vswprintf_s(buffer, bufferSize, format, args);
            }
            else if constexpr (std::is_same_v<CharT, TCHAR>)
            {
                _vstprintf_s(buffer, bufferSize, format, args);
            }
            va_end(args);

            // 結果をStringBuilderに追加
            m_buffer.append(buffer);
            
            delete[] buffer;
            return *this;
        }/**
         * @brief 現在の内容をクリア
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Clear()
        {
            m_buffer.clear();
            return *this;
        }

        /**
         * @brief 指定された文字列を挿入
         * @param index 挿入位置
         * @param str 挿入する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Insert(size_type index, const string_type& str)
        {
            if (index <= m_buffer.size())
            {
                m_buffer.insert(index, str);
            }
            return *this;
        }

        /**
         * @brief 指定された文字列を挿入
         * @param index 挿入位置
         * @param str 挿入する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Insert(size_type index, const CharT* str)
        {
            if (str && index <= m_buffer.size())
            {
                m_buffer.insert(index, str);
            }
            return *this;
        }

        /**
         * @brief 指定された範囲の文字を削除
         * @param startIndex 削除開始位置
         * @param length 削除する文字数
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Remove(size_type startIndex, size_type length)
        {
            if (startIndex < m_buffer.size())
            {
                m_buffer.erase(startIndex, length);
            }
            return *this;
        }        /**
         * @brief 文字列置換
         * @param oldStr 置換対象の文字列
         * @param newStr 置換後の文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& Replace(const string_type& oldStr, const string_type& newStr)
        {
            size_type pos = 0;
            while ((pos = m_buffer.find(oldStr, pos)) != string_type::npos)
            {
                m_buffer.replace(pos, oldStr.size(), newStr);
                pos += newStr.size();
            }
            return *this;
        }/**
         * @brief 現在のバッファ容量を変更
         * @param newCapacity 新しいバッファ容量
         * @return このインスタンス（チェーン呼び出し用）
         */
        TStringBuilder& EnsureCapacity(size_type newCapacity)
        {
            if (m_buffer.capacity() < newCapacity)
            {
                m_buffer.reserve(newCapacity);
            }
            return *this;
        }

        /**
         * @brief 現在のStringBuilderの内容をStringとして取得
         * @return 構築された文字列
         */
        string_type ToString() const
        {
            return m_buffer;
        }

        /**
         * @brief 現在の文字列長を取得
         * @return 構築された文字列の長さ
         */
        size_type Length() const
        {
            return m_buffer.length();
        }

        /**
         * @brief 現在のバッファ容量を取得
         * @return バッファ容量
         */
        size_type Capacity() const
        {
            return m_buffer.capacity();
        }

    private:
        string_type m_buffer;  // 内部バッファ
    };

    // 型エイリアス
    using StringBuilder = TStringBuilder<TCHAR>;
    using AnsiStringBuilder = TStringBuilder<char>;
    using WideStringBuilder = TStringBuilder<wchar_t>;

} // namespace NorvesLib::Core::Container