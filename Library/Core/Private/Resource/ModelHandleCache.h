#pragma once

#include "Container/Containers.h"
#include "Rendering/RenderTypes.h"
#include "Text/IdentityPool.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core::Resource
{
    struct ModelCacheAcquireResult
    {
        bool bFound = false;
        Rendering::ModelHandle Handle = Rendering::ModelHandle::Invalid();
    };

    struct ModelCachePublishResult
    {
        Rendering::ModelHandle ResolvedHandle = Rendering::ModelHandle::Invalid();
        bool bPublished = false;
        Container::VariableArray<Rendering::ModelHandle> HandlesToRelease;
    };

    struct ModelCacheReleaseResult
    {
        bool bManaged = false;
        Rendering::ModelHandle HandleToRelease = Rendering::ModelHandle::Invalid();
    };

    struct ModelCacheHandleBatch
    {
        Container::VariableArray<Rendering::ModelHandle> HandlesToRelease;
    };

    class ModelHandleCache final
    {
    public:
        ModelCacheAcquireResult Acquire(const Container::String& key, bool bAddExternalLease);
        ModelCachePublishResult PublishOrAcquire(const Container::String& key,
                                                 Rendering::ModelHandle candidate,
                                                 uint32_t externalLeaseCount);
        ModelCacheHandleBatch RetireCurrent();
        ModelCacheReleaseResult ReleaseLease(Rendering::ModelHandle handle);
        ModelCacheHandleBatch DrainAll();

    private:
        struct Entry
        {
            Rendering::ModelHandle Handle = Rendering::ModelHandle::Invalid();
            uint32_t ExternalLeaseCount = 0;
        };

        mutable Thread::Mutex m_Mutex;
        Container::UnorderedMap<Identity, Entry, Identity::Hasher> m_Current;
        Container::Map<uint64_t, Entry> m_Retired;
        Container::Set<uint64_t> m_ManagedIds;
    };
} // namespace NorvesLib::Core::Resource
