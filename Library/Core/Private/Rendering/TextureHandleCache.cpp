#include "Rendering/TextureHandleCache.h"

namespace NorvesLib::Core::Rendering
{
    TextureHandle TextureHandleCache::Find(const Container::String &cacheKey) const
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Entries.find(cacheKey);
        return it != m_Entries.end() ? it->second : TextureHandle::Invalid();
    }

    void TextureHandleCache::Store(const Container::String &cacheKey, TextureHandle handle)
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Entries[cacheKey] = handle;
    }

    bool TextureHandleCache::StoreIfAbsent(const Container::String &cacheKey,
                                           TextureHandle candidateHandle,
                                           TextureHandle *pOutResolvedHandle)
    {
        Thread::ScopedLock lock(m_Mutex);
        auto it = m_Entries.find(cacheKey);
        if (it != m_Entries.end())
        {
            if (pOutResolvedHandle)
            {
                *pOutResolvedHandle = it->second;
            }
            return false;
        }

        m_Entries[cacheKey] = candidateHandle;
        if (pOutResolvedHandle)
        {
            *pOutResolvedHandle = candidateHandle;
        }
        return true;
    }

    void TextureHandleCache::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Entries.clear();
    }
}
