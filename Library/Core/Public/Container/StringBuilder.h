#pragma once

#include <string>
#include <Windows.h>
#include <tchar.h>
#include <stdarg.h>
#include "Allocator.h"
#include "String.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief 効率的な文字列構築のためのStringBuilderクラス
     * 
     * TCHARベースの文字列を効率的に構築するためのクラスです。
     * 文字列の結合や追加を最適化し、必要に応じてバッファを拡張します。
     */
    class StringBuilder
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         * @param initialCapacity 初期バッファ容量
         */
        explicit StringBuilder(size_t initialCapacity = 16)
        {
            m_buffer.reserve(initialCapacity);
        }

        /**
         * @brief 初期文字列を指定するコンストラクタ
         * @param initialStr 初期文字列
         */
        explicit StringBuilder(const String& initialStr)
        {
            m_buffer = initialStr;
            m_buffer.reserve(initialStr.size() * 2);
        }

        /**
         * @brief 初期文字列を指定するコンストラクタ
         * @param initialStr 初期文字列
         */
        explicit StringBuilder(const TCHAR* initialStr)
        {
            if (initialStr)
            {
                m_buffer = initialStr;
                m_buffer.reserve(_tcslen(initialStr) * 2);
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
        StringBuilder& Append(const String& str)
        {
            m_buffer.append(str);
            return *this;
        }

        /**
         * @brief 文字列を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& Append(const TCHAR* str)
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
        StringBuilder& Append(TCHAR ch)
        {
            m_buffer.push_back(ch);
            return *this;
        }

        /**
         * @brief 数値を文字列として追加
         * @param value 追加する数値
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& Append(int value)
        {
            TCHAR buffer[32];
            _stprintf_s(buffer, _T("%d"), value);
            m_buffer.append(buffer);
            return *this;
        }

        /**
         * @brief 数値を文字列として追加
         * @param value 追加する数値
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& Append(float value)
        {
            TCHAR buffer[32];
            _stprintf_s(buffer, _T("%f"), value);
            m_buffer.append(buffer);
            return *this;
        }

        /**
         * @brief 新しい行を追加
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& AppendLine()
        {
            m_buffer.append(_T("\r\n"));
            return *this;
        }

        /**
         * @brief 文字列と改行を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& AppendLine(const String& str)
        {
            m_buffer.append(str);
            m_buffer.append(_T("\r\n"));
            return *this;
        }

        /**
         * @brief 文字列と改行を追加
         * @param str 追加する文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& AppendLine(const TCHAR* str)
        {
            if (str)
            {
                m_buffer.append(str);
            }
            m_buffer.append(_T("\r\n"));
            return *this;
        }

        /**
         * @brief 書式指定文字列を追加
         * @param format 書式文字列
         * @param ... フォーマット引数
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& AppendFormat(const TCHAR* format, ...)
        {
            if (!format) return *this;

            va_list args;
            va_start(args, format);

            // 必要なバッファサイズを計算
            int bufferSize = _vsctprintf(format, args) + 1;  // +1 for null terminator
            va_end(args);

            // 十分なサイズの一時バッファを作成
            TCHAR* buffer = new TCHAR[bufferSize];
            
            // 再度引数を設定してバッファにフォーマット
            va_start(args, format);
            _vstprintf_s(buffer, bufferSize, format, args);
            va_end(args);

            // 結果をStringBuilderに追加
            m_buffer.append(buffer);
            
            delete[] buffer;
            return *this;
        }

        /**
         * @brief 現在の内容をクリア
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& Clear()
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
        StringBuilder& Insert(size_t index, const String& str)
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
        StringBuilder& Insert(size_t index, const TCHAR* str)
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
        StringBuilder& Remove(size_t startIndex, size_t length)
        {
            if (startIndex < m_buffer.size())
            {
                m_buffer.erase(startIndex, length);
            }
            return *this;
        }

        /**
         * @brief 文字列置換
         * @param oldStr 置換対象の文字列
         * @param newStr 置換後の文字列
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& Replace(const String& oldStr, const String& newStr)
        {
            size_t pos = 0;
            while ((pos = m_buffer.find(oldStr, pos)) != String::npos)
            {
                m_buffer.replace(pos, oldStr.size(), newStr);
                pos += newStr.size();
            }
            return *this;
        }

        /**
         * @brief 現在のバッファ容量を変更
         * @param newCapacity 新しいバッファ容量
         * @return このインスタンス（チェーン呼び出し用）
         */
        StringBuilder& EnsureCapacity(size_t newCapacity)
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
        String ToString() const
        {
            return m_buffer;
        }

        /**
         * @brief 現在の文字列長を取得
         * @return 構築された文字列の長さ
         */
        size_t Length() const
        {
            return m_buffer.length();
        }

        /**
         * @brief 現在のバッファ容量を取得
         * @return バッファ容量
         */
        size_t Capacity() const
        {
            return m_buffer.capacity();
        }

    private:
        String m_buffer;  // 内部バッファ
    };
}