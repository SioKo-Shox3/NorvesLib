#include "Resource/ModelHandleCache.h"

namespace NorvesLib::Core::Resource
{
    ModelCacheAcquireResult ModelHandleCache::Acquire(const Container::String& key, bool bAddExternalLease)
    {
        Thread::ScopedLock lock(m_Mutex);
        const Identity cacheIdentity(key);
        auto it = m_Current.find(cacheIdentity);
        if (it == m_Current.end())
        {
            return {};
        }

        if (bAddExternalLease)
        {
            ++it->second.ExternalLeaseCount;
        }
        return {true, it->second.Handle};
    }

    ModelCachePublishResult ModelHandleCache::PublishOrAcquire(
        const Container::String& key,
        Rendering::ModelHandle candidate,
        uint32_t externalLeaseCount)
    {
        ModelCachePublishResult result;
        if (!candidate.IsValid())
        {
            return result;
        }

        Thread::ScopedLock lock(m_Mutex);
        const Identity cacheIdentity(key);
        auto it = m_Current.find(cacheIdentity);
        if (it != m_Current.end())
        {
            it->second.ExternalLeaseCount += externalLeaseCount;
            result.ResolvedHandle = it->second.Handle;
            result.HandlesToRelease.push_back(candidate);
            m_ManagedIds.insert(candidate.Id);
            return result;
        }

        m_Current[cacheIdentity] = Entry{candidate, externalLeaseCount};
        m_ManagedIds.insert(candidate.Id);
        result.ResolvedHandle = candidate;
        result.bPublished = true;
        return result;
    }

    ModelCacheHandleBatch ModelHandleCache::RetireCurrent()
    {
        ModelCacheHandleBatch batch;
        Thread::ScopedLock lock(m_Mutex);
        for (const auto& [key, entry] : m_Current)
        {
            if (entry.ExternalLeaseCount == 0)
            {
                batch.HandlesToRelease.push_back(entry.Handle);
            }
            else
            {
                m_Retired[entry.Handle.Id] = entry;
            }
        }
        m_Current.clear();
        return batch;
    }

    ModelCacheReleaseResult ModelHandleCache::ReleaseLease(Rendering::ModelHandle handle)
    {
        if (!handle.IsValid())
        {
            return {};
        }

        Thread::ScopedLock lock(m_Mutex);
        if (!m_ManagedIds.contains(handle.Id))
        {
            return {};
        }

        for (auto& [key, entry] : m_Current)
        {
            if (entry.Handle == handle)
            {
                if (entry.ExternalLeaseCount > 0)
                {
                    --entry.ExternalLeaseCount;
                }
                return {true, Rendering::ModelHandle::Invalid()};
            }
        }

        auto retiredIt = m_Retired.find(handle.Id);
        if (retiredIt == m_Retired.end())
        {
            return {true, Rendering::ModelHandle::Invalid()};
        }

        if (retiredIt->second.ExternalLeaseCount > 0)
        {
            --retiredIt->second.ExternalLeaseCount;
        }
        if (retiredIt->second.ExternalLeaseCount != 0)
        {
            return {true, Rendering::ModelHandle::Invalid()};
        }

        Rendering::ModelHandle handleToRelease = retiredIt->second.Handle;
        m_Retired.erase(retiredIt);
        return {true, handleToRelease};
    }

    ModelCacheHandleBatch ModelHandleCache::DrainAll()
    {
        ModelCacheHandleBatch batch;
        Container::Set<uint64_t> releasedIds;
        Thread::ScopedLock lock(m_Mutex);
        for (const auto& [key, entry] : m_Current)
        {
            if (entry.Handle.IsValid() && releasedIds.insert(entry.Handle.Id).second)
            {
                batch.HandlesToRelease.push_back(entry.Handle);
            }
        }
        for (const auto& [id, entry] : m_Retired)
        {
            if (entry.Handle.IsValid() && releasedIds.insert(entry.Handle.Id).second)
            {
                batch.HandlesToRelease.push_back(entry.Handle);
            }
        }
        m_Current.clear();
        m_Retired.clear();
        return batch;
    }
} // namespace NorvesLib::Core::Resource
