#pragma once

#include <atomic>
#include <type_traits>

namespace NorvesLib::Thread
{

    /**
     * @brief Atomicをラップしたアトミック変数クラス
     *
     * スレッド間で安全に読み書きできる変数型を提供します。
     * @tparam T ラップする型
     */
    template <typename T>
    class Atomic
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        constexpr Atomic() noexcept(std::is_nothrow_default_constructible_v<T>) : m_Value() {}

        /**
         * @brief 値を指定したコンストラクタ
         * @param value 初期値
         */
        constexpr Atomic(T value) noexcept : m_Value(value) {}

        /**
         * @brief コピーコンストラクタ（アトミックではない）
         * @param other コピー元オブジェクト
         */
        Atomic(const Atomic &other) : m_Value(other.GetValue()) {}

        /**
         * @brief コピー代入演算子
         * @param other コピー元オブジェクト
         * @return このオブジェクトへの参照
         */
        Atomic &operator=(const Atomic &other)
        {
            SetValue(other.GetValue());
            return *this;
        }

        /**
         * @brief 変数の代入演算子
         * @param value 代入する値
         * @return このオブジェクトへの参照
         */
        Atomic &operator=(T value) noexcept
        {
            SetValue(value);
            return *this;
        }

        /**
         * @brief アトミック変数の値を読み取る
         * @param order メモリオーダー（デフォルトはseq_cst）
         * @return 現在の値
         */
        T Load(std::memory_order order = std::memory_order_seq_cst) const noexcept
        {
            return m_Value.load(order);
        }

        /**
         * @brief アトミック変数の値を読み取る（Loadのエイリアス）
         * @param order メモリオーダー（デフォルトはseq_cst）
         * @return 現在の値
         */
        T GetValue(std::memory_order order = std::memory_order_seq_cst) const noexcept
        {
            return Load(order);
        }

        /**
         * @brief アトミック変数に値を書き込む
         * @param value 書き込む値
         * @param order メモリオーダー（デフォルトはseq_cst）
         */
        void Store(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            m_Value.store(value, order);
        }

        /**
         * @brief アトミック変数に値を書き込む（Storeのエイリアス）
         * @param value 書き込む値
         * @param order メモリオーダー（デフォルトはseq_cst）
         */
        void SetValue(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            Store(value, order);
        }

        /**
         * @brief アトミックに値を交換し、交換前の値を返す
         * @param value 新しい値
         * @param order メモリオーダー（デフォルトはseq_cst）
         * @return 交換前の値
         */
        T Exchange(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.exchange(value, order);
        }

        /**
         * @brief 現在の値が期待値と等しい場合に新しい値に置き換える
         * @param expected 期待値（不一致の場合は更新される）
         * @param value 新しい値
         * @param successOrder 成功時のメモリオーダー
         * @param failureOrder 失敗時のメモリオーダー
         * @return 交換が成功した場合はtrue
         */
        bool CompareExchangeWeak(T &expected, T value,
                                 std::memory_order successOrder = std::memory_order_seq_cst,
                                 std::memory_order failureOrder = std::memory_order_seq_cst) noexcept
        {
            return m_Value.compare_exchange_weak(expected, value, successOrder, failureOrder);
        }

        /**
         * @brief 現在の値が期待値と等しい場合に新しい値に置き換える（強い保証）
         * @param expected 期待値（不一致の場合は更新される）
         * @param value 新しい値
         * @param successOrder 成功時のメモリオーダー
         * @param failureOrder 失敗時のメモリオーダー
         * @return 交換が成功した場合はtrue
         */
        bool CompareExchangeStrong(T &expected, T value,
                                   std::memory_order successOrder = std::memory_order_seq_cst,
                                   std::memory_order failureOrder = std::memory_order_seq_cst) noexcept
        {
            return m_Value.compare_exchange_strong(expected, value, successOrder, failureOrder);
        }

        /**
         * @brief 暗黙の型変換演算子
         * @return 現在の値
         */
        operator T() const noexcept
        {
            return GetValue();
        }

        /**
         * @brief 加算演算子
         * @param value 加算する値
         * @return 加算後の値
         */
        T operator+=(T value) noexcept
        {
            return m_Value += value;
        }

        /**
         * @brief 減算演算子
         * @param value 減算する値
         * @return 減算後の値
         */
        T operator-=(T value) noexcept
        {
            return m_Value -= value;
        }

        /**
         * @brief インクリメント演算子（前置）
         * @return インクリメント後の値
         */
        T operator++() noexcept
        {
            return ++m_Value;
        }

        /**
         * @brief インクリメント演算子（後置）
         * @return インクリメント前の値
         */
        T operator++(int) noexcept
        {
            return m_Value++;
        }

        /**
         * @brief デクリメント演算子（前置）
         * @return デクリメント後の値
         */
        T operator--() noexcept
        {
            return --m_Value;
        }

        /**
         * @brief デクリメント演算子（後置）
         * @return デクリメント前の値
         */
        T operator--(int) noexcept
        {
            return m_Value--;
        }

        /**
         * @brief アトミックな加算
         * @param value 加算する値
         * @param order メモリオーダー
         * @return 加算前の値
         */
        T FetchAdd(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.fetch_add(value, order);
        }

        /**
         * @brief アトミックな減算
         * @param value 減算する値
         * @param order メモリオーダー
         * @return 減算前の値
         */
        T FetchSub(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.fetch_sub(value, order);
        }

        /**
         * @brief アトミックなAND演算
         * @param value AND演算する値
         * @param order メモリオーダー
         * @return 演算前の値
         */
        T FetchAnd(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.fetch_and(value, order);
        }

        /**
         * @brief アトミックなOR演算
         * @param value OR演算する値
         * @param order メモリオーダー
         * @return 演算前の値
         */
        T FetchOr(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.fetch_or(value, order);
        }

        /**
         * @brief アトミックなXOR演算
         * @param value XOR演算する値
         * @param order メモリオーダー
         * @return 演算前の値
         */
        T FetchXor(T value, std::memory_order order = std::memory_order_seq_cst) noexcept
        {
            return m_Value.fetch_xor(value, order);
        }

    private:
        std::atomic<T> m_Value; ///< ラップするstd::atomic
    };

} // namespace NorvesLib::Thread