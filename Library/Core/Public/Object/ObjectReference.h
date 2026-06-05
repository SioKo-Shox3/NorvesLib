#pragma once

#include "Object/ObjectHeap.h"
#include "Object/ReferenceCollector.h"
#include "Object/RuntimeSchema.h"
#include "Container/Containers.h"
#include <type_traits>

namespace NorvesLib::Core
{
    template <typename T>
    class ObjectRef
    {
    public:
        ObjectRef()
        {
            EnsureTypeRegistered();
        }

        ObjectRef(ObjectHeap *heap, ObjectHandle handle)
            : m_Heap(heap), m_Handle(handle)
        {
            EnsureTypeRegistered();
        }

        T *Resolve() const
        {
            return m_Heap ? m_Heap->Resolve<T>(m_Handle) : nullptr;
        }

        ObjectHandle GetHandle() const { return m_Handle; }
        ObjectHeap *GetHeap() const { return m_Heap; }
        bool IsValid() const { return Resolve() != nullptr; }
        explicit operator bool() const { return IsValid(); }

        void Reset()
        {
            m_Heap = nullptr;
            m_Handle = {};
        }

        void AddReferencedObjects(ReferenceCollector &collector) const
        {
            if (m_Handle)
            {
                collector.Add(m_Handle);
            }
        }

        static TypeId GetTypeId()
        {
            return EnsureTypeRegistered();
        }

    private:
        static TypeId EnsureTypeRegistered()
        {
            TypeOps ops;
            ops.AddReferences = [](const void *value, ReferenceCollector &collector)
            {
                static_cast<const ObjectRef<T> *>(value)->AddReferencedObjects(collector);
            };

            Container::String typeName = "ObjectRef<";
            typeName += T::StaticClass()->GetClassName().ToString();
            typeName += ">";
            return TypeRegistry::Get().Register<ObjectRef<T>>(typeName, TypeKind::Object, ops);
        }

        ObjectHeap *m_Heap = nullptr;
        ObjectHandle m_Handle;
    };

    template <typename T>
    class WeakObjectRef
    {
    public:
        WeakObjectRef() = default;
        WeakObjectRef(ObjectHeap *heap, ObjectHandle handle)
            : m_Heap(heap), m_Handle(handle)
        {
        }

        T *Resolve() const
        {
            return m_Heap ? m_Heap->Resolve<T>(m_Handle) : nullptr;
        }

        ObjectHandle GetHandle() const { return m_Handle; }
        ObjectHeap *GetHeap() const { return m_Heap; }
        bool IsValid() const { return Resolve() != nullptr; }
        explicit operator bool() const { return IsValid(); }

        void Reset()
        {
            m_Heap = nullptr;
            m_Handle = {};
        }

    private:
        ObjectHeap *m_Heap = nullptr;
        ObjectHandle m_Handle;
    };

} // namespace NorvesLib::Core
