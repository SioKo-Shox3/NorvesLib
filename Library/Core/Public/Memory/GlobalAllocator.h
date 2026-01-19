#pragma once

#include "IAllocator.h"
#include "SizeClassAllocator.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Memory
{
    // 前方宣言
    class ThreadLocalCache;

    /**
     * @brief グローバルアロケータ
     *
     * システム全体で共有されるメモリアロケータ。
     * スレッドローカルキャッシュと連携して動作し、
     * 高いスケーラビリティを実現します。
     *
     * 内部的にはサイズクラスアロケータを使用し、
     * 中央フリーリスト（CentralFreeList）を通じて
     * スレッドローカルキャッシュとバッチ転送を行います。
     */
    class GlobalAllocator : public IThreadSafeAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         */
        GlobalAllocator();

        /**
         * @brief デストラクタ
         */
        ~GlobalAllocator() override;

        // コピー・ムーブ禁止
        GlobalAllocator(const GlobalAllocator &) = delete;
        GlobalAllocator &operator=(const GlobalAllocator &) = delete;
        GlobalAllocator(GlobalAllocator &&) = delete;
        GlobalAllocator &operator=(GlobalAllocator &&) = delete;

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
         * @brief 管理しているメモリの合計サイズを取得
         * @return 管理しているメモリサイズ（バイト単位）
         */
        size_t GetTotalSize() const override;

        /**
         * @brief アロケータの種類を取得
         * @return AllocatorType::Global
         */
        AllocatorType GetType() const override;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        bool OwnsMemory(const void *ptr) const override;

        /**
         * @brief 割り当てられたブロックのサイズを取得
         * @param ptr 確認するメモリブロックポインタ
         * @return ブロックのサイズ（バイト単位）
         */
        size_t GetBlockSize(const void *ptr) const override;

        // ===========================================
        // スレッドローカルキャッシュ向けAPI
        // ===========================================

        /**
         * @brief 中央フリーリストからオブジェクトをバッチ取得
         * @param classIndex サイズクラスインデックス
         * @param objects 取得したオブジェクトを格納する配列
         * @param maxCount 最大取得数
         * @return 実際に取得したオブジェクト数
         */
        size_t FetchFromCentral(int classIndex, void **objects, size_t maxCount);

        /**
         * @brief 中央フリーリストにオブジェクトをバッチ返却
         * @param classIndex サイズクラスインデックス
         * @param objects 返却するオブジェクトの配列
         * @param count 返却するオブジェクト数
         */
        void ReturnToCentral(int classIndex, void **objects, size_t count);

        /**
         * @brief 指定されたサイズクラスのサイズを取得
         * @param classIndex サイズクラスインデックス
         * @return サイズクラスのサイズ（バイト単位）
         */
        static size_t GetSizeClassSize(int classIndex);

        /**
         * @brief サイズに対応するサイズクラスインデックスを取得
         * @param size 要求サイズ
         * @return サイズクラスインデックス（-1は大きなオブジェクト）
         */
        static int GetSizeClassIndex(size_t size);

        // ===========================================
        // 統計情報
        // ===========================================

        /**
         * @brief 中央フリーリストのオブジェクト数を取得
         * @param classIndex サイズクラスインデックス
         * @return オブジェクト数
         */
        size_t GetCentralFreeCount(int classIndex) const;

        /**
         * @brief 合計確保回数を取得
         * @return 確保回数
         */
        uint64_t GetTotalAllocationCount() const;

        /**
         * @brief 合計解放回数を取得
         * @return 解放回数
         */
        uint64_t GetTotalDeallocationCount() const;

    private:
        // フリーリストノード
        struct FreeNode
        {
            FreeNode *next;
        };

        // 中央フリーリスト（サイズクラスごと）
        struct CentralFreeList
        {
            FreeNode *freeList;  ///< フリーリストの先頭
            size_t count;        ///< リスト内のオブジェクト数
            Thread::Mutex mutex; ///< このリスト用のミューテックス
        };

        CentralFreeList m_centralLists[Config::NumSizeClasses]; ///< 中央フリーリスト

        Core::Container::TUniquePtr<SizeClassAllocator<true>> m_sizeClassAllocator; ///< サイズクラスアロケータ

        Thread::Atomic<uint64_t> m_totalAllocations;   ///< 合計確保回数
        Thread::Atomic<uint64_t> m_totalDeallocations; ///< 合計解放回数
    };

} // namespace NorvesLib::Memory
