// 持ち場の配置データ。形と中心は同じ Entity の World::EncounterZoneComponent を正本とする。
#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 持ち場の配置データ。形と中心は同じ Entity の World::EncounterZoneComponent を正本とする。
    class TerritoryMarkerComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(TerritoryMarkerComponent, Core::Component::Component)

    public:
        TerritoryMarkerComponent();

        PROPERTY(Core::Container::String, OwnerKey)
        PROPERTY(float, EngageRadius)
    };
} // namespace NorvesLib::Modules::Gameplay::Level
