// レベルのマーカーを距離と頂点予算に収めて描画する。
#pragma once

#include "Math/Vector3.h"

#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Rendering
{
    class DebugDrawQueue;
}

namespace Game::LevelEdit
{
    struct LevelMarkerDebugDrawSettings
    {
        float MaxDistance = 300.0f;
        uint32_t MaxVertices = 32768;
        bool bDrawTerritories = true;
        bool bDrawPoints = true;
        bool bDrawTrails = true;
        uint64_t SelectedObjectId = 0;
    };

    struct LevelMarkerDebugDrawStats
    {
        uint32_t Drawn = 0;
        uint32_t CulledByDistance = 0;
        uint32_t CulledByBudget = 0;
        uint32_t VerticesAdded = 0;
    };

    /// マーカーをカメラに近い順に、設定とキューの残りの小さい方の頂点数まで描く。
    /// ワールド変換は呼び出し前に World::UpdateWorldTransforms で更新しておく。
    LevelMarkerDebugDrawStats DrawLevelMarkers(const NorvesLib::Core::World& world,
                                               const NorvesLib::Math::Vector3& cameraPosition,
                                               const LevelMarkerDebugDrawSettings& settings,
                                               NorvesLib::Core::Rendering::DebugDrawQueue& queue);
} // namespace Game::LevelEdit
