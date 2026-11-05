#include "Physics/TerrainColliderBinding.h"

#include "Component/TerrainComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"

namespace NorvesLib::Modules::Physics
{
    EPhysicsResult AttachTerrainCollider(Core::World& world, Core::Component::TerrainComponent& terrain,
                                         Core::Entity*& outColliderEntity)
    {
        outColliderEntity = nullptr;
        Core::Entity* owner = terrain.GetOwner();
        const auto heightField = terrain.GetHeightField();
        if (owner == nullptr || Core::Container::IsNull(heightField))
        {
            return EPhysicsResult::InvalidState;
        }

        Core::Entity* colliderEntity = world.SpawnEntity<Core::Entity>(owner);
        if (colliderEntity == nullptr)
        {
            return EPhysicsResult::InvalidState;
        }
        colliderEntity->SetPosition(0.0f, 0.0f, 0.0f);
        ColliderComponent* collider = world.CreateComponent<ColliderComponent>(colliderEntity);
        if (collider == nullptr)
        {
            world.RemoveEntity(colliderEntity);
            return EPhysicsResult::InvalidState;
        }

        const EPhysicsResult result = collider->SetHeightField(heightField);
        if (result != EPhysicsResult::Success)
        {
            world.RemoveEntity(colliderEntity);
            return result;
        }

        outColliderEntity = colliderEntity;
        return EPhysicsResult::Success;
    }
} // namespace NorvesLib::Modules::Physics
