#pragma once

#include "AI/Navigation/NavTile.h"
#include "AI/Ports/INavTerrainSource.h"
#include "Container/Span.h"

namespace NorvesLib::Modules::AI
{
    /// 静的な障害物の足跡を、XZ 平面上で Y 軸まわりに回転した箱で表す。
    struct NavObstacleFootprint
    {
        float CenterX = 0.0f;
        float CenterZ = 0.0f;
        float HalfExtentX = 0.0f; // ローカル X 方向の半幅（m）
        float HalfExtentZ = 0.0f; // ローカル Z 方向の半幅（m）
        float YawRadians = 0.0f;  // 正の回転でローカル Z 軸がワールド X 軸へ向く。
    };

    struct NavBakeSettings
    {
        float MaxWalkableSlopeDegrees = 40.0f;
        float CliffStepHeight = 1.0f;     // 隣のセルとの高低差の上限（m）
        float ShallowWaterDepth = 0.05f;  // 浅瀬とみなす最小水深（m）
        float DeepWaterDepth = 0.6f;      // 深水とみなす最小水深（m）
        uint32_t ClearanceApronCells = 16; // 距離計算に含めるタイル外のセル数（各辺で最大 256）
    };

    struct NavBakeInput
    {
        NavGridDesc Grid;
        NavTileCoord Tile;
        const INavTerrainSource* Terrain = nullptr; // 省略時は高さ 0 m の平地を使う。
        Core::Container::Span<const NavObstacleFootprint> StaticObstacles;
        NavBakeSettings Settings;
    };

    class NavGridBaker
    {
    public:
        /// 地形と静的な足跡からタイルを焼く。失敗時は出力を保持する。
        static bool Bake(const NavBakeInput& input, NavTile& outTile);
    };
} // namespace NorvesLib::Modules::AI
