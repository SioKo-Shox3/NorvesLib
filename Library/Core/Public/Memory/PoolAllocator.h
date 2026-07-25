#pragma once

#include "IAllocator.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <limits>
#include <new>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace NorvesLib::Memory
{
    /**
     * @brief プールアロケータ（マルチスレッド対応）
     *
     * 固定サイズのオブジェクトを効率的に確保・解放するアロケータ。
     * スレッドセーフなバージョンと非スレッドセーフなバージョンを
     * テンプレートパラメータで選択できます。
     *
     * 主な用途:
     * - ゲームオブジェクト
     * - パーティクル
     * - コンポーネント
     * - ノード（ツリー、グラフ構造）
     *
     * @tparam bThreadSafe スレッドセーフにするかどうか
     */
    template <bool bThreadSafe = true>
    class PoolAllocator : public IAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         * @param blockSize プール内の各ブロックのサイズ（バイト単位）
         * @param blocksPerChunk 一度に確保するブロック数
         * @param alignment メモリアライメント要件
         */
        explicit PoolAllocator(
            size_t blockSize,
            size_t blocksPerChunk = Config::DefaultBlocksPerChunk,
            size_t alignment = Config::DefaultAlignment);

        /**
         * @brief デストラクタ
         */
        ~PoolAllocator() override;

        // コピー禁止
        PoolAllocator(const PoolAllocator &) = delete;
        PoolAllocator &operator=(const PoolAllocator &) = delete;

        /**
         * @brief メモリブロックを割り当てる
         * @param size 要求サイズ (blockSizeより大きい場合はnullptrを返す)
         * @param alignment アライメント要件（無視されます、コンストラクタで指定された値が使用されます）
         * @return 割り当てられたブロックへのポインタ、またはnullptr
         */
        void *Allocate(size_t size, size_t alignment = Config::DefaultAlignment) override;

        /**
         * @brief メモリブロックを解放する
         * @param ptr 解放するブロックへのポインタ
         */
        void Deallocate(void *ptr) override;

        /**
         * @brief 現在割り当てられているメモリの合計サイズを取得
         * @return 割り当てられたメモリの合計サイズ（バイト単位）
         */
        size_t GetAllocatedSize() const override;

        /**
         * @brief プールが管理するメモリの合計サイズを取得
         * @return 管理しているメモリの合計サイズ（バイト単位）
         */
        size_t GetTotalSize() const override;

        /**
         * @brief アロケータの種類を取得
         * @return AllocatorType::Pool
         */
        AllocatorType GetType() const override;

        /**
         * @brief アロケータがスレッドセーフかどうかを取得
         * @return スレッドセーフな場合true
         */
        bool IsThreadSafe() const override;

        /**
         * @brief 指定されたポインタがこのアロケータで割り当てられたものかチェック
         * @param ptr チェックするポインタ
         * @return このアロケータで割り当てられた場合true
         */
        bool OwnsMemory(const void *ptr) const override;

        /**
         * @brief プールのブロックサイズを取得
         * @return ブロックサイズ（バイト単位）
         */
        size_t GetBlockSize() const;

        /**
         * @brief 未使用ブロック数を取得
         * @return 未使用のブロック数
         */
        size_t GetFreeBlockCount() const;

        /**
         * @brief 合計ブロック数を取得
         * @return プール内の合計ブロック数
         */
        size_t GetTotalBlockCount() const;

        /**
         * @brief 割り当てられたブロックのサイズを取得
         * @param ptr 確認するメモリブロックポインタ
         * @return ブロックのサイズ（バイト単位）
         */
        size_t GetBlockSize(const void *ptr) const override;

    private:
        // フリーリストノード
        struct FreeNode
        {
            FreeNode *next;
        };

        // チャンク構造体（メモリの一塊）
        struct Chunk
        {
            Chunk* Next;
            void* Memory;
            size_t BlockCount;
        };

        class TPoolLockGuard
        {
        public:
            explicit TPoolLockGuard(PoolAllocator& allocator)
                : m_Allocator(allocator)
            {
                if constexpr (bThreadSafe)
                {
                    m_Allocator.Lock();
                }
            }

            ~TPoolLockGuard()
            {
                if constexpr (bThreadSafe)
                {
                    m_Allocator.Unlock();
                }
            }

            TPoolLockGuard(const TPoolLockGuard&) = delete;
            TPoolLockGuard& operator=(const TPoolLockGuard&) = delete;

        private:
            PoolAllocator& m_Allocator;
        };

        /**
         * @brief 新しいチャンクを確保してフリーリストに追加
         */
        void AllocateChunk();

        /**
         * @brief ロックを取得（スレッドセーフ版のみ）
         */
        void Lock() const;

        /**
         * @brief ロックを解放（スレッドセーフ版のみ）
         */
        void Unlock() const;

        size_t m_blockSize;      ///< 各ブロックのサイズ
        size_t m_alignment;      ///< アライメント要件
        size_t m_blocksPerChunk; ///< 一度に確保するブロック数

        FreeNode* m_freeList;  ///< フリーリストの先頭
        Chunk* m_chunkHead;    ///< 確保したチャンクのリスト

        Thread::Atomic<size_t> m_totalBlocks;     ///< 合計ブロック数
        Thread::Atomic<size_t> m_allocatedBlocks; ///< 割り当て済みブロック数

        mutable Thread::Mutex m_mutex; ///< ミューテックス（スレッドセーフ版用）
    };

    // 型エイリアス
    using ThreadSafePoolAllocator = PoolAllocator<true>;
    using SingleThreadPoolAllocator = PoolAllocator<false>;

    // ===========================================
    // テンプレート実装
    // ===========================================

    template <bool bThreadSafe>
    PoolAllocator<bThreadSafe>::PoolAllocator(size_t blockSize, size_t blocksPerChunk, size_t alignment)
        : m_blockSize(AlignUp((std::max)(blockSize, sizeof(FreeNode)), alignment)), m_alignment(alignment), m_blocksPerChunk(blocksPerChunk), m_freeList(nullptr), m_chunkHead(nullptr), m_totalBlocks(0), m_allocatedBlocks(0)
    {
        // 最初のチャンクを割り当てる
        AllocateChunk();
    }

    template <bool bThreadSafe>
    PoolAllocator<bThreadSafe>::~PoolAllocator()
    {
        Chunk* chunk = m_chunkHead;
        while (chunk != nullptr)
        {
            Chunk* next = chunk->Next;
#ifdef _WIN32
            _aligned_free(chunk->Memory);
            chunk->~Chunk();
            _aligned_free(chunk);
#else
            std::free(chunk->Memory);
            chunk->~Chunk();
            std::free(chunk);
#endif
            chunk = next;
        }
    }

    template <bool bThreadSafe>
    void PoolAllocator<bThreadSafe>::AllocateChunk()
    {
        if (m_blocksPerChunk == 0 || m_blockSize > (std::numeric_limits<size_t>::max)() / m_blocksPerChunk)
        {
            throw std::bad_alloc();
        }

        const size_t chunkSize = m_blockSize * m_blocksPerChunk;

#ifdef _WIN32
        void* chunkMemory = _aligned_malloc(chunkSize, m_alignment);
        void* chunkMetadata = _aligned_malloc(sizeof(Chunk), alignof(Chunk));
#else
        void* chunkMemory = std::aligned_alloc(m_alignment, chunkSize);
        const size_t metadataSize = AlignUp(sizeof(Chunk), alignof(Chunk));
        void* chunkMetadata = std::aligned_alloc(alignof(Chunk), metadataSize);
#endif

        if (chunkMemory == nullptr || chunkMetadata == nullptr)
        {
            if (chunkMemory != nullptr)
            {
#ifdef _WIN32
                _aligned_free(chunkMemory);
#else
                std::free(chunkMemory);
#endif
            }
            if (chunkMetadata != nullptr)
            {
#ifdef _WIN32
                _aligned_free(chunkMetadata);
#else
                std::free(chunkMetadata);
#endif
            }
            throw std::bad_alloc();
        }

        Chunk* chunk = nullptr;
        try
        {
            chunk = new (chunkMetadata) Chunk{nullptr, chunkMemory, m_blocksPerChunk};
        }
        catch (...)
        {
#ifdef _WIN32
            _aligned_free(chunkMetadata);
            _aligned_free(chunkMemory);
#else
            std::free(chunkMetadata);
            std::free(chunkMemory);
#endif
            throw;
        }

        FreeNode* localFreeList = nullptr;
        FreeNode* localFreeTail = nullptr;
        char* blockPtr = static_cast<char*>(chunkMemory);
        for (size_t i = 0; i < m_blocksPerChunk; ++i)
        {
            FreeNode* node = reinterpret_cast<FreeNode*>(blockPtr);
            node->next = localFreeList;
            localFreeList = node;
            if (localFreeTail == nullptr)
            {
                localFreeTail = node;
            }
            blockPtr += m_blockSize;
        }

        localFreeTail->next = m_freeList;
        chunk->Next = m_chunkHead;
        m_chunkHead = chunk;
        m_freeList = localFreeList;
        m_totalBlocks.Store(m_totalBlocks.Load() + m_blocksPerChunk);
    }

    template <bool bThreadSafe>
    void *PoolAllocator<bThreadSafe>::Allocate(size_t size, size_t /*alignment*/)
    {
        // 要求サイズがブロックサイズを超える場合はnullptr
        if (size > m_blockSize)
        {
            return nullptr;
        }

        TPoolLockGuard lockGuard(*this);

        // フリーリストが空の場合、新しいチャンクを確保
        if (!m_freeList)
        {
            AllocateChunk();
        }

        // フリーリストから1つ取得
        FreeNode *node = m_freeList;
        m_freeList = node->next;

        m_allocatedBlocks.Store(m_allocatedBlocks.Load() + 1);

        return node;
    }

    template <bool bThreadSafe>
    void PoolAllocator<bThreadSafe>::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

#ifndef NDEBUG
        // プール管理外のポインタでないことを確認（デバッグ用）
        if (!OwnsMemory(ptr))
        {
            assert(false && "Invalid pointer deallocated from PoolAllocator");
            return;
        }
#endif

        FreeNode *node = static_cast<FreeNode *>(ptr);

        if constexpr (bThreadSafe)
        {
            Lock();
        }

        // ブロックをフリーリストに戻す
        node->next = m_freeList;
        m_freeList = node;

        if constexpr (bThreadSafe)
        {
            Unlock();
        }

        m_allocatedBlocks.Store(m_allocatedBlocks.Load() - 1);
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetAllocatedSize() const
    {
        return m_allocatedBlocks.Load() * m_blockSize;
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetTotalSize() const
    {
        return m_totalBlocks.Load() * m_blockSize;
    }

    template <bool bThreadSafe>
    AllocatorType PoolAllocator<bThreadSafe>::GetType() const
    {
        return AllocatorType::Pool;
    }

    template <bool bThreadSafe>
    bool PoolAllocator<bThreadSafe>::IsThreadSafe() const
    {
        return bThreadSafe;
    }

    template <bool bThreadSafe>
    bool PoolAllocator<bThreadSafe>::OwnsMemory(const void *ptr) const
    {
        if (!ptr)
        {
            return false;
        }

        if constexpr (bThreadSafe)
        {
            Lock();
        }

        bool bOwned = false;
        for (const Chunk* chunk = m_chunkHead; chunk != nullptr; chunk = chunk->Next)
        {
            const char* start = static_cast<const char*>(chunk->Memory);
            const char* end = start + (m_blockSize * chunk->BlockCount);
            const char* p = static_cast<const char*>(ptr);

            if (p >= start && p < end)
            {
                // アライメントチェック
                size_t offset = static_cast<size_t>(p - start);
                if (offset % m_blockSize == 0)
                {
                    bOwned = true;
                    break;
                }
            }
        }

        if constexpr (bThreadSafe)
        {
            Unlock();
        }

        return bOwned;
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetBlockSize() const
    {
        return m_blockSize;
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetFreeBlockCount() const
    {
        return m_totalBlocks.Load() - m_allocatedBlocks.Load();
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetTotalBlockCount() const
    {
        return m_totalBlocks.Load();
    }

    template <bool bThreadSafe>
    size_t PoolAllocator<bThreadSafe>::GetBlockSize(const void *ptr) const
    {
        if (OwnsMemory(ptr))
        {
            return m_blockSize;
        }
        return 0;
    }

    template <bool bThreadSafe>
    void PoolAllocator<bThreadSafe>::Lock() const
    {
        if constexpr (bThreadSafe)
        {
            m_mutex.Lock();
        }
    }

    template <bool bThreadSafe>
    void PoolAllocator<bThreadSafe>::Unlock() const
    {
        if constexpr (bThreadSafe)
        {
            m_mutex.Unlock();
        }
    }

} // namespace NorvesLib::Memory
