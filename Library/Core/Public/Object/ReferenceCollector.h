#pragma once

#include "Object/ObjectHandle.h"
#include "Container/Containers.h"

namespace NorvesLib::Core
{
    class ReferenceCollector
    {
    public:
        void Add(ObjectHandle handle)
        {
            if (handle)
            {
                m_ObjectHandles.push_back(handle);
            }
        }

        const Container::VariableArray<ObjectHandle> &GetObjectHandles() const
        {
            return m_ObjectHandles;
        }

        bool Contains(ObjectHandle handle) const
        {
            for (ObjectHandle existing : m_ObjectHandles)
            {
                if (existing == handle)
                {
                    return true;
                }
            }
            return false;
        }

        void Clear()
        {
            m_ObjectHandles.clear();
        }

    private:
        Container::VariableArray<ObjectHandle> m_ObjectHandles;
    };

} // namespace NorvesLib::Core
