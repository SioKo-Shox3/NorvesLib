#pragma once

#ifndef NORVES_ENABLE_COMPONENT_DATA_REGISTRY
#define NORVES_ENABLE_COMPONENT_DATA_REGISTRY 0
#endif

#include "Container/Containers.h"
#include "Object/EntityHandle.h"
#include "Rendering/SceneProxy.h"
#include "Math/Transform.h"
#include "Math/Matrix4x4.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class World;

    struct EntityTransformData
    {
        EntityHandle Handle;
        uint64_t ObjectId = 0;
        uint64_t TransformVersion = 0;
        Math::Transform WorldTransform;
        Math::Matrix4x4 WorldMatrix = Math::Matrix4x4::Identity;
    };

    struct MeshComponentData
    {
        EntityHandle Handle;
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;
        uint64_t TransformVersion = 0;
        Rendering::MeshProxy Proxy;
        Rendering::BoundingSphere WorldBounds;
        Rendering::RenderLayer LayerMask = Rendering::RenderLayer::Default;
        bool bVisible = false;
    };

    struct MegaGeometryComponentData
    {
        EntityHandle Handle;
        uint64_t ObjectId = 0;
        uint64_t ComponentId = 0;
        uint64_t TransformVersion = 0;
        Rendering::MegaGeometryProxy Proxy;
        Rendering::BoundingSphere WorldBounds;
        Rendering::RenderLayer LayerMask = Rendering::RenderLayer::Default;
        bool bVisible = false;
    };

    class ComponentDataRegistry
    {
    public:
        bool IsAvailable() const;
        bool IsEnabled() const;
        bool SetEnabled(bool bEnabled);

        EntityHandle RegisterEntity(World& world, Entity& entity);
        void UnregisterEntity(Entity& entity);
        void UnregisterWorld(World& world);

        EntityHandle GetHandle(const Entity& entity) const;
        Entity* ResolveEntity(EntityHandle handle) const;

        void BeginFrameCapture();
        void PublishTransform(Entity& entity);
        void PublishMeshProxy(Entity& entity, const Rendering::MeshProxy& proxy);
        void PublishMegaGeometryProxy(Entity& entity, const Rendering::MegaGeometryProxy& proxy);

        Container::Span<const EntityTransformData> GetTransformData() const;
        Container::Span<const MeshComponentData> GetMeshData() const;
        Container::Span<const MegaGeometryComponentData> GetMegaGeometryData() const;

    private:
        struct EntitySlot
        {
            World* WorldPtr = nullptr;
            Entity* EntityPtr = nullptr;
            uint32_t Generation = 1;
            bool bOccupied = false;
        };

        bool IsHandleAlive(EntityHandle handle) const;
        void RemovePublishedData(EntityHandle handle);
        void RemoveTransformData(EntityHandle handle);
        void RemoveMeshData(EntityHandle handle);
        void RemoveMegaGeometryData(EntityHandle handle);
        uint32_t AllocateSlot();
        static Math::Matrix4x4 MakeRegistryWorldMatrix(const Math::Transform& transform);

        bool m_bEnabled = false;
        Container::VariableArray<EntitySlot> m_Slots;
        Container::VariableArray<uint32_t> m_FreeSlots;
        Container::UnorderedMap<const Entity*, uint32_t> m_EntityToSlot;
        Container::VariableArray<EntityTransformData> m_TransformData;
        Container::VariableArray<MeshComponentData> m_MeshData;
        Container::VariableArray<MegaGeometryComponentData> m_MegaGeometryData;
    };

} // namespace NorvesLib::Core
