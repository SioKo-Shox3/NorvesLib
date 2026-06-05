#pragma once

#include "Object/Object.h"
#include "Object/ObjectHandle.h"
#include "Container/Containers.h"
#include <cstdint>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core
{
    class ObjectHeap
    {
    public:
        ObjectHeap() = default;
        ~ObjectHeap();

        template <typename T, typename... Args>
        ObjectHandle Create(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>, "T must derive from Object");

            T *object = new T(std::forward<Args>(args)...);
            return Adopt(object);
        }

        ObjectHandle Create(const IClass *cls, IUnknown *outer = nullptr);
        bool EnqueueDestroy(ObjectHandle handle);
        bool DestroyNow(ObjectHandle handle);
        size_t ProcessDestroyQueue();
        void DestroyAll();

        Object *Resolve(ObjectHandle handle) const;

        template <typename T>
        T *Resolve(ObjectHandle handle) const
        {
            static_assert(std::is_base_of_v<Object, T>, "T must derive from Object");
            return ObjectUtility::CastTo<T>(Resolve(handle));
        }

        ObjectHandle GetHandle(const Object *object) const;
        size_t GetLiveObjectCount() const;
        size_t GetAllocatedSlotCount() const { return m_Slots.size(); }

    private:
        enum class SlotState : uint8_t
        {
            Free,
            Alive,
            DestroyQueued
        };

        struct Slot
        {
            Object *Instance = nullptr;
            uint32_t Generation = 1;
            SlotState State = SlotState::Free;
        };

        ObjectHandle Adopt(Object *object);
        bool IsHandleAlive(ObjectHandle handle) const;
        bool ReleaseSlot(uint32_t index);
        uint32_t AllocateSlot();
        static ObjectHandle MakeHandle(uint32_t index, const Slot &slot);

        Container::VariableArray<Slot> m_Slots;
        Container::VariableArray<uint32_t> m_FreeList;
        Container::VariableArray<uint32_t> m_DestroyQueue;
        Container::UnorderedMap<const Object *, uint32_t> m_ObjectToIndex;
    };

} // namespace NorvesLib::Core
