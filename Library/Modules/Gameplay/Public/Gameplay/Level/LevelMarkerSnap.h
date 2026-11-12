// マーカーと痕跡の親子関係を保つ接地対象の収集と吸着を提供する。
#pragma once

#include "Scene/LevelSnap.h"

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Modules::Gameplay::Level
{
    /// 両出力を空にし、World のルートと全子孫から bSnapToGround のマーカー所有 Entity を集める。
    /// 痕跡の点は直上の親が ScentTrail と吸着する LevelMarker を持つ場合に集め、親の GroundOffset を使う。
    /// 各列は親を子より先に並べ、マーカー列の吸着と変換更新の後に痕跡列を吸着する。
    void CollectLevelSnapTargets(const Core::World& world,
        Core::Container::VariableArray<Core::Scene::LevelSnap::LevelSnapTarget>& outMarkerTargets,
        Core::Container::VariableArray<Core::Scene::LevelSnap::LevelSnapTarget>& outTrailPointTargets);

    /// 変換を更新し、マーカー、痕跡の順で ReSnapAll に渡して件数を合計する。終了時も変換を更新する。
    Core::Scene::LevelSnap::LevelSnapStats ReSnapAllLevelMarkers(Core::World& world,
        const Core::Scene::SceneQuery& query, float maxDrop);

    /// 変換を更新して対象を集め、FindSnapDeviations に渡す。出力は最初に空にする。
    bool FindLevelMarkerSnapDeviations(Core::World& world, const Core::Scene::SceneQuery& query,
        float maxDrop, float tolerance,
        Core::Container::VariableArray<Core::Scene::LevelSnap::LevelSnapDeviation>& outDeviations);
} // namespace NorvesLib::Modules::Gameplay::Level
