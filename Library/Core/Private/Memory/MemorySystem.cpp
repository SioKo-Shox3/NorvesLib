#include "Memory/MemorySystem.h"

#include "Memory/FrameAllocator.h"
#include "Memory/GlobalAllocator.h"
#include "Memory/ThreadLocalCache.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace NorvesLib::Memory
{
    namespace
    {
        enum class ELifecycleState : uint8_t
        {
            Uninitialized,
            Running,
            ShuttingDown
        };

        struct ThreadCacheEntry
        {
            explicit ThreadCacheEntry(GlobalAllocator* globalAllocator)
                : Next(nullptr), Cache(globalAllocator)
            {
            }

            ThreadCacheEntry* Next;
            ThreadLocalCache Cache;
        };

        struct ThreadCacheSlot
        {
            ThreadLocalCache* Cache = nullptr;
            uint64_t Generation = 0;
            bool bCreating = false;
        };

        Thread::Atomic<ELifecycleState> s_lifecycleState(ELifecycleState::Uninitialized);
        Thread::Atomic<uint64_t> s_generation(0);
        Core::Container::TUniquePtr<GlobalAllocator> s_globalAllocator = nullptr;
        Core::Container::TUniquePtr<FrameAllocator> s_frameAllocator = nullptr;
        Thread::Mutex s_cacheMutex;
        ThreadCacheEntry* s_cacheHead = nullptr;
        thread_local ThreadCacheSlot s_threadCacheSlot;

        void* AllocatePlatformAligned(size_t size, size_t alignment)
        {
#ifdef _WIN32
            return _aligned_malloc(size, alignment);
#else
            if (alignment == 0)
            {
                return nullptr;
            }

            const size_t remainder = size % alignment;
            const size_t alignedSize = remainder == 0 ? size : size + (alignment - remainder);
            if (alignedSize < size)
            {
                return nullptr;
            }
            return std::aligned_alloc(alignment, alignedSize);
#endif
        }

        void FreePlatformAligned(void* memory)
        {
#ifdef _WIN32
            _aligned_free(memory);
#else
            std::free(memory);
#endif
        }

        bool IsRunning()
        {
            return s_lifecycleState.Load(std::memory_order_acquire) == ELifecycleState::Running;
        }

        uint64_t GetRunningGeneration()
        {
            if (!IsRunning())
            {
                return 0;
            }
            return s_generation.Load(std::memory_order_acquire);
        }

        void ClearStaleThreadCacheSlot(uint64_t generation)
        {
            if (s_threadCacheSlot.Generation != generation)
            {
                s_threadCacheSlot.Cache = nullptr;
                s_threadCacheSlot.Generation = 0;
            }
        }

        void DestroyThreadCacheEntry(ThreadCacheEntry* entry)
        {
            entry->Cache.FlushToGlobal();
            entry->~ThreadCacheEntry();
            FreePlatformAligned(entry);
        }
    }

    void MemorySystem::Initialize()
    {
        if (s_lifecycleState.Load(std::memory_order_acquire) != ELifecycleState::Uninitialized)
        {
            return;
        }

        Core::Container::TUniquePtr<GlobalAllocator> globalAllocator;
        Core::Container::TUniquePtr<FrameAllocator> frameAllocator;
        try
        {
            globalAllocator = Core::Container::MakeUnique<GlobalAllocator>();
            frameAllocator = Core::Container::MakeUnique<FrameAllocator>();
        }
        catch (...)
        {
            throw;
        }

        s_globalAllocator = std::move(globalAllocator);
        s_frameAllocator = std::move(frameAllocator);
        uint64_t generation = s_generation.Load(std::memory_order_acquire) + 1;
        if (generation == 0)
        {
            generation = 1;
        }
        s_generation.Store(generation, std::memory_order_release);
        s_lifecycleState.Store(ELifecycleState::Running, std::memory_order_release);
    }

    void MemorySystem::Shutdown()
    {
        if (s_lifecycleState.Load(std::memory_order_acquire) != ELifecycleState::Running)
        {
            return;
        }

        s_lifecycleState.Store(ELifecycleState::ShuttingDown, std::memory_order_release);
        ThreadCacheEntry* detachedHead = nullptr;
        {
            Thread::ScopedLock lock(s_cacheMutex);
            detachedHead = s_cacheHead;
            s_cacheHead = nullptr;
        }

        while (detachedHead != nullptr)
        {
            ThreadCacheEntry* next = detachedHead->Next;
            DestroyThreadCacheEntry(detachedHead);
            detachedHead = next;
        }

        s_threadCacheSlot.Cache = nullptr;
        s_threadCacheSlot.Generation = 0;
        s_lifecycleState.Store(ELifecycleState::Uninitialized, std::memory_order_release);
        s_frameAllocator.reset();
        s_globalAllocator.reset();
    }

    bool MemorySystem::IsInitialized()
    {
        return IsRunning();
    }

    void* MemorySystem::Allocate(size_t size, size_t alignment)
    {
        const ELifecycleState lifecycleState = s_lifecycleState.Load(std::memory_order_acquire);
        if (lifecycleState == ELifecycleState::Uninitialized)
        {
            return AllocatePlatformAligned(size, alignment);
        }

        if (lifecycleState == ELifecycleState::ShuttingDown)
        {
            return s_globalAllocator != nullptr ? s_globalAllocator->Allocate(size, alignment) : nullptr;
        }

        ThreadLocalCache* cache = GetOrCreateThreadCache();
        if (cache != nullptr)
        {
            return cache->Allocate(size, alignment);
        }
        return s_globalAllocator != nullptr ? s_globalAllocator->Allocate(size, alignment) : nullptr;
    }

    void MemorySystem::Deallocate(void* ptr)
    {
        if (ptr == nullptr)
        {
            return;
        }

        const ELifecycleState lifecycleState = s_lifecycleState.Load(std::memory_order_acquire);
        if (lifecycleState == ELifecycleState::Uninitialized)
        {
            FreePlatformAligned(ptr);
            return;
        }

        if (lifecycleState == ELifecycleState::ShuttingDown)
        {
            if (s_globalAllocator != nullptr)
            {
                s_globalAllocator->Deallocate(ptr);
            }
            return;
        }

        ThreadLocalCache* cache = GetOrCreateThreadCache();
        if (cache != nullptr)
        {
            cache->Deallocate(ptr);
            return;
        }
        if (s_globalAllocator != nullptr)
        {
            s_globalAllocator->Deallocate(ptr);
        }
    }

    void* MemorySystem::Reallocate(void* ptr, size_t newSize, size_t alignment)
    {
        if (ptr == nullptr)
        {
            return Allocate(newSize, alignment);
        }
        if (newSize == 0)
        {
            Deallocate(ptr);
            return nullptr;
        }

        const size_t oldSize = GetBlockSize(ptr);
        if (oldSize >= newSize)
        {
            return ptr;
        }

        void* newPtr = Allocate(newSize, alignment);
        if (newPtr == nullptr)
        {
            return nullptr;
        }
        std::memcpy(newPtr, ptr, (std::min)(oldSize, newSize));
        Deallocate(ptr);
        return newPtr;
    }

    size_t MemorySystem::GetBlockSize(const void* ptr)
    {
        if (!IsRunning() || ptr == nullptr || s_globalAllocator == nullptr)
        {
            return 0;
        }
        return s_globalAllocator->GetBlockSize(ptr);
    }

    GlobalAllocator* MemorySystem::GetGlobalAllocator()
    {
        return IsRunning() ? s_globalAllocator.get() : nullptr;
    }

    ThreadLocalCache* MemorySystem::GetThreadLocalCache()
    {
        return GetOrCreateThreadCache();
    }

    ThreadLocalCache* MemorySystem::GetOrCreateThreadCache()
    {
        const uint64_t generation = GetRunningGeneration();
        if (generation == 0)
        {
            return nullptr;
        }

        ClearStaleThreadCacheSlot(generation);
        if (s_threadCacheSlot.Cache != nullptr)
        {
            return s_threadCacheSlot.Cache;
        }
        if (s_threadCacheSlot.bCreating)
        {
            return nullptr;
        }

        s_threadCacheSlot.bCreating = true;
        void* memory = AllocatePlatformAligned(sizeof(ThreadCacheEntry), alignof(ThreadCacheEntry));
        if (memory == nullptr)
        {
            s_threadCacheSlot.bCreating = false;
            return nullptr;
        }

        ThreadCacheEntry* entry = nullptr;
        try
        {
            entry = new (memory) ThreadCacheEntry(s_globalAllocator.get());
        }
        catch (...)
        {
            FreePlatformAligned(memory);
            s_threadCacheSlot.bCreating = false;
            return nullptr;
        }

        {
            Thread::ScopedLock lock(s_cacheMutex);
            if (!IsRunning() || s_generation.Load(std::memory_order_acquire) != generation)
            {
                entry->~ThreadCacheEntry();
                FreePlatformAligned(entry);
                s_threadCacheSlot.bCreating = false;
                return nullptr;
            }
            entry->Next = s_cacheHead;
            s_cacheHead = entry;
        }

        s_threadCacheSlot.Cache = &entry->Cache;
        s_threadCacheSlot.Generation = generation;
        s_threadCacheSlot.bCreating = false;
        return s_threadCacheSlot.Cache;
    }

    void* MemorySystem::AllocateFrame(size_t size, size_t alignment)
    {
        return IsRunning() && s_frameAllocator != nullptr ? s_frameAllocator->Allocate(size, alignment) : nullptr;
    }

    void MemorySystem::AdvanceFrame()
    {
        if (IsRunning() && s_frameAllocator != nullptr)
        {
            s_frameAllocator->SwapBuffers();
        }
    }

    FrameAllocator* MemorySystem::GetFrameAllocator()
    {
        return IsRunning() ? s_frameAllocator.get() : nullptr;
    }

    size_t MemorySystem::GetTotalAllocatedSize()
    {
        return IsRunning() && s_globalAllocator != nullptr ? s_globalAllocator->GetAllocatedSize() : 0;
    }

    uint64_t MemorySystem::GetTotalAllocationCount()
    {
        return IsRunning() && s_globalAllocator != nullptr ? s_globalAllocator->GetTotalAllocationCount() : 0;
    }

    uint64_t MemorySystem::GetTotalDeallocationCount()
    {
        return IsRunning() && s_globalAllocator != nullptr ? s_globalAllocator->GetTotalDeallocationCount() : 0;
    }

    void MemorySystem::FlushThreadCache()
    {
        const uint64_t generation = GetRunningGeneration();
        if (generation == 0)
        {
            return;
        }
        ClearStaleThreadCacheSlot(generation);
        if (s_threadCacheSlot.Cache != nullptr)
        {
            s_threadCacheSlot.Cache->FlushToGlobal();
        }
    }

    void MemorySystem::FlushAllThreadCaches()
    {
        if (!IsRunning())
        {
            return;
        }
        Thread::ScopedLock lock(s_cacheMutex);
        for (ThreadCacheEntry* entry = s_cacheHead; entry != nullptr; entry = entry->Next)
        {
            entry->Cache.FlushToGlobal();
        }
    }
} // namespace NorvesLib::Memory
