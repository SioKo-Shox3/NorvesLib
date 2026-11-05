#include "Gameplay/Shisha/ShishaComponent.h"

#include "Object/World.h"

namespace Game::Gameplay
{
    IMPLEMENT_CLASS(ShishaComponent, NorvesLib::Modules::Gameplay::Enemy::EnemyComponent)

    ShishaComponent::ShishaComponent()
    {
        DefinitionPath = "Gameplay/enemies/shisha_human_01.enemy.json";
    }

    void ShishaComponent::Initialize()
    {
        Super::Initialize();
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        if (world && !world->FindComponentTick(StaticClass()))
        {
            world->RegisterComponentTick(StaticClass(), EnemyComponent::MakeTickRegistration());
        }
    }
} // namespace Game::Gameplay
