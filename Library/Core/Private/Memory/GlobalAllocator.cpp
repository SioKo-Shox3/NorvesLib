#include "Memory/GlobalAllocator.h"
#include "Memory/ThreadLocalCache.h"
#include <cassert>
#include <cstring>

namespace NorvesLib::Memory
{
    GlobalAllocator::GlobalAllocator()
        : m_sizeClassAllocator(Core::Container::MakeUnique<SizeClassAllocator<true>>()), m_totalAllocations(0), m_totalDeallocations(0)
    {
        // 中央フリーリストを初期化
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            m_centralLists[i].freeList = nullptr;
            m_centralLists[i].count = 0;
        }
    }

    GlobalAllocator::~GlobalAllocator()
    {
        // 中央フリーリストのオブジェクトをすべて解放
        for (size_t i = 0; i < Config::NumSizeClasses; ++i)
        {
            CentralFreeList &list = m_centralLists[i];
            Thread::ScopedLock lock(list.mutex);

            FreeNode *current = list.freeList;
            while (current)
            {
                FreeNode *next = current->next;
                m_sizeClassAllocator->Deallocate(current);
                current = next;
            }
            list.freeList = nullptr;
            list.count = 0;
        }
    }

    void *GlobalAllocator::Allocate(size_t size, size_t alignment)
    {
        if (size == 0)
        {
            return nullptr;
        }

        m_totalAllocations.Store(m_totalAllocations.Load() + 1);

        // サイズクラスアロケータから直接割り当て
        return m_sizeClassAllocator->Allocate(size, alignment);
    }

    void GlobalAllocator::Deallocate(void *ptr)
    {
        if (!ptr)
        {
            return;
        }

        m_totalDeallocations.Store(m_totalDeallocations.Load() + 1);

        // サイズクラスアロケータに直接返却
        m_sizeClassAllocator->Deallocate(ptr);
    }

    size_t GlobalAllocator::FetchFromCentral(int classIndex, void **objects, size_t maxCount)
    {
        if (classIndex < 0 || classIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return 0;
        }

        CentralFreeList &list = m_centralLists[classIndex];
        Thread::ScopedLock lock(list.mutex);

        size_t fetched = 0;
        size_t classSize = Config::SizeClasses[classIndex];

        // 中央フリーリストからオブジェクトを取得
        while (fetched < maxCount && list.freeList)
        {
            FreeNode *node = list.freeList;
            list.freeList = node->next;
            list.count--;
            objects[fetched++] = node;
        }

        // 中央フリーリストが空になった場合、サイズクラスアロケータから補充
        while (fetched < maxCount)
        {
            void *ptr = m_sizeClassAllocator->Allocate(classSize, Config::DefaultAlignment);
            if (!ptr)
            {
                break; // メモリ不足
            }
            objects[fetched++] = ptr;
            m_totalAllocations.Store(m_totalAllocations.Load() + 1);
        }

        return fetched;
    }

    void GlobalAllocator::ReturnToCentral(int classIndex, void **objects, size_t count)
    {
        if (classIndex < 0 || classIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return;
        }

        if (count == 0 || !objects)
        {
            return;
        }

        CentralFreeList &list = m_centralLists[classIndex];
        Thread::ScopedLock lock(list.mutex);

        // オブジェクトを中央フリーリストに追加
        for (size_t i = 0; i < count; ++i)
        {
            FreeNode *node = static_cast<FreeNode *>(objects[i]);
            node->next = list.freeList;
            list.freeList = node;
            list.count++;
        }
    }

    size_t GlobalAllocator::GetSizeClassSize(int classIndex)
    {
        return NorvesLib::Memory::GetSizeClassSize(classIndex);
    }

    int GlobalAllocator::GetSizeClassIndex(size_t size)
    {
        return NorvesLib::Memory::GetSizeClassIndex(size);
    }

    size_t GlobalAllocator::GetAllocatedSize() const
    {
        return m_sizeClassAllocator->GetAllocatedSize();
    }

    size_t GlobalAllocator::GetTotalSize() const
    {
        return m_sizeClassAllocator->GetTotalSize();
    }

    AllocatorType GlobalAllocator::GetType() const
    {
        return AllocatorType::Global;
    }

    bool GlobalAllocator::OwnsMemory(const void *ptr) const
    {
        return m_sizeClassAllocator->OwnsMemory(ptr);
    }

    size_t GlobalAllocator::GetBlockSize(const void *ptr) const
    {
        return m_sizeClassAllocator->GetBlockSize(ptr);
    }

    size_t GlobalAllocator::GetCentralFreeCount(int classIndex) const
    {
        if (classIndex < 0 || classIndex >= static_cast<int>(Config::NumSizeClasses))
        {
            return 0;
        }

        // 注意: この関数はスレッドセーフではない（統計情報の概算用）
        return m_centralLists[classIndex].count;
    }

    uint64_t GlobalAllocator::GetTotalAllocationCount() const
    {
        return m_totalAllocations.Load();
    }

    uint64_t GlobalAllocator::GetTotalDeallocationCount() const
    {
        return m_totalDeallocations.Load();
    }

} // namespace NorvesLib::Memory
