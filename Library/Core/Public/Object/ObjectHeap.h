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
        struct GCStats
        {
            size_t MarkedObjects = 0;
            size_t SweptObjects = 0;
            size_t DestroyQueuedObjects = 0;
        };

        struct GCBudget
        {
            size_t MaxObjectsPerFrame = 1000;
            double MaxTimeMs = 0.5;
        };

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
        GCStats CollectGarbage();
        GCStats CollectIncremental(GCBudget budget);
        bool IsIncrementalGCActive() const;
        void ResetIncrementalGC();
        Container::String DumpGCState() const;
        void DestroyAll();

        bool AddRoot(ObjectHandle handle);
        bool RemoveRoot(ObjectHandle handle);
        bool AddExternalRoot(ObjectHandle handle);
        bool RemoveExternalRoot(ObjectHandle handle);
        bool PinObject(ObjectHandle handle);
        bool UnpinObject(ObjectHandle handle);

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
            bool bMarked = false;
        };

        ObjectHandle Adopt(Object *object);
        bool IsHandleAlive(ObjectHandle handle) const;
        bool ReleaseSlot(uint32_t index);
        uint32_t AllocateSlot();
        static ObjectHandle MakeHandle(uint32_t index, const Slot &slot);
        bool AddUniqueHandle(Container::VariableArray<ObjectHandle> &handles, ObjectHandle handle);
        bool RemoveHandle(Container::VariableArray<ObjectHandle> &handles, ObjectHandle handle);
        bool MarkObject(ObjectHandle handle, size_t &markedCount);
        void ClearMarks();
        void StartIncrementalGC();
        void PushRootSet(Container::VariableArray<ObjectHandle> &stack) const;
        void PushReferences(Object *object, Container::VariableArray<ObjectHandle> &stack) const;

        Container::VariableArray<Slot> m_Slots;
        Container::VariableArray<uint32_t> m_FreeList;
        Container::VariableArray<uint32_t> m_DestroyQueue;
        Container::VariableArray<ObjectHandle> m_Roots;
        Container::VariableArray<ObjectHandle> m_ExternalRoots;
        Container::VariableArray<ObjectHandle> m_PinnedObjects;
        Container::UnorderedMap<const Object *, uint32_t> m_ObjectToIndex;

        enum class IncrementalPhase : uint8_t
        {
            Idle,
            Mark,
            Sweep
        };

        struct IncrementalState
        {
            IncrementalPhase Phase = IncrementalPhase::Idle;
            Container::VariableArray<ObjectHandle> MarkStack;
            uint32_t SweepIndex = 0;
            GCStats Stats;
        };

        IncrementalState m_Incremental;
    };

} // namespace NorvesLib::Core
