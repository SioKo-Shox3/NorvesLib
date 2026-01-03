#pragma once

#include "IAllocator.h"
#include "MemoryConfig.h"
#include "Thread/Atomic.h"
#include "Core/Public/Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Memory
{
    // 前方宣言
    class GlobalAllocator;

    /**
     * @brief スレッドローカルキャッシュ
     *
     * 各スレッドが独自に持つメモリキャッシュ。
     * 小さなオブジェクトの確保・解放をロックフリーで行うことができます。
     * キャッシュが空になるか溢れる場合は、グローバルアロケータと
     * バッチ転送を行います。
     *
     * TCMalloc/jemallocに類似したアーキテクチャです。
     */
    class ThreadLocalCache : public INonThreadSafeAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         * @param globalAllocator 親となるグローバルアロケータ
         */
        explicit ThreadLocalCache(GlobalAllocator *globalAllocator);

        /**
         * @brief デストラクタ
         */
        ~ThreadLocalCache() override;

        // コピー・ムーブ禁止
        ThreadLocalCache(const ThreadLocalCache &) = delete;
        ThreadLocalCache &operator=(const ThreadLocalCache &) = delete;
        ThreadLocalCache(ThreadLocalCache &&) = delete;
        ThreadLocalCache &operator=(ThreadLocalCache &&) = delete;

        /**
         * @brief メモリを割り当てる
         * @param size 要求サイズ（バイト単位）
         * @param alignment アライメント要件
         * @return 割り当てられたメモリへのポインタ、失敗時はnullptr
         */
        void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment) override;

        /**
         * @brief メモリを解放する
         * @param ptr 解放するポインタ
         */
        void Deallocate(void *ptr) override;

        /**
         * @brief 現在割り当てられているメモリの合計サイズを取得
         * @return 割り当てられたメモリサイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

        /**
         * @brief キャッシュが管理しているメモリの合計サイズを取得
         * @return 管理しているメモリサイズ（バイト単位）
         */
        size_t GetTotalSize() const override;

        /**
         * @brief アロケータの種類を取得
         * @return AllocatorType::ThreadLocal
         */
        AllocatorType GetType() const override;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        bool OwnsMemory(const void *ptr) const override;

        /**
         * @brief キャッシュをグローバルアロケータにフラッシュ
         *
         * キャッシュ内のすべてのオブジェクトをグローバルアロケータに戻します。
         * スレッド終了時などに呼び出します。
         */
        void FlushToGlobal();

        /**
         * @brief キャッシュ内のオブジェクト数を取得
         * @param sizeClassIndex サイズクラスインデックス
         * @return キャッシュ内のオブジェクト数
         */
        size_t GetCachedCount(int sizeClassIndex) const;

        /**
         * @brief キャッシュの合計オブジェクト数を取得
         * @return 合計オブジェクト数
         */
        size_t GetTotalCachedCount() const;

    private:
        // フリーリストノード
        struct FreeNode
        {
            FreeNode *next;
        };

        // サイズクラスごとのキャッシュ
        struct SizeClassCache
        {
            FreeNode *freeList; ///< フリーリストの先頭
            size_t count;       ///< キャッシュ内のオブジェクト数
            size_t maxCount;    ///< 最大キャッシュ数
        };

        /**
         * @brief グローバルからバッチ取得
         * @param classIndex サイズクラスインデックス
         * @return 取得成功時true
         */
        bool FetchFromGlobal(int classIndex);

        /**
         * @brief グローバルへバッチ返却
         * @param classIndex サイズクラスインデックス
         */
        void ReturnToGlobal(int classIndex);

        GlobalAllocator *m_globalAllocator;              ///< 親グローバルアロケータ
        SizeClassCache m_caches[Config::NumSizeClasses]; ///< サイズクラスごとのキャッシュ
        Thread::Atomic<size_t> m_allocatedSize;          ///< 割り当て済みサイズ
        Thread::Atomic<size_t> m_cachedSize;             ///< キャッシュサイズ
    };

} // namespace NorvesLib::Memory
