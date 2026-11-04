#include "Camera/LockOnTarget.h"
#include "Component/SkinnedMeshComponent.h"
#include "Math/Transform.h"
#include "Object/Entity.h"
#include "Object/World.h"
namespace NorvesLib::Core::Camera
{
    namespace
    {
        const Entity* ResolveEntity(const World& world, uint64_t entityId)
        {
            const auto* entity = entityId ? world.FindEntityByObjectId(entityId) : nullptr;
            return entity && !entity->IsPendingDestroy() && entity->IsActive() ? entity : nullptr;
        }
    }

    EntityLockOnTarget::EntityLockOnTarget(uint64_t entityId, float radius)
        : m_EntityId(entityId), m_Radius(radius)
    {
    }

    bool EntityLockOnTarget::TryResolve(const World& world, LockOnPoint& out) const
    {
        const auto* entity = ResolveEntity(world, m_EntityId);
        if (!entity)
            return false;

        LockOnPoint next;
        next.Position = entity->GetRenderWorldTransform().position;
        next.Radius = m_Radius;
        out = next;
        return true;
    }

    JointLockOnTarget::JointLockOnTarget(uint64_t entityId, uint32_t jointIndex, float radius)
        : m_EntityId(entityId), m_JointIndex(jointIndex), m_Radius(radius)
    {
    }

    bool JointLockOnTarget::TryResolve(const World& world, LockOnPoint& out) const
    {
        const auto* entity = ResolveEntity(world, m_EntityId);
        if (!entity)
            return false;

        const auto* mesh = entity->GetComponent<Component::SkinnedMeshComponent>();
        Math::Transform transform;
        if (!mesh || !mesh->TryGetJointWorldTransform(m_JointIndex, transform))
            return false;

        LockOnPoint next;
        next.Position = transform.position + entity->GetRenderWorldTransform().position -
                        entity->GetWorldTransform().position;
        next.Radius = m_Radius;
        out = next;
        return true;
    }
} // namespace NorvesLib::Core::Camera
