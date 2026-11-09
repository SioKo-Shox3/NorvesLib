// 既存の Gameplay Component とレベルの配置データのクラスを一括登録する。
#include "Gameplay/Level/GameplayComponentClasses.h"

#include "Gameplay/Combat/HealthComponent.h"
#include "Gameplay/Combat/HitboxComponent.h"
#include "Gameplay/Combat/HurtboxSetComponent.h"
#include "Gameplay/Enemy/EnemyComponent.h"
#include "Gameplay/World/ZoneSensorComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Gameplay/World/InteractableComponent.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/AnimalSpawnPointComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Object/IClass.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    void RegisterGameplayComponentClasses()
    {
        Core::ClassRegistry::Get().RegisterClass(Combat::HealthComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(Combat::HitboxComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(Combat::HurtboxSetComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(Enemy::EnemyComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::ZoneSensorComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::EncounterZoneComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(World::InteractableComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(LevelMarkerComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(TerritoryMarkerComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(RestPointComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(PoiMarkerComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(AnimalSpawnPointComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(ScentTrailComponent::StaticClass());
        Core::ClassRegistry::Get().RegisterClass(ScentTrailPointComponent::StaticClass());
    }
} // namespace NorvesLib::Modules::Gameplay::Level
