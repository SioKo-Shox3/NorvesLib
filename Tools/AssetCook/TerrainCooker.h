// メートル単位の高さ格子を NVTRN v0 に焼き込む API。
#pragma once
#include "HeightfieldImport.h"
#include "TerrainMasks.h"
#include "Asset/CookedTerrainFormat.h"

namespace NorvesLib::Tools::AssetCook
{
    // NRM0: 世界座標（y が上）の単位法線の x・z を RG8 に入れ、y は sqrt(max(0, 1 - x² - z²)) で復元する。
    inline constexpr Core::Asset::AssetPackageFourCC TerrainNormalLayerFourCC = Core::Asset::MakeAssetPackageFourCC('N', 'R', 'M', '0');
    // R=凹み度、空なら書かない。
    inline constexpr Core::Asset::AssetPackageFourCC TerrainPuddleLayerFourCC = Core::Asset::CookedTerrainFormatV0::PuddleLayerFourCC;
    inline constexpr const char* TerrainHeightSemantic = "height";
    inline constexpr const char* TerrainNormalSemantic = "normal.world.xz";

    struct TerrainBuildSettings
    {
        double CellSizeM = 1.0;    // 有限で正。
        uint32_t TileCells = 128;  // 2 の冪で 8..1024。grid の幅・高さは TileCells*N+1。
        const TerrainMaskFields* Masks = nullptr; // null なら HGT0・NRM0 だけ。
    };

    struct TerrainBuildResult
    {
        Core::Container::VariableArray<uint8_t> NvtrnBytes;
        uint32_t TileCountX = 0;
        uint32_t TileCountZ = 0;
        uint32_t LodCount = 0;
        float HeightMinM = 0.0f;
        float HeightMaxM = 0.0f;
    };

    // 地図全体で法線・量子化を行い、タイルへ切って LOD 誤差・AABB を求め、NVTRN v0 を書く。同じ入力なら同じバイト。
    [[nodiscard]] bool BuildCookedTerrain(const TerrainHeightGrid& grid, const TerrainBuildSettings& settings,
                                          TerrainBuildResult& outResult, Core::Container::AnsiString& outError);
} // namespace NorvesLib::Tools::AssetCook
