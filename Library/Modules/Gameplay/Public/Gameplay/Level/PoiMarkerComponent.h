// 見どころの識別子と発見条件を保持する。
#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 見どころの識別子と発見条件を保持する。
    class PoiMarkerComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(PoiMarkerComponent, Core::Component::Component)

    public:
        PoiMarkerComponent();

        PROPERTY(Core::Container::String, PoiId)
        PROPERTY(Core::Container::String, Category)
        PROPERTY(Core::Container::String, NameKey)
        PROPERTY(bool, bShownOnMap)
        PROPERTY(float, DiscoverRadius)
    };
} // namespace NorvesLib::Modules::Gameplay::Level
