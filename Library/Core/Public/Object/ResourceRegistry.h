#pragma once

#include "Object/Resource.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Thread/Mutex.h"
#include "Thread/Atomic.h"
#include "Text/IdentityPool.h"
#include <cstdint>
#include <limits>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace NorvesLib::Core
{
    struct ResourceRecord
    {
        ResourceId Id = 0;
        uint32_t Generation = 0;
        Container::String URI;
        ResourceType Type;
        ResourceLoadState LoadState = ResourceState::Unloaded;
        uint64_t VersionHash = 0;
        size_t DependencyCount = 0;
        size_t MemoryUsage = 0;
    };

    /**
     * @brief Typed handle for fast CPU Resource lookup.
     */
    template <typename T>
    struct ResourceHandle
    {
        static constexpr uint32_t InvalidIndex = std::numeric_limits<uint32_t>::max();

        uint32_t Index = InvalidIndex;
        uint32_t Generation = 0;
        uint64_t ResourceId = 0;

        bool IsValid() const
        {
            return Index != InvalidIndex && Generation != 0 && ResourceId != 0;
        }

        explicit operator bool() const { return IsValid(); }

        bool operator==(const ResourceHandle &other) const
        {
            return Index == other.Index &&
                   Generation == other.Generation &&
                   ResourceId == other.ResourceId;
        }

        bool operator!=(const ResourceHandle &other) const
        {
            return !(*this == other);
        }

        static ResourceHandle Invalid()
        {
            return {};
        }
    };

    /**
     * @brief CPU Resource registry.
     *
     * Resources are stored in per-type pools and resolved by generation handles.
     * ResourceRef can keep shared ownership while handles provide stable lookup.
     */
    class ResourceRegistry
    {
    private:
        class IResourcePool
        {
        public:
            virtual ~IResourcePool() = default;
            virtual size_t CollectGarbage() = 0;
            virtual void UnloadAll() = 0;
            virtual size_t GetResourceCount() const = 0;
            virtual size_t GetCachedPathCount() const = 0;
            virtual size_t GetTotalMemoryUsage() const = 0;
            virtual void AppendRecords(Container::VariableArray<ResourceRecord> &outRecords) const = 0;
        };

        template <typename T>
        class TResourcePool final : public IResourcePool
        {
        public:
            struct Slot
            {
                Container::TSharedPtr<T> Resource;
                Identity Path;
                uint64_t ResourceId = 0;
                uint32_t Generation = 1;
                bool bOccupied = false;
            };

            ResourceHandle<T> Register(Container::TSharedPtr<T> resource, const Container::String &path)
            {
                if (!resource)
                {
                    return ResourceHandle<T>::Invalid();
                }

                const uint64_t resourceId = resource->GetResourceId();
                auto idIt = m_IdToIndex.find(resourceId);
                if (idIt != m_IdToIndex.end())
                {
                    Slot &slot = m_Slots[idIt->second];
                    slot.Resource = resource;
                    return MakeHandle(idIt->second, slot);
                }

                uint32_t index = 0;
                if (!m_FreeList.empty())
                {
                    index = m_FreeList.back();
                    m_FreeList.pop_back();
                }
                else
                {
                    index = static_cast<uint32_t>(m_Slots.size());
                    m_Slots.push_back(Slot{});
                }

                Slot &slot = m_Slots[index];
                slot.Resource = std::move(resource);
                slot.ResourceId = resourceId;
                slot.Path = path.empty() ? Identity() : Identity(path);
                slot.bOccupied = true;
                if (slot.Generation == 0)
                {
                    slot.Generation = 1;
                }

                m_IdToIndex[slot.ResourceId] = index;
                if (slot.Path.IsValid())
                {
                    m_PathToIndex[slot.Path] = index;
                }

                return MakeHandle(index, slot);
            }

            Container::TSharedPtr<T> Resolve(ResourceHandle<T> handle) const
            {
                if (!IsHandleAlive(handle))
                {
                    return nullptr;
                }
                return m_Slots[handle.Index].Resource;
            }

            ResourceHandle<T> FindByPath(const Identity &path) const
            {
                auto it = m_PathToIndex.find(path);
                if (it == m_PathToIndex.end())
                {
                    return ResourceHandle<T>::Invalid();
                }

                const uint32_t index = it->second;
                if (index >= m_Slots.size())
                {
                    return ResourceHandle<T>::Invalid();
                }

                const Slot &slot = m_Slots[index];
                if (!slot.bOccupied || !slot.Resource)
                {
                    return ResourceHandle<T>::Invalid();
                }

                return MakeHandle(index, slot);
            }

            ResourceHandle<T> FindById(uint64_t resourceId) const
            {
                auto it = m_IdToIndex.find(resourceId);
                if (it == m_IdToIndex.end())
                {
                    return ResourceHandle<T>::Invalid();
                }

                const uint32_t index = it->second;
                if (index >= m_Slots.size())
                {
                    return ResourceHandle<T>::Invalid();
                }

                const Slot &slot = m_Slots[index];
                if (!slot.bOccupied || !slot.Resource)
                {
                    return ResourceHandle<T>::Invalid();
                }

                return MakeHandle(index, slot);
            }

            size_t CollectGarbage() override
            {
                size_t removedCount = 0;
                for (uint32_t index = 0; index < m_Slots.size(); ++index)
                {
                    Slot &slot = m_Slots[index];
                    if (!slot.bOccupied || !slot.Resource)
                    {
                        continue;
                    }

                    if (slot.Resource.use_count() <= 1)
                    {
                        ReleaseSlot(index);
                        ++removedCount;
                    }
                }
                return removedCount;
            }

            void UnloadAll() override
            {
                for (uint32_t index = 0; index < m_Slots.size(); ++index)
                {
                    Slot &slot = m_Slots[index];
                    if (slot.bOccupied)
                    {
                        ReleaseSlot(index);
                    }
                }
            }

            size_t GetResourceCount() const override
            {
                size_t count = 0;
                for (const Slot &slot : m_Slots)
                {
                    if (slot.bOccupied && slot.Resource)
                    {
                        ++count;
                    }
                }
                return count;
            }

            size_t GetCachedPathCount() const override
            {
                return m_PathToIndex.size();
            }

            size_t GetTotalMemoryUsage() const override
            {
                size_t totalSize = 0;
                for (const Slot &slot : m_Slots)
                {
                    if (slot.bOccupied && slot.Resource)
                    {
                        totalSize += slot.Resource->GetMemorySize();
                    }
                }
                return totalSize;
            }

            void AppendRecords(Container::VariableArray<ResourceRecord> &outRecords) const override
            {
                for (const Slot &slot : m_Slots)
                {
                    if (!slot.bOccupied || !slot.Resource)
                    {
                        continue;
                    }

                    const ResourceMetadata &metadata = slot.Resource->GetMetadata();
                    ResourceRecord record;
                    record.Id = slot.ResourceId;
                    record.Generation = slot.Generation;
                    record.URI = metadata.URI;
                    record.Type = metadata.Type;
                    record.LoadState = slot.Resource->GetLoadState();
                    record.VersionHash = metadata.VersionHash;
                    record.DependencyCount = metadata.Dependencies.size();
                    record.MemoryUsage = slot.Resource->GetMemorySize();
                    outRecords.push_back(std::move(record));
                }
            }

        private:
            static ResourceHandle<T> MakeHandle(uint32_t index, const Slot &slot)
            {
                ResourceHandle<T> handle;
                handle.Index = index;
                handle.Generation = slot.Generation;
                handle.ResourceId = slot.ResourceId;
                return handle;
            }

            bool IsHandleAlive(ResourceHandle<T> handle) const
            {
                if (!handle.IsValid() || handle.Index >= m_Slots.size())
                {
                    return false;
                }

                const Slot &slot = m_Slots[handle.Index];
                return slot.bOccupied &&
                       slot.Generation == handle.Generation &&
                       slot.ResourceId == handle.ResourceId &&
                       slot.Resource != nullptr;
            }

            void ReleaseSlot(uint32_t index)
            {
                Slot &slot = m_Slots[index];
                if (slot.Path.IsValid())
                {
                    m_PathToIndex.erase(slot.Path);
                }
                if (slot.ResourceId != 0)
                {
                    m_IdToIndex.erase(slot.ResourceId);
                }

                if (slot.Resource)
                {
                    slot.Resource->Unload();
                    slot.Resource.reset();
                }

                slot.Path = Identity();
                slot.ResourceId = 0;
                slot.bOccupied = false;
                ++slot.Generation;
                if (slot.Generation == 0)
                {
                    slot.Generation = 1;
                }
                m_FreeList.push_back(index);
            }

            Container::VariableArray<Slot> m_Slots;
            Container::VariableArray<uint32_t> m_FreeList;
            Container::UnorderedMap<Identity, uint32_t, Identity::Hasher> m_PathToIndex;
            Container::UnorderedMap<uint64_t, uint32_t> m_IdToIndex;
        };

    public:
        ResourceRegistry() = default;
        ~ResourceRegistry();

        bool Initialize();
        void Shutdown();
        bool IsInitialized() const { return m_bInitialized; }

        template <typename T>
        ResourceHandle<T> LoadHandle(const Container::String &path)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            ResourceHandle<T> cachedHandle = FindHandle<T>(path);
            if (cachedHandle.IsValid())
            {
                return cachedHandle;
            }

            auto resource = CreateResource<T>(path);
            if (resource && resource->Load())
            {
                return Register<T>(resource, path);
            }

            return ResourceHandle<T>::Invalid();
        }

        template <typename T>
        Container::TSharedPtr<T> Load(const Container::String &path)
        {
            return Resolve(LoadHandle<T>(path));
        }

        template <typename T>
        ResourceHandle<T> Register(Container::TSharedPtr<T> resource, const Container::String &path = "")
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            if (!resource)
            {
                return ResourceHandle<T>::Invalid();
            }

            if (resource->GetResourceId() == 0)
            {
                resource->SetResourceId(GenerateResourceId());
            }
            if (!path.empty())
            {
                resource->SetResourcePath(path);
                resource->SetResourceName(Identity(path));
            }
            else if (!resource->GetResourceName().IsValid())
            {
                resource->SetResourceName(Identity("TransientResource"));
            }

            Thread::ScopedLock lock(m_Mutex);
            return GetOrCreatePool<T>().Register(resource, path);
        }

        template <typename T>
        Container::TSharedPtr<T> Resolve(ResourceHandle<T> handle) const
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            Thread::ScopedLock lock(m_Mutex);
            const auto *pool = FindPool<T>();
            return pool ? pool->Resolve(handle) : nullptr;
        }

        template <typename T>
        ResourceHandle<T> GetHandle(uint64_t resourceId) const
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            Thread::ScopedLock lock(m_Mutex);
            const auto *pool = FindPool<T>();
            return pool ? pool->FindById(resourceId) : ResourceHandle<T>::Invalid();
        }

        template <typename T>
        Container::TSharedPtr<T> Get(uint64_t resourceId)
        {
            return Resolve(GetHandle<T>(resourceId));
        }

        template <typename T>
        ResourceHandle<T> FindHandle(const Container::String &path) const
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            Identity pathId(path);
            Thread::ScopedLock lock(m_Mutex);
            const auto *pool = FindPool<T>();
            return pool ? pool->FindByPath(pathId) : ResourceHandle<T>::Invalid();
        }

        template <typename T>
        Container::TSharedPtr<T> Find(const Container::String &path)
        {
            return Resolve(FindHandle<T>(path));
        }

        template <typename T>
        Container::TSharedPtr<T> CreateResource(const Container::String &path)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            auto resource = Container::MakeShared<T>();
            if (resource)
            {
                resource->SetResourceId(GenerateResourceId());
                resource->SetResourcePath(path);
                resource->SetResourceName(Identity(path));
                resource->Initialize();
            }
            return resource;
        }

        template <typename T>
        ResourceHandle<T> CreateTransientHandle(const Container::String &name)
        {
            auto resource = CreateTransient<T>(name);
            return resource ? GetHandle<T>(resource->GetResourceId()) : ResourceHandle<T>::Invalid();
        }

        template <typename T>
        Container::TSharedPtr<T> CreateTransient(const Container::String &name)
        {
            static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

            auto resource = Container::MakeShared<T>();
            if (resource)
            {
                resource->SetResourceId(GenerateResourceId());
                resource->SetResourceName(Identity(name));
                resource->Initialize();
                Register<T>(resource);
            }
            return resource;
        }

        size_t CollectGarbage();
        void UnloadAll();

        size_t GetResourceCount() const;
        size_t GetCachedPathCount() const;
        size_t GetTotalMemoryUsage() const;
        Container::VariableArray<ResourceRecord> GetRecords() const;

    private:
        template <typename T>
        TResourcePool<T> &GetOrCreatePool()
        {
            const std::type_index typeKey(typeid(T));
            auto it = m_TypePools.find(typeKey);
            if (it != m_TypePools.end())
            {
                return *static_cast<TResourcePool<T> *>(it->second.get());
            }

            auto pool = Container::MakeUnique<TResourcePool<T>>();
            TResourcePool<T> *poolPtr = pool.get();
            m_TypePools.emplace(typeKey, std::move(pool));
            return *poolPtr;
        }

        template <typename T>
        const TResourcePool<T> *FindPool() const
        {
            const std::type_index typeKey(typeid(T));
            auto it = m_TypePools.find(typeKey);
            if (it == m_TypePools.end())
            {
                return nullptr;
            }
            return static_cast<const TResourcePool<T> *>(it->second.get());
        }

        uint64_t GenerateResourceId();

        bool m_bInitialized = false;
        Thread::Atomic<uint64_t> m_NextResourceId{1};
        Container::UnorderedMap<std::type_index, Container::TUniquePtr<IResourcePool>> m_TypePools;
        mutable Thread::Mutex m_Mutex;

        ResourceRegistry(const ResourceRegistry &) = delete;
        ResourceRegistry &operator=(const ResourceRegistry &) = delete;
    };

} // namespace NorvesLib::Core
