#include "Object/ResourceRegistry.h"
#include "Logging/Logger.h"

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
        if (m_bInitialized)
        {
            return true;
        }

        m_NextResourceId.Store(1);
        m_bInitialized = true;

        LOG_INFO("ResourceRegistry", "ResourceRegistry initialized");
        return true;
    }

    void ResourceRegistry::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // すべてのリソースをアンロード
        UnloadAll();

        // キャッシュをクリア
        {
            Thread::ScopedLock lock(m_Mutex);
            m_PathToResource.clear();
            m_IdToResource.clear();
        }

        m_bInitialized = false;

        LOG_INFO("ResourceRegistry", "ResourceRegistry shutdown");
    }

    void ResourceRegistry::RegisterResource(Container::TSharedPtr<Resource> resource, const Container::String& path)
    {
        if (!resource)
        {
            return;
        }

        Thread::ScopedLock lock(m_Mutex);
        
        uint64_t id = resource->GetResourceId();
        
        // パスが指定されていればパスキャッシュにも登録
        if (!path.empty())
        {
            m_PathToResource[path] = resource;
        }
        
        // IDキャッシュに登録
        m_IdToResource[id] = resource;

        LOG_DEBUG("ResourceRegistry", "Registered resource: " + path + " (ID: " + Container::String(std::to_string(id)) + ")");
    }

    uint64_t ResourceRegistry::GenerateResourceId()
    {
        return m_NextResourceId.FetchAdd(1);
    }

    size_t ResourceRegistry::CollectGarbage()
    {
        size_t removedCount = 0;

        Thread::ScopedLock lock(m_Mutex);

        // パスキャッシュから無効なエントリを削除
        for (auto it = m_PathToResource.begin(); it != m_PathToResource.end(); )
        {
            if (it->second.expired())
            {
                it = m_PathToResource.erase(it);
                ++removedCount;
            }
            else
            {
                ++it;
            }
        }

        // IDキャッシュから無効なエントリを削除
        for (auto it = m_IdToResource.begin(); it != m_IdToResource.end(); )
        {
            if (it->second.expired())
            {
                it = m_IdToResource.erase(it);
                ++removedCount;
            }
            else
            {
                ++it;
            }
        }

        if (removedCount > 0)
        {
            LOG_DEBUG("ResourceRegistry", "Garbage collected " + Container::String(std::to_string(removedCount)) + " entries");
        }

        return removedCount;
    }

    void ResourceRegistry::UnloadAll()
    {
        Thread::ScopedLock lock(m_Mutex);

        // すべてのリソースをアンロード
        for (auto& pair : m_IdToResource)
        {
            if (auto resource = pair.second.lock())
            {
                resource->Unload();
            }
        }

        LOG_INFO("ResourceRegistry", "All resources unloaded");
    }

    size_t ResourceRegistry::GetResourceCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        
        size_t count = 0;
        for (const auto& pair : m_IdToResource)
        {
            if (!pair.second.expired())
            {
                ++count;
            }
        }
        return count;
    }

    size_t ResourceRegistry::GetCachedPathCount() const
    {
        Thread::ScopedLock lock(m_Mutex);
        
        size_t count = 0;
        for (const auto& pair : m_PathToResource)
        {
            if (!pair.second.expired())
            {
                ++count;
            }
        }
        return count;
    }

    size_t ResourceRegistry::GetTotalMemoryUsage() const
    {
        Thread::ScopedLock lock(m_Mutex);
        
        size_t totalSize = 0;
        for (const auto& pair : m_IdToResource)
        {
            if (auto resource = pair.second.lock())
            {
                totalSize += resource->GetMemorySize();
            }
        }
        return totalSize;
    }

} // namespace NorvesLib::Core
