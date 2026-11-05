// 寝床とファストトラベル点の配置データ。近接の操作は World::InteractableComponent が受け持つ。
#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 寝床とファストトラベル点の配置データ。近接の操作は World::InteractableComponent が受け持つ。
    class RestPointComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(RestPointComponent, Core::Component::Component)

    public:
        RestPointComponent();

        PROPERTY(Core::Container::String, PointId)
        PROPERTY(Core::Container::String, NameKey)
        PROPERTY(bool, bFastTravel)
        PROPERTY(bool, bSavePoint)
        PROPERTY(bool, bAdvancesTime)
        /// 到着時の向き（度）。
        PROPERTY(float, ArrivalYaw)
    };
} // namespace NorvesLib::Modules::Gameplay::Level
