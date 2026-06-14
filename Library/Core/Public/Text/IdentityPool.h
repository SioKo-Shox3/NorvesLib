#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/UnorderedMap.h"
#include "Container/Containers.h"
#include "Thread/Mutex.h"
#include <functional>
#include <type_traits>

namespace NorvesLib::Core
{
    /**
     * @brief FNV-1a ハッシュの素のループ（コンパイル時／ランタイム共通）
     * @param s 文字列先頭ポインタ
     * @param n 文字数
     * @return FNV-1a ハッシュ値（0→1 の予約変換は行わない素の値）
     *
     * s[i] は char（符号付き）であり、ランタイム実装の
     * `static_cast<uint64_t>(c)` と完全に一致させるため要素型を char に固定する。
     * 定数はランタイムと同一の FNV-1a オフセット基底／素数を使う。
     */
    constexpr uint64_t Fnv1aRaw(const char *s, size_t n) noexcept
    {
        constexpr uint64_t FNV_PRIME = 1099511628211ULL;
        constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

        uint64_t h = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < n; ++i)
        {
            h ^= static_cast<uint64_t>(s[i]);
            h *= FNV_PRIME;
        }
        return h;
    }

    /**
     * @brief Identity 用ハッシュ計算（空→0、素ハッシュ0→1 の予約を一元化）
     * @param s 文字列先頭ポインタ
     * @param n 文字数
     * @return Identity のハッシュ値（空文字列は 0、それ以外は非0）
     *
     * 空文字列は無効Identity（hash==0）とし、FNV-1a が偶然0になった場合は
     * 0 が Invalid 予約のため 1 に変換する。コンパイル時／ランタイム双方が
     * この関数を共有することでハッシュ値のバイト一致を保証する。
     */
    constexpr uint64_t IdentityHash(const char *s, size_t n) noexcept
    {
        if (n == 0)
        {
            return 0;
        }
        uint64_t h = Fnv1aRaw(s, n);
        return h == 0 ? 1u : h;
    }

    /**
     * @brief 文字列からハッシュ値を生成し、軽量に比較変更ができる文字列ID
     *
     * 通常はランタイムに IdentityPool 経由で構築する。加えて、文字列リテラルから
     * コンパイル時に構築するパス（`"Foo"_id` / Identity::Literal）も提供する。
     * コンパイル時パスはハッシュをコンパイル時に計算し IdentityPool には登録しない
     * （比較／Hasher はハッシュのみ、GetView()/ToString() は保持したビューを使う）。
     * コンパイル時パスはナロービルド（TCHAR=char）専用で、static_assert で保証する。
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
        constexpr uint64_t GetHash() const { return m_Hash; }

        /**
         * @brief 文字列ビューを取得
         * @return 文字列ビュー
         */
        constexpr Container::StringView GetView() const { return m_StringView; }

        /**
         * @brief 文字列を取得
         * @return 文字列
         */
        Container::String ToString() const;

        /**
         * @brief Identityが有効かチェック
         * @return 有効な場合true
         */
        constexpr bool IsValid() const { return m_Hash != 0 && !m_StringView.empty(); }

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しい場合true
         */
        constexpr bool operator==(const Identity &other) const { return m_Hash == other.m_Hash; }

        /**
         * @brief 比較演算子
         * @param other 比較対象
         * @return 等しくない場合true
         */
        constexpr bool operator!=(const Identity &other) const { return m_Hash != other.m_Hash; }

        /**
         * @brief std::unordered_map等で使用するためのハッシュ関数オブジェクト
         */
        struct Hasher
        {
            constexpr std::size_t operator()(const Identity &id) const
            {
                return static_cast<std::size_t>(id.GetHash());
            }
        };

        /**
         * @brief 文字列リテラルからコンパイル時に Identity を構築する
         * @param s 文字列先頭ポインタ（静的記憶域のリテラルを想定）
         * @param n 文字数
         * @return コンパイル時に計算した Identity
         *
         * IdentityPool には登録しない。保持する StringView はリテラルを指すため
         * 静的記憶域を持ち、ダングリングしない。ナロービルド（TCHAR=char）専用。
         */
        static consteval Identity Literal(const char *s, size_t n)
        {
            static_assert(std::is_same_v<Container::StringView::value_type, char>,
                          "Identity::Literal (\"\"_id) requires a narrow (TCHAR=char) build");
            return Identity(IdentityHash(s, n), n != 0 ? Container::StringView(s, n) : Container::StringView());
        }

    private:
        /**
         * @brief ハッシュ値とStringViewを直接設定するコンストラクタ
         * @param hash ハッシュ値
         * @param view 文字列ビュー
         */
        constexpr Identity(uint64_t hash, Container::StringView view) : m_Hash(hash), m_StringView(view) {}

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

    /**
     * @brief 文字列リテラルからコンパイル時 Identity を生成するユーザー定義リテラル
     *
     * `"Foo"_id` でコンパイル時にハッシュを計算した Identity を得る。
     * IdentityPool には登録しない（プールの逆引きは未使用、==/Hasher はハッシュのみ、
     * GetView()/ToString() は保持したビューを使う）。`using namespace` で取り込めるよう
     * inline namespace に置く。ナロービルド（TCHAR=char）専用。
     */
    inline namespace literals
    {
        consteval Identity operator""_id(const char *s, size_t n)
        {
            return Identity::Literal(s, n);
        }
    } // inline namespace literals

} // namespace NorvesLib::Core
