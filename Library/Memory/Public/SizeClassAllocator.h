#pragma once

#include "IAllocator.h"
#include "PoolAllocator.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>
#include <memory>

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
     * @brief サイズクラスアロケータ
     *
     * 複数のサイズクラスを持つプールアロケータの集合体。
     * 要求サイズに応じて適切なサイズクラスを選択し、
     * そのプールからメモリを確保します。
     *
     * 大きなオブジェクト（LargeObjectThreshold以上）は
     * 直接OSから確保されます。
     *
     * @tparam bThreadSafe スレッドセーフにするかどうか
     */
    template <bool bThreadSafe = true>
    class SizeClassAllocator : public IAllocator
    {
    public:
        /**
         * @brief コンストラクタ
         * @param blocksPerChunk 各サイズクラスのチャンクあたりのブロック数
         */
        explicit SizeClassAllocator(size_t blocksPerChunk = Config::DefaultBlocksPerChunk);

        /**
         * @brief デストラクタ
         */
        ~SizeClassAllocator() override;

        // コピー禁止
        SizeClassAllocator(const SizeClassAllocator &) = delete;
        SizeClassAllocator &operator=(const SizeClassAllocator &) = delete;

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
         * @return AllocatorType::SizeClass
         */
        AllocatorType GetType() const override;

        /**
         * @brief アロケータがスレッドセーフかどうか
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
         * @brief 割り当てられたブロックのサイズを取得
         * @param ptr 確認するメモリブロックポインタ
         * @return ブロックのサイズ（バイト単位）
         */
        size_t GetBlockSize(const void *ptr) const override;

        /**
         * @brief 指定されたサイズに対応するサイズクラスインデックスを取得
         * @param size 要求サイズ
         * @return サイズクラスインデックス（-1は大きなオブジェクト）
         */
        static int GetSizeClassIndexForSize(size_t size);

    private:
        // 大きなオブジェクトのヘッダー
        struct LargeObjectHeader
        {
            size_t size; ///< 割り当てサイズ
            LargeObjectHeader *prev;
            LargeObjectHeader *next;
        };

        /**
         * @brief 大きなオブジェクトを割り当てる
         * @param size 要求サイズ
         * @param alignment アライメント要件
         * @return 割り当てられたメモリへのポインタ
         */
        void *AllocateLarge(size_t size, size_t alignment);

        /**
         * @brief 大きなオブジェクトを解放する
         * @param ptr 解放するポインタ
         */
        void DeallocateLarge(void *ptr);

        /**
         * @brief 大きなオブジェクトかどうかチェック
         * @param ptr チェックするポインタ
         * @return 大きなオブジェクトの場合true
         */
        bool IsLargeObject(const void *ptr) const;

        /**
         * @brief ロックを取得
         */
        void Lock() const;

        /**
         * @brief ロックを解放
         */
        void Unlock() const;

        // サイズクラス別のプールアロケータ
        std::vector<std::unique_ptr<PoolAllocator<bThreadSafe>>> m_pools;

        // 大きなオブジェクトのリスト
        LargeObjectHeader *m_largeObjects;
        Thread::Atomic<size_t> m_largeObjectSize;
        Thread::Atomic<size_t> m_largeObjectCount;

        mutable Thread::Mutex m_largeMutex; ///< 大きなオブジェクト用ミューテックス
    };

    // 型エイリアス
    using ThreadSafeSizeClassAllocator = SizeClassAllocator<true>;
    using SingleThreadSizeClassAllocator = SizeClassAllocator<false>;

    // ===========================================
    // テンプレート実装
    // ===========================================

    template <bool bThreadSafe>
    SizeClassAllocator<bThreadSafe>::SizeClassAllocator(size_t blocksPerChunk)
        : m_largeObjects(nullptr), m_largeObjectSize(0), m_largeObjectCount(0)
    {
        // 各サイズクラスのプールを作成
        m_pools.reserve(Config::NumSizeClasses);

        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            size_t classSize = Config::SizeClasses[i];
            m_pools.push_back(Core::Container::MakeUnique<PoolAllocator<bThreadSafe>>(
                classSize,
                blocksPerChunk,
                Config::DefaultAlignment));
        }
    }

    template <bool bThreadSafe>
    SizeClassAllocator<bThreadSafe>::~SizeClassAllocator()
    {
        // 大きなオブジェクトを解放
        LargeObjectHeader *current = m_largeObjects;
        while (current)
        {
            LargeObjectHeader *next = current->next;
#ifdef _WIN32
            _aligned_free(current);
#else
            std::free(current);
#endif
            current = next;
        }

        // プールは自動的に解放される（unique_ptr）
    }

    template <bool bThreadSafe>
    int SizeClassAllocator<bThreadSafe>::GetSizeClassIndexForSize(size_t size)
    {
        return GetSizeClassIndex(size);
    }

    template <bool bThreadSafe>
    void *SizeClassAllocator<bThreadSafe>::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
        {
            return nullptr;
        }

        // アライメント調整後のサイズ
        size_t alignedSize = AlignUp(size, alignment);

        // サイズクラスを取得
        int classIndex = GetSizeClassIndexForSize(alignedSize);

        if (classIndex < 0)
        {
            // 大きなオブジェクト
            return AllocateLarge(alignedSize, alignment);
        }

        // 対応するプールから割り当て
        return m_pools[classIndex]->Allocate(alignedSize, alignment);
    }

    template <bool bThreadSafe>
    void SizeClassAllocator<bThreadSafe>::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        // まず各プールをチェック
        for (auto &pool : m_pools)
        {
            if (pool->OwnsMemory(ptr))
            {
                pool->Deallocate(ptr);
                return;
            }
        }

        // 大きなオブジェクトをチェック
        if (IsLargeObject(ptr))
        {
            DeallocateLarge(ptr);
            return;
        }

        // 不明なポインタ
        assert(false && "Attempting to deallocate pointer not owned by this allocator");
    }

    template <bool bThreadSafe>
    void *SizeClassAllocator<bThreadSafe>::AllocateLarge(size_t size, size_t alignment)
    {
        // ヘッダー + アライメント + データ
        size_t headerSize = AlignUp(sizeof(LargeObjectHeader), alignment);
        size_t totalSize = headerSize + size;

#ifdef _WIN32
        void *memory = _aligned_malloc(totalSize, alignment);
#else
        void *memory = std::aligned_alloc(alignment, totalSize);
#endif

        if (!memory)
        {
            return nullptr;
        }

        LargeObjectHeader *header = static_cast<LargeObjectHeader *>(memory);
        header->size = size;

        if constexpr (bThreadSafe)
        {
            Lock();
        }

        // リストに追加
        header->prev = nullptr;
        header->next = m_largeObjects;
        if (m_largeObjects)
        {
            m_largeObjects->prev = header;
        }
        m_largeObjects = header;

        if constexpr (bThreadSafe)
        {
            Unlock();
        }

        m_largeObjectSize.Store(m_largeObjectSize.Load() + size);
        m_largeObjectCount.Store(m_largeObjectCount.Load() + 1);

        // ペイロードポインタを返す
        return reinterpret_cast<char *>(memory) + headerSize;
    }

    template <bool bThreadSafe>
    void SizeClassAllocator<bThreadSafe>::DeallocateLarge(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        // ヘッダーを取得
        size_t headerSize = AlignUp(sizeof(LargeObjectHeader), Config::DefaultAlignment);
        LargeObjectHeader *header = reinterpret_cast<LargeObjectHeader *>(
            reinterpret_cast<char *>(ptr) - headerSize);

        if constexpr (bThreadSafe)
        {
            Lock();
        }

        // リストから削除
        if (header->prev)
        {
            header->prev->next = header->next;
        }
        else
        {
            m_largeObjects = header->next;
        }

        if (header->next)
        {
            header->next->prev = header->prev;
        }

        if constexpr (bThreadSafe)
        {
            Unlock();
        }

        m_largeObjectSize.Store(m_largeObjectSize.Load() - header->size);
        m_largeObjectCount.Store(m_largeObjectCount.Load() - 1);

#ifdef _WIN32
        _aligned_free(header);
#else
        std::free(header);
#endif
    }

    template <bool bThreadSafe>
    bool SizeClassAllocator<bThreadSafe>::IsLargeObject(const void *ptr) const
    {
        if (!ptr)
        {
            return false;
        }

        if constexpr (bThreadSafe)
        {
            Lock();
        }

        // ヘッダーを取得
        size_t headerSize = AlignUp(sizeof(LargeObjectHeader), Config::DefaultAlignment);
        const LargeObjectHeader *header = reinterpret_cast<const LargeObjectHeader *>(
            reinterpret_cast<const char *>(ptr) - headerSize);

        // リスト内を検索
        bool bFound = false;
        const LargeObjectHeader *current = m_largeObjects;
        while (current)
        {
            if (current == header)
            {
                bFound = true;
                break;
            }
            current = current->next;
        }

        if constexpr (bThreadSafe)
        {
            Unlock();
        }

        return bFound;
    }

    template <bool bThreadSafe>
    size_t SizeClassAllocator<bThreadSafe>::GetAllocatedSize() const
    {
        size_t total = m_largeObjectSize.Load();

        for (const auto &pool : m_pools)
        {
            total += pool->GetAllocatedSize();
        }

        return total;
    }

    template <bool bThreadSafe>
    size_t SizeClassAllocator<bThreadSafe>::GetTotalSize() const
    {
        size_t total = m_largeObjectSize.Load();

        for (const auto &pool : m_pools)
        {
            total += pool->GetTotalSize();
        }

        return total;
    }

    template <bool bThreadSafe>
    AllocatorType SizeClassAllocator<bThreadSafe>::GetType() const
    {
        return AllocatorType::SizeClass;
    }

    template <bool bThreadSafe>
    bool SizeClassAllocator<bThreadSafe>::IsThreadSafe() const
    {
        return bThreadSafe;
    }

    template <bool bThreadSafe>
    bool SizeClassAllocator<bThreadSafe>::OwnsMemory(const void *ptr) const
    {
        if (!ptr)
        {
            return false;
        }

        // 各プールをチェック
        for (const auto &pool : m_pools)
        {
            if (pool->OwnsMemory(ptr))
            {
                return true;
            }
        }

        // 大きなオブジェクトをチェック
        return IsLargeObject(ptr);
    }

    template <bool bThreadSafe>
    size_t SizeClassAllocator<bThreadSafe>::GetBlockSize(const void *ptr) const
    {
        if (!ptr)
        {
            return 0;
        }

        // 各プールをチェック
        for (const auto &pool : m_pools)
        {
            if (pool->OwnsMemory(ptr))
            {
                return pool->GetBlockSize(ptr);
            }
        }

        // 大きなオブジェクトをチェック
        if (IsLargeObject(ptr))
        {
            size_t headerSize = AlignUp(sizeof(LargeObjectHeader), Config::DefaultAlignment);
            const LargeObjectHeader *header = reinterpret_cast<const LargeObjectHeader *>(
                reinterpret_cast<const char *>(ptr) - headerSize);
            return header->size;
        }

        return 0;
    }

    template <bool bThreadSafe>
    void SizeClassAllocator<bThreadSafe>::Lock() const
    {
        if constexpr (bThreadSafe)
        {
            m_largeMutex.Lock();
        }
    }

    template <bool bThreadSafe>
    void SizeClassAllocator<bThreadSafe>::Unlock() const
    {
        if constexpr (bThreadSafe)
        {
            m_largeMutex.Unlock();
        }
    }

} // namespace NorvesLib::Memory
