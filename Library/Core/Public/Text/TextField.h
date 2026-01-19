#pragma once

#include "Container/String.h"
#include "Container/StringView.h"

namespace NorvesLib::Core
{
    /**
     * @brief インスタンス化された後は変更できない固定文字列を扱うクラス
     */
    class TextField
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        TextField() = default;

        /**
         * @brief C文字列からの初期化
         * @param str 初期化する文字列
         */
        TextField(const char* str);

        /**
         * @brief Container::Stringからの初期化
         * @param str 初期化する文字列
         */
        explicit TextField(const Container::String& str);

        /**
         * @brief Container::StringViewからの初期化
         * @param view 初期化する文字列ビュー
         */
        explicit TextField(Container::StringView view);

        /**
         * @brief コピーコンストラクタ
         * @param other コピー元オブジェクト
         */
        TextField(const TextField& other);

        /**
         * @brief ムーブコンストラクタ
         * @param other ムーブ元オブジェクト
         */
        TextField(TextField&& other) noexcept;

        /**
         * @brief コピー代入演算子
         * @param other コピー元オブジェクト
         * @return *this
         */
        TextField& operator=(const TextField& other);

        /**
         * @brief ムーブ代入演算子
         * @param other ムーブ元オブジェクト
         * @return *this
         */
        TextField& operator=(TextField&& other) noexcept;

        /**
         * @brief デストラクタ
         */
        ~TextField();

        /**
         * @brief 文字列の長さを取得
         * @return 文字列の長さ
         */
        size_t Length() const;

        /**
         * @brief 文字列が空かどうか
         * @return 文字列が空の場合true
         */
        bool IsEmpty() const;

        /**
         * @brief C文字列を取得
         * @return null終端C文字列
         */
        const char* CStr() const;

        /**
         * @brief Container::StringViewを取得
         * @return 文字列ビュー
         */
        Container::StringView View() const;

        /**
         * @brief Container::Stringを取得
         * @return Container::String
         */
        Container::String ToString() const;

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しい場合true
         */
        bool operator==(const TextField& other) const;

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しくない場合true
         */
        bool operator!=(const TextField& other) const;

    private:
        /**
         * @brief 文字列データ
         */
        Container::String m_Data;
    };

} // namespace NorvesLib::Core
