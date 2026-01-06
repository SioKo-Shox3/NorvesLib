#include "../Public/ThreadLocalCache.h"
#include "../Public/GlobalAllocator.h"
#include <cassert>
#include <cstring>

namespace NorvesLib::Memory
{
    ThreadLocalCache::ThreadLocalCache(GlobalAllocator *globalAllocator)
        : m_globalAllocator(globalAllocator), m_allocatedSize(0), m_cachedSize(0)
    {
        assert(globalAllocator != nullptr && "GlobalAllocator must not be null");

        // すべてのキャッシュを初期化
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            m_caches[i].freeList = nullptr;
            m_caches[i].count = 0;
            m_caches[i].maxCount = Config::ThreadCacheMaxObjects;
        }
    }

    ThreadLocalCache::~ThreadLocalCache()
    {
        // キャッシュをグローバルにフラッシュ
        FlushToGlobal();
    }

    void *ThreadLocalCache::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
        {
            return nullptr;
        }

        // アライメント調整後のサイズ
        size_t alignedSize = AlignUp(size, alignment);

        // サイズクラスを取得
        int classIndex = GetSizeClassIndex(alignedSize);

        if (classIndex < 0)
        {
            // 大きなオブジェクトは直接グローバルから取得
            void *ptr = m_globalAllocator->Allocate(alignedSize, alignment);
            if (ptr)
            {
                m_allocatedSize.Store(m_allocatedSize.Load() + alignedSize);
            }
            return ptr;
        }

        SizeClassCache &cache = m_caches[classIndex];

        // キャッシュが空の場合、グローバルからフェッチ
        if (!cache.freeList)
        {
            if (!FetchFromGlobal(classIndex))
            {
                return nullptr; // メモリ不足
            }
        }

        // キャッシュからオブジェクトを取得
        FreeNode *node = cache.freeList;
        cache.freeList = node->next;
        cache.count--;

        size_t classSize = Config::SizeClasses[classIndex];
        m_cachedSize.Store(m_cachedSize.Load() - classSize);
        m_allocatedSize.Store(m_allocatedSize.Load() + classSize);

        return node;
    }

    void ThreadLocalCache::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        // ポインタのサイズクラスを特定
        // グローバルアロケータに問い合わせ
        size_t blockSize = m_globalAllocator->GetBlockSize(ptr);

        if (blockSize == 0)
        {
            // 不明なポインタ
            assert(false && "Unknown pointer passed to ThreadLocalCache::Deallocate");
            return;
        }

        int classIndex = GetSizeClassIndex(blockSize);

        if (classIndex < 0)
        {
            // 大きなオブジェクトは直接グローバルに返却
            m_allocatedSize.Store(m_allocatedSize.Load() - blockSize);
            m_globalAllocator->Deallocate(ptr);
            return;
        }

        SizeClassCache &cache = m_caches[classIndex];
        size_t classSize = Config::SizeClasses[classIndex];

        // キャッシュがいっぱいの場合、一部をグローバルに返却
        if (cache.count >= cache.maxCount)
        {
            ReturnToGlobal(classIndex);
        }

        // キャッシュに追加
        FreeNode *node = static_cast<FreeNode *>(ptr);
        node->next = cache.freeList;
        cache.freeList = node;
        cache.count++;

        m_allocatedSize.Store(m_allocatedSize.Load() - classSize);
        m_cachedSize.Store(m_cachedSize.Load() + classSize);
    }

    bool ThreadLocalCache::FetchFromGlobal(int classIndex)
    {
        if (classIndex < 0 || classIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return false;
        }

        void *objects[Config::ThreadCacheBatchSize];
        size_t fetched = m_globalAllocator->FetchFromCentral(classIndex, objects, Config::ThreadCacheBatchSize);

        if (fetched == 0)
        {
            return false;
        }

        SizeClassCache &cache = m_caches[classIndex];
        size_t classSize = Config::SizeClasses[classIndex];

        // フェッチしたオブジェクトをキャッシュに追加
        for (size_t i = 0; i < fetched; ++i)
        {
            FreeNode *node = static_cast<FreeNode *>(objects[i]);
            node->next = cache.freeList;
            cache.freeList = node;
            cache.count++;
        }

        m_cachedSize.Store(m_cachedSize.Load() + fetched * classSize);

        return true;
    }

    void ThreadLocalCache::ReturnToGlobal(int classIndex)
    {
        if (classIndex < 0 || classIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return;
        }

        SizeClassCache &cache = m_caches[classIndex];

        // バッチサイズ分をグローバルに返却
        size_t returnCount = (std::min)(cache.count, Config::ThreadCacheBatchSize);

        if (returnCount == 0)
        {
            return;
        }

        void *objects[Config::ThreadCacheBatchSize];
        size_t classSize = Config::SizeClasses[classIndex];

        for (size_t i = 0; i < returnCount; ++i)
        {
            FreeNode *node = cache.freeList;
            cache.freeList = node->next;
            cache.count--;
            objects[i] = node;
        }

        m_cachedSize.Store(m_cachedSize.Load() - returnCount * classSize);
        m_globalAllocator->ReturnToCentral(classIndex, objects, returnCount);
    }

    void ThreadLocalCache::FlushToGlobal()
    {
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            SizeClassCache &cache = m_caches[i];

            while (cache.count > 0)
            {
                ReturnToGlobal(static_cast<int>(i));
            }
        }
    }

    size_t ThreadLocalCache::GetAllocatedSize() const
    {
        return m_allocatedSize.Load();
    }

    size_t ThreadLocalCache::GetTotalSize() const
    {
        return m_cachedSize.Load() + m_allocatedSize.Load();
    }

    AllocatorType ThreadLocalCache::GetType() const
    {
        return AllocatorType::ThreadLocal;
    }

    bool ThreadLocalCache::OwnsMemory(const void *ptr) const
    {
        // ThreadLocalCacheは所有権を持たない
        // 実際の所有権はGlobalAllocatorが持つ
        return m_globalAllocator->OwnsMemory(ptr);
    }

    size_t ThreadLocalCache::GetCachedCount(int sizeClassIndex) const
    {
        if (sizeClassIndex < 0 || sizeClassIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return 0;
        }
        return m_caches[sizeClassIndex].count;
    }

    size_t ThreadLocalCache::GetTotalCachedCount() const
    {
        size_t total = 0;
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            total += m_caches[i].count;
        }
        return total;
    }

} // namespace NorvesLib::Memory
