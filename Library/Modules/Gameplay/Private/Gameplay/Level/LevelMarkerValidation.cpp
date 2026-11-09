// Entity 階層全体のマーカーのキーと配置条件を検証する。
#include "Gameplay/Level/LevelMarkerValidation.h"

#include "Gameplay/Level/AnimalSpawnPointComponent.h"
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Object/Entity.h"
#include "Object/World.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    namespace
    {
        void ValidateEntity(Core::Entity& entity, Core::Container::VariableArray<Core::Container::String>& keys,
                            Core::Container::VariableArray<LevelMarkerIssue>& outIssues)
        {
            const auto* marker = entity.GetComponent<LevelMarkerComponent>();
            Core::Container::String key;
            if (marker != nullptr)
            {
                key = marker->getKey();
                if (key.empty())
                {
                    outIssues.push_back({ELevelMarkerIssue::EmptyKey, &entity, key});
                }
                for (const auto& previous : keys)
                {
                    if (previous == key)
                    {
                        outIssues.push_back({ELevelMarkerIssue::DuplicateKey, &entity, key});
                        break;
                    }
                }
                keys.push_back(key);
            }

            const bool bTerritory = entity.GetComponent<TerritoryMarkerComponent>() != nullptr;
            const bool bTypedMarker = bTerritory || entity.GetComponent<RestPointComponent>() != nullptr ||
                entity.GetComponent<PoiMarkerComponent>() != nullptr ||
                entity.GetComponent<AnimalSpawnPointComponent>() != nullptr ||
                entity.GetComponent<ScentTrailComponent>() != nullptr;
            if (bTypedMarker && marker == nullptr)
            {
                outIssues.push_back({ELevelMarkerIssue::MissingLevelMarker, &entity, key});
            }
            if (bTerritory && entity.GetComponent<World::EncounterZoneComponent>() == nullptr)
            {
                outIssues.push_back({ELevelMarkerIssue::TerritoryWithoutZone, &entity, key});
            }
            if (entity.GetComponent<ScentTrailPointComponent>() != nullptr)
            {
                const Core::Entity* parent = entity.GetParentEntity();
                if (parent == nullptr || parent->GetComponent<ScentTrailComponent>() == nullptr)
                {
                    outIssues.push_back({ELevelMarkerIssue::OrphanTrailPoint, &entity, key});
                }
            }

            for (Core::Entity* child : entity.GetChildEntities())
            {
                ValidateEntity(*child, keys, outIssues);
            }
        }
    }

    bool ValidateLevelMarkers(const Core::World& world, Core::Container::VariableArray<LevelMarkerIssue>& outIssues)
    {
        outIssues.clear();
        Core::Container::VariableArray<Core::Container::String> keys;
        for (Core::Entity* root : world.GetRootEntities())
        {
            ValidateEntity(*root, keys, outIssues);
        }
        return outIssues.empty();
    }
} // namespace NorvesLib::Modules::Gameplay::Level
