#pragma once

#include <memory>
#include <type_traits>

namespace NorvesLib::Core::Container
{
    /**
     * @brief スマートポインタのエイリアス定義
     *
     * NorvesLibプロジェクト全体で使用するスマートポインタの型エイリアス。
     * 将来的にカスタムアロケータやデバッグ機能の追加に対応可能。
     */

    // =============================================================================
    // スマートポインタ型エイリアス
    // =============================================================================

    /**
     * @brief ユニークポインタのエイリアス
     * @tparam T 管理する型
     * @tparam Deleter カスタムデリーター型（デフォルト: std::default_delete<T>）
     */
    template <typename T, typename Deleter = std::default_delete<T>>
    using TUniquePtr = std::unique_ptr<T, Deleter>;

    /**
     * @brief 共有ポインタのエイリアス
     * @tparam T 管理する型
     */
    template <typename T>
    using TSharedPtr = std::shared_ptr<T>;

    /**
     * @brief 弱参照ポインタのエイリアス
     * @tparam T 管理する型
     */
    template <typename T>
    using TWeakPtr = std::weak_ptr<T>;

    // =============================================================================
    // ファクトリー関数エイリアス
    // =============================================================================

    /**
     * @brief std::make_uniqueのエイリアス
     * @tparam T 作成する型
     * @tparam Args コンストラクタ引数の型
     * @param args コンストラクタ引数
     * @return TUniquePtr<T> 作成されたユニークポインタ
     */
    template <typename T, typename... Args>
    constexpr auto MakeUnique(Args &&...args) -> TUniquePtr<T>
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    /**
     * @brief std::make_sharedのエイリアス
     * @tparam T 作成する型
     * @tparam Args コンストラクタ引数の型
     * @param args コンストラクタ引数
     * @return TSharedPtr<T> 作成された共有ポインタ
     */
    template <typename T, typename... Args>
    constexpr auto MakeShared(Args &&...args) -> TSharedPtr<T>
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    // =============================================================================
    // コンセプトベースのユーティリティ
    // =============================================================================

    /**
     * @brief スマートポインタ型かどうかを判定するコンセプト
     */
    template <typename T>
    concept SmartPointer = requires(T t) {
        typename T::element_type;
        { t.get() } -> std::convertible_to<typename T::element_type *>;
        { t.reset() } -> std::same_as<void>;
        { static_cast<bool>(t) } -> std::same_as<bool>;
    };

    /**
     * @brief ユニークポインタ型かどうかを判定するコンセプト
     */
    template <typename T>
    concept UniquePointer = SmartPointer<T> && requires(T t) {
        { t.release() } -> std::convertible_to<typename T::element_type *>;
    };

    /**
     * @brief 共有ポインタ型かどうかを判定するコンセプト
     */
    template <typename T>
    concept SharedPointer = SmartPointer<T> && requires(T t) {
        { t.use_count() } -> std::convertible_to<long>;
        { t.unique() } -> std::convertible_to<bool>;
    };

    // =============================================================================
    // ユーティリティ関数
    // =============================================================================

    /**
     * @brief スマートポインタが有効かどうかを判定
     * @tparam PtrT スマートポインタ型
     * @param ptr 判定するポインタ
     * @return bool 有効な場合true
     */
    template <SmartPointer PtrT>
    constexpr bool IsValid(const PtrT &ptr) noexcept
    {
        return static_cast<bool>(ptr);
    }

    /**
     * @brief スマートポインタが無効かどうかを判定
     * @tparam PtrT スマートポインタ型
     * @param ptr 判定するポインタ
     * @return bool 無効な場合true
     */
    template <SmartPointer PtrT>
    constexpr bool IsNull(const PtrT &ptr) noexcept
    {
        return !static_cast<bool>(ptr);
    }

    /**
     * @brief ユニークポインタからシェアードポインタに変換
     * @tparam T 管理する型
     * @param uniquePtr 変換元のユニークポインタ
     * @return TSharedPtr<T> 変換されたシェアードポインタ
     */
    template <typename T>
    constexpr auto ToShared(TUniquePtr<T> &&uniquePtr) -> TSharedPtr<T>
    {
        return TSharedPtr<T>(uniquePtr.release());
    }

    /**
     * @brief 安全なキャスト（dynamic_cast相当）
     * @tparam TargetT キャスト先の型
     * @tparam SourceT キャスト元の型
     * @param sourcePtr キャスト元のポインタ
     * @return TSharedPtr<TargetT> キャスト結果（失敗時はnullptr）
     */
    template <typename TargetT, typename SourceT>
    constexpr auto DynamicPointerCast(const TSharedPtr<SourceT> &sourcePtr) -> TSharedPtr<TargetT>
    {
        return std::dynamic_pointer_cast<TargetT>(sourcePtr);
    }

    /**
     * @brief 静的キャスト（static_cast相当）
     * @tparam TargetT キャスト先の型
     * @tparam SourceT キャスト元の型
     * @param sourcePtr キャスト元のポインタ
     * @return TSharedPtr<TargetT> キャスト結果
     */
    template <typename TargetT, typename SourceT>
    constexpr auto StaticPointerCast(const TSharedPtr<SourceT> &sourcePtr) -> TSharedPtr<TargetT>
    {
        return std::static_pointer_cast<TargetT>(sourcePtr);
    }

    /**
     * @brief const キャスト（const_cast相当）
     * @tparam TargetT キャスト先の型
     * @tparam SourceT キャスト元の型
     * @param sourcePtr キャスト元のポインタ
     * @return TSharedPtr<TargetT> キャスト結果
     */
    template <typename TargetT, typename SourceT>
    constexpr auto ConstPointerCast(const TSharedPtr<SourceT> &sourcePtr) -> TSharedPtr<TargetT>
    {
        return std::const_pointer_cast<TargetT>(sourcePtr);
    }

} // namespace NorvesLib::Core::Container