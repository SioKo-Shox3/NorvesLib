#include "Resource/FontFaceCache.h"

namespace NorvesLib::Core
{
    Asset::AssetBlob FontFaceCache::Acquire(Container::AnsiStringView logicalPath,
                                            const Asset::AssetFileReader& reader)
    {
        Thread::ScopedLock lock(m_Mutex);
        for (const Entry& entry : m_Entries)
        {
            if (Container::AnsiStringView(entry.LogicalPath) == logicalPath)
            {
                return entry.Blob;
            }
        }

        const Asset::AssetReadResult result = reader.Read(logicalPath);
        if (!result.Succeeded())
        {
            return {};
        }

        m_Entries.push_back({Container::AnsiString(logicalPath), result.Blob});
        return result.Blob;
    }

    void FontFaceCache::Clear()
    {
        Thread::ScopedLock lock(m_Mutex);
        m_Entries.clear();
    }

    size_t FontFaceCache::GetCachedCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        return m_Entries.size();
    }
} // namespace NorvesLib::Core
