#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/UnorderedMap.h"
#include "Container/Containers.h"
#include "Thread/Mutex.h"
#include <functional>

namespace NorvesLib::Core
{
    /**
     * @brief 文字列からハッシュ値を生成し、軽量に比較変更ができる文字列ID
     */
    class Identity
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         * 空のIdentityを作成します
         */
        Identity() = default;

        /**
         * @brief C文字列からIdentityを作成
         * @param str 文字列
         */
        Identity(const char *str);

        /**
         * @brief Container::Stringからのコンストラクタ
         * @param str 文字列
         */
        explicit Identity(const Container::String &str);

        /**
         * @brief Container::StringViewからのコンストラクタ
         * @param view 文字列ビュー
         */
        explicit Identity(Container::StringView view);

        /**
         * @brief コピーコンストラクタ
         * @param other コピー元オブジェクト
         */
        Identity(const Identity &other) = default;

        /**
         * @brief ムーブコンストラクタ
         * @param other ムーブ元オブジェクト
         */
        Identity(Identity &&other) noexcept = default;

        /**
         * @brief コピー代入演算子
         * @param other コピー元オブジェクト
         * @return *this
         */
        Identity &operator=(const Identity &other) = default;

        /**
         * @brief ムーブ代入演算子
         * @param other ムーブ元オブジェクト
         * @return *this
         */
        Identity &operator=(Identity &&other) noexcept = default;

        /**
         * @brief ハッシュ値を取得
         * @return ハッシュ値
         */
        uint64_t GetHash() const { return m_Hash; }

        /**
         * @brief 文字列ビューを取得
         * @return 文字列ビュー
         */
        Container::StringView GetView() const { return m_StringView; }

        /**
         * @brief 文字列を取得
         * @return 文字列
         */
        Container::String ToString() const;

        /**
         * @brief Identityが有効かチェック
         * @return 有効な場合true
         */
        bool IsValid() const { return m_Hash != 0 && !m_StringView.empty(); }

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しい場合true
         */
        bool operator==(const Identity &other) const { return m_Hash == other.m_Hash; }

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しくない場合true
         */
        bool operator!=(const Identity &other) const { return m_Hash != other.m_Hash; }

        /**
         * @brief std::unordered_map等で使用するためのハッシュ関数オブジェクト
         */
        struct Hasher
        {
            std::size_t operator()(const Identity &id) const
            {
                return static_cast<std::size_t>(id.GetHash());
            }
        };

    private:
        /**
         * @brief ハッシュ値とStringViewを直接設定するコンストラクタ
         * @param hash ハッシュ値
         * @param view 文字列ビュー
         */
        Identity(uint64_t hash, Container::StringView view) : m_Hash(hash), m_StringView(view) {}

        // IdentityPoolをフレンドクラスとして定義
        friend class IdentityPool;

        // ハッシュ値
        uint64_t m_Hash = 0;

        // 文字列ビュー
        Container::StringView m_StringView;
    };

    /**
     * @brief すべてのIdentityを管理するシングルトンクラス
     */
    class IdentityPool
    {
    public:
        /**
         * @brief シングルトンインスタンスを取得
         * @return IdentityPoolのインスタンス
         */
        static IdentityPool &Get();

        /**
         * @brief 文字列からIdentityを作成または取得
         * @param str 文字列
         * @return Identity
         */
        Identity CreateIdentity(const char *str);

        /**
         * @brief 文字列からIdentityを作成または取得
         * @param str 文字列
         * @return Identity
         */
        Identity CreateIdentity(const Container::String &str);

        /**
         * @brief 文字列ビューからIdentityを作成または取得
         * @param view 文字列ビュー
         * @return Identity
         */
        Identity CreateIdentity(Container::StringView view);

        /**
         * @brief ハッシュ値からIdentityを取得
         * @param hash ハッシュ値
         * @return Identity。存在しない場合は無効なIdentity
         */
        Identity GetIdentity(uint64_t hash) const;

        /**
         * @brief ハッシュ値から文字列を取得
         * @param hash ハッシュ値
         * @return 文字列ビュー。存在しない場合は空のビュー
         */
        Container::StringView GetStringFromHash(uint64_t hash) const;

        /**
         * @brief 登録されているIdentity数を取得
         * @return Identity数
         */
        size_t GetIdentityCount() const;

        /**
         * @brief すべての文字列を取得
         * @return 登録されている文字列の配列
         */
        Container::VariableArray<Container::StringView> GetAllStrings() const;

    private:
        /**
         * @brief コンストラクタ（プライベート）
         */
        IdentityPool() = default;

        /**
         * @brief デストラクタ（プライベート）
         */
        ~IdentityPool() = default;

        /**
         * @brief ハッシュ値を計算
         * @param view 文字列ビュー
         * @return ハッシュ値
         */
        uint64_t CalculateHash(Container::StringView view) const;

        // ハッシュ値から文字列へのマッピング
        mutable Thread::Mutex m_Mutex;
        Container::UnorderedMap<uint64_t, Container::String> m_StringMap;
    };

} // namespace NorvesLib::Core
