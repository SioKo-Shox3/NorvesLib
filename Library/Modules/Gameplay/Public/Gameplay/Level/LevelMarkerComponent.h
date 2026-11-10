// 全マーカーに共通する安定キーと接地設定を保持する。
#pragma once

#include "Component/Component.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 全マーカーに共通する安定キーと接地設定を保持する。
    class LevelMarkerComponent : public Core::Component::Component
    {
        REFLECTION_CLASS(LevelMarkerComponent, Core::Component::Component)

    public:
        LevelMarkerComponent();

        PROPERTY(Core::Container::String, Key)
        PROPERTY(Core::Container::String, Tag)
        PROPERTY(bool, bSnapToGround)
        PROPERTY(float, GroundOffset)
    };
} // namespace NorvesLib::Modules::Gameplay::Level
