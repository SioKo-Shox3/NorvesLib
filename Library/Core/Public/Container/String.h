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
     * Windows APIに合わせてTCHAR文字列（UNICODEではwchar_t、それ以外ではchar）を扱うための
     * std::basic_stringのラッパーとして機能します
     */
    class String : public std::basic_string<TCHAR, std::char_traits<TCHAR>, Allocator<TCHAR>>
    {
    public:
        using Base = std::basic_string<TCHAR, std::char_traits<TCHAR>, Allocator<TCHAR>>;
        
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using Base::basic_string;

        // デフォルトコンストラクタ
        String() = default;

        // コピーコンストラクタ
        String(const String&) = default;

        // ムーブコンストラクタ
        String(String&&) = default;

        // TCHAR文字列からの構築
        String(const TCHAR* str) : Base(str) {}

        // std::basic_stringからの変換コンストラクタ
        explicit String(const std::basic_string<TCHAR>& other) : Base(other.begin(), other.end()) {}

        // std::basic_stringからの変換ムーブコンストラクタ
        explicit String(std::basic_string<TCHAR>&& other) 
            : Base(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // basic_string_viewからの構築
        explicit String(std::basic_string_view<TCHAR> view) : Base(view.data(), view.size()) {}

        // std::basic_stringへの変換演算子
        operator std::basic_string<TCHAR>() const
        {
            return std::basic_string<TCHAR>(this->data(), this->size());
        }

        // basic_string_viewへの変換演算子
        operator std::basic_string_view<TCHAR>() const
        {
            return std::basic_string_view<TCHAR>(this->data(), this->size());
        }

        // 代入演算子
        String& operator=(const String&) = default;
        String& operator=(String&&) = default;
        String& operator=(const TCHAR* str) { Base::operator=(str); return *this; }
        String& operator=(const std::basic_string<TCHAR>& str) { Base::assign(str.begin(), str.end()); return *this; }
        String& operator=(std::basic_string<TCHAR>&& str)
        {
            Base::assign(std::make_move_iterator(str.begin()), std::make_move_iterator(str.end()));
            str.clear();
            return *this;
        }
        String& operator=(std::basic_string_view<TCHAR> view) { Base::assign(view.data(), view.size()); return *this; }

        // 部分文字列取得
        String Substring(size_t start, size_t length = std::basic_string<TCHAR>::npos) const
        {
            if (empty() || start >= size())
            {
                return String();
            }
            
            return String(Base::substr(start, length));
        }

        // 文字列の長さ（文字数）を取得
        size_t Length() const
        {
            return size();
        }

        // 文字列結合演算子
        String operator+(const String& other) const
        {
            String result(*this);
            result.append(other);
            return result;
        }

        String operator+(const TCHAR* other) const
        {
            String result(*this);
            result.append(other);
            return result;
        }

        // 文字列が空かどうか
        bool IsEmpty() const
        {
            return empty();
        }
        
        // 文字列の比較
        bool Equals(const String& other) const
        {
            return *this == other;
        }

        bool Equals(const TCHAR* other) const
        {
            return *this == String(other);
        }
        
        // 指定した文字列で始まるかどうか
        bool StartsWith(const String& prefix) const
        {
            if (prefix.size() > size())
                return false;
            
            return compare(0, prefix.size(), prefix) == 0;
        }
        
        // 指定した文字列で終わるかどうか
        bool EndsWith(const String& suffix) const
        {
            if (suffix.size() > size())
                return false;
            
            return compare(size() - suffix.size(), suffix.size(), suffix) == 0;
        }
        
        // 文字列を小文字に変換
        String ToLower() const
        {
            String result(*this);
            for (TCHAR& c : result)
            {
                c = static_cast<TCHAR>(_totlower(c));
            }
            return result;
        }
        
        // 文字列を大文字に変換
        String ToUpper() const
        {
            String result(*this);
            for (TCHAR& c : result)
            {
                c = static_cast<TCHAR>(_totupper(c));
            }
            return result;
        }
        
        // 指定の文字列を検索
        size_t Find(const String& subStr, size_t pos = 0) const
        {
            return Base::find(subStr, pos);
        }

        size_t Find(const TCHAR* chars, size_t pos = 0) const
        {
            return Base::find(chars, pos);
        }
        
        // 右から指定の文字列を検索
        size_t FindLast(const String& subStr) const
        {
            return Base::rfind(subStr);
        }

        size_t FindLast(const TCHAR* chars) const
        {
            return Base::rfind(chars);
        }
        
        // 文字列の置換
        String Replace(const String& from, const String& to) const
        {
            String result(*this);
            size_t pos = 0;
            while ((pos = result.find(from, pos)) != std::basic_string<TCHAR>::npos)
            {
                result.replace(pos, from.size(), to);
                pos += to.size();
            }
            return result;
        }
        
        // 文字列を指定の区切り文字で分割
        VariableArray<String> Split(TCHAR delimiter) const
        {
            VariableArray<String> tokens;
            String token;
            
            for (const TCHAR c : *this)
            {
                if (c == delimiter)
                {
                    if (!token.IsEmpty())
                    {
                        tokens.push_back(token);
                        token.clear();
                    }
                }
                else
                {
                    token += c;
                }
            }
            
            if (!token.IsEmpty())
            {
                tokens.push_back(token);
            }
            
            return tokens;
        }
    };
}