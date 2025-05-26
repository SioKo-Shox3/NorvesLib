#pragma once

#include <cstddef>
#include "Container/PointerTypes.h"

namespace NorvesLib::Memory
{
    /**
     * メモリアロケーターのインターフェースクラス
     * カスタムメモリ割り当て戦略を実装するための基底クラス
     */
    class IAllocator
    {
    public:
        virtual ~IAllocator() = default;

        /**
         * メモリの割り当て
         * @param size 割り当てるサイズ（バイト単位）
         * @param alignment アライメント要件（バイト単位、デフォルトは16バイト）
         * @return 割り当てられたメモリブロックへのポインタ
         */
        virtual void *Allocate(size_t size, size_t alignment = 16) = 0;

        /**
         * メモリの解放
         * @param ptr 解放するメモリブロックへのポインタ
         */
        virtual void Deallocate(void *ptr) = 0;

        /**
         * 割り当てられたメモリの合計サイズを取得
         * @return 割り当てられたメモリの合計サイズ（バイト単位）
         */
        virtual size_t GetAllocatedSize() const = 0;
    };

    // アロケータのスマートポインタ型定義
    using AllocatorPtr = Core::Container::TSharedPtr<IAllocator>;
}