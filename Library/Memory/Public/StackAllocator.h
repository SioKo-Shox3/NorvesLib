#pragma once

#include "IAllocator.h"
#include <cstdint>

namespace NorvesLib::Memory
{
    /**
     * @brief スタックアロケータ
     *
     * LIFO（後入れ先出し）順序でメモリを確保・解放するアロケータ。
     * 非常に高速な確保・解放が可能ですが、解放は確保の逆順で行う必要があります。
     *
     * 主な用途:
     * - 関数スコープ内での一時メモリ確保
     * - シーンローディング時の一時データ
     * - 再帰的アルゴリズムのワーキングメモリ
     *
     * @note このアロケータは非スレッドセーフです
     */
    class StackAllocator : public INonThreadSafeAllocator
    {
    public:
        /**
         * @brief マーカー型（スタック位置を記録）
         */
        using Marker = size_t;

        /**
         * @brief コンストラクタ
         * @param size スタックの合計サイズ（バイト単位）
         */
        explicit StackAllocator(size_t size = Config::DefaultStackAllocatorSize);

        /**
         * @brief デストラクタ
         */
        ~StackAllocator() override;

        // コピー禁止
        StackAllocator(const StackAllocator &) = delete;
        StackAllocator &operator=(const StackAllocator &) = delete;

        // ムーブ可能
        StackAllocator(StackAllocator &&other) noexcept;
        StackAllocator &operator=(StackAllocator &&other) noexcept;

        /**
         * @brief メモリの割り当て
         * @param size 割り当てるサイズ（バイト単位）
         * @param alignment アライメント要件
         * @return 割り当てられたメモリへのポインタ、失敗時はnullptr
         */
        void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment) override;

        /**
         * @brief メモリの解放
         *
         * @note スタックアロケータでは個別の解放はサポートされません。
         *       代わりにFreeToMarker()を使用してください。
         * @param ptr 解放するポインタ（このアロケータでは無視されます）
         */
        void Deallocate(void *ptr) override;

        /**
         * @brief 現在のスタック位置を取得
         * @return 現在のマーカー
         */
        Marker GetMarker() const;

        /**
         * @brief 指定されたマーカー位置までスタックを巻き戻す
         * @param marker 巻き戻し先のマーカー
         */
        void FreeToMarker(Marker marker);

        /**
         * @brief スタック全体をリセット
         */
        void Reset() override;

        /**
         * @brief 割り当てられたメモリの合計サイズを取得
         * @return 割り当てられたメモリサイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

        /**
         * @brief スタックの合計サイズを取得
         * @return スタックの合計サイズ（バイト単位）
         */
        size_t GetTotalSize() const override;

        /**
         * @brief アロケータの種類を取得
         * @return AllocatorType::Stack
         */
        AllocatorType GetType() const override;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        bool OwnsMemory(const void *ptr) const override;

    private:
        void *m_memory;         ///< メモリブロックの先頭
        size_t m_totalSize;     ///< 合計サイズ
        size_t m_currentOffset; ///< 現在のオフセット
    };

    /**
     * @brief スコープベースのスタックアロケータマーカー管理
     *
     * スコープを抜ける際に自動的にスタックを巻き戻します
     */
    class ScopedStackMarker
    {
    public:
        /**
         * @brief コンストラクタ
         * @param allocator 管理するスタックアロケータ
         */
        explicit ScopedStackMarker(StackAllocator &allocator)
            : m_allocator(allocator), m_marker(allocator.GetMarker())
        {
        }

        /**
         * @brief デストラクタ（自動巻き戻し）
         */
        ~ScopedStackMarker()
        {
            m_allocator.FreeToMarker(m_marker);
        }

        // コピー・ムーブ禁止
        ScopedStackMarker(const ScopedStackMarker &) = delete;
        ScopedStackMarker &operator=(const ScopedStackMarker &) = delete;
        ScopedStackMarker(ScopedStackMarker &&) = delete;
        ScopedStackMarker &operator=(ScopedStackMarker &&) = delete;

        /**
         * @brief 保存されたマーカーを取得
         * @return マーカー
         */
        StackAllocator::Marker GetMarker() const { return m_marker; }

    private:
        StackAllocator &m_allocator;
        StackAllocator::Marker m_marker;
    };

} // namespace NorvesLib::Memory
