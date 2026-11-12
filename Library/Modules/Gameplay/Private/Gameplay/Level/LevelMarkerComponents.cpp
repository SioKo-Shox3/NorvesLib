// マーカーのクラス情報と配置データの既定値を定義する。
#include "Gameplay/Level/LevelMarkerComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/AnimalSpawnPointComponent.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    IMPLEMENT_CLASS(LevelMarkerComponent, Core::Component::Component)

    LevelMarkerComponent::LevelMarkerComponent()
    {
        Key = "";
        Tag = "";
        bSnapToGround = true;
        GroundOffset = 0.0f;
    }

    IMPLEMENT_CLASS(TerritoryMarkerComponent, Core::Component::Component)

    TerritoryMarkerComponent::TerritoryMarkerComponent()
    {
        OwnerKey = "";
        EngageRadius = 10.0f;
    }

    IMPLEMENT_CLASS(RestPointComponent, Core::Component::Component)

    RestPointComponent::RestPointComponent()
    {
        PointId = "";
        NameKey = "";
        bFastTravel = true;
        bSavePoint = false;
        bAdvancesTime = false;
        ArrivalYaw = 0.0f;
    }

    IMPLEMENT_CLASS(PoiMarkerComponent, Core::Component::Component)

    PoiMarkerComponent::PoiMarkerComponent()
    {
        PoiId = "";
        Category = "";
        NameKey = "";
        bShownOnMap = true;
        DiscoverRadius = 15.0f;
    }

    IMPLEMENT_CLASS(AnimalSpawnPointComponent, Core::Component::Component)

    AnimalSpawnPointComponent::AnimalSpawnPointComponent()
    {
        SpeciesId = "";
        Count = 1;
        Radius = 5.0f;
        BehaviorId = "";
    }

} // namespace NorvesLib::Modules::Gameplay::Level
