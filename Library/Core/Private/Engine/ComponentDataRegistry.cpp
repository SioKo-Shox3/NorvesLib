#include "Engine/ComponentDataRegistry.h"
#include "Object/Entity.h"
#include "Object/World.h"

namespace NorvesLib::Core
{
    bool ComponentDataRegistry::IsAvailable() const
    {
        return NORVES_ENABLE_COMPONENT_DATA_REGISTRY != 0;
    }

    bool ComponentDataRegistry::IsEnabled() const
    {
        return IsAvailable() && m_bEnabled;
    }

    bool ComponentDataRegistry::SetEnabled(bool bEnabled)
    {
        if (!IsAvailable())
        {
            m_bEnabled = false;
            BeginFrameCapture();
            return false;
        }

        m_bEnabled = bEnabled;
        if (!m_bEnabled)
        {
            BeginFrameCapture();
        }
        return m_bEnabled;
    }

    EntityHandle ComponentDataRegistry::RegisterEntity(World& world, Entity& entity)
    {
        if (!IsAvailable())
        {
            entity.SetEntityHandle(EntityHandle::Invalid());
            return EntityHandle::Invalid();
        }

        EntityHandle existingHandle = entity.GetEntityHandle();
        if (IsHandleAlive(existingHandle))
        {
            return existingHandle;
        }

        auto existingIt = m_EntityToSlot.find(&entity);
        if (existingIt != m_EntityToSlot.end())
        {
            EntityHandle handle{existingIt->second, m_Slots[existingIt->second].Generation};
            if (IsHandleAlive(handle))
            {
                entity.SetEntityHandle(handle);
                return handle;
            }
        }

        const uint32_t slotIndex = AllocateSlot();
        EntitySlot& slot = m_Slots[slotIndex];
        slot.WorldPtr = &world;
        slot.EntityPtr = &entity;
        slot.bOccupied = true;

        EntityHandle handle{slotIndex, slot.Generation};
        m_EntityToSlot[&entity] = slotIndex;
        entity.SetEntityHandle(handle);
        return handle;
    }

    void ComponentDataRegistry::UnregisterEntity(Entity& entity)
    {
        if (!IsAvailable())
        {
            entity.SetEntityHandle(EntityHandle::Invalid());
            return;
        }

        EntityHandle handle = entity.GetEntityHandle();
        if (!IsHandleAlive(handle))
        {
            auto existingIt = m_EntityToSlot.find(&entity);
            if (existingIt == m_EntityToSlot.end())
            {
                entity.SetEntityHandle(EntityHandle::Invalid());
                return;
            }

            handle = EntityHandle{existingIt->second, m_Slots[existingIt->second].Generation};
            if (!IsHandleAlive(handle))
            {
                m_EntityToSlot.erase(existingIt);
                entity.SetEntityHandle(EntityHandle::Invalid());
                return;
            }
        }

        EntitySlot& slot = m_Slots[handle.SlotIndex];
        m_EntityToSlot.erase(slot.EntityPtr);
        RemovePublishedData(handle);

        slot.WorldPtr = nullptr;
        slot.EntityPtr = nullptr;
        slot.bOccupied = false;
        ++slot.Generation;
        if (slot.Generation == 0)
        {
            slot.Generation = 1;
        }
        m_FreeSlots.push_back(handle.SlotIndex);
        entity.SetEntityHandle(EntityHandle::Invalid());
    }

    void ComponentDataRegistry::UnregisterWorld(World& world)
    {
        if (!IsAvailable())
        {
            return;
        }

        for (uint32_t index = 0; index < m_Slots.size(); ++index)
        {
            EntitySlot& slot = m_Slots[index];
            if (!slot.bOccupied || slot.WorldPtr != &world)
            {
                continue;
            }

            EntityHandle handle{index, slot.Generation};
            Entity* entity = slot.EntityPtr;
            if (entity)
            {
                entity->SetEntityHandle(EntityHandle::Invalid());
                m_EntityToSlot.erase(entity);
            }
            RemovePublishedData(handle);

            slot.WorldPtr = nullptr;
            slot.EntityPtr = nullptr;
            slot.bOccupied = false;
            ++slot.Generation;
            if (slot.Generation == 0)
            {
                slot.Generation = 1;
            }
            m_FreeSlots.push_back(index);
        }
    }

    EntityHandle ComponentDataRegistry::GetHandle(const Entity& entity) const
    {
        if (!IsAvailable())
        {
            return EntityHandle::Invalid();
        }

        EntityHandle handle = entity.GetEntityHandle();
        return IsHandleAlive(handle) ? handle : EntityHandle::Invalid();
    }

    Entity* ComponentDataRegistry::ResolveEntity(EntityHandle handle) const
    {
        return IsHandleAlive(handle) ? m_Slots[handle.SlotIndex].EntityPtr : nullptr;
    }

    void ComponentDataRegistry::BeginFrameCapture()
    {
        m_TransformData.clear();
        m_MeshData.clear();
        m_MegaGeometryData.clear();
    }

    void ComponentDataRegistry::PublishTransform(Entity& entity)
    {
        if (!IsEnabled())
        {
            return;
        }

        EntityHandle handle = GetHandle(entity);
        if (!handle.IsValid())
        {
            return;
        }

        RemoveTransformData(handle);

        const Math::Transform& worldTransform = entity.GetWorldTransform();
        EntityTransformData data;
        data.Handle = handle;
        data.ObjectId = entity.GetObjectId();
        data.TransformVersion = entity.GetTransformVersion();
        data.WorldTransform = worldTransform;
        data.WorldMatrix = MakeRegistryWorldMatrix(worldTransform);
        m_TransformData.push_back(data);
    }

    void ComponentDataRegistry::PublishMeshProxy(Entity& entity, const Rendering::MeshProxy& proxy)
    {
        if (!IsEnabled() || !proxy.IsValid())
        {
            return;
        }

        EntityHandle handle = GetHandle(entity);
        if (!handle.IsValid())
        {
            return;
        }

        for (auto it = m_MeshData.begin(); it != m_MeshData.end(); ++it)
        {
            if (it->Handle == handle && it->ComponentId == proxy.ComponentId)
            {
                *it = MeshComponentData{};
                it->Handle = handle;
                it->ObjectId = proxy.ObjectId;
                it->ComponentId = proxy.ComponentId;
                it->TransformVersion = entity.GetTransformVersion();
                it->Proxy = proxy;
                it->WorldBounds = proxy.WorldBounds;
                it->LayerMask = proxy.LayerMask;
                it->bVisible = proxy.bVisible;
                return;
            }
        }

        MeshComponentData data;
        data.Handle = handle;
        data.ObjectId = proxy.ObjectId;
        data.ComponentId = proxy.ComponentId;
        data.TransformVersion = entity.GetTransformVersion();
        data.Proxy = proxy;
        data.WorldBounds = proxy.WorldBounds;
        data.LayerMask = proxy.LayerMask;
        data.bVisible = proxy.bVisible;
        m_MeshData.push_back(data);
    }

    void ComponentDataRegistry::PublishMegaGeometryProxy(Entity& entity, const Rendering::MegaGeometryProxy& proxy)
    {
        if (!IsEnabled() || !proxy.IsValid())
        {
            return;
        }

        EntityHandle handle = GetHandle(entity);
        if (!handle.IsValid())
        {
            return;
        }

        for (auto it = m_MegaGeometryData.begin(); it != m_MegaGeometryData.end(); ++it)
        {
            if (it->Handle == handle && it->ComponentId == proxy.ComponentId)
            {
                *it = MegaGeometryComponentData{};
                it->Handle = handle;
                it->ObjectId = proxy.ObjectId;
                it->ComponentId = proxy.ComponentId;
                it->TransformVersion = entity.GetTransformVersion();
                it->Proxy = proxy;
                it->WorldBounds = proxy.WorldBounds;
                it->LayerMask = proxy.LayerMask;
                it->bVisible = proxy.bVisible;
                return;
            }
        }

        MegaGeometryComponentData data;
        data.Handle = handle;
        data.ObjectId = proxy.ObjectId;
        data.ComponentId = proxy.ComponentId;
        data.TransformVersion = entity.GetTransformVersion();
        data.Proxy = proxy;
        data.WorldBounds = proxy.WorldBounds;
        data.LayerMask = proxy.LayerMask;
        data.bVisible = proxy.bVisible;
        m_MegaGeometryData.push_back(data);
    }

    Container::Span<const EntityTransformData> ComponentDataRegistry::GetTransformData() const
    {
        return Container::Span<const EntityTransformData>(m_TransformData);
    }

    Container::Span<const MeshComponentData> ComponentDataRegistry::GetMeshData() const
    {
        return Container::Span<const MeshComponentData>(m_MeshData);
    }

    Container::Span<const MegaGeometryComponentData> ComponentDataRegistry::GetMegaGeometryData() const
    {
        return Container::Span<const MegaGeometryComponentData>(m_MegaGeometryData);
    }

    bool ComponentDataRegistry::IsHandleAlive(EntityHandle handle) const
    {
        if (!handle.IsValid() || handle.SlotIndex >= m_Slots.size())
        {
            return false;
        }

        const EntitySlot& slot = m_Slots[handle.SlotIndex];
        return slot.bOccupied &&
               slot.EntityPtr != nullptr &&
               slot.Generation == handle.Generation;
    }

    void ComponentDataRegistry::RemovePublishedData(EntityHandle handle)
    {
        RemoveTransformData(handle);
        RemoveMeshData(handle);
        RemoveMegaGeometryData(handle);
    }

    void ComponentDataRegistry::RemoveTransformData(EntityHandle handle)
    {
        for (size_t index = 0; index < m_TransformData.size();)
        {
            if (m_TransformData[index].Handle == handle)
            {
                m_TransformData[index] = m_TransformData.back();
                m_TransformData.pop_back();
                continue;
            }
            ++index;
        }
    }

    void ComponentDataRegistry::RemoveMeshData(EntityHandle handle)
    {
        for (size_t index = 0; index < m_MeshData.size();)
        {
            if (m_MeshData[index].Handle == handle)
            {
                m_MeshData[index] = m_MeshData.back();
                m_MeshData.pop_back();
                continue;
            }
            ++index;
        }
    }

    void ComponentDataRegistry::RemoveMegaGeometryData(EntityHandle handle)
    {
        for (size_t index = 0; index < m_MegaGeometryData.size();)
        {
            if (m_MegaGeometryData[index].Handle == handle)
            {
                m_MegaGeometryData[index] = m_MegaGeometryData.back();
                m_MegaGeometryData.pop_back();
                continue;
            }
            ++index;
        }
    }

    uint32_t ComponentDataRegistry::AllocateSlot()
    {
        if (!m_FreeSlots.empty())
        {
            const uint32_t index = m_FreeSlots.back();
            m_FreeSlots.pop_back();
            return index;
        }

        const uint32_t index = static_cast<uint32_t>(m_Slots.size());
        m_Slots.push_back(EntitySlot{});
        return index;
    }

    Math::Matrix4x4 ComponentDataRegistry::MakeRegistryWorldMatrix(const Math::Transform& transform)
    {
        Math::Matrix4x4 matrix = transform.ToMatrix();
        matrix.m30 = transform.position.x;
        matrix.m31 = transform.position.y;
        matrix.m32 = transform.position.z;
        matrix.m03 = 0.0f;
        matrix.m13 = 0.0f;
        matrix.m23 = 0.0f;
        matrix.m33 = 1.0f;
        return matrix;
    }

} // namespace NorvesLib::Core
