#include "../Public/MemorySystem.h"
#include <cassert>
#include <cstring>
#include <algorithm>

namespace NorvesLib::Memory
{
    // 静的メンバの定義
    bool MemorySystem::s_bInitialized = false;
    Core::Container::TUniquePtr<GlobalAllocator> MemorySystem::s_globalAllocator = nullptr;
    Core::Container::TUniquePtr<FrameAllocator> MemorySystem::s_frameAllocator = nullptr;
    Thread::ThreadLocalStorage<ThreadLocalCache *> MemorySystem::s_threadCache;
    Thread::Mutex MemorySystem::s_cacheMutex;
    std::vector<ThreadLocalCache *> MemorySystem::s_allThreadCaches;

    void MemorySystem::Initialize()
    {
        if (s_bInitialized)
        {
            return;
        }

        // グローバルアロケータを作成
        s_globalAllocator = Core::Container::MakeUnique<GlobalAllocator>();

        // フレームアロケータを作成
        s_frameAllocator = Core::Container::MakeUnique<FrameAllocator>();

        s_bInitialized = true;
    }

    void MemorySystem::Shutdown()
    {
        if (!s_bInitialized)
        {
            return;
        }

        // すべてのスレッドキャッシュをフラッシュして削除
        {
            Thread::ScopedLock lock(s_cacheMutex);
            for (auto *cache : s_allThreadCaches)
            {
                if (cache)
                {
                    cache->FlushToGlobal();
                    delete cache;
                }
            }
            s_allThreadCaches.clear();
        }

        // フレームアロケータを解放
        s_frameAllocator.reset();

        // グローバルアロケータを解放
        s_globalAllocator.reset();

        s_bInitialized = false;
    }

    bool MemorySystem::IsInitialized()
    {
        return s_bInitialized;
    }

    void *MemorySystem::Allocate(size_t size, size_t alignment)
    {
        if (!s_bInitialized)
        {
            // 初期化されていない場合は直接システムから確保
#ifdef _WIN32
            return _aligned_malloc(size, alignment);
#else
            return std::aligned_alloc(alignment, AlignUp(size, alignment));
#endif
        }

        // スレッドローカルキャッシュを使用
        ThreadLocalCache *cache = GetOrCreateThreadCache();
        if (cache)
        {
            return cache->Allocate(size, alignment);
        }

        // フォールバック: グローバルアロケータを直接使用
        return s_globalAllocator->Allocate(size, alignment);
    }

    void MemorySystem::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        if (!s_bInitialized)
        {
            // 初期化されていない場合は直接システムに返却
#ifdef _WIN32
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
            return;
        }

        // スレッドローカルキャッシュを使用
        ThreadLocalCache *cache = GetOrCreateThreadCache();
        if (cache)
        {
            cache->Deallocate(ptr);
            return;
        }

        // フォールバック: グローバルアロケータを直接使用
        s_globalAllocator->Deallocate(ptr);
    }

    void *MemorySystem::Reallocate(void *ptr, size_t newSize, size_t alignment)
    {
        if (!ptr)
        {
            return Allocate(newSize, alignment);
        }

        if (newSize == 0)
        {
            Deallocate(ptr);
            return nullptr;
        }

        // 既存ブロックのサイズを取得
        size_t oldSize = GetBlockSize(ptr);

        // サイズが変わらない場合はそのまま返す
        if (oldSize >= newSize)
        {
            return ptr;
        }

        // 新しいメモリを確保
        void *newPtr = Allocate(newSize, alignment);
        if (!newPtr)
        {
            return nullptr;
        }

        // データをコピー
        std::memcpy(newPtr, ptr, (std::min)(oldSize, newSize));

        // 古いメモリを解放
        Deallocate(ptr);

        return newPtr;
    }

    size_t MemorySystem::GetBlockSize(const void *ptr)
    {
        if (!s_bInitialized || !ptr)
        {
            return 0;
        }

        return s_globalAllocator->GetBlockSize(ptr);
    }

    GlobalAllocator *MemorySystem::GetGlobalAllocator()
    {
        return s_globalAllocator.get();
    }

    ThreadLocalCache *MemorySystem::GetThreadLocalCache()
    {
        if (!s_bInitialized)
        {
            return nullptr;
        }

        return GetOrCreateThreadCache();
    }

    ThreadLocalCache *MemorySystem::GetOrCreateThreadCache()
    {
        if (!s_bInitialized)
        {
            return nullptr;
        }

        // スレッドローカルストレージからキャッシュを取得
        ThreadLocalCache *cache = s_threadCache.Get();

        if (!cache)
        {
            // 新しいキャッシュを作成
            cache = new ThreadLocalCache(s_globalAllocator.get());

            // スレッドローカルストレージに保存
            s_threadCache.Set(cache);

            // グローバルリストに追加
            {
                Thread::ScopedLock lock(s_cacheMutex);
                s_allThreadCaches.push_back(cache);
            }
        }

        return cache;
    }

    void *MemorySystem::AllocateFrame(size_t size, size_t alignment)
    {
        if (!s_bInitialized || !s_frameAllocator)
        {
            return nullptr;
        }

        return s_frameAllocator->Allocate(size, alignment);
    }

    void MemorySystem::AdvanceFrame()
    {
        if (s_bInitialized && s_frameAllocator)
        {
            s_frameAllocator->SwapBuffers();
        }
    }

    FrameAllocator *MemorySystem::GetFrameAllocator()
    {
        return s_frameAllocator.get();
    }

    size_t MemorySystem::GetTotalAllocatedSize()
    {
        if (!s_bInitialized)
        {
            return 0;
        }

        return s_globalAllocator->GetAllocatedSize();
    }

    uint64_t MemorySystem::GetTotalAllocationCount()
    {
        if (!s_bInitialized)
        {
            return 0;
        }

        return s_globalAllocator->GetTotalAllocationCount();
    }

    uint64_t MemorySystem::GetTotalDeallocationCount()
    {
        if (!s_bInitialized)
        {
            return 0;
        }

        return s_globalAllocator->GetTotalDeallocationCount();
    }

    void MemorySystem::FlushThreadCache()
    {
        ThreadLocalCache *cache = s_threadCache.Get();
        if (cache)
        {
            cache->FlushToGlobal();
        }
    }

    void MemorySystem::FlushAllThreadCaches()
    {
        Thread::ScopedLock lock(s_cacheMutex);
        for (auto *cache : s_allThreadCaches)
        {
            if (cache)
            {
                cache->FlushToGlobal();
            }
        }
    }

} // namespace NorvesLib::Memory
