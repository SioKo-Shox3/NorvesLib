#include "Object/ObjectHeap.h"
#include "Object/IClass.h"
#include "Object/ObjectUtility.h"

namespace NorvesLib::Core
{
    ObjectHeap::~ObjectHeap()
    {
        DestroyAll();
    }

    ObjectHandle ObjectHeap::Create(const IClass *cls, IUnknown *outer)
    {
        if (!cls)
        {
            return {};
        }

        IUnknown *unknown = cls->NewInstance(outer);
        Object *object = ObjectUtility::CastTo<Object>(unknown);
        if (!object)
        {
            delete unknown;
            return {};
        }

        ObjectHandle handle = Adopt(object);
        if (!handle)
        {
            delete object;
            return {};
        }

        if (outer && !outer->AddInner(object))
        {
            DestroyNow(handle);
            return {};
        }

        return handle;
    }

    bool ObjectHeap::EnqueueDestroy(ObjectHandle handle)
    {
        if (!IsHandleAlive(handle))
        {
            return false;
        }

        const uint32_t index = static_cast<uint32_t>(handle.Id - 1);
        Slot &slot = m_Slots[index];
        if (slot.State == SlotState::DestroyQueued)
        {
            return true;
        }

        slot.State = SlotState::DestroyQueued;
        slot.Instance->SetFlag(OF_PendingDestroy, true);
        m_DestroyQueue.push_back(index);
        return true;
    }

    bool ObjectHeap::DestroyNow(ObjectHandle handle)
    {
        if (!IsHandleAlive(handle))
        {
            return false;
        }

        return ReleaseSlot(static_cast<uint32_t>(handle.Id - 1));
    }

    size_t ObjectHeap::ProcessDestroyQueue()
    {
        size_t destroyedCount = 0;
        Container::VariableArray<uint32_t> pending;
        pending.swap(m_DestroyQueue);

        for (uint32_t index : pending)
        {
            if (index < m_Slots.size() && m_Slots[index].State == SlotState::DestroyQueued)
            {
                if (ReleaseSlot(index))
                {
                    ++destroyedCount;
                }
            }
        }

        return destroyedCount;
    }

    void ObjectHeap::DestroyAll()
    {
        m_DestroyQueue.clear();
        for (uint32_t index = 0; index < m_Slots.size(); ++index)
        {
            if (m_Slots[index].State != SlotState::Free)
            {
                ReleaseSlot(index);
            }
        }
    }

    Object *ObjectHeap::Resolve(ObjectHandle handle) const
    {
        return IsHandleAlive(handle) ? m_Slots[static_cast<uint32_t>(handle.Id - 1)].Instance : nullptr;
    }

    ObjectHandle ObjectHeap::GetHandle(const Object *object) const
    {
        auto it = m_ObjectToIndex.find(object);
        if (it == m_ObjectToIndex.end())
        {
            return {};
        }

        const uint32_t index = it->second;
        if (index >= m_Slots.size())
        {
            return {};
        }

        const Slot &slot = m_Slots[index];
        if (slot.Instance != object || slot.State == SlotState::Free)
        {
            return {};
        }

        return MakeHandle(index, slot);
    }

    size_t ObjectHeap::GetLiveObjectCount() const
    {
        size_t count = 0;
        for (const Slot &slot : m_Slots)
        {
            if (slot.State != SlotState::Free && slot.Instance)
            {
                ++count;
            }
        }
        return count;
    }

    ObjectHandle ObjectHeap::Adopt(Object *object)
    {
        if (!object)
        {
            return {};
        }

        const uint32_t index = AllocateSlot();
        Slot &slot = m_Slots[index];
        slot.Instance = object;
        slot.State = SlotState::Alive;

        object->SetFlag(OF_HeapOwned, true);
        object->SetFlag(OF_PendingDestroy, false);
        if (!object->HasFlag(OF_Initialized))
        {
            object->Initialize();
        }

        m_ObjectToIndex[object] = index;
        return MakeHandle(index, slot);
    }

    bool ObjectHeap::IsHandleAlive(ObjectHandle handle) const
    {
        if (!handle.IsValid())
        {
            return false;
        }

        const uint64_t index64 = handle.Id - 1;
        if (index64 > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        const uint32_t index = static_cast<uint32_t>(index64);
        if (index >= m_Slots.size())
        {
            return false;
        }

        const Slot &slot = m_Slots[index];
        return slot.State != SlotState::Free &&
               slot.Instance != nullptr &&
               slot.Generation == handle.Generation;
    }

    bool ObjectHeap::ReleaseSlot(uint32_t index)
    {
        if (index >= m_Slots.size())
        {
            return false;
        }

        Slot &slot = m_Slots[index];
        if (slot.State == SlotState::Free || !slot.Instance)
        {
            return false;
        }

        Object *object = slot.Instance;
        m_ObjectToIndex.erase(object);

        object->SetFlag(OF_HeapOwned, false);
        object->SetFlag(OF_PendingDestroy, true);
        object->Finalize();
        delete object;

        slot.Instance = nullptr;
        slot.State = SlotState::Free;
        ++slot.Generation;
        if (slot.Generation == 0)
        {
            slot.Generation = 1;
        }
        m_FreeList.push_back(index);
        return true;
    }

    uint32_t ObjectHeap::AllocateSlot()
    {
        if (!m_FreeList.empty())
        {
            const uint32_t index = m_FreeList.back();
            m_FreeList.pop_back();
            return index;
        }

        const uint32_t index = static_cast<uint32_t>(m_Slots.size());
        m_Slots.push_back(Slot{});
        return index;
    }

    ObjectHandle ObjectHeap::MakeHandle(uint32_t index, const Slot &slot)
    {
        return ObjectHandle{
            .Id = static_cast<ObjectId>(index) + 1,
            .Generation = slot.Generation};
    }

} // namespace NorvesLib::Core
