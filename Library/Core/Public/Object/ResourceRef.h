#pragma once

#include "Object/ResourceRegistry.h"

namespace NorvesLib::Core
{
    template <typename T>
    class ResourceRef
    {
    public:
        static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

        ResourceRef() = default;

        ResourceRef(ResourceRegistry &registry, ResourceHandle<T> handle)
        {
            Set(registry, handle);
        }

        bool Set(ResourceRegistry &registry, ResourceHandle<T> handle)
        {
            m_Handle = handle;
            m_Resource = registry.Resolve(handle);
            return m_Resource != nullptr;
        }

        void Set(Container::TSharedPtr<T> resource, ResourceHandle<T> handle = {})
        {
            m_Resource = std::move(resource);
            m_Handle = handle;
        }

        T *Get() const
        {
            return m_Resource.get();
        }

        Container::TSharedPtr<T> GetShared() const
        {
            return m_Resource;
        }

        ResourceHandle<T> GetHandle() const
        {
            return m_Handle;
        }

        bool IsValid() const
        {
            return m_Resource != nullptr;
        }

        explicit operator bool() const
        {
            return IsValid();
        }

        void Reset()
        {
            m_Resource.reset();
            m_Handle = {};
        }

    private:
        ResourceHandle<T> m_Handle;
        Container::TSharedPtr<T> m_Resource;
    };

} // namespace NorvesLib::Core
