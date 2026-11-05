#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering::Terrain
{
    class TerrainHeightField;

    /// スプラットの層の名前と、消費者が引く表面の名前の対応
    struct TerrainSurfaceLayer
    {
        Container::AnsiString LayerName;
        Container::AnsiString SurfaceName;
    };

    /// 地形の材質の記述。添字 = SurfaceId（スプラットの層の番号）
    struct TerrainMaterialDesc
    {
        Container::VariableArray<TerrainSurfaceLayer> Layers;
        /// 表面の名前。範囲外は空
        Container::AnsiStringView GetSurfaceName(uint8_t surfaceId) const;
    };

    /// 層の名前から仮の表面の名前を引く。未知の名前は Soil
    Container::AnsiStringView GetProvisionalSurfaceName(Container::AnsiStringView layerName);
    /// 高さ場の層の名前から、仮の表で地形の材質の記述を作る
    TerrainMaterialDesc MakeProvisionalTerrainMaterialDesc(const TerrainHeightField& field);
} // namespace NorvesLib::Core::Rendering::Terrain
