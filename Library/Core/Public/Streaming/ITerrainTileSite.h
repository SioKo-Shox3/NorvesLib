// ストリーミングする地形の区画を配置・解放する相手の口を定義する。
#pragma once

#include "Streaming/StreamingTypes.h"

namespace NorvesLib::Core::Streaming
{
    struct TerrainTileCoordinate
    {
        uint32_t X = 0;
        uint32_t Z = 0;
        bool operator==(const TerrainTileCoordinate&) const = default;
    };

    enum class ETerrainTileSiteState : uint8_t
    {
        Loading,
        Ready,
        Failed
    };

    // すべて GameThread で呼ぶ。
    class ITerrainTileSite
    {
    public:
        virtual ~ITerrainTileSite() = default;
        // 高さ場を読み終えたかを返す。
        virtual ETerrainTileSiteState GetSiteState() const = 0;
        // 区画の中心の XZ が半開区間に入る区画を Z→X の順で返す。Ready 以外は空。
        virtual void CollectTilesInBounds(const StreamingBounds& bounds,
                                         Container::VariableArray<TerrainTileCoordinate>& outTiles) const = 0;
        // TerrainSystem::SetTileResident の番号。範囲外は Rendering::Terrain::kInvalidTerrainTileId
        // （Rendering/Terrain/TerrainHeightField.h で定義）を返す。
        virtual uint32_t GetTileId(const TerrainTileCoordinate& tile) const = 0;
        // 置けたかを返す。既に置いてあれば true。
        virtual bool PlaceTile(const TerrainTileCoordinate& tile) = 0;
        // 置いていなければ何もしない。
        virtual void RemoveTile(const TerrainTileCoordinate& tile) = 0;
        // 区画が置かれているかを返す。
        virtual bool IsTilePlaced(const TerrainTileCoordinate& tile) const = 0;
    };
} // namespace NorvesLib::Core::Streaming
