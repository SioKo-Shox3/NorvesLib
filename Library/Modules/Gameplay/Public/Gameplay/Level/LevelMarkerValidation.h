// レベルのマーカー配置の問題を列挙する。
#pragma once

#include "Container/String.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class Entity;
    class World;
}

namespace NorvesLib::Modules::Gameplay::Level
{
    enum class ELevelMarkerIssue : uint8_t
    {
        EmptyKey,
        DuplicateKey,
        MissingLevelMarker,
        TerritoryWithoutZone,
        OrphanTrailPoint
    };

    struct LevelMarkerIssue
    {
        ELevelMarkerIssue Kind;
        Core::Entity* Target = nullptr;
        Core::Container::String Key;
    };

    /// 問題が無ければ true。outIssues は最初に空にし、ルートとその子孫をすべて検証する。
    bool ValidateLevelMarkers(const Core::World& world, Core::Container::VariableArray<LevelMarkerIssue>& outIssues);
} // namespace NorvesLib::Modules::Gameplay::Level
