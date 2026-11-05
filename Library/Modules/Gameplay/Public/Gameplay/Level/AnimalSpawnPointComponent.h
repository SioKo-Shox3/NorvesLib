// 動物の出現点の配置データを保持する。
#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 動物の出現点の配置データを保持する。
    class AnimalSpawnPointComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(AnimalSpawnPointComponent, Core::Component::Component)

    public:
        AnimalSpawnPointComponent();

        PROPERTY(Core::Container::String, SpeciesId)
        PROPERTY(uint32_t, Count)
        PROPERTY(float, Radius)
        PROPERTY(Core::Container::String, BehaviorId)
    };
} // namespace NorvesLib::Modules::Gameplay::Level
