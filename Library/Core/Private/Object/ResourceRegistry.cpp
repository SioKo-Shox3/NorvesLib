#include "Object/ResourceRegistry.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core
{
    ResourceRegistry::~ResourceRegistry()
    {
        if (m_bInitialized)
        {
            Shutdown();
        }
    }

    bool ResourceRegistry::Initialize()
    {
        {
            Thread::ScopedLock lock(m_Mutex);
            if (m_bInitialized)
            {
                return true;
            }
            if (m_SessionEpoch == std::numeric_limits<uint64_t>::max())
            {
                return false;
            }
            ++m_SessionEpoch;
            m_NextResourceId.Store(1);
            m_bInitialized = true;
        }

        NORVES_LOG_INFO("ResourceRegistry", "ResourceRegistry initialized");
        return true;
    }

    void ResourceRegistry::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        UnloadAll();

        {
            Thread::ScopedLock lock(m_Mutex);
            m_TypePools.clear();
            m_bInitialized = false;
        }

        NORVES_LOG_INFO("ResourceRegistry", "ResourceRegistry shutdown");
    }

    uint64_t ResourceRegistry::GenerateResourceId()
    {
        return m_NextResourceId.FetchAdd(1);
    }

    size_t ResourceRegistry::CollectGarbage()
    {
        size_t removedCount = 0;

        Thread::ScopedLock lock(m_Mutex);
        for (auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                removedCount += pair.second->CollectGarbage();
            }
        }

        if (removedCount > 0)
        {
            NORVES_LOG_DEBUG("ResourceRegistry", "Garbage collected %zu resources", removedCount);
        }

        return removedCount;
    }

    void ResourceRegistry::UnloadAll()
    {
        Thread::ScopedLock lock(m_Mutex);
        for (auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                pair.second->UnloadAll();
            }
        }

        NORVES_LOG_INFO("ResourceRegistry", "All resources unloaded");
    }

    size_t ResourceRegistry::GetResourceCount() const
    {
        size_t count = 0;

        Thread::ScopedLock lock(m_Mutex);
        for (const auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                count += pair.second->GetResourceCount();
            }
        }

        return count;
    }

    size_t ResourceRegistry::GetCachedPathCount() const
    {
        size_t count = 0;

        Thread::ScopedLock lock(m_Mutex);
        for (const auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                count += pair.second->GetCachedPathCount();
            }
        }

        return count;
    }

    size_t ResourceRegistry::GetTotalMemoryUsage() const
    {
        size_t totalSize = 0;

        Thread::ScopedLock lock(m_Mutex);
        for (const auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                totalSize += pair.second->GetTotalMemoryUsage();
            }
        }

        return totalSize;
    }

    Container::VariableArray<ResourceRecord> ResourceRegistry::GetRecords() const
    {
        Container::VariableArray<ResourceRecord> records;

        Thread::ScopedLock lock(m_Mutex);
        for (const auto &pair : m_TypePools)
        {
            if (pair.second)
            {
                pair.second->AppendRecords(records);
            }
        }

        return records;
    }

} // namespace NorvesLib::Core
