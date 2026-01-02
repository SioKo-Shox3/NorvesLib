#pragma once

#include <cstddef>
#include "Container/PointerTypes.h"
#include "MemoryConfig.h"

namespace NorvesLib::Memory
{
    /**
     * @brief アロケータの種類
     */
    enum class AllocatorType : uint8_t
    {
        General,     ///< 汎用アロケータ
        Stack,       ///< スタックアロケータ
        Frame,       ///< フレームアロケータ
        Pool,        ///< プールアロケータ
        ThreadLocal, ///< スレッドローカルキャッシュ
        SizeClass,   ///< サイズクラスアロケータ
        Global       ///< グローバルアロケータ
    };

    /**
     * @brief メモリアロケーターのインターフェースクラス
     *
     * カスタムメモリ割り当て戦略を実装するための基底クラス
     */
    class IAllocator
    {
    public:
        virtual ~IAllocator() = default;

        /**
         * @brief メモリの割り当て
         * @param size 割り当てるサイズ（バイト単位）
         * @param alignment アライメント要件（バイト単位、デフォルトはプラットフォーム依存）
         * @return 割り当てられたメモリブロックへのポインタ、失敗時はnullptr
         */
        virtual void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment) = 0;

        /**
         * @brief メモリの解放
         * @param ptr 解放するメモリブロックへのポインタ
         */
        virtual void Deallocate(void *ptr) = 0;

        /**
         * @brief 割り当てられたメモリの合計サイズを取得
         * @return 割り当てられたメモリの合計サイズ（バイト単位）
         */
        virtual size_t GetAllocatedSize() const = 0;

        /**
         * @brief アロケータが管理するメモリの合計サイズを取得
         * @return 管理しているメモリの合計サイズ（バイト単位）
         */
        virtual size_t GetTotalSize() const = 0;

        /**
         * @brief 利用可能なメモリサイズを取得
         * @return 利用可能なメモリサイズ（バイト単位）
         */
        virtual size_t GetFreeSize() const
        {
            return GetTotalSize() - GetAllocatedSize();
        }

        /**
         * @brief アロケータの種類を取得
         * @return アロケータの種類
         */
        virtual AllocatorType GetType() const = 0;

        /**
         * @brief アロケータがスレッドセーフかどうかを取得
         * @return スレッドセーフな場合true
         */
        virtual bool IsThreadSafe() const = 0;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        virtual bool OwnsMemory(const void *ptr) const = 0;

        /**
         * @brief アロケータをリセット（すべてのメモリを解放）
         *
         * @note すべてのアロケータでサポートされているわけではありません
         */
        virtual void Reset()
        {
            // デフォルトでは何もしない（サポートしないアロケータ用）
        }

        /**
         * @brief 割り当てられたブロックのサイズを取得
         * @param ptr 確認するメモリブロックポインタ
         * @return ブロックのサイズ（バイト単位）、無効なポインタの場合は0
         */
        virtual size_t GetBlockSize(const void *ptr) const
        {
            (void)ptr;
            return 0; // デフォルトでは不明
        }
    };

    /**
     * @brief スレッドセーフなアロケータのインターフェース
     */
    class IThreadSafeAllocator : public IAllocator
    {
    public:
        bool IsThreadSafe() const override final
        {
            return true;
        }
    };

    /**
     * @brief 非スレッドセーフなアロケータのインターフェース
     */
    class INonThreadSafeAllocator : public IAllocator
    {
    public:
        bool IsThreadSafe() const override final
        {
            return false;
        }
    };

    // アロケータのスマートポインタ型定義
    using AllocatorPtr = Core::Container::TSharedPtr<IAllocator>;
    using AllocatorWeakPtr = Core::Container::TWeakPtr<IAllocator>;

} // namespace NorvesLib::Memory