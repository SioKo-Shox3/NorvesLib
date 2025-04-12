#pragma once

#include <string>
#include <string_view>
#include "Allocator.h"

namespace NorvesLib::Core::Container
{
    /**
     * @brief GlobalMemoryAllocatorを使用する文字列クラス
     * std::stringのラッパーとして機能します
     */
    class String : public std::basic_string<char, std::char_traits<char>, Allocator<char>>
    {
    public:
        using Base = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
        
        // 継承コンストラクタを使用して基底クラスのコンストラクタをすべて継承
        using Base::basic_string;

        // デフォルトコンストラクタ
        String() = default;

        // コピーコンストラクタ
        String(const String&) = default;

        // ムーブコンストラクタ
        String(String&&) = default;

        // C文字列からの構築
        String(const char* str) : Base(str) {}

        // std::stringからの変換コンストラクタ
        explicit String(const std::string& other) : Base(other.begin(), other.end()) {}

        // std::stringからの変換ムーブコンストラクタ
        explicit String(std::string&& other) 
            : Base(std::make_move_iterator(other.begin()), std::make_move_iterator(other.end()))
        {
            other.clear();
        }

        // string_viewからの構築
        explicit String(std::string_view view) : Base(view.data(), view.size()) {}

        // std::stringへの変換演算子
        operator std::string() const
        {
            return std::string(this->data(), this->size());
        }

        // string_viewへの変換演算子
        operator std::string_view() const
        {
            return std::string_view(this->data(), this->size());
        }

        // 代入演算子
        String& operator=(const String&) = default;
        String& operator=(String&&) = default;
        String& operator=(const char* str) { Base::operator=(str); return *this; }
        String& operator=(const std::string& str) { Base::assign(str.begin(), str.end()); return *this; }
        String& operator=(std::string&& str)
        {
            Base::assign(std::make_move_iterator(str.begin()), std::make_move_iterator(str.end()));
            str.clear();
            return *this;
        }
        String& operator=(std::string_view view) { Base::assign(view.data(), view.size()); return *this; }
    };
}