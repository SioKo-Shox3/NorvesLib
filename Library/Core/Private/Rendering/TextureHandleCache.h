#pragma once

#include "Rendering/RenderTypes.h"
#include "Container/Containers.h"
#include "Thread/Mutex.h"

namespace NorvesLib::Core::Rendering
{
    class TextureHandleCache final
    {
    public:
        TextureHandle Find(const Container::String &cacheKey) const;
        void Store(const Container::String &cacheKey, TextureHandle handle);
        bool StoreIfAbsent(const Container::String &cacheKey,
                           TextureHandle candidateHandle,
                           TextureHandle *pOutResolvedHandle = nullptr);
        void Clear();

    private:
        mutable Thread::Mutex m_Mutex;
        Container::Map<Container::String, TextureHandle> m_Entries;
    };
}
